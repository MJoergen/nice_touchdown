/***************************************************************
 * tablebase.c: checks the search against touchdown_db, which has the
 * value of every position of the 4x4 board.
 *
 *   touchdown_db -d FILE | tablebase DEPTH
 *
 * touchdown_db -d writes a line for each position with at most 4
 * pawns a side, from the side to move, X:
 *
 *       1d : .... OO.. .X.. X...  :    ... WIN
 *
 * its ranks from X's last down to its first, as the engine's text of
 * a position has them. For each it searches the position to DEPTH,
 * which must reach the end of every game, and checks that
 *
 *   - its value is touchdown_db's, WIN or LOSS, and always proved;
 *   - in a WIN, the move it plays leads to a position that
 *     touchdown_db has as a LOSS for the opponent;
 *   - in a "Win in N", that position searches as "Loss in N - 1".
 *
 * It prints the counts and the first differences, and returns 0 if
 * there are none.
 *
 *   touchdown_db -d FILE | tablebase eval
 *
 * checks the evaluation alone, engine_eval without a search, in each
 * position that has a move. It prints how often its sign is
 * touchdown_db's value, and how often the move it would play, the one
 * whose position is worst for the opponent (or a move to the last
 * rank), keeps a WIN: of all of them, of those not won at once by a
 * move to the last rank, of those that can come from the start, and
 * of those of them not won at once. Then, of those not won at once,
 * how often a WIN is for each tenth of the evaluation's probability of
 * a win. It is a measure, not a check: it returns 0, and its expected
 * output changes with the weights.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/engine/engine.h"

#define MAX_POSITIONS 1000000
#ifndef ERRORS_SHOWN
#define ERRORS_SHOWN 10
#endif

// A position, from the side to move, as 32 bits: the side's pawns in
// the high 16 and the opponent's in the low, square rank * 4 + file,
// with Black's board turned around (as it moves down).
typedef struct
{
    uint32_t key;
    int win;
} entry_t;

static entry_t table[MAX_POSITIONS];
static int n_table;
static int errors;

static uint32_t key(const position_t *p)
{
    uint32_t k = 0;
    for (int r = 0; r < 4; r++)
        for (int f = 0; f < 4; f++)
        {
            int pawn = engine_pawn(p, f, r);
            if (pawn < 0)
                continue;
            int sq = p->side == WHITE ? r * 4 + f : (3 - r) * 4 + (3 - f);
            k |= 1u << (sq + (pawn == p->side ? 16 : 0));
        }
    return k;
}

static int compare(const void *a, const void *b)
{
    uint32_t x = ((const entry_t *) a)->key, y = ((const entry_t *) b)->key;
    return x < y ? -1 : x > y;
}

// touchdown_db's value of p: 1 a WIN, 0 a LOSS, -1 not in it.
static int lookup(const position_t *p)
{
    entry_t e = {key(p), 0};
    entry_t *f = bsearch(&e, table, (size_t) n_table, sizeof e, compare);
    return f ? f->win : -1;
}

static void error(const char *text, const char *what)
{
    if (errors < ERRORS_SHOWN)
        printf("%s: %s\n", text, what);
    errors++;
}

/***************************************************************
 * tablebase eval: the evaluation alone
 ***************************************************************/

// Whether each entry of the table can come from the start.
static unsigned char reached[MAX_POSITIONS];

static void reach(const position_t *p)
{
    entry_t e = {key(p), 0};
    entry_t *f = bsearch(&e, table, (size_t) n_table, sizeof e, compare);
    if (!f || reached[f - table])
        return;
    reached[f - table] = 1;
    move_t list[ENGINE_MAX_MOVES];
    int n = engine_moves(p, list);
    for (int k = 0; k < n; k++)
    {
        position_t q;
        engine_make(p, list[k], &q);
        reach(&q);
    }
}

// The move of the evaluation, as tools/train.c plays it: one that
// ends the game, or else the one whose position is worst for the
// opponent, the first of them.
static move_t eval_move(const position_t *p, const move_t *list, int n)
{
    move_t best = list[0], l2[ENGINE_MAX_MOVES];
    int best_v = ENGINE_EVAL_MAX + 1;
    for (int i = 0; i < n; i++)
    {
        position_t q;
        engine_make(p, list[i], &q);
        if (engine_moves(&q, l2) == 0)
            return list[i];
        int v = engine_eval(&q);
        if (v < best_v)
            best_v = v, best = list[i];
    }
    return best;
}

typedef struct
{
    long n, right, losses, wins, kept;
} count_t;

static void count(count_t *c, int win, int v, int kept)
{
    c->n++;
    c->right += (v > 0) == win;
    c->losses += !win;
    c->wins += win;
    c->kept += kept;
}

static void print_count(const char *what, const count_t *c)
{
    long major = c->losses > c->wins ? c->losses : c->wins;
    printf("  %-20s %7ld: right %6.2f%% (majority %6.2f%%), move keeps the win %6.2f%%\n", what, c->n,
           100.0 * c->right / c->n, 100.0 * major / c->n, 100.0 * c->kept / c->wins);
}

static int eval_db(void)
{
    position_t p;
    move_t list[ENGINE_MAX_MOVES];
    count_t all = {0}, all_hard = {0}, start = {0}, start_hard = {0};
    long bin_n[10] = {0}, bin_wins[10] = {0};
    double bin_p[10] = {0};

    engine_set_size(4, 4);
    engine_start(&p);
    reach(&p);
    for (int i = 0; i < n_table; i++)
    {
        // The position again, from its key: the side to move, X, on
        // ranks 1 and up.
        char t[24];
        for (int r = 0; r < 4; r++)
        {
            for (int f = 0; f < 4; f++)
            {
                int sq = (3 - r) * 4 + f;
                t[r * 5 + f] = table[i].key >> (sq + 16) & 1 ? 'X' : table[i].key >> sq & 1 ? 'O' : '.';
            }
            t[r * 5 + 4] = r < 3 ? '/' : ' ';
        }
        t[20] = 'x';
        t[21] = '\0';
        engine_set_position(&p, t);
        int n = engine_moves(&p, list);
        if (n == 0)
            continue;
        int win = table[i].win, v = engine_eval(&p), kept = 0;
        if (win)
        {
            position_t q;
            engine_make(&p, eval_move(&p, list, n), &q);
            kept = engine_moves(&q, list) == 0 || lookup(&q) == 0;
            n = engine_moves(&p, list);
        }
        int hard = SQUARE_RANK(MOVE_TO(list[0])) < 3; // The moves to the last rank come first
        count(&all, win, v, kept);
        if (hard)
            count(&all_hard, win, v, kept);
        if (reached[i])
        {
            count(&start, win, v, kept);
            if (hard)
                count(&start_hard, win, v, kept);
        }
        if (hard)
        {
            double prob = 1 / (1 + exp(-v / 100.0));
            int b = prob >= 1 ? 9 : (int) (prob * 10);
            bin_n[b]++;
            bin_wins[b] += win;
            bin_p[b] += prob;
        }
    }

    printf("evaldb 4x4: the sign of engine_eval against touchdown_db, without a search\n");
    print_count("all", &all);
    print_count("not won at once", &all_hard);
    print_count("from the start", &start);
    print_count("  not won at once", &start_hard);
    printf("  not won at once, by the evaluation's probability of a win:\n");
    for (int b = 0; b < 10; b++)
        if (bin_n[b])
            printf("  %3d-%3d%% %7ld: probability %5.1f%%, WIN %5.1f%%\n", b * 10, b * 10 + 10, bin_n[b],
                   100.0 * bin_p[b] / bin_n[b], 100.0 * bin_wins[b] / bin_n[b]);
    return 0;
}

int main(int argc, char **argv)
{
    int eval = argc > 1 && !strcmp(argv[1], "eval");
    int depth = argc > 1 && !eval ? atoi(argv[1]) : 24;
    static char line[256];
    static char texts[MAX_POSITIONS][24];

    // The positions and their values.
    while (fgets(line, sizeof line, stdin))
    {
        char *s = strchr(line, ':');
        if (!s || n_table == MAX_POSITIONS)
            continue;
        char *t = texts[n_table];
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
        {
            error(t, "not a position");
            continue;
        }
        table[n_table].key = key(&p);
        table[n_table].win = strstr(line, "WIN") != NULL;
        n_table++;
    }
    // The texts in the order of the dump, and the table by its keys.
    static int win_of[MAX_POSITIONS];
    for (int i = 0; i < n_table; i++)
        win_of[i] = table[i].win;
    qsort(table, (size_t) n_table, sizeof table[0], compare);
    if (eval)
        return errors ? 1 : eval_db();

    int wins = 0, losses = 0;
    uint64_t nodes = 0;
    for (int i = 0; i < n_table; i++)
    {
        position_t p, q;
        engine_search_t r, r2;
        engine_set_position(&p, texts[i]);
        engine_search(&p, depth, 0, &r);
        nodes += r.nodes;
        wins += win_of[i];
        losses += !win_of[i];
        if (!ENGINE_PROVED(r.score))
            error(texts[i], "not proved at this depth");
        else if ((r.score > 0) != win_of[i])
            error(texts[i], win_of[i] ? "a WIN, searched as a loss" : "a LOSS, searched as a win");
        else if (r.score > 0)
        {
            engine_make(&p, r.move, &q);
            if (lookup(&q) != 0 && !engine_touchdown(&q))
                error(texts[i], "the move does not lead to a LOSS for the opponent");
            engine_search(&q, depth, 0, &r2);
            if (r2.score != -(r.score + 1))
                error(texts[i], "the opponent's loss is not one ply shorter");
        }
    }

    printf("tablebase 4x4: %d positions, %d wins, %d losses, %d differences\n",
           n_table, wins, losses, errors);
    fprintf(stderr, "%llu nodes\n", (unsigned long long) nodes);
    return errors ? 1 : 0;
}
