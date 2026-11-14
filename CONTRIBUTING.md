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

To use local copies of nice_cpu and niceemu instead, pass `NICE_CPU=DIR`
and `NICEEMU=PATH` to make.

## The tests

`tools/runtest.sh` runs a program with an input file as its console,
on the host or in niceemu, keeps the lines of the output that match
`tests/CHECK.grep`, and compares them, with main's exit status, with
`tests/CHECK.expected`. The MEGA65 build runs in niceemu too, behind
nice_cpu's stand-in for the monitor.
