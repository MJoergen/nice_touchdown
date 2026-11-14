/***************************************************************
 * perft.c: the number of paths of the move tree, to check the move
 * generator.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include "internal.h"

uint64_t engine_perft(const position_t *p, int depth)
{
    if (depth == 0)
        return 1;
    move_t list[ENGINE_MAX_MOVES];
    int n = engine_moves(p, list);
    if (depth == 1)
        return (uint64_t) n;
    uint64_t sum = 0;
    for (int i = 0; i < n; i++)
    {
        position_t q;
        engine_make(p, list[i], &q);
        sum += engine_perft(&q, depth - 1);
    }
    return sum;
}
