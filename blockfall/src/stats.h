#ifndef STATS_H
#define STATS_H

#include <stdbool.h>
#include <stddef.h>
#include "game.h"

/* Lifetime stats (REWORK_PLAN.md, "Lifetime Stats screen", 3.3.0), raylib-
 * free so they are tested headlessly; scr_stats.c draws them.
 *  - For each mode: runs, lines, pieces and time played, the locks that
 *    cleared one to four rows (and which of those were T-spins), T-spins,
 *    perfect clears, power-ups and holds: the run's own counters
 *    (RunStats, the results card's) folded in as each run ends.
 *  - Records per mode, each with the day it was set: the best pieces per
 *    second (a run of at least STATS_PPS_MIN_PIECES pieces), the biggest
 *    combo and the most quads in a run.
 *  - A run counts here exactly when it counts for runs.log and the
 *    trophies (Record_Classify: a piece locked and it scored, never a
 *    tuned playtest), so the two never disagree.
 *  - Kept in Blockfall's own file, $XDG_DATA_HOME/blockfall/stats.dat
 *    (sections per mode, key=value lines, unknown lines ignored), written
 *    crash-safe; runs.log stays as it is for Ghost Launcher. Counting
 *    starts the day the first run ends after this version is installed
 *    (`since`); nothing is read back out of runs.log. */

#define STATS_FILE "stats.dat"
#define STATS_DATE_LEN 12
#define STATS_PPS_MIN_PIECES 20
/* The lines-by-clear bar's parts: single, double, triple and quad lines
 * not made by a T-spin, then the lines T-spins cleared. */
#define STATS_BAR_PARTS 5

typedef struct {
    long value;                /* 0: none yet */
    char date[STATS_DATE_LEN]; /* YYYY-MM-DD it was set */
} StatRecord;

typedef struct {
    long runs, lines, pieces;
    long centis;          /* time played, hundredths of a second */
    long clears[5];       /* locks that cleared 1, 2, 3 or 4 rows ([0] unused) */
    long tspinClears[5];  /* of those, the T-spin locks ([4] stays 0: a T clears three at most) */
    long tspins, perfects, powers, holds;
    StatRecord pps;       /* pieces per second, times 1000 */
    StatRecord combo;     /* counted as the COMBO callout counts: 1 is two clears in a row */
    StatRecord quads;
} ModeStats;

typedef struct {
    char since[STATS_DATE_LEN]; /* the day the first run counted; "" until one has */
    ModeStats mode[MODE_COUNT];
} LifeStats;

void Stats_Clear(LifeStats *s);
/* Reads the file; false (and *s cleared) when there is none yet. */
bool Stats_Load(LifeStats *s);
bool Stats_Save(const LifeStats *s);

/* The section name a mode is kept under: "marathon", "sprint", "ultra",
 * "zen", "power", "dig" (3.4.0). */
const char *Stats_ModeKey(GameMode mode);

/* A run's pieces per second times 1000: 0 for a run under
 * STATS_PPS_MIN_PIECES pieces, or with no time on the clock. */
long Stats_RunPps(const Game *g);
/* Folds a run that just ended into its mode (the counters, then the three
 * records against `date`). What Stats_Fold does once a run counts. */
void Stats_Add(ModeStats *m, const Game *g, const char *date);
/* The run ended: fold it in if it counts (Record_Classify). True when it
 * did; the first one sets `since`. */
bool Stats_Fold(LifeStats *s, const Game *g, bool tuned, const char *date);

/* Every mode together: the counters summed, each record the best of the
 * modes' (who[i], when given: the mode that set it, -1 for none; a tie
 * goes to the first mode in GameMode order). */
void Stats_Total(const LifeStats *s, ModeStats *out, int who[3]);
/* The bar's parts (see STATS_BAR_PARTS) and their sum, the mode's lines. */
void Stats_Bar(const ModeStats *m, long parts[STATS_BAR_PARTS]);
long Stats_BarTotal(const long parts[STATS_BAR_PARTS]);
/* The bar's words: "Single", "Double", "Triple", "Quad", "T-spin". */
const char *Stats_BarName(int part);

/* "45s", "12m 05s", "3h 05m". */
void Stats_FormatTime(long centis, char *out, size_t n);
/* "2.31" (hundredths, rounded). */
void Stats_FormatPps(long ppsMilli, char *out, size_t n);

#endif
