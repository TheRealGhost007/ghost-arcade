# Brickburst

A lightweight, native brick-breaking game for Linux, built with
[raylib](https://www.raylib.com) and plain C. Game #3 of the Ghost Arcade
(after [Blockfall](../blockfall) and [Coilrush](../coilrush)).

All visuals and sound effects are original/procedurally generated. It's a
homage to Breakout and Arkanoid in mechanics only -- no artwork, audio, or
branding from any existing game is used.

## The game

- Twelve layouts, then round again with faster balls
- Where the ball lands on the paddle decides where it goes (up to 62 degrees
  either way); every paddle hit makes it a little quicker, up to a cap
- Bricks: one-, two- and three-hit (they brighten as they weaken), steel
  that never breaks and doesn't need to, and bombs that take their eight
  neighbours -- including other bombs
- Capsules: **x3** splits every ball in play three ways, **W** wide paddle,
  **S** slow, **F** fireball (burns straight through bricks), **+1** paddle
- Three paddles to start, six at most
- Keyboard or mouse: the paddle follows the mouse whenever it moves (same
  top speed as the keys, so neither is an advantage), click or Space launches
- Scores screen (this machine / world), your Ghost Launcher name, background
  online sync -- the same Ghost Arcade plumbing as the other games

## Controls (defaults, remappable in Settings)

| Action     | Keys                      |
|------------|---------------------------|
| Move       | Left/Right, A/D, or mouse |
| Launch     | Space, Up, or click       |
| Pause      | P                         |
| Restart    | R                         |
| Menu/Back  | Esc                       |
| Mute       | M                         |
| Volume     | - / =                     |
| FPS        | F3                        |

## Building

Requires `gcc`, `make`, `pkg-config`, `raylib` 6.0+.

```sh
make          # builds build/brickburst
make run
make test     # headless rules tests
make debug    # ASan/UBSan build
make install  # to ~/.local, with a .desktop launcher
```

`make uninstall` removes the program and its assets but leaves your save
data and scores alone.

Ghost Launcher catalog line:

```
Brickburst|~/.local/bin/brickburst|~/.local/share/brickburst/assets/icons/brickburst.png|Paddle, ball, bombs and twelve walls of bricks|~/Work/ghost-arcade/brickburst
```

## How it's put together

```
src/
  game.c/h      ALL the rules. Fixed 120 Hz physics step, no raylib.
  levels.c      the twelve layouts, as ASCII pictures
  ghostlink.c/h Ghost Arcade data contract (launcher name in, scores out)
  persist.c/h   settings + save data (XDG dirs)
  input.c/h, audio.c/h
  render.c/h    all drawing, plus render-only effects (trails, particles,
                shake) driven by the rules' one-frame events
  main.c        window, app state machine, keyboard/mouse -> Game_SetInput
tests/test_main.c   headless suite
tools/gen_sounds.py, tools/gen_icon.py   stdlib-only asset generators
```

Physics runs at a fixed 120 Hz whatever the frame rate. At the speed cap a
ball moves under 5 px per step -- less than its own diameter -- which is what
rules out tunnelling without swept collision tests, and the test suite fires
balls at a solid wall at top speed from many angles to prove it. A minimum
vertical share of the velocity after every bounce means a ball can never get
stuck rallying flat between the side walls.

The tests also cover the level parser, that every built-in layout is
clearable (a flood fill that treats steel as solid must reach every
breakable brick), paddle steering symmetry, brick hit axes and hit points,
bomb chains, every capsule and its expiry, the ball limit, lives, level
clear and wrap-around, update timing, and several long bot-played games that
check the state stays sane and that the same seed and inputs always produce
the same game.

Levels are plain strings read only through `Level_Parse()`, so a level is
tested exactly the way it is played. Adding one is typing a picture into
`levels.c`.
