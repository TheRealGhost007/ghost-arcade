#!/usr/bin/env python3
"""Generates the Lanehop app icon (256x256 RGBA PNG): a hare mid-hop between a
river with a log and a road with a car, drawn as chunky bevelled pixel-art tiles to match the
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
WATER = (24, 44, 104)
WATER_LINE = (52, 92, 176)
ROAD = (40, 38, 52)
LANE = (200, 196, 220)
GRASS = (30, 74, 50)
LOG = (140, 88, 48)
LOG_HI = (186, 124, 70)
HARE = (250, 240, 215)
EAR = (255, 170, 190)
EYE = (21, 16, 43)
CAR = (255, 104, 96)
GLASS = (150, 200, 255)
WHEEL = (20, 16, 30)

HARE_ART = [
    "..X..X..", "..X..X..", "..XXXX..", ".XXXXXX.", ".XKXXKX.", ".XXXXXX.", "..XXXX..", ".XX..XX.",
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


def main():
    c = Canvas()
    c.rect(0, 0, SIZE, SIZE, BG)

    # Top: river with a log. Middle: a strip of grass. Bottom: road with a car.
    c.rect(0, 20, SIZE, 88, WATER)
    for x in range(10, 250, 44):
        c.rect(x, 44, 22, 4, WATER_LINE)
        c.rect(x + 20, 84, 22, 4, WATER_LINE)
    c.rect(24, 56, 132, 26, LOG)
    c.rect(24, 56, 132, 5, LOG_HI)
    for x in (48, 96, 136):
        c.rect(x, 64, 6, 10, (100, 60, 30))
    c.rect(0, 108, SIZE, 40, GRASS)
    c.rect(0, 148, SIZE, 88, ROAD)
    for x in range(8, 256, 48):
        c.rect(x, 190, 28, 5, LANE)

    # A car on the road, in the bunny's colours.
    c.rect(150, 160, 84, 30, CAR)
    c.rect(150, 160, 84, 6, (255, 150, 140))
    c.rect(176, 166, 30, 14, GLASS)
    c.rect(160, 186, 14, 8, WHEEL)
    c.rect(210, 186, 14, 8, WHEEL)

    # The hare, mid-hop on the grass, big.
    ox, oy, px = 60, 84, 10
    for r, line in enumerate(HARE_ART):
        for col, ch in enumerate(line):
            if ch == "X":
                colr = EAR if (r < 2) else HARE
                c.rect(ox + col * px, oy + r * px - 8, px, px, colr)
            elif ch == "K":
                c.rect(ox + col * px, oy + r * px - 8, px, px, EYE)

    c.round_corners(36)

    out = os.path.join(os.path.dirname(__file__), "..", "assets", "icons", "lanehop.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    c.write_png(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
