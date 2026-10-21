#ifndef DANGER_H
#define DANGER_H

#include <stdbool.h>
#include "game.h"

/* The danger warning and the music that follows the board (REWORK_PLAN.md,
 * "Danger warning and music that follows the board"), decided without
 * raylib so it can be tested headlessly; well.c draws it, scr_play.c hands
 * the warning to soundplan.h and the intensity to the shared music. All of
 * it is looks and sound only: the rules never read any of it.
 *  - Danger: the stack is within DANGER_ON_ROWS of the top of the visible
 *    well. It only eases off once DANGER_OFF_ROWS are clear again
 *    (hysteresis), so a clear at the top never makes it flicker. Zen is
 *    never in danger (it has no losing), and a paused or finished run holds.
 *  - The well's frame eases to red over 0.4 s and pulses gently (steady
 *    with Reduced flashing); faint red stripes mark the top three rows; the
 *    next piece's outline shows in red where it would come in when it would
 *    not fit there.
 *  - A warning sound plays once when danger starts, and never more often
 *    than every 8 s however often the stack dips in and out.
 *  - Music intensity (0..1; the shared music brings the lead in above 0.35
 *    and the snare above 0.5): Marathon and Power follow the level AND the
 *    stack's height, so the snare comes in as the stack climbs and drops
 *    out when you dig out; Sprint follows the lines cleared; Ultra follows
 *    the time, full in its last ten seconds; Zen stays calm; danger adds a
 *    little to all but Zen.
 *  - Ultra's last five seconds each tick; Sprint's last five lines turn the
 *    counter gold (hudfx.h colours the rest). */

#define DANGER_ON_ROWS 4           /* in danger once the stack is this close to the top... */
#define DANGER_OFF_ROWS 7          /* ...and out of it only once this many rows are clear */
#define DANGER_EASE_SECONDS 0.4f   /* the frame eases to red (and back) */
#define DANGER_PULSE_HZ 1.25f
#define DANGER_PULSE_MIN 0.55f     /* the pulse swings between this and 1 */
#define DANGER_STRIPE_ROWS 3
#define DANGER_WARN_GAP_SECONDS 8.0f
#define DANGER_INTENSITY_BONUS 0.15f
#define INTENSITY_ZEN 0.45f        /* the lead, never the snare */
#define INTENSITY_FLOOR 0.32f      /* an empty well at level 1: bass and arpeggio alone */
#define INTENSITY_LEVEL_SHARE 0.34f
#define INTENSITY_STACK_SHARE 0.34f
#define INTENSITY_FULL_LEVEL 10    /* the level's share is full from here */
#define ULTRA_TICK_SECONDS 5       /* each of Ultra's last five seconds ticks */
#define SPRINT_GOLD_LINES 5        /* Sprint's counter is gold in its last five lines */

typedef struct {
    bool on;           /* in danger */
    float ease;        /* 0..1: how far the frame has eased to red */
    float t;           /* seconds in danger (the pulse) */
    float sinceWarn;   /* seconds since the warning sounded (large: never) */
    bool warn;         /* this frame asks for the warning sound */
    int freeRows;      /* visible rows clear above the stack, as of the last update */
    bool nextBlocked;  /* the next piece would not fit where pieces come in */
} Danger;

/* A new run or a restart: no danger, nothing remembered (the first danger
 * may warn at once). */
void Danger_Reset(Danger *d);
/* One frame. Only a run in play moves it (paused or over, it holds, and
 * nothing warns); Zen never enters danger. */
void Danger_Update(Danger *d, const Game *g, float dt);
/* Visible rows clear above the stack's top: the board's height when it is
 * empty, 0 when a block sits on the top row. */
int Danger_FreeRows(const Board *b);
/* The next piece could not come in where pieces do (a spawn, unturned). */
bool Danger_NextBlocked(const Game *g);
/* Danger on, off, or holding where it was, for `freeRows` (the hysteresis). */
bool Danger_Hysteresis(bool was, int freeRows);
/* The frame's redness this frame, 0..1: the ease, pulsing with time
 * (steady with Reduced flashing: `calm`). */
float Danger_Frame(const Danger *d, bool calm);
/* The stripes' and the outline's strength, 0..1 (the ease alone). */
float Danger_Stripes(const Danger *d);

/* The music's intensity for this frame of a run. */
float Danger_Intensity(const Game *g, int freeRows, bool danger);
/* Ultra: the whole second shown changed to one of the last five (5 to 1). */
bool Danger_UltraTick(int prevSecond, int second);
/* Sprint: the lines-left counter is gold (the last five lines, not at 0). */
bool Danger_SprintGold(int linesLeft);

#endif
