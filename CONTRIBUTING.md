# Contributing

This file is for anyone who changes the repository: how to build it and
run the tests.

## Building and testing

Needs Python 3.10+, clang with the wasm32 target, `wasm-ld`, `llvm-ar`,
a C compiler for the host, `git`, and `curl`.

    make             # list the targets
    make deps        # nice_cpu V2.0 and niceemu V2.0 into deps/
    make all         # build/nice/ for niceemu, build/mega65/ for the MEGA65, build/host/td_text for the host
    make test        # the tests, on the host and in niceemu
    make perft-deep  # the perft suite to depth 6, and 10000 games a size, on the host (about 2 minutes)

To use local copies of nice_cpu and niceemu instead, pass `NICE_CPU=DIR`
and `NICEEMU=PATH` to make.

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
