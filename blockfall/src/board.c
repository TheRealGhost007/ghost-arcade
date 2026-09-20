#include "board.h"
#include <string.h>

void Board_Init(Board *b, int w, int h) {
    if (w < BOARD_MIN_W) w = BOARD_MIN_W;
    if (w > BOARD_MAX_W) w = BOARD_MAX_W;
    if (h < BOARD_MIN_H) h = BOARD_MIN_H;
    if (h > BOARD_MAX_H) h = BOARD_MAX_H;
    b->w = w;
    b->h = h;
    memset(b->cells, CELL_EMPTY, sizeof(b->cells));
}

void Board_Clear(Board *b) {
    Board_Init(b, BOARD_W, BOARD_H);
}

bool Board_TestFit(const Board *b, PieceType type, int rotation, int px, int py) {
    Cell cells[CELLS_PER_PIECE];
    Tetromino_GetCells(type, rotation, cells);
    int total = Board_TotalH(b);
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        int x = px + cells[i].x;
        int y = py + cells[i].y;
        if (x < 0 || x >= b->w || y < 0 || y >= total) return false;
        if (b->cells[y][x] != CELL_EMPTY) return false;
    }
    return true;
}

void Board_LockPiece(Board *b, PieceType type, int rotation, int px, int py) {
    Cell cells[CELLS_PER_PIECE];
    Tetromino_GetCells(type, rotation, cells);
    int total = Board_TotalH(b);
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        int x = px + cells[i].x;
        int y = py + cells[i].y;
        if (x >= 0 && x < b->w && y >= 0 && y < total) {
            b->cells[y][x] = (int8_t)type;
        }
    }
}

static bool RowFull(const Board *b, int row) {
    for (int x = 0; x < b->w; x++) {
        if (b->cells[row][x] == CELL_EMPTY) return false;
    }
    return true;
}

int Board_ClearLines(Board *b, int outRows[4]) {
    /* Single-pass bottom-up compaction: walk rows bottom to top, skip full
     * ones (they vanish), and pack the rest down onto a write cursor. O(n)
     * with no repeated shifting. */
    int cleared = 0;
    int total = Board_TotalH(b);
    int writeRow = total - 1;
    for (int readRow = total - 1; readRow >= 0; readRow--) {
        if (RowFull(b, readRow)) {
            if (outRows && cleared < 4) outRows[cleared] = readRow;
            cleared++;
            continue;
        }
        if (writeRow != readRow) {
            memcpy(b->cells[writeRow], b->cells[readRow], sizeof(b->cells[readRow]));
        }
        writeRow--;
    }
    for (int y = writeRow; y >= 0; y--) {
        memset(b->cells[y], CELL_EMPTY, sizeof(b->cells[y]));
    }
    return cleared;
}

int Board_ClearBlock(Board *b, int cx, int cy, int radius) {
    int removed = 0, total = Board_TotalH(b);
    for (int y = cy - radius; y <= cy + radius; y++) {
        for (int x = cx - radius; x <= cx + radius; x++) {
            if (x < 0 || x >= b->w || y < 0 || y >= total) continue;
            if (b->cells[y][x] != CELL_EMPTY) { b->cells[y][x] = CELL_EMPTY; removed++; }
        }
    }
    return removed;
}

int Board_ClearColumn(Board *b, int x) {
    if (x < 0 || x >= b->w) return 0;
    int removed = 0, total = Board_TotalH(b);
    for (int y = 0; y < total; y++) {
        if (b->cells[y][x] != CELL_EMPTY) { b->cells[y][x] = CELL_EMPTY; removed++; }
    }
    return removed;
}

int Board_RemoveBottomRows(Board *b, int count) {
    int total = Board_TotalH(b);
    if (count < 0) count = 0;
    if (count > total) count = total;
    int removed = 0;
    for (int y = total - count; y < total; y++) for (int x = 0; x < b->w; x++) if (b->cells[y][x] != CELL_EMPTY) removed++;
    for (int y = total - 1; y >= count; y--) memcpy(b->cells[y], b->cells[y - count], sizeof(b->cells[y]));
    for (int y = 0; y < count; y++) memset(b->cells[y], CELL_EMPTY, sizeof(b->cells[y]));
    return removed;
}

int Board_CountFilled(const Board *b) {
    int n = 0, total = Board_TotalH(b);
    for (int y = 0; y < total; y++) for (int x = 0; x < b->w; x++) if (b->cells[y][x] != CELL_EMPTY) n++;
    return n;
}
