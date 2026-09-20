#!/usr/bin/env python3
"""Generates the Ghostmaze app icon (256x256 RGBA PNG): a ghost in a stone hall, with a
priest and the beam of his sight, drawn as chunky bevelled pixel-art tiles to match the
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
WALL = (62, 50, 104)
WALL_HI = (86, 72, 140)
FLOOR = (30, 24, 58)
GHOST = (246, 240, 255)
PINK = (255, 110, 190)
BEAM = (255, 226, 120)
GOLD = (255, 208, 80)
EYE = (21, 16, 43)
ROBE = (28, 22, 44)

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


def art(c, rows, x, y, px, pal):
    for r, line in enumerate(rows):
        for k, ch in enumerate(line):
            if ch in pal:
                c.rect(x + k * px, y + r * px, px, px, pal[ch])


def main():
    c = Canvas()
    c.rect(0, 0, SIZE, SIZE, FLOOR)
    for row in range(0, 256, 32):
        c.rect(0, row, SIZE, 3, WALL_HI)
        for col in range(0, 256, 64):
            off = 32 if (row // 32) % 2 else 0
            c.rect(col + off, row, 3, 32, (40, 32, 74))
    c.rect(0, 0, SIZE, 24, WALL)
    c.rect(0, 232, SIZE, 24, WALL)

    # The priest's beam, then the priest.
    beam = [(150, 150), (60, 118), (60, 182)]
    for y in range(110, 190):
        half = int((y - 110) * 0.0)
    for x in range(64, 160, 2):
        h = (x - 64) * 0.30 + 6
        c.rect(x, int(150 - h * 0.5 - 6), 2, int(h), (90, 82, 44))
    priest = ["..WWWW..", "..SSSS..", "..SESS..", ".XXXXXX.", "XXXWWXXX", "XXXXXXXX", ".XXXXXX.", "XX....XX"]
    art(c, priest, 168, 118, 9, {"W": (240, 236, 250), "S": (240, 200, 170), "E": EYE, "X": ROBE})
    c.rect(146, 158, 12, 12, GOLD)
    c.rect(150, 170, 5, 4, (166, 136, 50))

    # The ghost, big, with a pink glow.
    for r in range(70, 0, -6):
        pass
    ghost = ["..XXXX..", ".XXXXXX.", "XXXXXXXX", "XEEXXEEX", "XXXXXXXX", "XXXXXXXX", "XXXXXXXX", "X.XX.XX."]
    art(c, ghost, 30, 92, 12, {"X": GHOST, "E": EYE})
    c.rect(24, 88, 4, 4, PINK)
    c.rect(132, 88, 4, 4, PINK)
    c.rect(24, 180, 4, 4, PINK)
    c.rect(132, 180, 4, 4, PINK)

    c.round_corners(36)

    out = os.path.join(os.path.dirname(__file__), "..", "assets", "icons", "ghostmaze.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    c.write_png(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
