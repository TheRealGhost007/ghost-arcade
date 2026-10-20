#ifndef WIPE_H
#define WIPE_H

#include <stdbool.h>

/* The block wipe going into and out of a run (REWORK_PLAN.md, "Menu motion,
 * run wipes and menu sounds"): the new screen is drawn at once, and over it
 * a wall of blocks clears row by row from the top, each row flashing and
 * then zipping out from the middle to both sides, the way a line clears in
 * play: 0.25 s in all. Nothing waits for it; a press acts at once. With
 * Motion: Reduced there is none (a plain cut), and with Reduced flashing
 * the rows flash as a soft tint instead of white.
 *
 * The model is raylib-free (tested headlessly); Wipe_Draw, the only drawing,
 * lives in scr_common.c. */

#define WIPE_SECONDS 0.25f
#define WIPE_ROW_FLASH 0.04f
#define WIPE_ROW_ZIP 0.06f
#define WIPE_BLOCK 40
#define WIPE_ROWS 18 /* 720 / 40 */
#define WIPE_COLS 16 /* over 620 px: the outer two blocks are cut in half */
#define WIPE_X0 (-10)

typedef struct {
    bool on;
    float t; /* seconds since it began */
} Wipe;

/* A wipe begins (none with motion off). */
void Wipe_Start(Wipe *w, bool motion);
void Wipe_Tick(Wipe *w, float dt);
/* It is still covering part of the screen. */
bool Wipe_Active(const Wipe *w);

/* When row `row` (0 = the top) starts to clear. The last row is gone at
 * exactly WIPE_SECONDS. */
float Wipe_RowStart(int row);

typedef enum { WIPE_COVERED, WIPE_FLASH, WIPE_GONE } WipeCell;
/* One block of the wall now: still there, lit as its row clears, or gone. */
WipeCell Wipe_Cell(const Wipe *w, int row, int col);

/* Draws what is left of the wall over the frame (scr_common.c). */
void Wipe_Draw(const Wipe *w);

#endif
