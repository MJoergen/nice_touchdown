/***************************************************************
 * board.c: the size of the board, and the position at the start.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include <string.h>
#include "internal.h"

int board_files, board_ranks;
uint32_t board_valid[3];
uint32_t board_first[3];
uint32_t board_last[3];

// The bits of one rank: a mask of the board's files at rank r.
static void set_rank(uint32_t *bb, int r, uint32_t files)
{
    bb[r >> 1] |= files << (16 * (r & 1));
}

int engine_set_size(int files, int ranks)
{
    if (files < ENGINE_MIN_FILES || files > ENGINE_MAX_FILES
        || ranks < ENGINE_MIN_RANKS || ranks > ENGINE_MAX_RANKS)
        return -1;
    board_files = files;
    board_ranks = ranks;
    uint32_t row = (1u << files) - 1;
    memset(board_valid, 0, sizeof board_valid);
    memset(board_first, 0, sizeof board_first);
    memset(board_last, 0, sizeof board_last);
    for (int r = 0; r < ranks; r++)
        set_rank(board_valid, r, row);
    set_rank(board_first, 0, row);
    set_rank(board_last, ranks - 1, row);
    return 0;
}

int engine_files(void)
{
    if (!board_files)
        engine_set_size(8, 6);
    return board_files;
}

int engine_ranks(void)
{
    if (!board_ranks)
        engine_set_size(8, 6);
    return board_ranks;
}

void engine_start(position_t *p)
{
    engine_files(); // The default size, before the first engine_set_size
    for (int i = 0; i < 3; i++)
    {
        p->pawns[WHITE][i] = board_first[i];
        p->pawns[BLACK][i] = board_last[i];
    }
    p->side = WHITE;
}

int engine_pawn(const position_t *p, int file, int rank)
{
    int sq = SQUARE(file, rank);
    uint32_t bit = 1u << (sq & 31);
    if (p->pawns[WHITE][sq >> 5] & bit)
        return WHITE;
    if (p->pawns[BLACK][sq >> 5] & bit)
        return BLACK;
    return -1;
}
