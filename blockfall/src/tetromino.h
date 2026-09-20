#ifndef TETROMINO_H
#define TETROMINO_H

#include <stdint.h>

typedef enum {
    PIECE_I = 0,
    PIECE_O,
    PIECE_T,
    PIECE_S,
    PIECE_Z,
    PIECE_J,
    PIECE_L,
    PIECE_COUNT
} PieceType;

typedef struct {
    int8_t x, y;
} Cell;

#define CELLS_PER_PIECE 4
#define ROTATIONS_PER_PIECE 4

/* Fills out[4] with the local-space cell offsets for the given piece/rotation
 * (rotation is taken mod 4). Coordinates are in a small bounding box with
 * (0,0) at the top-left, y growing downward. */
void Tetromino_GetCells(PieceType type, int rotation, Cell out[CELLS_PER_PIECE]);

/* Distinct display color per piece, as 0xRRGGBBAA. */
uint32_t Tetromino_Color(PieceType type);

const char *Tetromino_Name(PieceType type);

#endif
