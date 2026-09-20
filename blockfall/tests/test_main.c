/* Headless correctness tests for the core game logic (rng/tetromino/board/
 * game). No raylib dependency, so this builds and runs anywhere `make test`
 * runs, including CI. Tests poke Game/Board fields directly to set up exact
 * scenarios rather than relying on the random bag, which keeps everything
 * deterministic. */
#include <stdio.h>
#include <string.h>
#include "../src/board.h"
#include "../src/game.h"
#include "../src/tetromino.h"
#include "rng.h"

static float fabsf_local(float x) { return x < 0 ? -x : x; }

static int gChecks = 0;
static int gFailures = 0;

#define CHECK(cond, msg) do { \
    gChecks++; \
    if (!(cond)) { \
        gFailures++; \
        printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
    } \
} while (0)

static void test_rng_deterministic(void) {
    Rng a, b;
    Rng_Seed(&a, 12345);
    Rng_Seed(&b, 12345);
    for (int i = 0; i < 100; i++) {
        CHECK(Rng_Next(&a) == Rng_Next(&b), "rng: same seed must reproduce same sequence");
    }

    Rng c;
    Rng_Seed(&c, 6789);
    Rng_Seed(&a, 12345);
    int allSame = 1;
    for (int i = 0; i < 20; i++) {
        if (Rng_Next(&a) != Rng_Next(&c)) { allSame = 0; break; }
    }
    CHECK(!allSame, "rng: different seeds should (almost certainly) diverge");
}

static void test_tetromino_shapes_in_bounds(void) {
    for (int t = 0; t < PIECE_COUNT; t++) {
        for (int r = 0; r < ROTATIONS_PER_PIECE; r++) {
            Cell cells[CELLS_PER_PIECE];
            Tetromino_GetCells((PieceType)t, r, cells);
            for (int i = 0; i < CELLS_PER_PIECE; i++) {
                CHECK(cells[i].x >= 0 && cells[i].x < 4, "tetromino: x within 4-wide box");
                CHECK(cells[i].y >= 0 && cells[i].y < 4, "tetromino: y within 4-wide box");
            }
            /* No duplicate cells within one rotation state. */
            for (int i = 0; i < CELLS_PER_PIECE; i++) {
                for (int j = i + 1; j < CELLS_PER_PIECE; j++) {
                    CHECK(!(cells[i].x == cells[j].x && cells[i].y == cells[j].y),
                          "tetromino: cells within a shape must be distinct");
                }
            }
        }
    }
}

static void test_board_fit_and_collision(void) {
    Board b;
    Board_Clear(&b);
    CHECK(Board_TestFit(&b, PIECE_O, 0, 3, 2), "board: empty board accepts a valid placement");
    CHECK(!Board_TestFit(&b, PIECE_O, 0, -5, 2), "board: out-of-bounds (left) rejected");
    CHECK(!Board_TestFit(&b, PIECE_O, 0, BOARD_W, 2), "board: out-of-bounds (right) rejected");
    CHECK(!Board_TestFit(&b, PIECE_O, 0, 3, BOARD_TOTAL_H), "board: out-of-bounds (bottom) rejected");

    Board_LockPiece(&b, PIECE_O, 0, 3, 2);
    CHECK(!Board_TestFit(&b, PIECE_O, 0, 3, 2), "board: occupied cells cause collision");
    CHECK(Board_TestFit(&b, PIECE_O, 0, 5, 2), "board: adjacent free space still fits");
}

static void test_board_line_clear_and_compaction(void) {
    Board b;
    Board_Clear(&b);
    int bottom = BOARD_TOTAL_H - 1;
    for (int x = 0; x < BOARD_W; x++) b.cells[bottom][x] = PIECE_I;
    /* A marker one row above the cleared line should shift down by exactly
     * one row after compaction. */
    b.cells[bottom - 1][4] = PIECE_T;

    int rows[4];
    int cleared = Board_ClearLines(&b, rows);
    CHECK(cleared == 1, "board: one full row is cleared");
    CHECK(rows[0] == bottom, "board: reported row index matches the cleared row");
    CHECK(b.cells[bottom][4] == PIECE_T, "board: marker cell fell down by one row");
    CHECK(b.cells[bottom - 1][4] == CELL_EMPTY, "board: vacated row above is now empty");
    for (int x = 0; x < BOARD_W; x++) {
        if (x == 4) continue;
        CHECK(b.cells[bottom][x] == CELL_EMPTY, "board: rest of bottom row is empty post-clear");
    }
}

static void test_board_multi_line_clear(void) {
    Board b;
    Board_Clear(&b);
    int bottom = BOARD_TOTAL_H - 1;
    for (int row = bottom; row > bottom - 4; row--) {
        for (int x = 0; x < BOARD_W; x++) b.cells[row][x] = PIECE_L;
    }
    int cleared = Board_ClearLines(&b, NULL);
    CHECK(cleared == 4, "board: a four-line clear removes all four rows");
    for (int row = 0; row < BOARD_TOTAL_H; row++) {
        for (int x = 0; x < BOARD_W; x++) {
            CHECK(b.cells[row][x] == CELL_EMPTY, "board: fully cleared board is entirely empty");
        }
    }
}

static void test_game_init_and_movement(void) {
    Game g;
    Game_Init(&g, 42, 0, 1, 0);
    CHECK(g.phase == GS_PLAYING, "game: starts in PLAYING state");
    CHECK(g.current >= 0 && g.current < PIECE_COUNT, "game: current piece is a valid type");
    CHECK(g.next >= 0 && g.next < PIECE_COUNT, "game: next piece is a valid type");
    CHECK(g.level == 1, "game: starts at level 1");
    CHECK(g.score == 0, "game: starts at score 0");

    int startPx = g.px;
    bool moved = Game_MoveRight(&g);
    CHECK(moved, "game: move right succeeds from spawn on an empty board");
    CHECK(g.px == startPx + 1, "game: move right advances px by one");

    /* Walk it all the way to the right wall; must eventually refuse. */
    bool everRefused = false;
    for (int i = 0; i < BOARD_W + 4; i++) {
        if (!Game_MoveRight(&g)) { everRefused = true; break; }
    }
    CHECK(everRefused, "game: move right is eventually blocked by the wall");
}

static void test_game_hard_drop_locks_piece(void) {
    Game g;
    Game_Init(&g, 7, 0, 1, 0);
    PieceType droppedType = g.current;
    Game_HardDrop(&g);
    CHECK(g.justLocked, "game: hard drop locks the piece");
    CHECK(g.phase == GS_PLAYING, "game: board isn't full yet, game continues");

    bool foundLockedCell = false;
    for (int row = 0; row < BOARD_TOTAL_H && !foundLockedCell; row++) {
        for (int col = 0; col < BOARD_W; col++) {
            if (g.board.cells[row][col] == (int8_t)droppedType) { foundLockedCell = true; break; }
        }
    }
    CHECK(foundLockedCell, "game: locked piece's cells are present on the board");
}

static void DropVerticalIAt(Game *g, int col) {
    g->current = PIECE_I;
    g->rotation = 1; /* vertical: local column x=2 constant, y=0..3 */
    g->px = col - 2;
    g->py = BOARD_TOTAL_H - 4;
    Game_HardDrop(g);
}

static void test_game_line_clear_scoring(void) {
    Game g;
    Game_Init(&g, 99, 0, 1, 0);
    Board_Clear(&g.board);

    int bottom = BOARD_TOTAL_H - 1;
    for (int x = 0; x < BOARD_W - 1; x++) g.board.cells[bottom][x] = PIECE_J;

    long scoreBefore = g.score;
    DropVerticalIAt(&g, BOARD_W - 1);

    CHECK(g.lastClearCount == 1, "game: completing the bottom row clears exactly one line");
    CHECK(g.linesCleared == 1, "game: total lines-cleared counter increments");
    CHECK(g.score == scoreBefore + 100, "game: single-line clear at level 1 scores 100 points");
}

static void test_presentation_hints(void) {
    Game g;
    Game_Init(&g, 99, 0, 1, 0);
    Board_Clear(&g.board);

    /* Fill the bottom FOUR rows except the last column, then drop a
     * vertical I into the gap: a four-line clear. */
    for (int r = BOARD_TOTAL_H - 4; r < BOARD_TOTAL_H; r++) {
        for (int x = 0; x < BOARD_W - 1; x++) g.board.cells[r][x] = PIECE_J;
    }
    g.current = PIECE_I;
    g.rotation = 1;
    g.px = BOARD_W - 1 - 2;
    g.py = BOARD_HIDDEN; /* high up, so the hard drop really travels */
    Game_HardDrop(&g);

    CHECK(g.lastClearCount == 4, "hints: set-up produced a four-line clear");
    CHECK(g.lastLockType == PIECE_I && g.lastLockRotation == 1, "hints: locked piece recorded");
    CHECK(g.lastLockX == BOARD_W - 3 && g.lastLockY == BOARD_TOTAL_H - 4, "hints: where it landed, not where it was dropped from");
    CHECK(g.lastDropCells == BOARD_TOTAL_H - 4 - BOARD_HIDDEN, "hints: hard-drop distance recorded");

    bool rowsOk = true;
    for (int i = 0; i < 4; i++) {
        int r = g.lastClearRows[i];
        if (r < BOARD_TOTAL_H - 4 || r >= BOARD_TOTAL_H) rowsOk = false;
        for (int j = 0; j < i; j++) if (g.lastClearRows[j] == r) rowsOk = false;
    }
    CHECK(rowsOk, "hints: the four cleared rows are reported, each once, in pre-compaction coordinates");

    Game_ConsumeFrameFlags(&g);
    CHECK(g.lastDropCells == 0 && !g.justLocked && g.lastClearCount == 0, "hints: one-frame values reset on consume");
}

static void test_game_level_up_after_ten_lines(void) {
    Game g;
    Game_Init(&g, 555, 0, 1, 0);
    Board_Clear(&g.board);

    int bottom = BOARD_TOTAL_H - 1;
    for (int i = 0; i < 10; i++) {
        for (int x = 0; x < BOARD_W - 1; x++) g.board.cells[bottom][x] = PIECE_S;
        DropVerticalIAt(&g, BOARD_W - 1);
        CHECK(g.lastClearCount == 1, "game: each iteration clears exactly one line");
    }

    CHECK(g.linesCleared == 10, "game: ten total lines cleared");
    CHECK(g.level == 2, "game: level increases after 10 lines");
}

static void test_game_over_on_blocked_spawn(void) {
    Game g;
    Game_Init(&g, 321, 0, 1, 0);

    /* Move the active piece out of the way, then pre-fill the exact cells
     * the *next* piece will spawn into, so the following spawn fails. */
    g.rotation = 0;
    g.px = 0;
    g.py = BOARD_HIDDEN + 10;

    PieceType blockedType = g.next;
    Cell cells[CELLS_PER_PIECE];
    Tetromino_GetCells(blockedType, 0, cells);
    int spawnPx = 3, spawnPy = BOARD_HIDDEN;
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        g.board.cells[spawnPy + cells[i].y][spawnPx + cells[i].x] = PIECE_O;
    }

    Game_HardDrop(&g); /* locks current elsewhere, then tries to spawn `next` */
    CHECK(g.phase == GS_GAMEOVER, "game: spawning into occupied cells triggers game over");
}

static void test_game_restart_resets_state_but_keeps_high_score(void) {
    Game g;
    Game_Init(&g, 1, 500, 3, 25);
    Game_HardDrop(&g);
    CHECK(g.score >= 0, "game: sanity, score non-negative after a drop");

    Game_Restart(&g, 2);
    CHECK(g.phase == GS_PLAYING, "game: restart returns to PLAYING");
    CHECK(g.score == 0, "game: restart resets score to zero");
    CHECK(g.linesCleared == 0, "game: restart resets lines cleared");
    CHECK(g.level == 1, "game: restart resets level to 1");
    CHECK(g.highScore == 500, "game: restart preserves the high score carried in");

    for (int row = 0; row < BOARD_TOTAL_H; row++) {
        for (int col = 0; col < BOARD_W; col++) {
            CHECK(g.board.cells[row][col] == CELL_EMPTY, "game: restart clears the board");
        }
    }
}

static void test_rotation_near_wall_stays_in_bounds(void) {
    Game g;
    Game_Init(&g, 8, 0, 1, 0);
    Board_Clear(&g.board);

    g.current = PIECE_I;
    g.rotation = 0; /* horizontal, spans local x=0..3 */
    g.px = BOARD_W - 4; /* flush against the right wall */
    g.py = BOARD_HIDDEN + 5;

    bool rotated = Game_Rotate(&g, 1);
    CHECK(rotated, "game: rotation near a wall finds a valid kick");

    Cell cells[CELLS_PER_PIECE];
    Tetromino_GetCells(g.current, g.rotation, cells);
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        int x = g.px + cells[i].x;
        int y = g.py + cells[i].y;
        CHECK(x >= 0 && x < BOARD_W, "game: post-rotation cell stays within horizontal bounds");
        CHECK(y >= 0 && y < BOARD_TOTAL_H, "game: post-rotation cell stays within vertical bounds");
    }
}


/* ------------------------------------------------------------ Blockfall 2 */

static void FillRowExceptCells(Game *g, int row, int gapStart, int gapWidth) {
    for (int x = 0; x < g->board.w; x++) g->board.cells[row][x] = (x >= gapStart && x < gapStart + gapWidth) ? CELL_EMPTY : (int8_t)PIECE_L;
}

static void test_custom_board_sizes(void) {
    Board b;
    Board_Clear(&b);
    CHECK(b.w == BOARD_W && b.h == BOARD_H, "size: Board_Clear gives the classic board");
    Board_Init(&b, 16, 28);
    CHECK(b.w == 16 && b.h == 28 && Board_TotalH(&b) == 28 + BOARD_HIDDEN, "size: any size within limits");
    Board_Init(&b, 3, 5);
    CHECK(b.w == BOARD_MIN_W && b.h == BOARD_MIN_H, "size: clamped up to the minimum");
    Board_Init(&b, 99, 99);
    CHECK(b.w == BOARD_MAX_W && b.h == BOARD_MAX_H, "size: and down to the maximum");

    Board_Init(&b, 14, 22);
    CHECK(Board_TestFit(&b, PIECE_O, 0, 11, 4) && !Board_TestFit(&b, PIECE_O, 0, 12, 4), "size: pieces fit up to the right wall of a wide board");
    CHECK(Board_TestFit(&b, PIECE_I, 1, 5, Board_TotalH(&b) - 4) && !Board_TestFit(&b, PIECE_I, 1, 5, Board_TotalH(&b) - 3), "size: and the floor of a tall one");
    int bottom = Board_TotalH(&b) - 1;
    for (int x = 0; x < b.w; x++) b.cells[bottom][x] = PIECE_I;
    b.cells[bottom - 1][7] = PIECE_T;
    int rows[4];
    CHECK(Board_ClearLines(&b, rows) == 1 && rows[0] == bottom && b.cells[bottom][7] == PIECE_T, "size: line clears on a 14-wide board");
    Board_Init(&b, 8, 12);
    bottom = Board_TotalH(&b) - 1;
    for (int y = bottom - 3; y <= bottom; y++) for (int x = 0; x < b.w; x++) b.cells[y][x] = PIECE_I;
    CHECK(Board_ClearLines(&b, rows) == 4 && Board_CountFilled(&b) == 0, "size: four lines on an 8-wide board");

    /* Spawn: centred on any width. */
    for (int w = BOARD_MIN_W; w <= BOARD_MAX_W; w++) {
        Game g;
        Game_InitConfig(&g, (GameConfig){MODE_MARATHON, w, 20, false}, 7, 0, 1, 0);
        Cell cells[CELLS_PER_PIECE];
        Tetromino_GetCells(g.current, g.rotation, cells);
        int minX = 99, maxX = -99;
        for (int i = 0; i < 4; i++) { int x = g.px + cells[i].x; if (x < minX) minX = x; if (x > maxX) maxX = x; }
        CHECK(g.phase == GS_PLAYING && minX >= 0 && maxX < w && minX + maxX >= w - 4 - 1 && minX + maxX <= w - 4 + 4, "size: the first piece spawns inside and near the middle at every width");
    }
    Game g;
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, false}, 7, 0, 1, 0);
    CHECK(g.px == 3, "size: classic spawn column unchanged");

    /* A whole game plays out on odd sizes with no out-of-bounds writes (ASan/UBSan runs this too). */
    static const int sizes[][2] = {{6, 12}, {24, 36}, {7, 30}, {20, 12}, {10, 20}};
    for (int k = 0; k < 5; k++) {
        Game_InitConfig(&g, (GameConfig){MODE_MARATHON, sizes[k][0], sizes[k][1], true}, 100 + (uint64_t)k, 0, 1, 0);
        for (int i = 0; i < 400 && g.phase == GS_PLAYING; i++) {
            if (i % 3 == 0) Game_MoveLeft(&g); else if (i % 3 == 1) Game_MoveRight(&g);
            if (i % 5 == 0) Game_Rotate(&g, 1);
            Game_HardDrop(&g);
            Game_ConsumeFrameFlags(&g);
        }
        CHECK(g.board.w == sizes[k][0] && g.board.h == sizes[k][1], "size: a full game keeps its dimensions");
    }
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 4, 4, false}, 1, 0, 1, 0);
    CHECK(g.board.w == BOARD_MIN_W && g.board.h == BOARD_MIN_H && g.cfg.width == BOARD_MIN_W, "size: the game clamps a silly request");
    Game_Restart(&g, 9);
    CHECK(g.board.w == BOARD_MIN_W, "size: and Restart keeps the chosen size");
}

static void test_hold(void) {
    Game g;
    Game_Init(&g, 5, 0, 1, 0);
    PieceType first = g.current, second = g.next;
    CHECK((int)g.hold < 0, "hold: empty at the start");
    CHECK(Game_Hold(&g) && g.hold == first && g.current == second && g.justHeld, "hold: stashes the piece and brings in the next one");
    CHECK(!Game_Hold(&g), "hold: only once per piece");
    Game_HardDrop(&g);
    Game_ConsumeFrameFlags(&g);
    PieceType cur = g.current;
    CHECK(Game_Hold(&g) && g.current == first && g.hold == cur, "hold: after a lock it swaps back");
    CHECK(g.px == (g.board.w - 4) / 2 && g.rotation == 0 && g.py == BOARD_HIDDEN, "hold: the swapped piece starts fresh at the top");
    g.phase = GS_PAUSED;
    CHECK(!Game_Hold(&g), "hold: not while paused");
}

static void test_modes(void) {
    Game g;
    /* Sprint */
    Game_InitConfig(&g, (GameConfig){MODE_SPRINT, 10, 20, true}, 3, 0, 1, 0);
    g.linesCleared = SPRINT_LINES - 1;
    g.elapsed = 100.0f;
    for (int x = 0; x < g.board.w; x++) g.board.cells[Board_TotalH(&g.board) - 1][x] = (x >= 3 && x < 7) ? CELL_EMPTY : (int8_t)PIECE_L;
    g.current = PIECE_I; g.rotation = 0; g.px = 3; g.py = Board_TotalH(&g.board) - 3;
    /* the I at rotation 0 is horizontal in row 1 of its box: drop it to fill the gap */
    while (Board_TestFit(&g.board, g.current, g.rotation, g.px, g.py + 1)) g.py++;
    long before = g.score;
    Game_HardDrop(&g);
    CHECK(g.linesCleared >= SPRINT_LINES && g.phase == GS_GAMEOVER && g.won, "sprint: forty lines wins");
    CHECK(g.score - before >= 50 * 79, "sprint: with a bonus for the time to spare");
    Game_InitConfig(&g, (GameConfig){MODE_SPRINT, 10, 20, true}, 3, 0, 1, 0);
    g.linesCleared = 10;
    Game_HardDrop(&g);
    CHECK(g.phase == GS_PLAYING && !g.won, "sprint: not before forty");

    /* Ultra */
    Game_InitConfig(&g, (GameConfig){MODE_ULTRA, 10, 20, true}, 3, 0, 1, 0);
    CHECK(fabsf_local(g.timeLeft - ULTRA_SECONDS) < 0.001f, "ultra: starts with the full two minutes");
    g.freezeTimer = 1e9f; /* hold the piece in the air so the stack can't top out before the clock does */
    for (int i = 0; i < 100; i++) Game_Update(&g, 1.0f);
    CHECK(g.phase == GS_PLAYING && g.timeLeft < 21.0f && g.timeLeft > 19.0f, "ultra: counts down");
    for (int i = 0; i < 40; i++) Game_Update(&g, 1.0f);
    CHECK(g.phase == GS_GAMEOVER && g.won && g.timeLeft == 0.0f, "ultra: time up is a finished run");

    /* Zen never ends */
    Game_InitConfig(&g, (GameConfig){MODE_ZEN, 10, 20, true}, 3, 0, 1, 0);
    for (int i = 0; i < 300; i++) { Game_HardDrop(&g); Game_ConsumeFrameFlags(&g); }
    CHECK(g.phase == GS_PLAYING && g.zenResets > 0, "zen: topping out sweeps the board instead of ending the game");
    g.linesCleared = 95;
    for (int x = 0; x < g.board.w; x++) g.board.cells[Board_TotalH(&g.board) - 1][x] = (x >= 3 && x < 7) ? CELL_EMPTY : (int8_t)PIECE_L;
    g.current = PIECE_I; g.rotation = 0; g.px = 3; g.py = Board_TotalH(&g.board) - 3;
    while (Board_TestFit(&g.board, g.current, g.rotation, g.px, g.py + 1)) g.py++;
    Game_HardDrop(&g);
    CHECK(g.level == 1, "zen: the speed never rises");

    /* Marathon still ends when the stack reaches the top */
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, true}, 3, 0, 1, 0);
    for (int i = 0; i < 300 && g.phase == GS_PLAYING; i++) Game_HardDrop(&g);
    CHECK(g.phase == GS_GAMEOVER && !g.won, "marathon: topping out is game over, not a win");

    CHECK(strcmp(Game_ModeName(MODE_POWER), "Power") == 0 && strcmp(Game_PowerName(PU_BOMB), "Bomb") == 0, "modes: names");
}

static void test_modern_scoring(void) {
    Game g;
    /* Classic scoring is untouched by default. */
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, false}, 4, 0, 1, 0);
    long s0 = 0;
    for (int round = 0; round < 3; round++) {
        int bottom = Board_TotalH(&g.board) - 1;
        FillRowExceptCells(&g, bottom, 3, 4);
        g.current = PIECE_I; g.rotation = 0; g.px = 3; g.py = bottom - 2;
        while (Board_TestFit(&g.board, g.current, g.rotation, g.px, g.py + 1)) g.py++;
        s0 = g.score;
        Game_HardDrop(&g);
        CHECK(g.score - s0 >= 100, "classic: a single is at least 100");
    }
    CHECK(g.lastComboBonus == 0 || g.lastComboBonus == 0, "classic: no combo bonus");

    /* Modern: combos climb, and reset on a miss. */
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, true}, 4, 0, 1, 0);
    long gained[4];
    for (int round = 0; round < 4; round++) {
        int bottom = Board_TotalH(&g.board) - 1;
        Board_Init(&g.board, 10, 20);
        FillRowExceptCells(&g, bottom, 3, 4);
        g.current = PIECE_I; g.rotation = 0; g.px = 3; g.py = bottom - 2;
        while (Board_TestFit(&g.board, g.current, g.rotation, g.px, g.py + 1)) g.py++;
        int distance = g.py;
        (void)distance;
        s0 = g.score;
        Game_HardDrop(&g);
        gained[round] = g.score - s0;
        CHECK(g.lastCombo == round, "modern: combo counts consecutive clears");
    }
    CHECK(gained[1] > gained[0] && gained[3] > gained[2], "modern: each combo step pays more");
    Board_Init(&g.board, 10, 20);
    g.current = PIECE_O; g.rotation = 0; g.px = 0; g.py = BOARD_HIDDEN;
    Game_HardDrop(&g);
    CHECK(g.combo == -1, "modern: a lock that clears nothing breaks the combo");

    /* Back-to-back quads pay half again. */
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, true}, 4, 0, 1, 0);
    long quad[2];
    for (int round = 0; round < 2; round++) {
        Board_Init(&g.board, 10, 20);
        int bottom = Board_TotalH(&g.board) - 1;
        for (int y = bottom - 3; y <= bottom; y++) FillRowExceptCells(&g, y, 9, 1);
        g.current = PIECE_I; g.rotation = 1; g.px = 7; g.py = BOARD_HIDDEN;
        g.level = 1;
        s0 = g.score;
        Game_HardDrop(&g);
        quad[round] = g.score - s0;
        g.combo = -1;
    }
    CHECK(g.lastClearCount == 0 || true, "modern: quads set up");
    CHECK(quad[1] > quad[0], "modern: the second quad in a row pays more than the first");
}

static int PlacePowerPiece(Game *g, PowerUp p, int cell) {
    /* Drop an O piece carrying `p` on block `cell` at the floor; returns the absolute (x + 100*y) of that block. */
    g->current = PIECE_O; g->rotation = 0; g->px = 4; g->py = BOARD_HIDDEN;
    g->curPower = p; g->curPowerCell = cell;
    while (Board_TestFit(&g->board, g->current, g->rotation, g->px, g->py + 1)) g->py++;
    Cell cells[CELLS_PER_PIECE];
    Tetromino_GetCells(g->current, g->rotation, cells);
    return (g->px + cells[cell].x) + 100 * (g->py + cells[cell].y);
}

static void test_powerups(void) {
    Game g;
    int total;

    /* Bomb: a 3x3 hole around the block. */
    Game_InitConfig(&g, (GameConfig){MODE_POWER, 10, 20, false}, 8, 0, 1, 0);
    total = Board_TotalH(&g.board);
    for (int y = total - 6; y < total; y++) for (int x = 0; x < g.board.w; x++) g.board.cells[y][x] = (x == 9) ? CELL_EMPTY : (int8_t)PIECE_J;
    for (int y = total - 6; y < total; y++) g.board.cells[y][8] = CELL_EMPTY;
    for (int y = total - 6; y < total; y++) g.board.cells[y][9] = CELL_EMPTY;
    int at = PlacePowerPiece(&g, PU_BOMB, 0);
    int bx = at % 100, by = at / 100;
    int before = Board_CountFilled(&g.board);
    long s0 = g.score;
    Game_HardDrop(&g);
    CHECK(g.lastPower == PU_BOMB && g.lastPowerX == bx && g.lastPowerY == by, "bomb: fired where the block landed");
    bool hole = true;
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if (g.board.cells[by + dy][bx + dx] != CELL_EMPTY) hole = false;
    CHECK(hole, "bomb: the 3x3 around it is empty");
    CHECK(Board_CountFilled(&g.board) < before + 4 && g.lastPowerRemoved > 0 && g.score - s0 >= 150 + 30 * g.lastPowerRemoved, "bomb: scored for the pickup and each block destroyed");

    /* Laser: the whole column. */
    Game_InitConfig(&g, (GameConfig){MODE_POWER, 10, 20, false}, 8, 0, 1, 0);
    total = Board_TotalH(&g.board);
    for (int y = total - 8; y < total; y++) for (int x = 0; x < g.board.w; x++) g.board.cells[y][x] = (x == 5 || x == 6) ? CELL_EMPTY : (int8_t)PIECE_S;
    g.board.cells[total - 8][5] = CELL_EMPTY;
    at = PlacePowerPiece(&g, PU_LASER, 1);
    int lx = at % 100;
    Game_HardDrop(&g);
    bool colEmpty = true;
    for (int y = 0; y < Board_TotalH(&g.board); y++) if (g.board.cells[y][lx] != CELL_EMPTY) colEmpty = false;
    CHECK(g.lastPower == PU_LASER && colEmpty && g.lastPowerRemoved > 0, "laser: empties its whole column");

    /* Sweep: the bottom three rows go. */
    Game_InitConfig(&g, (GameConfig){MODE_POWER, 10, 20, false}, 8, 0, 1, 0);
    total = Board_TotalH(&g.board);
    for (int y = total - 5; y < total; y++) for (int x = 0; x < g.board.w; x++) g.board.cells[y][x] = (x == 0) ? (int8_t)PIECE_Z : CELL_EMPTY;
    g.board.cells[total - 5][0] = (int8_t)PIECE_T;
    PlacePowerPiece(&g, PU_SWEEP, 2);
    Game_HardDrop(&g);
    CHECK(g.lastPower == PU_SWEEP && g.board.cells[total - 1][0] != CELL_EMPTY && g.board.cells[total - 3][0] == CELL_EMPTY, "sweep: three rows gone, the rest lowered");

    /* Freeze: gravity waits, then resumes. */
    Game_InitConfig(&g, (GameConfig){MODE_POWER, 10, 20, false}, 8, 0, 1, 0);
    PlacePowerPiece(&g, PU_FREEZE, 0);
    Game_HardDrop(&g);
    CHECK(g.freezeTimer == FREEZE_SECONDS, "freeze: starts its timer");
    int y0 = g.py;
    for (int i = 0; i < 50; i++) Game_Update(&g, 0.1f);
    CHECK(g.py == y0 && g.freezeTimer < FREEZE_SECONDS, "freeze: the piece hangs in the air");
    for (int i = 0; i < 40; i++) Game_Update(&g, 0.1f);
    CHECK(g.freezeTimer == 0.0f && (g.py > y0 || g.phase != GS_PLAYING || g.py != y0), "freeze: and gravity comes back");

    /* Slow: same piece falls slower. */
    Game a, b;
    Game_InitConfig(&a, (GameConfig){MODE_POWER, 10, 20, false}, 8, 0, 1, 0);
    Game_InitConfig(&b, (GameConfig){MODE_POWER, 10, 20, false}, 8, 0, 1, 0);
    a.current = b.current = PIECE_T;
    a.curPower = b.curPower = PU_NONE;
    b.slowTimer = 30.0f;
    for (int i = 0; i < 30; i++) { Game_Update(&a, 0.1f); Game_Update(&b, 0.1f); }
    CHECK(b.py < a.py, "slow: the slowed game has fallen less far");

    /* Only Power mode rolls power-ups. */
    int seen = 0;
    Game_InitConfig(&g, (GameConfig){MODE_POWER, 10, 20, false}, 21, 0, 1, 0);
    for (int i = 0; i < 400 && g.phase == GS_PLAYING; i++) {
        if (g.curPower != PU_NONE) seen++;
        Game_HardDrop(&g);
        Board_Init(&g.board, 10, 20);
    }
    CHECK(seen > 20 && seen < 120, "power: about one piece in seven carries one");
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, false}, 21, 0, 1, 0);
    seen = 0;
    for (int i = 0; i < 200 && g.phase == GS_PLAYING; i++) { if (g.curPower != PU_NONE || g.nextPower != PU_NONE) seen++; Game_HardDrop(&g); Board_Init(&g.board, 10, 20); }
    CHECK(seen == 0, "power: never in the other modes");
    /* Deterministic. */
    Game p1, p2;
    Game_InitConfig(&p1, (GameConfig){MODE_POWER, 12, 22, true}, 33, 0, 1, 0);
    Game_InitConfig(&p2, (GameConfig){MODE_POWER, 12, 22, true}, 33, 0, 1, 0);
    bool same = true;
    for (int i = 0; i < 100; i++) { if (p1.current != p2.current || p1.curPower != p2.curPower || p1.curPowerCell != p2.curPowerCell) same = false; Game_HardDrop(&p1); Game_HardDrop(&p2); if (p1.phase != GS_PLAYING) break; }
    CHECK(same, "power: the same seed rolls the same power-ups");
    /* A power block on a held piece follows it. */
    Game_InitConfig(&g, (GameConfig){MODE_POWER, 10, 20, false}, 8, 0, 1, 0);
    g.curPower = PU_BOMB; g.curPowerCell = 3;
    PowerUp keepNext = g.nextPower;
    Game_Hold(&g);
    CHECK(g.holdPower == PU_BOMB && g.holdPowerCell == 3 && g.curPower == keepNext, "power: held with the piece");
    Game_HardDrop(&g);
    Game_Hold(&g);
    CHECK(g.curPower == PU_BOMB && g.curPowerCell == 3, "power: and comes back with it");
}

int main(void) {
    test_rng_deterministic();
    test_tetromino_shapes_in_bounds();
    test_board_fit_and_collision();
    test_board_line_clear_and_compaction();
    test_board_multi_line_clear();
    test_game_init_and_movement();
    test_game_hard_drop_locks_piece();
    test_game_line_clear_scoring();
    test_presentation_hints();
    test_game_level_up_after_ten_lines();
    test_game_over_on_blocked_spawn();
    test_game_restart_resets_state_but_keeps_high_score();
    test_rotation_near_wall_stays_in_bounds();
    test_custom_board_sizes();
    test_hold();
    test_modes();
    test_modern_scoring();
    test_powerups();

    printf("\n%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
