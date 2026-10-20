#ifndef BACKDROP_H
#define BACKDROP_H

#include <stdbool.h>
#include <stdint.h>
#include "rng.h"
#include "tetromino.h"

/* The main menu's living backdrop: faint pieces fall behind the text and
 * land on a low skyline along the bottom of the screen. A full row flashes
 * and clears the way it does in play; a skyline grown too tall is swept
 * away. Seeded, so screenshots repeat exactly. No raylib (scr_menu.c draws
 * it). */

#define SKY_COLS 38
#define SKY_ROWS 5        /* the tallest it may grow before it is swept away */
#define SKY_GRID_ROWS 8   /* room above that for the piece that went too high */
#define SKY_CELL 16
#define SKY_X0 6          /* (620 - 38 x 16) / 2 */
#define SKY_BOTTOM 648    /* the skyline's bottom edge; the hint line is below it */
#define SKY_PIECES 6
#define SKY_LOCK_BLINK 0.14f
#define SKY_CLEAR_FLASH 0.30f
#define SKY_SWEEP 0.45f

typedef struct {
    PieceType type;
    int rot, col;
    float y;      /* the top of its 4x4 box, px */
    float speed;  /* px per second */
    float wait;   /* seconds before it starts to fall */
} SkyPiece;

typedef struct {
    int8_t cells[SKY_GRID_ROWS][SKY_COLS]; /* -1 empty, else the piece type; row 0 is the bottom */
    float blink[SKY_GRID_ROWS][SKY_COLS];  /* a cell that just landed glows for a moment */
    bool clearing[SKY_GRID_ROWS];          /* full rows flashing before they go */
    float clearT;                          /* time left on that flash */
    float sweepT;                          /* time left on a sweep (0 = none) */
    SkyPiece p[SKY_PIECES];
    Rng rng;
    int landed, clears, sweeps;            /* counted, for the tests */
} Skyline;

void Sky_Init(Skyline *s, uint64_t seed);
void Sky_Step(Skyline *s, float dt);

/* A grid cell's top-left on screen. */
int Sky_CellX(int col);
int Sky_CellY(int row);
/* A column's height: the row above its highest block. */
int Sky_Height(const Skyline *s, int col);
/* The highest filled row plus one, over every column. */
int Sky_MaxHeight(const Skyline *s);
/* How far a sweep has gone, 0..1 (blocks fade as it runs). */
float Sky_SweepProgress(const Skyline *s);

#endif
