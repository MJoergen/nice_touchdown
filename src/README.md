# src

- `engine/`: the engine core. `engine.h` is its interface; `internal.h`
  is what its files share. It does no I/O, reads no clock, and calls no
  malloc, so it builds for NICE and for the host alike.
  - `board.c`: the size of the board, its masks, and the start.
  - `movegen.c`: the legal moves of all pawns of a side at once, by
    shifts of the bitboards, in the order of the search: to the last
    rank, then captures, then the rest. The end of the game.
  - `makemove.c`: a move, by copy of the position.
  - `notation.c`: moves as text (`b1b2`), and positions as text
    (`OOOO/..../..../XXXX x`, touchdown_db's letters).
  - `perft.c`: perft.
  - `eval.c`: the evaluation of a leaf: an N-tuple network of windows
    of 3 files by 2 ranks, whose weights, in `eval_weights.h`,
    `tools/train.c` learns by self-play (`make weights`).
    `eval_weights.h` is written by it; do not edit it.
  - `search.c`: negamax with alpha-beta pruning to a fixed depth. Its
    scores are Win in N, Loss in N, and the evaluation, or Even without
    it; among the moves of the best score that is not proved, the root
    picks one at random, by the seed. Its positions and move lists are
    kept by ply in its own data, not on the stack.
  - `game.c`: the game as the front ends see it: its positions, its
    moves, undo, and its result.
- `platform/`: the clock, the default time of a move, the rate of the
  players' clocks, a seed, and reads of the console, a byte at a time,
  which wait or not: `nice.c` for NICE, `host.c` for a PC, behind
  `platform.h`. `nice.c` tells niceemu from a board by the ratio of
  the cycle counter to the microsecond counter.
- `ui/version.h`: the version the front ends show, from
  `build/gen/version.c`, which the Makefile writes (`VERSION`).
- `ui/screen.c`: the windowed front end, on the 80x25 screen, for NICE
  only. Its keys are listed at the top of the file and on the screen.
  `ui/pawns.h` has the glyphs of the pawns, which `tools/mkpawns.py`
  draws (`make pawns`).
- `ui/text.c`: the text front end, on the console. `help` lists its
  commands. `go` prints the search as `info depth D score S nodes N
  pv ...` and plays its move.
