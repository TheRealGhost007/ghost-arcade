#ifndef HOWTODEMO_H
#define HOWTODEMO_H

#include <stdbool.h>
#include "game.h"

/* How to play's moving examples (REWORK_PLAN.md, "How to play with moving
 * examples"): each page has a small well running a looping demo on the
 * real rules (game.c), from a short script of presses, so what it shows is
 * what the game does. No raylib: the tests run every demo and check it
 * clears the rows its page says it does.
 *
 *   Basics         a T slides, turns both ways, soft-drops a little and
 *                  hard-drops into a gap: a single.
 *   Hold and next  an S goes into Hold, the I that was next clears a row,
 *                  then Hold swaps the S back in.
 *   Scoring        a quad (800), then a back-to-back quad (1 200 and a
 *                  combo's 50).
 *   Modes          the Basics single in the mode picked (its own number
 *                  shown under the well: lines left, time left...); Dig
 *                  (3.4.0) instead stands an I in the gaps of four garbage
 *                  rows: a quad, 800 and 400 for the rows dug.
 *   Power-ups      the power-up picked fires on the stack. */

typedef enum { HOWTO_BASICS, HOWTO_HOLD, HOWTO_SCORING, HOWTO_MODES, HOWTO_POWER, HOWTO_PAGES } HowToPage;
const char *HowTo_PageName(int page);

#define DEMO_W 8
#define DEMO_H 12

/* What a demo presses: the page's captions light that key's cap. */
typedef enum { DOP_NONE, DOP_LEFT, DOP_RIGHT, DOP_TURN, DOP_TURN_LEFT, DOP_SOFT, DOP_HARD, DOP_HOLD } DemoPress;

#define DEMO_MAX_CLEARS 8

typedef struct {
    Game game;
    int page, variant;    /* variant: Modes' mode, Power-ups' power-up */
    int step;             /* the script's next step */
    float wait;           /* seconds until it */
    float t;              /* seconds into this loop */
    int loops;            /* loops played */
    DemoPress press;      /* the key it pressed last */
    float pressAge;       /* seconds since */
    int clears[DEMO_MAX_CLEARS]; /* rows each clearing lock cleared, this loop */
    long gained[DEMO_MAX_CLEARS]; /* the points each of them paid for its rows (drop points aside) */
    int clearCount;
    int filledBefore, filledAfter; /* blocks on the board before and after the last lock (a power-up's work) */
    PowerUp fired;        /* the power-up the last lock fired */
    char callout[40];     /* over the well: "QUAD +800" */
    float calloutAge;
    bool ended;           /* the script is done; the loop waits, then starts over */
} HowToDemo;

/* variant: a GameMode for HOWTO_MODES, a PowerUp for HOWTO_POWER. */
void HowToDemo_Start(HowToDemo *d, int page, int variant);
/* One frame: the script's presses, then the rules (gravity, lock delay,
 * clocks). It loops by itself. */
void HowToDemo_Tick(HowToDemo *d, float dt);

/* What a page's demo says it does (its caption), for the tests: the rows
 * each clear takes, in order, and the points they pay. */
typedef struct {
    int clears[4];
    long gained[4];
    int count;
} DemoClaim;
DemoClaim HowToDemo_Claim(int page, int variant);

#endif
