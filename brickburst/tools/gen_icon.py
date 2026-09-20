#!/usr/bin/env python3
"""Generates the Brickburst app icon (256x256 RGBA PNG): a staggered wall of
bricks with a hole knocked through it, the ball and the paddle, drawn as chunky bevelled pixel-art tiles to match the
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
BG_CHECK = (26, 21, 54)
ROWS = [(255, 79, 120), (255, 150, 60), (255, 210, 63), (96, 220, 130), (33, 212, 200)]
PADDLE = (242, 236, 255)
BALL = (255, 246, 224)
SPARK = (255, 210, 63)
ORANGE = (255, 150, 60)


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


def main():
    c = Canvas()
    c.rect(0, 0, SIZE, SIZE, BG)
    for y in range(GRID):
        for x in range(y & 1, GRID, 2):
            c.rect(x * SCALE, y * SCALE, SCALE, SCALE, BG_CHECK)

    # Five rows of two-cell-wide bricks, staggered like a real wall, with a
    # hole punched where the ball has just been.
    hole = {(2, 6), (2, 7), (3, 7), (3, 8), (4, 6), (4, 7), (5, 7), (5, 8), (6, 8), (6, 9)}
    for r, color in enumerate(ROWS):
        y = 2 + r
        start = 1 if r % 2 == 0 else 2
        for x in range(1, 15):
            if (y, x) not in hole:
                c.tile(x, y, color, inset=0)
        for x in range(start, 16, 2):  # mortar between bricks
            c.rect(x * SCALE - 1, y * SCALE, 2, SCALE, BG)
        c.rect(SCALE, (y + 1) * SCALE - 1, 14 * SCALE, 2, BG)

    # The ball, flying out of the hole, with a short trail and some debris.
    for i, (bx, by) in enumerate([(8.4, 9.0), (8.0, 9.9), (7.6, 10.8)]):
        s = 14 - i * 4
        c.rect(int(bx * SCALE) - s // 2, int(by * SCALE) - s // 2, s, s, lerp(BALL, BG, i * 0.35))
    for sx, sy in [(6, 8), (10, 8), (9, 7)]:
        c.rect(sx * SCALE + 5, sy * SCALE + 6, 5, 5, SPARK)

    # The paddle, with the machine's orange on its end caps.
    for x in range(5, 11):
        c.tile(x, 13, PADDLE, inset=0)
    c.rect(5 * SCALE, 13 * SCALE, 5, SCALE, ORANGE)
    c.rect(11 * SCALE - 5, 13 * SCALE, 5, SCALE, ORANGE)

    c.round_corners(36)

    out = os.path.join(os.path.dirname(__file__), "..", "assets", "icons", "brickburst.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    c.write_png(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
