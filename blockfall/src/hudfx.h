#ifndef HUDFX_H
#define HUDFX_H

#include <stdbool.h>
#include "game.h"
#include "layout.h"
#include "runrecord.h"

/* The play screen's lively parts, decided without raylib so they can be
 * tested headlessly; the drawing (panel.c, effects.c) only reads them. All
 * of it is looks only: the rules never read any of it.
 *  - Callouts: up to three stacked lines over the well (QUAD, BACK-TO-BACK,
 *    COMBO 3, LEVEL 4, BOMB, NEW BEST, FRESH START), all from one moment:
 *    the next lock that has words replaces them. They rise 8 px in 0.12 s,
 *    hold, and are gone at 0.9 s (pixel text never fades), together, so no
 *    line ever hops up or changes size while it shows. A single gets no
 *    word, only its popup. When more arrive at once than fit, the least
 *    important go.
 *  - Popups: "+1 350" rising 24 px from the cleared rows over 0.7 s, gold
 *    for 800 points and up.
 *  - The score counts up to its real value in 0.35 s (snapping while paused
 *    or once the run is over); a stat that changes nudges up 2 px for
 *    0.08 s.
 *  - Level up: the ten pips light one by one in 0.15 s and then empty, the
 *    well's frame flashes violet for 0.3 s and the grid brightens for 0.4 s.
 *  - Ultra's countdown turns gold at 30 s and red at 10 s, ticking each
 *    second from there.
 * Reduced motion (motion = false) keeps everything still: no rise, no
 * nudge, the pips light at once. */

#define CALLOUT_SECONDS 0.9f
#define CALLOUT_RISE_SECONDS 0.12f
#define POPUP_SECONDS 0.7f
#define POPUP_GOLD_POINTS 800
#define COUNTUP_SECONDS 0.35f
#define NUDGE_SECONDS 0.08f
#define NUDGE_PX 2
#define LEVELUP_PIPS_SECONDS 0.15f
#define LEVELUP_FRAME_SECONDS 0.3f
#define LEVELUP_GRID_SECONDS 0.4f
#define ULTRA_GOLD_SECONDS 30.0f
#define ULTRA_RED_SECONDS 10.0f
#define HUD_POPUPS 4

/* Least important first: of more than three at once, the later kinds show. */
typedef enum { CALL_LEVEL, CALL_POWER, CALL_COMBO, CALL_B2B, CALL_CLEAR, CALL_NEW_BEST, CALL_FRESH } CalloutKind;
typedef enum { TINT_LIGHT, TINT_GOLD, TINT_POWER } CalloutTint;

typedef struct {
    CalloutKind kind;
    char text[24], shortText[12];
    CalloutTint tint;
    PowerUp power;   /* for TINT_POWER */
    float age;
    int seq;         /* arrival order; those arriving together in display order */
} Callout;

typedef struct {
    char text[32];
    bool gold;
    float col, row;  /* where it starts: board column and visible row (fractions: centres) */
    float age;
} Popup;

typedef struct {
    Callout call[CALLOUT_MAX];
    int calls, seq;
    Popup pop[HUD_POPUPS];
    int pops;
    long shown, from, target; /* the score as drawn, counting up */
    float countT;
    float nudge[STAT_KINDS];  /* seconds since each kind of stat changed */
    float levelAge;           /* seconds since the last level up */
    long lastScore;
    int lastSecond;           /* Ultra: the last whole second shown */
} HudFx;

/* A new run (or a restart): nothing showing, the score as it is. */
void HudFx_Reset(HudFx *h, const Game *g);
/* This frame's events (call it BEFORE Game_ConsumeFrameFlags). passedBest:
 * the run just passed the best it started with. */
void HudFx_React(HudFx *h, const Game *g, bool passedBest);
/* Moves time on. playing: the run is on (paused or over, the score snaps). */
void HudFx_Tick(HudFx *h, float dt, bool playing);

/* The callouts showing, top to bottom; returns how many. */
int HudFx_Callouts(const HudFx *h, const Callout **out);
/* The biggest size a callout may take: the most important line showing is
 * the headline (32 px where it fits), every other line 16 px at most. */
int HudFx_CalloutMaxSize(const HudFx *h, const Callout *c);
/* How far a callout has risen (0..CALLOUT_RISE). */
int HudFx_CalloutRise(const Callout *c, bool motion);
/* How far a popup has risen (0..POPUP_RISE). */
int HudFx_PopupRise(const Popup *p, bool motion);
/* 2 while a stat kind's nudge lasts, else 0. */
int HudFx_Nudge(const HudFx *h, StatKind k, bool motion);
/* The pips to light: `into` (lines into the level), or the level-up run. */
int HudFx_PipsLit(const HudFx *h, int into, bool motion);
/* 1 at a level up, fading to 0: the well's frame and its grid. */
float HudFx_FrameFlash(const HudFx *h);
float HudFx_GridGlow(const HudFx *h);

/* Ultra's countdown colour. */
typedef enum { ULTRA_NORMAL, ULTRA_GOLD, ULTRA_RED } UltraTint;
UltraTint Hud_UltraTint(float timeLeft);
/* The whole seconds Ultra's countdown shows ("1:59" is 119). */
int Hud_UltraSeconds(float timeLeft);
/* The first time a run passes the best it started with (a NEW BEST callout
 * and a soft coin sound): points boards only, never Zen (it keeps no scores)
 * or a tuned playtest (it is never recorded), and only over a real best. */
bool Hud_PassesBest(long score, long best, RecordUnit unit, GameMode mode, bool recordable, bool alreadyPassed);

#endif
