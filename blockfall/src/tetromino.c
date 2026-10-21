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

/* A cell value past the pieces' is a garbage block (board.h's CELL_GARBAGE,
 * Dig mode): one grey, so it is never mistaken for a piece. */
#define GARBAGE_COLOR 0x6E7286FFu

uint32_t Tetromino_Color(PieceType type) {
    if ((int)type < 0 || type >= PIECE_COUNT) return GARBAGE_COLOR;
    return kColors[type];
}

const char *Tetromino_Name(PieceType type) {
    if ((int)type < 0 || type >= PIECE_COUNT) return "Garbage";
    return kNames[type];
}

/* The published SRS tables, y UP as the wiki prints them. Rows: the eight
 * quarter turns in the order 0>R, R>0, R>2, 2>R, 2>L, L>2, L>0, 0>L
 * (R = 1, 2 = 2, L = 3); Turn() below finds the row for a (from, to). */
typedef struct { int8_t x, y; } Kick;
static const Kick kKicksJLSTZ[8][5] = {
    {{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}},   /* 0>R */
    {{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}},       /* R>0 */
    {{0, 0}, {1, 0}, {1, -1}, {0, 2}, {1, 2}},       /* R>2 */
    {{0, 0}, {-1, 0}, {-1, 1}, {0, -2}, {-1, -2}},   /* 2>R */
    {{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}},      /* 2>L */
    {{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}},    /* L>2 */
    {{0, 0}, {-1, 0}, {-1, -1}, {0, 2}, {-1, 2}},    /* L>0 */
    {{0, 0}, {1, 0}, {1, 1}, {0, -2}, {1, -2}},      /* 0>L */
};
static const Kick kKicksI[8][5] = {
    {{0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2}},     /* 0>R */
    {{0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2}},     /* R>0 */
    {{0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1}},     /* R>2 */
    {{0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1}},     /* 2>R */
    {{0, 0}, {2, 0}, {-1, 0}, {2, 1}, {-1, -2}},     /* 2>L */
    {{0, 0}, {-2, 0}, {1, 0}, {-2, -1}, {1, 2}},     /* L>2 */
    {{0, 0}, {1, 0}, {-2, 0}, {1, -2}, {-2, 1}},     /* L>0 */
    {{0, 0}, {-1, 0}, {2, 0}, {-1, 2}, {2, -1}},     /* 0>L */
};
/* Half turns (SRS+ 180, y up), rows 0>2, 2>0, R>L, L>R; the I uses the
 * same list (SRS+ gives the I none of its own worth the name). */
static const Kick kKicks180[4][6] = {
    {{0, 0}, {0, 1}, {1, 1}, {-1, 1}, {1, 0}, {-1, 0}},   /* 0>2 */
    {{0, 0}, {0, -1}, {-1, -1}, {1, -1}, {-1, 0}, {1, 0}}, /* 2>0 */
    {{0, 0}, {1, 0}, {1, 2}, {1, 1}, {0, 2}, {0, 1}},     /* R>L */
    {{0, 0}, {-1, 0}, {-1, 2}, {-1, 1}, {0, 2}, {0, 1}},  /* L>R */
};

static int QuarterRow(int from, int to) {
    static const int8_t kRows[4][4] = { /* [from][to], -1 for no quarter turn */
        {-1, 0, -1, 7}, {1, -1, 2, -1}, {-1, 3, -1, 4}, {6, -1, 5, -1},
    };
    return kRows[from][to];
}

int Tetromino_Kicks(PieceType type, int from, int to, Cell out[TETROMINO_MAX_KICKS]) {
    from = ((from % 4) + 4) % 4;
    to = ((to % 4) + 4) % 4;
    if (type == PIECE_O || from == to) {
        out[0] = (Cell){0, 0};
        return 1;
    }
    const Kick *k;
    int n;
    int row = QuarterRow(from, to);
    if (row >= 0) {
        k = type == PIECE_I ? kKicksI[row] : kKicksJLSTZ[row];
        n = 5;
    } else {
        static const int8_t kHalf[4] = {0, 2, 1, 3}; /* from 0, R, 2, L */
        k = kKicks180[kHalf[from]];
        n = 6;
    }
    for (int i = 0; i < n; i++) out[i] = (Cell){k[i].x, (int8_t)-k[i].y}; /* the board counts y downward */
    return n;
}
