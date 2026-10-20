#ifndef PLAYSTEP_H
#define PLAYSTEP_H

#include <stdbool.h>
#include "game.h"
#include "repeat.h"

/* One frame of a run, without raylib, so it can be tested headlessly:
 * auto-repeat, the moves in the order they have always been made, gravity,
 * and READY-GO. scr_play.c reads the keys and the pad into a PlayIntent
 * and plays the sounds; everything in between is here.
 *  - Auto-repeat as it always was by default: Left and Right act at once,
 *    again after 0.17 s, then every 0.035 s; soft drop every 0.03 s from the
 *    start. Settings > Feel changes the three (PlayHandling): a repeat speed
 *    of 0 sends the piece straight to the wall once the delay is over, and
 *    an Instant soft drop takes it straight down without locking it. It
 *    keeps time in every phase, paused too.
 *  - READY-GO (REWORK_PLAN.md, "Hold, Next and READY-GO in motion"): at the
 *    start of a run READY shows for 0.6 s while the run waits. The pieces
 *    are there, but nothing moves or falls and the Sprint and Ultra clocks
 *    do not run. A turn (either way) or a hold pressed during READY is made
 *    at GO, the turns in the order they were pressed;
 *    nothing else is kept, so a hard drop never happens twice, and a key
 *    held through READY acts as a fresh press at GO. GO then shows for
 *    0.4 s while play is already on. After a restart only GO shows and play
 *    begins at once. Settings can turn READY-GO off. Pausing during READY
 *    pauses READY with it.
 *  - The countdown after a pause (a Settings switch, off by default): 3, 2,
 *    1 over the board, 0.3 s each, with the piece in view. It waits as READY
 *    does (nothing moves or falls, no clock runs, a turn or a hold is kept
 *    for its end), then play goes on with no GO. */

#define PLAY_DAS_SECONDS 0.17f
#define PLAY_ARR_SECONDS 0.035f
#define PLAY_SOFT_DROP_SECONDS 0.03f
#define CUE_READY_SECONDS 0.6f
#define CUE_GO_SECONDS 0.4f
#define CUE_MAX_TURNS 3 /* turns kept from READY */
/* Soft drop speeds (persist.h's SOFT_*), seconds per row; Instant is 0. */
#define PLAY_SOFT_SLOW_SECONDS 0.06f
#define PLAY_SOFT_FAST_SECONDS 0.015f
#define CUE_COUNT_STEP 0.3f /* each of 3, 2, 1 */
#define CUE_COUNT_SECONDS (3.0f * CUE_COUNT_STEP)

typedef enum { CUE_NONE, CUE_READY, CUE_GO, CUE_COUNT } CueStage;

typedef struct {
    CueStage stage;
    float t;            /* seconds into the stage */
    int turns;          /* turns pressed during READY, made at GO */
    int turnDir[CUE_MAX_TURNS]; /* each one's way: +1 right, -1 left */
    bool hold;          /* a hold pressed during READY, made at GO */
    bool goSound;       /* GO began: the caller plays its jingle and clears this */
} StartCue;

typedef struct {
    bool left, right, softDrop;   /* held (Left and Right together already cancel) */
    bool rotate, hardDrop, hold;  /* pressed this frame */
    bool rotateLeft;              /* pressed this frame: a turn counter-clockwise */
} PlayIntent;

/* How Left, Right and the soft drop repeat (Settings > Feel). */
typedef struct {
    float delay;   /* auto-repeat delay, seconds (0.17) */
    float speed;   /* a step every so many seconds once it repeats (0.035); 0 = straight to the wall */
    float soft;    /* soft drop, seconds per row (0.03); 0 = Instant */
} PlayHandling;
/* Blockfall's own feel, as it always was. */
PlayHandling PlayHandling_Default(void);
/* From the settings' numbers: ms, ms and a SOFT_* speed. */
PlayHandling PlayHandling_From(int delayMs, int speedMs, int softSpeed);

typedef struct {
    bool moved;          /* a sideways move was made */
    bool moveRepeat;     /* ...by auto-repeat (the piece snaps rather than glides) */
    bool rotated;        /* a turn was made (at GO: one kept from READY) */
    bool rotateBlocked;  /* a turn was pressed and could not be made (an O has none to make) */
    bool held;           /* the piece went into Hold */
    bool waiting;        /* READY: the run waited this frame */
} PlayStepResult;

typedef struct {
    RepeatButton left, right, softDrop;
    StartCue cue;
    PlayHandling handling;
    bool handlingSet;   /* false (a zeroed driver): the default feel */
} PlayDriver;

/* Your Feel settings for this driver (any time; the next frame uses them). */
void PlayDriver_SetHandling(PlayDriver *d, PlayHandling h);

/* A run starts: READY then GO, or after a restart only GO, or with
 * READY-GO off neither. Auto-repeat carries on as it was. */
void PlayDriver_Start(PlayDriver *d, bool readyGo, bool restart);
/* After a pause: 3, 2, 1, then play goes on. A pause made during READY
 * goes back to READY instead (it already holds the run). */
void PlayDriver_Countdown(PlayDriver *d);
/* READY or the countdown is up and the run is waiting for it. */
bool PlayDriver_Waiting(const PlayDriver *d);
/* The number the countdown shows now: 3, 2 or 1 (0 when it is not on). */
int PlayDriver_CountNumber(const StartCue *c);
/* One frame, in any phase. speed: the F2 panel's game speed (1 normally). */
void Play_Step(PlayDriver *d, Game *g, const PlayIntent *in, float dt, float speed, PlayStepResult *out);

#endif
