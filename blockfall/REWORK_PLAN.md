# Blockfall rework plan

Status: BUILT. Phases 1 to 8 and the chosen extras (Daily challenge, Stats, Dig, Practice) were built, verified and installed on 2026-10-21 as Blockfall 3.6.1; the Power rework, new trophies, attract demo, milestone and faster danger music were not built. Written 2026-10-14 from a code map with hidden-window screenshots, a verified bug audit, and three design proposals (menus and UI, animations and feel, gameplay and modes) merged by a planner. Decisions below are the owner's to make.

## Decided by the owner (2026-10-14)

- **Classic best 74,372 (save.dat):** keep it as the classic best to beat. Nothing in the save changes.
- **Modern rules:** on EVERY board, the classic 10x20 included (not the recommended "all but classic"): standard SRS turning with wall kicks, a 5-piece Next queue, T-spins and perfect clears.
- **Pad layout:** keep today's buttons and fill the empty ones: A hard drop, d-pad Up rotate, X rotate left (new), Y or LB hold, Start pause, B opens the pause menu, View restart (asks first). Every button can be changed in Settings.
- **Extras after the core:** Daily challenge, Stats screen, Dig mode, Practice room.
- **Classic scores under modern rules:** when modern rules land (phase 8), the classic 10x20 Marathon gets a fresh score board; the old table and 74,372 stay visible as "Classic (old rules)".
- **Build pace:** build the core, phases 1 to 6 (version 3.0), verify and install it, then check with the owner before phases 7, 8 and the extras.
- **Defaults kept from the plan's recommendations:** the button kit as opt-in ghost-common files that only Blockfall uses for now; Sprint gets a fresh board ranked by time (old rows kept as "Sprint (old)", the launcher learns to show times); custom board sizes keep their scores on this machine only; line clears are looks only (no added delay); quit and restart ask "are you sure?" when the run has points; Reduced flashing in phase 1 and a Blockfall Motion switch in phase 2.

## Summary

This plan rebuilds Blockfall's menus and play screen in the Ghost Arcade style and adds real animation to the moments you see most: line clears, landing pieces, hold, the start and end of a run, and moving between screens. Phase 1 fixes all 29 confirmed bugs first, including Esc on Play setup closing the game and another mode's score leaking into your menu Best. After that come five more core phases, each one a working Blockfall you can play: new menus, the play screen, how play feels, pause and results, then settings. Two recommended phases (sound and spectacle, modern rules) and a list of extras you choose from come last. The other ten games stay exactly as they are, because the new button kit goes into the shared code as opt-in files that only Blockfall uses for now.

## Blockfall today

- What exists (2.0.0): five modes (Marathon, Sprint, Ultra, Zen, Power), five preset boards plus any size from 6x12 to 24x36, Hold, combos and back-to-back on non-classic boards, five power-ups, scores per mode and size with world boards, keys you can rebind, chiptune music that climbs with the level, achievements, and How to play, Settings, Scores and Updates screens.
- Menus: keyboard-only lists from the shared code. There is no mouse, holding a key does not repeat, moving makes no sound, and every screen change is a cut. Play setup is 3 to 5 rows on an empty screen with no picture of the board.
- Play screen: one layout for every board. Tiny boards waste a 320 px panel and 24-wide boards get 14 px cells. Most panel text is 8 px. There is only one Next piece, and a power-up shows as a 4 px dot.
- Animation: clears flash white bars over rows that have already fallen. Only one banner shows at a time and it spills off narrow wells. The score has no count-up and back-to-back has no visual. Spawn, moves, hold, next, pause and game over are all instant, and the game-over card never changes.
- Sound: seven sounds. Hold borrows the rotate sound. Power-ups, combos, natural locks and the menus are silent.
- Top problems: Esc on Play setup closes the game. Space at the start hard-drops the first piece. Quitting can write another mode's score into the menu's Best. Pad B throws a run away at once. A pad player can get stuck on 'Press a key'. Reduced flashing does nothing.
- Your two classic Bests disagree. The menu shows 74,372 (from save.dat). In a game, Best shows 12,248, the top classic Marathon in the score table. No 74,372 row exists in any score table.
- Good bones: the rules code is clean and kept apart from the drawing code, it passes 1,118 checks, and a hidden-window screenshot tool already works for checking every step.

## Confirmed bugs (29), fixed in phase 1

Each was proven with a run and then re-checked by a second agent.

| Severity | Where | Problem |
|---|---|---|
| high | main.c:358 | Esc on the Play setup screen closes the whole game |
| medium | main.c:607 | Quitting the app writes any mode's score into the classic marathon save (save.dat Best) |
| medium | game.c:388 | Zen ('no game over') ends the run when a Hold swap brings in a piece that cannot spawn |
| medium | game.c:120 | Power-ups fire before the line clear, so a Laser or Bomb piece cancels the lines it just completed |
| medium | game.c:271 | MAX_LOCK_RESETS does not bound lock delay: tapping rotate keeps a grounded piece alive forever |
| medium | game.c:160 | Sprint is ranked by points, not time: a run twice as slow can top the Sprint table |
| medium | main.c:379 | Starting a run with Space hard-drops the first piece |
| medium | main.c:608 | Quitting writes any mode's score, level and lines into the classic Best |
| medium | main.c:590 | A tuned (F2) playtest run sets the menu's Best |
| medium | main.c:127 | Two different 'Best' values for the classic board; the game-over card says 'A new best' when it isn't |
| medium | render.c:343 | The 'Reduced flashing' setting does nothing in Blockfall |
| medium | main.c:566 | The run keeps going when the window loses focus |
| medium | main.c:451 | A pad player gets stuck on 'Press a key' in Settings |
| medium | gamepad.c:73 | Pad B abandons the run at once, and View restarts it, with no confirmation |
| medium | render.c:356 | Banners, the PAUSED card and the game-over card spill out of the well at many board sizes |
| medium | game.c:388 | Zen ends with GAME OVER when a held piece does not fit at the spawn point (cross-area: game.c) |
| low | game.c:62 | No lock-out rule: a piece can lock partly inside the hidden rows, where it is solid but never drawn |
| low | game.c:115 | After a Sweep, the lock-blink hint points at where the piece was before the board moved down |
| low | test_main.c:268 | Tests do not prove several things they claim; lock delay, drop scoring, speed curve and classic multi-line scoring are unpinned |
| low | winscale.c:39 | Alt+Enter toggles fullscreen and also activates the selected menu item |
| low | main.c:557 | Every R tap counts as a played run (achievements can be farmed, runs.log fills with zeros) |
| low | main.c:198 | Abandoned Sprint runs go onto the Sprint board, and the Sprint time is never recorded |
| low | render.c:476 | The HUD control card (and the pause and game-over hints) show default keys after rebinding |
| low | render.c:459 | A third status line is drawn under the controls divider on shorter wells |
| low | render.c:131 | Blocks in the four hidden rows are not drawn, including a power-up block; bomb effects draw over the header |
| low | main.c:587 | Finishing a Sprint (or Ultra's time running out) plays the game-over sting, with five other sounds at once |
| low | render.c:229 | The landed-piece blink is drawn in the wrong place after a Sweep (and on cells a Bomb just removed) |
| low | ghostlink.h:26 | Per-size score tables can exceed ghostlink's 512-row cap, which then silently drops the last tables in sort order |
| low | main.c:415 | The Scores screen only shows the board size currently chosen in Play setup |

### The fixes in plain words

- HIGH (1) Esc on the Play setup screen closes the whole game instead of going back. Fix: the main loop always draws one frame per pass, so the Esc press is used up and you land on the menu.
- HIGH (2) Starting a run with Space also hard-drops the first piece. Fix: same one-frame rule; the press that starts the run never reaches play.
- HIGH (3) Quitting after any mode or board writes that run's score, level and lines into the menu's classic Best (save.dat). Fix: save.dat only ever takes classic 10x20 Marathon runs.
- MEDIUM (4) A tuned (F2) test run can set the menu's Best. Fix: tuned runs never touch save.dat, the same as they never reach the score tables.
- MEDIUM (5) The classic board has two different Bests (menu vs in game), so 'A new best' can be wrong. Fix: one Best per mode and board. For the classic board it is the higher of save.dat and the score table (see decision 3).
- MEDIUM (6) Zen promises no game over, but Hold can end it with GAME OVER. The board can also empty with no FRESH START banner. Fix: Zen gives a fresh start there too, with the banner.
- MEDIUM (7) Power-ups fire before lines clear, so a Laser or Bomb piece wrecks the rows it just completed. You lose the points and the combo. Fix: clear lines first, then fire. A power block that was in a cleared row still fires where it was.
- MEDIUM (8) Tapping rotate keeps a landed piece alive forever, because the 15-move limit is bypassed. Fix: every reset of the landing timer counts toward the 15, and the count refreshes only when the piece reaches a new lowest row.
- MEDIUM (9) Sprint is ranked by points, not time. Runs you leave halfway still go on the Sprint board, and the finish time is never stored. Fix: only finished Sprints count, and a new 'Sprint 40' board is ranked by time (decision 2).
- MEDIUM (10) The 'Reduced flashing' setting does nothing in Blockfall. Fix: no white flashes (a soft tint instead), and blinking things hold steady.
- MEDIUM (11) The run keeps going when the window loses focus, and the pad stops responding meanwhile. Fix: the game pauses.
- MEDIUM (12) A pad player gets stuck on 'Press a key' in Settings. Fix: pad B cancels, and the wait gives up by itself after 5 s.
- MEDIUM (13) Pad B throws the run away at once, and pad View restarts at once, with no question asked. Fix (stopgap): Esc or B pauses first and a second press leaves. View restarts only from pause. Phase 5 replaces this with the full pause menu.
- MEDIUM (14) Banners, the PAUSED card and the game-over card spill out of the well on narrow, short and flat boards. Fix (stopgap): text sizes itself to the well (32 px, then 16 px, then two lines), and the end card is centred on the whole window.
- LOW (15) Alt+Enter goes fullscreen AND opens the selected menu item. Fix: Enter with Alt held is never 'select'.
- LOW (16) Every R tap counts as a played run, so the 'Regular' trophy can be farmed and the run log fills with zeros. Fix: a run counts only if a piece locked and it scored.
- LOW (17) The controls card, the pause hint, the game-over hints and How to play show the default keys after a rebind, and never pad buttons. Fix: they show your keys now, and pad buttons once phase 2 lands.
- LOW (18) Key-binding traps: 'Back to menu' can never be set back to Escape, two actions can share a key, and rebinding an action silently takes it off the pad. Fix: Esc always works as pause and back, and your old key keeps working too. A clash swaps the two actions. The pad keeps every action.
- LOW (19) On shorter wells, a third status line is drawn under the controls divider. Fix: corrected spacing.
- LOW (20) Blocks in the 4 hidden rows above the well are solid but invisible, a piece can lock up there, and a bomb there draws over the title. Fix: locking above the top ends the run, the same as reaching the top (Zen gets a fresh start). Effects are clipped to the well.
- LOW (21) After a Sweep or a Bomb, the landing blink flashes empty cells. Fix: the blink uses the piece's position after the power-up fired.
- LOW (22) Finishing a Sprint or reaching Ultra's time-up plays the game-over sting plus five other sounds at once. Fix: a 'cleared' sting, and only one big sound at a time.
- LOW (23) Per-board score tables can pass the shared 512-row limit, which silently deletes the scores of the last boards in the file. Fix: custom sizes keep their bests on this machine (decision 2), and the shared score code never rewrites a file it could not read in full.
- LOW (24) Scores only shows the board picked in Play setup, Zen is missing without a word, and the period (T key) has no pad button. Fix (stopgap): LB/RB or [ and ] change the board and pad Y changes the period. Phase 5 brings the full browser.
- LOW (25) Pressing M or - / = shows nothing on screen and is only saved when you quit. Fix: a small volume chip appears, and the change is saved at once.
- LOW (26) 'Best 0' shows on boards with no scores yet. Fix: 'No best yet'.
- LOW (27) Clear particles fly out over the margin and the panel, and hard-drop dust falls below the well. Fix: kept inside the well.
- LOW (28) Some tests prove nothing: the wall-rotation test passes with no kicks at all, one check is always true, and classic double, triple and quad scores are never checked. Lock delay, gravity, drop points, auto-repeat and the settings file have no tests at all. Fix: real tests, written before the other fixes.
- LOW (29) README.md is out of date: it says '10x20 playfield', lists files that no longer exist and has no Hold. Fix: refreshed.

## Core: nicer menus, better animations, the fixes

### Checks in the repo: screenshot tool and real tests (tech, M)

- The hidden-window screenshot tool moves into blockfall/tools/shots and runs with 'make shots'. It never shows a window on your screen and never touches your real saves.
- Today's 24 scripts become regression shots, and every phase adds its own.
- New headless tests for things that have none today: auto-repeat, the settings file, lock delay, drop points, the speed curve, the classic score table and the ghost piece. Phase 2 adds tests for the screens and input.
- The tests that prove nothing get fixed, and a sanitizer build runs the tests too.

**Why:** Most confirmed bugs sit in code with no tests (main.c, persist.c, repeat.c). Checking animation frame by frame, with no window opened while you use the machine, needs this tool.

**Needs:** Nothing. First step of phase 1.

**Decision / default:** Phase 1. No choice needed.

### One input reader and a screen stack (tech, M)

- main.c's single big loop splits into one file per screen, on a Back stack, so Back always returns to the screen you came from.
- The loop draws exactly one frame per pass. Breaking that rule is the root of two of the high bugs.
- Keys, pad and mouse are read once per frame as actions (Confirm, Back, Rotate...) rather than raw keys. The game remembers which device you used last, for the hints.
- Holding Up/Down repeats in every menu.
- Play input stays exactly as today: the same auto-repeat timing, and Left+Right held together still cancel.

**Why:** Every later screen is built on it. It ends a whole family of bugs where one press is read twice: Esc quitting from setup, Space hard-dropping at the start, Alt+Enter selecting an item.

**Needs:** Checks in the repo

**Decision / default:** Phase 2. The reader goes into ghost-common as new files that only Blockfall uses (decision 1).

### Button kit for the games (shared, opt-in) (menus and UI, L)

The launcher's controls come to Blockfall in violet:
- buttons and list rows, On/Off switches, < value > steppers, ten-block volume bars, tabs, and an 'are you sure?' box with the safe answer picked first
- five looks: normal, hover (mouse), focused (panel colour + 4 px violet bar), pressed (label sinks 2 px), and disabled (faint, with the hint line saying why)
- mouse: hover lights a control, a click acts when you let go, and the wheel scrolls lists and nudges sliders
- hint lines become key chips that show keyboard keys or Xbox buttons, whichever you touched last.
The house style stays: Press Start 2P at 8/16/32/48, and selection shown by colour and bar, never by a size change.

**Why:** This is the 'nicer menus' part of your request. It adds mouse support (none today) and makes Blockfall feel like the reworked launcher. The next game to be reworked gets it for free.

**Needs:** One input reader and a screen stack

**Decision / default:** Decision 1. Recommended: a shared copy that only Blockfall uses for now.

### Menu motion, run wipes and menu sounds (animations and feel, M)

- The focus bar slides to the next item in 0.08 s instead of jumping.
- Screens slide in 16 px: from the right going forward, from the left going back (0.14 s). Text moves; it never fades.
- A pressed button flashes violet for 2 frames.
- Going into and out of a run uses a block wipe: rows clear from top to bottom in 0.25 s, like a line clear.
- The achievement toast slides back out instead of vanishing.
- Updates scrolls with the mouse wheel, and LB/RB jump a page.
- Sounds: a soft, slightly varied tick on each move, plus confirm, back, and a warning when an 'are you sure?' box opens. All of these already exist in the shared sound code.
- Input never waits for motion; a press acts at once.

**Why:** Today every screen change is a cut, the selection bar jumps, holding a key does nothing and the menus are silent.

**Needs:** Button kit; Comfort switches

**Decision / default:** Phase 2. The run wipes land in phase 5.

### Comfort switches: Motion and Reduced flashing (animations and feel, S)

- Reduced flashing finally works in Blockfall: no white flashes (a soft tint of the piece colour instead), and blinking things hold steady.
- A new Blockfall setting, 'Motion: Full / Reduced'. Reduced turns slides, wipes, glides, row falls and card drops into plain cuts, and halves the particles. Screen shake stays its own switch.
- Every effect checks the same two helpers, so no new effect can forget them.

**Why:** Reduced flashing doing nothing is a confirmed bug. This plan adds a lot of motion, so the off switches have to be there from the start.

**Needs:** Checks in the repo

**Decision / default:** Reduced flashing in phase 1. The Motion switch in phase 2, for Blockfall only for now.

### Main menu: a living title screen (menus and UI, M)

- On launch, the letters of BLOCKFALL drop in one by one with a small thud. This is skipped when you come back from a run, and any key skips it.
- The faint falling pieces now land on a low skyline along the bottom. Full rows flash and clear the way they do in play. It stays faint and out of the hint line.
- The menu items become buttons with mouse hover and click. Start game carries a dim second line with your last setup.
- The single 'Best / level / lines' line, which only knows the classic Marathon, becomes 'Your bests' for the board you last played: Marathon, Sprint, Ultra and Power.
- B or Esc first moves the bar to Quit, and a second press quits. Today one press quits.
- Kept: the icon, version, tagline, 'Playing as', 'Made by' and all six items.

**Why:** It is the first thing you see every time. Today it is a static list, and its stat line only knows the classic Marathon.

**Needs:** Button kit; Menu motion

**Decision / default:** Phase 2. Default: B goes to Quit first. Tell me if you want something different.

### Play setup: mode list, board preview, quick Start (menus and UI, M)

- Left side: the five modes, each with a short note (Marathon endless, Sprint 40 lines, Ultra two minutes, Zen no game over, Power power-ups). Up/Down picks one.
- Right side:
  - two lines on what the mode is and how it ends
  - a board picker '< Classic 10x20 >' that shows the size of every preset
  - a preview of the board at its true shape, which reshapes in 0.15 s
  - 'In play: 30 px cells, panel at the side'
  - your best and the world best on this board.
- Width and Height always work. Holding repeats and speeds up after 1 s, so 12 to 36 no longer takes 24 presses. A size that matches a preset lights that preset; anything else is Custom.
- A 'Scores for this board' button.
- The screen opens on last time's mode, so Enter, Enter replays your last setup. Enter on any mode starts the run. LB/RB or [ and ] change the board from anywhere. B, Esc or the Back chip go back.
- Kept: every mode, every preset, the custom size limits, and the setup remembered in config.ini.

**Why:** Today it is 3 to 5 text rows on an empty screen. A custom board says only 'Custom', there is no picture of the board and no bests, Enter is ignored on Width and Height, and Esc closes the game.

**Needs:** Button kit

**Decision / default:** Phase 2. I chose a list over cards across the top because a list has room for new modes later.

### Play screen that fits every board (menus and UI, L)

- A layout chooser puts the panel at the side for tall boards, or in a strip across the top for flat ones, whichever gives bigger cells. It is tested on all 475 sizes from 6x12 to 24x36: nothing overlaps and nothing leaves the window. 175 sizes get bigger cells; 24x12 goes from 14 px to 22 px.
- Tiny boards get a roomy panel with a big score instead of empty space.
- A used Hold turns grey with a small lock. In Power mode, Next and Hold name the power-up ('Next: Bomb') and show its letter instead of a 4 px dot.
- One main number per mode:
  - Sprint: lines left and the clock
  - Ultra: a big countdown that turns gold at 30 s, then red with a tick in the last 10 s
  - Zen: fresh starts.
- Combo and back-to-back chips light up while active. Freeze and Slow become draining bars.
- The controls card shows YOUR keys, or pad buttons after you use the pad, and it can be hidden.
- The footer line goes. M and - / = move onto the controls card, and pressing them shows a small volume chip for 1.5 s.
- 'No best yet' replaces 'Best 0'.
- Kept: the F3 FPS counter, the TUNED marker, the level pips and every stat.

**Why:** It fixes the confirmed overflow, default-keys and status-line bugs and ends the wasted space. It also makes back-to-back visible; it pays +50% today but nothing on screen shows it.

**Needs:** One input reader; Comfort switches

**Decision / default:** Phase 3.

### Callouts, score popups and a livelier panel (animations and feel, S)

- One callout system replaces the single banner: up to 3 stacked lines (TRIPLE / COMBO 3 / BACK-TO-BACK). Each rises 8 px, holds, and is gone at 0.9 s.
- Each callout sizes itself to the well: 32 px if it fits, then 16 px, then two lines. It never leaves the well.
- DOUBLE, TRIPLE and QUAD get a word; a single gets only its popup.
- A '+points' popup rises from the cleared rows: gold for 800 and up, violet below that.
- The score counts up in about 0.35 s, and a stat that changes nudges up 2 px.
- Combo shows as 'Combo x3' with dots. After a quad, a gold 'B2B ready' appears.
- The first time you pass your best mid-run: a gold NEW BEST and a soft coin sound.
- Level up: LEVEL n, the pips light one by one, the well frame flashes violet and the grid brightens for a moment.

**Why:** Today only one banner shows (the last one wins), and it spills off narrow and short wells. The score jumps, and back-to-back and small clears get no feedback.

**Needs:** Play screen that fits every board

**Decision / default:** Phase 3. Default: a single gets a popup but no word.

### Line clears that flash, burst, then fall (animations and feel, M)

- The full rows flash for 0.06 s. The cells then zip out from where the piece landed towards both walls and burst into pixels that stay inside the well. Then everything above drops into the gap with a 2 px settle.
- Each clear size feels different:
  - single: popup only
  - double
  - triple: a small shake
  - quad: all four rows burst at once with gold mixed in, the frame flashes gold, and an 8 px shake.
- The clear sound rises in pitch with the size. Back-to-back adds its own line and a brighter sound.
- At levels 10-14 the whole beat takes 0.18 s and at 15+ only 0.12 s, so the picture never lags the real stack for long. The ghost hides until the rows settle.
- How it works: the game hands the drawing code a copy of the board as it was when the piece locked. The animation plays from that copy and then switches back. The rules do not change.

**Why:** This is the moment you see most, and today it looks wrong: rows collapse in the same frame, so the white bars sit on blocks that have already fallen. Singles, doubles and triples look the same, and back-to-back has no visual.

**Needs:** Power-up order fix (phase 1); Play screen; Comfort switches

**Decision / default:** Decision 6. Recommended: looks only, with no change to game speed.

### Piece motion and landing feel (animations and feel, M)

- Spawn: the new piece slides in half a cell from the top and fills in (0.08 s).
- A single tap glides over 0.04 s; auto-repeat snaps. The piece is never drawn more than half a cell from where it really is.
- Rotate: a tiny 2 px pop. A blocked rotation plays a quiet, dull tick, so you know the press registered.
- Soft drop leaves short streaks. Hard drop leaves a smear from start to landing, a 2-frame squash and dust thrown sideways (kept inside the well), plus today's thud.
- A natural lock (no hard drop) gets a 1 px settle, a brief brighter bevel and a soft tick. Today it is silent.
- A landed piece darkens as its 0.5 s lock time runs out, and brightens again when you move it.
- Ghost: an outline plus a light fill. Rows it would complete get small marks at both edges.

**Why:** Today the piece appears instantly, snaps cell by cell, locks silently, and its dust falls out of the well. Small, honest motion makes the game feel responsive, and the lock shading and ghost marks help at high speed.

**Needs:** Line clears (they share the effects code)

**Decision / default:** Phase 4. Default: smooth taps that snap during auto-repeat, with a Look switch 'Piece motion: Smooth / Snap'.

### Hold, Next and READY-GO in motion (animations and feel, M)

- Hold: the outgoing piece flies into the Hold box in 0.12 s with a new swoosh (today it borrows the rotate sound). When hold is ready again, the box edge flashes violet once.
- Next: the old preview slides out as the new one slides in (0.10 s).
- Run start: READY (violet), then GO (gold), with the existing READY and GO jingles. The pieces are visible, but the clock does not run yet. A rotate or hold pressed during READY happens at GO.
- After R, only GO (0.4 s).

**Why:** Hold and Next change instantly today, and a run starts with only a jingle. The fly-in shows what just happened, and READY-GO gives every run, Sprint above all, a fair start.

**Needs:** Play screen; Piece motion

**Decision / default:** Phase 4. Default: READY-GO at the start of a run and only GO after a restart, with a switch to turn it off.

### Pause menu with quick settings and 'are you sure?' (menus and UI, M)

- Esc, P, pad Start or pad B open it. The well dims over 0.12 s (the piece stays hidden), and a card slides up with Resume (picked first), Restart, Settings, How to play and Quit to menu.
- Settings opens a quick panel inside the card: effects volume, music volume (you hear it at once), Sound On/Off, ghost piece and screen shake. Changes save at once.
- Restart and Quit ask first when the run has points: 'Leave this run? It counts for 12 450.' [Keep playing] [Leave]. Keep playing is picked first, and B or Esc also keeps playing.
- An optional 3-2-1 countdown after Resume (a Game setting).
- If the window loses focus, the game pauses here.
- Kept: P still pauses and resumes. Your old 'Back to menu' key opens this menu.

**Why:** It fixes three confirmed bugs: pad B throws the run away, pad View restarts at once, and the run keeps going when the window loses focus. Today pause is a dead end that only says 'P carries on'.

**Needs:** Button kit

**Decision / default:** Decision 4. Phase 5.

### Results card after every run (menus and UI, M)

- A short end show on the board:
  - top-out: the blocked piece blinks red, then the rows turn grey from the bottom up
  - Sprint clear: the rows burst upward in colour
  - Time up: the rows dim.
- Then a card drops in, centred on the whole window so it fits every board: GAME OVER (red), SPRINT CLEAR (violet) or TIME UP (gold).
- The score counts up with rising ticks; Sprint shows the time as the big number. A new best stamps a gold NEW BEST with confetti and the new-best jingle. Otherwise the card says, for example, 'Best 26 714, 11 694 to go'.
- Rank: '#3 on this machine', and the world rank from the last sync.
- The run in numbers: lines, level, time, pieces, pieces per second, quads, best combo, back-to-backs and power-ups (fresh starts in Zen).
- Buttons: Play again (also R or A), Change setup, Scores, Menu (also B or Esc).
- Input is ignored for the first 0.4 s, so a held hard drop does not skip the card. Any press during the count-up jumps to the end.
- A trophy you just earned shows here, not only back on the menu.

**Why:** Today the end is a static overlay inside the well that spills off many boards. Only R and Esc work on it, 'A new best' can be wrong, and a Sprint clear plays the game-over sting.

**Needs:** Button kit; Callouts; simple run counters in the rules code (they change no rules)

**Decision / default:** Phase 5. Default: Play again is picked first.

### Scores you can browse by mode and board (menus and UI, M)

- Tabs: Marathon, Sprint, Ultra, Power (click, Left/Right or LB/RB). Zen shows as a faint tab that says 'Zen keeps no scores'.
- A board picker '< Classic 10x20 >' under the tabs (pad X or a click).
- This machine | World tabs, and All time | This week | Today chips. Pad Y changes the period; today only the T key can.
- Kept: the medals, your highlighted rows, the 'You are #4, 15 570 behind #3' line and the 2-second refresh.
- A 'Play this board' button starts a run in that mode on that board.
- Opened from the results card, it lands on that run's board with your new row blinking.

**Why:** Today Scores shows only the board picked in Play setup, Zen is missing without a word, and the period has no pad button.

**Needs:** Button kit; decision 2; the score-file safety fix (phase 1)

**Decision / default:** Phase 5. Phase 1 adds a quick board switch to the old screen.

### Settings with tabs (menus and UI, L)

- Tabs across the top: Controls, Feel, Look, Sound, Game. LB/RB, Tab or a click switch tabs.
- A note says what each tab is for, and a bottom line explains the focused row. Changes save at once. Rows marked * are shared with every game.
- Controls: one row per action, with [Key] [Second key] [Pad] caps and friendly names (Left, Space, A, LB).
  - Press a cap and it pulses 'Press a key' or 'Press a button', with a 5 s bar.
  - A key already in use swaps places with it, and the note says so.
  - [Reset controls] asks first.
  - Esc always works as pause and back, so it can never be lost.
- Look: ghost, grid, controls card in play, piece motion, menu motion, screen shake*, reduced flashing*, fullscreen*.
- Sound: effects volume (each block ticks at its own pitch), music volume*, Sound On/Off, [Play a test sound].
- Game: pause when the window loses focus, R restarts (at once / asks first), countdown after pause, [Show How to play on next launch].
- Kept: every key you have set today, both second keys and all four shared rows. Old config.ini files load unchanged.

**Why:** Today Settings is one 20-row list that mixes everything and shows raw key names (ESCAPE). Esc cannot be set back, two actions can share a key, and the pad cannot rebind anything; it silently loses any action you rebind.

**Needs:** Button kit; One input reader

**Decision / default:** Phase 6. The pad layout is decision 5.

### More controls: rotate left, second keys, handling (gameplay and modes, M)

- Rotate left (counter-clockwise): Z by default, pad X. The rules already support it; only the key was missing.
- A second key for hard drop and for hold.
- Feel tab:
  - auto-repeat delay, 50-300 ms
  - repeat speed, 0-100 ms, where 0 means straight to the wall
  - soft drop speed: Slow, Normal, Fast or Instant
  - presets: Relaxed, Standard (today's 170 ms / 35 ms) and Fast.
- A strip shows a piece sliding with your values; hold Left/Right on it to try them.
- The defaults stay exactly as today.

**Why:** Today only clockwise rotation can be bound, there is one hard-drop key and one hold key, and auto-repeat is fixed in the code. Rotate left is the first thing anyone who plays modern block games misses.

**Needs:** Settings with tabs

**Decision / default:** Phase 6. The defaults match today's feel.

## Recommended

### How to play with moving examples (menus and UI, M)

- Five pages, with tabs and < > arrows: Basics, Hold and next, Scoring, Modes, Power-ups.
- Each page has a small live well running a looping demo on the real rules:
  - a piece slides, turns, drops and clears a row
  - Hold swaps a piece
  - a quad, then a back-to-back quad, with the points shown.
- The captions use YOUR keys, or Xbox buttons after you use the pad.
- Scoring lists 100 / 300 / 500 / 800 times the level, the combo and back-to-back bonuses, and which board keeps the classic scoring.
- Power-ups shows five tiles; the focused one plays on the mini well.
- First launch still opens it.

**Why:** Today's card is fixed text. It always shows the default keys and says nothing about modes, scoring, hold or power-ups.

**Needs:** Settings with tabs (your keys); the play screen's block drawing

**Decision / default:** Phase 6. Default: first launch shows only Basics, with 'More in How to play'.

### Ghost, grid and block-pattern options (menus and UI, S)

Three options on the Look tab:
- ghost piece: Outline, Solid or Off
- grid: On, Faint or Off
- optional patterns on the blocks, so colour-blind players can tell the pieces apart.

**Why:** The ghost is a thin, half-strength outline, the grid cannot be changed, and colour is the only way to tell the pieces apart.

**Needs:** Settings with tabs

**Decision / default:** Phase 6.

### Sound pass: about 16 sounds, one big sound at a time (sound, M)

- New sounds made in code, as today (nothing sampled): lock tick, hold swoosh, bomb, laser, freeze, slow, sweep and a back-to-back sparkle.
- Variety from the shared sound calls that already exist:
  - moves vary slightly
  - single, double and triple clears rise in pitch
  - each combo step rises a semitone
  - the results count-up ticks rise.
- Shared jingles: READY/GO; WARNING (danger, and Ultra's last 10 s); COIN (power-ups, and passing your best mid-run); NEW_BEST.
- One rule: each lock plays at most one big sound plus one accent. The level-up sound waits 0.15 s so it does not clash with the clear.
- Kept: today's seven sounds, M to mute and - / = for volume.

**Why:** Today there are seven sounds. Hold borrows the rotate sound; natural locks, power-ups, combos, back-to-back and pause are silent; and a finished Sprint plays six sounds at once.

**Needs:** Callouts; Line clears

**Decision / default:** Phase 7. Expect a second round once you have listened. Default: natural locks get a soft tick.

### Power-ups with their own look and sound (animations and feel, M)

- Bomb: the cell blinks, the 3x3 around it bursts in its own colours, then a square shockwave, a 9 px shake and its own sound.
- Laser: a charge line runs down the column, then the beam. The column's cells pop out from the power cell.
- Freeze: an ice wash, frost growing in from the corners and a draining bar. It blinks in the last 1.5 s, then the frost breaks with a thaw tick.
- Slow: the orange frame plus two faint afterimages behind the piece, a draining bar, and a wind-up sound at the end.
- Sweep: a green bar crosses the bottom 3 rows and bursts them, then the stack drops 3 rows. Today they just vanish.
- Each pickup shows the power's name in its colour with a soft coin sound. Power marks glow in the power's colour and hold steady with Reduced flashing.

**Why:** Power is the showpiece mode, yet four of the five power-ups make no sound, and Sweep and the end of Freeze or Slow have no visual.

**Needs:** Line clears; Sound pass; the power-up order fix (phase 1)

**Decision / default:** Phase 7.

### Danger warning and music that follows the board (animations and feel, S)

- Near the top, the well frame eases to red and pulses gently (steady with Reduced flashing), and faint red stripes mark the top rows. It only eases off once you have dug well clear, so it never flickers.
- If the next piece would not fit at the top, its outline shows there in red.
- A warning sound plays once when danger starts, at most every 8 s.
- Music intensity follows the stack height as well as the level: the snare comes in as the stack climbs and drops out when you dig out. Sprint follows lines, Ultra follows time, and Zen stays calm.
- Ultra's last 10 s: the timer turns red, a warning sounds, and each of the last 5 seconds ticks. Sprint's last 5 lines: the counter turns gold.

**Why:** Today nothing warns you near the top, Ultra's last seconds look like any others, and the music reaches full at about level 5 and then never changes.

**Needs:** Play screen that fits every board

**Decision / default:** Phase 7. Never in Zen.

### Sprint splits and pace (gameplay and modes, S)

- A big timer with hundredths.
- Split marks at 10, 20 and 30 lines show how far ahead of or behind your best pace you are ('-1.4 s').
- The finish says 'new best by 0.8 s'.
- The clock starts on GO.

**Why:** Sprint is a race, so you should see how the race is going.

**Needs:** Sprint ranked by time (decision 2); READY-GO

**Decision / default:** Phase 7, and only if Sprint is ranked by time.

### Modern rules: SRS turning, 5-piece queue, T-spins, perfect clears (gameplay and modes, L)

- Pieces turn with the standard SRS wall kicks. Spins against walls and into gaps behave like any modern block game, and pieces can no longer climb the stack.
- The Next box becomes a queue: one large piece and up to four smaller ones (a setting, 1 to 6). A given seed still gives the same pieces.
- T-spins (minis included) and perfect clears are detected and score on modern boards. Back-to-back also chains T-spin clears.
- Callouts such as T-SPIN DOUBLE and PERFECT CLEAR.
- An optional 180-degree turn key, unbound by default.

**Why:** These are the first things a modern block-game player misses. Today's wall-kick list is a generic one that lets pieces climb.

**Needs:** Checks in the repo; Callouts

**Decision / default:** Decision 7. Phase 8.

## Optional extras, pick any

### Daily challenge (gameplay and modes, M)

- Everyone gets the same pieces (and power-ups) today in a 2-minute score attack.
- The twist changes with the date: Ultra on Wide, Power Ultra, Tall, Tiny, a fast start, and so on.
- Retry as often as you like; your best counts, and a streak shows.
- Ghost Launcher's D key opens it, and Blockfall joins the launcher's daily desk with the other seven games.

**Why:** Seven games already have a daily through the shared daily code, and Blockfall is missing. 'Same pieces for everyone' is the classic block-game daily.

**Needs:** Results card; Scores browser; a launcher change (its daily rotation is built for exactly 7 games)

**Decision / default:** Decision 8.

### Lifetime Stats screen (menus and UI, S)

A Stats item on the menu shows, for each mode:
- runs, lines and time played
- records: best pieces per second, biggest combo, most quads in a run
- a lines-by-type bar.
It is kept in Blockfall's own file; runs.log stays as it is for the launcher.

**Why:** It gives every mode something to chase besides the score, and the counters already exist after phase 5.

**Needs:** Results card (run counters)

**Decision / default:** Decision 8.

### Dig mode (rising garbage) (gameplay and modes, M)

- The well starts with 8 grey garbage rows, each with one gap.
- A new row pushes up every few seconds, from about every 8 s down to every 3 s, and a warning bar fills before each rise.
- Clearing garbage scores and counts as 'Dug'.
- Topping out ends the run.

**Why:** Every mode starts from an empty well today, and gravity is the only pressure. Digging out of garbage is a well-loved modern mode.

**Needs:** Play setup list; Line clears

**Decision / default:** Decision 8.

### Power mode rework: chain reactions (gameplay and modes, M)

- Cascade replaces Freeze: floating blocks drop, and any rows they fill clear as a chain ('CHAIN x2').
- Floating blocks also settle after Bomb and Laser.
- Sweep's three rows count toward your level.
- Rare power-ups such as Sweep turn up less often.

**Why:** Freeze and Slow do nearly the same thing, Bomb and Laser leave floating blocks behind, and Sweep gives no line credit.

**Needs:** Power-ups with their own look; the power-up order fix

**Decision / default:** Decision 8. Needs fresh Power boards, because the scores would no longer compare.

### Practice room (gameplay and modes, M)

- A Practice item with options: start level 1-20, gravity on or off, no lock, infinite hold, starting garbage, and finesse hints ('2 extra presses').
- Undo one piece at a time, up to 50 times.
- A PRACTICE tag shows, and nothing is recorded.

**Why:** Today there is no way to learn or try things out.

**Needs:** One input reader (a free pad button for undo)

**Decision / default:** Decision 8.

### New Blockfall trophies (gameplay and modes, M)

- About 14 trophies that reward skill, for example: a first quad, two quads back to back, combo x5, Sprint under 2:00, 30 000 in Ultra, 150 lines in one Marathon, all five power-ups in one run.
- The six trophies you already have stay earned.

**Why:** Today's six are generic score, level and run-count trophies, and at your level you already have them all.

**Needs:** Results card; Modern rules for the T-spin trophies; a shared trophy change and a launcher update (its trophy page fits six)

**Decision / default:** Decision 8.

### Attract mode demo (animations and feel, M)

- After 30 s idle on the menu, a simple bot plays a real game full screen, showing DEMO and 'Press any button', like an arcade cabinet.
- Any input goes back to the menu.
- It never records scores, trophies or runs.

**Why:** Pure arcade-hall charm.

**Needs:** Main menu; Line clears

**Decision / default:** Decision 8.

### Music that speeds up in danger (sound, S)

- The music plays about 10% faster in danger and in Ultra's last 10 s, like the classic handheld.
- It comes from a new opt-in function in the shared music code that defaults to normal speed.

**Why:** A classic way to build tension.

**Needs:** Danger warning; a change to the shared music code (all 11 games rebuilt and listened to)

**Decision / default:** Decision 8.

### Marathon milestone at 150 lines (gameplay and modes, S)

A 'Marathon clear' celebration at 150 lines. It is looks only: the run keeps going and the scoring does not change.

**Why:** Today a long Marathon never marks your progress after level 15.

**Needs:** Callouts

**Decision / default:** Decision 8.

## Rejected, and why

### A real pause after every line clear (animations and feel, S)

Play freezes for about 0.2 s after each clear, as in modern Tetris.

It slows every run and changes Sprint and Ultra times, so those boards would have to start over. The looks-only line clear gives the same picture with no rule change.

### Master mode (20G) (gameplay and modes, L)

An expert mode where pieces land instantly from level 10.

It needs an entry delay, a new gravity system and the modern lock rules first, and it is made for experts rather than relaxed play. The Marathon milestone is the gentler goal.

### Replays of your best runs (tech, L)

Save your best runs and watch them back.

It is the biggest item proposed. The whole game loop would need to run on fixed steps, and every rules change in this plan would break older replays.

### Changing the shared menu screens for every game (menus and UI, M)

Adding the slides and new looks inside the shared ui.c that all eleven games use.

Every game would change at once and need retesting. New opt-in files keep the other ten games exactly as they are until each one is reworked.

### A block wipe on every menu change (animations and feel, S)

A diagonal block wipe between every pair of menu screens.

It gets busy when you move through several screens in a row. A light slide is quicker to read, and the bigger wipe is kept for the moments that matter: starting and leaving a run.

### An arcade-wide Motion setting now (tech, S)

Make 'Reduced motion' a shared setting for all games today.

Every game rewrites the shared settings file with only the settings it knows, so a new shared one would be erased until every game is rebuilt. Blockfall's own switch works today and can become shared later.

## Phases

Each phase ships a working Blockfall.

### Phase 1: Fix what is broken (Blockfall 2.1.0)

- First, move the screenshot tool into the repo ('make shots') and add the missing tests, so each fix is proven.
- Fix all 29 confirmed bugs (see the fixes list). A few fixes are stopgaps that later phases replace with the full design: Esc and B pause first and a second press leaves, Scores gets a quick board switch, and banners and the end card size themselves to the well.
- Refresh README.md and add an Updates entry.
- Kept exactly as now: every mode, board, key, setting and sound, the F2 tuning panel, F3, F11 and Alt+Enter, M and - / =, achievements, scores, world boards, ghost-sync and your player name.
Checked by:
- 'make test': new tests that fail on today's code and pass after the fix. They cover Esc on setup, Space at the start, the save mix-up, Zen hold, the power-up order, rotate spam, locking above the top, Sprint ranking and what counts as a run.
- 'make shots': today's 24 scripts again. The bug shots (Esc on setup, Space start, the Power run leaking into Best, Zen hold, Alt+Enter, rebinding, reduced flashing, tiny and flat boards) must now show the fixed result. Every other shot must match today's.
- You: play each mode for a few minutes on keyboard and pad. Try Esc on setup, Space to start, a rebind and Reduced flashing.

**You get:** Blockfall looks almost the same but stops misbehaving. Your saves are read as they are, and from now on only real classic Marathon runs can change the menu's Best.

**Other games:** Mostly no. There are two small shared touches. (1) If you choose time-ranked Sprint, Ghost Launcher learns to show Sprint 40 scores as times, in the same release. (2) The shared score-file code gets a safety fix so it never deletes rows it could not read. No game is near that limit, and the other games pick the fix up the next time they are rebuilt, with nothing visible changing. The uncommitted ghost-common work from earlier stays as it is.

### Phase 2: New menus (2.2.0)

- The input reader and screen stack, the shared button kit as new opt-in files, menu motion and sounds, and the Motion switch.
- The new main menu and the new Play setup. Updates moves onto the kit.
- Scores, Settings and How to play keep their old look for now, but they already run on the new stack: Back, mouse scrolling and the pad all work.
Checked by:
- Tests: the launcher's kit tests ported over, plus new ones for actions (keys, pad, mouse, Alt+Enter), the Back stack and hold-to-repeat.
- Shots of each new screen: every focus, hover and press, frames in the middle of a slide, and Reduced motion. Scripts prove that Esc or B goes back from every screen and that Enter, Space or A never leak into play.
- All the old play shots must still match, because play is untouched.
- You: walk through the menus with keyboard, pad and mouse.

**You get:** Blockfall with a living title screen, a new Play setup with a board preview, and menus you can click that slide and make sounds. The play screen is still the old one.

**Other games:** No change for the other ten games or the launcher. ghost-common gains new files (input reader and kit) that only Blockfall builds. The launcher keeps its own copy until it switches over, later, as a separate step.

### Phase 3: The play screen (2.3.0)

- render.c splits into four parts: layout (testable without a window), the panel, the well and the effects.
- The play screen that fits every board, plus callouts, popups and the livelier panel.
Checked by:
- A layout test over all 475 board sizes: nothing overlaps, nothing leaves the window, and every callout fits.
- Shots at 6x12, 6x14, 8x26, 10x20, 14x20, 18x30, 24x12 and 24x36: early in a run, in the middle of a clear, and with callouts showing.
- You: a classic Marathon and one unusual size.

**You get:** A play screen that uses the space on every board, shows your keys, and celebrates clears without spilling out of the well.

**Other games:** Blockfall only.

### Phase 4: How play feels (2.4.0)

- Line clears that flash, burst and fall; piece motion and landing; Hold, Next and READY-GO in motion.
Checked by:
- Tests: the board copy behind the clear animation matches the real clear row for row, effects get shorter at high levels, and READY-GO never runs the Sprint or Ultra clock.
- Frame-by-frame shots: clear frames 1, 4, 8 and 14 for each clear size, hard drop, the hold flight, the next slide, READY and GO. Each in Full and Reduced motion, and with Reduced flashing.
- You: play at level 1 and at level 15 to judge the feel.

**You get:** The same rules and the same timing, but the game feels much more alive.

**Other games:** Blockfall only.

### Phase 5: Around a run (2.5.0)

- The pause menu, the results card, the scores browser, and the block wipe going into and out of a run.
- Replaces the phase 1 stopgaps for pause and Scores.
Checked by:
- Tests: what counts as a run; the run counters (pieces, quads, combos) checked against scripted games.
- Shots: the pause menu and its 'are you sure?' box; results for game over, Sprint clear, Time up and a new best; the scores browser on several boards. A script proves that Enter or A on Resume does not hard-drop.
- You: leave and restart runs on the pad.

**You get:** No more dead ends: every run starts with a wipe and ends on a results card you can act on.

**Other games:** Blockfall only.

### Phase 6: Settings and How to play (3.0.0, core done)

- Settings with tabs, more controls (rotate left, second keys, handling), the ghost, grid and pattern options, and How to play with moving examples.
Checked by:
- Tests: old config.ini files load unchanged, every stored key has a friendly name, a clash swaps the two actions, pad capture works, and each How to play demo clears the rows it says it does.
- Shots of every tab and every capture state.
- You: rebind keys on keyboard and pad, and try the handling presets.

**You get:** The full core rework: every screen is new and every bug is fixed.

**Other games:** Blockfall only, plus Blockfall's own entry in the shared How to play text, which the launcher's About tab also shows. Other games' entries are not touched.

### Phase 7 (recommended): Sound and spectacle (3.1.0)

- The sound pass, power-up effects, the danger warning with music that follows the board, and Sprint splits.
Checked by:
- A test that each lock plays at most one big sound.
- Shots of every power-up and of the danger state, in full and reduced settings.
- You: listen and play some Power. Sound is taste, so expect a second round.

**You get:** Every moment has its own sound, power-ups are a show, and the music rises and falls with the board.

**Other games:** Blockfall only. The new sound files are Blockfall's, and so is the music intensity formula.

### Phase 8 (recommended): Modern rules (3.2.0)

- Only if decision 7 says yes: SRS turning, the Next queue, T-spins and perfect clears, on the boards you choose.
Checked by:
- Wall-kick tests copied from the published SRS table, lock-reset and T-spin cases, and a test that a seed still gives the same pieces as before.
- Shots of the queue and the new callouts.
- You: play on a modern board and on the classic board.

**You get:** Blockfall plays like a modern block game on the boards you picked, and the classic board stays as you know it.

**Other games:** Blockfall only.

### Phase 9 (optional): Extras you pick

- Whichever extras you choose in decision 8: Daily challenge, Stats screen, Dig mode, Power rework, Practice room, new trophies, attract demo, Marathon milestone, faster danger music.
- Each one is its own small release, checked the same way: tests, shots, then you play it.

**You get:** One working Blockfall per extra.

**Other games:** It depends on the pick. The Daily needs a launcher change (its rotation is built for 7 games). New trophies need a shared trophy change and a launcher update. Faster music touches the shared music code, so all 11 games are rebuilt and listened to. Everything else is Blockfall only.

## Decisions for the owner

- 1. Where should the new button kit live? (a) Move it into the shared ghost-common code now and switch the launcher over in the same step: one copy, but it reopens the launcher you just finished. (b) Copy it into Blockfall only: fastest, but every later game copies it again. (c) Put a shared copy in ghost-common that only Blockfall uses for now, and switch the launcher later as a small step of its own. Recommended: (c). The other ten games and the launcher stay exactly as they are.
- 2. How should Sprint and unusual board sizes be scored? Sprint: (a) start a fresh 'Sprint 40' board ranked by finish time, fastest first, keep the old points board as 'Sprint (old)', and teach the launcher to show times; or (b) keep ranking by points. Custom sizes: (a) keep their bests on this machine only, off the shared and world boards; or (b) keep putting every size on the world boards. Recommended: rank Sprint by time, and keep custom sizes local so only the five preset boards go online. That also keeps the score file well under its row limit.
- 3. Your classic best of 74,372 lives in save.dat, but the top classic Marathon in the score table is 12,248, and no 74,372 row exists in any table. Most likely it comes from the versions before the score table (before 1.2.0). It could also have leaked in through a bug from another mode or a tuned run, and there is no way to tell which. (a) Keep it as the classic best to beat. (b) Keep it, but show it as 'Old best' next to the table's best. (c) Clear it so only the score table counts. Recommended: (a). Nothing in your files changes until you choose.
- 4. Leaving and restarting mid-run. Esc and pad B will open a pause menu instead of throwing the run away. Then: (a) 'Quit to menu' and 'Restart' ask 'are you sure?' when the run has points, and R and pad View ask too, with a Settings switch for instant R; (b) R stays instant as today, and only Quit asks; (c) nothing asks. Recommended: (a).
- 5. Pad layout. (a) Keep today's buttons and fill the empty ones: A hard drop, d-pad Up rotate, X rotate left (new), Y and LB hold, Start pause, B pause menu (no longer leaves at once), View restart (asks first). (b) Switch to the common modern layout: Up hard drop, A rotate right, B rotate left, LB/RB hold, Menu pause. Either way, every button can be changed in Settings. Recommended: (a), so your habits keep working.
- 6. Line clears. (a) Looks only: the rows flash, burst and fall while the next piece is already coming, so game speed and Sprint and Ultra times stay exactly the same. (b) A real 0.2 s pause after each clear, as in modern Tetris: it slows every run and needs fresh Sprint and Ultra boards. Recommended: (a).
- 7. Modern rules (phase 8): standard SRS turning, a 5-piece Next queue, T-spins and perfect clears. (a) Add them on every board except the classic 10x20 Marathon. That board keeps today's turning, one Next piece and today's scoring, so your classic scores keep their meaning; rotate left still works there because it is a control, not a rule. (b) Add them everywhere, classic included. (c) Skip modern rules. Recommended: (a). Either way, the phase 1 bug fixes (the lock-delay limit, the power-up order) apply to every board.
- 8. Which extras, if any, after the core? Pick any: Daily challenge (same pieces for everyone each day; needs a small launcher change), lifetime Stats screen, Dig mode (rising garbage), Practice room (undo, no gravity, any start level), Power rework (chain reactions; fresh Power boards), about 14 new Blockfall trophies (needs a launcher update), an attract-mode demo after 30 s idle, a Marathon milestone at 150 lines, and music that speeds up in danger (touches the shared music code). Recommended: start with the Daily challenge and the Stats screen, and decide the rest after playing the finished core.

## Risks

- Size: the core is six phases. The biggest steps are phase 2 (input reader and button kit) and phase 3 (splitting the play-screen drawing code). Each phase ships on its own, so we can stop after any of them.
- Play feel must not change by accident. Auto-repeat timing and 'Left+Right cancel' are pinned by tests before the input code moves.
- Score meaning: the classic 10x20 Marathon keeps its scoring, and save.dat keeps meaning 'classic Marathon'. Two bug fixes do change play a little on every board: spamming rotate can no longer keep a piece alive forever, and Laser and Bomb pieces now keep the lines they complete. Time-ranked Sprint starts a fresh board, and the old Sprint rows stay as 'Sprint (old)'.
- The launcher must ship together with the Sprint change, or it would show a Sprint time as a big number (9 992 760).
- Until the launcher switches over, two copies of the button kit exist, and any fix made in the meantime has to go into both.
- Line clears play from a copy of the board, so the picture can trail the real stack by up to 0.24 s. That shortens at high levels, and the ghost hides until the rows settle.
- Sliding pixel text has to land on whole pixels, or it blurs when the window is scaled.
- Habits change: Esc and pad B open the pause menu instead of leaving at once, and quitting from the main menu takes two presses of B.
- ghost-common has uncommitted work from earlier (menu spacing, the frame counter, music, the new song files). The rework builds on it and must not lose it. The safest start is to commit that work first, on its own.
- Your real files are never touched while building: every test and screenshot run uses a temporary home folder and never opens a window on your screen. The first time you start a new version, it reads your saves as they are. Each phase is installed only after its checks pass and you say go.
- Defaults I picked for you (change any of them): B on the main menu goes to Quit first; READY-GO before a run and only GO after R; smooth taps that snap during auto-repeat; singles get a popup but no word; natural locks get a soft tick; first launch shows only the Basics page; the game pauses when the window loses focus; Play again is picked first on the results card.
- Sound is a matter of taste, so expect a second round after you listen.

## Mockups

```
1) MAIN MENU (620 x 720 logical window; 1 character = 10 px, 1 line = about 27 px)

+--------------------------------------------------------------+
| [#] v2.1.0                                  Playing as Ghost |
|                                                              |
|      []                          [][]                        |
|          B  L  O  C  K  F  A  L  L                           |
|            Stack them. Clear them. Keep up.                  |
|                                                              |
|         ##====================================+              |
|         ##  Start game                        |              |
|         ##  Marathon on Classic 10x20         |              |
|         ##====================================+              |
|             How to play                                      |
|             Scores                                           |
|             Settings                                         |
|             Updates                                          |
|             Quit                                             |
|                                                              |
|     Your bests on Classic 10x20                              |
|     Marathon   74 372        Sprint   1:12.40                |
|     Ultra       3 902        Power    no run yet             |
|                                                              |
|  []            [][]                        []                |
|  [][][]    [][][]  [][]    [][][][]    [][][]  [][]    [][]  |
|  [][][][]  [][][][][][][]  [][][][][]  [][][][][][][][][][]  |
|                                                              |
|   (Up)(Down) choose     (A) select     (B) to Quit           |
|                  Made by The Real Ghost                      |
+--------------------------------------------------------------+
- BLOCKFALL is 48 px violet. On launch the letters drop in one by one with a small thud; any key skips it.
- Faint pieces fall behind the text and land on the skyline at the bottom. Full rows flash (no white with Reduced flashing) and clear.
- ## marks the focused button: panel colour, a 4 px violet bar and violet text. It slides between items in 0.08 s. Mouse hover lights a button and a click presses it.
- Start game's second line is your last setup. 'Your bests' is for that board (Sprint shows a time if decision 2 says so).
- Hints show keyboard keys or Xbox buttons, whichever you touched last. B first jumps to Quit; a second B quits.

2) PLAY SETUP

+--------------------------------------------------------------+
| [<- Back]                                   Playing as Ghost |
|                                                              |
|  Play setup                                                  |
|  ==========                                                  |
|                                                              |
|  Mode                       |  Sprint                        |
|                             |  Clear 40 lines as fast as     |
|     Marathon   endless      |  you can. The clock starts     |
|  ## Sprint     40 lines     |  on GO.                        |
|  ##                         |                                |
|     Ultra      two minutes  |  Board  <  Classic 10x20  >    |
|     Zen        no game over |                                |
|     Power      power-ups    |   +----------+                 |
|                             |   |..........|  In play:       |
|  Size                       |   |...[][][].|  30 px cells,   |
|     Width      < 10 >       |   |....[]....|  panel at       |
|     Height     < 20 >       |   |..........|  the side       |
|                             |   |[]..[][]..|                 |
|                             |   |[][][].[][|                 |
|                             |   +----------+                 |
|                             |                                |
|                             |  Your best    1:12.40   #1     |
|                             |  World best   0:58.21          |
|     [ Scores for this board ]                                |
|                                                              |
|         [==============  Start  (Enter)  ==============]     |
|                                                              |
|  (Up)(Down) mode   (LB)(RB) board   (A) start   (B) back     |
+--------------------------------------------------------------+
- It opens with the focus on last time's mode, so Enter, Enter replays it. Up/Down walks the modes, Width, Height, the Scores button and Start.
- Left/Right, LB/RB or [ and ] change the board from anywhere. On Width and Height they change the number instead; hold to repeat, faster after 1 s. A size that matches a preset lights that preset; anything else is Custom.
- The preview shows the board's real shape with some rubble and a falling piece, and reshapes in 0.15 s.
- Your best and World best are in the mode's own unit: time for Sprint, points everywhere else.
- Start is always lit, and Enter on any mode row starts too. B, Esc or [<- Back] go back to the menu.

3) PLAY SCREEN (Power, classic 10x20, level 9; a TRIPLE with combo 3, 0.1 s after the lock)

+--------------------------------------------------------------+
|   BLOCKFALL  Power                                           |
|  +------------------------------+   Next                     |
|  |            [@]               |   +----------------+       |
|  |         [@][@][@]            |   |                |       |
|  |                              |   |   [][][][]     |       |
|  |                              |   |       (B)      |       |
|  |      +----------------+      |   |  Next: Bomb    |       |
|  |      |     TRIPLE     |      |   +----------------+       |
|  |      |    COMBO  3    |      |   Hold                     |
|  |      +----------------+      |   +----------------+       |
|  |                              |   |   ..   (dim:   |       |
|  |         .  .  .              |   |  ...    used)  |       |
|  |            .                 |   +----------------+       |
|  |[]       [][]          [][]   |   Score                    |
|  |[][]  [][][][]    []   [][]   |   18 240   +1 350          |
|  |== == == == == * * *  +1 350  |   Level 9                  |
|  |== == == == == * * * * *      |   [][][][][][][]...        |
|  |== == == == == * * * * *      |   Lines 87                 |
|  |[][][]   [][][][][][][][][]   |   Best  24 680             |
|  |[][][][]   [][][][][][][][]   |   (Combo x3) (B2B)         |
|  +------------------------------+   --------------------     |
|                                     Move       Left Right    |
|                                     Turn       Up            |
|                                     Turn left  Z             |
|                                     Drop       Space         |
|                                     Hold       C             |
|                                     Pause      Esc  P        |
+--------------------------------------------------------------+
- == rows: the cleared rows flash, the cells burst outward ( * ) from where the piece landed, and then the stack above drops in. The pixels never leave the well.
- TRIPLE / COMBO 3: stacked callouts sized to fit the well. They rise 8 px and are gone at 0.9 s. +1 350 rises from the rows while the Score counts up to it.
- [@] is the new piece sliding in from the top; the . . . outline is the ghost.
- Next names the power-up a piece carries ('Next: Bomb', letter B). A used Hold is dimmed, and its edge flashes once when hold is ready again.
- Level pips, Lines, Best (it turns gold once you pass it), and the Combo and B2B chips.
- The controls card shows YOUR keys, or Xbox buttons after you touch the pad. M and - / = live on this card, and pressing them shows a small volume chip.
- On flat boards such as 24x12, the panel moves into a strip across the top so the cells can be bigger.
```

## Supporting material

Current-state screenshots, the code map and the designers' full proposals were saved in the planning session's scratchpad (blockfall-plan/). Phase 1 moves the screenshot tool into blockfall/tools/shots.
