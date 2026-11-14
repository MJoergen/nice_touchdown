# CLAUDE.md

nice_touchdown is the game of TouchDown, in C, for the NICE CPU
(nice_cpu). It runs in niceemu, on the MEGA65 under NiceMonMEGA65's
monitor, and on the host. README.md is the design. Its structure
follows nice_chess (`../nice_chess`), which is the model for the
Makefile, the tests, the platform layer, and the front ends.

## Layout

- `src/`: the code; `src/README.md` says what each file does.
  `src/engine/` is the core: no I/O, no clock, no malloc, so the same
  files build for NICE and for the host. `src/platform/` and `src/ui/`
  are around it.
- `tests/`: the inputs of the tests, `*.grep` (the lines of the output
  that are compared) and `*.expected`. `tools/runtest.sh` runs one test.
  `perftsuite.c` checks the move generator against a slow one, and
  `tablebase.c` the search against touchdown_db's 4x4 database; after a
  change to the engine, both must still pass.
- `tools/`: `runtest.sh`; `mockup.py` draws the mockups of the README.
- `CONTRIBUTING.md`: building and the tests. Keep it, README.md, and
  `make help` in sync with the Makefile.
- `deps/` (fetched by `make deps`) and `build/` are not in git.
  `deps/nice_cpu-TAG` is nice_cpu at the pinned tag: the translator,
  the runtime and libc, and the docs of the CPU and toolchain
  (`doc/ISA.md`, `doc/TOOLCHAIN.md`).

## Commands

    make                # help: the targets
    make deps           # nice_cpu and niceemu into deps/
    make all            # build everything
    make test           # host and niceemu tests; run after every change
    make perft-deep     # the perft suite deeper, on the host; in CI too

## Conventions

- The engine must keep working on NICE and on the host. The host build
  has asserts; the NICE builds are `-O 2 -D NDEBUG --no-bounds-checks`,
  so an access out of bounds is caught only on the host.
- Speed is measured in niceemu steps, which are deterministic. Report
  figures as steps, and as seconds on the board at about 54 million
  steps a second.
