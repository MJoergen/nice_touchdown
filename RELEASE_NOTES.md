# Release notes

The releases of nice_touchdown, newest first. Each section lists the
changes since the one before that a player or a user of the builds can
see. [doc/RELEASE.md](doc/RELEASE.md) says how a release is made.

A release is named `Vmajor.minor`, and its files are
`nice_touchdown-Vmajor.minor.tar.gz` and `SHA256SUMS`. Each section
names the nice_cpu tag the release is built with, and the NiceMonMEGA65
release it was checked on.

## V1.0 (unreleased)

The first version: TouchDown on boards of 4x4 to 12x6, against an
engine with a fixed-depth search, on the MEGA65's screen and keyboard,
on a console, in niceemu, and on a PC.

**What it needs**

- The MEGA65 builds need the monitor of NiceMonMEGA65 V2.0 or later,
  as they reach the memory through the byte windows, which the monitor
  has from V2.0 on. From the SD card: `load TD.BIN` (the windowed front
  end) or `load TDT.BIN` (the text front end), then `r`.
- The builds for niceemu need niceemu of nice_cpu V2.0 or later, the
  window build for the windowed front end.
- The host build, `nice_touchdown`, is the text front end for x86_64
  Linux, linked statically.

**The game**

- Boards of 4 to 12 files and 4 to 6 ranks, 8x6 at first.
- The windowed front end: moves by the cursor or typed, undo, switch
  sides, rotate the board, a new game, the depth, the width (X), and
  the height (Y), letters in either case, and Esc to make the
  engine move now.
- The text front end: `new WxH`, `position`, `d`, `moves`, moves such
  as `a1a2`, `undo`, `go [depth N]`, `depth`, `seed`, `perft`, and
  `divide`.

**The engine**

- Negamax with alpha-beta pruning to a fixed depth, 8 at first, which
  takes 0.4 seconds a move on the MEGA65 on 8x6 and 3 seconds on 12x6.
- Its scores are Win in N, Loss in N, and Even; it plays the fastest
  win, or the slowest loss, and among Even moves one at random.
- Checked against touchdown_db: at depth 24 it has the value of every
  position of the 4x4 board.

Built with nice_cpu V2.0.
