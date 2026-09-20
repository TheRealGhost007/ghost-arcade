# Skyraid

A lightweight, native fixed shooter for Linux, built with
[raylib](https://www.raylib.com) and plain C. Game #4 of the Ghost Arcade,
and the first built on [ghost-common](../ghost-common) from the start.

A homage to Space Invaders in mechanics only. The cast is the house brand's
own -- bats, skulls and ghosts -- and every sprite, sound and line of code is
original.

## The game

- Eleven columns by five rows march across and down. **The fewer are left,
  the faster they march** (from a step every ~0.67 s down to ~0.045 s for
  the last one), and each wave is a little quicker and starts a row lower
- **One shot on screen at a time**: a miss costs you the wait
- Up to three enemy bolts in the air; only the lowest invader in a column
  has a clear shot, so thinning a column from the bottom changes who fires
- Four bunkers erode under fire from both sides and are trampled flat where
  the formation walks through them
- A mystery ship crosses every so often; its value (50-300) depends on how
  many shots you have fired, as in the original -- the 9th shot of every 15
  is the jackpot
- Bats 30, skulls 20, ghosts 10. Extra ship at 1500. Three ships to start
- If the formation reaches the ground the game ends at once, ships or not
- Optional CRT scanlines (Settings)

## Controls (defaults, remappable in Settings)

| Action     | Keys              |
|------------|-------------------|
| Move       | Left/Right or A/D |
| Fire       | Space or W (hold) |
| Pause      | P                 |
| Restart    | R                 |
| Menu/Back  | Esc               |
| Mute       | M                 |
| Volume     | - / =             |

## Building

Requires `gcc`, `make`, `pkg-config`, `raylib` 6.0+, and the
`../ghost-common` folder next to this one.

```sh
make          # builds build/skyraid
make run
make test     # headless rules tests
make debug    # ASan/UBSan build
make install  # to ~/.local, with a .desktop launcher
```

Ghost Launcher catalog line:

```
Skyraid|~/.local/bin/skyraid|~/.local/share/skyraid/assets/icons/skyraid.png|Hold the line against the marching formation|~/Work/ghost-arcade/skyraid
```

## How it's put together

```
src/
  game.c/h     ALL the rules. Fixed 120 Hz step, no raylib.
  render.c/h   the playfield: string-art sprites, bunkers, bolts, score
               pop-ups, shake -- effects driven by the rules' one-frame events
  persist.c/h  settings + save data (XDG dirs)
  main.c       window, app state machine, sound cues, and the menu / scores /
               settings / updates screens (all four drawn by ghost-common)
../ghost-common/
  ui.c/h       house style: font + type scale, palette, particles, the
               shared screens
  sfx, keynames, ghostlink, rng
tests/test_main.c   headless suite
```

The march bass line is one sample played at four pitches, retriggered on
every formation step -- so the music speeds up exactly as the invaders do,
with no separate tempo logic.

The tests cover the march-speed curve, the score tables, bunker shape and
symmetry, edge drop-and-turn (and that an emptied edge column lets the rest
travel further), that only the lowest invader of a column ever fires and
never more than three bolts fly, the one-shot rule, the gap between sprites
being a real gap, bunker erosion from both sides and the open arch,
trampling, death/respawn freeze, invasion, waves (lower start, capped,
bunkers rebuilt), the extra ship, the mystery ship's value and exit, update
timing, and long bot-played games for sanity and determinism.

## Power-ups (1.1.0)

Capsules fall from the mystery ship (always) and from about one invader in fourteen. Catch them with the ship:
**R**apid (several shots at once, 9 s), **T**win (two side-by-side shots, 9 s), **S**hield (absorbs one hit), **N**ova (wipes the lowest row).
Losing your last ship clears them.
