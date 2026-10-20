#ifndef FEEL_H
#define FEEL_H

#include <stdbool.h>
#include "board.h"
#include "game.h"

/* How play feels (REWORK_PLAN.md phase 4), decided without raylib so it can
 * be tested headlessly; well.c, effects.c and panel.c only draw it. All of
 * it is looks only: the rules never read any of it, and nothing here
 * changes when a piece falls, locks or scores.
 *  - Line clears replay from a copy of the board as the piece was stamped
 *    (Game.lockBoard): the full rows flash, then their cells zip out from
 *    where the piece landed towards both walls (a quad bursts all four rows
 *    at once), then everything above drops into the gap and settles 2 px.
 *    The live board takes over at the end, row for row the same picture, so
 *    nothing pops. 0.24 s in all; 0.18 s at levels 10-14 and 0.12 s from
 *    15, so the picture never trails the stack for long. A new lock ends the
 *    one before at once. The ghost hides until the rows have settled.
 *  - The falling piece slides in half a cell from the top as it comes in
 *    (0.08 s). A single tap and a gravity step glide over 0.04 s;
 *    auto-repeat and soft drop snap. It is never drawn more than half a
 *    cell from where it really is. A turn pops 2 px for two frames. A
 *    piece on the ground darkens as its lock delay runs out.
 *  - Landing: a hard drop leaves a smear from where it started and squashes
 *    for two frames; a natural lock settles 1 px with a brighter bevel.
 *    Soft drop leaves short streaks.
 *  - Hold: the outgoing piece flies into the Hold box (0.12 s); the box's
 *    edge flashes once when hold is ready again. Next: the old preview
 *    slides out as the new one slides in (0.10 s).
 * Motion: Reduced (FeelFrame.motion false) makes every glide, slide, flight,
 * fall, smear and squash a plain cut, and a clear only a short flash before
 * the rows snap down. Piece motion: Snap (FeelFrame.glide false) keeps the
 * piece moving cell by cell. */

/* Line clears */
#define CLEAR_FLASH_SECONDS 0.06f
#define CLEAR_ZIP_SECONDS 0.08f
#define CLEAR_DROP_SECONDS 0.10f
#define QUAD_FLASH_SECONDS 0.10f
#define QUAD_GAP_SECONDS 0.04f     /* a quad bursts at once; the gap holds before the fall */
#define CLEAR_CUT_FLASH_SECONDS 0.08f /* Reduced motion */
#define CLEAR_FALL_PART 0.75f      /* of the drop: the fall; the rest is the settle */
#define CLEAR_SETTLE_PX 2
#define QUAD_FRAME_SECONDS 0.25f   /* a quad flashes the well's frame gold */
/* The falling piece */
#define GLIDE_SECONDS 0.04f
#define GLIDE_MAX_CELLS 0.5f
#define SPAWN_SECONDS 0.08f
#define SPAWN_CELLS 0.5f
#define POP_SECONDS (2.5f / 60.0f)  /* two frames drawn */
#define POP_PX 2
#define LOCK_SHADE_MAX 0.30f
#define STREAK_SECONDS 0.08f
/* Landing */
#define SMEAR_SECONDS 0.12f
#define SQUASH_SECONDS (2.5f / 60.0f) /* two frames drawn */
#define SQUASH_PX 2
#define SETTLE_SECONDS 0.06f
#define SETTLE_PX 1
#define BEVEL_SECONDS 0.10f
#define IMPACT_SECONDS 0.14f       /* a hard drop's landing flash (white, or a tint with Reduced flashing) */
/* Hold and Next */
#define HOLD_FLIGHT_SECONDS 0.12f
#define HOLD_READY_SECONDS 0.15f
#define NEXT_SLIDE_SECONDS 0.10f

typedef enum { CLEAR_NONE, CLEAR_FLASH, CLEAR_ZIP, CLEAR_DROP } ClearStage;
typedef struct { float flash, zip, drop; } ClearTimes;

/* What a frame's input says about the motion this frame. */
typedef struct {
    bool motion;     /* Motion: Full */
    bool glide;      /* Piece motion: Smooth (and Motion: Full) */
    bool moveRepeat; /* this frame's sideways move came from auto-repeat */
} FeelFrame;

typedef struct {
    bool on;              /* started by the last lock (it may have ended: see Feel_ClearStage) */
    Board before;         /* the board as the piece was stamped, before the clear and any power-up */
    int rows[4], count;   /* the full rows in `before` (rows count the hidden ones), bottom first */
    int shift;            /* rows a Sweep then moved everything down */
    int from[BOARD_MAX_TOTAL_H]; /* each live row's row in `before`; -1: it came in empty at the top */
    int lockCol;          /* where the zip starts: the middle of the piece that landed */
    PieceType lockType;
    bool quad, b2b;
    ClearTimes times;
    float t, prevT;       /* seconds since the lock, now and a frame ago */
} ClearReplay;

typedef struct {
    bool on;              /* a piece is in play */
    PieceType type;
    int rot, px, py;      /* where the rules had it last frame */
    float ox0, oy0;       /* the glide: drawn = where it is + offset (cells), easing to 0 */
    float glideT;
    float spawnT;         /* since it came in */
    bool slid;            /* it slides in (Smooth) */
    float popT;           /* since it turned */
    float streakT;        /* since the last soft-drop step */
} PieceFeel;

typedef struct {
    bool on;
    PieceType type;
    int rot, x;
    int y;                /* where it landed, on the board drawn while a clear replays (`before`) */
    int liveY;            /* ...and on the live board (a Sweep moves it down 3) */
    int fromY;            /* a hard drop: the row it started from */
    bool hard;            /* hard drop */
    bool replayed;        /* its lock started a clear replay */
    bool motion;
    float t;
} LandFeel;

typedef struct {
    ClearReplay clear;
    PieceFeel piece;
    LandFeel land;
    float quadFrameT;
    /* Hold: the piece flying into the box */
    bool flightOn;
    float flightT;
    PieceType flightType;
    int flightRot;
    float flightX, flightY;  /* where it was drawn, in cells (rows count the hidden ones) */
    PowerUp flightPower;
    int flightPowerCell;
    float holdReadyT;
    /* Next: the old preview sliding out */
    bool slideOn;
    float slideT;
    PieceType slideOld;
    PowerUp slideOldPower;
    int slideOldCell;
    /* last frame */
    PieceType lastNext;
    PowerUp lastNextPower;
    int lastNextCell;
    int lastHold;
    bool lastHoldUsed;
} Feel;

/* A new run or a restart: nothing left over, the first piece slides in. */
void Feel_Reset(Feel *f, const Game *g, bool glide);
/* This frame's events (call it BEFORE Game_ConsumeFrameFlags). */
void Feel_React(Feel *f, const Game *g, const FeelFrame *in);
void Feel_Tick(Feel *f, float dt);

/* --- line clears */
/* The stages' lengths for a clear of `count` rows at a level. */
ClearTimes Feel_ClearTimes(int count, int level, bool motion);
float Feel_ClearTotal(ClearTimes t);
/* Where each row of the board after a clear (and a Sweep's `shift`) came
 * from in the board before it: out[row] for every row of totalH, -1 for a
 * row that came in empty at the top. */
void Feel_RowSources(const int *rows, int count, int shift, int totalH, int *out);
ClearStage Feel_ClearStage(const Feel *f);
/* A cell of a cleared row has zipped out by time t. */
bool Feel_ClearCellGone(const ClearReplay *r, int col, float t);
/* How far above its place a live row is drawn during the fall, in px (a
 * little below it, negative, while it settles; 0 once it is home). */
int Feel_RowLift(const Feel *f, int liveRow, int cell);
/* The gold flash of the well's frame after a quad, 1 fading to 0. */
float Feel_QuadFrame(const Feel *f);
/* The ghost shows (not while cleared rows are still falling). */
bool Feel_ShowGhost(const Feel *f);

/* The clear sound's pitch: a single 1, a double 1.12, a triple 1.26 (each
 * rings higher), a quad its own sound at 1, a back-to-back quad two
 * semitones brighter. */
float Feel_ClearPitch(int count, bool b2b);
/* A turn that cannot be made: the turn's sound, low and dull. */
#define ROTATE_BLOCKED_PITCH 0.6f

/* --- the falling piece */
/* How far from where it is the piece is drawn, in cells (x, y), within half
 * a cell. */
void Feel_PieceOffset(const Feel *f, float *dx, float *dy);
/* How filled in the piece is as it comes in, 0.35 to 1. */
float Feel_SpawnFill(const Feel *f);
/* The 2 px a turn pops the blocks in by, for two frames. */
int Feel_PopInset(const Feel *f);
/* How much a piece on the ground has darkened as its lock delay runs out (0
 * to LOCK_SHADE_MAX); 0 again once a move gives the time back. */
float Feel_LockShade(const Game *g);
/* The soft drop's streaks, 1 fading to 0. */
float Feel_Streak(const Feel *f);
/* The rows (counting the hidden ones) the piece would complete where its
 * ghost is; returns how many. */
int Feel_GhostRows(const Game *g, int ghostY, int out[4]);

/* --- landing */
bool Feel_Squash(const Feel *f);
int Feel_Settle(const Feel *f);
float Feel_Bevel(const Feel *f);   /* a natural lock's brighter bevel, 1 to 0 */
float Feel_Impact(const Feel *f);  /* a hard drop's landing flash, 1 to 0 */
float Feel_Smear(const Feel *f);   /* a hard drop's smear, 1 to 0 */

/* --- Hold and Next */
/* The hold flight is on: k is how far it has got (eased, 0 to 1). */
bool Feel_HoldFlight(const Feel *f, float *k);
/* The Hold box's ready flash, 1 fading to 0. */
float Feel_HoldReady(const Feel *f);
/* The Next box is sliding: k is how far (eased, 0 to 1). */
bool Feel_NextSlide(const Feel *f, float *k);

#endif
