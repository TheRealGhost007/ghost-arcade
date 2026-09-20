#!/usr/bin/env python3
"""Generates the Moondrop app icon (256x256 RGBA PNG): a lander coming down onto a
lit landing pad over a jagged ridge, in glowing vector lines, drawn as chunky bevelled pixel-art tiles to match the
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
LIME = (255, 184, 64)
GLOW = (96, 62, 20)
PAD = (255, 244, 190)
WHITE = (242, 236, 255)


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


def line(c, x0, y0, x1, y1, th, color):
    """Thick line as a run of square dabs: chunky, and stays in the pixel world."""
    n = int(max(abs(x1 - x0), abs(y1 - y0)))
    for i in range(n + 1):
        t = i / max(1, n)
        x = int(x0 + (x1 - x0) * t)
        y = int(y0 + (y1 - y0) * t)
        c.rect(x - th // 2, y - th // 2, th, th, color)


def poly(c, pts, th, color, closed=True):
    for i in range(len(pts) - (0 if closed else 1)):
        a, b = pts[i], pts[(i + 1) % len(pts)]
        line(c, a[0], a[1], b[0], b[1], th, color)


def main():
    c = Canvas()
    c.rect(0, 0, SIZE, SIZE, BG)
    for i in range(24):
        x, y = (i * 97 + 13) % 240 + 8, (i * 53 + 7) % 130 + 8
        c.rect(x, y, 3, 3, GLOW if i % 3 else WHITE)

    # A jagged ridge with one flat pad, lit.
    ridge = [(0, 200), (30, 176), (58, 208), (86, 190), (112, 214), (150, 214), (176, 196), (208, 222), (230, 186), (256, 204)]
    for a, b in zip(ridge, ridge[1:]):
        if (a, b) == ((112, 214), (150, 214)):
            line(c, a[0], a[1], b[0], b[1], 6, PAD)
        else:
            line(c, a[0], a[1], b[0], b[1], 5, LIME)
    line(c, 112, 214, 112, 202, 4, PAD)
    line(c, 150, 214, 150, 202, 4, PAD)

    # The lander, a little tilted, coming down over the pad with its flame lit.
    cx, cy = 132, 132
    hull = [(-16, -22), (16, -22), (28, -6), (28, 16), (-28, 16), (-28, -6)]
    poly(c, [(cx + x, cy + y) for x, y in hull], 5, WHITE)
    poly(c, [(cx + x, cy + y) for x, y in [(-9, -14), (9, -14), (9, -2), (-9, -2)]], 4, WHITE)
    line(c, cx - 28, cy + 16, cx - 24, cy + 46, 5, WHITE)
    line(c, cx + 28, cy + 16, cx + 24, cy + 46, 5, WHITE)
    line(c, cx - 38, cy + 46, cx - 14, cy + 46, 5, WHITE)
    line(c, cx + 14, cy + 46, cx + 38, cy + 46, 5, WHITE)
    line(c, cx - 8, cy + 18, cx, cy + 40, 4, (255, 150, 70))
    line(c, cx + 8, cy + 18, cx, cy + 40, 4, (255, 150, 70))

    c.round_corners(36)

    out = os.path.join(os.path.dirname(__file__), "..", "assets", "icons", "moondrop.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    c.write_png(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
