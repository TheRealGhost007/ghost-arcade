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
your Marathon, Sprint, Ultra, Power and Dig bests on the board you last set
up (five since 3.4.0; two to a line on three lines since 3.6.1, so a mode's
name never runs into its best, "no run yet" included).
Its eight items: Start game, How to play, Scores, Settings, Updates, **Stats**
(3.3.0, see below), **Daily challenge** (3.6.0, see below; its second line
says today's twist, your best today and the streak) and Quit.

"Start game" opens **Play setup**, remembered from last time:

- **Mode:** a list of Marathon (endless, the pace climbs every ten lines),
  Sprint (clear 40 lines as fast as you can), Ultra (two minutes), Zen (no
  game over: a full stack is swept away and play carries on), Power and
  **Dig** (3.4.0, rising rows: see The play screen). Up
  and Down pick one; the right side says what it is and how it ends, and
  shows your best and the world best on the board (Dig's preview shows the
  grey rows it starts with). Since 3.6.1 the right column ends inside the
  margin, a long name beside the world best is cut short before its value,
  and a custom size's world best reads "this PC only".
- **Board:** Classic 10x20, Wide 14x20, Tall 8x26, Huge 18x30, Tiny 6x14, or
  Custom: any width from 6 to 24 and height from 12 to 36. Left/Right, LB/RB
  or `[` `]` change it from anywhere; Width and Height always work (hold to
  repeat, faster after a second), and a size that matches a preset is that
  preset. A preview shows the board at its true shape, and "In play" the
  cell size it will have. The board scales to fit the window.
- It opens on last time's mode, so Enter, Enter plays your last setup again;
  Enter on any mode row (or Width, Height) starts, and so does Start.
  "Scores for this board" opens its Scores, and **Practice room** (3.5.0,
  under it) opens the room's options: a Marathon on this board with the
  rules bent, where nothing is recorded (see The Practice room).
- **Power mode:** about one piece in seven carries a power-up on one of its
  blocks, and it fires when the piece lands, after any lines it completed
  have cleared: **Bomb** (clears the 3x3 around it), **Laser** (clears its
  whole column), **Freeze** (gravity waits six seconds), **Slow** (gravity is
  2.5x slower for twelve) and **Sweep** (the bottom three rows go).
- **Hold** a piece (once per piece). **Modern rules on every board** (3.2.0,
  the classic 10x20 included): pieces turn with the standard **SRS wall
  kicks** (the published tables for J L S T Z and for the I), so spins
  against walls and into gaps go as in any modern block game and pieces no
  longer climb the stack; **Next is a queue** of the next piece and up to
  five more small ones (Settings > Game, five as it comes; the rules always
  draw six ahead, so a seed gives the pieces it always gave); combos and
  back-to-back; **T-spins** (a T whose last move was a turn, with three of
  its corners filled: 400 with no lines, 800 / 1 200 / 1 600 for one, two,
  three; a mini, with a front corner open, 100 / 200 / 400; times the
  level), which chain back-to-back with quads (half again each; a T-spin
  with no lines keeps the chain); and **perfect clears** (an empty well
  after a clear: 800, 1 200, 1 800 or 2 000 more, 3 200 for a back-to-back
  quad). An optional **Rotate 180** key (none until you set one) makes a
  half turn in one press; a T-spin made by a half turn is full or mini by
  its corners alone (3.6.1: one that went through the 180 kicks' fifth
  offset used to count as a full T-spin, so a mini paid as a full one).
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

- **Controls:** a table of the eleven actions (Move left, Move right, Soft
  drop, Hard drop, Rotate right, Rotate left, Hold, Pause, Restart, since
  3.2.0 Rotate 180, which comes with no key or button, and since 3.5.0
  Undo, the Practice room's key, U and RB as it comes; those two are the
  optional kind, whose only key may be cleared or taken), each
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
  **Ready, go**, **Resume countdown** (3, 2, 1 after a pause), **Show
  How to play: Next launch**, a button (its Basics page opens the next time
  Blockfall starts, once; the note says so, and the row's help says it is
  set), and **Next queue** (3.2.0): how many pieces Next shows, 1 to 6, five
  as it comes (looks only: the pieces are the same).

## How to play

Five pages, with tabs and `<` `>` buttons: **Basics, Hold and next,
Scoring, Modes** and **Power-ups**. Each has a small well playing a looping
example on the real rules: a T slides, turns both ways, soft-drops and
hard-drops into a gap for a single; an S goes into Hold and comes back; a
quad (800) and then a back-to-back quad (1 250 with the combo's 50); the
mode you pick plays the single with its own number under the well (Dig,
3.4.0, instead stands an I in the gaps of four garbage rows: a quad, 800
and the rows' 400, with the Dug count and the next row's wait under the
well); the power-up you pick fires on a stack. The captions name **your** keys, or
your pad's buttons once you use the pad, and the key the example presses
lights up as it presses it. Scoring lists the clears (100, 300, 500, 800
times the level), the level's speed, the combo and back-to-back bonuses,
T-spins and perfect clears, the drop points, and that every board scores
the same way (3.2.0). Left/Right, LB/RB, Tab or a click change the page; on Modes and
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
  letter (B, L, F, S, W) with a glow of the power's colour round it (steady
  with Reduced flashing). **The Next queue** (3.2.0): under the large next
  piece a row of up to five small ones, the pieces after it (beside it, in
  two columns, when the panel is a strip); the box grows by 24 px and Hold
  and the stats move down with it. They move up a slot with the slide as a
  piece comes in (a cut with Reduced motion), and a small block that carries
  a power-up is drawn in that power's colour.
- **One main number per mode:** the score (Marathon, Power, Dig), the clock
  (Sprint, big with hundredths, the lines left under it: gold in the last
  five), the countdown (Ultra: gold from 0:30, red from 0:10, ticking each
  second, and each of the last five seconds sounds a rising tick), the
  fresh starts (Zen). Level has its ten pips (Dig shows its **Dug** count
  there instead: its level stays 1); Best says "No best yet" on a
  new board and turns gold once you pass it.
- **Dig** (3.4.0): the well starts with eight grey garbage rows, each with
  one gap, never in the column of the gap of the row under it. Another row
  pushes up from the floor every 8 s at first and sooner as the run goes
  on, evenly down to every 3 s at 150 s and after; the stack and a piece
  resting on it go up with it (the stack is drawn a cell low and eases up
  over 0.12 s; a cut with Reduced motion), with a low thud (the hard drop's
  sound at 0.6). The **Next row** bar (after the combo chips; in the
  strip's status row on a flat board) fills over the wait and turns red,
  pulsing twice a second, in its last quarter (steady red with Reduced
  flashing); a pause holds it, and so does READY. Every garbage row a clear
  takes counts as **Dug** and pays 100 on top of the clear's points; two
  rows or more in one clear say "DUG 2" under the clear's word. The run
  ends when a row is pushed past the top of the well, or a piece has
  nowhere to go: a top-out, with the rows dug on the results card. The
  gaps come from an rng of their own, so a seed gives Dig the same pieces
  as every other mode. Danger and the music treat Dig as a stacking mode.
- **Danger** (3.1.0): with the stack within four rows of the top the well's
  frame eases to red over 0.4 s and pulses gently (steady with Reduced
  flashing), faint red stripes hatch the top three rows, and when the next
  piece would not fit where pieces come in its outline shows there in red.
  It only lets go once seven rows are clear again, so it never flickers,
  and the shared WARNING jingle (the one Ultra plays at 0:10; a low warning
  of its own until 3.6.1) plays once as it starts, never more often than
  every 8 s. Zen, which cannot be lost, never shows it.
- **The music follows the board** (3.1.0): in Marathon, Power and Dig its
  intensity climbs with the stack as well as the level (0.32 on an empty
  well at level 1; the lead comes in above 0.35 and the snare above 0.5),
  so the snare arrives as the stack rises and drops out when you dig out,
  and danger adds a little. Sprint follows the lines cleared, Ultra the time
  (full in its last ten seconds), and Zen stays at one calm level.
- **Sprint splits** (3.1.0): Sprint is a race, so its clock is the big
  number and starts on GO. Marks at 10, 20 and 30 lines say how far ahead
  ("-1.4 s", gold) or behind ("+0.8 s") your best pace on the board you are:
  over the well for a moment, and under the clock until the next mark. The
  pace is the splits of the fastest finished Sprint on this machine
  (`splits.dat`); with none yet a mark shows its own time ("10: 0:24.10").
  The finish says "New best by 0.8 s".
- **Combo and back-to-back** chips light up while they are on ("Combo x3",
  and a gold "B2B ready" after a quad or a T-spin clear, on every board);
  Freeze and Slow are draining bars (Freeze's blinks in its last 1.5 s).
- **Callouts:** up to three lines over the well (DOUBLE, TRIPLE, QUAD,
  T-SPIN, T-SPIN MINI, T-SPIN SINGLE / DOUBLE / TRIPLE, PERFECT CLEAR over
  the clear's own word, BACK-TO-BACK, COMBO 3, LEVEL 4, a power-up's name,
  NEW BEST, FRESH START), the most important one big. A T-spin's points pop
  up even with no lines. They rise a little, hold and are gone in
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
- **Power-ups** (3.1.0) each have their own show, all looks only (the rules
  fire them on the frame the piece locks, as before):
  - **Bomb:** the 3x3 it takes stays in view and blinks for 0.12 s (steady
    lit with Reduced flashing), then bursts in its own colours as a square
    shockwave spreads (it fades in place with Reduced motion) and the well
    shakes 9 px.
  - **Laser:** a charge line runs down the column for 0.12 s (there at once
    with Reduced motion), then the beam, and the column's cells pop out from
    the power cell, nearest first.
  - **Freeze:** an ice wash crosses the well, frost grows in from the
    corners over 0.6 s and holds; in the last 1.5 s it blinks with its bar
    (steady, a little dimmer, with Reduced flashing), and when it ends the
    frost breaks with a thaw tick.
  - **Slow:** the orange frame, two faint afterimages trailing the piece
    (none with Reduced motion), the draining bar and a wind-up sound as it
    ends.
  - **Sweep:** a green bar crosses the bottom three rows in 0.14 s and
    bursts their cells as it passes, then the stack drops three rows like a
    clear's fall (with Reduced motion the rows flash green and cut). Before
    3.1.0 they just vanished.
  - Each one fires with its name in its colour over the well, its own sound
    and a soft coin.
- **Sound** (3.1.0): seventeen sounds (eighteen until 3.6.1), all made in
  code (`tools/gen_sounds.py`, nothing sampled). A natural lock gets a soft tick,
  Hold its own swoosh (it borrowed the turn's sound), each power-up its own
  sound, and a back-to-back quad a sparkle. Moves vary a little, a double and
  a triple ring higher than a single, each combo step adds a semitone, and
  the results card's count-up ticks rise. One rule: each lock plays at most
  one big sound (a power-up, a quad or a clear) plus one accent (the sparkle,
  the coin, the thud or the tick), and the level-up sound waits 0.15 s so it
  never clashes with the clear. Ultra plays the shared WARNING once as its
  countdown turns red at 0:10 and ticks each of its last five seconds;
  danger plays the same shared WARNING jingle as it starts (3.6.1; until
  then it had its own low two-note warning), and when danger and Ultra's
  0:10 come together it plays once. M, - and = work as they always did.
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
  confetti and its jingle ("Old best 24 680" under it; a Sprint says "New
  best by 0.8 s (was 1:40.00)"); otherwise "Best 26 714, 11 694 to go" (or
  seconds behind, for Sprint). Then the run's place on
  this machine and where it would stand on the world board (as last
  synced), a trophy it just earned, and the run in numbers: lines, level,
  pieces and pieces per second, time, quads, best combo, back-to-backs,
  T-spins (3.2.0, where holds were) and power-ups (fresh starts in Zen, the
  rows dug in Dig since 3.4.0). Play again (picked first; your
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

- **One Best per mode and board.** Since 3.2.0 (modern rules on every
  board) the classic 10x20 Marathon has a **fresh board**, `marathon-10x20`:
  the menu's Best, Play setup, the panel, the results card and the world
  board start it afresh. The old `marathon` rows and the best in `save.dat`
  (the 74 372 from before the score tables existed) are the classic board
  under the old rules, shown as **Classic (old rules)**, the last board of
  the Scores browser's Marathon tab: read, never written to again. Nothing
  changes `save.dat` any more.
- **Sprint is ranked by time**, fastest first, on its own **Sprint 40**
  boards, and only a finished Sprint goes on them. A row there stores
  10,000,000 minus the time in hundredths of a second (so the faster run is
  still the bigger number every table sorts first); Blockfall and Ghost
  Launcher show it as a time, `1:12.40`. The points-ranked Sprint rows from
  before 2.1 are kept and shown as **Sprint (old)**.
- The five preset boards share their scores with
  [Ghost Launcher](../ghost-launcher) and the online board (`marathon-10x20`
  for the classic Marathon, `ultra` for Ultra on the classic board,
  `sprint40-14x20`, `power-18x30`, `dig` and `dig-14x20` for Dig (3.4.0),
  ...; the launcher names `marathon-10x20`
  "Marathon" and the old `marathon` "Classic (old rules)", and `dig-14x20`
  "Dig 14x20" by its general rule). **Custom sizes
  keep their scores on this machine only**, in Blockfall's own table, so they
  never reach the world board.
- A run counts (runs log, trophies, scores, stats) only if a piece locked
  and it scored; a tuned run (F2 panel) never counts, and nor does a run in
  the Practice room (3.5.0).
- **The Scores browser:** a tab for each mode (Marathon, Sprint, Ultra,
  Power, Dig and Zen; a click, Left/Right or
  LB/RB; Zen's tab is faint and says it keeps no scores), a board picker
  under them (`[` `]`, X or pad X, a click or the wheel) listing the five
  presets, the size set up in Play setup and every size with scores on this
  machine or the world, with Sprint's old points boards last as "(old)" and
  the classic Marathon's old-rules board last on its tab as "(old rules)";
  This machine | World (a click, Up/Down) and All time | This week | Today
  (a click, `T` or pad Y). The table keeps its medals, your rows lit and
  where you stand ("You are #4, 15 570 behind #3"), and under the Classic
  (old rules) table your classic best from before the tables existed.
  **Play this board** starts a run in that mode on that board.

## Stats

**Stats** on the main menu (3.3.0) is what you have played, for life:

- A row for **All modes** and one for each mode (Dig since 3.4.0, before
  Zen), with its **runs, lines and
  time played**. Up and Down, the d-pad, the pointer or a click put the
  focus bar on a row, and the rest of the screen is that row's.
- **Records**, each with the day it was set (and, under All modes, the mode
  that set it, the mode and the day each on a line of their own since
  3.6.1, so a long mode name no longer runs into the next tile): the best **pieces per second** (a run of at least 20
  pieces), the **biggest combo** and the **most quads in a run**.
- A **lines-by-clear bar**: single, double, triple and quad lines in piece
  colours, with the lines T-spins cleared set apart, a legend with each
  share, the total, and a line with the T-spins, perfect clears, power-ups
  and holds. It grows in from the left over 0.35 s when the screen opens
  and when the row changes (there at once with Reduced motion); nothing on
  the screen flashes.
- It lives in Blockfall's own file, `$XDG_DATA_HOME/blockfall/stats.dat`:
  as a run ends, its counters (the ones the results card shows) are folded
  into its mode. **The same runs count as for the runs log and the
  trophies**: a piece locked and it scored, never a tuned playtest.
  Counting starts with the first run that ends after 3.3.0 is installed;
  `runs.log` is written as before, for Ghost Launcher.
- Esc, pad B, the mouse's back button or [<- Back] go back to the menu.

## The Practice room

**Practice room** (3.5.0), a button on Play setup under Scores, is a place
to learn and try things out: a Marathon on the board you set up, whatever
mode Play setup had, with the rules bent as you like. **Nothing it does is
recorded**: no score, no best (the panel shows Undos left in Best's place),
no stats, no `runs.log` line, no trophy, and Restart and Quit to menu never
ask "are you sure?" there.

- **The options**, a screen of their own, kept in `config.ini` between
  runs: **Start level** (1 to 20: the speed, and the lines count up from
  it), **Gravity** (Off: the piece hangs where it is until you drop it;
  soft and hard drops still work), **Lock delay** (Off: a landed piece
  never locks by itself, so you can slide it about; a hard drop locks it),
  **Hold** (Once, once per piece, or Infinite: as often as you like on one
  piece; "Once per piece" until 3.6.1, too wide for its stepper), **Starting garbage** (0 to 12 of Dig's grey rows, one gap each,
  to practise digging; clearing one pays the clear alone) and **Finesse
  hints**. Up and Down move from row to row, Left and Right change a value
  (a click on its arrows or halves too), Start starts; the line under the
  rows names your Undo key and button, and Esc, pad B or [<- Back] go back
  to Play setup.
- **Undo:** U, or the pad's RB (Settings > Controls, the eleventh row),
  takes the last piece back, up to 50 times in a row, exactly: the well,
  the piece and the queue, the bag and its rng, Hold, the score, the level
  and the clock as they were when that piece came in, so the pieces after
  it come as they would have. UNDO shows over the well with a low swoosh
  (the hold's sound at 0.7), the panel's **Undos left** counts down (and
  nudges), and at the run's start nothing more goes back. A restart starts
  the count over. Outside the room the key does nothing.
- **Finesse hints:** a piece that took more presses than it needed says
  how many as it lands, in dim words under the lock's own ("2 extra
  presses", "1 extra press"; "+2" where the well is narrow); a clean lock
  says nothing. A tap of Left or Right is one press, Left or Right held to
  the wall is one however far it slides, a turn is one (a half turn two:
  the optional Rotate 180 key is not assumed); soft drop, hard drop and
  Hold are not presses. The fewest presses are found from where the piece
  comes in to a hard drop onto exactly the cells it locked on, whatever
  its rotation number (an S lying flat is the same either way up); a
  placement that needs the stack (a tuck under an overhang, a spin into a
  slot) gets no hint.
- **On the play screen:** a gold **PRACTICE** tag where the mode's name
  goes (with the board's size off the classic board), Undo on the controls
  card in the Volume row's place (M still mutes), and the results card
  saying "Practice: nothing is recorded" with the run's holds as its last
  number. The run ends as a Marathon does, at the top of the well.
- With Motion: Reduced the callouts sit still and the options' focus plate
  is just there; with Reduced flashing nothing flashes white as a piece is
  taken back or lands.

## The Daily challenge

**Daily challenge** (3.6.0), on the main menu above Quit, is a two-minute
score attack everyone plays with the same pieces today. Ghost Launcher's
**D** key (`blockfall --daily`) opens the game on it, and Blockfall is the
eighth game on the launcher's daily desk.

- **The same pieces for everyone:** the run is seeded from the shared daily
  code the other seven daily games use (`ghost-common/daily.c`: the same
  seed all day, a different one every day), so the pieces, and the
  power-ups where the twist has them, are the day's. A restart plays them
  again from the start.
- **The twist changes with the date**, the same for everyone, picked from
  the date alone: **Ultra** on the classic board, **Ultra on Wide**
  (14x20), **Power Ultra** (power-ups on the clock), **Tall** (8x26),
  **Tiny** (6x14), a **Fast start** (the classic board from level 8, the
  lines counting up from it) and **Dig on the clock** (Dig's rising rows
  with the clock ending it). Every twist has its day each week, in an
  order the week's own daily seed shuffles, and never the same one two
  days running.
- **Retry as often as you like:** your best counts. Every run with points
  goes on the day's own board, `daily-YYYYMMDD`, on the shared table (so
  Ghost Launcher and the world board see it, with the five preset boards'
  rows), and **never on a normal board**: a daily Power Ultra leaves the
  Power board alone. It counts for the lifetime stats under its twist's
  mode and for the trophies, and its `runs.log` line carries the day's key,
  which is how the launcher's desk counts Blockfall's days.
- **The streak** is days in a row with a daily played; it lives until a
  whole day passes without one (yesterday's still counts until today is
  over), as the launcher's desk counts it. It lives in Blockfall's own
  `$XDG_DATA_HOME/blockfall/daily.dat`: a line per day with your best and
  the runs. A run with nothing landed or no points touches none of it.
- **The Daily screen:** the day, the twist and what it is, your best
  today, the runs today, the streak and the best streak, today's board on
  this machine and in the world (the top five of each, your rows in gold)
  and where your best would stand in the world, then Start. Esc, pad B,
  the mouse's back button or [<- Back] go back to the menu. It reads the
  files as it opens, and reads the clock again, so a game left open past
  midnight plays the new day.
- **In play:** a gold **DAILY** tag with the twist where the mode's name
  goes (with the board's size off the classic board), the clock first in
  the panel as Ultra's (Dug in Lines' place on the Dig twist), Best the
  day's board's, Ultra's warning at ten seconds and its last ticks, and
  the music following the clock. The pause menu's "Leave this run?" says
  what it counts for on today's daily board. The results card says TIME UP
  (or GAME OVER on a Dig top-out) with "Daily: Power Ultra on Classic
  10x20", your place on this machine and in the world, and its **Daily**
  and **Scores** buttons both go to the Daily screen, which shows the
  board.
- With Motion: Reduced the screens cut instead of sliding; the Daily
  screen itself has nothing that moves or flashes.

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
| Rotate 180    | none until set (3.2.0) | none until set    |       |
| Undo (Practice room, 3.5.0) | U    | RB                    |       |
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
  left then starts without a key; the same goes for Undo's U and RB (3.5.0:
  an older file that already gave them to another action keeps them there,
  and Undo starts with none). Rotate 180 (a half turn in one press, with
  the SRS+ 180 kicks) has no key or button at all until you set one. It and
  Undo are the two actions whose only key can be cleared (Delete) or taken
  by a swap; neither is on the controls card outside the Practice room,
  where Undo takes the Volume row.
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
| Row shown (Stats)            | Up / Down                    | D-pad               | Hover or click a row     |

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
  `auto_pause`, `show_howto`), 3.2.0 `rotate_half`, `rotate_half2`,
  `pad_rotate_half`, `pad_rotate_half2` and `next_queue`, and 3.5.0 `undo`,
  `undo2`, `pad_undo`, `pad_undo2` and the Practice room's `practice_level`,
  `practice_gravity`, `practice_lock`, `practice_hold`, `practice_garbage`
  and `practice_hints`, all of which an older build ignores. A key with
  none is written `NONE`.
- The classic Marathon best under the old rules (score, level, lines):
  `$XDG_DATA_HOME/blockfall/save.dat`, read and never changed since 3.2.0
- Custom-size scores (this machine only): `$XDG_DATA_HOME/blockfall/scores.txt`
- Sprint's best pace per board, the splits at 10, 20, 30 and 40 lines in
  hundredths (3.1.0): `$XDG_DATA_HOME/blockfall/splits.dat`
- The runs log: `$XDG_DATA_HOME/blockfall/runs.log`
- Lifetime stats per mode (runs, lines, time, pieces, clears by size and
  the T-spin ones among them, T-spins, perfect clears, power-ups, holds,
  and the three records with their days; 3.3.0):
  `$XDG_DATA_HOME/blockfall/stats.dat`, sections per mode with `key=value`
  lines; a line this build does not know is ignored
- The Daily challenge's days (3.6.0): `$XDG_DATA_HOME/blockfall/daily.dat`,
  a `day|your best|runs` line per day a daily was played; a line this
  build does not know is skipped
- Shared with Ghost Launcher: `$XDG_DATA_HOME/ghost-launcher/scores/blockfall.txt`
  (this machine) and `scores/global/blockfall.txt` (the world board), plus
  achievements and your player name
- Shared by every game: `~/.config/ghost-arcade/prefs.ini` (music volume,
  screen shake, reduced flashing, fullscreen)

All are plain, human-readable text files.

## Project structure

```
src/
  tetromino.c/h   piece shapes, rotation states, colours, the SRS wall-kick
                  tables (and the SRS+ 180 list)
  board.c/h       the well at any size, collision, locking, line clears,
                  the power-up board effects
  game.c/h        the rules: gravity, lock delay, SRS turning, the Next
                  queue, hold, scoring (T-spins, perfect clears, combos,
                  back-to-back), levels, modes, power-ups, Dig's garbage
                  rows and rises (3.4.0), top-out and
                  lock-out, and the one-frame events the sounds and
                  effects react to
  runrecord.c/h   what a run records: which runs count, the boards (the
                  classic Marathon's fresh marathon-10x20 and its old-rules
                  marathon, Sprint by time, custom sizes local), the end sound
  localtable.c/h  this machine's own score table for custom sizes
  controls.c/h    the eleven actions' keys and pad buttons, clash swaps
                  (Esc and Menu never handed over, no action left without
                  a key but the two optional ones), key and button names
                  for the screens (no raylib)
  finesse.c/h     the Practice room's hints: the fewest presses that put a
                  piece where it locked (3.5.0, no raylib)
  practice.c/h    the Practice room's undo: a copy of the Game per piece,
                  up to fifty back (3.5.0, no raylib)
  challenge.c/h   the Daily challenge (3.6.0, no raylib): the day's key and
                  date arithmetic, the twist's weekly rotation and each
                  twist's board and rules, the streak file and its logic,
                  the words on the menu, over the well and on the card
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
                  as it was stamped (a Sweep's bar and drop too), the
                  piece's glide, slide in and pop, landing, the ghost's
                  rows, Hold's flight, Next's slide (no raylib)
  soundplan.c/h   which sounds a frame asks for: one big sound and one
                  accent per lock, the pitches, the delayed level up, the
                  jingles (no raylib)
  powerfx.c/h     the power-ups' shows in time: the Bomb's blink and
                  shockwave, the Laser's charge and pops, the Freeze's
                  frost, blink and thaw, the Slow's afterimages (no raylib)
  danger.c/h      the danger warning (free rows, hysteresis, the 8 s
                  warning gap, the next piece's outline), the music's
                  intensity per mode, Ultra's last ticks (no raylib)
  splits.c/h      Sprint's split marks against your best pace, their words,
                  the pace file (no raylib)
  stats.c/h       the lifetime stats: the fold of a run's counters into its
                  mode, the records, the total over the modes, the
                  lines-by-clear bar, the file (no raylib)
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
    scr_stats.c     Stats (stats.h), on the button kit: the rows, the
                    records, the bar
    scr_practice.c  the Practice room's options (3.5.0), on the button kit
    scr_daily.c     the Daily challenge screen (3.6.0), on the button kit:
                    today's twist, your best, the streak, today's board
    scr_common.c    menu sounds, the slide, the wipe, the title, the Back
                    button
  actions.c/h     what keys and the pad do in a run (no raylib)
  menukeys.c/h    Blockfall's own menu keys: W A S D, [ ], T, X, Delete
                  (no raylib)
  menumodel.c/h   what the menus decide: title drop, slide, held steppers,
                  the setup's board rules, the preview, the Practice
                  room's options and their words (no raylib)
  backdrop.c/h    the menu's skyline of landing pieces (no raylib)
  comfort.c/h     the Motion and Reduced flashing switches every effect asks
  scoresview.c/h  the Scores browser's table (times for Sprint 40, the
                  blinking new row, the classic best from before the table)
  changelog.h     the Updates screen
tests/            headless tests (no raylib): rules, records and files,
                  modern rules (the SRS tables against the published ones,
                  T-spins, perfect clears, the seed's sequence, the queue,
                  Rotate 180, the old-rules board read only),
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
                  every How to play demo against what its page says; the
                  sound plan (thousands of locks against the one-big-sound
                  rule, each lock's cues, a run with every effect driven
                  against one without), the power-ups' timelines, danger
                  (hysteresis, the warning's gap, intensity in every mode,
                  Ultra's ticks) and Sprint's splits (marks, words, the pace
                  file, the clock on GO); the lifetime stats (the file's
                  round trip and what it puts up with, the fold of a
                  scripted run, the records and their days, the total, the
                  bar's T-spin lines); and Dig (3.4.0: the garbage it starts
                  with, a seed's pieces unchanged, the rise curve and the
                  rises, the lifted piece and the top-out, the warning bar,
                  Dug counting and its bonus, its keys, panel, card,
                  callout, rise look and sound, words and demo); and the
                  Practice room (3.5.0: each option against the rules, a
                  restart keeping them, undo exact to the byte over fifty
                  steps and the same pieces after it, the finesse search
                  against known press counts and the counter against a
                  frame's presses, nothing recorded, the eleventh action
                  in the controls, config.ini and the play keys, the
                  Settings page, the panel and the words); and the Daily
                  challenge (3.6.0: the day's key and dates across month
                  and year ends, the twist's rotation, the same date's
                  pieces and power-ups and a different date's, the clock
                  in every twist, the fast start, Dig on the clock, a
                  restart, what it records and the normal boards left
                  alone, the streak file and logic, the panel, music,
                  sounds, card, pause words and menu line)
tools/
  shots/          the hidden-window screenshot harness (`make shots`)
  gen_sounds.py   generates the synthesized .wav sound effects (all
                  seventeen; deterministic, so the old ones never change)
assets/           sounds, the pixel font, the icon
packaging/        the XDG desktop entry
```

The rules (`tetromino.c`, `board.c`, `game.c`) and everything that decides
what is recorded, what a key does or where things go on screen
(`runrecord.c`, `localtable.c`, `controls.c`, `layout.c`, `hudfx.c`,
`feel.c`, `playstep.c`, `persist.c`, `repeat.c`, `actions.c`, `menumodel.c`,
`backdrop.c`, `app.c`, `pausemenu.c`, `results.c`, `wipe.c`, `setpage.c`,
`howtodemo.c`, `look.c`, `soundplan.c`, `powerfx.c`, `danger.c`,
`splits.c`, `stats.c`, `finesse.c`, `practice.c`, `challenge.c`) have no
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
