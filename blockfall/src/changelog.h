#ifndef CHANGELOG_H
#define CHANGELOG_H

#include <stddef.h>

/* Static changelog data for the in-game Updates screen. Header-only since
 * it's only ever included by render.c; add a new entry at the top of
 * kChangelog (index 0 = newest) when bumping BLOCKFALL_VERSION. */

#include "ui.h"

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
    {"2.0.0", "2026-09-20", kNotes_2_0_0},
    {"1.3.0", "2026-09-19", kNotes_1_3_0},
    {"1.2.0", "2026-09-19", kNotes_1_2_0},
    {"1.1.0", "2026-09-11", kNotes_1_1_0},
    {"1.0.0", "2026-09-11", kNotes_1_0_0},
};
#define CHANGELOG_COUNT ((int)(sizeof(kChangelog) / sizeof(kChangelog[0])))

#endif
