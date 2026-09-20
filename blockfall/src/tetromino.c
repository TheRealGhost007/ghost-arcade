#include "tetromino.h"
#include <stddef.h>

/* Standard SRS-style shape tables. Each piece has 4 rotation states with
 * 4 cells each, defined in a bounding box (4x4 for I/O, 3x3 padded to 4x4
 * for the rest). Coordinates: x right, y down. */
static const Cell kShapes[PIECE_COUNT][ROTATIONS_PER_PIECE][CELLS_PER_PIECE] = {
    /* I */
    {
        { {0,1}, {1,1}, {2,1}, {3,1} },
        { {2,0}, {2,1}, {2,2}, {2,3} },
        { {0,2}, {1,2}, {2,2}, {3,2} },
        { {1,0}, {1,1}, {1,2}, {1,3} },
    },
    /* O */
    {
        { {1,0}, {2,0}, {1,1}, {2,1} },
        { {1,0}, {2,0}, {1,1}, {2,1} },
        { {1,0}, {2,0}, {1,1}, {2,1} },
        { {1,0}, {2,0}, {1,1}, {2,1} },
    },
    /* T */
    {
        { {1,0}, {0,1}, {1,1}, {2,1} },
        { {1,0}, {1,1}, {2,1}, {1,2} },
        { {0,1}, {1,1}, {2,1}, {1,2} },
        { {1,0}, {0,1}, {1,1}, {1,2} },
    },
    /* S */
    {
        { {1,0}, {2,0}, {0,1}, {1,1} },
        { {1,0}, {1,1}, {2,1}, {2,2} },
        { {1,1}, {2,1}, {0,2}, {1,2} },
        { {0,0}, {0,1}, {1,1}, {1,2} },
    },
    /* Z */
    {
        { {0,0}, {1,0}, {1,1}, {2,1} },
        { {2,0}, {1,1}, {2,1}, {1,2} },
        { {0,1}, {1,1}, {1,2}, {2,2} },
        { {1,0}, {0,1}, {1,1}, {0,2} },
    },
    /* J */
    {
        { {0,0}, {0,1}, {1,1}, {2,1} },
        { {1,0}, {2,0}, {1,1}, {1,2} },
        { {0,1}, {1,1}, {2,1}, {2,2} },
        { {1,0}, {1,1}, {0,2}, {1,2} },
    },
    /* L */
    {
        { {2,0}, {0,1}, {1,1}, {2,1} },
        { {1,0}, {1,1}, {1,2}, {2,2} },
        { {0,1}, {1,1}, {2,1}, {0,2} },
        { {0,0}, {1,0}, {1,1}, {1,2} },
    },
};

static const uint32_t kColors[PIECE_COUNT] = {
    0x28C7E8FF, /* I - cyan */
    0xF4D03FFF, /* O - amber/yellow */
    0xB565D8FF, /* T - purple */
    0x4ADE80FF, /* S - green */
    0xF14C6AFF, /* Z - red */
    0x4C7CF1FF, /* J - blue */
    0xF19A3EFF, /* L - orange */
};

static const char *kNames[PIECE_COUNT] = {
    "I", "O", "T", "S", "Z", "J", "L"
};

void Tetromino_GetCells(PieceType type, int rotation, Cell out[CELLS_PER_PIECE]) {
    int r = ((rotation % ROTATIONS_PER_PIECE) + ROTATIONS_PER_PIECE) % ROTATIONS_PER_PIECE;
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        out[i] = kShapes[type][r][i];
    }
}

uint32_t Tetromino_Color(PieceType type) {
    return kColors[type];
}

const char *Tetromino_Name(PieceType type) {
    return kNames[type];
}
