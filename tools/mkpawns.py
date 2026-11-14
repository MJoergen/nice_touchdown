#!/usr/bin/env python3
"""mkpawns.py - draws the pawns as glyphs into a C header.

Usage: mkpawns.py OUTPUT.h

The pawns are glyphs of 3x3 characters of 16x32 pixels, as mockup.py
draws them (pawn_masks): White's as an outline, Black's filled, the
pawn of nice_chess's tools/mkpieces.py. The header is in git, so that
a build needs no Pillow; make pawns writes it again. Needs Pillow.
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mockup import pawn_masks  # noqa: E402


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    out = ["// The pawns as glyphs, from tools/mkpawns.py; do not edit.",
           "// White, then Black; the 9 cells of each row by row;",
           "// 32 rows of 16 pixels each, the leftmost in bit 15.",
           "static const uint16_t pawn_glyphs[2 * 9][32] = {"]
    for name, mask in zip("wb", pawn_masks()):
        for cy in range(3):
            for cx in range(3):
                rows = [sum(0x8000 >> x for x in range(16) if mask.getpixel((cx * 16 + x, cy * 32 + y)))
                        for y in range(32)]
                out.append(f"    // {name}P {3 * cy + cx}")
                out.append("    {" + ", ".join(f"0x{r:04x}" for r in rows) + "},")
    out.append("};")
    with open(sys.argv[1], "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
