# Ghost Launcher

A lightweight native launcher for custom retro games (starting with
[Blockfall](../blockfall)), built with raylib and plain C — same stack as
the games it launches, so it stays small and fast.

## Features

- **The arcade floor:** every catalog entry is a cabinet in a scrolling row.
  The selected machine is lit -- marquee, screen, coin slots -- in a colour
  pulled from the game's own icon; the rest stand powered down. Enter
  launches it (forks + execs, detached, so the launcher keeps running)
- **Insert coin:** pressing Enter tosses a coin into the selected cabinet's
  slot -- it arcs in, the slots flash, the credit chime rings
  (`assets/sounds/coin.wav`, synthesized by `tools/gen_sounds.py`), the
  screen warms up, and then the game starts. Just under a second, and the
  row is locked while it plays
- **Install from the launcher:** a game whose executable is missing stands
  dark with a "NOT INSTALLED" sign taped to its screen. If its catalog entry
  names a source folder, Enter runs `make install` there in the background
  (output in `~/.local/share/ghost-launcher/install.log`) and the machine
  lights up when it finishes
- **Instruction card** for the selected game: your best score, the world
  best from the online board, and time played
- **One name for every game** (`U`), with an optional override for a single
  game (`Shift+U`). Games read it for their score tables
- **Online leaderboard** via the bundled `ghost-sync` helper (see
  `ROADMAP.md`); the launcher refreshes the boards in the background at
  startup. Turn it off with `enabled=0` in
  `~/.config/ghost-launcher/online.conf`
- **Admin mode**, unlocked only on the owner's machine (see "About the admin
  lock" below): add, edit (including changing the icon), and remove catalog
  entries from inside the app
- **Playtime** (`L`): total time the launcher has been open and per-game
  time, most-played first
- Close via the X button in the top-right (mouse click) -- Esc is used for
  "back/cancel" within screens, not for quitting

## Controls

| Action | Key |
|---|---|
| Move along the row | Left/Right (Up/Down and W/S also work) |
| Play the selected game (or install it, if it is missing) | Enter or Space |
| Close app | click the X (top-right) |
| Back / cancel a screen | Esc |
| Playtime | L |
| Set your name (every game) | U |
| Set a name for just this game | Shift+U |
| Add a game *(admin only)* | A |
| Edit selected game *(admin only)* | E |
| Remove selected game *(admin only)* | Delete |

## The hall

Everything behind the cabinets is drawn in code, in layers back to front: a
wall with faint panel seams, a row of dim machines far down the hall (so the
room looks full however short the catalog), ceiling spotlights with their
pools on the carpet, a neon tube that occasionally stutters, an EXIT sign,
the confetti carpet, a strip light along the foot of the wall, and a few dust
motes drifting through the light. The ambient light **eases toward the colour
of the selected machine**, so the whole room leans toward the game you are
about to play. The secondary screens (playtime, name, add a game) get a
quieter version -- a gradient, faint beams and the motes -- so their text
stays easy to read.

## Design notes

The look is a dim arcade hall rather than a list UI: indigo wall, confetti
carpet, cabinets drawn entirely from rectangles in `src/render.c` (no image
assets beyond each game's icon). Two rules keep it coherent as it grows:

- **Type is 8, 16 or 24 px, nothing else.** Press Start 2P is an 8x8 pixel
  design; at any other size its strokes come out uneven.
- **A cabinet's colour comes from its icon** (dominant vivid hue), so adding
  a game never means picking a colour, and no two machines look templated.

## About the admin lock

Admin mode (add, edit and remove catalog entries inside the launcher) unlocks on one
machine only: the one its owner ran `make admin-lock` on.

- `make admin-lock` writes `admin.local.mk`, holding a one-way fingerprint of this
  computer: `sha256("ghost-launcher-admin-v1:" + /etc/machine-id)`. The file is
  git-ignored, so neither the machine id nor its fingerprint is ever in the repository.
- `make` bakes that fingerprint into `src/adminlock.c`. With no `admin.local.mk`, which
  is what everyone else building from this repo has, there is no admin mode at all.
- A copy of the owner's binary does not unlock on any other computer.
- `make admin-unlock` removes the file again.

What it is not: a security boundary. The catalog is a plain text file that the person
using the computer can always edit, and someone building from source can lock admin
mode to their own machine. It never gives anyone access to another player's computer,
scores, or the online leaderboard (see `online/SECURITY.md` for that).

## Data locations

- Catalog: `$XDG_CONFIG_HOME/ghost-launcher/games.txt` (seeded from the
  bundled `assets/games.txt` on first run) — one game per line,
  `name|exec|icon|description|install-from`; paths may use `~/`. The last
  field is optional: a source folder with a Makefile that has an `install`
  target
- Playtime: `$XDG_DATA_HOME/ghost-launcher/playtime.txt`
- Achievements: `$XDG_DATA_HOME/ghost-launcher/achievements/<game>.txt` (`id|date` per unlock) and `<game>.runs` (runs played). Games write them when a run ends (six per game, defined in `ghost-common/achievements.c`); the launcher's **H** screen shows them, and a toast appears on the game's menu when you earn one.
- Names: `$XDG_DATA_HOME/ghost-launcher/profiles.txt` (`__DEFAULT__|name`
  for every game, `Game Name|name` for an override)
- Scores the games write: `$XDG_DATA_HOME/ghost-launcher/scores/<game>.txt`;
  online boards downloaded by ghost-sync: `scores/global/<game>.txt`

## Building

```sh
make          # builds build/ghost-launcher and build/ghost-sync (needs libcurl)
make test     # headless tests: score reader, profiles, sync data layer
make run
make install  # installs to ~/.local/{bin,share}, adds a desktop launcher
```

## Adding a new game later

With admin mode unlocked (your own build, on the machine you ran
`make admin-lock` on), press `A` from the list and fill in: name, executable path, icon path
(optional), description (optional). Or hand-edit your catalog file
directly — the launcher re-reads it on startup.

## What's not built yet

- The "download games" / distribution side (hosting builds somewhere like
  GitHub Releases and fetching a remote manifest) — this version only
  manages games already installed locally.
- Playtime/profiles are launcher-local; they don't sync anywhere and
  individual games don't currently read the profile username. Tying this
  into the online Supabase leaderboard discussed for Blockfall would need
  that backend set up first.

## Daily challenge

Press **D** on Brickburst, Skyraid, Rockdrift, Lanehop, Crawlshot, Moondrop or Girderclimb to launch it with `--daily`.
Everyone gets the same seed for the local date, the version label shows `DAILY`, and scores go to a separate
`daily-YYYYMMDD` table so they never mix with normal runs. Code: `ghost-common/daily.c`.

## Fullscreen and the F2 tuning panel

- **F11** (or Alt+Enter) toggles fullscreen in every game and the launcher; it is also a row in each game's Settings and is remembered
  (`~/.config/ghost-arcade/prefs.ini`). Games draw at their normal size into a texture that is scaled to the screen (whole-number
  scaling when little space is wasted, smooth otherwise) with black bars, and the mouse is mapped back.
- **F2** in a game opens the tuning panel and holds the game still. Every game has **Game speed**; Moondrop also has gravity, engine thrust and
  fuel burn, Skyraid enemy fire delay and ship speed. Left/Right change a value (Shift = 5x), R resets a row, Backspace all, **S saves**
  (`~/.config/ghost-arcade/tuning/<game>.ini`), F2 or Esc closes. While anything is off its default, runs are not recorded, so the scoreboards
  stay honest. Add a knob with `Tune_Add()` in ghost-common/tuning.h.

## How to play

Every game has a **How to play** card (goal, controls, one tip): it shows the first time you start a game and is on each game's menu.
The text for all eleven lives in one place, `ghost-common/howto.c`.

## Stats screen (L)

The old playtime list is now a table: per game, time played (with a bar), number of runs (from each game's `runs.log`), trophies and the date
of the last run, plus arcade-wide totals and a chart of runs over the last 14 days.

## Online sharing is opt-in

Nothing is uploaded until you say yes. On first launch the launcher asks "Share scores online?" (default No; Esc means No);
press **O** on the game list to change your answer later. The answer is the `enabled=` line in
`~/.config/ghost-launcher/online.conf`. What is sent when it is on: arcade name, game, mode, score, date and a random per-install ID.

## Update notices

The launcher tells you when a newer Ghost Arcade has been pushed to GitHub: a teal
**UPDATE AVAILABLE** pill appears on the game list, and **V** opens the details (your
build, the newest commit and its title, when it last checked, and the two commands
that update you). **R** on that screen checks again immediately.

How it works: the build remembers which git commit it was made from. At start-up the
launcher runs `ghost-sync --check-update` in the background, which asks GitHub's public
API for the recent commits on `main` (one anonymous request, at most every six hours)
and writes the answer to `~/.local/share/ghost-launcher/update.txt`. The launcher only
reads that file; it never touches the network itself.

- It **never downloads or installs anything**. Updating is always you running
  `git pull && make install`.
- Nothing about you is sent, and it works whether or not you share scores online.
- If your build was made from commits that are not on GitHub (your own work in
  progress), it says so rather than nagging you to update.
- `check_updates=0` in `~/.config/ghost-launcher/online.conf` turns it off;
  `update_repo=owner/name` points it at a fork.
- `ghost-sync --check-update-now` does the same from a terminal.
