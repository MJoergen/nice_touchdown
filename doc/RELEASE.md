# Releasing nice_touchdown

The steps of a release, in order. Do not go on while a step fails.

## Version names

A release is named `Vmajor.minor`: `V1.0`, `V1.1`, and so on. The
front ends show the version: the banner of the text front end and the
title of the windowed one. A build of a tagged commit shows the tag;
other builds show what `git describe` says, with `-dirty` for a changed
tree. The release workflow sets `VERSION` to the tag.

## What a release has

- An annotated tag, whose message is the tag's section of
  [RELEASE_NOTES.md](../RELEASE_NOTES.md).
- A GitHub release with that section as its text, and two files:
  - `nice_touchdown-V1.0.tar.gz`, with `nice_touchdown-V1.0/`:
    - `mega65/TD.BIN` and `mega65/TDT.BIN`, the windowed and the text
      front ends for the MEGA65, under NiceMonMEGA65's monitor, with
      8.3 names for its `load` from the SD card;
    - `niceemu/td_text.bin` and `niceemu/td_screen.bin`, the front
      ends for niceemu;
    - `linux-x86_64/nice_touchdown`, the text front end on the host,
      without asserts and linked statically;
    - `README.md`, `RELEASE_NOTES.md`, and `LICENSE`.
  - `SHA256SUMS`, the SHA-256 sum of the archive.

`make dist` makes both in `build/dist/`.

## 1. Prepare

The branch is `main`, the working tree is clean, `HEAD` is
`origin/main`, and the last run of the test workflow on it passed.

## 2. Test on the host and in niceemu

    make deps test perft-deep size

## 3. Test on the board

On a MEGA65 under NiceMonMEGA65's monitor, at the release the notes
will name, with a checkout of NiceMonMEGA65 (`NICEMON=DIR`,
`../NiceMonMEGA65` by default):

    make board-test ETH=IP        # or PORT=/dev/ttyUSBn

Then play a game in the windowed front end on its screen and keyboard,
`make board-screen ETH=IP`, and check what the tests cannot: Esc while
the engine thinks makes it move, INST/DEL erases a typed move, and Q
returns to the monitor.

## 4. Date the notes

Give the release's section of RELEASE_NOTES.md its date, as
`## V1.0 (2026-11-20)`, and name the NiceMonMEGA65 release it was
checked on. Commit and push it, and wait for the test workflow to pass.
`tools/notes.sh V1.0` must print the section.

## 5. Tag

    tools/notes.sh V1.0 > /tmp/notes.md
    git tag -a V1.0 -F /tmp/notes.md
    git push origin V1.0

The push runs the release workflow, which tests the tagged commit,
makes the archive with the tag as its version, and creates the GitHub
release. Check its page, and that `sha256sum -c SHA256SUMS` passes on
the downloaded archive.
