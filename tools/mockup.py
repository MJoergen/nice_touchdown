#!/usr/bin/env python3
"""mockup.py - draws a proposed screen of the windowed front end as a PNG.

Usage: mockup.py [--font FONT.zig] [--cols N] [--rows N] OUTPUT.png

The screen is niceemu's 80x25 characters of 16x32 pixels, in the font
of nice_cpu's emulator (Spleen 16x32, emulator/src/font.zig), the same
as nice_chess. The pawns are 3x3 glyphs, drawn as nice_chess's
tools/mkpieces.py draws its pawn: white as an outline, black filled.
The game in the picture is played out here, move by move, so the board
and the list of moves agree. This is a mockup of a planned screen, not
a dump of a running program. Needs Pillow.
"""

from __future__ import annotations

import argparse
import re

from PIL import Image, ImageChops, ImageDraw, ImageFilter

COLS, ROWS, CW, CH = 80, 25, 16, 32
PX = 42  # The first column of the right half, as in nice_chess

# Colors, 12 bits each, as in nice_chess's src/ui/screen.c.
SQ_LIGHT, SQ_DARK = 0xCA7, 0x863
LAST_LIGHT, LAST_DARK = 0xDC5, 0xAA3
SEL_LIGHT, SEL_DARK = 0x7C7, 0x5A5  # The cursor, on narrow squares
INK, MARK, PANEL = 0x111, 0xF11, 0x123
TEXT, BRIGHT, DIM, WARN = 0xCCC, 0xFFF, 0x89A, 0xFA4

# The pawn of nice_chess's tools/mkpieces.py, in pixels of 48x96.
PAWN = [("rect", (5, 82, 43, 90)), ("rect", (9, 76, 39, 82)),
        ("poly", [(13, 76), (35, 76), (29, 52), (19, 52)]),
        ("ellipse", (12, 47, 36, 55)), ("ellipse", (14, 26, 34, 48))]
PW, PH, S, LINE, GAP = 48, 96, 8, 2, 2

# The game shown, White first. White's pawns start on rank 1, Black's
# on the last rank.
GAME = {
    (8, 6): "d1d2 e6e5 d2d3 e5e4 e1e2 d6d5 d3e4 d5e4 f1f2 c6c5 "
            "f2f3 e4f3 e2f3 c5c4 b1b2 c4c3 b2c3 b6b5 c3c4 b5b4",
    (12, 6): "f1f2 g6g5 f2f3 g5g4 f3g4 h6h5 g4h5 i6h5 c1c2 b6b5 "
             "j1j2 k6k5",
}


def load_font(path: str) -> list[list[int]]:
    glyphs = []
    for line in open(path, encoding="utf-8"):
        m = re.match(r"\s*\.\{(.*)\},\s*//", line)
        if m:
            glyphs.append([int(v, 16) for v in m.group(1).split(",")])
    if len(glyphs) != 256:
        raise SystemExit(f"mockup.py: {path} has {len(glyphs)} glyphs, not 256")
    return glyphs


def pawn_masks() -> tuple[Image.Image, Image.Image]:
    """The white pawn (outlines) and the black pawn (filled), 48x96."""
    def shape(kind, arg):
        im = Image.new("1", (PW * S, PH * S), 0)
        d = ImageDraw.Draw(im)
        if kind == "poly":
            d.polygon([(x * S, y * S) for x, y in arg], fill=1)
        elif kind == "ellipse":
            d.ellipse([v * S for v in arg], fill=1)
        else:
            d.rectangle([v * S for v in arg], fill=1)
        return im

    def erode(im, px):
        im = im.convert("L")
        for _ in range(px):
            im = im.filter(ImageFilter.MinFilter(3))
        return im.convert("1")

    def down(im):
        small = im.convert("L").resize((PW, PH), Image.Resampling.BOX)
        return small.point(lambda v: 255 if v >= 128 else 0).convert("1")

    lines = Image.new("1", (PW * S, PH * S), 0)
    gaps = Image.new("1", (PW * S, PH * S), 0)
    whole = Image.new("1", (PW * S, PH * S), 0)
    for kind, arg in PAWN:
        m = shape(kind, arg)
        whole = ImageChops.logical_or(whole, m)
        notm = ImageChops.invert(m)
        lines = ImageChops.logical_or(ImageChops.logical_and(lines, notm),
                                      ImageChops.logical_and(m, ImageChops.invert(erode(m, LINE * S))))
        gaps = ImageChops.logical_or(ImageChops.logical_and(gaps, notm),
                                     ImageChops.logical_and(m, ImageChops.invert(erode(m, GAP * S))))
    inside = erode(whole, (LINE + 1) * S)
    black = ImageChops.logical_and(whole, ImageChops.invert(ImageChops.logical_and(gaps, inside)))
    return down(lines), down(black)


def play(cols: int, rows: int, moves: list[str]) -> dict[tuple[int, int], int]:
    """The board after the moves: (file, rank) -> +1 White, -1 Black."""
    board = {(f, 0): 1 for f in range(cols)} | {(f, rows - 1): -1 for f in range(cols)}
    side = 1
    for mv in moves:
        m = re.fullmatch(r"([a-l])(\d)([a-l])(\d)", mv)
        assert m, mv
        f0, r0 = ord(m[1]) - 97, int(m[2]) - 1
        f1, r1 = ord(m[3]) - 97, int(m[4]) - 1
        assert board.get((f0, r0)) == side, f"{mv}: no pawn of the side to move"
        assert r1 == r0 + side and abs(f1 - f0) <= 1 and 0 <= f1 < cols, f"{mv}: not forward"
        if f1 == f0:
            assert (f1, r1) not in board, f"{mv}: blocked"
        else:
            assert board.get((f1, r1)) == -side, f"{mv}: nothing to capture"
        del board[(f0, r0)]
        board[(f1, r1)] = side
        assert r1 not in (0, rows - 1), f"{mv}: the game is over"
        side = -side
    return board


class Screen:
    def __init__(self) -> None:
        self.cells = [[(PANEL, TEXT, " ")] * COLS for _ in range(ROWS)]
        self.custom: dict[str, list[int]] = {}

    def put(self, col, row, bg, fg, ch) -> None:
        self.cells[row][col] = (bg, fg, ch)

    def text(self, col, row, fg, s) -> int:
        for ch in s:
            self.put(col, row, PANEL, fg, ch)
            col += 1
        return col

    def render(self, font, path) -> None:
        def rgb(c):
            return ((c >> 8 & 15) * 17, (c >> 4 & 15) * 17, (c & 15) * 17)
        img = Image.new("RGB", (COLS * CW, ROWS * CH))
        px = img.load()
        for r in range(ROWS):
            for c in range(COLS):
                bg, fg, ch = self.cells[r][c]
                g = self.custom[ch] if ch in self.custom else font[ord(ch)]
                b, f = rgb(bg), rgb(fg)
                for y in range(CH):
                    for x in range(CW):
                        px[c * CW + x, r * CH + y] = f if g[y] >> (15 - x) & 1 else b
        img.save(path)


def main() -> None:
    ap = argparse.ArgumentParser(description="Draws a proposed screen as a PNG.")
    ap.add_argument("png")
    ap.add_argument("--font", default="../nice_chess/deps/nice_cpu-V2.0/emulator/src/font.zig")
    ap.add_argument("--cols", type=int, default=8)
    ap.add_argument("--rows", type=int, default=6)
    args = ap.parse_args()
    cols, rows = args.cols, args.rows
    moves = GAME[(cols, rows)].split()

    font = load_font(args.font)
    scr = Screen()
    # The pawns as 9 glyphs each, named by private characters.
    for side, mask in zip("WB", pawn_masks()):
        for cy in range(3):
            for cx in range(3):
                scr.custom[f"{side}{cy}{cx}"] = [
                    sum(0x8000 >> x for x in range(16) if mask.getpixel((cx * 16 + x, cy * 32 + y)))
                    for y in range(32)]

    # The board: squares 5 characters wide up to 8 columns, else 3, and
    # 3 high; in the middle of the left half (columns 0 to 41, rows 0 to 24).
    board = play(cols, rows, moves)
    sw = 5 if cols <= 8 else 3
    left = 1 + (40 - sw * cols) // 2
    top = (24 - 3 * rows) // 2
    last = moves[-1]
    lsq = {(ord(last[0]) - 97, int(last[1]) - 1), (ord(last[2]) - 97, int(last[3]) - 1)}
    cursor = (2, 3) if cols == 8 else (9, 1)
    for rank in range(rows):
        row = top + 3 * (rows - 1 - rank)
        scr.put(left - 1, row + 1, PANEL, DIM, str(rank + 1))
        for f in range(cols):
            col = left + sw * f
            light = (f + rank) & 1
            bg = (LAST_LIGHT if light else LAST_DARK) if (f, rank) in lsq else (SQ_LIGHT if light else SQ_DARK)
            if (f, rank) == cursor and sw == 3:
                bg = SEL_LIGHT if light else SEL_DARK
            for r in range(3):
                for c in range(sw):
                    scr.put(col + c, row + r, bg, INK, " ")
            p = board.get((f, rank))
            if p:
                side = "W" if p > 0 else "B"
                o = (sw - 3) // 2
                for r in range(3):
                    for c in range(3):
                        scr.put(col + o + c, row + r, bg, INK, f"{side}{r}{c}")
            if (f, rank) == cursor and sw == 5:
                scr.put(col, row + 1, bg, MARK, "[")
                scr.put(col + 4, row + 1, bg, MARK, "]")
    for f in range(cols):
        scr.put(left + sw * f + sw // 2, top + 3 * rows, PANEL, DIM, chr(97 + f))

    # The right half.
    scr.text(PX, 0, BRIGHT, "nice_touchdown V0.1")
    scr.text(PX, 1, BRIGHT, "White  you              0:41")
    scr.text(PX, 2, TEXT, "Black  nice_touchdown   0:03")
    scr.text(PX, 3, BRIGHT, "White to move.")
    scr.text(PX, 4, DIM, f"Board {cols}x{rows}, depth 8")
    if cols == 8:
        scr.text(PX, 6, TEXT, "Depth 8  Score Even  Nodes 24817")
        scr.text(PX, 7, DIM, "PV b5b4 c4c5 a6a5 a1a2")
    else:
        scr.text(PX, 6, TEXT, "Depth 8  Score Even  Nodes 91352")
        scr.text(PX, 7, DIM, "PV k6k5 j2j3 l6l5 i1i2")
    scr.text(PX, 9, DIM, "Moves")
    pairs = [moves[i:i + 2] for i in range(0, len(moves), 2)]
    for i, pr in enumerate(pairs):
        h, r = divmod(i, 9)
        col = PX + 19 * h
        col = scr.text(col, 10 + r, DIM, f"{i + 1:3d}. ")
        scr.text(col, 10 + r, TEXT, "  ".join(pr))
    col = scr.text(PX, 20, DIM, "Your move: ")
    scr.text(col, 20, BRIGHT, "c4c5" if cols == 8 else "")
    scr.text(PX, 22, DIM, "Arrows+Enter or a1a2: move  Q quit")
    scr.text(PX, 23, DIM, "U undo  S switch  R rotate  N new")
    scr.text(PX, 24, DIM, "+/- depth  X width  Y height")
    scr.render(font, args.png)


if __name__ == "__main__":
    main()
