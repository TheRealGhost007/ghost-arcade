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
  best from the online board, and time played, with one status line (play,
  install, an install that failed and where its log is, or why Daily did
  nothing) that always fits its column
- **A hall you can click and drive from the pad:** point at a side machine
  and it glows (with the hand pointer); click it and it slides to the
  middle; click the lit machine and the coin drops. The wheel moves one
  machine per notch. Along the bottom, a control strip of buttons -- Play
  (Install, Installing), Daily (faint, with the reason, on machines that
  have none), This game, Stats, Menu, admin's Add/Edit/Remove and
  `< 4 / 12 >` -- each showing its key, or after pad use its pad button
  (house colours: a grey disc with the letter; Settings > Screen > Button
  hints can hold either). The header's Playing as
  (a red "Set your name" when there is none), NEW VERSION and Menu are
  chips too. The pointer hides once keys or the pad take over, and comes
  back when the mouse moves
- **Your row:** the launcher opens on the machine you played last
  (Settings > Player > Start on: Last played or First machine). `F`, or the
  Favourite button on the card, puts a gold star on a machine's marquee and
  brings it to the front; `T` steps the row order -- your order, recently
  played, most played, A-Z -- and favourites always come first (the order is
  named over the row when it is not your own). Settings > Machines lists every
  machine in your order, with its status (Installed, Not installed,
  Installing) under its name, and Favourite, Up, Down and Hide (a hidden machine
  keeps its scores; one machine always stays on the row). It is all yours, in
  `launcher.ini` under each machine's program name, never in `games.txt`; the
  row is put in order only when the launcher opens or you change something,
  so machines never shuffle while you browse, and a wrap from the last
  machine to the first jumps instead of sliding past the whole row
- **Hall music:** the hall plays the built-in "ghostarcade" chiptune softly
  (bass, arpeggio and hats) at the shared music volume, or a Chipforge song
  picked on Chipforge's page (Songs) or given to Ghost Arcade (launcher) on
  Chipforge's assign screen. It fades out for the coin drop and is silent
  while a game the launcher started is running and whenever the launcher is
  not the window in front. Settings > Sound > Hall music turns it off
- **The Daily challenge desk** (the chalkboard on the hall's wall, the
  Arcade menu, `Shift+D`, or a machine page's Daily challenge): today's
  date, your streak (days in a row with a daily played; it lives until a
  whole day passes without one) and your best, the last fourteen days as
  dots, and a row for each daily game on your row -- played today, your best
  today, the world's best today and your place on that board. One game is
  starred "Featured today", picked from the date alone so everyone sees the
  same one (each daily game has a day in every round of eight, never two
  in a row). Enter, A
  or a click on a row goes back to the hall and drops that machine's daily
  coin; a machine that is not installed is faint, with the reason. The
  chalkboard reads, for example, "DAILY 2 of 8 today", with a chalk box per
  game, filled once played. `D` on the hall still starts the lit machine's
  daily at once. It all comes from each game's `runs.log` and the daily
  score boards; nothing new is stored
- **Attract mode:** after a minute with nothing pressed on the hall
  (Settings > Screen > Attract mode: Off, 1 min or 2 min) the card slides
  away, a large PRESS ANY BUTTON stands where it was (steady with Reduced
  flashing), and every six seconds the row glides on to the next installed
  machine and plays its demo. Any key (Shift alone too), pad button, click,
  wheel or mouse move wakes it and the row returns to your machine; that
  press does nothing else, and neither does a click or the rest of a wheel
  flick in the moment after it, so waking never drops a coin or moves you. It never starts while a game the
  launcher started is running, under the menu or a box, or on an empty row
- **Chipforge is a jukebox:** its machine has an arched top in a neon tube,
  bubble tubes up its sides and a speaker grille, and its screen scrolls the
  notes of the song the hall is playing (lead, arpeggio and bass, the drums
  as dots); with hall music off it shows the song's title card. The same
  screen fills its page
- **Quiet hall:** while a game the launcher started is running and the
  launcher is behind it, the hall rests at 10 frames a second with nothing
  moving, and is back at 60 the moment you return
- **A page for every machine** (`I`, pad X, This game on the strip, or a
  click on the card; Enter on the row still plays at once): its name and
  description, a strip of numbers (best, time played, runs, last played,
  trophies), its attract demo drawn large, and the buttons Play (Install, or
  Open Chipforge), Daily challenge (faint, with the reason, where there is
  none), Name for this game and Favourite. Tabs on the right: About (the
  game's own how-to card: goal, controls, tip), Scores (this computer's top
  ten beside the world's, board by board -- `X` or pad X steps the board --
  with your rows highlighted, your world place, when the boards came down
  and how today's daily went) and Trophies. Chipforge's page has About and
  Songs: the hall's song, picked from your Chipforge library (Up/Down from
  the buttons into the songs, Enter, A or a click; "In the hall" marks the
  one playing; it writes the same `music.ini` line Chipforge's assign screen
  does, and the hall crossfades to it), or who plays what (`X`, pad X or the
  arrows switch). PgUp/PgDn, LT/RT or the arrows by the X turn to the
  machine before or after; Play goes back to the row and drops the coin
  there, and Daily challenge opens the daily desk on this machine
- **Chipforge is a tool, not a game:** Play reads Open Chipforge, its card
  counts the songs in your library and names the hall's song, and it is left
  out of the run counts, trophy totals and dailies
- **Settings** (`Tab`, the Arcade menu, or a click on "Playing as"): tabs
  across the top -- Player, Sound, Screen, Online, Controls, Machines -- switched with
  Tab/Shift+Tab, the pad's LB/RB or a click. Every row is a button, an
  On/Off switch or a ten-block volume bar, and every change saves at once
  and says "Saved". Rows marked `*` (music volume, fullscreen, reduced
  flashing, screen shake) are shared with every game: they live in the
  games' `prefs.ini`, which the launcher reads again right before each
  save. Hall music, Hall sounds (the coin and the clicks, 0 = mute), Start
  on, the row order, favourites, hidden machines, Attract mode and Button
  hints (Auto, Keyboard or Xbox pad: what the strip, the menu and every hint
  line name; Auto follows what you used last) are the launcher's own, in
  `~/.config/ghost-launcher/launcher.ini`. Controls is a reference
  card of every key and pad button, screen by screen, printed from the
  same table the launcher reads its keys from
- **One name for every game** (Settings > Player, or `U` to type it at
  once), with an optional name for a single game (listed per machine with
  Set/Edit/Clear, or `Shift+U` on the selected machine). Games read it for
  their score tables. Typing has a cursor (Left/Right/Home/End), Backspace
  and Delete that repeat, Ctrl+V, and a counter; new names stop at 32
  characters (what the online boards keep), and `|` never goes in. On the
  pad, an arcade high-score **name board** opens under the name: A-Z, 0-9,
  space . - _ !, Del and Done, picked with the d-pad and A (X deletes, Y a
  space, Menu saves, B puts the old name back); the mouse can click its
  letters and the keyboard still types
- **Online leaderboard** via the bundled `ghost-sync` helper (see
  `ROADMAP.md`); the launcher refreshes the boards in the background at
  startup. Settings > Online turns sharing and update checks on or off,
  checks for updates or syncs scores on demand, and shows the server
  (which stays file-only on purpose)
- **Admin mode**, unlocked only on the owner's machine (see "About the admin
  lock" below): add, edit (including changing the icon), and remove catalog
  entries from inside the app -- `A`, `E` and Delete on the hall, Edit and
  Remove on a machine's page, or Add a machine, Edit and Remove in Settings >
  Machines. The form shows all five fields at once (Tab, Up/Down or a click
  between them, Enter goes on to the next and from the last to Save), says
  "found" / "not found" beside each path, refuses `|` and a second machine
  of the same name, and has Save and Cancel. Renaming a machine says what is
  kept under the old name (its playtime and its own player name) and Carry
  them over (on at first) moves them; pointing it at another program says its
  scores and trophies stay with the old program name, and carries your row
  settings (star, place, hiding) over
- **Playtime** (`L`): total time the launcher has been open and per-game
  time, most-played first
- **The Arcade menu** (Esc, pad B or Menu, or the Menu chip by the X): a
  panel over the hall's left wall that reaches every screen -- the Daily
  challenge desk, Settings, Stats, Trophies, Updates -- and Leave the
  arcade. Each item shows the key that still opens it straight from the
  hall (the desk's Shift+D is on the Controls cards)
- **Buttons for the mouse** on every screen: [< Back] top left, the X, the
  first-run Yes/No answers, the trophy arrows, Check now, every Settings
  row. A click acts when you let go over the same button (drag off to
  cancel)
- **Asks before it matters:** Delete asks before a machine leaves the row,
  a changed admin form asks before it is thrown away, and leaving asks only
  while a game or an install the launcher started is still going. The X
  quits at once. Every save says "Saved" or "Couldn't save"

## Controls

| Action | Keyboard | Xbox pad | Mouse |
|---|---|---|---|
| Move along the row | Left/Right (Up/Down and W/S also work); hold to keep moving | d-pad or stick (hold to keep moving) | click a side machine; the wheel, one per notch; the strip's `<` `>` |
| Five machines along | PgUp / PgDn | LB / RB | |
| Play the selected game (or install it, if it is missing) | Enter or Space | A | click the lit machine, or Play |
| Daily challenge (games that have one) | D | Y | Daily |
| Daily challenge desk | Shift+D (the same keys close it again) | Menu, then Daily challenge | the chalkboard on the wall |
| On the desk: choose a game / play its daily | Up/Down / Enter or Space | d-pad / A | point at a row / click it |
| This game's page | I (H still opens Trophies) | X | This game, or a click on the card |
| On a page: the buttons / the tabs | Up/Down, Enter / Left/Right, Tab | d-pad, A / LB/RB | click |
| On a page: next board (Scores) or song list (Chipforge) / machine before or after | X / PgUp, PgDn | X / LT, RT | the board's arrows / the arrows by the X |
| On a page: the daily desk on this machine | D (Shift+D works too, as on the hall) | Y | Daily challenge |
| Chipforge's Songs: give the hall a song | Up/Down past the buttons, then Enter | d-pad, then A | click the song |
| On a page: back to the hall | Esc or I | B | [< Back] |
| Favourite: a gold star, first on the row (again to take it off) | F | Settings > Machines | Favourite on the card |
| Next row order (yours, recently played, most played, A-Z) | T | Settings > Machines | |
| Arcade menu | Esc | Menu or B | Menu (header or strip) |
| In a menu or on a screen's buttons | arrows, then Enter or Space | d-pad, then A | point and click |
| Back one screen | Esc | B | [< Back], or the mouse's back button |
| Settings | Tab | from the Arcade menu | "Playing as" (opens Player) |
| Settings: next / previous tab | Tab / Shift+Tab | RB / LB | click a tab |
| Settings: change a switch or a volume bar | Left/Right, or Enter | d-pad left/right, or A | click a half, a block, drag, or the wheel |
| Stats / Trophies / Updates | L / H / V (the same key closes it again) | View for Stats; the rest from the Arcade menu | Stats on the strip; NEW VERSION for Updates |
| Online settings | O | from the Arcade menu, then RB | |
| Type your name (every game) / a name for just this game | U / Shift+U (Enter saves and comes back, Esc puts it back) | Settings > Player, A on Edit: the name board | click the name |
| While typing a name | Left/Right/Home/End, Backspace/Delete, Ctrl+V | name board: d-pad, A types, X deletes, Y space, Menu saves, B puts it back | the board's letters, Save, Cancel, or click away to keep it |
| Check for updates now (Updates screen) | R | View | Check now |
| Update now, or try again (Updates screen) | U | X | Update now / Try again |
| Change game (Trophies) | Left/Right (Enter steps on) | d-pad (A steps on) | the arrows beside the name, or the wheel over them |
| Scroll Stats or Trophies | Up/Down, PgUp/PgDn, Home/End | d-pad, LB/RB | the wheel |
| Fullscreen | F11 or Alt+Enter | | |
| Wake attract mode (and only that) | any key, Shift alone included | any button | move, click or the wheel |
| Leave the arcade | Esc, then Leave the arcade | Menu, then Leave the arcade | the X (at once), or the EXIT sign |
| Add a game *(admin only)* | A | | Add, or Settings > Machines > Add a machine |
| Edit selected game *(admin only)* | E (on the hall or its page) | | Edit (strip, page, Settings > Machines) |
| Remove selected game *(admin only, asks first)* | Delete (on the hall or its page) | | Remove (strip, page, Settings > Machines) |
| The admin form: next field / previous / Save | Tab or Down, Enter / Shift+Tab or Up / Enter on Save | d-pad, A | click a field, Save, Cancel |

## The hall

Everything behind the cabinets is drawn in code, in layers back to front: a
wall with faint panel seams, a row of dim machines far down the hall (so the
room looks full however short the catalog), ceiling spotlights with their
pools on the carpet, a neon tube that occasionally stutters, an EXIT sign,
the confetti carpet, a strip light along the foot of the wall, and a few dust
motes drifting through the light. On the right-hand wall hangs the daily
chalkboard; Chipforge stands among the cabinets as a jukebox; and left alone
for a minute the hall goes into attract mode and shows its machines off. The ambient light **eases toward the colour
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
- The launcher's own settings: `$XDG_CONFIG_HOME/ghost-launcher/launcher.ini`
  (`hall_sounds=0..10`, `hall_music=on|off`, `button_hints=`,
  `attract_mode=off|1min|2min`, and your row:
  `row_order=yours|recent|most|az`, `start_on=last|first`, `last_played=`,
  and per machine, by program name, `fav.<name>=1`, `hide.<name>=1`,
  `place.<name>=N`, `played.<name>=<time>`); the ones shared with every game:
  `$XDG_CONFIG_HOME/ghost-arcade/prefs.ini`. The hall's song is the
  `ghostarcade=` line of `$XDG_CONFIG_HOME/ghost-arcade/music.ini`, which
  Chipforge's assign screen and Chipforge's page in the launcher write. The time once played under Gemdive's old name, Gemdelve,
  is added to Gemdive once (`migrated.gemdelve=1` marks it done)
- Scores the games write: `$XDG_DATA_HOME/ghost-launcher/scores/<game>.txt`;
  online boards downloaded by ghost-sync: `scores/global/<game>.txt`. ghost-sync
  asks for every regular board but only the daily boards of the last 14 days
  (the server keeps every daily forever, and they would crowd the later games
  out of the answer), each board's #1s first, so an answer cut short loses the
  bottom of every board rather than whole games. A machine's page reads both
  files

## Building

```sh
make          # builds build/ghost-launcher and build/ghost-sync (needs libcurl)
make test     # headless tests: score reader, profiles, sync data layer (the board download's 14-day daily filter), input, the key table, the text field, Settings, the button kit, the hall (hit testing, pad map, strip), the name board, your row (every order, favourites, hiding, place vs catalog line), the Gemdelve fold, the hall music rules, a machine's page (boards per mode, tabs, keys), the admin form (every field at once, validation) and the rename carry-over, the daily desk (streaks across month and year ends, the featured pick, its rows and keys), attract mode (the idle timer, the tour, the wake rules), the jukebox's note window and Update now (log steps, failure words, update.txt for this build, a real background run of tools/fake-update.sh)
make shots    # screenshots of every screen from a hidden window (tools/shots), into build/shots/png
make shots-asan  # the same scripts under ASan/UBSan
make monkey   # random keys, pad and mouse at the ASan build (MONKEY_RUNS, MONKEY_FRAMES, MONKEY_SEED)
              # tools/shots/scripts/gallery.txt draws every button-kit control in every look
make run
make install  # installs to ~/.local/{bin,share}, adds a desktop launcher
```

## Adding a new game later

With admin mode unlocked (your own build, on the machine you ran
`make admin-lock` on), press `A` on the hall (or Add a machine in Settings >
Machines) and fill in: name, the program's path, an icon (optional), a
description (optional) and a source folder to install it from (optional). Or
hand-edit your catalog file directly — the launcher re-reads it on startup.

## What's not built yet

- A machine added to Ghost Arcade in a later version does not appear on an
  install that already has its own `games.txt`; admin adds it by hand (Add a
  machine in Settings > Machines). The plan's "New machines in this version"
  notice is still to come.
- The "download games" / distribution side (hosting builds somewhere like
  GitHub Releases and fetching a remote manifest) — this version only
  manages games already installed locally.
- Playtime/profiles are launcher-local; they don't sync anywhere and
  individual games don't currently read the profile username. Tying this
  into the online Supabase leaderboard discussed for Blockfall would need
  that backend set up first.

## Daily challenge

Press **D** on Brickburst, Skyraid, Rockdrift, Lanehop, Crawlshot, Moondrop, Girderclimb or Blockfall to launch it with
`--daily`. Everyone gets the same seed for the local date, the version label shows `DAILY` (Blockfall opens on its Daily
challenge screen instead: a two-minute score attack whose twist changes with the date), and scores go to a separate
`daily-YYYYMMDD` table so they never mix with normal runs. Code: `ghost-common/daily.c`. The daily desk (**Shift+D**,
the chalkboard, the Arcade menu) lists all eight with today's scores, the streak and the featured game (`src/desk.c`;
each game is featured once in every round of eight days, never two days running).

## Fullscreen and the F2 tuning panel

- **F11** (or Alt+Enter) toggles fullscreen in every game and the launcher; it is also a row in each game's Settings (and in the
  launcher's Settings > Screen) and is remembered
  (`~/.config/ghost-arcade/prefs.ini`). Games draw at their normal size into a texture that is scaled to the screen (whole-number
  scaling when little space is wasted, smooth otherwise) with black bars, and the mouse is mapped back. The launcher reads that
  file again when it gets the focus back and when a game it started closes, so F11 there never undoes a setting changed in a game.
- **F2** in a game opens the tuning panel and holds the game still. Every game has **Game speed**; Moondrop also has gravity, engine thrust and
  fuel burn, Skyraid enemy fire delay and ship speed. Left/Right change a value (Shift = 5x), R resets a row, Backspace all, **S saves**
  (`~/.config/ghost-arcade/tuning/<game>.ini`), F2 or Esc closes. While anything is off its default, runs are not recorded, so the scoreboards
  stay honest. Add a knob with `Tune_Add()` in ghost-common/tuning.h.

## How to play

Every game has a **How to play** card (goal, controls, one tip): it shows the first time you start a game and is on each game's menu.
The text for all eleven lives in one place, `ghost-common/howto.c`.

## Stats screen (L)

The old playtime list is now a table: per game, time played (with a bar), number of runs (from each game's `runs.log`), trophies and the date
of the last run, plus arcade-wide totals and a chart of runs over the last 14 days. Chipforge's row shows its time and "a tool"; it has no runs
or trophies to count.

## Online sharing is opt-in

Nothing is uploaded until you say yes. On first launch the launcher asks "Share scores online?" (default No; Esc means No);
change your answer later in Settings > Online (**O** on the game list goes straight there); turning it on syncs at once. The
answer is the `enabled=` line in `~/.config/ghost-launcher/online.conf`; the launcher rewrites only that line (and
`check_updates=`), never the server or key. What is sent when it is on: arcade name, game, mode, score, date and a random
per-install ID.

## Update notices

The launcher tells you when a newer Ghost Arcade has been pushed to GitHub: a teal
**NEW VERSION** pill appears on the game list, and **V** (or the pill) opens the
Updates screen: your build, the newest commit and its title, and when it last checked.
**R** on that screen checks again immediately, as does Check now in Settings > Online,
where Check for updates also turns the checks off.

### Update now

When an update is waiting, the Updates screen has an **Update now** button (U, pad X,
or a click). It runs the arcade's own `./install.sh --update` from the folder this
build was made from, in the background: no terminal, no sudo, the test suites always
on (a build that fails them is never installed). While it runs the screen shows the
steps (Getting the newest version, Building, Testing, Installing), the one under way
lit, with how long it has taken; you can leave the screen and it keeps going, but no
game can be started until it is done (Play says "Updating..." and the dailies are
faint until it is done). Stop works until Installing begins; after that it finishes.
If it ends while you are elsewhere, the hall says so in place of "Press Enter to play"
("The update did not finish. Press V to see why." or "Ghost Arcade updated. Press V
to restart.") until you open Updates, which shows how it went.

- **Done:** "Ghost Arcade is up to date" with the new version, and **Restart the
  launcher** (or Later). Restarting saves what quitting saves, then starts the newly
  installed launcher; it waits (faint, "Close your games first.") while a game or an
  install the launcher started is still running. The notice goes at once: update.txt
  is rewritten for the new build.
- **Failed:** it says why in plain words (your copy of the code has changes of its own,
  GitHub could not be reached, this copy cannot sign in to GitHub (an ssh clone),
  packages are missing and which (git included), the build or the tests failed,
  installing failed), the last lines of the log (git's own `error:` lines kept), and
  where the whole log is: `~/.local/share/ghost-launcher/update.log`. Try again or
  Back. The terminal command is offered only where a terminal could do better (not for
  changes of its own). Nothing is ever reported as installed unless install.sh said so
  and exited cleanly. If update.log cannot be written, it does not start, and says so.
- **One at a time:** the launcher's updater and `./install.sh --update` in a terminal
  both hold `~/.local/share/ghost-launcher/update.lock` (flock) while they run, so a
  second one (another launcher, another terminal) is refused rather than racing on the
  same checkout. An update a launcher before this one left running (you left the
  arcade mid-update) is followed: this launcher reads its update.log for the steps and,
  when it ends, says how it went and offers Restart. One running in a terminal just
  makes games wait; GitHub is asked about the new build when it ends.
- It will not start while games you started from the launcher are open ("Close your
  games first"), while a machine is installing, while another update runs, or when this
  build does not know its git checkout. The button is faint and says why; the old
  terminal command is shown under it then, as a fallback.
- `install.sh` prints plain text (no colours) when its output is not a terminal, never
  asks a question without one (missing packages end it with their names instead), and
  splits `git pull --ff-only` into a fetch and a fast-forward so each failure says which.
- For testing, `GHOST_LAUNCHER_UPDATER=/path/to/script` runs that instead of
  install.sh (a relative path is taken from where the launcher was started); `tools/fake-update.sh` prints the same steps and can fail at any one
  (`FAKE_UPDATE_FAIL=packages|local|network|build|test|install`) or go slowly
  (`FAKE_UPDATE_SLEEP=5`). `make test` uses it, so the tests never run git.

### How the notice works

The build remembers which git commit it was made from. At start-up the
launcher runs `ghost-sync --check-update` in the background, which asks GitHub's public
API for the recent commits on `main` (one anonymous request, at most every six hours,
or straight away when the build has changed) and writes the answer to
`~/.local/share/ghost-launcher/update.txt`. The launcher only reads that file; it never
touches the network itself.

- The file is believed only when its `current=` line is this build's commit. One that
  an older build wrote (you updated since) shows no NEW VERSION, no "1 new" and no
  "N changes newer"; the launcher asks GitHub again instead (unless checks are off).
- Nothing is downloaded or installed until you press Update now (or run
  `./install.sh --update` yourself).
- Nothing about you is sent, and it works whether or not you share scores online.
- If your build was made from commits that are not on GitHub (your own work in
  progress), it says so rather than nagging you to update.
- `check_updates=0` in `~/.config/ghost-launcher/online.conf` turns the checks off
  (Update now still works when an update is already known);
  `update_repo=owner/name` points it at a fork.
- `ghost-sync --check-update-now` does the same from a terminal.
