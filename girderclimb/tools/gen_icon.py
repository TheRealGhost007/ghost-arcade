#!/usr/bin/env python3
"""Generates the Girderclimb app icon (256x256 RGBA PNG): a giant ghost at the top of a
red girder tower, a barrel rolling down and a little climber below, drawn as chunky bevelled pixel-art tiles to match the
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
GIRDER = (255, 92, 122)
GIRDER_DARK = (148, 36, 72)
GHOST = (226, 232, 255)
BARREL = (168, 104, 60)
BAND = (90, 52, 32)
GOLD = (255, 208, 80)
EYE = (21, 16, 43)
RED = (255, 60, 80)

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


def art(c, rows, x, y, px, pal):
    for r, line in enumerate(rows):
        for k, ch in enumerate(line):
            if ch != "." and ch in pal:
                c.rect(x + k * px, y + r * px, px, px, pal[ch])


def girder(c, x0, y0, x1, y1):
    n = 60
    for i in range(n + 1):
        x = x0 + (x1 - x0) * i // n
        y = y0 + (y1 - y0) * i // n
        c.rect(x - 4, y, 8, 18, GIRDER)
        c.rect(x - 4, y, 8, 3, lighten(GIRDER))
        c.rect(x - 4, y + 15, 8, 3, GIRDER_DARK)


def ring(c, cx, cy, r, th, col):
    import math
    for i in range(72):
        ang = i * 2 * math.pi / 72
        c.rect(int(cx + math.cos(ang) * r) - th // 2, int(cy + math.sin(ang) * r) - th // 2, th, th, col)


def line(c, x0, y0, x1, y1, th, col):
    n = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
    for i in range(n + 1):
        c.rect(int(x0 + (x1 - x0) * i / n) - th // 2, int(y0 + (y1 - y0) * i / n) - th // 2, th, th, col)


def main():
    c = Canvas()
    c.rect(0, 0, SIZE, SIZE, BG)
    # fire at the bottom
    for x in range(0, 256, 16):
        h = 26 + (x * 7) % 34
        c.rect(x, 256 - h, 18, h, (255, 90, 40))
        c.rect(x + 3, 256 - h + 14, 12, h - 14, (255, 170, 60))
    c.rect(0, 244, SIZE, 12, (255, 240, 150))
    # girders up the left, and a ring with a swinging climber
    girder(c, 10, 66, 90, 74)
    girder(c, 166, 150, 246, 142)
    ring(c, 150, 46, 13, 4, (255, 214, 90))
    line(c, 150, 46, 92, 156, 3, (236, 226, 200))
    hero = ["..hhhh..", ".HHHHHH.", "HHHHHHHH", "..SSSS..", "..SESS..", "..SSSS..", ".BBBBBB.", ".BBRRBB.", ".BBBBBB.", ".BBBBBB.", ".bb..bb.", ".bb..bb.", ".TT..TT."]
    art(c, hero, 72, 150, 6, {"H": GOLD, "h": (166, 136, 50), "S": (240, 200, 170), "E": EYE, "B": (88, 140, 235), "b": (60, 100, 190), "R": GIRDER, "T": (70, 60, 90)})
    c.round_corners(36)

    out = os.path.join(os.path.dirname(__file__), "..", "assets", "icons", "girderclimb.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    c.write_png(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
