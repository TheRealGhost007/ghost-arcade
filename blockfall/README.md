# Blockfall

A lightweight, native falling-block puzzle game for Linux, built with
[raylib](https://www.raylib.com) and plain C. Built and tuned for Omarchy
(Hyprland/Wayland on Arch), but it's a normal X11/GLFW desktop app and
should run on most Linux desktops.

All visuals and sound effects are original/procedurally generated for this
project — no copyrighted Tetris artwork, audio, or branding is used, and the
project is deliberately named and coded independently of the trademarked
"Tetris" name.

## Blockfall 2.0: modes, sizes, power-ups

"Start game" opens a **play setup** screen (remembered from last time):

- **Mode:** Marathon (endless), Sprint (clear 40 lines, with a time bonus),
  Ultra (two minutes), Zen (no game over: a full stack is swept away and play
  carries on) and Power.
- **Board:** Classic 10x20, Wide 14x20, Tall 8x26, Huge 18x30, Tiny 6x14, or
  Custom: any width from 6 to 24 and height from 12 to 36. The board scales to
  fit the window.
- **Power mode:** about one piece in seven carries a power-up on one of its
  blocks, and it fires when the piece lands: **Bomb** (clears the 3x3 around
  it), **Laser** (clears its whole column), **Freeze** (gravity waits six
  seconds), **Slow** (gravity is 2.5x slower for twelve) and **Sweep** (the
  bottom three rows go).
- **Hold** a piece with `C` (once per piece). Combos and back-to-back
  four-line clears score extra on every board except the classic marathon,
  whose scoring is unchanged so old scores still mean what they did.
- Scores are kept **per mode and per board size** (`marathon` for the classic
  board, `sprint-14x20`, `power-18x30`, ...). Left/Right on the Scores screen
  switches mode; `T` cycles All time / This week / Today.

## Features

- 10x20 playfield, the standard seven tetrominoes (I, O, T, S, Z, J, L)
- 7-bag randomizer (each piece appears exactly once per shuffled bag of 7)
- Movement, soft drop, hard drop, rotation with wall kicks, lock delay
- Line clears, scoring, leveling, increasing fall speed
- Ghost piece (landing preview), next-piece preview
- Pause, restart, and a proper game-over screen
- Game feel: cleared lines flash and burst into pixels (four at once earns a
  QUAD banner and a shake), hard drops thud in proportion to the fall and
  kick up dust, pieces blink as they lock
- Scores screen with this machine's top ten or the world board, shared with
  [Ghost Launcher](../ghost-launcher) (see its ROADMAP.md for the data
  contract); plays under the name you set in the launcher
- Start menu and an in-game Settings screen for rebinding every control and
  adjusting/muting audio, all live (no restart needed)
- High score / top level / best lines and all settings persisted to your
  XDG config/data directories, not the source tree
- A `.desktop` launcher so it shows up in your application menu

## Controls (defaults, fully remappable in Settings)

| Action     | Keys        |
|------------|-------------|
| Move       | Left/A, Right/D |
| Soft drop  | Down/S      |
| Rotate     | Up/W        |
| Hard drop  | Space       |
| Pause      | P           |
| Restart    | R           |
| Menu/Back  | Esc         |
| Mute       | M           |
| Volume     | - / =       |

In the menu: Up/Down (or W/S) to navigate, Enter/Space to select.
In Settings: select a control row and press Enter to rebind it, then press
any key; Left/Right adjusts volume; Esc goes back.

## Building and running

Requires `gcc`, `make`, `pkg-config`, and the `raylib` library (6.0+; on
Arch/Omarchy: `sudo pacman -S raylib`).

```sh
make          # builds build/blockfall
make run      # builds and launches it
make test     # builds and runs the headless game-logic test suite
```

### Installing (adds it to your application menu)

```sh
make install
```

This installs to your user's XDG locations (no root needed):

- Binary: `~/.local/bin/blockfall`
- Assets: `~/.local/share/blockfall/assets`
- Icon: `~/.local/share/icons/hicolor/256x256/apps/blockfall.png`
- Launcher: `~/.local/share/applications/blockfall.desktop`

Make sure `~/.local/bin` is on your `PATH` (it is by default on most
distros, including Omarchy). After installing, launch it by typing
`blockfall` in a terminal, or find "Blockfall" in your application
launcher/menu.

`make uninstall` removes all of the above.

## Data locations

Following the XDG Base Directory spec, nothing is written inside the
project/source directory during play:

- Settings (key bindings, volume, mute): `$XDG_CONFIG_HOME/blockfall/config.ini`
  (defaults to `~/.config/blockfall/config.ini`)
- High score / top level / best lines: `$XDG_DATA_HOME/blockfall/save.dat`
  (defaults to `~/.local/share/blockfall/save.dat`)

Both are plain, human-readable `key=value` text files.

## Project structure

```
src/
  rng.c/h          xorshift64* PRNG (used by the 7-bag randomizer)
  tetromino.c/h     piece shapes, rotation states, colors
  board.c/h         10x20 grid, collision, locking, line clearing
  game.c/h          core game state machine: gravity, lock delay, scoring,
                    leveling, wall kicks, game over, restart
  persist.c/h       settings + save data, read/written to XDG dirs
  input.c/h         key-name <-> raylib keycode mapping, DAS/ARR repeat logic
  audio.c/h         sound effect loading/playback (raylib audio)
  ghostlink.c/h     Ghost Arcade data contract: launcher name in, scores out
  render.c/h        all drawing: board, HUD, menu, scores, settings, overlays,
                    and the render-only effects (flashes, shake, particles)
  main.c            window/game loop, input routing, app state machine
tests/
  test_main.c        headless correctness tests for rng/board/game (no
                     raylib dependency — runs anywhere, including CI)
tools/
  gen_sounds.py      generates the synthesized .wav sound effects
assets/
  sounds/*.wav       procedurally generated sound effects
  icons/blockfall.png  app icon (procedurally composed, not hand-drawn art)
packaging/
  blockfall.desktop   XDG desktop entry
```

`src/rng.c`, `tetromino.c`, `board.c`, and `game.c` have **no dependency on
raylib** — all core gameplay logic is plain, portable C. This is what lets
`make test` build and run a full correctness test suite (1000+ assertions
covering collision, rotation/wall-kicks, line clearing, scoring, leveling,
game-over detection, and restart) without a display or GPU, and it's also
why the game logic is easy to reason about and extend independently of
rendering/input/audio concerns.

## Modifying controls later

Easiest: launch the game, go to **Settings** from the main menu, select a
control, press Enter, then press the new key. It's saved immediately and
takes effect without restarting.

Alternatively, edit `~/.config/blockfall/config.ini` directly — it's a
plain text file with one `action=KEY_NAME` per line (key names like `A`,
`LEFT`, `SPACE`, `LEFT_SHIFT`, etc.). The game re-reads it on startup.

## Performance notes

- Rendering uses raylib's OpenGL backend with vsync (`FLAG_VSYNC_HINT`) plus
  a `SetTargetFPS(60)` safety cap, so the game paces itself to the display
  refresh instead of busy-spinning the CPU/GPU.
- All textures/sounds are loaded once at startup; nothing is allocated or
  read from disk during the per-frame game loop.
- The board is a flat `int8_t[24][10]` (under 240 bytes) with no dynamic
  allocation anywhere in the core game logic — gameplay state updates are
  just small fixed-size array scans.
- Input uses an accumulator-based DAS/ARR (delayed auto-shift / auto-repeat)
  state machine timed in real seconds (not frame counts), so movement feel
  stays consistent regardless of frame rate.
- Settings/save files are only touched on actual state changes (a setting
  change, game over, or quit) — never during the hot per-frame loop.
