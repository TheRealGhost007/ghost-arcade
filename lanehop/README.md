# Lanehop

A lightweight, native road-crossing game for Linux, built with
[raylib](https://www.raylib.com) and plain C, on top of
[ghost-common](../ghost-common). Game #7 of the Ghost Arcade.

A homage to Frogger in mechanics only. The hero is a hare, the burrows are
its home, and every sprite, sound and line of code is original.

## The game

Cross five lanes of traffic, a verge, and five lanes of river to reach one of
five burrows in the hedge. Fill all five to clear the level.

- **Road:** anything that touches you is fatal, including one that arrives
  while you stand still. Cars, a fast bike lane, and long trucks
- **River:** you must ride the logs and turtles. Open water, or being carried
  off the side of the world, is fatal
- **Diving turtles:** one river lane's turtles wobble, go under, and come
  back, each group on its own beat. Wobbling is the warning; only fully
  under is fatal
- **Burrows:** land within half a tile of a burrow's centre. Hedge is fatal,
  so is a burrow that's already taken, and so is one with a **crocodile** in
  it. A **fly** in a burrow is worth 200
- **Scoring:** 10 for each new row (once per life), 50 plus 10 a second left
  on the clock for getting home, 1000 for clearing a level, and an extra
  hare every 10,000 points
- **30 seconds per hare.** The bar turns yellow, then blinks red for the
  last six seconds, with a tick each second
- Each level everything runs faster, up to double
- Quick taps chain: one hop can be buffered while another is in flight

## Controls (defaults, remappable in Settings)

| Action     | Keys          |
|------------|---------------|
| Hop        | Arrows / WASD |
| Pause      | P             |
| Restart    | R             |
| Menu/Back  | Esc           |
| Mute       | M             |
| Volume     | - / =         |

## Building

Requires `gcc`, `make`, `pkg-config`, `raylib` 6.0+, and `../ghost-common`.

```sh
make          # builds build/lanehop
make run
make test     # headless rules tests
make debug    # ASan/UBSan build
make install  # to ~/.local, with a .desktop launcher
```

Ghost Launcher catalog line:

```
Lanehop|~/.local/bin/lanehop|~/.local/share/lanehop/assets/icons/lanehop.png|Five lanes of traffic, five of river, one hare|~/Work/ghost-arcade/lanehop
```

## How it's put together

```
src/
  game.c/h    ALL the rules. Fixed 120 Hz step, no raylib. Lanes are data.
  render.c/h  the world from rectangles: lanes, vehicles, logs, turtles,
              burrows, the hare and its deaths
  persist.c/h, main.c
tests/test_main.c   headless suite, with a lookahead bot
tools/gen_sounds.py, gen_icon.py
```

**Lanes are data.** Each is a direction, a speed and a pattern string, one
character per tile (`.` empty, anything else solid) that repeats forever and
scrolls. The renderer, the collision code and the tests all read the same
table, so the tests hold every lane to fairness: gaps between vehicles are at
least two tiles, gaps between river platforms at most four, platforms
between 35% and 70% of a lane, neighbouring lanes flowing opposite ways.

**The hare stands on continuous x.** Riding a log leaves it between tiles, so
a hop is always exactly one tile from wherever it is. The hitbox is narrower
than a tile, and nothing hits you in mid-hop; the check happens when you land.

**Can it be done?** The test suite includes a bot that, before each hop,
copies the whole game, tries the hop, runs a second ahead and only commits if
the hare survives. It gets a hare home in every one of six seeded games,
filling 18 burrows in 100 simulated seconds each, which proves the lanes are
crossable rather than merely plausible. The tests also cover scrolling and
wrapping, run detection across the pattern seam, hops (chaining, buffering,
walls, sideways from a fraction), every death cause, burrows and their
tolerance, the fly and the crocodile, level clear, the timer, lives, extra
lives, and random button-mashing that must never break an invariant.

## Verge goodies (1.1.0)

Every so often something appears on the safe verge (the middle row) for eight seconds: a **carrot** (+300), a **clock** (+10 seconds on this hare's timer),
or a **shield charm** (the next car, river or burrow mishap is undone and you go back to the start line, no life lost; it can't save you from the clock).
Hopping onto it collects it.
