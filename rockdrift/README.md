# Rockdrift

A lightweight, native vector-line space-rock shooter for Linux, built with
[raylib](https://www.raylib.com) and plain C, on top of
[ghost-common](../ghost-common). Game #6 of the Ghost Arcade, and the one
that looks least like the others: **nothing on screen is a filled shape**.
Every rock, ship and saucer is a handful of glowing lines.

A homage to Asteroids in mechanics only. The outlines, sounds and code are
all original.

## The game

- **Real inertia.** The ship keeps drifting until you turn round and thrust
  the other way (top speed 380 px/s). Nothing slows you down
- **The field wraps** -- for the ship, the rocks, every bullet, and (for the
  saucers' aim) the shortest line between two points
- Large rocks split into two medium, medium into two small, and small ones
  vanish: 20 / 50 / 100 points, so the small ones are worth the trouble.
  Each piece is quicker than what it came from. Every rock has its own jagged
  outline, generated from a seed
- **Four bullets at a time**, each living under a second
- **Two saucers.** The large one (200) fires in any direction. The small one
  (1000) aims at you -- through the wrapped edge if that is shorter -- with
  an error that shrinks as your score climbs, from about 30 degrees at zero
  to under 5 at 30,000. Small saucers become more common as you score
- **Hyperspace** drops you somewhere random. One press in six you come out
  in pieces, and it doesn't check that the spot is free
- Rocks that hit you break too, and you come back in the middle only when
  it's clear, with two seconds of protection
- A two-note **heartbeat** that quickens as the rocks run out; an extra ship
  every 10,000 points (up to six); waves of 4 rocks, growing to 11

## Controls (defaults, remappable in Settings)

| Action     | Keys              |
|------------|-------------------|
| Turn       | Left/Right or A/D |
| Thrust     | Up or W           |
| Fire       | Space (hold)      |
| Hyperspace | Left Shift        |
| Pause      | P                 |
| Restart    | R                 |
| Menu/Back  | Esc               |
| Mute       | M                 |
| Volume     | - / =             |

Settings also has a switch for the glow.

## Building

Requires `gcc`, `make`, `pkg-config`, `raylib` 6.0+, and `../ghost-common`.

```sh
make          # builds build/rockdrift
make run
make test     # headless rules tests
make debug    # ASan/UBSan build
make install  # to ~/.local, with a .desktop launcher
```

Ghost Launcher catalog line:

```
Rockdrift|~/.local/bin/rockdrift|~/.local/share/rockdrift/assets/icons/rockdrift.png|Vector-line space rocks with real inertia|~/Work/ghost-arcade/rockdrift
```

## How it's put together

```
src/
  game.c/h    ALL the rules. Fixed 120 Hz step, no raylib, toroidal field.
  render.c/h  the playfield, as lines (the only landscape window, 800x640)
  persist.c/h, main.c
tests/test_main.c   headless suite
tools/gen_sounds.py, gen_icon.py
```

**How the glow works.** Every line is queued during the frame. At the end
they are drawn twice: first all of them thick and dim in additive blending
(the halo, which brightens where lines cross, as phosphor did), then all of
them thin and bright. One blend-mode switch per frame, not per line.

**Wrapping.** Anything within its own radius of an edge is drawn again,
shifted by one field width or height, where its far side pokes through.

The tests cover the wrapped-distance maths, rock outlines (deterministic and
within bounds so hit circles are honest), thrust and inertia and the speed
cap, wrapping of ship and bullets, the four-bullet limit and lifetime, splits
(counts, positions, points, quicker children, the array never overflowing),
death and the wait for a clear middle, respawn protection, hyperspace (the
rising-edge rule, and 3000 seeded trials that must land near one in six),
both saucers (the small one's shots must stay within its stated error at
scores 0 and 30,000, and take the short way through a wrapped edge), waves,
the extra ship repeating, the heartbeat, and long bot-played games for
sanity and determinism.
