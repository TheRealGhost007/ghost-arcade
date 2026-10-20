#ifndef RENDER_H
#define RENDER_H

#include "game.h"
#include "persist.h"
#include "controls.h"
#include "runrecord.h"
#include "layout.h"
#include "playstep.h"
#include "results.h"
#include "wipe.h"

#define CELL_SIZE 30
/* Same window as every Ghost Arcade game. The extra width/height beyond the
 * fixed 300x600 board goes to chrome (side panel, menus); the pixel font
 * (Press Start 2P) runs much wider per character than a normal UI font. */
#define WINDOW_WIDTH 620
#define WINDOW_HEIGHT 720

/* What the play screen shows besides the rules' own state. */
typedef struct {
    ControlWords keys;  /* your keys, for the controls card and the hints */
    bool showFps;
    RecordUnit unit;    /* the board ranks points, or Sprint times */
    long best;          /* this machine's best on the board when the run began; 0 = none yet */
    bool newBest;       /* the run that just ended beat it */
    bool focusLost;     /* paused because the window lost focus */
    float chipTime;     /* the volume chip shows while this is above 0 (M, - and =) */
    bool soundOn;
    int volume;         /* effects volume, 0-100 */
    unsigned run;       /* changes with every run and restart: the panel's count-up and callouts start over */
    bool padHints;      /* the pad was used last: the controls card shows its buttons */
    bool showControls;  /* the controls card (Settings can hide it) */
    bool passedBest;    /* this run passed the best it started with: Best turns gold */
    bool justPassedBest;/* ...on this frame (a NEW BEST callout) */
    bool moveRepeat;    /* this frame's sideways move was auto-repeat: the piece snaps (feel.h) */
    StartCue cue;       /* READY, GO or the countdown over the well (playstep.h) */
    int ghost;          /* GHOST_OFF, GHOST_OUTLINE or GHOST_SOLID (Settings > Look, the pause menu's quick panel) */
    int grid;           /* GRID_ON, GRID_FAINT or GRID_OFF (Settings > Look) */
    const Results *end; /* the run has ended: the end show on the board (results.h); NULL while it is on */
    /* Drawn over the play screen inside its frame, under the volume chip:
     * the pause menu or the results card (scr_play.c). */
    void (*overlay)(void *ctx);
    void *overlayCtx;
    const Wipe *wipe;   /* the block wipe into the run, drawn over everything */
} RenderView;

/* Draws one full gameplay frame (owns BeginDrawing/EndDrawing). Call it
 * BEFORE Game_ConsumeFrameFlags(): it reads the one-frame lock/clear events
 * to start its flashes, shake, particles, callouts and popups. Every effect
 * honours the shared Screen shake and Reduced flashing settings and
 * Blockfall's Motion setting, and stays inside the well. The pause menu and
 * the results card come in through view->overlay. */
void Render_Frame(const Game *g, const RenderView *view);

/* What the last frame showed, for the screenshot harness. */
typedef struct {
    PlayLayout layout;
    long shownScore;    /* the score as drawn (counting up) */
    int callouts;
    char callout[CALLOUT_MAX][24]; /* top to bottom */
    int popups;
    char popup[32];     /* the newest */
    /* How play moved (feel.h) */
    int clearStage;     /* ClearStage */
    int clearRows;      /* the rows the last clear replay is for */
    float clearTotal;   /* its length, seconds */
    int lift;           /* the most a row is drawn above its place (negative: settling below) */
    int pieceDX, pieceDY; /* px the piece is drawn from where it is */
    int pop;            /* the turn's inset, px */
    float fill;         /* how filled in the piece is as it comes in */
    bool ghost, flight, slide, holdReady, squash, smear, impact, bevel, streak, quadFrame;
    int settle;         /* px */
    int bits;           /* the well's pixels in flight */
} RenderInspect;
void Render_Inspect(RenderInspect *out);

/* The menus are screens of their own (screens.h); the blocks they draw come
 * from here too (blockdraw.h). */

#endif
