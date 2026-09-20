# ghost-common

Code shared by every Ghost Arcade game. Games compile these files straight
from this folder (`COMMON_DIR ?= ../ghost-common` in each Makefile, with
`COMMON_MODULES` naming the ones they use) -- there is no library to build
or install.

| Module      | What it is | raylib? |
|-------------|------------|---------|
| `rng`       | xorshift64* PRNG; all game randomness goes through a seeded instance so runs are reproducible | no |
| `ghostlink` | the Ghost Arcade data contract: launcher name in, local scores out, world board in, background `ghost-sync` trigger (spec: `ghost-launcher/ROADMAP.md`) | no |
| `keynames`  | "LEFT"/"A"/"SPACE" <-> raylib key codes, for hand-editable configs and in-game rebinding | yes |
| `sfx`       | sound effects from a game-supplied file table; silent no-op if audio is unavailable; pitched playback | yes |
| `ui`        | the house style: Press Start 2P at 8/16/32/48 only, the indigo palette plus ONE light colour per game, bevelled blocks, pixel particles, and the four screens every game has (menu, scores, settings, updates) | yes |
| `fonts/`    | Press Start 2P (SIL OFL) -- copy into a new game's `assets/fonts/` |  |

Who uses what: **all five games** (Blockfall, Coilrush, Brickburst, Skyraid,
Ghostmaze) use every module. What stays in a game is only what is specific to
it: its rules, its playfield drawing, its settings struct, and its key map
(a small struct built with `Keys_CodeFromName`). Blockfall additionally keeps
`repeat.c/h`, its DAS/ARR auto-repeat for held keys.

Starting a new game: copy Skyraid's Makefile, `persist.c/h`, `main.c` and
`tools/`, write `game.c/h` (rules, no raylib, fixed step, one-frame event
flags) and `render.c/h` (the playfield only), and give it a light colour.

`ghostlink` is tested in Coilrush's suite; `rng` in every game's. A change
to the house style (palette, type scale, a screen's layout) is now made once,
in `ui.c`, and picked up by every game on its next build.
