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

/* SRS wall kicks (3.2.0). For a turn from rotation state `from` to `to`
 * (each 0..3; a quarter turn either way, or a half turn), fills out[] with
 * the offsets to try in order, (0,0) first, and returns how many: the
 * published Super Rotation System tables for J L S T Z and for the I
 * (tetris.wiki, "Super Rotation System", Wall Kicks), five each, and for a
 * half turn the SRS+ 180 list (tetris.wiki, the same page, "180 rotation",
 * as TETR.IO plays it), six. The published tables count y upward; these
 * are in board coordinates, y growing downward, so the y of every offset
 * is negated. The O has one offset, (0,0). The fifth quarter-turn kick
 * (index 4) is the one that turns a T-spin mini into a full T-spin. */
#define TETROMINO_MAX_KICKS 6
int Tetromino_Kicks(PieceType type, int from, int to, Cell out[TETROMINO_MAX_KICKS]);

#endif
