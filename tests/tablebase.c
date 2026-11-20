/***************************************************************
 * tablebase.c: checks the search, and measures the evaluation,
 * against a database of positions and their values (tools/db.h):
 * touchdown_db's of the 4x4 board, or solve --dump's of a size.
 *
 *   tablebase DEPTH < DATABASE
 *
 * For each position it searches to DEPTH, which must reach the end of
 * every game, and checks that
 *
 *   - its value is the database's, WIN or LOSS, and always proved;
 *   - in a WIN, the move it plays leads to a position that the
 *     database has as a LOSS for the opponent, or that is over;
 *   - in a "Win in N", that position searches as "Loss in N - 1".
 *
 * It prints the counts and the first differences, and returns 0 if
 * there are none.
 *
 *   tablebase eval < DATABASE
 *
 * measures the evaluation alone, engine_eval without a search, in each
 * position that has a move. It prints how often its sign is the
 * database's value, and how often the move it would play, the one
 * whose position is worst for the opponent (or a move to the last
 * rank), keeps a WIN: of all of them, of those not won at once by a
 * move to the last rank, and, if the database has others too (as
 * touchdown_db's), of those that can come from the start, and of
 * those of them not won at once. Then, of those not won at once,
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
#include "../tools/db.h"

#ifndef ERRORS_SHOWN
#define ERRORS_SHOWN 10
#endif

static db_t db;
static int errors;

// The database's value of p: 1 a WIN, 0 a LOSS, -1 not in it.
static int lookup(const position_t *p)
{
    long i = db_lookup(&db, p);
    return i < 0 ? -1 : db.win[i];
}

static void error(const position_t *p, const char *what)
{
    char text[ENGINE_POSITION_STR_SIZE];
    if (errors < ERRORS_SHOWN)
        printf("%s: %s\n", engine_position_str(p, text), what);
    errors++;
}

/***************************************************************
 * tablebase eval: the evaluation alone
 ***************************************************************/

// Whether each position of the database can come from the start.
static unsigned char *reached;

static void reach(const position_t *p)
{
    if (db_over(p))
        return;
    db_entry_t e = {db_key(p), 0};
    db_entry_t *f = bsearch(&e, db.keys, (size_t) db.n_keys, sizeof e, db_compare);
    if (!f || reached[f->index])
        return;
    // The positions of the key: p, and its mirror if the database has
    // both.
    db_entry_t *g = f;
    while (g > db.keys && !db_compare(g - 1, f))
        g--;
    for (; g < db.keys + db.n_keys && !db_compare(g, f); g++)
        reached[g->index] = 1;
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

static int eval_db(const char *source)
{
    position_t p;
    move_t list[ENGINE_MAX_MOVES];
    count_t all = {0}, all_hard = {0}, start = {0}, start_hard = {0};
    long bin_n[10] = {0}, bin_wins[10] = {0};
    double bin_p[10] = {0};

    reached = calloc((size_t) db.n, 1);
    if (!reached)
    {
        perror("tablebase");
        return 1;
    }
    engine_start(&p);
    reach(&p);
    for (long i = 0; i < db.n; i++)
    {
        p = db.pos[i];
        int n = engine_moves(&p, list);
        if (n == 0)
            continue;
        int win = db.win[i], v = engine_eval(&p), kept = 0;
        if (win)
        {
            position_t q;
            engine_make(&p, eval_move(&p, list, n), &q);
            kept = engine_moves(&q, list) == 0 || lookup(&q) == 0;
            n = engine_moves(&p, list);
        }
        // The moves to the last rank come first.
        int last = p.side == WHITE ? db.ranks - 1 : 0;
        int hard = SQUARE_RANK(MOVE_TO(list[0])) != last;
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

    printf("evaldb %dx%d: the sign of engine_eval against %s, without a search\n", db.files, db.ranks, source);
    print_count("all", &all);
    print_count("not won at once", &all_hard);
    if (start.n < all.n)
    {
        print_count("from the start", &start);
        print_count("  not won at once", &start_hard);
    }
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

    db_read(&db, stdin, "tablebase");
    // touchdown_db's dump has every position; solve's those that can
    // come from the start.
    const char *source = db.n_keys < db.n ? "touchdown_db" : "solve";
    if (eval)
        return eval_db(source);

    long wins = 0, losses = 0;
    uint64_t nodes = 0;
    for (long i = 0; i < db.n; i++)
    {
        position_t p = db.pos[i], q;
        engine_search_t r, r2;
        engine_search(&p, depth, 0, &r);
        nodes += r.nodes;
        wins += db.win[i];
        losses += !db.win[i];
        if (!ENGINE_PROVED(r.score))
            error(&p, "not proved at this depth");
        else if ((r.score > 0) != db.win[i])
            error(&p, db.win[i] ? "a WIN, searched as a loss" : "a LOSS, searched as a win");
        else if (r.score > 0)
        {
            engine_make(&p, r.move, &q);
            move_t list[ENGINE_MAX_MOVES];
            if (lookup(&q) != 0 && engine_moves(&q, list) != 0)
                error(&p, "the move does not lead to a LOSS for the opponent");
            engine_search(&q, depth, 0, &r2);
            if (r2.score != -(r.score + 1))
                error(&p, "the opponent's loss is not one ply shorter");
        }
    }

    printf("tablebase %dx%d: %ld positions, %ld wins, %ld losses, %d differences\n", db.files, db.ranks, db.n,
           wins, losses, errors);
    fprintf(stderr, "%llu nodes\n", (unsigned long long) nodes);
    return errors ? 1 : 0;
}
