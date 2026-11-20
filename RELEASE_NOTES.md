# Release notes

The releases of nice_touchdown, newest first. Each section lists the
changes since the one before that a player or a user of the builds can
see. [doc/RELEASE.md](doc/RELEASE.md) says how a release is made.

A release is named `Vmajor.minor`, and its files are
`nice_touchdown-Vmajor.minor.tar.gz` and `SHA256SUMS`. Each section
names the nice_cpu tag the release is built with, and the NiceMonMEGA65
release it was checked on.

## V1.2 (not yet released)

**The engine**

- The probability of a win on the screen is calibrated: of the
  positions it shows as 70 to 80%, about three in four are a win,
  where V1.1's were nearly all a win, and a score of 0 shows 61% for
  the side to move, as it has the move, where V1.1 showed 50%. The
  table is fitted to 13000 positions of games on 6x6 to 12x6 whose
  value the solver of the host finds; on positions of other games it
  is within 4 points in each tenth. The moves the engine plays are
  the same.
- An opening book of the start of the 21 sizes that are solved, from
  4x4 to 12x4, 10x5 and 8x6: there the score is `Win` or `Loss`,
  exact, and the engine plays one of the moves that win, at random:
  the first two moves of the side that wins, against each reply, but
  not those from which the engine's search fails to win more often
  than from the best. The default, 8x6, is a win for White.

## V1.1 (2026-11-19)

An evaluation, learned by self-play, which makes the engine much
stronger at the same time a move. The release has the windowed front
end only.

**What it needs**

- The MEGA65 build, `mega65/touchdown.bin`, needs the monitor of
  NiceMonMEGA65 V2.0.1 or later, which loads files by their long
  names. From the SD card, with V2.2.1 or later, whose commands are
  BASIC's: `load "touchdown.bin"`, then `sys 0x8000`; before V2.2.1,
  `load touchdown.bin`, then `r`.
- The build for niceemu, `niceemu/td_screen.bin`, needs niceemu's
  window build of nice_cpu V2.0 or later.

**Changes**

- The MEGA65 build is `touchdown.bin`, where it was `TD.BIN`.
- The text front end is no longer released or supported: V1.0's
  `TDT.BIN`, `niceemu/td_text.bin`, and the host build
  `nice_touchdown` are gone.

**The engine**

- An evaluation learned by self-play: an N-tuple network of windows of
  3 files by 2 ranks, whose weights hold for every size of the board.
  It wins 95 to 99% of the games against V1.0's search at the same
  depth, and 89 to 94% at depth 6 against V1.0's depth 8.
- The scores on the screen are Win in N, Loss in N, and the
  evaluation's probability of a win, such as `62%`, where V1.0 showed
  Even; among the moves of the best probability the engine plays one
  at random.
- The depth is 6 at first, which takes 0.5 seconds a move on the
  MEGA65 on 8x6 and 3 seconds on 12x6, as V1.0's depth 8 did.

Built with nice_cpu V2.0, and checked on a MEGA65 R6 with
NiceMonMEGA65 V2.3.2.

## V1.0 (2026-11-19)

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

Built with nice_cpu V2.0, and checked on a MEGA65 R6 with
NiceMonMEGA65 V2.3.2.
