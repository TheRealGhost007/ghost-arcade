#!/usr/bin/env python3
"""Generates the Coilrush app icon (256x256 RGBA PNG): a snake coiled into a
spiral around a berry, drawn as chunky bevelled pixel-art tiles to match the
in-game look. Composed entirely in code -- nothing hand-drawn or copied.
Uses only the Python standard library (zlib + struct for the PNG encoding).
"""
import os
import struct
import zlib

GRID = 16   # logical pixel-art cells per side
SCALE = 16  # output pixels per cell -> 256x256
SIZE = GRID * SCALE

BG = (22, 24, 34)
BG_CHECK = (28, 31, 44)
HEAD = (140, 240, 150)
TAIL = (36, 140, 116)
FOOD = (240, 90, 100)
STEM = (96, 220, 130)
EYE = (14, 15, 22)


def spiral_path():
    """Cells from tail (outside) to head (centre), one cell of gap between coils."""
    corners = [(2, 2), (13, 2), (13, 13), (2, 13), (2, 4), (11, 4), (11, 11),
               (4, 11), (4, 6), (9, 6), (9, 9), (6, 9)]
    path = [corners[0]]
    for (x0, y0), (x1, y1) in zip(corners, corners[1:]):
        dx = (x1 > x0) - (x1 < x0)
        dy = (y1 > y0) - (y1 < y0)
        x, y = x0, y0
        while (x, y) != (x1, y1):
            x, y = x + dx, y + dy
            path.append((x, y))
    return path


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

    path = spiral_path()
    for i, (x, y) in enumerate(path):
        t = i / (len(path) - 1)  # 0 = tail, 1 = head
        c.tile(x, y, lerp(TAIL, HEAD, t))

    # Head faces left (the last spiral leg runs right-to-left): eyes on its leading edge.
    hx, hy = path[-1]
    c.rect(hx * SCALE + 3, hy * SCALE + 3, 3, 3, EYE)
    c.rect(hx * SCALE + 3, hy * SCALE + SCALE - 6, 3, 3, EYE)

    # The berry it is coiling toward.
    fx, fy = 7, 8
    c.tile(fx, fy, FOOD, inset=2)
    c.rect(fx * SCALE + SCALE // 2 - 1, fy * SCALE, 3, 3, STEM)

    c.round_corners(36)

    out = os.path.join(os.path.dirname(__file__), "..", "assets", "icons", "coilrush.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    c.write_png(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
