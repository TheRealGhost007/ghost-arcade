#!/usr/bin/env python3
"""Generates the Rockdrift app icon (256x256 RGBA PNG): a jagged rock and the
ship that is about to split it, in glowing vector lines, drawn as chunky bevelled pixel-art tiles to match the
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
LIME = (180, 255, 100)
GLOW = (60, 90, 40)
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
    import math
    c = Canvas()
    c.rect(0, 0, SIZE, SIZE, BG)
    for y in range(GRID):
        for x in range(y & 1, GRID, 2):
            c.rect(x * SCALE, y * SCALE, SCALE, SCALE, BG_CHECK)

    # A jagged rock, drawn twice: a fat dim pass for the glow, a thin bright one.
    radii = [1.0, 0.82, 1.1, 0.9, 1.05, 0.78, 1.0, 0.9, 1.12, 0.85, 0.95]
    cx, cy, R = 150, 100, 78
    rock = [(cx + math.sin(2 * math.pi * i / 11) * R * r, cy - math.cos(2 * math.pi * i / 11) * R * r) for i, r in enumerate(radii)]
    poly(c, rock, 9, GLOW)
    poly(c, rock, 4, LIME)

    # The ship, nose up, low left, with its own glow.
    ship = [(70, 140 + 20), (100, 220), (86, 210), (54, 210), (40, 220)]
    hull = [(0, -17), (3, -9), (4, -3), (13, 6), (14, 12), (9, 10), (6, 12), (4, 9), (0, 10), (-4, 9), (-6, 12), (-9, 10), (-14, 12), (-13, 6), (-4, -3), (-3, -9)]
    ship = [(96 + x * 4, 196 + y * 4) for x, y in hull]
    poly(c, ship, 9, GLOW)
    poly(c, ship, 4, WHITE)
    canopy = [(96 + x * 4, 196 + y * 4) for x, y in [(0, -11), (2, -6), (0, -2), (-2, -6)]]
    poly(c, canopy, 4, WHITE)

    # A shot on its way to the rock.
    line(c, 100, 132, 108, 118, 5, WHITE)

    c.round_corners(36)

    out = os.path.join(os.path.dirname(__file__), "..", "assets", "icons", "rockdrift.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    c.write_png(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
