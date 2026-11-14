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
- `platform/`: the clock, the default time of a move, the rate of the
  players' clocks, a seed, and reads of the console, a byte at a time,
  which wait or not: `nice.c` for NICE, `host.c` for a PC, behind
  `platform.h`. `nice.c` tells niceemu from a board by the ratio of
  the cycle counter to the microsecond counter.
- `ui/version.h`: the version the front ends show, from
  `build/gen/version.c`, which the Makefile writes (`VERSION`).
- `ui/text.c`: the text front end, on the console. `help` lists its
  commands.
