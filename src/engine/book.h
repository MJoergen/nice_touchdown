/***************************************************************
 * book.h: the engine's opening book, of the start of the sizes that
 * tools/solve.c solves. Written by it (make book):
 *
 *   build/host/solve --threads 16 --hash 32768 --book build/gen/book.h 4x4 5x4 6x4 7x4 8x4 9x4 10x4 11x4 12x4 4x5 5x5 6x5 7x5 8x5 9x5 10x5 4x6 5x6 6x6 7x6 8x6
 *
 * Each entry: the files, the ranks, 1 if the start is a WIN for
 * White and 0 if a LOSS, the number of the numbers that follow, and
 * they: if a WIN, all of White's first moves that win; if a LOSS,
 * for each of White's first moves, in the order of engine_moves, the
 * number of Black's replies that win, and they. A move is a move_t (engine.h): the square it is from in the
 * low byte, and the square it is to in the high byte, a square
 * rank * 16 + file. 0 ends the book.
 *
 * Do not edit.
 ***************************************************************/

static const uint16_t book[] = {
    // 4x4: a WIN for White, by a1a2 d1d2
    4, 4, 1, 2,
    0x1000, 0x1303,
    // 5x4: a LOSS for White; Black's replies that win:
    //   a1a2: e4e3
    //   b1b2: d4d3 e4e3
    //   c1c2: a4a3 c4c3 e4e3
    //   d1d2: a4a3 b4b3
    //   e1e2: a4a3
    5, 4, 0, 14,
    1, 0x2434, 2, 0x2333, 0x2434, 3, 0x2030, 0x2232,
    0x2434, 2, 0x2030, 0x2131, 1, 0x2030,
    // 6x4: a LOSS for White; Black's replies that win:
    //   a1a2: a4a3 c4c3 f4f3
    //   b1b2: a4a3 c4c3 e4e3 f4f3
    //   c1c2: a4a3 e4e3 f4f3
    //   d1d2: a4a3 b4b3 f4f3
    //   e1e2: a4a3 b4b3 d4d3 f4f3
    //   f1f2: a4a3 d4d3 f4f3
    6, 4, 0, 26,
    3, 0x2030, 0x2232, 0x2535, 4, 0x2030, 0x2232, 0x2434,
    0x2535, 3, 0x2030, 0x2434, 0x2535, 3, 0x2030, 0x2131,
    0x2535, 4, 0x2030, 0x2131, 0x2333, 0x2535, 3, 0x2030,
    0x2333, 0x2535,
    // 7x4: a WIN for White, by a1a2 b1b2 f1f2 g1g2
    7, 4, 1, 4,
    0x1000, 0x1101, 0x1505, 0x1606,
    // 8x4: a LOSS for White; Black's replies that win:
    //   a1a2: b4b3
    //   b1b2: b4b3
    //   c1c2: e4e3
    //   d1d2: a4a3 d4d3 e4e3
    //   e1e2: d4d3 e4e3 h4h3
    //   f1f2: d4d3
    //   g1g2: g4g3
    //   h1h2: g4g3
    8, 4, 0, 20,
    1, 0x2131, 1, 0x2131, 1, 0x2434, 3, 0x2030,
    0x2333, 0x2434, 3, 0x2333, 0x2434, 0x2737, 1, 0x2333,
    1, 0x2636, 1, 0x2636,
    // 9x4: a WIN for White, by a1a2 d1d2 e1e2 f1f2 i1i2
    9, 4, 1, 5,
    0x1000, 0x1303, 0x1404, 0x1505, 0x1808,
    // 10x4: a WIN for White, by b1b2 i1i2
    10, 4, 1, 2,
    0x1101, 0x1808,
    // 11x4: a LOSS for White; Black's replies that win:
    //   a1a2: a4a3 c4c3 f4f3 g4g3 k4k3
    //   b1b2: c4c3 f4f3 g4g3 k4k3
    //   c1c2: c4c3 e4e3 f4f3 j4j3 k4k3
    //   d1d2: a4a3 e4e3 j4j3 k4k3
    //   e1e2: a4a3 k4k3
    //   f1f2: a4a3 f4f3 k4k3
    //   g1g2: a4a3 k4k3
    //   h1h2: a4a3 b4b3 g4g3 k4k3
    //   i1i2: a4a3 b4b3 f4f3 g4g3 i4i3
    //   j1j2: a4a3 e4e3 f4f3 i4i3
    //   k1k2: a4a3 e4e3 f4f3 i4i3 k4k3
    11, 4, 0, 54,
    5, 0x2030, 0x2232, 0x2535, 0x2636, 0x2a3a, 4, 0x2232,
    0x2535, 0x2636, 0x2a3a, 5, 0x2232, 0x2434, 0x2535, 0x2939,
    0x2a3a, 4, 0x2030, 0x2434, 0x2939, 0x2a3a, 2, 0x2030,
    0x2a3a, 3, 0x2030, 0x2535, 0x2a3a, 2, 0x2030, 0x2a3a,
    4, 0x2030, 0x2131, 0x2636, 0x2a3a, 5, 0x2030, 0x2131,
    0x2535, 0x2636, 0x2838, 4, 0x2030, 0x2434, 0x2535, 0x2838,
    5, 0x2030, 0x2434, 0x2535, 0x2838, 0x2a3a,
    // 12x4: a WIN for White, by b1b2 e1e2 h1h2 k1k2
    12, 4, 1, 4,
    0x1101, 0x1404, 0x1707, 0x1a0a,
    // 4x5: a LOSS for White; Black's replies that win:
    //   a1a2: b5b4 c5c4 d5d4
    //   b1b2: b5b4 c5c4
    //   c1c2: b5b4 c5c4
    //   d1d2: a5a4 b5b4 c5c4
    4, 5, 0, 14,
    3, 0x3141, 0x3242, 0x3343, 2, 0x3141, 0x3242, 2,
    0x3141, 0x3242, 3, 0x3040, 0x3141, 0x3242,
    // 5x5: a WIN for White, by a1a2 b1b2 c1c2 d1d2 e1e2
    5, 5, 1, 5,
    0x1000, 0x1101, 0x1202, 0x1303, 0x1404,
    // 6x5: a LOSS for White; Black's replies that win:
    //   a1a2: c5c4 d5d4 e5e4 f5f4
    //   b1b2: c5c4 d5d4 e5e4
    //   c1c2: c5c4 d5d4 e5e4
    //   d1d2: b5b4 c5c4 d5d4
    //   e1e2: b5b4 c5c4 d5d4
    //   f1f2: a5a4 b5b4 c5c4 d5d4
    6, 5, 0, 26,
    4, 0x3242, 0x3343, 0x3444, 0x3545, 3, 0x3242, 0x3343,
    0x3444, 3, 0x3242, 0x3343, 0x3444, 3, 0x3141, 0x3242,
    0x3343, 3, 0x3141, 0x3242, 0x3343, 4, 0x3040, 0x3141,
    0x3242, 0x3343,
    // 7x5: a WIN for White, by a1a2 b1b2 d1d2 f1f2 g1g2
    7, 5, 1, 5,
    0x1000, 0x1101, 0x1303, 0x1505, 0x1606,
    // 8x5: a WIN for White, by b1b2 c1c2 f1f2 g1g2
    8, 5, 1, 4,
    0x1101, 0x1202, 0x1505, 0x1606,
    // 9x5: a WIN for White, by b1b2 c1c2 d1d2 e1e2 f1f2 g1g2 h1h2
    9, 5, 1, 7,
    0x1101, 0x1202, 0x1303, 0x1404, 0x1505, 0x1606, 0x1707,
    // 10x5: a WIN for White, by b1b2 c1c2 h1h2 i1i2
    10, 5, 1, 4,
    0x1101, 0x1202, 0x1707, 0x1808,
    // 4x6: a WIN for White, by a1a2 b1b2 c1c2 d1d2
    4, 6, 1, 4,
    0x1000, 0x1101, 0x1202, 0x1303,
    // 5x6: a WIN for White, by a1a2 b1b2 d1d2 e1e2
    5, 6, 1, 4,
    0x1000, 0x1101, 0x1303, 0x1404,
    // 6x6: a LOSS for White; Black's replies that win:
    //   a1a2: a6a5 b6b5 c6c5 d6d5 e6e5 f6f5
    //   b1b2: a6a5 b6b5 c6c5 d6d5 f6f5
    //   c1c2: a6a5 b6b5 c6c5 d6d5 f6f5
    //   d1d2: a6a5 c6c5 d6d5 e6e5 f6f5
    //   e1e2: a6a5 c6c5 d6d5 e6e5 f6f5
    //   f1f2: a6a5 b6b5 c6c5 d6d5 e6e5 f6f5
    6, 6, 0, 38,
    6, 0x4050, 0x4151, 0x4252, 0x4353, 0x4454, 0x4555, 5,
    0x4050, 0x4151, 0x4252, 0x4353, 0x4555, 5, 0x4050, 0x4151,
    0x4252, 0x4353, 0x4555, 5, 0x4050, 0x4252, 0x4353, 0x4454,
    0x4555, 5, 0x4050, 0x4252, 0x4353, 0x4454, 0x4555, 6,
    0x4050, 0x4151, 0x4252, 0x4353, 0x4454, 0x4555,
    // 7x6: a LOSS for White; Black's replies that win:
    //   a1a2: a6a5 b6b5 c6c5 d6d5 e6e5 f6f5 g6g5
    //   b1b2: a6a5 b6b5 c6c5 d6d5 e6e5 f6f5
    //   c1c2: a6a5 b6b5 c6c5 d6d5 e6e5 f6f5
    //   d1d2: a6a5 b6b5 c6c5 d6d5 e6e5 f6f5 g6g5
    //   e1e2: b6b5 c6c5 d6d5 e6e5 f6f5 g6g5
    //   f1f2: b6b5 c6c5 d6d5 e6e5 f6f5 g6g5
    //   g1g2: a6a5 b6b5 c6c5 d6d5 e6e5 f6f5 g6g5
    7, 6, 0, 52,
    7, 0x4050, 0x4151, 0x4252, 0x4353, 0x4454, 0x4555, 0x4656,
    6, 0x4050, 0x4151, 0x4252, 0x4353, 0x4454, 0x4555, 6,
    0x4050, 0x4151, 0x4252, 0x4353, 0x4454, 0x4555, 7, 0x4050,
    0x4151, 0x4252, 0x4353, 0x4454, 0x4555, 0x4656, 6, 0x4151,
    0x4252, 0x4353, 0x4454, 0x4555, 0x4656, 6, 0x4151, 0x4252,
    0x4353, 0x4454, 0x4555, 0x4656, 7, 0x4050, 0x4151, 0x4252,
    0x4353, 0x4454, 0x4555, 0x4656,
    // 8x6: a WIN for White, by b1b2 c1c2 d1d2 e1e2 f1f2 g1g2
    8, 6, 1, 6,
    0x1101, 0x1202, 0x1303, 0x1404, 0x1505, 0x1606,
    0,
};
