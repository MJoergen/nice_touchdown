/***************************************************************
 * book.c: the opening book, of the start of the sizes that the
 * solver of the host solves (tools/solve.c, book.h).
 *
 * At the start of such a size, the book has whether White wins, and
 * if so, all the moves that win. If White loses, every move of White's
 * loses, and the book has, for each, all of Black's replies that win.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <assert.h>
#include "internal.h"
#include "book.h"

static int same(const position_t *p, const position_t *q)
{
    for (int i = 0; i < 3; i++)
        if (p->pawns[WHITE][i] != q->pawns[WHITE][i] || p->pawns[BLACK][i] != q->pawns[BLACK][i])
            return 0;
    return p->side == q->side;
}

int book_probe(const position_t *p, const uint16_t **moves, int *n)
{
    position_t start, q;
    move_t list[ENGINE_MAX_MOVES];
    engine_start(&start);
    for (const uint16_t *e = book; e[0]; e += 4 + e[3])
    {
        if (e[0] != board_files || e[1] != board_ranks)
            continue;
        if (same(p, &start))
        {
            *moves = e + 4;
            *n = e[2] ? e[3] : 0;
            return e[2];
        }
        if (e[2] || p->side != BLACK)
            return -1;
        // White has made a move, which loses: Black's replies.
        int m = engine_moves(&start, list);
        const uint16_t *g = e + 4;
        for (int i = 0; i < m; i++, g += 1 + g[0])
        {
            engine_make(&start, list[i], &q);
            if (same(p, &q))
            {
                *moves = g + 1;
                *n = g[0];
                return 1;
            }
        }
        assert(g == e + 4 + e[3]);
        return -1;
    }
    return -1;
}
