# Blockfall

A lightweight, native falling-block puzzle game for Linux, built with
[raylib](https://www.raylib.com) and plain C, and one of the eleven games of
the Ghost Arcade. Built and tuned for Omarchy (Hyprland/Wayland on Arch), but
it's a normal X11/GLFW desktop app and should run on most Linux desktops.

All visuals and sound effects are original/procedurally generated for this
project. No copyrighted Tetris artwork, audio, or branding is used, and the
project is deliberately named and coded independently of the trademarked
"Tetris" name.

## Playing

The main menu is a living title screen: the letters of BLOCKFALL drop in as
the game starts, faint pieces land on a skyline along the bottom (full rows
flash and clear), Start game names your last setup, and **Your bests** shows
your Marathon, Sprint, Ultra and Power bests on the board you last set up.

"Start game" opens **Play setup**, remembered from last time:

- **Mode:** a list of Marathon (endless, the pace climbs every ten lines),
  Sprint (clear 40 lines as fast as you can), Ultra (two minutes), Zen (no
  game over: a full stack is swept away and play carries on) and Power. Up
  and Down pick one; the right side says what it is and how it ends, and
  shows your best and the world best on the board.
- **Board:** Classic 10x20, Wide 14x20, Tall 8x26, Huge 18x30, Tiny 6x14, or
  Custom: any width from 6 to 24 and height from 12 to 36. Left/Right, LB/RB
  or `[` `]` change it from anywhere; Width and Height always work (hold to
  repeat, faster after a second), and a size that matches a preset is that
  preset. A preview shows the board at its true shape, and "In play" the
  cell size it will have. The board scales to fit the window.
- It opens on last time's mode, so Enter, Enter plays your last setup again;
  Enter on any mode row (or Width, Height) starts, and so does Start.
  "Scores for this board" opens its Scores.
- **Power mode:** about one piece in seven carries a power-up on one of its
  blocks, and it fires when the piece lands, after any lines it completed
  have cleared: **Bomb** (clears the 3x3 around it), **Laser** (clears its
  whole column), **Freeze** (gravity waits six seconds), **Slow** (gravity is
  2.5x slower for twelve) and **Sweep** (the bottom three rows go).
- **Hold** a piece (once per piece). Combos and back-to-back four-line clears
  score extra on every board except the classic Marathon, whose scoring is
  unchanged so old scores still mean what they did.
- **Lock delay:** a landed piece locks after half a second. A move or turn
  on the ground restarts that up to 15 times; reaching a lower row than the
  piece has been on gives the 15 back. A piece that locks partly above the
  top of the well ends the run, the same as reaching the top (Zen sweeps).
- Esc, P, pad Menu (Start) or pad B open the **pause menu**, and the game
  opens it by itself when its window loses focus (Settings > Game > Auto
  pause). A run that ends (a top-out, a
  finished Sprint, Ultra's time up) ends on a **results card** (see Around a
  run); one you leave from the pause menu, and Zen, which never ends, go
  straight back to the menu.

## Settings

Five tabs across the top: **Controls, Feel, Look, Sound** and **Game**. LB
and RB, Tab and Shift+Tab, Left/Right on the tab bar or a click switch them;
Settings opens on the tab you left it on. Up/Down move from row to row (the
tab bar sits above the first), Left/Right change a value or move along a
row, Enter, Space or pad A press. A line under the tab says what it is for,
a line at the bottom explains the row you are on (or, with the mouse, the
one under the pointer, or why a faint control cannot be used). Every change
saves at once; rows marked * are shared with every game.

- **Controls:** a table of the nine actions (Move left, Move right, Soft
  drop, Hard drop, Rotate right, Rotate left, Hold, Pause, Restart), each
  with a key, a second key, a pad button and a second pad button, named in
  words (Left Shift, not LEFT_SHIFT; the pad's buttons as you see them on
  the pad). Press a cap and it waits five seconds for the new key ("Press",
  with a bar draining under it); a pad cap waits for a button. Esc, a click
  or five quiet seconds give up (and Menu on the pad; B gives up on a key
  cap, while on a pad cap B is a button you can set).
  - A key another action already has **swaps places** with yours, and the
    note line says what moved ("C moved from Hold to Rotate left; Hold took
    Z"). The only key (or button) an action has is never taken: the note
    says why and nothing changes.
  - **Esc and Menu always pause**, and a swap never hands them to another
    action. **Enter and Space pick menu items**, so Pause and Restart never
    get them, pressed on a cap or by a swap (Space stays the hard drop, and
    any other action may have either). Shift, Ctrl and Alt bind when let go, so Alt+Enter still only
    goes fullscreen. M (the mute key) and keys Blockfall cannot store are
    turned away with a note. Delete, Backspace or pad Y clear a second key
    or button.
  - **Reset controls** puts every key and button back as Blockfall came,
    asking first ("Keep mine" is picked first).
  - The Pause row's first key and button pause (P, Menu); its second ones
    (Esc, B) also go back a step: out of the pause menu's settings, or from
    the results card to the menu. They were "Back to menu" before 3.0.0.
- **Feel:** how Left, Right and Down repeat in a run.
  - **Repeat delay** (auto-repeat), 50 to 300 ms: how long Left or Right is
    held before the piece slides by itself. **Repeat speed**, 0 to 100 ms:
    a step every so many ms once it does; 0 sends it straight to the wall.
    **Soft drop speed**: Slow (a row every 60 ms), Normal (30, as it always
    was), Fast (15) or Instant (to the floor, without locking).
  - **Presets:** Relaxed (230 ms, 50 ms), **Standard** (170 ms, 35 ms,
    Normal: how Blockfall has always felt, and the default) and Fast
    (110 ms, 15 ms, Fast). The one your values match is lit; any other mix
    says Custom.
  - **Try it:** a strip where a T slides with your values by itself; hold
    Left or Right on it to try them.
- **Look:** **Ghost piece** (Outline, as it always was; Solid; or Off),
  **Grid** (On, Faint or Off), **Block patterns** (a mark of its own on each
  piece's blocks, so you can tell them apart without their colours; a
  sample shows beside the switch), **Controls card** (Shown or Hidden),
  **Piece motion** (Smooth or Snap), **Motion** (Full or Reduced), and the
  shared **Screen shake**, **Reduced flashing** and **Fullscreen**.
- **Sound:** **Effects volume** (ten blocks; each ticks at its own pitch as
  you set it), the shared **Music volume**, **Sound** On/Off and **Test
  sound** (a line clear at the effects volume; faint while the sound is
  off).
- **Game:** **Auto pause** (the run pauses when the window loses focus; on),
  **R restarts** (named with your Restart key: Asks first or At once),
  **Ready, go**, **Resume countdown** (3, 2, 1 after a pause), and **Show
  How to play: Next launch**, a button (its Basics page opens the next time
  Blockfall starts, once; the note says so, and the row's help says it is
  set).

## How to play

Five pages, with tabs and `<` `>` buttons: **Basics, Hold and next,
Scoring, Modes** and **Power-ups**. Each has a small well playing a looping
example on the real rules: a T slides, turns both ways, soft-drops and
hard-drops into a gap for a single; an S goes into Hold and comes back; a
quad (800) and then a back-to-back quad (1 250 with the combo's 50); the
mode you pick plays the single with its own number under the well; the
power-up you pick fires on a stack. The captions name **your** keys, or
your pad's buttons once you use the pad, and the key the example presses
lights up as it presses it. Scoring lists the clears (100, 300, 500, 800
times the level), the level's speed, the combo and back-to-back bonuses,
the drop points, and that the classic 10x20 Marathon keeps the classic
scoring. Left/Right, LB/RB, Tab or a click change the page; on Modes and
Power-ups Up/Down (or a click) pick what the well plays; Enter goes on to
the next page (and back from the last). Within one run of Blockfall, How
to play reopens on the page you left it on, from the menu or the pause
menu, and starts on Basics on a fresh launch.

The first launch shows the Basics page alone, with "More in How to play"
and a Let's play button. Ghost Launcher's About tab shows Blockfall's short
card from the shared How to play text, which names the 3.0.0 keys.

## The play screen

- **It fits every board.** The panel sits beside the well on tall boards and
  becomes a strip across the top of flat ones, whichever gives bigger cells
  (24x12 gets 22 px cells instead of 14; 145 of the 475 sizes grow). Tiny
  boards get a roomy panel with Next and Hold side by side and a big score.
- **Next and Hold.** A used Hold goes grey with a small lock. In Power mode
  each box names the power-up its piece carries, and the block shows its
  letter (B, L, F, S, W).
- **One main number per mode:** the score (Marathon, Power), the lines left
  (Sprint, with the clock under it), the countdown (Ultra: gold from 0:30,
  red from 0:10, ticking each second), the fresh starts (Zen). Level has its
  ten pips; Best says "No best yet" on a new board and turns gold once you
  pass it.
- **Combo and back-to-back** chips light up while they are on ("Combo x3",
  and a gold "B2B ready" after a quad on the boards that score them);
  Freeze and Slow are draining bars.
- **Callouts:** up to three lines over the well (DOUBLE, TRIPLE, QUAD,
  BACK-TO-BACK, COMBO 3, LEVEL 4, a power-up's name, NEW BEST, FRESH START),
  the most important one big. They rise a little, hold and are gone in
  0.9 s (the next clear with words replaces them), sized so they never
  leave the well. A single gets no word, only its
  popup: every clear shows "+points" rising from its rows, gold from 800.
  The score counts up to its new value, a stat that changes nudges up, and a
  level up lights the pips one by one, flashes the frame and brightens the
  grid. Passing your best mid-run says NEW BEST once, with a soft coin.
- **The controls card** shows your keys, or your pad's buttons once you use
  the pad: Move, Soft drop, Rotate, Rotate left, Hard drop, Hold, Pause
  (your Pause key and Esc together; Menu and B on the pad), Restart, plus M
  (mute) and - / = (volume), which show a small chip at the bottom of the
  window. Settings > Look > **Controls card** hides it.

## How play feels

All of this is looks only: the rules, the speed and the Sprint and Ultra
clocks are exactly what they were.

- **Line clears** replay from a copy of the board as the piece was stamped:
  the full rows flash, their blocks zip out from where the piece landed
  towards both walls and burst into pixels that stay in the well, then the
  rows above drop into the gap and settle 2 px, ending on exactly the real
  stack. A quad bursts all four rows at once with gold among the pixels and
  flashes the frame gold. The beat is 0.24 s, 0.18 s at levels 10-14 and
  0.12 s from 15; a new lock ends the last one at once, and the ghost waits
  until the rows have settled. A double and a triple ring higher than a
  single, a triple shakes a little, and a back-to-back quad sounds brighter.
- **The piece** slides in half a cell from the top as it comes in. A single
  tap or a gravity step glides over 0.04 s (never more than half a cell from
  where the piece really is); auto-repeat and soft drop snap, and soft drop
  leaves short streaks. A turn pops 2 px; a turn that cannot be made gives a
  dull tick. A piece on the ground darkens as its half second runs out.
- **Landing:** a hard drop leaves a smear from where it started, squashes for
  two frames, flashes and throws dust sideways; a piece that locks on its own
  settles 1 px with a brighter edge.
- **The ghost** has a light fill (or is solid, or off: Settings > Look),
  and the rows it would complete are marked on the well's frame at both
  sides. The grid can be faint or off, and **block patterns** put each
  piece's mark on its blocks in the well, in Next and Hold, and on the piece
  flying into Hold.
- **Hold and Next:** a held piece flies into the Hold box, whose edge flashes
  once when hold is ready again; Next slides its old piece out as the new one
  comes in.
- **READY, then GO** start each run: READY holds the run for 0.6 s (the
  clock waits; a turn or a hold pressed then happens at GO), and GO shows as
  play begins. After a restart only GO shows, and play starts at once.
- Settings: **Piece motion** Smooth or Snap (cell by cell; Look) and
  **Ready, go** On or Off (Game). **Motion: Reduced** cuts the glides, slides, flights, smears and
  falls (a clear is a short flash, then the rows snap down); **Reduced
  flashing** makes every flash a soft tint of the piece's colour.

## Around a run

- **The pause menu** (Esc, P, pad Menu or pad B; your Pause keys and
  buttons too): the screen dims over 0.12 s and a card slides up, centred on the
  window so it fits every board, with Resume (picked first), Restart,
  Settings, How to play and Quit to menu. Esc, pad B, P or pad Start resume.
  Buttons act when let go, so Enter, Space or pad A on Resume never reach
  play as a hard drop. **Settings** turns the card into a quick panel:
  effects and music volume (you hear them at once), sound, the ghost piece
  and screen shake, saved at once. How to play opens over the run and comes
  back to the menu.
- **"Are you sure?"** Restart and Quit to menu ask first when the run has
  points ("Leave this run? It counts for 12 450 on Classic 10x20."), and so
  do your Restart key and the pad's Restart button in play. Keep playing is
  picked first, and Esc or pad B
  say no. A run with no points restarts or leaves at once. Settings > Game >
  **R restarts: At once** lets your Restart key restart without asking; the
  pad's Restart button and the menu's Restart button always ask.
- **Resume countdown** (Settings > Game, off by default): Resume shows 3, 2, 1
  over the board, 0.3 s each; the clock waits and a turn or hold pressed
  then happens at its end.
- **The end of a run:** one sound (game over, or the cleared sting), then a
  short show on the board: the piece that could not come in blinks red and
  the rows turn grey from the bottom up; a Sprint clear bursts the rows
  upward in their colours; at Ultra's time up they dim. At 0.9 s the
  **results card** drops in over the dimmed screen: GAME OVER (red), SPRINT
  CLEAR (violet) or TIME UP (gold), the score counting up with rising ticks
  (a finished Sprint shows its time). A new best stamps NEW BEST in gold with
  confetti and its jingle ("Old best 24 680" under it); otherwise "Best 26
  714, 11 694 to go" (or seconds behind, for Sprint). Then the run's place on
  this machine and where it would stand on the world board (as last
  synced), a trophy it just earned, and the run in numbers: lines, level,
  pieces and pieces per second, time, quads, best combo, back-to-backs,
  holds and power-ups (fresh starts in Zen). Play again (picked first; your
  Restart key or the pad's Restart button too), Change setup, Scores (that
  board, the run's row blinking) and Menu (Esc or pad B too). The card
  ignores presses for its first 0.4 s, so a hard drop still held never skips
  it; a press after that jumps to the end of the show.
- **The block wipe:** going into and out of a run, a wall of blocks clears
  over the new screen row by row from the top, each row lit and then zipping
  out from the middle like a line clear, 0.25 s in all. Nothing waits for
  it. Motion: Reduced makes these plain cuts, the card drop and the slide
  included (the count-up takes 0.2 s); Reduced flashing holds the blocked
  piece steady red and keeps the wipe and the stamp from flashing white.

## Scores

- **One Best per mode and board.** The classic Marathon's is the higher of
  `save.dat` (the classic best from before the score tables existed) and the
  score table; only classic Marathon runs ever change `save.dat`.
- **Sprint is ranked by time**, fastest first, on its own **Sprint 40**
  boards, and only a finished Sprint goes on them. A row there stores
  10,000,000 minus the time in hundredths of a second (so the faster run is
  still the bigger number every table sorts first); Blockfall and Ghost
  Launcher show it as a time, `1:12.40`. The points-ranked Sprint rows from
  before 2.1 are kept and shown as **Sprint (old)**.
- The five preset boards share their scores with
  [Ghost Launcher](../ghost-launcher) and the online board (`marathon` for
  the classic board, `sprint40-14x20`, `power-18x30`, ...). **Custom sizes
  keep their scores on this machine only**, in Blockfall's own table, so they
  never reach the world board.
- A run counts (runs log, trophies, scores) only if a piece locked and it
  scored, and a tuned run (F2 panel) never counts.
- **The Scores browser:** a tab for each mode (a click, Left/Right or
  LB/RB; Zen's tab is faint and says it keeps no scores), a board picker
  under them (`[` `]`, X or pad X, a click or the wheel) listing the five
  presets, the size set up in Play setup and every size with scores on this
  machine or the world, with Sprint's old points boards last as "(old)";
  This machine | World (a click, Up/Down) and All time | This week | Today
  (a click, `T` or pad Y). The table keeps its medals, your rows lit and
  where you stand ("You are #4, 15 570 behind #3"), and under the classic
  Marathon table your classic best from before the tables existed.
  **Play this board** starts a run in that mode on that board.

## Controls

Every key and pad button in a run can be changed in Settings > Controls
(two keys and two buttons per action); these are the defaults.

| Action        | Keys               | Xbox pad              | Mouse |
|---------------|--------------------|-----------------------|-------|
| Move          | Left / A, Right / D | D-pad or stick left, right |  |
| Soft drop     | Down / S           | D-pad down            |       |
| Hard drop     | Space              | A                     |       |
| Rotate right  | Up / W             | D-pad up              |       |
| Rotate left   | Z                  | X                     |       |
| Hold          | C                  | Y / LB                |       |
| Pause menu    | P / Esc            | Menu (Start) / B      |       |
| Restart       | R (asks first when the run has points, unless Settings says At once) | View (the pad's Restart button always asks first) | |
| Mute          | M                  |                       |       |
| Volume        | - / = (keypad - and + too) |               |       |
| Frame rate    | F3                 |                       |       |
| Tuning panel  | F2                 |                       |       |
| Fullscreen    | F11 or Alt+Enter   |                       |       |

- Hard drop, Rotate left, Hold and Restart have no second key until you
  set one. An older config.ini that already uses Z keeps it, and Rotate
  left then starts without a key.
- The keys and the pad are set apart: a rebound key never drops off the
  pad. **Esc and Menu always open the pause menu** too (a config.ini from
  before 3.0.0 that gave Esc to another action keeps it: Esc then does that
  action in play, and Pause's own keys pause). M and - / = show a small
  volume chip and are saved at once.
- **Enter, Space and pad A pick menu items, so they cannot be Pause or
  Restart** (the pause menu would resume or ask to restart instead of
  picking the focused item). Settings turns them away on those two rows
  with a note and never hands them over in a swap; any other action may
  have them (Space and A are the hard drop). A hand-edited config.ini that
  gives them to Pause or Restart loads with that slot as none; when that
  would leave the action with no key or button at all, its first default
  (P or R, Menu or View) comes back instead.

**In the menus:**

| What                         | Keys                         | Xbox pad            | Mouse                    |
|------------------------------|------------------------------|---------------------|--------------------------|
| Move between items (held, they repeat) | Up/Down, Left/Right (W A S D) | D-pad or stick | Point at an item       |
| Press / select (acts when let go) | Enter or Space          | A                   | Click (let go over it)   |
| Back to the screen you came from | Esc                      | B                   | The back button, or [<- Back] |
| Tabs (Settings, Scores) and pages (How to play) | Tab, Shift+Tab | LB / RB        | Click a tab              |
| Change a value               | Left / Right                 | D-pad left / right  | Click its arrows or halves; the wheel over it |
| Board (Play setup, Scores)   | `[` `]` (X in Scores)        | LB / RB (X in Scores) | The board's arrows     |
| Period (Scores)              | T                            | Y                   | Click it                 |
| Clear a second key or button (Settings) | Delete or Backspace | Y               |                          |
| Scroll (Updates)             | Up/Down, PgUp/PgDn, Home/End | D-pad, LB/RB        | The wheel                |

Hints show your keys, or the pad's buttons once you use the pad. On the main
menu the first Esc or B moves to Quit and a second one quits.

The controls card in play and How to play name your keys, and the hint lines
under the pause menu and the results card show keys or pad buttons.
Reduced flashing (Settings > Look, shared) replaces the white flashes with a
soft tint and holds blinking things steady; Screen shake can be turned off.
**Motion** (Settings > Look, Full or Reduced) turns the menus' slides, the
focus bar's glide and the title drop into plain cuts, keeps callouts, popups
and the panel's numbers still, lights the level pips at once and halves the
particles; in play it also cuts the piece's glides, Hold's flight, Next's
slide, the hard drop's smear and squash and the clear's fall (see How play
feels).

## Building and running

Requires `gcc`, `make`, `pkg-config`, and the `raylib` library (6.0+; on
Arch/Omarchy: `sudo pacman -S raylib`).

```sh
make            # builds build/blockfall
make run        # builds and launches it
make test       # the headless test suite (no raylib or display needed)
make test-asan  # the same under ASan/UBSan
make shots      # screenshots of every screen, without showing a window
```

`make shots` (`tools/shots/`) runs the real game in a hidden window with
scripted keys, pad and mouse, no audio and no ghost-sync, each script in its
own temp HOME, and checks with hyprctl that the window never maps. It writes PNGs and a
log per script to `build/shots/png` (`SHOTS_OUT=` and `SHOTS_SCRIPTS=` pick
others). The scripts double as regression checks: `expect` lines check the
game, files and pixels, and a run fails if any does.

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
distros, including Omarchy). `make uninstall` removes all of the above and
never your saves or scores.

## Data locations

Following the XDG Base Directory spec, nothing is written inside the
project/source directory during play:

- Settings (keys and pad buttons, Feel, Look, Sound and Game, the last play
  setup): `$XDG_CONFIG_HOME/blockfall/config.ini`. Every line an older
  Blockfall wrote is kept as it was; 3.0.0 adds its own (`rotate_left`,
  `hard_drop2`, `hold2`, `restart2`, `pad_left` ... `pad_restart2`,
  `das_ms`, `arr_ms`, `soft_drop_speed`, `grid`, `block_patterns`,
  `auto_pause`, `show_howto`), which an older build ignores. A key with
  none is written `NONE`.
- The classic Marathon best (score, level, lines):
  `$XDG_DATA_HOME/blockfall/save.dat`
- Custom-size scores (this machine only): `$XDG_DATA_HOME/blockfall/scores.txt`
- The runs log: `$XDG_DATA_HOME/blockfall/runs.log`
- Shared with Ghost Launcher: `$XDG_DATA_HOME/ghost-launcher/scores/blockfall.txt`
  (this machine) and `scores/global/blockfall.txt` (the world board), plus
  achievements and your player name
- Shared by every game: `~/.config/ghost-arcade/prefs.ini` (music volume,
  screen shake, reduced flashing, fullscreen)

All are plain, human-readable text files.

## Project structure

```
src/
  tetromino.c/h   piece shapes, rotation states, colours
  board.c/h       the well at any size, collision, locking, line clears,
                  the power-up board effects
  game.c/h        the rules: gravity, lock delay, kicks, hold, scoring,
                  levels, modes, power-ups, top-out and lock-out, and the
                  one-frame events the sounds and effects react to
  runrecord.c/h   what a run records: which runs count, save.dat, the
                  boards (Sprint by time, custom sizes local), the end sound
  localtable.c/h  this machine's own score table for custom sizes
  controls.c/h    the nine actions' keys and pad buttons, clash swaps (Esc
                  and Menu never handed over, no action left without a
                  key), key and button names for the screens (no raylib)
  setpage.c/h     Settings' tabs: each tab's rows and controls, the arrows'
                  walk, what each change does, Feel's presets and try-it
                  strip, the wait for a key (no raylib)
  howtodemo.c/h   How to play's moving examples: short scripts of presses
                  played on the real rules (no raylib)
  look.c/h        block patterns, a mark per piece (no raylib)
  layout.c/h      where the play screen puts things on every board size (the
                  panel at the side or in a strip), and how text is sized to
                  stay inside the well (no raylib)
  hudfx.c/h       the play screen's lively parts: callouts, popups, the
                  count-up, nudges, the level-up pips (no raylib)
  feel.c/h        how play feels: the line clear's replay from the board
                  as it was stamped, the piece's glide, slide in and pop,
                  landing, the ghost's rows, Hold's flight, Next's slide
                  (no raylib)
  playstep.c/h    one frame of a run: auto-repeat (with your Feel
                  settings), the moves, gravity, READY-GO and the countdown
                  after a pause (no raylib)
  pausemenu.c/h   the pause menu, its quick panel and "are you sure?" box,
                  and what that box says (no raylib)
  results.c/h     the end show's timeline and the results card: its words,
                  numbers, buttons and input (no raylib)
  wipe.c/h        the block wipe into and out of a run (no raylib; drawn
                  in scr_common.c)
  persist.c/h     config.ini and save.dat
  repeat.c/h      auto-repeat (DAS/ARR) for moving and soft drop
  render.c/h      the play screen's frame: the header, the three parts
                  below, then what scr_play.c lays over it (the pause menu,
                  the results card), the volume chip and the wipe
  well.c          the well: frame, grid, stack (and a clear replaying),
                  the landed piece, ghost, falling piece, the end show
  effects.c       what happens in it: blasts, shake, the well's pixels,
                  callouts, READY and GO, the countdown, a Sprint clear's
                  burst
  panel.c         the panel or strip: Next, Hold, stats, chips, the card
  renderpriv.h    what those four share
  blockdraw.h     the play screen's block, for the menus' blocks too
  main.c          the window and the loop: reads input once a frame, updates
                  the screen on top, draws exactly one frame per pass
  app.c/h         the Back stack and what the screens share (no raylib)
  screens.h       the screens, one file each:
    scr_menu.c      the main menu (title drop, skyline, Your bests)
    scr_setup.c     Play setup (modes, board, preview, bests, Start)
    scr_play.c      the run, recording it, the pause menu and the results
                    card
    scr_scores.c    the Scores browser, on the button kit (its table:
                    scoresview.c)
    scr_settings.c  Settings with tabs, on the button kit: reads the keys
                    while one is being set, saves, plays the sounds, draws
    scr_howto.c     How to play with moving examples, on the button kit
    scr_updates.c   Updates (changelog.h), on the button kit
    scr_common.c    menu sounds, the slide, the wipe, the title, the Back
                    button
  actions.c/h     what keys and the pad do in a run (no raylib)
  menukeys.c/h    Blockfall's own menu keys: W A S D, [ ], T, X, Delete
                  (no raylib)
  menumodel.c/h   what the menus decide: title drop, slide, held steppers,
                  the setup's board rules, the preview (no raylib)
  backdrop.c/h    the menu's skyline of landing pieces (no raylib)
  comfort.c/h     the Motion and Reduced flashing switches every effect asks
  scoresview.c/h  the Scores browser's table (times for Sprint 40, the
                  blinking new row, the classic best from before the table)
  changelog.h     the Updates screen
tests/            headless tests (no raylib): rules, records and files,
                  the play screen's layout on all 475 board sizes in every
                  mode, its callouts and count-up, the clear's replay
                  against the real clear, the piece's motion, READY-GO and
                  the run's frame against 2.3.0's, the button kit and input
                  reader, play keys, the menus' rules, the Back stack, and
                  around a run: the run's counters against modern scoring,
                  the pause menu and its box, the results card and its
                  timeline, the wipe, the countdown, the boards Scores
                  lists, the 2.5.0 settings; and Settings with tabs:
                  config.ini old and new, key words, binding (a fuzz),
                  Feel against the built-in feel, every tab's page, and
                  every How to play demo against what its page says
tools/
  shots/          the hidden-window screenshot harness (`make shots`)
  gen_sounds.py   generates the synthesized .wav sound effects
assets/           sounds, the pixel font, the icon
packaging/        the XDG desktop entry
```

The rules (`tetromino.c`, `board.c`, `game.c`) and everything that decides
what is recorded, what a key does or where things go on screen
(`runrecord.c`, `localtable.c`, `controls.c`, `layout.c`, `hudfx.c`,
`feel.c`, `playstep.c`, `persist.c`, `repeat.c`, `actions.c`, `menumodel.c`,
`backdrop.c`, `app.c`, `pausemenu.c`, `results.c`, `wipe.c`, `setpage.c`,
`howtodemo.c`, `look.c`) have no
dependency on raylib, which is what lets `make test` check them without a
display. The shared Ghost Arcade code (the house style, sounds, music,
scores, fullscreen and more) lives in `../ghost-common`. Blockfall is the
first game to use its opt-in menu kit there: the input reader (`uiinput`),
the key table (`uikeymap`), the Back stack (`uinav`), the button kit
(`uikit`, `uikitdraw`) and the text field (`uitextfield`), copies of Ghost
Launcher's own, which only Blockfall builds for now.

## Performance notes

- Rendering uses raylib's OpenGL backend with vsync (`FLAG_VSYNC_HINT`) plus
  a `SetTargetFPS(60)` safety cap, so the game paces itself to the display
  refresh instead of busy-spinning the CPU/GPU.
- All textures/sounds are loaded once at startup; nothing is allocated or
  read from disk during the per-frame game loop (the Scores screen re-reads
  its table on a change and every two seconds).
- The board is a fixed `int8_t[40][24]` with no dynamic allocation anywhere
  in the core game logic.
- Auto-repeat is timed in real seconds (not frame counts), so movement feel
  stays consistent regardless of frame rate; a stalled frame counts as at
  most 0.1 s.
- Settings and save files are only touched on actual state changes (a setting
  change, a volume key, the end of a run, or quit), never every frame.
- How to play's examples run a small game of their own (an 8x12 well) only
  while the page is open.
