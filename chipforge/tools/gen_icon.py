#!/usr/bin/env python3
"""Generates the Chipforge app icon (256x256 RGBA PNG): a little ghost wearing
headphones, standing on a row of piano keys in front of a tracker grid, drawn
as chunky bevelled pixel-art tiles to match the rest of the arcade. Composed
entirely in code -- nothing hand-drawn or copied. Uses only the Python standard
library (zlib + struct for the PNG encoding).
"""
import os
import struct
import zlib

GRID = 16   # logical pixel-art cells per side
SCALE = 16  # output pixels per cell -> 256x256
SIZE = GRID * SCALE

BG = (21, 16, 43)
BG_CHECK = (26, 21, 54)
RULE = (44, 36, 78)         # the tracker's row and column rules
BEAT = (31, 25, 62)         # every fourth step row, like the editor's beat rows
TEAL = (40, 220, 190)       # notes on the grid, the headphone band
TEAL_DARK = (26, 140, 122)  # the headphone cups
GHOST = (242, 236, 255)
EYE = (21, 16, 43)
KEY_WHITE = (214, 208, 236)
KEY_BLACK = (34, 27, 62)

# One character per cell. '.' is the background (grid showing through),
# 'G' ghost, 'E' eye, 'D' headphone cup, 'N' a note on the grid, 'W' a white
# key, 'B' a black key. The headphone band is drawn in code (see below) because
# it is thinner than a cell.
ART = [
    "N....N.....N.N..",
    "....N......N....",
    "N.........N.....",
    "................",
    "N...GGGGGGGG...N",
    "....GGGGGGGG....",
    "N.DDGGEEGGEEGGDD",
    "..DDGGEEGGEEGGDD",
    "...GGGGGGGGGG...",
    "N..GGGGGGGGGG..N",
    "...GGGGGGGGGG...",
    "N..G.GG..GG.G..N",
    "WBWBWWBWBWBWWBWB",
    "WBWBWWBWBWBWWBWB",
    "WWWWWWWWWWWWWWWW",
    "WWWWWWWWWWWWWWWW",
]


def lighten(c):
    return tuple(v + (255 - v) // 3 for v in c)


def darken(c):
    return tuple(v * 2 // 3 for v in c)


class Canvas:
    def __init__(self):
        self.px = [[(0, 0, 0, 0)] * SIZE for _ in range(SIZE)]

    def rect(self, x, y, w, h, color):
        rgba = color + (255,)
        for yy in range(max(0, y), min(SIZE, y + h)):
            row = self.px[yy]
            for xx in range(max(0, x), min(SIZE, x + w)):
                row[xx] = rgba

    def tile(self, cx, cy, color, inset=1):
        """Bevelled block filling cell (cx, cy), same recipe as the games' DrawTileAt."""
        x, y = cx * SCALE + inset, cy * SCALE + inset
        s = SCALE - inset * 2
        bevel = 3 if s >= 12 else 2
        self.rect(x, y, s, s, color)
        self.rect(x, y, s, bevel, lighten(color))
        self.rect(x, y, bevel, s, lighten(color))
        self.rect(x, y + s - bevel, s, bevel, darken(color))
        self.rect(x + s - bevel, y, bevel, s, darken(color))

    def round_corners(self, radius):
        for y in range(SIZE):
            for x in range(SIZE):
                cx = radius if x < radius else SIZE - 1 - radius if x >= SIZE - radius else x
                cy = radius if y < radius else SIZE - 1 - radius if y >= SIZE - radius else y
                if (x - cx) ** 2 + (y - cy) ** 2 > radius ** 2:
                    self.px[y][x] = (0, 0, 0, 0)

    def write_png(self, path):
        raw = b"".join(b"\x00" + bytes(v for p in row for v in p) for row in self.px)

        def chunk(tag, data):
            body = tag + data
            return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

        with open(path, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n")
            f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)))
            f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
            f.write(chunk(b"IEND", b""))


def main():
    c = Canvas()
    c.rect(0, 0, SIZE, SIZE, BG)
    for y in range(GRID):
        for x in range(y & 1, GRID, 2):
            c.rect(x * SCALE, y * SCALE, SCALE, SCALE, BG_CHECK)

    # The tracker behind everything: a rule under every step row and one
    # between each of the four tracks, so the empty cells read as a grid.
    for y in range(0, 12):
        c.rect(0, y * SCALE + SCALE - 1, SIZE, 1, RULE)
    for x in (4, 8, 12):
        c.rect(x * SCALE - 1, 0, 2, 12 * SCALE, RULE)
    # Every fourth row is a beat: a touch brighter than the rest.
    for y in (0, 4, 8):
        c.rect(0, y * SCALE, SIZE, SCALE, BEAT)

    for r, line in enumerate(ART):
        for col, ch in enumerate(line):
            if ch == "N":
                # A note on the grid: a small teal block inside the cell.
                c.rect(col * SCALE + 4, r * SCALE + 5, SCALE - 8, SCALE - 10, TEAL)
            elif ch == "G":
                # Ghost cells are drawn flush (no inset) so the body is one shape.
                c.rect(col * SCALE, r * SCALE, SCALE, SCALE, GHOST)
            elif ch == "E":
                c.rect(col * SCALE, r * SCALE, SCALE, SCALE, GHOST)
                c.rect(col * SCALE + 2, r * SCALE + 2, SCALE - 4, SCALE - 4, EYE)
            elif ch == "D":
                c.tile(col, r, TEAL_DARK, inset=0)
            elif ch == "W":
                c.tile(col, r, KEY_WHITE)
            elif ch == "B":
                c.tile(col, r, KEY_BLACK)

    # The headphone band: an 8px arc over the head that steps outward and
    # down into each cup. Drawn as rectangles (left side, then mirrored).
    band = [
        (4 * SCALE, 3 * SCALE + 4, 8 * SCALE, 8),         # across the top
        (3 * SCALE + 4, 3 * SCALE + 4, 12, 8),            # left shoulder
        (3 * SCALE + 4, 3 * SCALE + 4, 8, 24),            # down the side of the head
        (2 * SCALE + 4, 4 * SCALE + 8, 16, 8),            # step outward
        (2 * SCALE + 4, 4 * SCALE + 8, 8, 2 * SCALE - 8), # down into the cup
    ]
    for x, y, w, h in band:
        c.rect(x, y, w, h, TEAL)
        c.rect(SIZE - x - w, y, w, h, TEAL)
    # Round the ghost's top corners with the background so the head is not a box.
    c.rect(4 * SCALE, 4 * SCALE, 4, 4, BG)
    c.rect(12 * SCALE - 4, 4 * SCALE, 4, 4, BG)
    # A little shine on the band and the ghost's brow.
    c.rect(5 * SCALE, 3 * SCALE + 5, 6 * SCALE, 2, lighten(TEAL))
    c.rect(5 * SCALE, 4 * SCALE + 3, 3 * SCALE, 3, (255, 255, 255))

    c.round_corners(36)

    out = os.path.join(os.path.dirname(__file__), "..", "assets", "icons", "chipforge.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    c.write_png(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
