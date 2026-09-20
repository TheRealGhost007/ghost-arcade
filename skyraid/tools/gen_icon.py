#!/usr/bin/env python3
"""Generates the Skyraid app icon (256x256 RGBA PNG): a ghost and two bats
bearing down on the player's ship and its single shot, drawn as chunky bevelled pixel-art tiles to match the
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
GHOST = (150, 244, 160)
BAT = (255, 79, 163)
SHIP = (242, 236, 255)
SHOT = (64, 200, 255)
STAR = (120, 112, 170)

GHOST_ART = [
    "...XXXXXX...", "..XXXXXXXX..", ".XX.XXXX.XX.", ".XX.XXXX.XX.",
    ".XXXXXXXXXX.", ".XXXXXXXXXX.", ".XXXXXXXXXX.", ".X.XX..XX.X.",
]
BAT_ART = [
    "X..........X", "XX........XX", "XXX.X..X.XXX", "XXXXXXXXXXXX",
    ".XXXX..XXXX.", "..XXXXXXXX..", "...XX..XX...",
]


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


def stamp(c, art, ox, oy, px, color):
    for r, line in enumerate(art):
        for col, ch in enumerate(line):
            if ch == "X":
                c.rect(ox + col * px, oy + r * px, px, px, color)


def main():
    c = Canvas()
    c.rect(0, 0, SIZE, SIZE, BG)
    for y in range(GRID):
        for x in range(y & 1, GRID, 2):
            c.rect(x * SCALE, y * SCALE, SCALE, SCALE, BG_CHECK)
    for sx, sy in [(30, 40), (210, 30), (60, 150), (226, 120), (24, 100), (190, 190), (120, 22)]:
        c.rect(sx, sy, 4, 4, STAR)

    # One big ghost bearing down, two bats flanking it above.
    stamp(c, BAT_ART, 22, 34, 6, BAT)
    stamp(c, BAT_ART, 162, 34, 6, BAT)
    stamp(c, GHOST_ART, 56, 78, 12, GHOST)

    # The ship and its one shot.
    c.rect(124, 176, 8, 22, SHOT)
    c.rect(122, 208, 12, 8, SHIP)
    c.rect(98, 216, 60, 20, SHIP)
    c.rect(126, 204, 4, 4, SHOT)

    c.round_corners(36)

    out = os.path.join(os.path.dirname(__file__), "..", "assets", "icons", "skyraid.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    c.write_png(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
