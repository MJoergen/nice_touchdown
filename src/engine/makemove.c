/***************************************************************
 * makemove.c: a move, by copy: a position is only seven words, so
 * the search keeps the one before instead of an undo.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <assert.h>
#include "internal.h"

void engine_make(const position_t *p, move_t m, position_t *out)
{
    int from = MOVE_FROM(m), to = MOVE_TO(m);
    int us = p->side, them = us ^ 1;
    uint32_t from_bit = 1u << (from & 31), to_bit = 1u << (to & 31);

    assert(p->pawns[us][from >> 5] & from_bit);
    assert(!(p->pawns[us][to >> 5] & to_bit));
    assert(board_valid[to >> 5] & to_bit);

    if (out != p)
        *out = *p;
    out->pawns[us][from >> 5] &= ~from_bit;
    out->pawns[us][to >> 5] |= to_bit;
    out->pawns[them][to >> 5] &= ~to_bit;
    out->side = them;
}
