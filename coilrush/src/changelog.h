#ifndef CHANGELOG_H
#define CHANGELOG_H

#include <stddef.h>

/* Static changelog data for the in-game Updates screen. Header-only since
 * it's only ever included by render.c; add a new entry at the top of
 * kChangelog (index 0 = newest) when bumping COILRUSH_VERSION. */

#include "ui.h"

static const char *const kNotes_1_2_0[] = {
    "The snake now glides between cells instead of jumping, in every mode, including across the open edges in Wrap",
    "Swallowed food travels down the body as a bulge",
    "Pickups burst into pixels when eaten, and the tongue flicks",
    "Dying shakes the board and bursts the snake segment by segment before the score card appears",
    "New look to match the Ghost Launcher arcade hall: indigo screen, and every piece of text at a clean pixel size",
    "Maze mode shows the food left on the stage as pips; the volume setting is ten blocks",
    "Plays under your arcade-wide Ghost Launcher name when Coilrush has no name of its own",
    NULL,
};

static const char *const kNotes_1_1_0[] = {
    "Online leaderboard: finished runs are uploaded in the background by the ghost-sync helper",
    "Scores screen now has LOCAL and GLOBAL views (Up/Down to switch)",
    "The global board shows each player's best run per mode, top 10",
    "Set your name in Ghost Launcher (press U on Coilrush) to appear as yourself instead of PLAYER",
    "Works offline exactly as before; turn syncing off with enabled=0 in ~/.config/ghost-launcher/online.conf",
    NULL,
};

static const char *const kNotes_1_0_0[] = {
    "Initial release: a 24x24 grid snake game with three modes",
    "CLASSIC: the edges are deadly. WRAP: the edges loop around",
    "MAZE: clear 8 food to advance through six wall layouts, faster each lap",
    "Buffered turns, so quick double-taps around corners always register",
    "Timed bonus pickups worth more the faster you reach them",
    "Per-mode high scores, plus a top-10 table shared with Ghost Launcher",
    "Uses your Ghost Launcher profile name on the score table",
    "Rebindable controls, procedurally generated sound effects",
    NULL,
};

static const UiChangelogEntry kChangelog[] = {
    {"1.2.0", "2026-09-19", kNotes_1_2_0},
    {"1.1.0", "2026-09-19", kNotes_1_1_0},
    {"1.0.0", "2026-09-19", kNotes_1_0_0},
};
#define CHANGELOG_COUNT ((int)(sizeof(kChangelog) / sizeof(kChangelog[0])))

#endif
