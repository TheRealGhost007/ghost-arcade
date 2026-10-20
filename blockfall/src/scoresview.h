#ifndef SCORESVIEW_H
#define SCORESVIEW_H

#include <stdbool.h>
#include "ghostlink.h"
#include "ui.h"
#include "runrecord.h"

/* The table of the Scores browser (scr_scores.c draws the screen round it:
 * the tabs, the board picker, This machine | World, the periods and Play
 * this board): the medals, your rows lit, a run just put there blinking,
 * where you stand ("You are #4, 15 570 behind #3", a Sprint gap in
 * seconds), why a board is empty, whose table it is, and the classic best
 * from before the tables existed. Drawn inside a frame the caller owns. */
typedef struct {
    const char *modeLabel;   /* "Marathon", "Sprint 40", "Sprint (old)" */
    const char *boardLabel;  /* "Classic 10x20" */
    bool world;
    UiScorePeriod period;
    const GhostScore *entries;
    int count;
    const char *username;
    RecordUnit unit;
    const char *empty;       /* why there is nothing, or NULL for the usual words */
    const char *footnote;    /* the line about whose table this is, or NULL for the usual */
    const char *periodKey;   /* the key that changes the period, for the empty message ("T", "Y") */
    long oldBest;            /* > 0: the classic best from before the score table (save.dat), above its top */
    /* The run just put here (from the results card): it blinks, then stays marked. */
    bool blink;
    long blinkValue;
    const char *blinkDate;
    bool blinkLit;           /* this frame it is lit */
    int blinkRow;            /* set by the drawing: the row it found, or -1 */
} ScoresViewInfo;

#define SCORES_ROW_Y 232
#define SCORES_ROW_PITCH 32
void ScoresView_Draw(ScoresViewInfo *v);

#endif
