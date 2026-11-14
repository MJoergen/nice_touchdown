/***************************************************************
 * internal.h: what the engine's files share.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#ifndef INTERNAL_H
#define INTERNAL_H

#include "engine.h"

// The geometry of the board, which engine_set_size sets: the bits of
// the squares on the board, and of rank 1 and of the last rank.
extern int board_files, board_ranks;
extern uint32_t board_valid[3];
extern uint32_t board_first[3];
extern uint32_t board_last[3];

#endif // INTERNAL_H
