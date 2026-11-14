/***************************************************************
 * game.c: the game as the front ends see it: the positions from its
 * start, the moves between them, and its result.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include "internal.h"

static position_t positions[ENGINE_MAX_GAME + 1];
static move_t moves[ENGINE_MAX_GAME];
static int plies; // The moves made, so positions[plies] is the current one

void engine_new_game(void)
{
    engine_start(&positions[0]);
    plies = 0;
}

int engine_game_set_position(const char *s)
{
    if (engine_set_position(&positions[0], s))
        return -1;
    plies = 0;
    return 0;
}

const position_t *engine_position(void)
{
    if (!board_files)
        engine_new_game();
    return &positions[plies];
}

int engine_game_plies(void)
{
    return plies;
}

move_t engine_game_move(int i)
{
    return moves[i];
}

int engine_play(move_t m)
{
    if (plies >= ENGINE_MAX_GAME)
        return -1;
    moves[plies] = m;
    engine_make(&positions[plies], m, &positions[plies + 1]);
    plies++;
    return 0;
}

int engine_undo(void)
{
    if (plies == 0)
        return -1;
    plies--;
    return 0;
}

int engine_result(void)
{
    move_t list[ENGINE_MAX_MOVES];
    const position_t *p = engine_position();
    if (engine_moves(p, list))
        return ENGINE_PLAYING;
    return p->side == WHITE ? ENGINE_BLACK_WINS : ENGINE_WHITE_WINS;
}
