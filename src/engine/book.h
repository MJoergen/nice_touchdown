/***************************************************************
 * book.h: the engine's opening book, of the start of the sizes that
 * tools/solve.c solves. Written by it (make book):
 *
 *   build/host/solve --threads 16 --hash 32768 --book build/gen/book.h 4x4 5x4 6x4 7x4 8x4 9x4 10x4 11x4 12x4 4x5 5x5 6x5 7x5 8x5 9x5 10x5 4x6 5x6 6x6 7x6 8x6
 *
 * Each entry: the files, the ranks, 1 if the start is a WIN for
 * White and 0 if a LOSS, the number of moves, and the moves: if a
 * WIN, White's first move, which wins; if a LOSS, for each of White's
 * first moves, in the order of engine_moves, Black's reply, which
 * wins. A move is a move_t (engine.h): the square it is from in the
 * low byte, and the square it is to in the high byte, a square
 * rank * 16 + file. 0 ends the book.
 *
 * Do not edit.
 ***************************************************************/

static const uint16_t book[] = {
    // 4x4: a WIN for White, by a1a2
    4, 4, 1, 1, 0x1000,
    // 5x4: a LOSS for White; Black's replies: a1a2 e4e3, b1b2 d4d3, c1c2 a4a3, d1d2 a4a3
    // e1e2 a4a3
    5, 4, 0, 5, 0x2434, 0x2333, 0x2030, 0x2030, 0x2030,
    // 6x4: a LOSS for White; Black's replies: a1a2 a4a3, b1b2 e4e3, c1c2 e4e3, d1d2 b4b3
    // e1e2 b4b3, f1f2 f4f3
    6, 4, 0, 6, 0x2030, 0x2434, 0x2434, 0x2131, 0x2131, 0x2535,
    // 7x4: a WIN for White, by b1b2
    7, 4, 1, 1, 0x1101,
    // 8x4: a LOSS for White; Black's replies: a1a2 b4b3, b1b2 b4b3, c1c2 e4e3, d1d2 d4d3
    // e1e2 d4d3, f1f2 d4d3, g1g2 g4g3, h1h2 g4g3
    8, 4, 0, 8, 0x2131, 0x2131, 0x2434, 0x2333, 0x2333, 0x2333, 0x2636, 0x2636,
    // 9x4: a WIN for White, by e1e2
    9, 4, 1, 1, 0x1404,
    // 10x4: a WIN for White, by b1b2
    10, 4, 1, 1, 0x1101,
    // 11x4: a LOSS for White; Black's replies: a1a2 f4f3, b1b2 f4f3, c1c2 c4c3, d1d2 e4e3
    // e1e2 a4a3, f1f2 f4f3, g1g2 k4k3, h1h2 b4b3
    // i1i2 i4i3, j1j2 e4e3, k1k2 f4f3
    11, 4, 0, 11, 0x2535, 0x2535, 0x2232, 0x2434, 0x2030, 0x2535, 0x2a3a, 0x2131,
    0x2838, 0x2434, 0x2535,
    // 12x4: a WIN for White, by e1e2
    12, 4, 1, 1, 0x1404,
    // 4x5: a LOSS for White; Black's replies: a1a2 d5d4, b1b2 c5c4, c1c2 b5b4, d1d2 a5a4
    4, 5, 0, 4, 0x3343, 0x3242, 0x3141, 0x3040,
    // 5x5: a WIN for White, by c1c2
    5, 5, 1, 1, 0x1202,
    // 6x5: a LOSS for White; Black's replies: a1a2 d5d4, b1b2 c5c4, c1c2 c5c4, d1d2 c5c4
    // e1e2 d5d4, f1f2 c5c4
    6, 5, 0, 6, 0x3343, 0x3242, 0x3242, 0x3242, 0x3343, 0x3242,
    // 7x5: a WIN for White, by d1d2
    7, 5, 1, 1, 0x1303,
    // 8x5: a WIN for White, by c1c2
    8, 5, 1, 1, 0x1202,
    // 9x5: a WIN for White, by d1d2
    9, 5, 1, 1, 0x1303,
    // 10x5: a WIN for White, by c1c2
    10, 5, 1, 1, 0x1202,
    // 4x6: a WIN for White, by b1b2
    4, 6, 1, 1, 0x1101,
    // 5x6: a WIN for White, by b1b2
    5, 6, 1, 1, 0x1101,
    // 6x6: a LOSS for White; Black's replies: a1a2 c6c5, b1b2 c6c5, c1c2 c6c5, d1d2 c6c5
    // e1e2 d6d5, f1f2 d6d5
    6, 6, 0, 6, 0x4252, 0x4252, 0x4252, 0x4252, 0x4353, 0x4353,
    // 7x6: a LOSS for White; Black's replies: a1a2 c6c5, b1b2 c6c5, c1c2 c6c5, d1d2 c6c5
    // e1e2 e6e5, f1f2 e6e5, g1g2 e6e5
    7, 6, 0, 7, 0x4252, 0x4252, 0x4252, 0x4252, 0x4454, 0x4454, 0x4454,
    // 8x6: a WIN for White, by c1c2
    8, 6, 1, 1, 0x1202,
    0,
};
