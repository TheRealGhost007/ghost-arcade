#!/usr/bin/env python3
"""Generates the Pond Kings app icon (256x256 RGBA PNG): a hockey puck wearing a
gold crown, two crossed wooden sticks behind it, on a frozen pond under a night
sky with a few stars and a faint band of northern lights. Pixel art: drawn on a
64x64 grid, then scaled up 4x with nearest neighbour so it stays crisp.
Composed entirely in code -- nothing hand-drawn or copied. Uses only the
Python standard library (zlib + struct for the PNG encoding).

The launcher tints the cabinet from the icon's dominant vivid hue (saturation x
value weighted, dark and greyish pixels don't vote), so the crown's gold is the
loudest colour here, the night blue the biggest, and the aurora stays faint.
"""
import math
import os
import struct
import zlib

GRID = 64   # logical pixel-art cells per side
SCALE = 4   # output pixels per cell -> 256x256
SIZE = GRID * SCALE
MID = 32    # the emblem's centre column: everything is mirror-symmetric about it

SKY = [(9, 13, 38), (11, 16, 45), (13, 19, 52), (15, 22, 58), (17, 25, 63)]
SKY_BREAKS = [13, 25, 36, 46]          # rows where one sky band dithers into the next
STAR = (255, 246, 214)
STAR_DIM = (120, 132, 186)
AURORA = (110, 250, 140)
ICE = (198, 226, 242)
ICE_LIGHT = (234, 247, 254)
ICE_SHADE = (160, 198, 228)
ICE_EDGE = (104, 146, 196)
SNOW = (240, 246, 255)
SNOW_SHADE = (184, 204, 236)
WOOD = (196, 130, 64)
WOOD_LIGHT = (236, 180, 110)
WOOD_DARK = (140, 84, 40)
TAPE = (28, 28, 36)
TAPE_STRIPE = (66, 66, 80)
PUCK_TOP = (62, 64, 78)
PUCK_RIM = (112, 116, 134)
PUCK_SIDE = (22, 22, 30)
PUCK_SHINE = (48, 50, 62)
GOLD = (255, 208, 36)
GOLD_LIGHT = (255, 242, 140)
GOLD_DARK = (222, 152, 22)
RUBY = (226, 40, 70)
SAPPHIRE = (66, 168, 255)
GLINT = (255, 255, 255)
OUTLINE = (8, 9, 22)


class Canvas:
    def __init__(self):
        self.px = [[(0, 0, 0, 0)] * GRID for _ in range(GRID)]

    def put(self, x, y, color, alpha=1.0):
        if not (0 <= x < GRID and 0 <= y < GRID):
            return
        if alpha >= 1.0:
            self.px[y][x] = color + (255,)
            return
        r, g, b, _ = self.px[y][x]
        self.px[y][x] = (round(r + (color[0] - r) * alpha), round(g + (color[1] - g) * alpha),
                         round(b + (color[2] - b) * alpha), 255)

    def stamp(self, sprite, outline=OUTLINE):
        """Draws a sprite ({(x, y): colour}) with a one-cell outline around its silhouette."""
        for x, y in sprite:
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                if (x + dx, y + dy) not in sprite:
                    self.put(x + dx, y + dy, outline)
        for (x, y), color in sprite.items():
            self.put(x, y, color)

    def round_corners(self, radius):
        for y in range(GRID):
            for x in range(GRID):
                cx = radius if x < radius else GRID - radius if x >= GRID - radius else x + 0.5
                cy = radius if y < radius else GRID - radius if y >= GRID - radius else y + 0.5
                if (x + 0.5 - cx) ** 2 + (y + 0.5 - cy) ** 2 > radius ** 2:
                    self.px[y][x] = (0, 0, 0, 0)

    def write_png(self, path):
        """Writes the 64x64 picture scaled up SCALE times with nearest neighbour."""
        rows = []
        for row in self.px:
            line = b"\x00" + b"".join(bytes(p) * SCALE for p in row)
            rows.extend([line] * SCALE)

        def chunk(tag, data):
            body = tag + data
            return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

        with open(path, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n")
            f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)))
            f.write(chunk(b"IDAT", zlib.compress(b"".join(rows), 9)))
            f.write(chunk(b"IEND", b""))


def in_ellipse(x, y, cx, cy, rx, ry):
    return ((x + 0.5 - cx) / rx) ** 2 + ((y + 0.5 - cy) / ry) ** 2 <= 1.0


def mirror(x):
    """The cell mirrored about the centre of column MID."""
    return 2 * MID - x


# ------------------------------------------------------------------ the sky

def draw_sky(c):
    for y in range(GRID):
        band = sum(1 for b in SKY_BREAKS if y > b)
        for x in range(GRID):
            color = SKY[band]
            if y in SKY_BREAKS and (x + y) % 2:   # a one-row checker dither between bands
                color = SKY[band + 1]
            c.put(x, y, color)


def draw_aurora(c):
    """A faint wavy curtain: a brighter hem, rays fading upward, a soft lower fringe."""
    for x in range(GRID):
        hem = 17.0 - 0.07 * (x - MID) + 3.0 * math.sin(x * 0.15 + 0.4)
        y0 = int(round(hem))
        rays = 3 + int(round(3 * (0.5 + 0.5 * math.sin(x * 0.83) * math.sin(x * 0.29 + 1.1))))
        c.put(x, y0 + 1, AURORA, 0.14)
        c.put(x, y0, AURORA, 0.40)
        c.put(x, y0 - 1, AURORA, 0.32)
        for k in range(rays):
            c.put(x, y0 - 2 - k, AURORA, 0.24 * (1.0 - k / rays))


def draw_stars(c):
    for x, y in ((40, 4), (8, 30), (56, 33)):          # bigger, with dim arms
        c.put(x, y, STAR)
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            c.put(x + dx, y + dy, STAR_DIM)
    for x, y in ((21, 5), (54, 26), (5, 22), (14, 34), (50, 35), (29, 9)):
        c.put(x, y, STAR)
    for x, y in ((13, 3), (58, 9), (46, 30), (3, 36), (26, 21), (60, 38)):
        c.put(x, y, STAR_DIM)


# ------------------------------------------------------------------- the pond

ICE_CX, ICE_CY, ICE_RX, ICE_RY = MID + 0.5, 49.5, 30.0, 10.5


def draw_ice(c):
    for y in range(GRID):
        for x in range(GRID):
            if in_ellipse(x, y, ICE_CX, ICE_CY, ICE_RX, ICE_RY):
                # The snowbank along the back edge throws a shadow onto the
                # ice, and the front rim is shaded a little.
                if not in_ellipse(x, y - 2, ICE_CX, ICE_CY, ICE_RX, ICE_RY):
                    color = ICE_EDGE
                elif (not in_ellipse(x, y - 3, ICE_CX, ICE_CY, ICE_RX, ICE_RY)
                      or not in_ellipse(x, y + 2, ICE_CX, ICE_CY, ICE_RX, ICE_RY)):
                    color = ICE_SHADE
                else:
                    color = ICE
                c.put(x, y, color)
    # Glossy streaks where the light catches the ice.
    for x0, x1, y in ((7, 13, 50), (15, 17, 50), (46, 52, 51), (54, 55, 51), (26, 33, 57)):
        for x in range(x0, x1 + 1):
            c.put(x, y, ICE_LIGHT)
    for x, y in ((10, 48), (50, 49), (37, 58)):
        c.put(x, y, GLINT)


def draw_snowbank(c):
    """Lumpy snowbanks along the back edge of the pond -- the rink's boards."""
    for x in range(GRID):
        dx = (x + 0.5 - ICE_CX) / ICE_RX
        if abs(dx) >= 0.97:
            continue
        top_of_ice = ICE_CY - ICE_RY * math.sqrt(1.0 - dx * dx)
        lump = 1.6 + 1.4 * (0.5 + 0.5 * math.sin(x * 0.62 + 0.3)) + 0.6 * math.sin(x * 1.7)
        y_base = int(math.floor(top_of_ice)) + 1
        for y in range(int(round(y_base - lump)), y_base + 1):
            c.put(x, y, SNOW_SHADE if y == y_base else SNOW)


# ------------------------------------------------------------------ the sticks

def thick_line(cells, x0, y0, x1, y1, width=2):
    n = max(abs(x1 - x0), abs(y1 - y0))
    for i in range(n + 1):
        t = i / max(1, n)
        x = int(round(x0 + (x1 - x0) * t))
        y = int(round(y0 + (y1 - y0) * t))
        for dy in range(width):
            for dx in range(width):
                cells.add((x + dx, y + dy))


def stick_sprite(knob, heel, toe):
    """One hockey stick: a round wooden shaft with a taped knob, and a wooden
    blade with a wrap of black tape round its middle."""
    shaft = set()
    thick_line(shaft, knob[0], knob[1], heel[0], heel[1])
    blade = set()
    thick_line(blade, heel[0], heel[1], toe[0], toe[1])
    blade.update({(toe[0] + 1, toe[1] - 1), (toe[0] + 2, toe[1] - 1)})   # the toe curls up a little
    sprite = {}
    for x, y in shaft:
        if (x, y - 1) not in shaft:
            sprite[(x, y)] = WOOD_LIGHT
        elif (x, y + 1) not in shaft:
            sprite[(x, y)] = WOOD_DARK
        else:
            sprite[(x, y)] = WOOD
        if y - knob[1] <= 2:
            sprite[(x, y)] = TAPE_STRIPE if (x + y) % 3 == 0 else TAPE
    taped = range(heel[0] + 3, toe[0] - 1)
    for x, y in blade:
        if x in taped:
            sprite[(x, y)] = TAPE_STRIPE if (x + y) % 3 == 0 else TAPE
        else:
            sprite[(x, y)] = WOOD_LIGHT if (x, y - 1) not in blade else WOOD_DARK
    return sprite


def draw_sticks(c):
    # Crossed behind the puck: the shafts come out past the crown's sides, the
    # heels past the puck's, and the blades lie on the ice.
    left = stick_sprite(knob=(8, 17), heel=(45, 54), toe=(52, 54))
    right = {(mirror(x), y): color for (x, y), color in left.items()}
    c.stamp(right)
    c.stamp(left)


# -------------------------------------------------------------------- the puck

PUCK_CX, PUCK_TOP_CY, PUCK_RX, PUCK_RY, PUCK_DEPTH = MID + 0.5, 39.5, 11.5, 3.5, 5


def draw_puck_shadow(c):
    for y in range(GRID):
        for x in range(GRID):
            if in_ellipse(x, y, PUCK_CX, PUCK_TOP_CY + PUCK_DEPTH + 1.5, PUCK_RX + 2.5, 2.6):
                c.put(x, y, ICE_SHADE)


def draw_puck(c):
    top, side = set(), set()
    for y in range(GRID):
        for x in range(GRID):
            if in_ellipse(x, y, PUCK_CX, PUCK_TOP_CY, PUCK_RX, PUCK_RY):
                top.add((x, y))
            elif (in_ellipse(x, y, PUCK_CX, PUCK_TOP_CY + PUCK_DEPTH, PUCK_RX, PUCK_RY)
                  or (abs(x + 0.5 - PUCK_CX) <= PUCK_RX and PUCK_TOP_CY <= y + 0.5 <= PUCK_TOP_CY + PUCK_DEPTH)):
                side.add((x, y))
    sprite = {}
    for x, y in side:
        sprite[(x, y)] = PUCK_SHINE if 0.3 < (x + 0.5 - PUCK_CX) / PUCK_RX < 0.6 else PUCK_SIDE
    for x, y in top:
        # The rim catches the light along the face's edge.
        sprite[(x, y)] = PUCK_RIM if (x, y + 1) not in top or (x, y - 1) not in top else PUCK_TOP
    c.stamp(sprite)


# ------------------------------------------------------------------- the crown

# 19 cells wide, centred on column MID; symmetric in shape, lit from the left:
#   G gold   L light   D dark   R ruby   S sapphire   W glint   . empty
CROWN = [
    ".L.......L.......L.",
    "LGD.....LGD.....LGD",
    ".G.......G.......G.",
    ".G.......G.......G.",
    ".LD.....LGD.....LD.",
    ".LGD...LGGGD...LGD.",
    ".LGGD.LGGGGGD.LGGD.",
    ".LGGGGGGGGGGGGGGGD.",
    "LLLLLLLLLLLLLLLLLLL",
    "GGWSGGGGGRGGGGGWSGD",
    "GGSSGGGGWRRGGGGSSGD",
    "GGGGGGGGGRGGGGGGGGD",
    ".DDDDDDDDDDDDDDDDD.",
]
CROWN_PALETTE = {"G": GOLD, "L": GOLD_LIGHT, "D": GOLD_DARK, "R": RUBY, "S": SAPPHIRE, "W": GLINT}


def draw_crown(c):
    width = len(CROWN[0])
    x0 = MID - width // 2
    y0 = 27
    sprite = {}
    for j, row in enumerate(CROWN):
        for i, ch in enumerate(row):
            if ch != ".":
                sprite[(x0 + i, y0 + j)] = CROWN_PALETTE[ch]
    c.stamp(sprite)


def main():
    c = Canvas()
    draw_sky(c)
    draw_aurora(c)
    draw_stars(c)
    draw_ice(c)
    draw_snowbank(c)
    draw_puck_shadow(c)
    draw_sticks(c)
    draw_puck(c)
    draw_crown(c)
    c.round_corners(10)

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "icons", "pondkings.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    c.write_png(out)
    print("wrote %s" % os.path.normpath(out))


if __name__ == "__main__":
    main()
