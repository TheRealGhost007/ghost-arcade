#ifndef CHANGELOG_H
#define CHANGELOG_H

#include <stddef.h>

/* Static changelog data for the in-game Updates screen. Header-only since
 * it's only ever included by render.c; add a new entry at the top of
 * kChangelog (index 0 = newest) when bumping BRICKBURST_VERSION. */

#include "ui.h"

static const char *const kNotes_1_1_0[] = {
    "Three new pickups: Laser (hold launch to shoot bricks from both edges of the paddle), Sticky (balls catch on the paddle until you launch them) and Shield (a floor that saves one ball, once)",
    "Combos: every five bricks you break without a ball touching the paddle adds one to the score multiplier, up to x5",
    "New sounds for shooting, catching and the shield; chiptune music and a music volume in Settings",
    "Scores screen shows medals and where you stand; T cycles All time, This week and Today",
    NULL,
};

static const char *const kNotes_1_0_0[] = {
    "Initial release: twelve layouts of bricks, then round again with faster balls",
    "Steer the ball with where it lands on the paddle; every paddle hit makes it a little quicker",
    "Two- and three-hit bricks, steel that never breaks, and bombs that take their neighbours (and each other)",
    "Five capsules to catch: three-way split, wide paddle, slow, fireball, and an extra paddle",
    "Keyboard or mouse: the paddle follows the mouse whenever it moves, click to launch",
    "Shares its score table and your name with Ghost Launcher, and posts to the online board",
    NULL,
};

static const UiChangelogEntry kChangelog[] = {
    {"1.1.0", "2026-09-20", kNotes_1_1_0},
    {"1.0.0", "2026-09-19", kNotes_1_0_0},
};
#define CHANGELOG_COUNT ((int)(sizeof(kChangelog) / sizeof(kChangelog[0])))

#endif
