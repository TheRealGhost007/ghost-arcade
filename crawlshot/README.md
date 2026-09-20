# Crawlshot

A caterpillar homage for Ghost Arcade: twelve segments come down a garden of
toadstools and you shoot them from the bottom six rows. C17 + raylib, no
assets beyond the font, sounds and icon it generates.

## Rules

- Shoot a caterpillar segment and it becomes a toadstool; the caterpillar
  splits, and the back half grows a new head. Head 100, body 10.
- A toadstool takes four shots (1 point when it goes). Caterpillars drop a row
  and turn round at every toadstool or edge; at the bottom they climb back into
  your garden (never above it).
- **Spider**: zig-zags through the garden eating toadstools. Shot close 900,
  middling 600, far 300.
- **Flea**: drops down planting toadstools, but only when your garden is nearly
  bare. Two shots, 200.
- **Scorpion** (from wave 2): poisons a row of toadstools (1000 to shoot it).
  A caterpillar touching a poisoned toadstool dives straight to the bottom.
- Lose a shooter and every damaged or poisoned toadstool is healed one by one,
  5 points each, before the next caterpillar arrives.
- Each wave splits the twelve segments into a shorter caterpillar plus more
  single heads, and everything gets a little faster. Toadstools carry over.
- Extra shooter every 12,000 points.

## Controls

Arrows or WASD move, Space fires (hold to stream), P pauses, R restarts, Esc
returns to the menu. All rebindable in Settings; Settings also has optional
mouse steering (move to steer, click to fire) and CRT scanlines.

## Build

    make            # build/crawlshot
    make test       # headless rules tests (no raylib needed)
    make install    # ~/.local/bin + ~/.local/share/crawlshot
    python3 tools/gen_sounds.py && python3 tools/gen_icon.py

Shared UI, sound, keys, RNG and the online score client come from
`../ghost-common`. Scores are shared with Ghost Launcher under the "arcade"
mode of the `crawlshot` table.

## Spores (1.1.0)

Shot spiders, fleas and scorpions always drop a glowing spore, and about one toadstool in sixteen does too. Catch it:
**P**ierce (shots pass through toadstools and segments for 9 s), **B**last (your next six hits also take the eight cells around them),
**F**reeze (the caterpillar stops for 4 s). Dying clears them.
