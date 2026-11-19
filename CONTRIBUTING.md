# Contributing

This file is for anyone who changes the repository: how to build it and
run the tests.

## Building and testing

Needs Python 3.10+, clang with the wasm32 target, `wasm-ld`, `llvm-ar`,
a C and a C++ compiler for the host, `git`, and `curl`.

    make             # list the targets
    make deps        # nice_cpu V2.0, niceemu V2.0, and touchdown_db into deps/
    make all         # build/nice/ for niceemu, build/mega65/ for the MEGA65, build/host/td_text for the host
    make test        # the tests, on the host and in niceemu
    make pawns       # draw the pawns again into src/ui/pawns.h, after a change to tools/mkpawns.py (needs Pillow)
    make size        # the size of the MEGA65 builds, against the 92K words the monitor leaves them
    make dist        # the archive of a release, and SHA256SUMS, in build/dist/
    make perft-deep  # the perft suite to depth 6, and 10000 games a size, on the host (about 2 minutes)
    make train       # learn N-tuple weights by TD self-play, and check them on 4x4 (about 1.5 minutes)
    make weights     # learn the engine's weights into src/engine/eval_weights.h (about 5 minutes)
    make solve       # solve the start of the boards solved in under 30 s each: who wins (about a minute)

To use local copies of nice_cpu and niceemu instead, pass `NICE_CPU=DIR`
and `NICEEMU=PATH` to make.

On a MEGA65 under NiceMonMEGA65's monitor, with `ETH=IP` or
`PORT=DEVICE`, and a checkout of NiceMonMEGA65 (`NICEMON=DIR`):

    make board-text    # run the text front end, and talk to it over the line
    make board-screen  # run the windowed front end, for the screen and keyboard
    make board-test    # the text front end's tests, on the board

## The tests

`tools/runtest.sh` runs a program with an input file as its console,
on the host or in niceemu, keeps the lines of the output that match
`tests/CHECK.grep`, and compares them, with main's exit status, with
`tests/CHECK.expected`. The MEGA65 build runs in niceemu too, behind
nice_cpu's stand-in for the monitor.

`tests/perftsuite.c` checks the engine's move generator against a
plain, slow one of its own, square by square, on every size of board:
at every node of the move tree from the start, and of games of moves
at random played to their end, it compares the moves, their order,
the position after each, and the text of moves and positions. Its
`.expected` files hold the counts of the tree and how the games ended,
which the slow generator checks as they are made.

`tests/tablebase.c` checks the search against touchdown_db's database
of the 4x4 board, and measures the evaluation alone against it
(below). The test `solve` checks the solver against it too, and solves
the boards up to 6x5 and 5x6.

## The 4x4 database

[touchdown_db](https://github.com/MJoergen/touchdown_db), at the
commit of `TOUCHDOWN_DB_COMMIT` in the Makefile, has the value, WIN or
LOSS for the side to move, of every 4x4 position with at most 4 pawns
a side. The build fetches its sources into `deps/touchdown_db/`, makes
it (`build/host/touchdown_db`), and runs it to write
`build/gen/touchdown4x4.txt`, in under a second.

A position is a 24-bit index, seen from the side to move, whose pawns
move towards row 0: bits 15-0 are the squares that have a pawn, row by
row, and bits 23-16 have a bit for each of those pawns in the same
order, 1 for the side to move (`index.h`). Of the 2^24 indices,
`indexIsValid` leaves out those with more than 4 pawns a side, or with
bits for more pawns than there are.

It solves the board by sweeps over all the indices, not by moves taken
back. A position is known when:

- the side to move has already won (a pawn on row 0, or the opponent
  has none): it cannot come in a game, and is marked WIN, as are the
  indices left out;
- the side to move has lost (an opponent's pawn on its first row, or
  no pawns): LOSS;
- every position after a move is known: WIN if one of them is a LOSS,
  else LOSS (also when there is no move).

The sweeps go on until one finds nothing new: 8 of them. It keeps two
bitmaps of 2^24 bits: the values (`build/gen/touchdown.tb`) and which
are known (`build/gen/touchdown.known`, which the build moves there
from `/tmp`).

`touchdown_db -d` writes a line for each valid index but those where
the side to move has already won: the index, the board (its ranks from
the side to move's last down to its first, the side to move `X`), and
the value. Games that are over, lost by the side to move, are among
them. `touchdown4x4.txt` has 755591 lines: 220104 WIN and 535487 LOSS.
The start is a WIN: on 4x4 the player who moves first wins.

It is used three times:

- `tests/tablebase.c` (`make test`) searches every position to depth
  24, which reaches the end of every game, and checks that the value
  is the database's and proved, that the move played in a WIN leads to
  a LOSS of the database, and that a Win in N is followed by a Loss in
  N - 1. Its expected output: `755591 positions, 220104 wins, 535487
  losses, 0 differences`.
- `tablebase eval` (the test `evaldb`) measures the engine's
  evaluation alone, `engine_eval` without a search, in the 275836
  positions that have a move: how often its sign is the database's
  value, and how often the move it would play (one that ends the game,
  or else the one whose position is worst for the opponent) keeps a
  WIN. It reports all of them, those not won at once by a move to the
  last rank, those that can come from the start (8077), and those of
  them not won at once (3722); and, of those not won at once, how
  often a WIN is for each tenth of the evaluation's probability of a
  win. It is a measure, not a check, and `tests/evaldb.expected`
  changes with the weights.
- `tools/train.c --db` (`make train`, `make weights`) checks the
  evaluation it learns in the same way, with its weights as floats
  rather than the engine's hundredths.

The figures of the README's evaluation are of the last 3722, where
always guessing the more common value is right for 60%:

| Weights                                                 | Right |
|---------------------------------------------------------|-------|
| `make weights`: self-play on every size (and `evaldb`)  | 85%   |
| the same, on every size but 4x4 (`--sizes`)             | 84%   |
| `make train TRAIN_ARGS="--fit 100"`: 3x2, fit to the DB | 88%   |
| `make train TRAIN_ARGS="--fit 100 --window 3x3"`        | 92%   |

`--fit` shows how much a layout of windows can know of the 4x4 board
at all. It takes seconds, where `make weights` takes five minutes.

## The solver

`tools/solve.c` (`make solve`, with `SOLVE_ARGS`) finds, on the host,
whether the side to move wins a position with best play: the start of
a size, such as `6x4`, or a position as text. Its comment has the
details. As every move takes a pawn one rank forward, a game is short
and has no cycles, so a depth-first search that stops at the first
move that wins, with a table of the positions it has solved, finds
the value without a depth. The table keeps a position as the side to
move sees it, and as the smaller of it and its mirror, so that a
position, the same with the other side to move, and their mirrors
share an entry.

`--check FILE` solves every position of touchdown_db's 4x4 database
and compares them; `--moves` gives the value of each move at the
root; `--order plain` tries the moves in the order of the move
generator rather than the evaluation's, which on these boards takes
about as many nodes; `--hash MB` is the size of the table (1024).

The figures, from the start, on one core:

| Size | First player | Nodes   | Time  |
|------|--------------|---------|-------|
| 4x4  | WIN          | 276     |       |
| 5x4  | LOSS         | 2497    |       |
| 6x4  | LOSS         | 13177   |       |
| 7x4  | WIN          | 72823   |       |
| 8x4  | LOSS         | 643413  | 0.5 s |
| 9x4  | WIN          | 2.8 M   | 1.8 s |
| 10x4 | WIN          | 37 M    | 22 s  |
| 11x4 | LOSS         | 240 M   | 153 s |
| 4x5  | LOSS         | 1730    |       |
| 5x5  | WIN          | 13982   |       |
| 6x5  | LOSS         | 223550  | 0.2 s |
| 7x5  | WIN          | 3.5 M   | 2.3 s |
| 8x5  | WIN          | 38 M    | 25 s  |
| 9x5  | WIN          | 381 M   | 255 s |
| 4x6  | WIN          | 3826    |       |
| 5x6  | WIN          | 145404  | 0.1 s |
| 6x6  | LOSS         | 1.6 M   | 1.0 s |
| 7x6  | LOSS         | 41 M    | 28 s  |
| 8x6  | WIN          | 776 M   | 519 s |

Those of 7x6 and above are with `--hash 6144`, six at a time. 12x4 and
10x5 are not solved in 10 minutes; the larger boards, with more
positions than the table holds, lose more of them to the
replacement. The next steps would be a larger table on disk, or a
proof-number search, which looks at fewer positions.

## The evaluation and its trainer

`tools/train.c` learns the weights of the engine's evaluation
(`src/engine/eval.c`), an N-tuple network, by TD(λ) self-play on the
host, on boards of the sizes of `--sizes`, and checks them against
touchdown_db's 4x4 database: how often the sign of the evaluation is
the database's value, and how often its move keeps a win, of all
positions and of those that can come from the start. `--fit EPOCHS`
learns the database itself instead, which shows how much the windows
can know at all. Its comment has the details.

`make train TRAIN_ARGS="..."` is for experiments. `make weights` learns
the engine's weights with `WEIGHTS_ARGS` and writes them to
`src/engine/eval_weights.h`, which is in git, with the command line
that made them; a run is the same every time. After it, the tests that
search, and `evaldb`, have new expected outputs.

The test `evalcheck` (`train --check-engine`) checks that the engine's
evaluation, written for speed, is the sum of the weights of the
trainer's windows, in every position of 2700 games at random on every
size. The trainer reads the weights from `eval_weights.h` too, and
needs it to build: one written by another layout of the windows is
made again with a trainer built against a stand-in `engine_eval`.

The tests of the windowed front end (`tests/screen*.keys`) type keys
into it in niceemu, and compare the moves it logs and the screen at
the end, with its colors, for the build for niceemu and for the MEGA65.
`tools/screenshot.py` of nice_chess draws such a screen as a PNG, from
niceemu's `-d 0xFFFF0000:2000 -d 0xFFFFFF86:1 -d 0xFFFE0000:16384`.

The tests that search set `seed 1`, so that the engine's choice among
moves as good is the same on the host as in niceemu.
