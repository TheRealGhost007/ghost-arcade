# Moondrop

A Lunar Lander homage for Ghost Arcade: bring a lander down onto procedurally
generated moon terrain with a limited tank. Drawn entirely in glowing vector
lines (no filled shapes), like Rockdrift. C17 + raylib.

## Rules

- Gravity pulls, thrust pushes along the nose, and fuel burns only while the
  engine does. Rotation is free.
- A landing counts when **both feet are on one pad**, you are under the speed
  limits (28 down, 16 sideways) and within 6 degrees of upright. Anything
  else is a crash. The readouts (top-left) turn red when a number is too much.
- Pads pay x1, x2, x3 or x5 (the narrower, the more) times 50, plus a tenth
  of your fuel and 50 more for a feather-light touchdown.
- A landing refuels 150; a crash costs 200 fuel but no life. The run ends when
  the tank is empty. Each new moon is rougher, with fewer and narrower pads.
- The moon wraps left to right. The camera zooms in as you near the ground.

## Controls

Left/Right or A/D rotate, Up/W/Space thrust, P pause, R restart, Esc menu.
All rebindable in Settings, which also has the glow toggle.

## Build

    make            # build/moondrop
    make test       # headless rules tests (no raylib needed)
    make install    # ~/.local/bin + ~/.local/share/moondrop
    python3 tools/gen_sounds.py && python3 tools/gen_icon.py

Shared UI, sound, keys, RNG and the online score client come from
`../ghost-common`. Scores are shared with Ghost Launcher under the "arcade"
mode of the `moondrop` table.

## Weather and fuel pods (1.1.0)

From level 3 the moon has wind: a steady push with slow gusts, shown as **WIND** in the header (gold, red when strong) and streaks across the sky.
It is capped at what a legal landing tilt can cancel, so it is always fair, just busy.
Every moon also has two or three floating fuel pods; fly through one for +130 fuel and 50 points.
