#!/usr/bin/env python3
"""Generates the Gemdive app icon (256x256 RGBA PNG): a digger tunnelling through dirt
beside a boulder and a handful of gems, drawn as chunky bevelled pixel-art tiles to match the
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
DIRT = (104, 68, 46)
DIRT_DARK = (80, 50, 36)
DIRT_LIGHT = (134, 90, 60)
BOULDER = (150, 150, 172)
GOLD = (255, 208, 80)
EYE = (21, 16, 43)

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


def gem(c, x, y, px, col):
    art = ["..XXXX..", ".XXXXXX.", "XXXXXXXX", "XXXXXXXX", ".XXXXXX.", "..XXXX..", "...XX..."]
    for r, line in enumerate(art):
        for k, ch in enumerate(line):
            if ch != "X":
                continue
            colr = lighten(col) if r <= 1 else (darken(col) if (k >= 5 or r >= 4) else col)
            c.rect(x + k * px, y + r * px, px, px, colr)


def main():
    c = Canvas()
    c.rect(0, 0, SIZE, SIZE, DIRT)
    # earth texture
    for i in range(70):
        x, y = (i * 67 + 11) % 248, (i * 41 + 5) % 248
        c.rect(x, y, 8, 5, DIRT_DARK if i % 2 else DIRT_LIGHT)
    # a tunnel dug through the middle
    c.rect(0, 120, SIZE, 72, BG)
    c.rect(0, 120, SIZE, 5, darken(BG))

    # gems along the tunnel and above it
    gem(c, 18, 132, 6, (96, 226, 255))
    gem(c, 196, 132, 6, (255, 110, 190))
    gem(c, 60, 30, 6, (120, 240, 150))
    gem(c, 176, 40, 6, (255, 214, 90))

    # a boulder, resting on the tunnel roof
    bx, by, bp = 84, 44, 9
    art = ["..XXXX..", ".XXXXXX.", "XXXXXXXX", "XXXXXXXX", "XXXXXXXX", "XXXXXXXX", ".XXXXXX.", "..XXXX.."]
    for r, line in enumerate(art):
        for k, ch in enumerate(line):
            if ch != "X":
                continue
            colr = lighten(BOULDER) if r + k <= 3 else (darken(BOULDER) if r + k >= 11 else BOULDER)
            c.rect(bx + k * bp, by + r * bp, bp, bp, colr)

    # the digger in the tunnel
    dx, dy, dp = 96, 128, 8
    dig = ["..HHHH..", ".HHLLHH.", "..SSSS..", "..SESS..", ".BBBBBB.", ".BBBBBB.", "..T..T..", ".TT..TT."]
    pal = {"H": GOLD, "L": (255, 250, 200), "S": (240, 200, 170), "E": EYE, "B": (96, 150, 230), "T": (70, 60, 90)}
    for r, line in enumerate(dig):
        for k, ch in enumerate(line):
            if ch != ".":
                c.rect(dx + k * dp, dy + r * dp - 2, dp, dp, pal[ch])

    c.round_corners(36)

    out = os.path.join(os.path.dirname(__file__), "..", "assets", "icons", "gemdive.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    c.write_png(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
