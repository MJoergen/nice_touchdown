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
    make book        # the opening book of the solved sizes into src/engine/book.h (about half an hour on 16 cores and 32 GB)
    make calibrate   # fit the probability of a win shown to solved positions of games (about 2 hours on 16 cores)

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

`tests/tablebase.c` checks the search against the databases of
positions and their values, touchdown_db's of 4x4 and the solver's of
6x4 and 5x5, and measures the evaluation alone against them (below).
The test `solve` checks the solver against touchdown_db's, and solves
the boards up to 6x5 and 5x6.

## The databases

A database is a file of positions and their values, WIN or LOSS for
the side to move, of one size of board. There are two kinds, which
`tools/db.h` reads for the programs of the host: touchdown_db's of
every 4x4 position, and the solver's of every position that can come
from the start of a size.

### touchdown_db's of 4x4

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

### The solver's

`solve --dump FILE SIZE` (below) writes every position that can come
from the start of SIZE, with its value, one to a line, as a position
as text then the value: `OOOO/..../.X../XX.X o LOSS`. A position, the
same with the other side to move, and their mirrors come as one. The
build makes them as `build/gen/solve_SIZE.txt`, such as
`build/gen/solve_6x4.txt`. They are far smaller than touchdown_db's
kind of database, of every position, as most positions never come in
a game:

| Size | Positions | File   | Time  |
|------|-----------|--------|-------|
| 4x4  | 4055      | 0.1 MB |       |
| 4x5  | 23945     | 0.6 MB |       |
| 5x4  | 61319     | 2 MB   |       |
| 4x6  | 93399     | 3 MB   |       |
| 5x5  | 576730    | 20 MB  | 0.3 s |
| 6x4  | 882745    | 30 MB  | 0.6 s |
| 5x6  | 3.2 M     | 120 MB | 2 s   |
| 7x4  | 12.4 M    | 470 MB | 9 s   |
| 6x5  | 13.1 M    | 540 MB | 9 s   |
| 6x6  | 102 M     | 4 GB   | 83 s  |
| 8x4  | 173 M     | 7 GB   | 167 s |

Each file more multiplies them by about 14; a table of 6x6 or 8x4
takes some GB, and of the sizes above them is out of reach.

### Their uses

- `tests/tablebase.c` (`make test`) searches every position of a
  database to a depth that reaches the end of every game, and checks
  that the value is the database's and proved, that the move played
  in a WIN leads to a LOSS of the database, and that a Win in N is
  followed by a Loss in N - 1: the test `tablebase` on touchdown_db's
  4x4 (`755591 positions, 220104 wins, 535487 losses, 0 differences`),
  and `tablebase_6x4` and `tablebase_5x5` on the solver's, which checks
  the solver too, by another way. 5x6 and larger take too long.
- `tablebase eval` (the tests `evaldb`, on touchdown_db's 4x4, and
  `evaldb_6x4`, `evaldb_5x5` and `evaldb_4x6`, on the solver's)
  measures the engine's evaluation alone, `engine_eval` without a
  search, in the positions that have a move: how often its sign is the
  database's value, and how often the move it would play (one that
  ends the game, or else the one whose position is worst for the
  opponent) keeps a WIN. It reports all of them, those not won at once
  by a move to the last rank, and, for touchdown_db's, those that can
  come from the start (8077), and those of them not won at once
  (3722); and, of those not won at once, how often a WIN is for each
  tenth of the evaluation's probability of a win. It is a measure, not
  a check, and its expected outputs change with the weights.
- `tools/train.c --db FILE`, more than one (`make train`, `make
  weights`, with the databases of `TRAIN_DBS`: touchdown_db's 4x4 and
  the solver's 6x4, 5x5 and 4x6), checks the evaluation it learns in
  the same way, with its weights as floats rather than the engine's
  hundredths; and `--fit` learns them.
- `solve --check FILE` solves every position of a database, and
  compares them (the test `solve`, on touchdown_db's 4x4).

Of the positions not won at once, the evaluation's sign is right for
85% of 4x4's that can come from the start, 89% of 6x4's, 86% of 5x5's
and 85% of 4x6's, where always guessing the more common value is
right for 60%, 73%, 61% and 54%. On the larger boards it is too
careful: of the positions of 6x4 and 5x5 that it gives 70 to 80%, 95%
are a WIN.

The figures of the README's evaluation are of the 3722 of 4x4, from
touchdown_db's database alone (`TRAIN_DBS=build/gen/touchdown4x4.txt`
for `--fit`):

| Weights                                                 | Right |
|---------------------------------------------------------|-------|
| `make weights`: self-play on every size (and `evaldb`)  | 85%   |
| the same, on every size but 4x4 (`--sizes`)             | 84%   |
| `--fit 100`: 3x2, fit to the database                   | 88%   |
| `--fit 100 --window 3x3`                                | 92%   |

`--fit` shows how much a layout of windows can know of a board at
all. It takes seconds, where `make weights` takes five minutes.

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

Before it searches the moves of a position, a rule of races may know
its value: a pawn is free when no enemy pawn can ever come to a square
from which it blocks or takes it, and no pawn of its own is ahead of
it; the side to move wins if it has a free pawn no slower than every
pawn of the opponent, and loses if the opponent has a free pawn faster
than every pawn of its own. It saves 30 to 40% of the nodes.

`--check FILE` solves every position of a database and compares
them; `--dump FILE SIZE` writes the database of a size (The
databases); `--moves` gives the value of each move at the
root; `--order plain` tries the moves in the order of the move
generator rather than the evaluation's, which on these boards takes
about as many nodes; `--no-races` searches without the rule of races;
`--hash MB` is the size of the table (1024); `--threads N` searches
with N threads, which share the table, each in an order of the moves
of its own, so that their counts are not the same every time.

The figures, from the start, on one core up to 7x6, and with
`--threads 16` above it:

| Size | First player | Nodes   | Time    | Table  |
|------|--------------|---------|---------|--------|
| 4x4  | WIN          | 223     |         |        |
| 5x4  | LOSS         | 1960    |         |        |
| 6x4  | LOSS         | 9843    |         |        |
| 7x4  | WIN          | 52349   |         |        |
| 8x4  | LOSS         | 485908  | 0.5 s   |        |
| 9x4  | WIN          | 2.1 M   | 2.2 s   |        |
| 10x4 | WIN          | 27 M    | 29 s    | 4 GB   |
| 4x5  | LOSS         | 1248    |         |        |
| 5x5  | WIN          | 9274    |         |        |
| 6x5  | LOSS         | 142142  | 0.2 s   |        |
| 7x5  | WIN          | 2.2 M   | 2.5 s   |        |
| 8x5  | WIN          | 23 M    | 29 s    | 4 GB   |
| 4x6  | WIN          | 2621    |         |        |
| 5x6  | WIN          | 95595   | 0.1 s   |        |
| 6x6  | LOSS         | 915215  | 1.1 s   |        |
| 7x6  | LOSS         | 23 M    | 29 s    | 4 GB   |
| 11x4 | LOSS         | 304 M   | 20 s    | 40 GB  |
| 9x5  | WIN          | 708 M   | 53 s    | 40 GB  |
| 8x6  | WIN          | 1.3 G   | 102 s   | 40 GB  |
| 12x4 | WIN          | 1.4 G   | 97 s    | 40 GB  |
| 10x5 | WIN          | 18 G    | 32 min  | 16 GB  |

12x4 was solved too on one core without the rule of races, in 18
minutes with a table of 32 GB, also a WIN. 10x5 had a table that was
full: the larger the board, the more of its positions the table loses
to the replacement and searches again.

A proof-number search (df-pn) was tried too, and dropped: it agreed
with the others, but took 3 to 10 times the nodes of the depth-first
search, with about as many positions in the table, as on these boards
most of the work is to show that every move of a lost position loses,
where the order of the search does not help.

## The opening book

`src/engine/book.c` has the value of the start of each size that the
solver solves, and all the moves that win: White's, or, if White
loses, Black's replies to each move of White's. `tools/solve.c --book FILE
SIZE...` solves them and writes `book.h`; `make book` (`BOOK_SIZES`,
`BOOK_ARGS`) writes it into `src/engine/book.h`, which is in git. A
move wins if it ends the game, or if the solver's table, from the
solve of the start, has its position as a LOSS, or else if the solver
solves it as one. It takes 32 minutes on 16 cores, 23 of them for 10x5; a size solved
since goes into `BOOK_SIZES`.

The search gives the book's value, `ENGINE_BOOK_WIN` or its negative,
only when it proves neither a Win nor a Loss itself, and then plays one
of the book's moves, if a WIN, at random by the seed. The test `book` (`tests/book.input`, on the
host, in niceemu, and on the board) searches at depth 1, which proves
nothing, the start of each size and, where White loses, the position
after each of White's first moves, and plays the book's move; and 9x6,
which the book has not. That the moves win, the solver found.

## The probability of a win

The engine shows a score that is not proved as the probability that
the side to move wins, by a table of the score (`percent[]` of
`src/engine/search.c`). `make calibrate` fits it:
`tools/calibrate.c` plays 200 games on each of 8x6, 6x6, 8x5, 10x4
and 12x6, at depth 6, from 0 to 4 moves at random, and prints the
positions from ply 8 on whose score is not proved, with the scores of
the search at depths 1 to 8. `tools/calibrate.py` solves each with the
solver, 16 at a time and in at most 30 s each, and keeps the values in
`build/calibrate/values.txt`, so that a run again solves only the new
(`--cached`, none). It prints the log loss of the probability as the
score is, as the engine's table has it, and as the best logistic
`1 / (1 + e^-(a * score / 100 + b))` has it, for each size and depth;
how often a position is a WIN for each tenth of the probability shown
at depth 6; and a new table, the probability that only rises with the
score and fits the values best (isotonic regression), of the scores at
depths 4 to 8, for `src/engine/search.c`. After a new table, the tests
that show a probability have new expected outputs.

The table of now is from 12993 positions, solved of 18525 in 30 s
each (12x6 3135 of 6991, the others most), with 62053 scores; a full
run takes about 2 hours on 16 cores, most of it in the positions of
12x6 that are not solved in time. Its log loss, and that of the score
as it is and of the best logistic:

| Depth | As it is | Best logistic | The table |
|-------|----------|---------------|-----------|
| 4     | 0.6355   | 0.6136        | 0.6110    |
| 6     | 0.5690   | 0.5236        | 0.5181    |
| 8     | 0.5566   | 0.5003        | 0.4968    |

The best logistic has a of 1.8 at depth 4 to 2.5 at depth 8, and b of
0.3 to 0.5: the score is too careful, more so the deeper, and leaves
out the move. A logistic is too steep in the middle and not steep
enough at the ends, where the table is right: at depth 6, positions
that a = 2.5, b = 0.5 shows as 80 to 90% are a WIN 74% of the time.
On 8216 positions of other games, the table is within 4 points in
each tenth at depth 6.

## The evaluation and its trainer

`tools/train.c` learns the weights of the engine's evaluation
(`src/engine/eval.c`), an N-tuple network, by TD(λ) self-play on the
host, on boards of the sizes of `--sizes`, and checks them against
the databases of `--db` (The databases): how often the sign of the
evaluation is the database's value, and how often its move keeps a
win. `--fit EPOCHS` learns the databases themselves instead, which
shows how much the windows can know at all. Its comment has the
details.

`make train TRAIN_ARGS="..." TRAIN_DBS="..."` is for experiments. `make weights` learns
the engine's weights with `WEIGHTS_ARGS` and writes them to
`src/engine/eval_weights.h`, which is in git, with the command line
that made them; a run is the same every time. After it, the tests that
search, and those of `evaldb`, have new expected outputs.

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
