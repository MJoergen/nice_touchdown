/***************************************************************
 * eval.c: the evaluation, an N-tuple network.
 *
 * The position is seen from the side to move, with its ranks turned
 * around when that is Black, so that its pawns move up. A window of 3
 * files by 2 ranks is placed at every place on the board; each of its
 * 6 squares is empty, the side's, or the opponent's, so the window is
 * in one of 3^6 = 729 states, and each state has a weight. The
 * evaluation is the sum of the weights of all the windows.
 *
 * The windows of all files share one table, so that the weights hold
 * for every width; there is a table for each number of ranks and each
 * pair of ranks the window is on (a slot), and in each for the windows
 * inside the board and those at an edge. A window at the right edge is
 * mirrored, and is one at the left edge. tools/train.c learns the
 * weights by self-play, on every size, and writes eval_weights.h.
 *
 * The state of a window is the number in base 3 whose digits are its
 * squares, 1 for the side's pawn and 2 for the opponent's: of the
 * rank nearer the side first, from the file on the left, then of the
 * other. For a pair of ranks, the side's pawns are interleaved, two
 * bits a file: the lower rank's in the even bits, the upper's in the
 * odd. A window's 6 bits are then a shift and a mask, and its state
 * tern6[mine] + tern6_2[theirs], with tern6_2 twice tern6; and
 * mirrored, the same with tern6_mirror and tern6_mirror_2.
 *
 * Copyright (c) 2026 Michael Jørgensen. MIT license (see LICENSE).
 ***************************************************************/

#include "internal.h"
#include "eval_weights.h"

// The first slot of each number of ranks: the boards of 4 ranks have
// 3 pairs of ranks, those of 5 have 4, and those of 6 have 5.
static const int8_t slot_base[ENGINE_MAX_RANKS + 1] = {0, 0, 0, 0, 0, 3, 7};

static int eval_on = 1;

void engine_set_eval(int on)
{
    eval_on = on != 0;
}

int engine_eval_on(void)
{
    return eval_on;
}

// The bits of 6 files, spread to the even bits of 12.
static const uint16_t spread6[64] = {
    0x000, 0x001, 0x004, 0x005, 0x010, 0x011, 0x014, 0x015,
    0x040, 0x041, 0x044, 0x045, 0x050, 0x051, 0x054, 0x055,
    0x100, 0x101, 0x104, 0x105, 0x110, 0x111, 0x114, 0x115,
    0x140, 0x141, 0x144, 0x145, 0x150, 0x151, 0x154, 0x155,
    0x400, 0x401, 0x404, 0x405, 0x410, 0x411, 0x414, 0x415,
    0x440, 0x441, 0x444, 0x445, 0x450, 0x451, 0x454, 0x455,
    0x500, 0x501, 0x504, 0x505, 0x510, 0x511, 0x514, 0x515,
    0x540, 0x541, 0x544, 0x545, 0x550, 0x551, 0x554, 0x555,
};

// The bits of a rank of 12 files, spread to the even bits of 24.
static inline uint32_t spread(uint32_t row)
{
    return spread6[row & 63] | (uint32_t) spread6[row >> 6] << 12;
}

int engine_eval(const position_t *p)
{
    int files = board_files, ranks = board_ranks;
    const uint32_t *us = p->pawns[p->side], *them = p->pawns[p->side ^ 1];

    // The ranks, from the side to move's view, spread.
    uint32_t mine[ENGINE_MAX_RANKS], theirs[ENGINE_MAX_RANKS];
    for (int r = 0; r < ranks; r++)
    {
        int a = p->side == WHITE ? r : ranks - 1 - r;
        int shift = 16 * (a & 1);
        mine[r] = spread(us[a >> 1] >> shift & 0xfff);
        theirs[r] = spread(them[a >> 1] >> shift & 0xfff);
    }

    int sum = 0;
    int inner = files - 4; // The windows inside the board, of a pair of ranks
    const int16_t (*w)[2][EVAL_STATES] = &eval_weights[slot_base[ranks]];
    for (int r = 0; r + 1 < ranks; r++, w++)
    {
        const int16_t *inside = (*w)[0], *edge = (*w)[1];
        uint32_t m = mine[r] | mine[r + 1] << 1;
        uint32_t t = theirs[r] | theirs[r + 1] << 1;

        sum += edge[tern6[m & 63] + tern6_2[t & 63]];
        for (int k = 0; k < inner; k++)
        {
            m >>= 2;
            t >>= 2;
            sum += inside[tern6[m & 63] + tern6_2[t & 63]];
        }
        m >>= 2;
        t >>= 2;
        sum += edge[tern6_mirror[m & 63] + tern6_mirror_2[t & 63]];
    }
    if (sum > ENGINE_EVAL_MAX)
        sum = ENGINE_EVAL_MAX;
    if (sum < -ENGINE_EVAL_MAX)
        sum = -ENGINE_EVAL_MAX;
    return sum;
}
