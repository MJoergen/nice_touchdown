/***************************************************************
 * solve.c: solves positions of TouchDown, on the host: whether the
 * side to move wins or loses, with best play.
 *
 *   solve [OPTIONS] SIZE|POSITION...
 *
 * Each argument is a size, such as 6x4, for its start, or a position
 * as text, such as "OOOO/..../..../XXXX x" (engine_set_position).
 *
 * The search. A game has no draws, and every move takes a pawn one
 * rank forward, so a game is short and has no cycles: a position is a
 * WIN if one of its moves leads to a LOSS for the opponent, and else a
 * LOSS (also when it has no move). The search is depth-first, and
 * stops at the first move that wins. At each position it tries first
 * a move to the last rank, then a move to a position that has no
 * move, or that the table has as a LOSS, and then the others, the one
 * whose position the evaluation (engine_eval) has worst for the
 * opponent first (--order plain: in the order of engine_moves).
 *
 * The table. Every position searched is kept in a table of --hash
 * megabytes, with its value, so that a position that comes again by
 * other moves is not searched again. A position is kept as the side to
 * move sees it, with its board turned around when that is Black, so
 * that White to move and Black to move share their entries; and as
 * the smaller of it and its mirror, files a and h swapped, which has
 * the same value. When a bucket of the table is full, the entry whose
 * search took the fewest nodes goes.
 *
 * --moves prints the value of each move at the root, which solves
 * each of them.
 *
 * --check FILE solves every position of touchdown_db -d's dump of the
 * 4x4 board, as tests/tablebase.c reads it, with one table, and
 * compares them with touchdown_db's values.
 *
 * It prints the value and the nodes, and then the time on a line of
 * its own, which tests/solve.grep leaves out.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/engine/engine.h"

static int order_eval = 1;
static int show_moves;
static long hash_mb = 1024;
static const char *check_file;

static int files, ranks;
static uint64_t nodes;

/***************************************************************
 * The table
 ***************************************************************/

// An entry: the pawns of the side to move, as it sees the board, its
// ranks 0 to ranks - 2 by files bits each, in a, with in the top 4
// bits the log2 of the nodes its search took; and the pawns of the
// opponent, ranks 1 to ranks - 1, in b, with the value in bit 63, 1 a
// WIN. Both have at most 5 * 12 = 60 bits. a is 0 for an empty entry:
// a position that is kept has a pawn of the side to move.
typedef struct
{
    uint64_t a, b;
} entry_t;

#define KEY_MASK ((UINT64_C(1) << 60) - 1)
#define BUCKET 4

static entry_t *table;
static uint64_t n_buckets;
static uint64_t stored, replaced;
static uint16_t mirror[1 << ENGINE_MAX_FILES];

static void table_init(void)
{
    n_buckets = 1;
    while (n_buckets * 2 * BUCKET * sizeof(entry_t) <= (uint64_t) hash_mb << 20)
        n_buckets *= 2;
    table = calloc(n_buckets * BUCKET, sizeof(entry_t));
    if (!table)
    {
        perror("solve");
        exit(1);
    }
}

static void table_clear(void)
{
    memset(table, 0, n_buckets * BUCKET * sizeof(entry_t));
    stored = replaced = 0;
}

static void set_size(int f, int r)
{
    files = f;
    ranks = r;
    for (int x = 0; x < 1 << f; x++)
    {
        int m = 0;
        for (int i = 0; i < f; i++)
            if (x >> i & 1)
                m |= 1 << (f - 1 - i);
        mirror[x] = (uint16_t) m;
    }
}

// The pawns of side c on rank r (from White's side), files bits.
static uint32_t rank_bits(const position_t *p, int c, int r)
{
    return p->pawns[c][r / 2] >> (16 * (r % 2)) & 0xfff;
}

// The key of p, which must have a move: the smaller of it and its
// mirror.
static entry_t key(const position_t *p)
{
    int me = p->side, you = !me;
    uint64_t a = 0, b = 0, ma = 0, mb = 0;
    for (int r = 0; r < ranks; r++)
    {
        // r is the rank as the side to move sees it.
        int rr = me == WHITE ? r : ranks - 1 - r;
        uint32_t mine = rank_bits(p, me, rr), yours = rank_bits(p, you, rr);
        if (r < ranks - 1)
        {
            a |= (uint64_t) mine << (r * files);
            ma |= (uint64_t) mirror[mine] << (r * files);
        }
        if (r > 0)
        {
            b |= (uint64_t) yours << ((r - 1) * files);
            mb |= (uint64_t) mirror[yours] << ((r - 1) * files);
        }
    }
    entry_t k = {a, b};
    if (ma < a || (ma == a && mb < b))
        k.a = ma, k.b = mb;
    return k;
}

static entry_t *bucket(entry_t k)
{
    uint64_t h = k.a * UINT64_C(0x9e3779b97f4a7c15) ^ k.b * UINT64_C(0xc2b2ae3d27d4eb4f);
    h ^= h >> 31;
    h *= UINT64_C(0xbf58476d1ce4e5b9);
    h ^= h >> 29;
    return table + (h & (n_buckets - 1)) * BUCKET;
}

// The value of k in the table: 1 a WIN, 0 a LOSS, -1 not in it.
static int probe(entry_t k)
{
    entry_t *e = bucket(k);
    for (int i = 0; i < BUCKET; i++)
        if ((e[i].a & KEY_MASK) == k.a && (e[i].b & KEY_MASK) == k.b && e[i].a)
            return (int) (e[i].b >> 63);
    return -1;
}

static void store(entry_t k, int win, uint64_t work)
{
    int w = 0;
    while (w < 15 && work >> (w + 1))
        w++;
    entry_t *e = bucket(k), *victim = e;
    for (int i = 0; i < BUCKET; i++)
    {
        if (!e[i].a || ((e[i].a & KEY_MASK) == k.a && (e[i].b & KEY_MASK) == k.b))
        {
            victim = e + i;
            break;
        }
        if (e[i].a >> 60 < victim->a >> 60)
            victim = e + i;
    }
    if (victim->a && ((victim->a & KEY_MASK) != k.a || (victim->b & KEY_MASK) != k.b))
        replaced++;
    else if (!victim->a)
        stored++;
    victim->a = k.a | (uint64_t) w << 60;
    victim->b = k.b | (uint64_t) win << 63;
}

/***************************************************************
 * The search
 ***************************************************************/

// Whether the side to move of p wins. p must have n moves, list.
static int solve_moves(const position_t *p, const move_t *list, int n)
{
    uint64_t start = nodes++;
    entry_t k = key(p);
    int v = probe(k);
    if (v >= 0)
        return v;

    // A move to the last rank wins; they come first.
    int last = p->side == WHITE ? ranks - 1 : 0;
    if (SQUARE_RANK(MOVE_TO(list[0])) == last)
    {
        store(k, 1, 1);
        return 1;
    }

    // The positions after the moves; one that has no move, or that
    // the table has as a LOSS, wins.
    position_t q[ENGINE_MAX_MOVES];
    move_t l2[ENGINE_MAX_MOVES][ENGINE_MAX_MOVES];
    int n2[ENGINE_MAX_MOVES], score[ENGINE_MAX_MOVES], idx[ENGINE_MAX_MOVES];
    for (int i = 0; i < n; i++)
    {
        engine_make(p, list[i], &q[i]);
        n2[i] = engine_moves(&q[i], l2[i]);
        if (n2[i] == 0 || probe(key(&q[i])) == 0)
        {
            store(k, 1, nodes - start);
            return 1;
        }
        score[i] = order_eval ? engine_eval(&q[i]) : 0;
        idx[i] = i;
    }

    // The others, the worst for the opponent first (an insertion
    // sort, stable, so that ties keep the order of engine_moves).
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && score[idx[j]] < score[idx[j - 1]]; j--)
        {
            int t = idx[j];
            idx[j] = idx[j - 1];
            idx[j - 1] = t;
        }
    int win = 0;
    for (int i = 0; i < n && !win; i++)
        win = !solve_moves(&q[idx[i]], l2[idx[i]], n2[idx[i]]);
    store(k, win, nodes - start);
    return win;
}

// Whether the side to move of p wins. A position set up with a pawn
// of the side to move on its last rank is a WIN already; the table
// has no room for it.
static int solve(const position_t *p)
{
    if (rank_bits(p, p->side, p->side == WHITE ? ranks - 1 : 0))
        return 1;
    move_t list[ENGINE_MAX_MOVES];
    int n = engine_moves(p, list);
    return n ? solve_moves(p, list, n) : 0;
}

/***************************************************************
 * The front
 ***************************************************************/

static double seconds(clock_t t0)
{
    return (double) (clock() - t0) / CLOCKS_PER_SEC;
}

static void solve_arg(const char *arg)
{
    position_t p;
    int f, r;
    char c;
    if (sscanf(arg, "%dx%d%c", &f, &r, &c) == 2)
    {
        if (engine_set_size(f, r))
        {
            fprintf(stderr, "solve: no size %s\n", arg);
            exit(2);
        }
        engine_start(&p);
    }
    else if (engine_set_position(&p, arg))
    {
        fprintf(stderr, "solve: not a position: %s\n", arg);
        exit(2);
    }
    set_size(engine_files(), engine_ranks());
    table_clear();
    nodes = 0;

    char text[ENGINE_POSITION_STR_SIZE], ms[8];
    clock_t t0 = clock();
    int win = solve(&p);
    printf("%dx%d %s: %s, %llu nodes, %llu positions kept, %llu replaced\n", files, ranks,
           engine_position_str(&p, text), win ? "WIN" : "LOSS", (unsigned long long) nodes,
           (unsigned long long) stored, (unsigned long long) replaced);
    printf("  time %.2f s\n", seconds(t0));
    fflush(stdout);

    if (show_moves)
    {
        move_t list[ENGINE_MAX_MOVES];
        int n = engine_moves(&p, list);
        for (int i = 0; i < n; i++)
        {
            position_t q;
            engine_make(&p, list[i], &q);
            printf("  %s %s\n", engine_move_str(list[i], ms), solve(&q) ? "loses" : "wins");
        }
        fflush(stdout);
    }
}

// Solves the positions of touchdown_db -d's dump, and compares them.
static int check(const char *file)
{
    FILE *fp = fopen(file, "r");
    if (!fp)
    {
        perror(file);
        return 1;
    }
    char line[256], t[24];
    long n = 0, wins = 0, losses = 0, diffs = 0;
    engine_set_size(4, 4);
    set_size(4, 4);
    table_clear();
    nodes = 0;
    while (fgets(line, sizeof line, fp))
    {
        char *s = strchr(line, ':');
        if (!s)
            continue;
        s += 2;
        for (int r = 0; r < 4; r++, s += 5)
        {
            memcpy(t + r * 5, s, 4);
            t[r * 5 + 4] = r < 3 ? '/' : ' ';
        }
        t[20] = 'x';
        t[21] = '\0';
        position_t p;
        if (engine_set_position(&p, t))
            continue;
        int db = strstr(line, "WIN") != NULL;
        int win = solve(&p);
        n++;
        wins += db;
        losses += !db;
        if (win != db && diffs++ < 10)
            printf("%s: touchdown_db has %s, solve %s\n", t, db ? "WIN" : "LOSS", win ? "WIN" : "LOSS");
    }
    fclose(fp);
    printf("solve 4x4: %ld positions, %ld wins, %ld losses, %ld differences\n", n, wins, losses, diffs);
    return diffs != 0;
}

static void usage(void)
{
    fprintf(stderr,
            "usage: solve [OPTIONS] SIZE|POSITION...\n"
            "  --hash MB      the size of the table (1024)\n"
            "  --order eval   try the moves the evaluation likes first (the default)\n"
            "  --order plain  in the order of the move generator\n"
            "  --moves        the value of each move at the root\n"
            "  --check FILE   solve touchdown_db -d's dump of the 4x4 board, and compare\n");
    exit(2);
}

int main(int argc, char **argv)
{
    int i = 1;
    for (; i < argc && argv[i][0] == '-' && argv[i][1] == '-'; i++)
    {
        const char *a = argv[i];
        if (!strcmp(a, "--moves"))
            show_moves = 1;
        else if (i + 1 < argc && !strcmp(a, "--hash"))
            hash_mb = atol(argv[++i]);
        else if (i + 1 < argc && !strcmp(a, "--check"))
            check_file = argv[++i];
        else if (i + 1 < argc && !strcmp(a, "--order"))
        {
            const char *v = argv[++i];
            if (!strcmp(v, "eval"))
                order_eval = 1;
            else if (!strcmp(v, "plain"))
                order_eval = 0;
            else
                usage();
        }
        else
            usage();
    }
    if (hash_mb < 1 || (i == argc && !check_file))
        usage();
    table_init();
    int status = check_file ? check(check_file) : 0;
    for (; i < argc; i++)
        solve_arg(argv[i]);
    return status;
}
