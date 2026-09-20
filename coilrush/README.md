# Coilrush

A lightweight, native grid snake game for Linux, built with
[raylib](https://www.raylib.com) and plain C. Game #2 of the Ghost Arcade
(after [Blockfall](../blockfall)), built for Omarchy (Hyprland/Wayland on
Arch) but a normal GLFW desktop app that should run on most Linux desktops.

All visuals and sound effects are original/procedurally generated for this
project. It's a homage to Snake and QBasic Nibbles in mechanics only -- no
artwork, audio, or branding from any existing game is used.

## Modes

| Mode    | Edges        | Twist |
|---------|--------------|-------|
| CLASSIC | deadly       | speeds up every 5 food |
| WRAP    | loop around  | speeds up every 5 food |
| MAZE    | deadly       | Nibbles-style stages: eat 8 food to clear a wall layout, the snake resets, the score carries, the next stage is faster. Six layouts, then they cycle. |

Pick the mode on the main menu with Left/Right. Each mode has its own high
score, best length, and top-10 table.

## Features

- 24x24 board, buffered turns (a quick "up, left" inside one step registers
  as two turns on consecutive steps -- no dropped inputs on tight corners)
- The tail-vacate rule: moving into the cell your tail is leaving is legal,
  so you can chase your own tail tip
- Timed gold bonus pickups after every 5th food: worth more the faster you
  get there, and they don't make you longer
- Filling the entire board is detected and counted as a win
- Game feel: the snake glides between cells (the rules still move a whole
  cell per step -- the renderer interpolates, including across Wrap's open
  edges), swallowed food travels down the body as a bulge, pickups burst
  into pixels, and dying shakes the board and pops the snake segment by
  segment before the score card appears
- Start menu, Scores screen, in-game Settings (rebind every control live,
  volume, mute), Updates/changelog screen
- Ghost Launcher integration (see below)
- Settings and bests persisted to your XDG config/data directories

## Controls (defaults, fully remappable in Settings)

| Action     | Keys            |
|------------|-----------------|
| Steer      | Arrows / WASD   |
| Pause      | P               |
| Restart    | R               |
| Menu/Back  | Esc             |
| Mute       | M               |
| Volume     | - / =           |
| FPS        | F3              |

In the menu: Up/Down to navigate, Left/Right to change mode, Enter/Space to
select.

## Building and running

Requires `gcc`, `make`, `pkg-config`, and `raylib` (6.0+; on Arch/Omarchy:
`sudo pacman -S raylib`).

```sh
make          # builds build/coilrush
make run      # builds and launches it
make test     # builds and runs the headless test suite
make debug    # ASan/UBSan build
make install  # installs to ~/.local and adds a .desktop launcher
```

`make install` puts the binary at `~/.local/bin/coilrush`, assets at
`~/.local/share/coilrush/assets`, plus an icon and desktop entry.
`make uninstall` removes all of it.

To add it to Ghost Launcher, append this line to
`~/.config/ghost-launcher/games.txt` (or use the launcher's admin Add form). The last field is the source folder the
launcher can reinstall the game from:

```
Coilrush|~/.local/bin/coilrush|~/.local/share/coilrush/assets/icons/coilrush.png|Snake with classic, wrap and maze modes|~/Work/ghost-arcade/coilrush
```

## Ghost Launcher integration

Coilrush is the first game to implement the **Ghost Arcade data contract**
(`src/ghostlink.c`, spec in `ghost-launcher/ROADMAP.md`):

- It reads the username you set for "Coilrush" in Ghost Launcher (press `U`
  there) from `~/.local/share/ghost-launcher/profiles.txt`, and plays as
  `PLAYER` if there isn't one. Re-read at the start of every run.
- It writes finished runs to `~/.local/share/ghost-launcher/scores/coilrush.txt`
  as `mode|username|score|date`, top 10 per mode. The in-game Scores screen
  reads the same file, and the launcher can show it too.
- A run counts however it ends: death, full board, restart, back to menu, or
  closing the window.
- **Online board:** at startup and after every finished run the game spawns
  Ghost Launcher's `ghost-sync` helper in the background. It uploads the
  local table and downloads the global top 10 (each player's best run) to
  `scores/global/coilrush.txt`, which the Scores screen shows under GLOBAL
  (Up/Down switches LOCAL/GLOBAL). The game itself has no network code and
  never waits on it. Opt out with `enabled=0` in
  `~/.config/ghost-launcher/online.conf`.

The game runs fine with no launcher installed.

## Data locations

- Settings: `$XDG_CONFIG_HOME/coilrush/config.ini`
- Per-mode high score / best length: `$XDG_DATA_HOME/coilrush/save.dat`
- Shared score table: `$XDG_DATA_HOME/ghost-launcher/scores/coilrush.txt`

All plain, human-readable text.

## Project structure

```
src/
  rng.c/h          xorshift64* PRNG (food placement)
  game.c/h         ALL the rules: grid, ring-buffer snake, turn queue, food,
                   bonus, leveling, maze layouts + stages, win/death
  ghostlink.c/h    Ghost Arcade data contract (profile name in, scores out)
  persist.c/h      settings + save data, read/written to XDG dirs
  input.c/h        key-name <-> raylib keycode mapping
  audio.c/h        sound effect loading/playback
  render.c/h       all drawing: board, HUD, menu, scores, settings, overlays
  main.c           window/game loop, input routing, app state machine
tests/test_main.c  headless tests (no raylib): 2700+ checks
tools/
  gen_sounds.py    synthesizes the .wav effects (stdlib only)
  gen_icon.py      composes the pixel-art icon PNG (stdlib only)
```

`rng.c`, `game.c` and `ghostlink.c` have **no dependency on raylib**. The
test suite covers movement, turn buffering, reversal rejection, growth,
edge/wall/self death, wrap, the tail-chase rule (legal normally, fatal while
growing), leveling and the speed floor, the bonus lifecycle, food placement,
the full-board win, every maze layout (spawn lane clear, all open cells
reachable by flood fill), stage advance, update timing, pause/restart/high
score, a random-walk fuzz pass that re-checks grid/body consistency, and the
ghostlink profile/score files against a throwaway `XDG_DATA_HOME`.

## Spirit relics (1.3.0)

After every seventh food a glowing relic drifts onto the board for a while. Eat it for one of three spells:
**P**hase (the snake turns see-through and can slide through its own body; edges and walls still kill, and if the spell ends
while your head is inside your body, that's the end), **S**low (steps take 60% longer), or **X** (double points).
