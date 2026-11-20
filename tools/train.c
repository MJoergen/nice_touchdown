/***************************************************************
 * train.c: learns the weights of an N-tuple evaluation by self-play,
 * with TD(λ), and checks it against databases of positions and their
 * values (tools/db.h). On
 * the host only; an experiment, not yet part of the engine.
 *
 *   train [OPTIONS]
 *
 * The evaluation. A position is seen from the side to move, with its
 * ranks turned around when that is Black, so that "my" pawns always
 * move up. A window of W files by H ranks (--window WxH, 3x2 at first)
 * is placed at every place on the board, and each square of it is
 * empty, mine, or the opponent's: one of 3^(W*H) states. Each state
 * has a weight, and the evaluation is the sum of the weights of all
 * the windows: a logit, whose sigmoid is the probability that the side
 * to move wins. (There are no draws.)
 *
 * The windows of all files share one table, so that what is learned on
 * one width holds on every width. A table is kept for each number of
 * ranks and each rank of the window, as the distance to the goal
 * matters most, and for windows inside the board, at an edge, and at
 * both edges (a window as wide as the board). A window at the right
 * edge is mirrored and counted as one at the left edge.
 *
 * --material adds a weight for each difference of the pawns, from
 * -6 to 6, shared by all sizes.
 *
 * The learning (TD). Each game is on a size at random of --sizes
 * (such as 4x4,8x6, or all). It starts with 0 to --opening random
 * moves, which are not learned from. Then the side to move makes each
 * legal move, and plays the one whose position is worst for the
 * opponent, or with probability --epsilon a random move. After the
 * game, the value of each position moves towards its λ-return, by the
 * rate --alpha: 1 for the position before the last move (the side to
 * move wins), and for the others (1 - λ) times one less the value of
 * the next position, plus λ times one less its λ-return. The update
 * is that of the logistic loss: alpha times (target - value) added to
 * the weight of each window.
 *
 * The check (--db FILE, more than one: touchdown_db -d's dump of the
 * 4x4 board, or solve --dump's of a size). For each position of each
 * database where the game is not over, whether the sign of the
 * evaluation is the database's value,
 * and, in its WINs, whether the move of the evaluation, as the
 * training plays, keeps the win. Every --report games, and at the end.
 *
 * --fit EPOCHS learns the databases themselves instead, by the same
 * update with their values as the targets: how well the windows can
 * know those boards at all.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/engine/engine.h"
#include "../src/engine/eval_weights.h"
#include "db.h"

/***************************************************************
 * The options
 ***************************************************************/
static long   total_games = 1000000;
static double alpha = 0.01;
static double lambda = 0.8;
static double epsilon = 0.4;
static int    opening_moves = 4;
static long   report_every = 100000;
static uint64_t seed0 = 1;
static int    win_w = 3, win_h = 2;
static int    material;
static int    fit_epochs;
static const char *save_file;
static const char *load_file;
static const char *header_file;
static long   check_games;

// The sizes of the games: files and ranks.
static int sizes[(ENGINE_MAX_FILES + 1) * (ENGINE_MAX_RANKS + 1)][2];
static int n_sizes;

/***************************************************************
 * Random numbers: xorshift64*.
 ***************************************************************/
static uint64_t rng = 1;

static uint32_t rnd(void)
{
    rng ^= rng >> 12;
    rng ^= rng << 25;
    rng ^= rng >> 27;
    return (uint32_t) ((rng * 0x2545F4914F6CDD1Dull) >> 32);
}

static double rnd_unit(void)
{
    return rnd() / 4294967296.0;
}

/***************************************************************
 * The evaluation
 ***************************************************************/
#define MAX_WINDOW_SQUARES 9
#define MAX_FEATURES (ENGINE_MAX_FILES * ENGINE_MAX_RANKS + 1)
#define N_CLASSES 3 // inside, at an edge, at both edges

static int n_states;                          // 3^(W*H)
static int slot_base[ENGINE_MAX_RANKS + 1];   // the first table of each number of ranks
static int material_base;                     // the index of the material weights
static int n_weights;
static float *w;

static uint32_t tern[1 << MAX_WINDOW_SQUARES]; // bits to base 3, digits 0 and 1
static uint32_t rev[1 << 4];                  // W bits in the reverse order

static void init_eval(void)
{
    if (win_w < 1 || win_w > 4 || win_h < 1 || win_h > ENGINE_MIN_RANKS
        || win_w * win_h > MAX_WINDOW_SQUARES)
    {
        fprintf(stderr, "train: a window of %dx%d is not supported\n", win_w, win_h);
        exit(2);
    }
    n_states = 1;
    for (int i = 0; i < win_w * win_h; i++)
        n_states *= 3;
    for (uint32_t b = 0; b < (1u << (win_w * win_h)); b++)
    {
        uint32_t t = 0, d = 1;
        for (int i = 0; i < win_w * win_h; i++, d *= 3)
            if (b >> i & 1)
                t += d;
        tern[b] = t;
    }
    for (uint32_t b = 0; b < (1u << win_w); b++)
    {
        rev[b] = 0;
        for (int i = 0; i < win_w; i++)
            if (b >> i & 1)
                rev[b] |= 1u << (win_w - 1 - i);
    }
    int slots = 0;
    for (int r = ENGINE_MIN_RANKS; r <= ENGINE_MAX_RANKS; r++)
    {
        slot_base[r] = slots;
        slots += r - win_h + 1;
    }
    material_base = slots * N_CLASSES * n_states;
    n_weights = material_base + (material ? 13 : 0);
    w = calloc((size_t) n_weights, sizeof *w);
    if (!w)
    {
        perror("train");
        exit(1);
    }
}

// The ranks of side's pawns, as 12-bit rows, from the view of the side
// to move: row 0 is its first rank.
static void get_rows(const position_t *p, int side, uint32_t *row)
{
    int ranks = engine_ranks();
    for (int r = 0; r < ranks; r++)
    {
        int a = p->side == WHITE ? r : ranks - 1 - r;
        row[r] = p->pawns[side][a >> 1] >> (16 * (a & 1)) & 0xfff;
    }
}

// The indexes of the weights of p, from the side to move. Returns how
// many.
static int features(const position_t *p, int *idx)
{
    int files = engine_files(), ranks = engine_ranks();
    uint32_t me[ENGINE_MAX_RANKS], them[ENGINE_MAX_RANKS];
    uint32_t mask = (1u << win_w) - 1;
    get_rows(p, p->side, me);
    get_rows(p, p->side ^ 1, them);
    int n = 0;
    for (int r = 0; r + win_h <= ranks; r++)
    {
        int slot = slot_base[ranks] + r;
        for (int f = 0; f + win_w <= files; f++)
        {
            int left = f == 0, right = f + win_w == files;
            int cls = left && right ? 2 : left || right ? 1 : 0;
            int mirror = right && !left;
            uint32_t a = 0, b = 0;
            for (int k = 0; k < win_h; k++)
            {
                uint32_t x = me[r + k] >> f & mask, y = them[r + k] >> f & mask;
                if (mirror)
                    x = rev[x], y = rev[y];
                a |= x << (k * win_w);
                b |= y << (k * win_w);
            }
            idx[n++] = (slot * N_CLASSES + cls) * n_states + (int) (tern[a] + 2 * tern[b]);
        }
    }
    if (material)
    {
        int d = 0;
        for (int r = 0; r < ranks; r++)
            d += __builtin_popcount(me[r]) - __builtin_popcount(them[r]);
        d = d < -6 ? -6 : d > 6 ? 6 : d;
        idx[n++] = material_base + d + 6;
    }
    return n;
}

static float logit(const position_t *p)
{
    int idx[MAX_FEATURES];
    int n = features(p, idx);
    float s = 0;
    for (int i = 0; i < n; i++)
        s += w[idx[i]];
    return s;
}

static double sigmoid(double x)
{
    return 1.0 / (1.0 + exp(-x));
}

// The value of p for the side to move: the probability it wins, 0 if
// the game is over (it has lost).
static double value(const position_t *p)
{
    move_t list[ENGINE_MAX_MOVES];
    if (engine_moves(p, list) == 0)
        return 0;
    return sigmoid(logit(p));
}

// Moves the value of p towards target.
static void learn(const position_t *p, double target)
{
    int idx[MAX_FEATURES];
    int n = features(p, idx);
    float s = 0;
    for (int i = 0; i < n; i++)
        s += w[idx[i]];
    float d = (float) (alpha * (target - sigmoid(s)));
    for (int i = 0; i < n; i++)
        w[idx[i]] += d;
}

// The move of p (of n in list) whose position is worst for the
// opponent: a move that ends the game at once, or the least logit.
static move_t best_move(const position_t *p, const move_t *list, int n)
{
    move_t best = list[0];
    float best_v = INFINITY;
    move_t l2[ENGINE_MAX_MOVES];
    for (int i = 0; i < n; i++)
    {
        position_t q;
        engine_make(p, list[i], &q);
        if (engine_moves(&q, l2) == 0)
            return list[i];
        float v = logit(&q);
        if (v < best_v)
            best_v = v, best = list[i];
    }
    return best;
}

/***************************************************************
 * Self-play
 ***************************************************************/
static void play_game(void)
{
    static position_t states[ENGINE_MAX_GAME];
    static double target[ENGINE_MAX_GAME];
    move_t list[ENGINE_MAX_MOVES];
    position_t p;
    int n;

    int s = (int) (rnd() % (uint32_t) n_sizes);
    engine_set_size(sizes[s][0], sizes[s][1]);
    do
    {
        engine_start(&p);
        int k = (int) (rnd() % (uint32_t) (opening_moves + 1));
        for (int i = 0; i < k && (n = engine_moves(&p, list)) > 0; i++)
            engine_make(&p, list[rnd() % (uint32_t) n], &p);
    } while (engine_moves(&p, list) == 0);

    int t = 0;
    while ((n = engine_moves(&p, list)) > 0)
    {
        states[t++] = p;
        move_t m = rnd_unit() < epsilon ? list[rnd() % (uint32_t) n] : best_move(&p, list, n);
        engine_make(&p, m, &p);
    }

    // The λ-returns, from the last position back: the side to move of
    // the last wins, as the game is over after its move.
    target[t - 1] = 1;
    for (int i = t - 2; i >= 0; i--)
        target[i] = (1 - lambda) * (1 - value(&states[i + 1])) + lambda * (1 - target[i + 1]);
    for (int i = 0; i < t; i++)
        learn(&states[i], target[i]);
}

/***************************************************************
 * The databases (--db): touchdown_db's of the 4x4 board, and solve
 * --dump's of other sizes (tools/db.h)
 ***************************************************************/
#define MAX_DBS 16

static const char *db_files[MAX_DBS];
static db_t dbs[MAX_DBS];
static unsigned char *db_reach[MAX_DBS]; // Whether each position can come from the start
static int n_dbs;

// Marks the positions of database d that can come from p: p, and its
// mirror if the database has both.
static void reach(int d, const position_t *p)
{
    const db_t *db = &dbs[d];
    if (db_over(p))
        return;
    db_entry_t e = {db_key(p), 0};
    db_entry_t *f = bsearch(&e, db->keys, (size_t) db->n_keys, sizeof e, db_compare);
    if (!f || db_reach[d][f->index])
        return;
    db_entry_t *g = f;
    while (g > db->keys && !db_compare(g - 1, f))
        g--;
    for (; g < db->keys + db->n_keys && !db_compare(g, f); g++)
        db_reach[d][g->index] = 1;
    move_t list[ENGINE_MAX_MOVES];
    int n = engine_moves(p, list);
    for (int k = 0; k < n; k++)
    {
        position_t q;
        engine_make(p, list[k], &q);
        reach(d, &q);
    }
}

static void load_dbs(void)
{
    for (int d = 0; d < n_dbs; d++)
    {
        db_load(&dbs[d], db_files[d]);
        db_reach[d] = calloc((size_t) dbs[d].n, 1);
        if (!db_reach[d])
        {
            perror("train");
            exit(1);
        }
        position_t p;
        engine_start(&p);
        reach(d, &p);
    }
}

// Counts of the check: the positions, those the evaluation has right,
// the LOSSes, the WINs, and the WINs whose move keeps the win.
typedef struct
{
    long n, right, losses, wins, kept;
} count_t;

static void count(count_t *c, int win, double v, int kept)
{
    c->n++;
    c->right += (v > 0.5) == win;
    c->losses += !win;
    c->wins += win;
    c->kept += kept;
}

static void print_count(const char *what, const count_t *c)
{
    long major = c->losses > c->wins ? c->losses : c->wins;
    printf("  %-22s %8ld: value %6.2f%% (majority %6.2f%%)", what, c->n, 100.0 * c->right / c->n,
           100.0 * major / c->n);
    if (c->wins)
        printf(", move keeps the win %6.2f%%", 100.0 * c->kept / c->wins);
    printf("\n");
}

// Prints, for each database, how often the evaluation has its value,
// and how often its move keeps a win: of all its positions where the
// game is not over, and, if it has others (as touchdown_db's), of
// those that can come from the start; and of each, those not won at
// once by a move to the last rank.
static void check_db(const char *what)
{
    move_t list[ENGINE_MAX_MOVES];
    for (int d = 0; d < n_dbs; d++)
    {
        const db_t *db = &dbs[d];
        count_t all = {0}, all_hard = {0}, start = {0}, start_hard = {0};
        double loss = 0;
        engine_set_size(db->files, db->ranks);
        for (long i = 0; i < db->n; i++)
        {
            const position_t *p = &db->pos[i];
            int k = engine_moves(p, list);
            if (k == 0)
                continue;
            int win = db->win[i];
            double v = sigmoid(logit(p));
            loss -= log(win ? v + 1e-12 : 1 - v + 1e-12);
            int kept = 0;
            if (win)
            {
                position_t q;
                engine_make(p, best_move(p, list, k), &q);
                long j = db_lookup(db, &q);
                kept = engine_moves(&q, list) == 0 || (j >= 0 && !db->win[j]);
            }
            // The moves to the last rank come first.
            int last = p->side == WHITE ? db->ranks - 1 : 0;
            int hard = SQUARE_RANK(MOVE_TO(list[0])) != last;
            count(&all, win, v, kept);
            if (hard)
                count(&all_hard, win, v, kept);
            if (db_reach[d][i])
            {
                count(&start, win, v, kept);
                if (hard)
                    count(&start_hard, win, v, kept);
            }
        }
        printf("%s, %dx%d: log loss %.4f\n", what, db->files, db->ranks, loss / all.n);
        print_count("all", &all);
        print_count("all, not won at once", &all_hard);
        if (start.n < all.n)
        {
            print_count("from the start", &start);
            print_count("  not won at once", &start_hard);
        }
    }
    fflush(stdout);
}

// Learns the databases: their values as the targets, each database in
// turn, in a random order.
static void fit_db(void)
{
    move_t list[ENGINE_MAX_MOVES];
    long *order[MAX_DBS], n[MAX_DBS];
    for (int d = 0; d < n_dbs; d++)
    {
        order[d] = malloc((size_t) dbs[d].n * sizeof *order[d]);
        if (!order[d])
        {
            perror("train");
            exit(1);
        }
        n[d] = 0;
        engine_set_size(dbs[d].files, dbs[d].ranks);
        for (long i = 0; i < dbs[d].n; i++)
            if (engine_moves(&dbs[d].pos[i], list) > 0)
                order[d][n[d]++] = i;
    }
    for (int e = 1; e <= fit_epochs; e++)
    {
        for (int d = 0; d < n_dbs; d++)
        {
            long *o = order[d];
            engine_set_size(dbs[d].files, dbs[d].ranks);
            for (long i = n[d] - 1; i > 0; i--)
            {
                long j = (long) (rnd() % (uint32_t) (i + 1));
                long x = o[i];
                o[i] = o[j];
                o[j] = x;
            }
            for (long i = 0; i < n[d]; i++)
                learn(&dbs[d].pos[o[i]], dbs[d].win[o[i]]);
        }
        char what[32];
        snprintf(what, sizeof what, "epoch %d", e);
        check_db(what);
    }
    for (int d = 0; d < n_dbs; d++)
        free(order[d]);
}

/***************************************************************
 * The engine's weights
 ***************************************************************/

// Writes the weights as eval_weights.h, in hundredths, with the
// command line that made them.
static int write_header(int argc, char **argv)
{
    if (win_w != 3 || win_h != 2 || material)
    {
        fprintf(stderr, "train: the engine has windows of 3x2, and no material\n");
        return 2;
    }
    FILE *fp = fopen(header_file, "w");
    if (!fp)
    {
        perror(header_file);
        return 1;
    }
    fprintf(fp, "/***************************************************************\n"
                " * eval_weights.h: the weights of the evaluation (eval.c), in\n"
                " * hundredths of a logit. Written by tools/train.c (make weights):\n"
                " *\n"
                " *  ");
    for (int i = 0; i < argc; i++)
        fprintf(fp, " %s", argv[i]);
    fprintf(fp, "\n"
                " *\n"
                " * Do not edit.\n"
                " ***************************************************************/\n\n"
                "#ifndef EVAL_WEIGHTS_H\n#define EVAL_WEIGHTS_H\n\n"
                "#include <stdint.h>\n\n"
                "#define EVAL_STATES %d\n#define EVAL_SLOTS %d\n\n",
            n_states, material_base / (N_CLASSES * n_states));

    // The states of 6 bits of a pair of ranks, interleaved, as eval.c
    // has them: bit 2j the square of file j of the lower rank, bit
    // 2j + 1 that of the upper rank. Their state is tern[] of the
    // bits rank by rank, as features() has them; and of the window
    // mirrored, with file j as 2 - j. Then the same, twice, for the
    // opponent's pawns.
    static const char *names[4] = {"tern6", "tern6_2", "tern6_mirror", "tern6_mirror_2"};
    for (int k = 0; k < 4; k++)
    {
        fprintf(fp, "static const uint16_t %s[64] = {", names[k]);
        for (uint32_t b = 0; b < 64; b++)
        {
            uint32_t x = 0;
            for (int j = 0; j < 3; j++)
            {
                int file = k >= 2 ? 2 - j : j;
                x |= (b >> (2 * j) & 1) << file | (b >> (2 * j + 1) & 1) << (3 + file);
            }
            fprintf(fp, "%s%u,", b % 16 ? " " : "\n    ", tern[x] * (k & 1 ? 2 : 1));
        }
        fprintf(fp, "\n};\n\n");
    }

    fprintf(fp, "static const int16_t eval_weights[EVAL_SLOTS][2][EVAL_STATES] = {\n");
    int slots = material_base / (N_CLASSES * n_states);
    for (int slot = 0; slot < slots; slot++)
    {
        fprintf(fp, "    {\n");
        for (int cls = 0; cls < 2; cls++)
        {
            fprintf(fp, "        { // slot %d, %s", slot, cls ? "at an edge" : "inside");
            for (int i = 0; i < n_states; i++)
            {
                double x = round(w[(slot * N_CLASSES + cls) * n_states + i] * 100);
                x = x > 32767 ? 32767 : x < -32767 ? -32767 : x;
                fprintf(fp, "%s%d,", i % 12 ? " " : "\n            ", (int) x);
            }
            fprintf(fp, "\n        },\n");
        }
        fprintf(fp, "    },\n");
    }
    fprintf(fp, "};\n\n#endif // EVAL_WEIGHTS_H\n");
    if (fclose(fp))
    {
        perror(header_file);
        return 1;
    }
    return 0;
}

// Checks engine_eval against the windows of features(), with the
// weights of eval_weights.h, in the positions of games at random on
// every size. Returns 0 if it has them all.
static int check_engine(void)
{
    if (win_w != 3 || win_h != 2 || material
        || material_base != EVAL_SLOTS * N_CLASSES * EVAL_STATES)
    {
        fprintf(stderr, "train: the engine has windows of 3x2, and no material\n");
        return 2;
    }
    for (int slot = 0; slot < EVAL_SLOTS; slot++)
        for (int cls = 0; cls < 2; cls++)
            for (int i = 0; i < EVAL_STATES; i++)
                w[(slot * N_CLASSES + cls) * n_states + i] = eval_weights[slot][cls][i];
    long positions = 0, errors = 0;
    for (long g = 0; g < check_games; g++)
    {
        int s = (int) (g % n_sizes);
        engine_set_size(sizes[s][0], sizes[s][1]);
        position_t p;
        move_t list[ENGINE_MAX_MOVES];
        int n;
        engine_start(&p);
        while ((n = engine_moves(&p, list)) > 0)
        {
            long want = (long) logit(&p);
            want = want > ENGINE_EVAL_MAX ? ENGINE_EVAL_MAX
                   : want < -ENGINE_EVAL_MAX ? -ENGINE_EVAL_MAX : want;
            int got = engine_eval(&p);
            positions++;
            if (got != want)
            {
                char text[ENGINE_POSITION_STR_SIZE];
                if (errors++ < 10)
                    printf("%s: engine_eval %d, the windows %ld\n", engine_position_str(&p, text),
                           got, want);
            }
            engine_make(&p, list[rnd() % (uint32_t) n], &p);
        }
    }
    printf("check-engine: %ld games, %ld positions, %ld differences\n", check_games, positions,
           errors);
    return errors != 0;
}

/***************************************************************
 * The options, and the main loop
 ***************************************************************/
static void parse_sizes(const char *s)
{
    n_sizes = 0;
    if (!strcmp(s, "all"))
    {
        for (int f = ENGINE_MIN_FILES; f <= ENGINE_MAX_FILES; f++)
            for (int r = ENGINE_MIN_RANKS; r <= ENGINE_MAX_RANKS; r++)
                sizes[n_sizes][0] = f, sizes[n_sizes++][1] = r;
        return;
    }
    while (*s)
    {
        int f, r, len;
        if (sscanf(s, "%dx%d%n", &f, &r, &len) != 2 || f < ENGINE_MIN_FILES
            || f > ENGINE_MAX_FILES || r < ENGINE_MIN_RANKS || r > ENGINE_MAX_RANKS)
        {
            fprintf(stderr, "train: bad --sizes at \"%s\"\n", s);
            exit(2);
        }
        sizes[n_sizes][0] = f, sizes[n_sizes++][1] = r;
        s += len;
        if (*s == ',')
            s++;
    }
}

static void usage(void)
{
    fprintf(stderr,
            "usage: train [OPTIONS]\n"
            "  --games N      self-play games (1000000)\n"
            "  --sizes LIST   sizes of the games, such as 4x4,8x6, or all (all)\n"
            "  --alpha A      learning rate (0.01)\n"
            "  --lambda L     TD(lambda) (0.8)\n"
            "  --epsilon E    probability of a random move (0.4)\n"
            "  --opening N    0 to N random moves at the start (4)\n"
            "  --window WxH   the window of the N-tuples (3x2)\n"
            "  --material     a weight for each difference of the pawns\n"
            "  --seed S       the seed (1)\n"
            "  --report N     check against the database every N games (100000)\n"
            "  --db FILE      a database to check against: touchdown_db -d's dump of the 4x4\n"
            "                 board, or solve --dump's of a size (tools/db.h); more than one\n"
            "  --fit EPOCHS   learn the databases themselves instead of self-play\n"
            "  --save FILE    write the weights, as floats\n"
            "  --load FILE    start from the weights of --save\n"
            "  --header FILE  write the weights, as the engine's eval_weights.h\n"
            "  --check-engine GAMES  check engine_eval against eval_weights.h\n");
    exit(2);
}

int main(int argc, char **argv)
{
    parse_sizes("all");
    for (int i = 1; i < argc; i++)
    {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--material"))
        {
            material = 1;
            continue;
        }
        if (!v)
            usage();
        i++;
        if (!strcmp(a, "--games"))
            total_games = atol(v);
        else if (!strcmp(a, "--sizes"))
            parse_sizes(v);
        else if (!strcmp(a, "--alpha"))
            alpha = atof(v);
        else if (!strcmp(a, "--lambda"))
            lambda = atof(v);
        else if (!strcmp(a, "--epsilon"))
            epsilon = atof(v);
        else if (!strcmp(a, "--opening"))
            opening_moves = atoi(v);
        else if (!strcmp(a, "--window"))
        {
            if (sscanf(v, "%dx%d", &win_w, &win_h) != 2)
                usage();
        }
        else if (!strcmp(a, "--seed"))
            seed0 = strtoull(v, NULL, 0);
        else if (!strcmp(a, "--report"))
            report_every = atol(v);
        else if (!strcmp(a, "--db"))
        {
            if (n_dbs == MAX_DBS)
                usage();
            db_files[n_dbs++] = v;
        }
        else if (!strcmp(a, "--fit"))
            fit_epochs = atoi(v);
        else if (!strcmp(a, "--save"))
            save_file = v;
        else if (!strcmp(a, "--load"))
            load_file = v;
        else if (!strcmp(a, "--header"))
            header_file = v;
        else if (!strcmp(a, "--check-engine"))
            check_games = atol(v);
        else
            usage();
    }
    rng = seed0 * 0x9E3779B97F4A7C15ull | 1;
    init_eval();
    printf("window %dx%d%s: %d weights\n", win_w, win_h, material ? " and material" : "",
           n_weights);
    if (check_games)
        return check_engine();
    if (load_file)
    {
        FILE *fp = fopen(load_file, "rb");
        if (!fp || fread(w, sizeof *w, (size_t) n_weights, fp) != (size_t) n_weights)
        {
            fprintf(stderr, "train: cannot read %d weights from %s\n", n_weights, load_file);
            return 1;
        }
        fclose(fp);
    }
    if (n_dbs)
        load_dbs();

    if (fit_epochs)
    {
        if (!n_dbs)
            usage();
        fit_db();
    }
    else
    {
        clock_t start = clock();
        for (long g = 1; g <= total_games; g++)
        {
            play_game();
            if (g % report_every == 0 || g == total_games)
            {
                char what[64];
                snprintf(what, sizeof what, "%ld games, %.0f s", g,
                         (double) (clock() - start) / CLOCKS_PER_SEC);
                if (n_dbs)
                    check_db(what);
                else
                    printf("%s\n", what);
                fflush(stdout);
            }
        }
    }

    if (save_file)
    {
        FILE *fp = fopen(save_file, "wb");
        if (!fp || fwrite(w, sizeof *w, (size_t) n_weights, fp) != (size_t) n_weights)
        {
            perror(save_file);
            return 1;
        }
        fclose(fp);
    }
    if (header_file)
        return write_header(argc, argv);
    return 0;
}
