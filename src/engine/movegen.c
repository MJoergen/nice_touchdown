/***************************************************************
 * movegen.c: the legal moves, for all pawns of a side at once.
 *
 * A move one rank forward is a shift of the bitboard by 16 bits, up
 * for White and down for Black, across the three words. A capture is
 * that and a shift by one file, within each word: as a rank has at
 * most 12 files, bits 12 to 15 of each rank are 0, so a shift by one
 * file never carries into the next rank. A capture needs no mask of
 * the edge files: what goes past an edge lands on a bit that is off
 * the board, where the opponent has no pawn.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include "internal.h"

// a, one rank up (towards Black's side).
static inline void shift_up(const uint32_t *a, uint32_t *r)
{
    r[2] = a[2] << 16 | a[1] >> 16;
    r[1] = a[1] << 16 | a[0] >> 16;
    r[0] = a[0] << 16;
}

// a, one rank down (towards White's side).
static inline void shift_down(const uint32_t *a, uint32_t *r)
{
    r[0] = a[0] >> 16 | a[1] << 16;
    r[1] = a[1] >> 16 | a[2] << 16;
    r[2] = a[2] >> 16;
}

int engine_touchdown(const position_t *p)
{
    const uint32_t *back = p->side == WHITE ? board_first : board_last;
    const uint32_t *them = p->pawns[p->side ^ 1];
    return ((them[0] & back[0]) | (them[1] & back[1]) | (them[2] & back[2])) != 0;
}

// Adds a move to each square of targets, from the square delta away.
static int add_moves(move_t *list, int n, uint32_t *targets, int delta)
{
    for (int i = 0; i < 3; i++)
    {
        uint32_t t = targets[i];
        while (t)
        {
            int to = i * 32 + __builtin_ctz(t);
            t &= t - 1;
            list[n++] = MOVE(to + delta, to);
        }
    }
    return n;
}

int engine_moves(const position_t *p, move_t *list)
{
    if (engine_touchdown(p))
        return 0;

    const uint32_t *us = p->pawns[p->side];
    const uint32_t *them = p->pawns[p->side ^ 1];
    const uint32_t *last = p->side == WHITE ? board_last : board_first;

    // The squares one rank forward of our pawns, and the moves there:
    // forward to an empty square, and left (to the file before) and
    // right (to the file after) onto a pawn of theirs. The deltas are
    // from - to.
    uint32_t fwd[3], push[3], left[3], right[3];
    int d_push, d_left, d_right;
    if (p->side == WHITE)
    {
        shift_up(us, fwd);
        d_push = -16, d_left = -15, d_right = -17;
    }
    else
    {
        shift_down(us, fwd);
        d_push = 16, d_left = 17, d_right = 15;
    }
    for (int i = 0; i < 3; i++)
    {
        uint32_t empty = board_valid[i] & ~(us[i] | them[i]);
        push[i] = fwd[i] & empty;
        left[i] = fwd[i] >> 1 & them[i];
        right[i] = fwd[i] << 1 & them[i];
    }

    // To the last rank first, then the other captures, then the rest.
    uint32_t t[3];
    int n = 0;
    for (int i = 0; i < 3; i++)
        t[i] = right[i] & last[i];
    n = add_moves(list, n, t, d_right);
    for (int i = 0; i < 3; i++)
        t[i] = left[i] & last[i];
    n = add_moves(list, n, t, d_left);
    for (int i = 0; i < 3; i++)
        t[i] = push[i] & last[i];
    n = add_moves(list, n, t, d_push);
    for (int i = 0; i < 3; i++)
        t[i] = right[i] & ~last[i];
    n = add_moves(list, n, t, d_right);
    for (int i = 0; i < 3; i++)
        t[i] = left[i] & ~last[i];
    n = add_moves(list, n, t, d_left);
    for (int i = 0; i < 3; i++)
        t[i] = push[i] & ~last[i];
    n = add_moves(list, n, t, d_push);
    return n;
}
