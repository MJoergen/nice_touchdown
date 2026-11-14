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
 *   - its value is touchdown_db's, WIN or LOSS, and never Even;
 *   - in a WIN, the move it plays leads to a position that
 *     touchdown_db has as a LOSS for the opponent;
 *   - in a "Win in N", that position searches as "Loss in N - 1".
 *
 * It prints the counts and the first differences, and returns 0 if
 * there are none.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

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

int main(int argc, char **argv)
{
    int depth = argc > 1 ? atoi(argv[1]) : 24;
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
        if (r.score == 0)
            error(texts[i], "Even at this depth");
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
