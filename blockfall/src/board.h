#ifndef BOARD_H_INCLUDED
#define BOARD_H_INCLUDED

#include <stdbool.h>
#include "tetromino.h"

/* The classic board. Every board starts out this size (Board_Clear); a game
 * that wants another size calls Board_Init. */
#define BOARD_W 10
#define BOARD_H 20
/* Extra rows above the visible playfield give tall pieces (I) and spawn
 * rotations room without artificially triggering game-over. */
#define BOARD_HIDDEN 4
#define BOARD_TOTAL_H (BOARD_H + BOARD_HIDDEN)

/* Custom boards: any size within these limits. */
#define BOARD_MIN_W 6
#define BOARD_MAX_W 24
#define BOARD_MIN_H 12
#define BOARD_MAX_H 36
#define BOARD_MAX_TOTAL_H (BOARD_MAX_H + BOARD_HIDDEN)

#define CELL_EMPTY (-1)

typedef struct {
    int w, h; /* visible width and height; the grid also has BOARD_HIDDEN rows above */
    /* Stores PieceType of the locked block, or CELL_EMPTY. int8_t keeps the
     * whole board small: cheap to copy, cheap to clear. */
    int8_t cells[BOARD_MAX_TOTAL_H][BOARD_MAX_W];
} Board;

/* Empties the board and gives it the classic size. */
void Board_Clear(Board *b);
/* Empties the board at the given size (clamped to the limits above). */
void Board_Init(Board *b, int w, int h);
static inline int Board_TotalH(const Board *b) { return b->h + BOARD_HIDDEN; }

/* True if the given piece placement is fully in-bounds and free of
 * collisions with locked cells. */
bool Board_TestFit(const Board *b, PieceType type, int rotation, int px, int py);

/* Stamps the piece's cells into the board permanently. Caller must have
 * already validated the fit. */
void Board_LockPiece(Board *b, PieceType type, int rotation, int px, int py);

/* Removes every fully-filled row, compacts the board downward, and returns
 * how many rows were cleared (0-4). Writes the cleared row indices (top to
 * bottom order as found) into outRows if non-NULL (capacity 4). */
int Board_ClearLines(Board *b, int outRows[4]);

/* Power-up helpers: each returns how many filled cells it removed. */
int Board_ClearBlock(Board *b, int cx, int cy, int radius); /* the (2r+1) square around a cell */
int Board_ClearColumn(Board *b, int x);
/* Drops the bottom `count` rows off the board and lowers everything above. */
int Board_RemoveBottomRows(Board *b, int count);

int Board_CountFilled(const Board *b);

#endif
