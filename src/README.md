# src

- `engine/`: the engine core (from Stage 1 on). It does no I/O, reads
  no clock, and calls no malloc, so it builds for NICE and for the
  host alike.
- `platform/`: the clock, the default time of a move, the rate of the
  players' clocks, a seed, and reads of the console, a byte at a time,
  which wait or not: `nice.c` for NICE, `host.c` for a PC, behind
  `platform.h`. `nice.c` tells niceemu from a board by the ratio of
  the cycle counter to the microsecond counter.
- `ui/version.h`: the version the front ends show, from
  `build/gen/version.c`, which the Makefile writes (`VERSION`).
- `ui/text.c`: the text front end, on the console. `help` lists its
  commands.
