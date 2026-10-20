#ifndef CHANGELOG_H
#define CHANGELOG_H

#include <stddef.h>

/* Static changelog data for the in-game Updates screen. Header-only since
 * it's only ever included by render.c; add a new entry at the top of
 * kChangelog (index 0 = newest) when bumping BLOCKFALL_VERSION. */

#include "ui.h"

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
