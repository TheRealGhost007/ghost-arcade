#ifndef CHANGELOG_H
#define CHANGELOG_H

#include <stddef.h>

/* Static changelog data for the in-game Updates screen. Header-only since
 * it's only ever included by render.c; add a new entry at the top of
 * kChangelog (index 0 = newest) when bumping BLOCKFALL_VERSION. */

#include "ui.h"

static const char *const kNotes_3_6_1[] = {
    "Danger plays the shared WARNING jingle as it starts, the one Ultra plays at 0:10 (from ghost-common), in place of its own low two-note warning; still at most every 8 s, never in Zen, and once when danger and Ultra's 0:10 come together",
    "A T-spin made by a half turn (Rotate 180) is full or mini by its corners alone: one that went through the 180 kicks' fifth offset counted as a full T-spin, so a mini paid as a full one. Minis stay minis; quarter turns score as before",
    "The main menu's Your bests sits two to a line on three lines, so a mode's name never runs into its best (\"no run yet\" included), still above the skyline",
    "Stats: under All modes a record's mode and its day take a line each, so a long mode name no longer runs into the next tile",
    "Practice room: the rows sit inside the screen's margin, and Hold reads Once or Infinite (\"Once per piece\" was wider than its stepper)",
    "Play setup: the Size header keeps clear of Dig's row, the right column ends inside the margin, a long name beside the world best is cut short before its value, and a custom size's world best reads \"this PC only\"",
    NULL,
};

static const char *const kNotes_3_6_0[] = {
    "The Daily challenge, an item on the main menu above Quit: a two-minute score attack everyone plays with the same pieces today. The run is seeded from the shared daily code the other seven daily games use, so the pieces (and power-ups) are the day's for everyone, and it goes on the day's own board, daily-YYYYMMDD, the one Ghost Launcher's desk and the world board read. The normal boards are never touched",
    "The twist changes with the date, the same for everyone: Ultra on the classic board, Ultra on Wide, Power Ultra (power-ups on the clock), Tall, Tiny, a fast start from level 8, and Dig on the clock. Every twist has its day each week, never the same one two days running",
    "Retry as often as you like: your best counts, every run has its row, and a streak shows: days in a row with a daily played, kept in Blockfall's own daily.dat (the day, your best, the runs); it lives until a whole day passes without one, as the launcher's desk counts it. The menu item says today's twist, your best today and the streak",
    "The Daily screen: the day, the twist and what it is, your best today, the runs, the streak and the best streak, today's board on this machine and in the world (your rows in gold) and where your best would stand, then Start. --daily (Ghost Launcher's D key) opens the game on it; Blockfall joins the launcher's daily desk as the eighth daily game",
    "In play a gold DAILY tag with the twist where the mode's name goes, the clock first in the panel as Ultra's (with Dug in Lines' place on the Dig twist), Best the day's board's, Ultra's warning and last ticks, and the music following the clock. A restart plays the day's pieces again. The results card says Daily and the twist, and its Daily and Scores buttons go to the Daily screen",
    "Old config.ini, save.dat and score files load unchanged; a daily counts for the lifetime stats under its twist's mode and in runs.log under the day's key",
    NULL,
};

static const char *const kNotes_3_5_0[] = {
    "The Practice room, a button on Play setup under Scores: a Marathon on the board you set up with the rules bent, to learn and try things out. Nothing it does is recorded: no score, no best, no stats, no runs.log, no trophy",
    "Its options, kept between runs: the start level (1 to 20; the lines count up from it), gravity on or off (off, the piece hangs until you drop it), the lock delay on or off (off, a landed piece never locks by itself; a hard drop locks it), Hold once per piece or infinite, starting garbage (0 to 12 of Dig's grey rows), and finesse hints",
    "Undo: U or the pad's RB takes the last piece back, up to 50 times in a row, exactly: the board, the queue and the bag, Hold, the score, the level and the clock as they were when the piece came in, so the next pieces come as they would have. An UNDO word over the well and a low swoosh; the panel counts the pieces that can still be taken back in Best's place",
    "Finesse hints: a piece that took more presses than it needed says how many as it lands, in dim words under the lock's own (\"2 extra presses\"). A tap or a held slide is one press, a turn is one; drops and Hold are not presses. A placement finesse does not count (a tuck, a spin) gets no hint",
    "A gold PRACTICE tag where the mode's name goes over the well, Undo on the controls card in the Volume row's place, and the results card says Practice: nothing is recorded. Restart and Quit never ask \"are you sure?\" there",
    "Undo is an eleventh row in Settings > Controls (U and RB as it comes), rebindable like any other and, like Rotate 180, the kind whose only key may be cleared or taken; it does nothing outside the room. An older config.ini loads unchanged: Undo takes U and RB unless one of yours already is, and the room starts with its defaults",
    NULL,
};

static const char *const kNotes_3_4_0[] = {
    "Dig, a sixth mode on Play setup (rising rows): the well starts with eight grey garbage rows, each with one gap, never in the column of the gap of the row under it. Another row pushes up from the floor every 8 s at first and sooner as the run goes on, every 3 s from two and a half minutes in; the stack and a piece resting on it go up with it (the stack eases up a cell; a cut with Reduced motion), with a low thud",
    "Next row, a bar in the panel (after the combo chips; in the strip's status row on a flat board): it fills over the wait and turns red, pulsing, in its last quarter (steady red with Reduced flashing). A pause holds it, and so does READY",
    "Digging: every garbage row a clear takes counts as Dug (the panel's second number, in Level's place: Dig's level stays 1, the rises are its pressure) and pays 100 on top of the clear's points; a clear that digs two rows or more says DUG 2 under its word. The run ends when a row is pushed past the top, or a piece has nowhere to go: a top-out, with the rows dug on the results card",
    "Its own score boards, one per board size like the others (dig on the classic board, dig-14x20 on Wide; custom sizes stay on this machine): the menu's Your bests, now five on three columns, Play setup's bests, the results card and a Dig tab in the Scores browser before Zen. Ghost Launcher and the world board read the dig boards as they are. The Stats screen has a Dig row",
    "How to play's Modes page has the Dig row, with a demo that stands an I in the gaps of four garbage rows: a quad, 800 and the rows' 400, and the Dug count and the next row's wait under its well",
    "The garbage draws its gaps from an rng of its own, so a seed gives Dig the pieces it gives every other mode. Dig comes last in the mode list, so an older config.ini's setup_mode still means what it did; old save, score and config files load unchanged",
    NULL,
};

static const char *const kNotes_3_3_0[] = {
    "Stats, a new item on the main menu above Quit: a row for All modes and one for each mode with its runs, lines and time played, and for the row the focus bar is on (Up and Down, the d-pad, the pointer or a click), the records and the lines-by-clear bar",
    "Three records, each with the day it was set and, under All modes, the mode that set it: the best pieces per second (a run of at least 20 pieces), the biggest combo and the most quads in a run",
    "The bar: single, double, triple and quad lines in piece colours, with the lines T-spins cleared set apart, a legend with each share, the total, and a line with the T-spins, perfect clears, power-ups and holds. It grows in from the left over 0.35 s when the screen opens and when the row changes (there at once with Reduced motion)",
    "The stats live in Blockfall's own file, stats.dat, next to the score tables: as a run ends its counters (the results card's) are folded into its mode. The same runs count as for the runs log and the trophies: a piece locked and it scored, never a tuned playtest. Counting starts with the first run that ends after this version; runs.log is written as before, for Ghost Launcher",
    "Esc, pad B, the mouse's back button or Back go back to the menu, as everywhere",
    NULL,
};

static const char *const kNotes_3_2_0[] = {
    "Modern rules on every board, the classic 10x20 included. Pieces turn with the standard SRS wall kicks (the published tables for J L S T Z and for the I), so spins against walls and into gaps go as in any modern block game, and pieces can no longer climb the stack. The lock delay's resets count a kick like any move",
    "T-spins: a T whose last move was a turn, with three of its corners filled, scores 400 with no lines, 800 for a single, 1 200 for a double and 1 600 for a triple (a mini, with a front corner open, 100, 200 or 400), times the level. Back-to-back now chains T-spin clears with quads, half again each. A T-spin with no lines keeps the chain",
    "Perfect clears: a clear that leaves the well empty adds 800 for a single, 1 200 for a double, 1 800 for a triple and 2 000 for a quad (3 200 back to back), times the level",
    "Callouts for all of it: T-SPIN, T-SPIN MINI, T-SPIN SINGLE, DOUBLE and TRIPLE, PERFECT CLEAR over the clear's own word, with their points popping up; a T-spin's sparkle is the back-to-back's pitched up, and a perfect clear plays the quad's sound higher. The results card counts the run's T-spins",
    "Next is a queue: the next piece large and up to five more small under it (beside it on a flat board), five as it comes; Settings > Game > Next queue sets 1 to 6. The rules always draw six ahead, so a seed gives exactly the pieces (and power-ups) it always gave, however many are shown. The queue moves up with the slide as a piece comes in (a cut with Reduced motion)",
    "The classic 10x20 Marathon gets a fresh score board (marathon-10x20) under the modern rules: the menu's Best, Play setup, the results card and the world board start it afresh. The old table and the 74 372 in save.dat stay as Classic (old rules), the last board of the Scores browser's Marathon tab, read and never written to again; save.dat is never changed. Ghost Launcher names the two boards Marathon and Classic (old rules)",
    "Rotate 180, a half turn in one press, is a tenth row in Settings > Controls with no key or button until you set one (the SRS+ 180 kicks); it is the one action whose only key can be cleared or taken",
    "An older config.ini loads unchanged: the queue shows five, Rotate 180 has no key",
    NULL,
};

static const char *const kNotes_3_1_0[] = {
    "Danger: with the stack within four rows of the top the well's frame eases to red and pulses (steady with Reduced flashing), faint red stripes hatch the top three rows, and the next piece's outline shows in red where it would come in when it would not fit. It only lets go once seven rows are clear, so it never flickers, and a low warning plays once as it starts, never more often than every 8 s. Never in Zen",
    "The music follows the board: in Marathon and Power its intensity climbs with the stack as well as the level, so the snare comes in as the stack rises and drops out when you dig out (it used to reach full at about level 5 and stay there). Sprint follows the lines, Ultra the time, full in its last ten seconds, and Zen stays calm",
    "Ultra's last five seconds each tick, a little higher each time; Sprint's lines-left counter turns gold in its last five lines",
    "Sprint is a race: the clock is its big number, with hundredths, and it starts on GO. Marks at 10, 20 and 30 lines say how far ahead (gold, -1.4 s) or behind (+0.8 s) your best pace on the board you are, over the well for a moment and under the clock until the next mark; with no pace yet a mark shows its time. The finish says New best by 0.8 s, and the fastest finish's splits are kept as the pace to race (splits.dat)",
    "The sound pass: a soft tick when a piece locks on its own, a swoosh for Hold (it borrowed the turn's sound), a sound for each power-up, a thaw tick when a Freeze ends and a wind-up when a Slow ends, and a sparkle for a quad back to back. All made in code, as every Blockfall sound is",
    "Moves vary a little, each clear in a combo rings a semitone higher than the last, and the results card's count-up ticks rise. Each lock plays at most one big sound and one accent, and the level-up sound waits 0.15 s so it never lands on the clear",
    "Ultra plays the warning once as its countdown turns red at 0:10",
    "Bomb: the 3x3 it takes blinks where it stood, then bursts in its own colours with a square shockwave and a 9 px shake",
    "Laser: a charge line runs down the column, then the beam, and the column's cells pop out from the power cell",
    "Freeze: an ice wash, frost growing in from the corners, a blink in its last 1.5 s (the bar too) and the frost breaking with a tick when it ends",
    "Slow: the orange frame, two faint afterimages behind the piece, and the wind-up as it ends",
    "Sweep: a green bar crosses the bottom three rows and bursts them, then the stack drops the three rows (they just vanished before)",
    "Power marks glow in their power's colour. Reduced flashing holds every blink and glow steady and keeps the white out; Motion: Reduced cuts the charge line, the frost's growth, the afterimages, the shockwave's spread and the Sweep's bar",
    NULL,
};

static const char *const kNotes_3_0_0[] = {
    "Settings has tabs across the top: Controls, Feel, Look, Sound and Game. LB and RB, Tab or a click switch them, every change saves at once, and rows marked * are shared with every game",
    "Rotate left: Z on the keyboard and X on the pad. Hard drop, Rotate left, Hold and Restart get a second key, and Hold also answers LB",
    "Every action has two keys and two pad buttons you can set. Pick one and press the new key or button; a five-second bar shows how long it waits, and Esc (or Menu on the pad) gives up",
    "A key another action already has swaps places with yours, and a note says what moved. Esc and Menu always pause, so a swap never takes them away, and Delete or pad Y clears a second key",
    "Reset controls puts every key and button back, asking first. Keys have proper names now: Left Shift, not LEFT_SHIFT",
    "Feel: how long Left or Right waits before it slides (50 to 300 ms), how fast it slides (0 to 100 ms, 0 goes straight to the wall) and the soft drop's speed (Slow, Normal, Fast or Instant). The presets Relaxed, Standard and Fast, and a strip to try them on. Standard is how Blockfall always felt",
    "Look: the ghost piece as an outline, solid or off, the grid on, faint or off, and patterns on the blocks so you can tell the pieces apart without their colours",
    "Game: pause when the window loses focus (on, as before), how R restarts, Ready go, the countdown after a pause, and How to play on the next launch",
    "How to play has five pages with moving examples on the real rules: Basics, Hold and next, Scoring, Modes and Power-ups. They name your keys, or your pad's buttons, and light each one as the example presses it",
    "The first launch shows just the Basics page, with the rest a menu away",
    "The controls card in a run has a Rotate left row and shows Pause's two keys together",
    NULL,
};

static const char *const kNotes_2_5_0[] = {
    "Esc, P, pad Start or pad B open a pause menu: Resume, Restart, Settings, How to play and Quit to menu. The screen dims, the card slides up, and the game pauses here when its window loses focus",
    "Settings in the pause menu changes the effects and music volume, sound, the ghost piece and screen shake on the spot, saved at once",
    "Restart and Quit to menu ask first when the run has points, and so do R and pad View in play. Keep playing is picked first, and Esc or pad B say no",
    "Enter, Space or pad A on Resume never drop your piece: buttons act when you let go",
    "A run that ends (a top-out, a finished Sprint, Ultra's time up) ends on a results card. First the piece that could not come in blinks red and the rows turn grey (a Sprint clear bursts them upward, time up dims them), then GAME OVER, SPRINT CLEAR or TIME UP drops in with your score counting up",
    "A new best stamps NEW BEST in gold with confetti and its jingle; otherwise the card says how far your best is. It shows your place on this machine, where you would stand in the world, a trophy you just earned and the run in numbers",
    "Play again, Change setup, Scores and Menu on the card, and R plays again and Esc goes to the menu. It ignores presses for the first 0.4 s, so a hard drop still held never skips it, and a press after that jumps to the end",
    "Scores is a browser: a tab for each mode (Zen's says it keeps no scores), a board picker with every size you have scores on, This machine or World, and the periods. Play this board starts a run there, and a run you just finished blinks on its board",
    "Under the classic Marathon table, your classic best from before the score table",
    "A wall of blocks clears row by row into and out of every run (a plain cut with Motion: Reduced)",
    "New in Settings: Ghost piece, R restarts (Asks first or At once) and Countdown after pause (3, 2, 1 before play goes on)",
    "How to play names the pause menu and how R restarts",
    NULL,
};

static const char *const kNotes_2_4_0[] = {
    "Line clears flash, then the blocks zip out from where your piece landed and burst into pixels, and the rows above drop into the gap and settle. A quad bursts all four rows at once with gold and flashes the frame gold",
    "Each clear size has its own beat: a double and a triple ring higher than a single, a triple gives a small shake, and a quad back to back sounds brighter",
    "The clear never slows the game: it is looks only, and at levels 10 to 14 it takes 0.18 s and from 15 just 0.12 s, so the picture never trails the stack for long",
    "Pieces slide in from the top, a single tap glides half a cell instead of jumping (holding a key still snaps), and a turn gives a small pop. A turn that cannot be made gives a dull tick",
    "A hard drop leaves a smear, squashes on landing and throws dust sideways; a piece that lands on its own settles with a brighter edge, and a landed piece darkens as its time to lock runs out",
    "The ghost has a light fill, and the rows it would complete are marked on both sides of the well",
    "A held piece flies into the Hold box, Hold's edge flashes when it is ready again, and Next slides its old piece out as the new one comes in",
    "Each run starts with READY, then GO: the clock waits for GO, and a turn or hold pressed during READY happens at GO. After a restart only GO shows",
    "New in Settings: Piece motion (Smooth or Snap) and Ready, go (On or Off). Motion: Reduced and Reduced flashing calm all of it",
    NULL,
};

static const char *const kNotes_2_3_0[] = {
    "The play screen fits every board: tall boards keep the panel at the side, flat ones get a strip across the top and much bigger blocks (24x12 goes from 14 px to 22 px), and tiny boards get a roomy panel with a big score",
    "Each mode has its main number up front: the score, Sprint's lines left with the clock, Ultra's countdown (gold from 0:30, red and ticking from 0:10), Zen's fresh starts",
    "Clears get callouts: DOUBLE, TRIPLE, QUAD, BACK-TO-BACK, COMBO and LEVEL stack up to three lines, sized to fit the well, and every clear shows its points rising from the rows",
    "The score counts up, a stat that changes gives a little nudge, and a level up lights the pips one by one and flashes the well's frame",
    "Passing your best mid-run says NEW BEST with a soft coin, and Best turns gold and follows your score",
    "Combo and back-to-back show as chips that light up, with B2B ready in gold after a quad, and Freeze and Slow as bars that drain",
    "A used Hold goes grey with a small lock, and in Power mode Next and Hold name the power-up and show its letter",
    "The controls card shows your pad's buttons once you use the pad, now names M and - / = too, and Settings has a Controls card row to hide it",
    NULL,
};

static const char *const kNotes_2_2_0[] = {
    "A new main menu: the title drops in as the game starts, faint pieces land on a skyline along the bottom, and Your bests shows Marathon, Sprint, Ultra and Power on the board you last set up",
    "A new Play setup: the five modes in a list with what each one is, a board picker, a preview of the board at its real shape, and your best and the world best on it",
    "Play setup opens on the mode you played last, so Enter, Enter plays it again. Left/Right, LB/RB or [ and ] change the board, and Scores for this board is one press away",
    "Width and Height always work, Enter on them starts too, and holding them repeats and speeds up after a second. A size that matches a preset picks that preset",
    "The menus work with the mouse: point to light a button, click to press it, and the wheel scrolls Updates and moves through Settings",
    "Holding Up or Down (W, S or the d-pad) keeps moving in every menu, and each move makes a soft tick",
    "The focus bar glides from item to item, screens slide in, and a pressed button flashes",
    "A new Motion setting, Full or Reduced: Reduced turns the slides, the glide and the title drop into plain cuts and halves the particles",
    "Hints show your pad's buttons once you use the pad, and your keys again when you type",
    "Esc, pad B or the mouse's back button go back to the screen you came from. On the main menu the first press moves to Quit and a second one quits",
    "Updates scrolls with the wheel, and LB/RB or Page Up and Page Down jump a page",
    "The achievement toast on the menu slides back out instead of vanishing",
    NULL,
};

static const char *const kNotes_2_1_0[] = {
    "Esc on Play setup goes back to the menu instead of closing the game, and Space no longer drops the first piece as it starts a run",
    "Enter on any row of Play setup starts the run, Width and Height included",
    "Esc or pad B pauses first and a second press leaves the run; pad View restarts only from pause",
    "The game pauses when its window loses focus",
    "Sprint is ranked by time on a fresh Sprint 40 board, fastest first, finished runs only; your old Sprint scores are kept as Sprint (old)",
    "One Best per mode and board. The classic board's is the higher of your old best and the score table, and only classic Marathon runs change it",
    "Zen keeps its promise: a held piece that cannot come in is a fresh start, not game over",
    "Laser and Bomb pieces keep the lines they complete: power-ups fire after the clear",
    "Tapping rotate can no longer keep a landed piece alive for ever, and a piece that locks above the top ends the run",
    "Reduced flashing works here now: no white flashes, and power-up marks hold steady",
    "Banners, the pause card and the end card fit every board size, and effects stay inside the well",
    "The controls card, the pause card and How to play show your keys, and a key already in use swaps places with the one you set",
    "On the pad, B gets you out of Press a key (so do five seconds of nothing)",
    "M and - / = show a volume chip and are saved at once",
    "Scores: [ and ] or LB and RB change the board, pad Y changes the period, and Zen says why it has no table",
    "Custom board sizes keep their scores on this machine only",
    "A restart before anything has landed no longer counts as a run",
    "Finishing a Sprint or reaching Ultra's time up plays a cleared sting, one sound at a time",
    NULL,
};

static const char *const kNotes_2_0_0[] = {
    "Custom board sizes: pick Classic, Wide, Tall, Huge or Tiny, or set any width (6 to 24) and height (12 to 36); the board scales to fit",
    "A play-setup screen before every run, remembered from last time",
    "Five modes: Marathon, Sprint (40 lines), Ultra (two minutes), Zen (no game over) and Power",
    "Power mode: some pieces carry a Bomb, Laser, Freeze, Slow or Sweep on one block, and it fires when the piece lands",
    "Hold a piece with C, and a Hold box beside Next",
    "Combos and back-to-back quads score extra on every board except the classic marathon",
    "Scores are kept per mode and per board size; Left/Right on the Scores screen switches mode",
    "Chiptune music, a music volume, and READY and new-best jingles",
    NULL,
};

static const char *const kNotes_1_3_0[] = {
    "Cleared lines flash and burst into pixels; four at once gets a QUAD banner and a proper shake",
    "Hard drops land with a thud scaled to how far the piece fell, and kick up dust",
    "A piece blinks as it locks, and level-ups announce themselves on the board",
    "New Scores screen: this machine's top ten, or the world board (Up/Down switches)",
    "New look to match the Ghost Launcher arcade hall: indigo screen, violet light, and every piece of text at a clean pixel size",
    "Level progress shows as ten pips under the level; the volume setting is ten blocks",
    "The menu background is now falling tetrominoes instead of spinning squares",
    NULL,
};

static const char *const kNotes_1_2_0[] = {
    "Joined the Ghost Arcade score tables: finished runs are saved for Ghost Launcher to show",
    "Online leaderboard: scores upload in the background when the ghost-sync helper is installed",
    "Plays under the name you set in Ghost Launcher (press U there) instead of PLAYER",
    "A run you restart or leave part-way still counts for what it scored",
    NULL,
};

static const char *const kNotes_1_1_0[] = {
    "Renamed the project to Blockfall (previously used a placeholder name)",
    "Added a start menu with an animated falling-blocks background",
    "Added an in-game Settings screen: rebind any control live, adjust volume, mute",
    "Added this Updates screen",
    "Switched the UI to a pixelated retro font throughout",
    "Enlarged the window and reworked layout to fit the new font comfortably",
    "Redesigned the app icon",
    "Fixed pieces spawning partially above the visible playfield",
    NULL,
};

static const char *const kNotes_1_0_0[] = {
    "Initial release: 10x20 board, all seven tetrominoes, 7-bag randomizer",
    "Movement, soft/hard drop, rotation with wall kicks, lock delay",
    "Line clearing, scoring, leveling, increasing fall speed",
    "Ghost piece, next-piece preview, pause, restart, game over",
    "Procedurally generated sound effects with volume/mute controls",
    "High score persistence and a desktop launcher",
    NULL,
};

static const UiChangelogEntry kChangelog[] = {
    {"3.6.1", "2026-10-21", kNotes_3_6_1},
    {"3.6.0", "2026-10-21", kNotes_3_6_0},
    {"3.5.0", "2026-10-20", kNotes_3_5_0},
    {"3.4.0", "2026-10-20", kNotes_3_4_0},
    {"3.3.0", "2026-10-20", kNotes_3_3_0},
    {"3.2.0", "2026-10-20", kNotes_3_2_0},
    {"3.1.0", "2026-10-20", kNotes_3_1_0},
    {"3.0.0", "2026-10-14", kNotes_3_0_0},
    {"2.5.0", "2026-10-14", kNotes_2_5_0},
    {"2.4.0", "2026-10-14", kNotes_2_4_0},
    {"2.3.0", "2026-10-14", kNotes_2_3_0},
    {"2.2.0", "2026-10-14", kNotes_2_2_0},
    {"2.1.0", "2026-10-14", kNotes_2_1_0},
    {"2.0.0", "2026-09-20", kNotes_2_0_0},
    {"1.3.0", "2026-09-19", kNotes_1_3_0},
    {"1.2.0", "2026-09-19", kNotes_1_2_0},
    {"1.1.0", "2026-09-11", kNotes_1_1_0},
    {"1.0.0", "2026-09-11", kNotes_1_0_0},
};
#define CHANGELOG_COUNT ((int)(sizeof(kChangelog) / sizeof(kChangelog[0])))

#endif
