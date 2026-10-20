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
| `song`      | the `Song` data model and `.gsong` text format, the player's library (`$XDG_DATA_HOME/ghost-arcade/music/`) and the per-game assignment file (`music.ini`) that Chipforge writes | no |
| `musicsynth`| the chiptune synth: plays a `Song` (bass, arp, lead, drums) with intensity-gated layers, and composes the twelve built-in game themes into one-pattern songs (`Music_ThemeToSong`); renders WAV files headless | no |
| `music`     | what the games call: streams the synth through raylib with modes, crossfades and the muffled pause; prefers the Chipforge song assigned to the game in `music.ini` over its built-in theme | yes |
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
