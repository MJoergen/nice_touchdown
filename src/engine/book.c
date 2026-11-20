/***************************************************************
 * book.c: the opening book, of the start of the sizes that the
 * solver of the host solves (tools/solve.c, book.h).
 *
 * At the start of such a size, the book has whether White wins, and
 * the first two moves of the side that wins: all its moves that win,
 * and, after each of them and each reply, all its moves that win
 * again. If White wins, those are White's first and second moves; if
 * White loses, every move of White's loses, and they are Black's
 * replies to it and Black's next moves. A position after one of those
 * moves the book has as a LOSS for its side to move.
 *
 * The book has no index: a probe walks it from the start, making the
 * moves, until a position is the one probed, which is at most some
 * thousands of moves.
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

// The position probed, and the moves of the book's answer.
static const position_t *target;
static const uint16_t *found_moves;
static int found_n;

/***************************************************************
 * walk
 * Walks the node at *e of q, a WIN for its side to move, of the given
 * depth (book.h), and moves *e past it. Returns 1 if target is q, with
 * its moves in found_moves and found_n; 0 if target is a position
 * after one of q's moves that win, a LOSS; and else -1, if target is
 * not in the node.
 ***************************************************************/
static int walk(const uint16_t **e, const position_t *q, int depth)
{
    const uint16_t *g = *e;
    int k = g[0];
    const uint16_t *wins = g + 1;
    *e = g + 1 + k;
    if (same(target, q))
    {
        found_moves = wins;
        found_n = k;
        return 1;
    }
    position_t r, s;
    move_t list[ENGINE_MAX_MOVES];
    for (int i = 0; i < k; i++)
    {
        engine_make(q, wins[i], &r);
        if (same(target, &r))
            return 0;
        if (depth <= 1)
            continue;
        int n = engine_moves(&r, list);
        for (int j = 0; j < n; j++)
        {
            engine_make(&r, list[j], &s);
            int v = walk(e, &s, depth - 1);
            if (v >= 0)
                return v;
        }
    }
    return -1;
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
        const uint16_t *g = e + 4;
        int v = -1;
        target = p;
        if (e[2])
            v = walk(&g, &start, 2);
        else if (same(p, &start))
            v = 0;
        else
        {
            int m = engine_moves(&start, list);
            for (int i = 0; i < m && v < 0; i++)
            {
                engine_make(&start, list[i], &q);
                v = walk(&g, &q, 2);
            }
        }
        assert(v >= 0 || g == e + 4 + e[3]); // The walk read all the entry
        if (v == 1)
        {
            *moves = found_moves;
            *n = found_n;
        }
        return v;
    }
    return -1;
}
