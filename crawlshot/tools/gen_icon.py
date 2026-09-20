#!/usr/bin/env python3
"""Generates the Crawlshot app icon (256x256 RGBA PNG): a caterpillar winding down through
toadstools toward the shooter, drawn as chunky bevelled pixel-art tiles to match the
in-game look. Composed entirely in code -- nothing hand-drawn or copied.
Uses only the Python standard library (zlib + struct for the PNG encoding).
"""
import os
import struct
import zlib

GRID = 16   # logical pixel-art cells per side
SCALE = 16  # output pixels per cell -> 256x256
SIZE = GRID * SCALE

BG = (21, 16, 43)
FLOOR = (36, 26, 70)
CAP = (240, 96, 120)
STEM = (236, 226, 200)
WORM = (120, 220, 110)
HEAD = (226, 255, 150)
EYE = (21, 16, 43)
LIGHT = (232, 104, 255)
WHITE = (255, 255, 255)

def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


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
        """Bevelled block filling cell (cx, cy), same recipe as render.c's DrawTileAt."""
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


def mushroom(c, x, y, px, cap):
    art = ["..XXXX..", ".XXXXXX.", "XXXXXXXX", "XXXXXXXX", ".XXXXXX.", "...SS...", "...SS...", "...SS..."]
    for r, line in enumerate(art):
        for col, ch in enumerate(line):
            if ch == ".":
                continue
            colr = STEM if ch == "S" else cap
            if ch == "X" and ((r == 2 and col in (2, 5)) or (r == 3 and col == 3)):
                colr = lerp(cap, WHITE, 0.55)
            c.rect(x + col * px, y + r * px, px, px, colr)


def segment(c, x, y, px, body, head=False):
    art = ["..XXXX..", ".XXXXXX.", "XXXXXXXX", "XXXXXXXX", "XXXXXXXX", ".XXXXXX.", "..XXXX.."]
    for r, line in enumerate(art):
        for col, ch in enumerate(line):
            if ch == "X":
                c.rect(x + col * px, y + r * px, px, px, body)
    if head:
        c.rect(x + 4 * px, y + 2 * px, px, 2 * px, EYE)
        c.rect(x + 6 * px, y + 2 * px, px, 2 * px, EYE)
    for col in range(0, 8, 2):
        c.rect(x + col * px, y + 7 * px, px, px, darken(darken(body)))


def main():
    c = Canvas()
    c.rect(0, 0, SIZE, SIZE, BG)
    c.rect(0, 176, SIZE, 80, FLOOR)

    # A few toadstools scattered through the field.
    for (x, y, cap) in ((22, 26, CAP), (200, 22, (92, 170, 255)), (34, 116, (255, 196, 72)), (206, 108, CAP), (118, 30, (176, 112, 255))):
        mushroom(c, x, y, 4, cap)

    # The caterpillar snakes down the middle: right along the top, down, left.
    px = 5
    cells = [(150, 68), (110, 68), (70, 68), (70, 108), (110, 108), (150, 108)]
    for i, (x, y) in enumerate(cells):
        segment(c, x, y, px, HEAD if i == len(cells) - 1 else WORM, head=(i == len(cells) - 1))
    # Its head turned down at the last corner: a cell below it.
    segment(c, 150, 148, px, HEAD, head=True)
    c.rect(150 + 4 * px, 148 + 2 * px, px, 2 * px, EYE)

    # The shooter below, with a shot on its way up.
    ox, oy, spx = 96, 188, 8
    art = ["...XX...", "..XXXX..", "..XWWX..", ".XXXXXX.", "XXXXXXXX", "XX.XX.XX", "X..XX..X"]
    for r, line in enumerate(art):
        for col, ch in enumerate(line):
            if ch != ".":
                c.rect(ox + col * spx, oy + r * spx, spx, spx, (255, 240, 250) if ch == "W" else LIGHT)
    c.rect(ox + 3 * spx + 3, 150 + 40, 10, 30, WHITE)

    c.round_corners(36)

    out = os.path.join(os.path.dirname(__file__), "..", "assets", "icons", "crawlshot.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    c.write_png(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
