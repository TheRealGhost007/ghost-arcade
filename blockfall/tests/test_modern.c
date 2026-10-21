/* Modern rules on every board (Blockfall 3.2.0, REWORK_PLAN.md "Modern
 * rules", the owner's decision that the classic 10x20 plays them too):
 *  - SRS turning: the kick tables against the published ones, a kick off
 *    the wall, the fifth kick, no climbing, and a kick resetting the lock
 *    delay as any move does;
 *  - T-spins (full and mini) and perfect clears: when they are, what they
 *    score, how back-to-back chains them, what cancels one;
 *  - the Next queue: the same seed gives the same pieces (and power-ups) as
 *    the bag always gave, the queue's pieces come in in order, Settings'
 *    queue length and the layout it makes;
 *  - the optional Rotate 180 key: the turn, the action, the setting;
 *  - the classic Marathon's fresh board and the old-rules board, read only;
 *  - the callouts, the sounds and the results card's T-spin count.
 * No raylib. The record tests run in the temp HOME test_main.c made. */
#define _XOPEN_SOURCE 700
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "check.h"
#include "../src/actions.h"
#include "../src/board.h"
#include "../src/controls.h"
#include "../src/game.h"
#include "../src/hudfx.h"
#include "../src/layout.h"
#include "../src/persist.h"
#include "../src/playstep.h"
#include "../src/results.h"
#include "../src/runrecord.h"
#include "../src/setpage.h"
#include "../src/soundplan.h"
#include "../src/tetromino.h"
#include "rng.h"
#include "uikit.h"

#define FRAME (1.0f / 60.0f)
#define NEAR(a, b) (fabsf((a) - (b)) < 0.0005f)

/* Row y from the bottom of the visible well: '#' a block, '.' none. */
static void Row(Game *g, int y, const char *pattern) {
    int row = Board_TotalH(&g->board) - 1 - y;
    for (int x = 0; x < g->board.w; x++) {
        char c = x < (int)strlen(pattern) ? pattern[x] : '.';
        g->board.cells[row][x] = c == '#' ? (int8_t)PIECE_J : CELL_EMPTY;
    }
}

static void Place(Game *g, PieceType t, int rot, int px, int py) {
    g->current = t;
    g->rotation = rot;
    g->px = px;
    g->py = py;
    g->curPower = PU_NONE;
    g->fallTimer = g->lockTimer = 0.0f;
    g->lockResets = 0;
    g->lowestRow = py;
    g->grounded = false;
    g->lastMoveWasTurn = false;
    g->lastKick = 0;
    g->lastTurnHalf = false;
}

static Game Modern(int w, int h, uint64_t seed) {
    Game g;
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, w, h, true}, seed, 0, 1, 0);
    Board_Init(&g.board, w, h);
    return g;
}

/* ----------------------------------------------------------- SRS tables */

/* The published tables, copied from tetris.wiki, "Super Rotation System",
 * section "Wall Kicks" (y counts UP there): the J, L, S, T, Z table and the
 * I table, each row a turn in the order 0>R, R>0, R>2, 2>R, 2>L, L>2, L>0,
 * 0>L, and the SRS+ 180 kicks from the same page ("180 rotation", as
 * TETR.IO plays them), rows 0>2, 2>0, R>L, L>R. */
typedef struct { int x, y; } Pub;
static const Pub kPubJLSTZ[8][5] = {
    {{0, 0}, {-1, 0}, {-1, +1}, {0, -2}, {-1, -2}},
    {{0, 0}, {+1, 0}, {+1, -1}, {0, +2}, {+1, +2}},
    {{0, 0}, {+1, 0}, {+1, -1}, {0, +2}, {+1, +2}},
    {{0, 0}, {-1, 0}, {-1, +1}, {0, -2}, {-1, -2}},
    {{0, 0}, {+1, 0}, {+1, +1}, {0, -2}, {+1, -2}},
    {{0, 0}, {-1, 0}, {-1, -1}, {0, +2}, {-1, +2}},
    {{0, 0}, {-1, 0}, {-1, -1}, {0, +2}, {-1, +2}},
    {{0, 0}, {+1, 0}, {+1, +1}, {0, -2}, {+1, -2}},
};
static const Pub kPubI[8][5] = {
    {{0, 0}, {-2, 0}, {+1, 0}, {-2, -1}, {+1, +2}},
    {{0, 0}, {+2, 0}, {-1, 0}, {+2, +1}, {-1, -2}},
    {{0, 0}, {-1, 0}, {+2, 0}, {-1, +2}, {+2, -1}},
    {{0, 0}, {+1, 0}, {-2, 0}, {+1, -2}, {-2, +1}},
    {{0, 0}, {+2, 0}, {-1, 0}, {+2, +1}, {-1, -2}},
    {{0, 0}, {-2, 0}, {+1, 0}, {-2, -1}, {+1, +2}},
    {{0, 0}, {+1, 0}, {-2, 0}, {+1, -2}, {-2, +1}},
    {{0, 0}, {-1, 0}, {+2, 0}, {-1, +2}, {+2, -1}},
};
static const Pub kPub180[4][6] = {
    {{0, 0}, {0, +1}, {+1, +1}, {-1, +1}, {+1, 0}, {-1, 0}},
    {{0, 0}, {0, -1}, {-1, -1}, {+1, -1}, {-1, 0}, {+1, 0}},
    {{0, 0}, {+1, 0}, {+1, +2}, {+1, +1}, {0, +2}, {0, +1}},
    {{0, 0}, {-1, 0}, {-1, +2}, {-1, +1}, {0, +2}, {0, +1}},
};
/* The eight quarter turns, as (from, to). */
static const int kTurns[8][2] = {{0, 1}, {1, 0}, {1, 2}, {2, 1}, {2, 3}, {3, 2}, {3, 0}, {0, 3}};
static const int kHalfTurns[4][2] = {{0, 2}, {2, 0}, {1, 3}, {3, 1}};

static bool SameKicks(PieceType t, int from, int to, const Pub *pub, int count) {
    Cell out[TETROMINO_MAX_KICKS];
    if (Tetromino_Kicks(t, from, to, out) != count) return false;
    for (int i = 0; i < count; i++)
        if (out[i].x != pub[i].x || out[i].y != -pub[i].y) return false; /* the board counts y down */
    return true;
}

static void test_srs_tables(void) {
    bool jlstz = true, iPiece = true, half = true;
    static const PieceType kFive[5] = {PIECE_J, PIECE_L, PIECE_S, PIECE_T, PIECE_Z};
    for (int r = 0; r < 8; r++) {
        for (int p = 0; p < 5; p++)
            if (!SameKicks(kFive[p], kTurns[r][0], kTurns[r][1], kPubJLSTZ[r], 5)) jlstz = false;
        if (!SameKicks(PIECE_I, kTurns[r][0], kTurns[r][1], kPubI[r], 5)) iPiece = false;
    }
    for (int r = 0; r < 4; r++)
        for (int p = 0; p < 5; p++)
            if (!SameKicks(kFive[p], kHalfTurns[r][0], kHalfTurns[r][1], kPub180[r], 6)) half = false;
    CHECK(jlstz, "srs: J L S T Z turn with the published wall-kick table, every quarter turn, in its order");
    CHECK(iPiece, "srs: the I turns with its own published table");
    CHECK(half, "srs: a half turn uses the SRS+ 180 list");
    Cell out[TETROMINO_MAX_KICKS];
    CHECK(Tetromino_Kicks(PIECE_O, 0, 1, out) == 1 && out[0].x == 0 && out[0].y == 0, "srs: the O has one offset, in place");
    CHECK(Tetromino_Kicks(PIECE_T, 1, 1, out) == 1, "srs: no turn, one offset");
    CHECK(Tetromino_Kicks(PIECE_T, -1, 0, out) == 5 && out[1].x == -1 && out[1].y == 0, "srs: rotations are taken mod 4 (L>0)");
    /* The tables as printed: every I offset reaches at most 2 cells, every
     * other at most 1 sideways and 2 down, so no kick ever throws a piece
     * clear across the well. */
    bool bounded = true;
    for (int t = 0; t < PIECE_COUNT; t++)
        for (int from = 0; from < 4; from++)
            for (int to = 0; to < 4; to++) {
                int n = Tetromino_Kicks((PieceType)t, from, to, out);
                for (int i = 0; i < n; i++) if (abs(out[i].x) > 2 || abs(out[i].y) > 2) bounded = false;
            }
    CHECK(bounded, "srs: no offset reaches more than two cells");
}

/* --------------------------------------------------------- SRS in play */

static void test_srs_play(void) {
    Game g = Modern(10, 20, 3);
    /* A vertical I against the right wall: flat in place would leave the
     * well, so R>2's second offset (-1, 0) takes it (the old test's case). */
    Place(&g, PIECE_I, 1, 7, BOARD_HIDDEN + 5);
    CHECK(Game_Rotate(&g, 1) && g.rotation == 2 && g.px == 6 && g.py == BOARD_HIDDEN + 5 && g.lastKick == 1 && g.lastMoveWasTurn,
          "srs: the I off the right wall takes the table's second offset, one to the left");
    /* Against the left wall, L>0's second offset (+1, 0) brings it in. */
    Place(&g, PIECE_I, 3, -1, BOARD_HIDDEN + 5); /* local column 1 is board column 0 */
    CHECK(Game_Rotate(&g, 1) && g.rotation == 0 && g.px == 0 && g.lastKick == 1, "srs: the I off the left wall takes L>0's second offset, one to the right");
    /* A T on the floor: 0>R cannot turn in place (its stem would go through
     * the floor) and the third offset lifts it one row; after that it turns
     * in place for ever. Twenty turns never lift it a second row: pieces do
     * not climb. */
    Board_Init(&g.board, 10, 20);
    int floorTop = Board_TotalH(&g.board) - 2;
    Place(&g, PIECE_T, 0, 3, floorTop);
    CHECK(Game_Rotate(&g, 1) && g.rotation == 1 && g.py == floorTop - 1 && g.lastKick == 2, "srs: the T on the floor turns up one row by its third offset");
    int highest = g.py;
    for (int i = 0; i < 20; i++) {
        Game_Rotate(&g, (i & 1) ? 1 : -1);
        if (g.py < highest) highest = g.py;
    }
    CHECK(highest >= floorTop - 1, "srs: turning back and forth on the floor never climbs a second row");
    /* Every piece against a taller stack at its right: turns never put it
     * on top of that stack (the old nudge list, with (0,-2), could). */
    bool climbed = false;
    for (int t = 0; t < PIECE_COUNT; t++) {
        Board_Init(&g.board, 10, 20);
        for (int y = 0; y < 6; y++) Row(&g, y, "....######"); /* a step six high at column 4 */
        Place(&g, (PieceType)t, 0, 0, Board_TotalH(&g.board) - 2);
        int start = g.py;
        for (int i = 0; i < 12; i++) {
            Game_Rotate(&g, (i % 3 == 0) ? -1 : 1);
            if (g.py < start - 2) climbed = true; /* two rows up is the most any I kick reaches */
            Cell cells[CELLS_PER_PIECE];
            Tetromino_GetCells(g.current, g.rotation, cells);
            for (int c = 0; c < CELLS_PER_PIECE; c++)
                if (g.px + cells[c].x >= 4 && g.py + cells[c].y < Board_TotalH(&g.board) - 6) climbed = true; /* over the step */
        }
    }
    CHECK(!climbed, "srs: no piece turns its way up onto a stack beside it");
    /* With nowhere to go the turn fails and nothing moves. */
    Board_Init(&g.board, 10, 20);
    for (int y = 0; y < 4; y++) Row(&g, y, "###.######");
    Place(&g, PIECE_I, 1, 1, Board_TotalH(&g.board) - 4); /* standing in the one-wide slot */
    CHECK(!Game_Rotate(&g, 1) && g.rotation == 1 && g.px == 1 && !g.lastMoveWasTurn, "srs: a turn with no offset that fits does nothing");

    /* The fifth offset: a T pointing down, turned clockwise (2>L), with
     * blocks placed exactly to stop the first four offsets, drops into a
     * slot by the fifth, (+1, +2); the kick's index is kept for the T-spin
     * rule. */
    Board_Init(&g.board, 10, 20);
    int py0 = 14, px0 = 3;
    g.board.cells[py0][px0 + 1] = PIECE_J;     /* stops the turn in place */
    g.board.cells[py0][px0 + 2] = PIECE_J;     /* stops offsets 2 and 3 */
    g.board.cells[py0 + 3][px0] = PIECE_J;     /* stops offset 4 (two down) */
    g.board.cells[py0 + 2][px0 + 3] = PIECE_J; /* the slot's corners: top right, */
    g.board.cells[py0 + 4][px0 + 3] = PIECE_J; /* bottom right, */
    g.board.cells[py0 + 4][px0 + 1] = PIECE_J; /* bottom left; top left stays open */
    Place(&g, PIECE_T, 2, px0, py0);
    CHECK(Game_Rotate(&g, 1) && g.rotation == 3 && g.px == px0 + 1 && g.py == py0 + 2 && g.lastKick == TSPIN_FULL_KICK,
          "srs: with the first four offsets blocked the fifth, one right and two down, places the T");
    CHECK(Game_TSpinKind(&g) == TSPIN_FULL, "tspin: three corners with a front one open is a mini, but the fifth kick makes it full");
    g.lastKick = 0;
    CHECK(Game_TSpinKind(&g) == TSPIN_MINI, "tspin: the same spot reached by another offset is a mini");
}

static void test_lock_reset_on_kick(void) {
    Game g = Modern(10, 20, 5);
    int floorTop = Board_TotalH(&g.board) - 2;
    Place(&g, PIECE_T, 0, 3, floorTop);
    Game_Update(&g, 0.3f);
    CHECK(g.grounded && g.lockTimer > 0.25f && g.lockResets == 0, "lock: the T sits on the floor with its delay running");
    CHECK(Game_Rotate(&g, 1) && g.lockTimer == 0.0f && g.lockResets == 1, "lock: a kick (the turn lifted it) restarts the delay and counts a reset");
    /* Pointing right it stands on the floor; pointing down it sits on it:
     * both turn in place, both grounded, so each turn is a reset, up to
     * the fifteen; the sixteenth and on no longer touch the clock. */
    for (int i = 0; i < 30; i++) Game_Rotate(&g, (i & 1) ? 1 : -1);
    CHECK(g.lockResets == MAX_LOCK_RESETS && g.lockTimer == 0.0f && g.phase == GS_PLAYING && !g.justLocked,
          "lock: turns on the floor spend the fifteen resets");
    Game_Update(&g, 0.3f);
    Game_Rotate(&g, 1);
    CHECK(g.lockTimer > 0.25f, "lock: once they are spent a turn no longer restarts the delay");
    Game_Update(&g, LOCK_DELAY_SECONDS);
    CHECK(g.justLocked, "lock: and the piece locks whatever it turned");
}

/* -------------------------------------------------------------- T-spins */

/* The classic T-spin double slot (10 wide): row 0 all but column 4, row 1
 * all but columns 3 to 5, an overhang at (3, row 2). The T comes in
 * standing (rotation 1) in the slot and turns to point down. */
static void TsdSlot(Game *g) {
    Board_Init(&g->board, 10, 20);
    Row(g, 0, "####.#####");
    Row(g, 1, "###...####");
    Row(g, 2, "###.......");
    g->board.cells[Board_TotalH(&g->board) - 3][3] = PIECE_J; /* the overhang */
}

static void test_tspin_double(void) {
    Game g = Modern(10, 20, 8);
    TsdSlot(&g);
    int bottom = Board_TotalH(&g.board) - 1;
    Place(&g, PIECE_T, 1, 3, bottom - 2);
    CHECK(Game_Rotate(&g, 1) && g.rotation == 2 && g.px == 3 && g.py == bottom - 2 && g.lastKick == 0, "tsd: the T turns in place to point down into the slot");
    CHECK(Game_TSpinKind(&g) == TSPIN_FULL, "tsd: with the overhang and the floor it is a full T-spin (both front corners filled)");
    long before = g.score;
    Game_HardDrop(&g);
    CHECK(g.lastDropCells == 0 && g.lastTSpin == TSPIN_FULL && g.lastClearCount == 2, "tsd: a hard drop that goes nowhere keeps the spin; two rows clear");
    CHECK(g.score - before == 1200 && !g.lastWasB2B && g.backToBack && g.stats.tspins == 1 && g.stats.clears[2] == 1,
          "tsd: a T-spin double is 1 200 at level 1, and arms back-to-back");
    Game_ConsumeFrameFlags(&g);
    /* A quad after it is back to back (half again), and a T-spin double
     * after that too: the chain runs through both kinds. */
    Board_Init(&g.board, 10, 20);
    for (int y = 0; y < 4; y++) Row(&g, y, "#########.");
    Row(&g, 4, "#.........");
    Place(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
    before = g.score;
    Game_HardDrop(&g);
    CHECK(g.lastClearCount == 4 && g.lastWasB2B && g.score - before == 1200 + 50 + 2L * g.lastDropCells,
          "tsd: the quad after a T-spin double is back to back (1 200) with the combo's 50");
    CHECK(g.stats.backToBacks == 1, "tsd: the run's counter chains quads and T-spins alike");
    Game_ConsumeFrameFlags(&g);
    TsdSlot(&g);
    Place(&g, PIECE_T, 1, 3, bottom - 2);
    Game_Rotate(&g, 1);
    before = g.score;
    Game_HardDrop(&g);
    CHECK(g.lastTSpin == TSPIN_FULL && g.lastWasB2B && g.score - before == 1800 + 100, "tsd: and a T-spin double after the quad pays 1 800 (plus combo 2's 100)");
    Game_ConsumeFrameFlags(&g);

    /* A T-spin that clears nothing: 400, and it neither breaks nor makes
     * back-to-back; the combo is left alone too. */
    TsdSlot(&g);
    Row(&g, 0, "####.####."); /* a hole in each row: nothing clears */
    Row(&g, 1, "###...###.");
    bool b2bWas = g.backToBack;
    int comboWas = g.combo;
    Place(&g, PIECE_T, 1, 3, bottom - 2);
    Game_Rotate(&g, 1);
    before = g.score;
    Game_HardDrop(&g);
    CHECK(g.lastTSpin == TSPIN_FULL && g.lastClearCount == 0 && g.score - before == 400, "tspin: a T-spin with no lines is 400");
    CHECK(g.backToBack == b2bWas && comboWas >= 0 && g.combo == -1 && !g.lastWasB2B,
          "tspin: it neither breaks nor makes back-to-back, though like any lock without a clear it ends the combo");
    Game_ConsumeFrameFlags(&g);

    /* The T must still be where the turn left it: a slide, a step down or a
     * fall after the turn makes it an ordinary lock. */
    g = Modern(10, 20, 8);
    Place(&g, PIECE_T, 0, 3, BOARD_HIDDEN);
    Game_Rotate(&g, 1);
    CHECK(g.lastMoveWasTurn, "tspin: a turn marks the piece");
    Game_MoveLeft(&g);
    CHECK(!g.lastMoveWasTurn, "tspin: a slide after it unmarks it");
    Game_Rotate(&g, 1);
    Game_SoftDrop(&g);
    CHECK(!g.lastMoveWasTurn, "tspin: so does a soft-drop step");
    Game_Rotate(&g, 1);
    g.fallTimer = 10.0f;
    Game_Update(&g, FRAME);
    CHECK(!g.lastMoveWasTurn, "tspin: and a step of gravity");
    Game_Rotate(&g, 1);
    Game_HardDrop(&g);
    CHECK(g.lastTSpin == TSPIN_NONE && g.piecesLocked == 1, "tspin: a hard drop that falls is a move: no T-spin on an open floor");
    /* Three corners are the least; two are nothing. */
    g = Modern(10, 20, 8);
    Row(&g, 0, "####.#####");
    Row(&g, 1, "###...####");
    Place(&g, PIECE_T, 1, 3, bottom - 2);
    Game_Rotate(&g, 1);
    CHECK(Game_TSpinKind(&g) == TSPIN_NONE, "tspin: the slot without its overhang has two corners: not a T-spin");
    /* Only a T spins. */
    g = Modern(10, 20, 8);
    Board_Init(&g.board, 10, 20);
    for (int y = 0; y < 3; y++) Row(&g, y, "##..######");
    Place(&g, PIECE_S, 1, 1, bottom - 4);
    Game_Rotate(&g, 1);
    CHECK(Game_TSpinKind(&g) == TSPIN_NONE, "tspin: an S in a tight spot is no T-spin");
}

static void test_tspin_mini(void) {
    Game g = Modern(10, 20, 9);
    int bottom = Board_TotalH(&g.board) - 1;
    /* Row 0 open at column 0 only; a T beside the wall turns right: in place
     * its stem hits the row, the second offset (-1, 0) puts its stem in the
     * hole with the wall at its back. Three corners (two of them the wall),
     * but the front ones are not both filled: a mini. */
    Row(&g, 0, ".#########");
    Place(&g, PIECE_T, 0, 0, bottom - 2);
    CHECK(Game_Rotate(&g, 1) && g.rotation == 1 && g.px == -1 && g.lastKick == 1, "mini: the T slides into the hole by the wall");
    CHECK(Game_TSpinKind(&g) == TSPIN_MINI, "mini: three corners, the front ones not both filled");
    long before = g.score;
    Game_HardDrop(&g);
    CHECK(g.lastTSpin == TSPIN_MINI && g.lastClearCount == 1 && g.score - before == 200 && g.stats.tspins == 1,
          "mini: a T-spin mini single is 200 at level 1");
    CHECK(g.backToBack, "mini: a mini that clears arms back-to-back like any T-spin clear");
    Game_ConsumeFrameFlags(&g);
    /* A mini with no lines: 100 (the floor and the wall alone: a T turned
     * against the left wall on the floor). */
    g = Modern(10, 20, 9);
    Row(&g, 0, ".#....####");
    Place(&g, PIECE_T, 0, 0, bottom - 2);
    Game_Rotate(&g, 1);
    CHECK(g.px == -1 && Game_TSpinKind(&g) == TSPIN_MINI, "mini: (the same turn, with the row not completing)");
    before = g.score;
    Game_HardDrop(&g);
    CHECK(g.lastTSpin == TSPIN_MINI && g.lastClearCount == 0 && g.score - before == 100, "mini: a mini with no lines is 100");
    /* The points table itself. */
    CHECK(Game_ClearPoints(0, TSPIN_NONE) == 0 && Game_ClearPoints(1, TSPIN_NONE) == 100 && Game_ClearPoints(4, TSPIN_NONE) == 800,
          "points: the classic table");
    CHECK(Game_ClearPoints(0, TSPIN_FULL) == 400 && Game_ClearPoints(1, TSPIN_FULL) == 800 && Game_ClearPoints(2, TSPIN_FULL) == 1200 &&
              Game_ClearPoints(3, TSPIN_FULL) == 1600,
          "points: T-spin 400, 800, 1 200, 1 600");
    CHECK(Game_ClearPoints(0, TSPIN_MINI) == 100 && Game_ClearPoints(1, TSPIN_MINI) == 200 && Game_ClearPoints(2, TSPIN_MINI) == 400,
          "points: mini 100, 200, 400");
    /* On the classic table (the tests' modernScoring false) a T-spin is
     * detected but not paid. */
    Game c;
    Game_InitConfig(&c, (GameConfig){MODE_MARATHON, 10, 20, false}, 9, 0, 1, 0);
    Board_Init(&c.board, 10, 20);
    Row(&c, 0, ".#########");
    Place(&c, PIECE_T, 0, 0, bottom - 2);
    Game_Rotate(&c, 1);
    before = c.score;
    Game_HardDrop(&c);
    CHECK(c.lastTSpin == TSPIN_MINI && c.score - before == 100, "points: the classic table alone pays a mini single as a single");
}

/* -------------------------------------------------------- perfect clears */

static void test_perfect_clear(void) {
    Game g = Modern(10, 20, 11);
    for (int y = 0; y < 4; y++) Row(&g, y, "#########.");
    Place(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
    long before = g.score;
    Game_HardDrop(&g);
    long drop = 2L * g.lastDropCells;
    CHECK(g.lastClearCount == 4 && g.lastPerfectClear && Board_CountFilled(&g.board) == 0 && g.stats.perfectClears == 1,
          "perfect: a quad that empties the well is a perfect clear");
    CHECK(g.score - before == 800 + 2000 + drop, "perfect: a quad's 800 and the perfect clear's 2 000");
    Game_ConsumeFrameFlags(&g);
    for (int y = 0; y < 4; y++) Row(&g, y, "#########.");
    Place(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
    before = g.score;
    Game_HardDrop(&g);
    drop = 2L * g.lastDropCells;
    CHECK(g.lastPerfectClear && g.lastWasB2B && g.score - before == 1200 + 50 + 3200 + drop,
          "perfect: a back-to-back quad perfect clear is 1 200, the combo's 50 and 3 200");
    Game_ConsumeFrameFlags(&g);
    /* A single that empties the well: 100 and 800. */
    g = Modern(10, 20, 11);
    Row(&g, 0, "....######");
    Place(&g, PIECE_I, 0, 0, BOARD_HIDDEN);
    before = g.score;
    Game_HardDrop(&g);
    drop = 2L * g.lastDropCells;
    CHECK(g.lastClearCount == 1 && g.lastPerfectClear && g.score - before == 100 + 800 + drop, "perfect: a single that empties the well adds 800");
    Game_ConsumeFrameFlags(&g);
    /* One block left is not perfect. */
    g = Modern(10, 20, 11);
    Row(&g, 0, "....######");
    Row(&g, 1, ".........#");
    Place(&g, PIECE_I, 0, 0, BOARD_HIDDEN);
    Game_HardDrop(&g);
    CHECK(g.lastClearCount == 1 && !g.lastPerfectClear, "perfect: a block left standing is not a perfect clear");
    CHECK(Game_PerfectClearBonus(1, false) == 800 && Game_PerfectClearBonus(2, false) == 1200 && Game_PerfectClearBonus(3, false) == 1800 &&
              Game_PerfectClearBonus(4, false) == 2000 && Game_PerfectClearBonus(4, true) == 3200 && Game_PerfectClearBonus(0, false) == 0,
          "perfect: the bonus table");
    /* The classic table alone (modernScoring false) pays no bonus. */
    Game c;
    Game_InitConfig(&c, (GameConfig){MODE_MARATHON, 10, 20, false}, 11, 0, 1, 0);
    Board_Init(&c.board, 10, 20);
    Row(&c, 0, "....######");
    Place(&c, PIECE_I, 0, 0, BOARD_HIDDEN);
    before = c.score;
    Game_HardDrop(&c);
    CHECK(c.lastPerfectClear && c.score - before == 100 + 2L * c.lastDropCells, "perfect: the classic table alone pays the single");
}

/* ------------------------------------------------------ the piece sequence */

/* The bag as game.c has always drawn it: seven pieces shuffled by a
 * Fisher-Yates walk from the top with Rng_Range(i + 1), then in Power mode
 * a roll for a power-up right after each draw (Rng_Range(100) under 14,
 * then the power and the block). This test re-draws it here, apart from
 * the game, so a change to the order of the rng's calls would show. */
typedef struct {
    Rng rng;
    PieceType bag[PIECE_COUNT];
    int pos;
    bool power;
} RefBag;

static void RefInit(RefBag *b, uint64_t seed, bool power) {
    Rng_Seed(&b->rng, seed);
    b->pos = PIECE_COUNT;
    b->power = power;
}

static PieceType RefDraw(RefBag *b, PowerUp *power, int *cell) {
    if (b->pos >= PIECE_COUNT) {
        for (int i = 0; i < PIECE_COUNT; i++) b->bag[i] = (PieceType)i;
        for (int i = PIECE_COUNT - 1; i > 0; i--) {
            int j = (int)Rng_Range(&b->rng, (uint32_t)(i + 1));
            PieceType t = b->bag[i];
            b->bag[i] = b->bag[j];
            b->bag[j] = t;
        }
        b->pos = 0;
    }
    PieceType p = b->bag[b->pos++];
    *power = PU_NONE;
    *cell = 0;
    if (b->power && Rng_Range(&b->rng, 100) < POWER_CHANCE_PERCENT) {
        *power = (PowerUp)(1 + (int)Rng_Range(&b->rng, PU_COUNT - 1));
        *cell = (int)Rng_Range(&b->rng, CELLS_PER_PIECE);
    }
    return p;
}

static void test_seed_sequence(void) {
    static const uint64_t kSeeds[] = {1, 42, 7919, 0x5EEDu, 123456789u};
    bool pieces = true, powers = true, queued = true, sevens = true;
    for (size_t s = 0; s < sizeof(kSeeds) / sizeof(kSeeds[0]); s++) {
        for (int mode = 0; mode < 2; mode++) {
            Game g;
            Game_InitConfig(&g, (GameConfig){mode ? MODE_POWER : MODE_MARATHON, 10, 20, true}, kSeeds[s], 0, 1, 0);
            RefBag ref;
            RefInit(&ref, kSeeds[s], mode == 1);
            PowerUp rp;
            int rc;
            /* The first draw is the first piece in; the queue holds the next six. */
            if (RefDraw(&ref, &rp, &rc) != g.current || rp != g.curPower || (rp != PU_NONE && rc != g.curPowerCell)) pieces = false;
            PieceType ahead[QUEUE_MAX];
            for (int i = 0; i < QUEUE_MAX; i++) {
                ahead[i] = g.queue[i];
                if (RefDraw(&ref, &rp, &rc) != g.queue[i] || rp != g.queuePower[i] || (rp != PU_NONE && rc != g.queueCell[i])) queued = false;
            }
            if (g.next != g.queue[0] || g.nextPower != g.queuePower[0]) queued = false;
            int counts[PIECE_COUNT] = {0}, counted = 0;
            counts[g.current]++;
            counted++;
            for (int k = 0; k < 70; k++) {
                Board_Init(&g.board, 10, 20); /* never top out */
                Game_HardDrop(&g);
                Game_ConsumeFrameFlags(&g);
                if (k < QUEUE_MAX && g.current != ahead[k]) queued = false;
                if (k >= QUEUE_MAX) {
                    PieceType want = RefDraw(&ref, &rp, &rc);
                    if (want != g.current) pieces = false;
                    if (rp != g.curPower || (rp != PU_NONE && rc != g.curPowerCell)) powers = false;
                }
                counts[g.current]++;
                if (++counted % 7 == 0) { /* a bag of seven: every piece once */
                    for (int t = 0; t < PIECE_COUNT; t++) if (counts[t] != 1) sevens = false;
                    memset(counts, 0, sizeof(counts));
                }
            }
        }
    }
    CHECK(pieces, "seed: a seed gives the pieces the seven-bag always gave, in Marathon and in Power (drawn apart from the game here)");
    CHECK(powers, "seed: and the same power-ups on the same pieces");
    CHECK(queued, "seed: the queue holds the six pieces to come, next first, and they come in in that order");
    CHECK(sevens, "seed: (the bag still deals each piece once per seven)");
    /* Two games from one seed are the same game. */
    Game a, b;
    Game_InitConfig(&a, (GameConfig){MODE_POWER, 14, 20, true}, 77, 0, 1, 0);
    Game_InitConfig(&b, (GameConfig){MODE_POWER, 14, 20, true}, 77, 0, 1, 0);
    bool same = true;
    for (int i = 0; i < 40; i++) {
        if (memcmp(a.queue, b.queue, sizeof(a.queue)) != 0 || a.current != b.current) same = false;
        Game_HardDrop(&a);
        Game_HardDrop(&b);
        if (a.phase != GS_PLAYING) break;
    }
    CHECK(same, "seed: two games from one seed hold the same queue all along");
    /* A next set by hand (the harness, How to play's demos) is the piece
     * that comes in; the queue goes on behind it. */
    Game_InitConfig(&a, (GameConfig){MODE_MARATHON, 10, 20, true}, 5, 0, 1, 0);
    PieceType second = a.queue[1];
    a.next = PIECE_O;
    Game_HardDrop(&a);
    CHECK(a.current == PIECE_O && a.next == second && a.queue[0] == second, "seed: a next set by hand comes in, and the queue moves up behind it");
}

/* ----------------------------------------------------- the queue shown */

static bool Overlap(LRect a, LRect b) { return LRect_Overlaps(a, b); }

static void test_queue_layout(void) {
    Settings s;
    Settings_Default(&s);
    CHECK(s.nextQueue == QUEUE_SHOWN_DEFAULT && QUEUE_SHOWN_DEFAULT == 5, "queue: Next shows five pieces as Blockfall comes");
    /* The classic board with the queue: the Next box grows by the queue's
     * row, Hold and the stats move down, the card still fits; without it
     * everything is where it always was. */
    HudSpec spec = Layout_Spec(MODE_MARATHON, true, true);
    PlayLayout plain = Layout_Play(10, 20, &spec);
    spec.queue = 4;
    PlayLayout l = Layout_Play(10, 20, &spec);
    CHECK(plain.next.h == PREVIEW_BOX_H && plain.queueSlots == 0 && LRect_Empty(plain.queue) && plain.hold.y == 140,
          "queue: with none shown the Next box is as it was");
    CHECK(l.next.h == PREVIEW_BOX_H + QUEUE_ROW_H && l.hold.y == plain.hold.y + QUEUE_ROW_H && l.stat[0].y == plain.stat[0].y + QUEUE_ROW_H &&
              l.showCard && l.queueSlots == 4 && l.queueCols == 4 && l.queueCell == QUEUE_CELL,
          "queue: on the classic board the Next box is 24 px taller, Hold and the stats move down, the card stays, four 8 px slots in a row");
    CHECK(LRect_Inside(l.nextBig, l.next) && l.nextBig.h == PREVIEW_BOX_H && LRect_Inside(l.queue, l.next), "queue: the large piece's part and the queue's row are both inside the box");
    /* Every size, every mode, with five shown: nothing overlaps or leaves
     * the window, every slot is inside the box and apart from the others. */
    bool inside = true, apart = true, slotsOk = true, cellsOk = true;
    for (int w = BOARD_MIN_W; w <= BOARD_MAX_W; w++)
        for (int h = BOARD_MIN_H; h <= BOARD_MAX_H; h++)
            for (int m = 0; m < MODE_COUNT; m++) {
                spec = Layout_Spec((GameMode)m, true, true);
                spec.queue = QUEUE_SHOWN_MAX;
                l = Layout_Play(w, h, &spec);
                LRect rects[12];
                int n = 0;
                rects[n++] = l.next; rects[n++] = l.hold;
                for (int i = 0; i < HUD_STATS; i++) rects[n++] = l.stat[i];
                rects[n++] = l.combo; rects[n++] = l.b2b; rects[n++] = l.freeze; rects[n++] = l.slow; rects[n++] = l.card;
                rects[n++] = Layout_WellFrame(&l);
                LRect window = {0, 0, LAYOUT_WINDOW_W, LAYOUT_FOOTER_Y};
                for (int i = 0; i < n; i++) {
                    if (!LRect_Inside(rects[i], window)) inside = false;
                    for (int j = i + 1; j < n; j++) if (Overlap(rects[i], rects[j])) apart = false;
                }
                if (l.queueSlots != QUEUE_SHOWN_MAX || l.queueCell < QUEUE_MIN_CELL || l.queueCell > QUEUE_CELL) cellsOk = false;
                for (int i = 0; i < l.queueSlots; i++) {
                    LRect slot = Layout_QueueSlot(&l, i);
                    if (!LRect_Inside(slot, l.next) || Overlap(slot, l.nextBig)) slotsOk = false;
                    for (int j = i + 1; j < l.queueSlots; j++) if (Overlap(slot, Layout_QueueSlot(&l, j))) slotsOk = false;
                    for (int t = 0; t < PIECE_COUNT; t++) {
                        int xs[4], ys[4];
                        Layout_QueueCells(&l, i, (PieceType)t, xs, ys);
                        for (int c = 0; c < 4; c++)
                            if (!LRect_Inside((LRect){xs[c], ys[c], l.queueCell, l.queueCell}, slot)) slotsOk = false;
                    }
                }
            }
    CHECK(inside && apart, "queue: with five shown nothing on any board overlaps or leaves the window");
    CHECK(slotsOk, "queue: every slot is inside the Next box, clear of the large piece's part, apart from the others, and holds every piece");
    CHECK(cellsOk, "queue: five slots everywhere, with cells of 4 to 8 px");
    /* The strip: the box grows to the right instead, Hold moves over. */
    spec = Layout_Spec(MODE_MARATHON, true, true);
    plain = Layout_Play(24, 12, &spec);
    spec.queue = 5;
    l = Layout_Play(24, 12, &spec);
    CHECK(l.place == PLACE_STRIP && l.next.w == plain.next.w + QUEUE_STRIP_W && l.next.h == plain.next.h && l.hold.x == plain.hold.x + QUEUE_STRIP_W &&
              l.queueCols == 2 && l.nextBig.w == STRIP_BOX_W,
          "queue: in the strip the Next box is wider by the queue's two columns and Hold moves right");
    /* Settings > Game: the row, its range, a change. */
    Kit_SetScreen(620, 720);
    ArcadePrefs pr = {70, true, false, false};
    static SetPage p;
    SetView v = {&s, &pr, TAB_GAME, KIT_NONE, true};
    SetPage_Layout(&v, &p);
    const KitItem *it = Kit_Get(&p.list, SID_QUEUE);
    const SetRow *row = SetPage_RowOf(&p, SID_QUEUE);
    CHECK(it && it->kind == KIT_STEPPER && it->value == 5 && it->min == 1 && it->max == 6 && strcmp(it->label, "5 pieces") == 0 && row &&
              strcmp(row->label, "Next queue") == 0,
          "queue: Settings > Game has a Next queue stepper, 1 to 6, at 5");
    KitEvent ev = {KIT_EV_CHANGE, SID_QUEUE, 3};
    SetEffect e = SetPage_Apply(&s, &pr, &ev);
    CHECK(e.kind == SE_CHANGED && e.config && s.nextQueue == 3, "queue: a change sets it and saves config.ini");
    ev.value = 99;
    SetPage_Apply(&s, &pr, &ev);
    CHECK(s.nextQueue == 6, "queue: clamped to six");
    s.nextQueue = 1;
    SetPage_Layout(&v, &p);
    it = Kit_Get(&p.list, SID_QUEUE);
    CHECK(it && strcmp(it->label, "1 piece") == 0, "queue: at one it says piece");
}

/* ------------------------------------------------------------- Rotate 180 */

static bool sDown[512], sPressed[512];
static bool KDown(int code) { return code > 0 && code < 512 && sDown[code]; }
static bool KPressed(int code) { return code > 0 && code < 512 && sPressed[code]; }
static const KeyReader kKb = {KDown, KPressed};
static int Code(const char *name) {
    if (strcmp(name, "V") == 0) return 86;
    if (strcmp(name, "UP") == 0) return 265;
    if (strcmp(name, "SPACE") == 0) return 32;
    if (strcmp(name, "ESCAPE") == 0) return 256;
    return name[0] >= 'A' && name[0] <= 'Z' && !name[1] ? name[0] : 0;
}

static void test_rotate_180(void) {
    Game g = Modern(10, 20, 13);
    Place(&g, PIECE_T, 0, 3, BOARD_HIDDEN + 4);
    CHECK(Game_Rotate(&g, 2) && g.rotation == 2 && g.px == 3 && g.py == BOARD_HIDDEN + 4 && g.lastKick == 0 && g.justRotated,
          "180: a half turn in the open turns in place");
    CHECK(Game_Rotate(&g, 2) && g.rotation == 0, "180: and back");
    Place(&g, PIECE_L, 1, 3, BOARD_HIDDEN + 4);
    CHECK(Game_Rotate(&g, 2) && g.rotation == 3, "180: R to L");
    /* On the floor a T pointing up cannot point down in place (its stem
     * would be in the floor): the 180 list's second offset lifts it one. */
    int floorTop = Board_TotalH(&g.board) - 2;
    Place(&g, PIECE_T, 0, 3, floorTop);
    CHECK(Game_Rotate(&g, 2) && g.rotation == 2 && g.py == floorTop - 1 && g.lastKick == 1 && g.lastMoveWasTurn, "180: on the floor it kicks up one");
    Place(&g, PIECE_O, 0, 3, floorTop);
    CHECK(!Game_Rotate(&g, 2), "180: the O has no half turn to make");
    /* A half turn into a T-spin slot counts as a turn (the corner rule
     * decides; the fifth-kick upgrade is a quarter turn's alone). */
    TsdSlot(&g);
    int bottom = Board_TotalH(&g.board) - 1;
    Place(&g, PIECE_T, 0, 3, bottom - 2);
    CHECK(Game_Rotate(&g, 2) && g.rotation == 2 && g.py == bottom - 2 && Game_TSpinKind(&g) == TSPIN_FULL, "180: turned into the slot it is a T-spin");

    /* A half turn by the 180 list's fifth offset (index 4, R>L: two up) is
     * no fifth SRS kick: a T pointing right turns to point left, the first
     * four offsets blocked, and lands with three corners filled, the left
     * (front) top one open. A mini, not a full T-spin (3.6.1: index 4 of
     * the 180 list used to pass for the TST kick). */
    {
        int px = 3, py = bottom - 2;
        Game h = Modern(10, 20, 13);
        Row(&h, 4, ".....#....");   /* top right corner (and blocks offset 2) */
        Row(&h, 3, "###..#####");   /* the row the T completes */
        Row(&h, 2, "...#.#....");   /* bottom left and bottom right corners (the right one blocks offsets 1 and 3) */
        Row(&h, 1, "...#......");   /* blocks the turn in place */
        Row(&h, 0, "...#......");
        Place(&h, PIECE_T, 1, px, py);
        CHECK(Game_Rotate(&h, 2) && h.rotation == 3 && h.px == px && h.py == py - 2 && h.lastKick == TSPIN_FULL_KICK && h.lastTurnHalf,
              "180: with offsets 0 to 3 blocked the 180 list's index 4 (two up) places the T");
        CHECK(Game_TSpinKind(&h) == TSPIN_MINI, "180: a half turn's index 4 is not the fifth kick: three corners, front ones not both filled, is a mini");
        long before = h.score;
        Game_HardDrop(&h);
        CHECK(h.lastTSpin == TSPIN_MINI && h.lastClearCount == 1 && h.score - before == 200,
              "180: locked, it scores a T-spin mini single (200 at level 1), not a full T-spin single");
        /* The same spot reached by a quarter turn's fifth kick stays full
         * (the upgrade belongs to quarter turns). */
        Game_ConsumeFrameFlags(&h);
        Row(&h, 4, ".....#....");
        Row(&h, 3, "###..#####");
        Row(&h, 2, "...#.#....");
        Row(&h, 1, "...#......");
        Row(&h, 0, "...#......");
        Place(&h, PIECE_T, 3, px, py - 2);
        h.lastMoveWasTurn = true;
        h.lastKick = TSPIN_FULL_KICK;
        h.lastTurnHalf = false;
        CHECK(Game_TSpinKind(&h) == TSPIN_FULL, "180: (the same spot after a quarter turn's fifth kick is a full T-spin)");
    }

    /* The action: no key or button as Blockfall comes; bound, it turns. */
    Settings s;
    Settings_Default(&s);
    CHECK(strcmp(Controls_ActionLabel(ACT_ROTATE_180), "Rotate 180") == 0 && ACT_ROTATE_180 == 9 && ACT_ROTATE_180 == CONTROL_ACTIONS - 2,
          "180: the tenth action, after the nine (Undo came after it in 3.5.0)");
    CHECK(Controls_IsNone(Controls_KeyConst(&s, ACT_ROTATE_180, 0)) && Controls_IsNone(Controls_KeyConst(&s, ACT_ROTATE_180, 1)) &&
              Controls_Pad(&s, ACT_ROTATE_180, 0) == PAD_NONE && Controls_Pad(&s, ACT_ROTATE_180, 1) == PAD_NONE && Controls_AreDefault(&s),
          "180: unbound as Blockfall comes, on the keyboard and the pad");
    PlayKeys k;
    Actions_Keys(&s, Code, &k);
    CHECK(k.key[ACT_ROTATE_180][0] == 0 && k.pad[ACT_ROTATE_180][0] == PAD_NONE, "180: so the play keys have none for it");
    BindResult r = Controls_BindKey(&s, ACT_ROTATE_180, 0, "V");
    CHECK(r.kind == BIND_SET && strcmp(s.rotateHalf, "V") == 0 && !Controls_AreDefault(&s), "180: V binds to it");
    r = Controls_BindPad(&s, ACT_ROTATE_180, 0, PB_LT); /* a free button (RB is Undo's since 3.5.0) */
    CHECK(r.kind == BIND_SET && s.pad[ACT_ROTATE_180][0] == PB_LT, "180: and LT on the pad");
    Actions_Keys(&s, Code, &k);
    Input pad;
    memset(&pad, 0, sizeof(pad));
    pad.focused = true;
    PlayInput pi;
    memset(sDown, 0, sizeof(sDown));
    memset(sPressed, 0, sizeof(sPressed));
    sDown[86] = sPressed[86] = true;
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(pi.rotateHalf && !pi.rotate && !pi.rotateLeft, "180: V pressed is a half turn, not a quarter");
    sDown[86] = sPressed[86] = false;
    pad.pad[PB_LT] = pad.padDown[PB_LT] = true;
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(pi.rotateHalf, "180: and so is LT");
    /* A frame of play turns the piece. */
    g = Modern(10, 20, 13);
    Place(&g, PIECE_T, 0, 3, BOARD_HIDDEN + 4);
    PlayDriver d;
    memset(&d, 0, sizeof(d));
    PlayDriver_Start(&d, false, false);
    PlayIntent in;
    memset(&in, 0, sizeof(in));
    in.rotateHalf = true;
    PlayStepResult out;
    Play_Step(&d, &g, &in, FRAME, 1.0f, &out);
    CHECK(out.rotated && g.rotation == 2, "180: a frame with the half turn pressed turns the piece half way");
    /* Kept through READY like any turn. */
    g = Modern(10, 20, 13);
    Place(&g, PIECE_T, 0, 3, BOARD_HIDDEN + 4);
    PlayDriver_Start(&d, true, false);
    Play_Step(&d, &g, &in, FRAME, 1.0f, &out);
    CHECK(out.waiting && g.rotation == 0 && d.cue.turns == 1 && d.cue.turnDir[0] == 2, "180: pressed during READY it waits");
    memset(&in, 0, sizeof(in));
    for (int i = 0; i < 40 && d.cue.stage == CUE_READY; i++) Play_Step(&d, &g, &in, FRAME, 1.0f, &out);
    CHECK(g.rotation == 2, "180: and is made at GO");

    /* Optional: its only key clears, and a swap may take it. */
    CHECK(Controls_ClearKey(&s, ACT_ROTATE_180, 0) && Controls_IsNone(s.rotateHalf), "180: its only key can be cleared (no other action's can)");
    CHECK(Controls_ClearPad(&s, ACT_ROTATE_180, 0) && s.pad[ACT_ROTATE_180][0] == PAD_NONE, "180: and its only button");
    CHECK(!Controls_ClearKey(&s, ACT_HOLD, 0), "180: (Hold's only key still stays)");
    Controls_BindKey(&s, ACT_ROTATE_180, 0, "V");
    r = Controls_BindKey(&s, ACT_HOLD, 1, "V");
    CHECK(r.kind == BIND_SWAPPED && strcmp(s.hold2, "V") == 0 && Controls_IsNone(s.rotateHalf) && Controls_AllDifferent(&s),
          "180: V bound elsewhere moves away and leaves Rotate 180 with none (it is optional)");
    Controls_BindKey(&s, ACT_ROTATE_180, 0, "B");
    Controls_BindKey(&s, ACT_ROTATE_180, 1, "N");
    CHECK(Controls_ClearKey(&s, ACT_ROTATE_180, 0) && strcmp(s.rotateHalf, "N") == 0 && Controls_IsNone(s.rotateHalf2),
          "180: clearing its first key moves the second up");
    Controls_Reset(&s);
    CHECK(Controls_AreDefault(&s) && Controls_IsNone(s.rotateHalf), "180: Reset controls leaves it unbound");

    /* config.ini: written as NONE, read back, absent in an old file. */
    char path[800];
    snprintf(path, sizeof(path), "%s/blockfall", getenv("XDG_CONFIG_HOME"));
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/blockfall/config.ini", getenv("XDG_CONFIG_HOME"));
    FILE *f = fopen(path, "w");
    fprintf(f, "hold=C\nrotate_half=V\npad_rotate_half=RB\nnext_queue=3\n");
    fclose(f);
    CHECK(Settings_Load(&s) && strcmp(s.rotateHalf, "V") == 0 && Controls_IsNone(s.rotateHalf2) && s.pad[ACT_ROTATE_180][0] == PB_RB &&
              s.pad[ACT_ROTATE_180][1] == PAD_NONE && s.nextQueue == 3,
          "config 3.2: rotate_half, pad_rotate_half and next_queue read back");
    f = fopen(path, "w");
    fprintf(f, "hold=C\nnext_queue=0\n");
    fclose(f);
    CHECK(Settings_Load(&s) && Controls_IsNone(s.rotateHalf) && s.pad[ACT_ROTATE_180][0] == PAD_NONE && s.nextQueue == 1,
          "config 3.2: a file without them leaves Rotate 180 unbound; next_queue is clamped up to 1");
    f = fopen(path, "w");
    fprintf(f, "next_queue=42\n");
    fclose(f);
    CHECK(Settings_Load(&s) && s.nextQueue == 6, "config 3.2: and down to 6");
    s.nextQueue = 4;
    snprintf(s.rotateHalf, KEYNAME_LEN, "V");
    Settings back;
    CHECK(Settings_Save(&s) && Settings_Load(&back) && memcmp(&s, &back, sizeof(s)) == 0, "config 3.2: saved and read back exactly");
    char text[8192];
    f = fopen(path, "r");
    size_t n = fread(text, 1, sizeof(text) - 1, f);
    text[n] = '\0';
    fclose(f);
    CHECK(strstr(text, "rotate_half=V\n") && strstr(text, "rotate_half2=NONE\n") && strstr(text, "pad_rotate_half=NONE\n") &&
              strstr(text, "pad_rotate_half2=NONE\n") && strstr(text, "next_queue=4\n") && strstr(text, "hold=C\n"),
          "config 3.2: the new lines are written after every old one");
    remove(path);
    /* The Controls table: ten rows, the last above Reset controls. */
    KitRect last = SetPage_CapRect(ACT_ROTATE_180, 3);
    CHECK(last.y + last.h <= SET_RESET_Y && SetPage_IsCap(SID_CAP_OF(ACT_ROTATE_180, 3)) && SetPage_CapAction(SID_CAP_OF(ACT_ROTATE_180, 0)) == ACT_ROTATE_180,
          "180: its caps sit above Reset controls");
    ControlWords w;
    Settings_Default(&s);
    Controls_Words(&s, &w);
    CHECK(strcmp(w.rotateHalf, "-") == 0 && strcmp(w.rotateHalfName, "none") == 0 && w.padRotateHalf[0] == '\0', "180: its words say none until it is set");
}

/* ----------------------------------------- the classic board's fresh table */

static void WriteText(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs(text, f);
    fclose(f);
}

static bool ReadText(const char *path, char *out, size_t size) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    size_t n = fread(out, 1, size - 1, f);
    out[n] = '\0';
    fclose(f);
    return true;
}

static void test_old_rules_read_only(void) {
    const char *data = getenv("XDG_DATA_HOME");
    char dir[700], shared[800], save[700];
    snprintf(dir, sizeof(dir), "%s/ghost-launcher", data);
    mkdir(dir, 0755);
    snprintf(dir, sizeof(dir), "%s/ghost-launcher/scores", data);
    mkdir(dir, 0755);
    snprintf(shared, sizeof(shared), "%s/ghost-launcher/scores/blockfall.txt", data);
    Persist_DataFile("save.dat", save, sizeof(save));
    /* The owner's files as they are: the old table's classic top and
     * save.dat's 74 372. */
    WriteText(shared, "# Ghost Arcade scores for blockfall: mode|username|score|date\nmarathon|TRG|12248|2026-09-19\nsprint|TRG|7620|2026-09-20\n");
    WriteText(save, "high_score=74372\nhigh_level=11\nhigh_lines=101\n");
    struct stat before;
    stat(save, &before);

    Game g;
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, true}, 1, 0, 1, 0);
    g.score = 500;
    g.piecesLocked = 9;
    RunRecord r = Record_Classify(&g, false);
    CHECK(r.counts && r.toBoard && !r.toSave && strcmp(r.key, "marathon-10x20") == 0, "old rules: a classic Marathon run goes to marathon-10x20 and never to save.dat");
    bool wentShared = false;
    CHECK(Record_Submit(&r, "TRG", "2026-10-20", &wentShared) == 1 && wentShared, "old rules: it is first on the fresh board");
    char text[4096];
    CHECK(ReadText(shared, text, sizeof(text)) && strstr(text, "marathon-10x20|TRG|500|2026-10-20") && strstr(text, "marathon|TRG|12248|2026-09-19"),
          "old rules: the shared table gains the new row and keeps the old marathon row as it was");
    GhostScore rows[GHOSTLINK_TOP_N];
    CHECK(Record_LoadBoard("marathon", false, rows, GHOSTLINK_TOP_N) == 1 && rows[0].score == 12248, "old rules: the old board still reads one row");
    CHECK(Record_LoadBoard("marathon-10x20", false, rows, GHOSTLINK_TOP_N) == 1 && rows[0].score == 500, "old rules: the fresh board reads the run");
    CHECK(Record_LoadBest(MODE_MARATHON, 10, 20, 74372) == 500, "old rules: the classic Marathon's Best is the fresh board's: 500, not 74 372 or 12 248");
    int rank = 0;
    CHECK(Record_LoadYourBest(MODE_MARATHON, 10, 20, "TRG", 74372, &rank) == 500 && rank == 1, "old rules: and so is Your best");
    struct stat after;
    stat(save, &after);
    SaveData sd;
    CHECK(SaveData_Load(&sd) && sd.highScore == 74372 && before.st_mtime == after.st_mtime && before.st_size == after.st_size,
          "old rules: save.dat still says 74 372 and was not touched");
    CHECK(Record_ClassicBest(sd.highScore, 12248) == 74372, "old rules: the Classic (old rules) board's best to show is save.dat's 74 372");
    /* Nothing is written to the old board whatever the run. */
    bool never = true;
    static const GameMode kModes[] = {MODE_MARATHON, MODE_SPRINT, MODE_ULTRA, MODE_POWER};
    for (int i = 0; i < 4; i++) {
        Game_InitConfig(&g, (GameConfig){kModes[i], 10, 20, true}, 1, 0, 1, 0);
        g.score = 900;
        g.piecesLocked = 3;
        g.won = true;
        g.elapsed = 50.0f;
        r = Record_Classify(&g, false);
        if (strcmp(r.key, "marathon") == 0 || strcmp(r.key, "sprint") == 0 || r.toSave) never = false;
    }
    CHECK(never, "old rules: no mode's run is keyed to an old board or save.dat");
    BoardSize b[4];
    CHECK(Record_BoardsWithRows(MODE_MARATHON, true, b, 4) == 1 && b[0].w == 10 && b[0].h == 20, "old rules: the Scores browser finds the old board to list");
    remove(shared);
    remove(save);
}

/* ------------------------------------------------- callouts and sounds */

static int Texts(const HudFx *h, char out[][24]) {
    const Callout *c[CALLOUT_MAX];
    int n = HudFx_Callouts(h, c);
    for (int i = 0; i < n; i++) memcpy(out[i], c[i]->text, 24);
    return n;
}

static void test_callouts_and_sounds(void) {
    Game g = Modern(10, 20, 21);
    HudFx h;
    HudFx_Reset(&h, &g);
    SoundPlan p;
    SoundPlan_Reset(&p, &g);
    SoundEvents quiet;
    memset(&quiet, 0, sizeof(quiet));
    SoundCue c[SOUND_CUES];
    char t[3][24];
    int bottom = Board_TotalH(&g.board) - 1;

    /* A T-spin double: its word, gold, its popup, the sparkle pitched up
     * and the clear's sound. (The turn's own flags are consumed first, as
     * they would be a frame earlier in play.) */
    TsdSlot(&g);
    Place(&g, PIECE_T, 1, 3, bottom - 2);
    Game_Rotate(&g, 1);
    Game_ConsumeFrameFlags(&g);
    Game_HardDrop(&g);
    HudFx_React(&h, &g, false);
    SoundPlan_React(&p, &g, &quiet);
    int n = SoundPlan_Take(&p, c, SOUND_CUES);
    const Callout *cs[CALLOUT_MAX];
    HudFx_Callouts(&h, cs);
    CHECK(Texts(&h, t) == 1 && strcmp(t[0], "T-SPIN DOUBLE") == 0 && cs[0]->tint == TINT_GOLD && strcmp(cs[0]->shortText, "T-SPIN") == 0,
          "callouts: a T-spin double says T-SPIN DOUBLE in gold (T-SPIN where that will not fit)");
    CHECK(h.pops == 1 && strcmp(h.pop[0].text, "+1 200") == 0 && h.pop[0].gold, "callouts: and pops +1 200 in gold");
    bool accent = false, big = false;
    for (int i = 0; i < n; i++) {
        if (c[i].weight == SND_ACCENT && c[i].how == SND_PITCHED && c[i].id == SFX_B2B && NEAR(c[i].arg, TSPIN_PITCH)) accent = true;
        if (c[i].weight == SND_BIG && c[i].id == SFX_CLEAR) big = true;
    }
    CHECK(n == 2 && accent && big, "sounds: a T-spin's accent is the sparkle pitched up, its big sound the clear's");
    HudFx_Tick(&h, FRAME, true);
    Game_ConsumeFrameFlags(&g);
    /* A T-spin with no lines: T-SPIN and +400, from a fresh game. */
    g = Modern(10, 20, 21);
    HudFx_Reset(&h, &g);
    TsdSlot(&g);
    Row(&g, 0, "####.####.");
    Row(&g, 1, "###...###.");
    Place(&g, PIECE_T, 1, 3, bottom - 2);
    Game_Rotate(&g, 1);
    Game_ConsumeFrameFlags(&g);
    Game_HardDrop(&g);
    HudFx_React(&h, &g, false);
    CHECK(Texts(&h, t) == 1 && strcmp(t[0], "T-SPIN") == 0 && h.pops == 1 && strcmp(h.pop[0].text, "+400") == 0 &&
              NEAR(h.pop[0].row, (float)(g.lastLockY + 1 - BOARD_HIDDEN) + 0.5f),
          "callouts: a T-spin with no lines says T-SPIN and pops +400 from the T's row");
    Game_ConsumeFrameFlags(&g);
    /* A mini, then a back-to-back T-spin double after it: BACK-TO-BACK
     * under its word, the plain sparkle as the accent. */
    g = Modern(10, 20, 21);
    HudFx_Reset(&h, &g);
    SoundPlan_Reset(&p, &g);
    Row(&g, 0, ".#########");
    Place(&g, PIECE_T, 0, 0, bottom - 2);
    Game_Rotate(&g, 1);
    Game_ConsumeFrameFlags(&g);
    Game_HardDrop(&g);
    HudFx_React(&h, &g, false);
    HudFx_Callouts(&h, cs);
    CHECK(Texts(&h, t) == 1 && strcmp(t[0], "T-SPIN MINI") == 0 && cs[0]->tint == TINT_LIGHT && strcmp(cs[0]->shortText, "MINI") == 0,
          "callouts: a mini says T-SPIN MINI, not gold");
    HudFx_Tick(&h, FRAME, true);
    Game_ConsumeFrameFlags(&g);
    TsdSlot(&g);
    Place(&g, PIECE_T, 1, 3, bottom - 2);
    Game_Rotate(&g, 1);
    Game_ConsumeFrameFlags(&g);
    Game_HardDrop(&g);
    HudFx_React(&h, &g, false);
    SoundPlan_React(&p, &g, &quiet);
    n = SoundPlan_Take(&p, c, SOUND_CUES);
    CHECK(g.lastWasB2B && Texts(&h, t) == 2 && strcmp(t[0], "T-SPIN DOUBLE") == 0 && strcmp(t[1], "BACK-TO-BACK") == 0,
          "callouts: T-SPIN DOUBLE over BACK-TO-BACK after the mini armed it");
    CHECK(n == 2 && c[0].how == SND_PLAY && c[0].id == SFX_B2B && c[0].weight == SND_ACCENT, "sounds: back to back, the sparkle is the plain one");
    HudFx_Tick(&h, FRAME, true);
    Game_ConsumeFrameFlags(&g);
    /* A perfect clear: PERFECT CLEAR above the clear's word, the quad's
     * sound pitched up as the one big sound. */
    g = Modern(10, 20, 21);
    HudFx_Reset(&h, &g);
    SoundPlan_Reset(&p, &g);
    for (int y = 0; y < 4; y++) Row(&g, y, "#########.");
    Place(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
    Game_HardDrop(&g);
    HudFx_React(&h, &g, false);
    SoundPlan_React(&p, &g, &quiet);
    n = SoundPlan_Take(&p, c, SOUND_CUES);
    HudFx_Callouts(&h, cs);
    CHECK(Texts(&h, t) == 2 && strcmp(t[0], "PERFECT CLEAR") == 0 && strcmp(t[1], "QUAD") == 0 && cs[0]->kind == CALL_PERFECT && cs[0]->tint == TINT_GOLD,
          "callouts: PERFECT CLEAR over QUAD, in gold");
    CHECK(HudFx_CalloutMaxSize(&h, cs[0]) == 32 && HudFx_CalloutMaxSize(&h, cs[1]) == 16, "callouts: PERFECT CLEAR is the headline");
    big = false;
    int bigs = 0;
    for (int i = 0; i < n; i++) {
        if (c[i].weight == SND_BIG) bigs++;
        if (c[i].weight == SND_BIG && c[i].how == SND_PITCHED && c[i].id == SFX_QUAD && NEAR(c[i].arg, PERFECT_CLEAR_PITCH)) big = true;
    }
    CHECK(bigs == 1 && big, "sounds: a perfect clear's one big sound is the quad's, higher");
    HudFx_Tick(&h, FRAME, true);
    Game_ConsumeFrameFlags(&g);
    /* A perfect clear by a single: PERFECT CLEAR alone (a single has no word). */
    Row(&g, 0, "....######");
    Place(&g, PIECE_I, 0, 0, BOARD_HIDDEN);
    Game_HardDrop(&g);
    HudFx_React(&h, &g, false);
    CHECK(Texts(&h, t) == 1 && strcmp(t[0], "PERFECT CLEAR") == 0, "callouts: a perfect clear by a single says PERFECT CLEAR alone");
    Game_ConsumeFrameFlags(&g);

    /* The results card counts T-spins where Holds was. */
    g = Modern(10, 20, 21);
    g.stats.tspins = 3;
    g.piecesLocked = 10;
    g.score = 5000;
    g.elapsed = 60.0f;
    RunFacts f = {false, 0, 0, 0, 0, NULL, "Classic 10x20"};
    ResultsCard card;
    Results_Build(&card, &g, &f);
    CHECK(strcmp(card.statLabel[8], "T-spins") == 0 && strcmp(card.statValue[8], "3") == 0, "results: the card counts the run's T-spins");
}

void test_modern_all(void) {
    test_srs_tables();
    test_srs_play();
    test_lock_reset_on_kick();
    test_tspin_double();
    test_tspin_mini();
    test_perfect_clear();
    test_seed_sequence();
    test_queue_layout();
    test_rotate_180();
    test_old_rules_read_only();
    test_callouts_and_sounds();
}
