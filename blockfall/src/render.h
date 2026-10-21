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
#include "danger.h"
#include "splits.h"

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
    int queue;          /* pieces the Next box shows, 1 to QUEUE_MAX (Settings > Game, 3.2.0); 0 or 1: the one large piece alone */
    int undoLeft;       /* the Practice room (3.5.0): pieces that can still be taken back (the panel's Undos left) */
    const Results *end; /* the run has ended: the end show on the board (results.h); NULL while it is on */
    const Danger *danger; /* the danger warning: the red frame, the stripes, the next piece's outline (danger.h); NULL: none */
    const Splits *splits; /* Sprint's splits against your best pace (splits.h); NULL: none */
    float intensity;    /* the music's intensity this frame (danger.h), kept for the harness */
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
    /* The power-ups' shows (powerfx.h, effects.c) */
    char power[32];     /* none, bomb-blink, bomb-shock, laser-charge, laser-beam, thaw */
    float frost;        /* the Freeze's frost, 0 to 1 */
    int afterimages;    /* the Slow's afterimages drawn */
    float sweepBar;     /* the Sweep's bar, in cells from the left wall (-1: none) */
    /* Danger and Sprint's race (danger.h, splits.h) */
    bool dangerOn;
    float dangerFrame;  /* the frame's redness as drawn, 0 to 1 */
    int freeRows;
    bool nextBlocked;
    int queue;          /* small pieces drawn after Next (3.2.0) */
    char queuePieces[32]; /* them by name, '|'-joined ("T|O|S|Z"), "" for none */
    char timer[16];     /* Sprint's clock as drawn ("1:12.40") */
    char split[24];     /* the split line as drawn ("10: -1.4 s"), or "" */
    bool splitGold, linesGold;
    /* Dig (3.4.0) */
    float riseFill;     /* the "Next row" bar's fill as drawn, 0 to 1 */
    bool riseWarn;      /* ...and it was red (the wait's last quarter) */
    int riseDrop;       /* px the stack was drawn below its place, easing up after a rise */
} RenderInspect;
void Render_Inspect(RenderInspect *out);

/* The menus are screens of their own (screens.h); the blocks they draw come
 * from here too (blockdraw.h). */

#endif
