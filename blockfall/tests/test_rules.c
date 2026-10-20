/* The rules the old suite never pinned (see REWORK_PLAN.md, bug 19 / fix 28):
 * lock delay and its reset cap, gravity per level, drop points, the classic
 * score table at more than one level, a rotation that really needs a kick,
 * the ghost piece; plus the rule bugs phase 1 fixed: Zen hold, the power-up
 * order, the kick-lift stall, locking above the top, the sweep hint. Every
 * scenario goes through the public calls main.c makes for the player's keys
 * (Game_Move/Rotate/SoftDrop/HardDrop/Hold/Update); only the board and the
 * current piece are set up by hand. */
#include <string.h>
#include "check.h"
#include "../src/board.h"
#include "../src/game.h"
#include "../src/tetromino.h"

#define FRAME (1.0f / 60.0f)

static GameConfig Cfg(GameMode mode, int w, int h, bool modern) { return (GameConfig){mode, w, h, modern}; }

/* Row y counted from the bottom (0 = floor row), '#' filled, '.' empty. */
static void Row(Game *g, int y, const char *pattern) {
    int row = Board_TotalH(&g->board) - 1 - y;
    for (int x = 0; x < g->board.w; x++) {
        char c = x < (int)strlen(pattern) ? pattern[x] : '.';
        g->board.cells[row][x] = c == '#' ? (int8_t)PIECE_J : CELL_EMPTY;
    }
}

/* The current piece as a fresh spawn would be (what Place() does), so the
 * lock and fall state is a new piece's. */
static void Spawn(Game *g, PieceType t) {
    g->current = t;
    g->rotation = 0;
    g->px = (g->board.w - 4) / 2;
    g->py = BOARD_HIDDEN;
    g->curPower = PU_NONE;
    g->holdUsed = false;
}

/* Puts a piece somewhere specific (a test set-up, not a spawn). */
static void Put(Game *g, PieceType t, int rot, int px, int py) {
    Spawn(g, t);
    g->rotation = rot;
    g->px = px;
    g->py = py;
}

static float Gravity(int level) { float v = 1.0f - 0.07f * (float)(level - 1); return v < 0.08f ? 0.08f : v; }

static void SetLevel(Game *g, int level) {
    g->level = level;
    g->fallInterval = Gravity(level);
}

/* Runs frames until the current piece locks; returns seconds taken, or -1. */
static float SecondsToLock(Game *g, float limit) {
    for (float t = 0.0f; t < limit; t += FRAME) {
        Game_Update(g, FRAME);
        if (g->justLocked) return t + FRAME;
        Game_ConsumeFrameFlags(g);
    }
    return -1.0f;
}

/* ------------------------------------------------------------ lock delay */

static void test_lock_delay(void) {
    Game g;
    Game_InitConfig(&g, Cfg(MODE_MARATHON, 10, 20, false), 11, 0, 1, 0);
    Spawn(&g, PIECE_T);
    while (Game_SoftDrop(&g)) {}
    float t = SecondsToLock(&g, 5.0f);
    CHECK(t > LOCK_DELAY_SECONDS - 0.03f && t < LOCK_DELAY_SECONDS + 0.05f, "lock: a resting piece locks after half a second");

    /* A grounded move restarts the half second. */
    Game_InitConfig(&g, Cfg(MODE_MARATHON, 10, 20, false), 11, 0, 1, 0);
    Spawn(&g, PIECE_T);
    while (Game_SoftDrop(&g)) {}
    for (int i = 0; i < 24; i++) { Game_Update(&g, FRAME); Game_ConsumeFrameFlags(&g); } /* 0.4 s on the floor */
    CHECK(Game_MoveLeft(&g), "lock: the move succeeds");
    t = SecondsToLock(&g, 5.0f);
    CHECK(t > LOCK_DELAY_SECONDS - 0.03f, "lock: a move on the floor gives a fresh half second");

    /* Fifteen resets, then no more: moves after that do not stop the clock. */
    Game_InitConfig(&g, Cfg(MODE_MARATHON, 10, 20, false), 11, 0, 1, 0);
    Spawn(&g, PIECE_T);
    while (Game_SoftDrop(&g)) {}
    Game_Update(&g, FRAME);
    Game_ConsumeFrameFlags(&g);
    for (int i = 0; i < MAX_LOCK_RESETS; i++) {
        if (i % 2 == 0) Game_MoveLeft(&g); else Game_MoveRight(&g);
        Game_Update(&g, FRAME);
        Game_ConsumeFrameFlags(&g);
    }
    CHECK(g.lockResets == MAX_LOCK_RESETS && !g.justLocked, "lock: fifteen moves on the floor spend the resets");
    float elapsed = 0.0f;
    bool locked = false;
    for (int f = 0; f < 600 && !locked; f++) {
        if (f % 2 == 0) Game_MoveLeft(&g); else Game_MoveRight(&g);
        Game_Update(&g, FRAME);
        elapsed += FRAME;
        locked = g.justLocked;
        Game_ConsumeFrameFlags(&g);
    }
    CHECK(locked && elapsed <= LOCK_DELAY_SECONDS + 0.05f, "lock: once spent, moving every frame still locks within half a second");
}

/* The kick-lift stall (bug 5): tapping rotate on the floor lifted the piece
 * with an upward kick, the air frame zeroed the lock timer without counting,
 * and the piece never locked. Returns the seconds it took to lock, -1 if not
 * within the limit. */
static float RotateSpam(PieceType t, int level, int tapEvery, bool spendResets, float limit) {
    Game g;
    Game_InitConfig(&g, Cfg(MODE_MARATHON, 10, 20, false), 5, 0, 1, 0);
    SetLevel(&g, level);
    Spawn(&g, t);
    while (Game_SoftDrop(&g)) {}
    Game_Update(&g, FRAME);
    Game_ConsumeFrameFlags(&g);
    if (spendResets) {
        for (int i = 0; i < MAX_LOCK_RESETS; i++) {
            if (i % 2 == 0) Game_MoveLeft(&g); else Game_MoveRight(&g);
            Game_Update(&g, FRAME);
            if (g.justLocked) return 0.0f;
            Game_ConsumeFrameFlags(&g);
        }
    }
    int frame = 0;
    for (float s = 0.0f; s < limit; s += FRAME, frame++) {
        if (frame % tapEvery == 0) Game_Rotate(&g, 1);
        Game_Update(&g, FRAME);
        if (g.justLocked) return s + FRAME;
        Game_ConsumeFrameFlags(&g);
    }
    return -1.0f;
}

static void test_lock_stall(void) {
    static const PieceType kPieces[] = {PIECE_I, PIECE_T, PIECE_S, PIECE_Z, PIECE_J, PIECE_L};
    static const int kLevels[] = {1, 20};
    static const int kTaps[] = {20, 12, 6, 2};
    for (size_t p = 0; p < sizeof(kPieces) / sizeof(kPieces[0]); p++) {
        for (size_t l = 0; l < 2; l++) {
            float air = 2.0f * Gravity(kLevels[l]); /* a lift is at most two rows, and gravity brings it back */
            for (size_t k = 0; k < sizeof(kTaps) / sizeof(kTaps[0]); k++) {
                float spent = RotateSpam(kPieces[p], kLevels[l], kTaps[k], true, 600.0f);
                CHECK(spent >= 0.0f && spent <= LOCK_DELAY_SECONDS + air + 0.1f,
                      "lock: with the resets spent, tapping rotate cannot keep a landed piece alive");
                /* From a fresh piece: fifteen resets at most, each worth a
                 * half second on the floor plus a lift, then the same bound. */
                float fresh = RotateSpam(kPieces[p], kLevels[l], kTaps[k], false, 600.0f);
                CHECK(fresh >= 0.0f && fresh <= (float)(MAX_LOCK_RESETS + 1) * (LOCK_DELAY_SECONDS + air) + 0.1f,
                      "lock: rotate spam from a fresh landing is bounded too");
            }
        }
    }
}

/* Falling to a new lowest row is what refreshes the fifteen. */
static void test_lock_refresh_on_descent(void) {
    Game g;
    Game_InitConfig(&g, Cfg(MODE_MARATHON, 10, 20, false), 9, 0, 1, 0);
    /* A ledge: the T lands on a step, slides off it and falls further. */
    Row(&g, 0, "....######");
    Row(&g, 1, "....######");
    Row(&g, 2, "....######");
    Spawn(&g, PIECE_T);
    g.px = 6;
    while (Game_SoftDrop(&g)) {}
    Game_Update(&g, FRAME);
    Game_ConsumeFrameFlags(&g);
    for (int i = 0; i < MAX_LOCK_RESETS - 1; i++) {
        if (i % 2 == 0) Game_MoveRight(&g); else Game_MoveLeft(&g);
        Game_Update(&g, FRAME);
        Game_ConsumeFrameFlags(&g);
    }
    CHECK(g.lockResets == MAX_LOCK_RESETS - 1, "lock: fourteen moves on the step are counted");
    for (int i = 0; i < 8 && Game_MoveLeft(&g); i++) {} /* off the step */
    int y0 = g.py;
    bool locked = false, down = false;
    for (int i = 0; i < 400 && !locked && !down; i++) {
        Game_Update(&g, FRAME);
        locked = g.justLocked;
        down = g.py > y0 && g.grounded;
        Game_ConsumeFrameFlags(&g);
    }
    CHECK(down && !locked && g.py == y0 + 3, "lock: the T falls off the step to the floor");
    CHECK(g.lockResets == 0, "lock: reaching a new lowest row gives the fifteen back");
}

/* --------------------------------------------------------------- gravity */

static void test_gravity_curve(void) {
    static const int kLevels[] = {1, 5, 10, 14, 15, 30};
    for (size_t i = 0; i < sizeof(kLevels) / sizeof(kLevels[0]); i++) {
        Game g;
        Game_InitConfig(&g, Cfg(MODE_MARATHON, 10, 20, false), 3, 0, 1, 0);
        SetLevel(&g, kLevels[i]);
        Spawn(&g, PIECE_O);
        float interval = Gravity(kLevels[i]);
        int y0 = g.py;
        int before = (int)(interval * 60.0f) - 2;
        for (int f = 0; f < before; f++) Game_Update(&g, FRAME);
        CHECK(g.py == y0, "gravity: the piece holds its row until the level's interval is up");
        for (int f = 0; f < 4; f++) Game_Update(&g, FRAME);
        CHECK(g.py == y0 + 1, "gravity: then falls exactly one row");
    }

    /* Level up really changes the speed: ten lines take a classic game to level 2. */
    Game g;
    Game_InitConfig(&g, Cfg(MODE_MARATHON, 10, 20, false), 3, 0, 1, 0);
    float start = g.fallInterval;
    g.linesCleared = 9;
    Row(&g, 0, "#########.");
    Put(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
    Game_HardDrop(&g);
    CHECK(g.level == 2 && g.fallInterval < start && g.fallInterval == Gravity(2), "gravity: level 2 falls faster, on the curve");
}

/* ----------------------------------------------------------- drop points */

static void test_drop_points(void) {
    Game g;
    Game_InitConfig(&g, Cfg(MODE_MARATHON, 10, 20, false), 2, 0, 1, 0);
    Spawn(&g, PIECE_T);
    long s0 = g.score;
    int steps = 0;
    while (Game_SoftDrop(&g)) steps++;
    CHECK(steps > 10 && g.score - s0 == steps, "drops: a soft drop is one point a row");
    CHECK(!Game_SoftDrop(&g) && g.score - s0 == steps, "drops: none for a step that cannot happen");

    Game_InitConfig(&g, Cfg(MODE_MARATHON, 10, 20, false), 2, 0, 1, 0);
    Spawn(&g, PIECE_T);
    s0 = g.score;
    Game_HardDrop(&g);
    CHECK(g.lastDropCells > 10 && g.score - s0 == 2L * g.lastDropCells, "drops: a hard drop is two points a row");
}

/* ------------------------------------------------------- classic scoring */

/* n full rows but column 9, and a vertical I down column 9: n lines. */
static long ClassicClear(int n, int level) {
    Game g;
    Game_InitConfig(&g, Cfg(MODE_MARATHON, 10, 20, false), 4, 0, 1, 0);
    SetLevel(&g, level);
    g.linesCleared = (level - 1) * LINES_PER_LEVEL;
    for (int y = 0; y < n; y++) Row(&g, y, "#########.");
    Put(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
    long s0 = g.score;
    Game_HardDrop(&g);
    if (g.lastClearCount != n) return -1;
    return g.score - s0 - 2L * g.lastDropCells;
}

static void test_classic_score_table(void) {
    static const long kTable[5] = {0, 100, 300, 500, 800};
    for (int level = 1; level <= 3; level += 2) {
        for (int n = 1; n <= 4; n++) {
            long got = ClassicClear(n, level);
            char msg[96];
            snprintf(msg, sizeof(msg), "classic: %d line%s at level %d pays exactly %ld", n, n > 1 ? "s" : "", level, kTable[n] * level);
            CHECK(got == kTable[n] * level, msg);
        }
    }
}

/* ----------------------------------------------------- kicks and the ghost */

static void test_real_kick(void) {
    /* A vertical I hugging the left wall cannot turn flat in place (it would
     * stick out of the well): only a sideways kick lets it turn. */
    Game g;
    Game_InitConfig(&g, Cfg(MODE_MARATHON, 10, 20, false), 8, 0, 1, 0);
    Put(&g, PIECE_I, 1, -2, BOARD_HIDDEN + 6);
    CHECK(!Board_TestFit(&g.board, PIECE_I, 2, -2, BOARD_HIDDEN + 6), "kick: the set-up really blocks the turn in place");
    CHECK(Game_Rotate(&g, 1) && g.rotation == 2 && g.px != -2, "kick: the I turns by shifting off the wall");
    Cell cells[CELLS_PER_PIECE];
    Tetromino_GetCells(g.current, g.rotation, cells);
    bool inside = true;
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        int x = g.px + cells[i].x, y = g.py + cells[i].y;
        if (x < 0 || x >= g.board.w || y < 0 || y >= Board_TotalH(&g.board)) inside = false;
    }
    CHECK(inside, "kick: and ends up wholly inside the well");

    /* No room at all: an O-shaped pocket only an O fits. The I cannot turn and stays as it was. */
    Game_InitConfig(&g, Cfg(MODE_MARATHON, 10, 20, false), 8, 0, 1, 0);
    for (int y = 0; y < 20; y++) Row(&g, y, "####.#####");
    Put(&g, PIECE_I, 1, 2, Board_TotalH(&g.board) - 4);
    CHECK(Board_TestFit(&g.board, g.current, 1, g.px, g.py), "kick: a vertical I fits its one-wide shaft");
    CHECK(!Game_Rotate(&g, 1) && g.rotation == 1 && g.px == 2 && g.py == Board_TotalH(&g.board) - 4, "kick: with nowhere to go the turn fails and nothing moves");
}

static void test_ghost(void) {
    Game g;
    Game_InitConfig(&g, Cfg(MODE_MARATHON, 10, 20, false), 8, 0, 1, 0);
    Spawn(&g, PIECE_O);
    int floorY = Board_TotalH(&g.board) - 2; /* O fills local rows 0-1 */
    CHECK(Game_GhostY(&g) == floorY, "ghost: on an empty well it rests on the floor");
    Row(&g, 3, "....#.....");
    CHECK(Game_GhostY(&g) == floorY - 4, "ghost: and on top of whatever is under it");
    Row(&g, 3, "..........");
    Game_HardDrop(&g);
    CHECK(g.board.cells[floorY][4] == (int8_t)PIECE_O && g.board.cells[floorY + 1][5] == (int8_t)PIECE_O, "ghost: a hard drop lands where the ghost was");
}

/* ------------------------------------------------------------------- Zen */

static void test_zen_hold(void) {
    /* A stack that blocks the I's spawn cells but not the O's (bug 3). */
    Game g;
    Game_InitConfig(&g, Cfg(MODE_ZEN, 10, 20, true), 6, 0, 1, 0);
    Row(&g, 18, "...#......");
    Row(&g, 17, "##.#####.#");
    Spawn(&g, PIECE_O);
    g.hold = PIECE_I;
    CHECK(Board_TestFit(&g.board, PIECE_O, 0, g.px, g.py) && !Board_TestFit(&g.board, PIECE_I, 0, g.px, g.py), "zen hold: the set-up blocks only the held piece");
    CHECK(Game_Hold(&g), "zen hold: the swap happens");
    CHECK(g.phase == GS_PLAYING, "zen hold: Zen has no game over, not even through Hold");
    CHECK(g.zenResets == 1 && g.justZenReset && Board_CountFilled(&g.board) == 0, "zen hold: it is a fresh start instead");
    CHECK(g.current == PIECE_I && Board_TestFit(&g.board, g.current, g.rotation, g.px, g.py), "zen hold: with the held piece in play");

    /* Every other mode still tops out there. */
    Game_InitConfig(&g, Cfg(MODE_MARATHON, 10, 20, true), 6, 0, 1, 0);
    Row(&g, 18, "...#......");
    Row(&g, 17, "##.#####.#");
    Spawn(&g, PIECE_O);
    g.hold = PIECE_I;
    Game_Hold(&g);
    CHECK(g.phase == GS_GAMEOVER && !g.won, "hold: a held piece that cannot come in ends a Marathon");
}

/* ------------------------------------------------------- power-up order */

static void test_power_order(void) {
    /* Laser on a vertical I down a four-deep well: the quad clears first,
     * then the laser fires on what is left (bug 4). */
    for (int cell = 0; cell < CELLS_PER_PIECE; cell++) {
        Game g;
        Game_InitConfig(&g, Cfg(MODE_POWER, 10, 20, false), 8, 0, 1, 0);
        for (int y = 0; y < 4; y++) Row(&g, y, "#########.");
        Put(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
        g.curPower = PU_LASER;
        g.curPowerCell = cell;
        long s0 = g.score;
        Game_HardDrop(&g);
        CHECK(g.lastClearCount == 4 && g.linesCleared == 4, "power order: a Laser I down the well still scores its quad");
        CHECK(Board_CountFilled(&g.board) == 0 && g.lastPower == PU_LASER, "power order: the well ends empty and the laser fired");
        CHECK(g.score - s0 == 800 + 2L * g.lastDropCells + 150 + 15L * g.lastPowerRemoved, "power order: quad points plus the pickup");
    }

    /* A flat Bomb I that completes one row clears that row. */
    {
        Game g;
        Game_InitConfig(&g, Cfg(MODE_POWER, 10, 20, true), 8, 0, 1, 0);
        Row(&g, 0, "....######");
        Row(&g, 1, "....######");
        Put(&g, PIECE_I, 0, 0, BOARD_HIDDEN);
        g.curPower = PU_BOMB;
        g.curPowerCell = 3; /* the block beside the stack */
        Game_HardDrop(&g);
        CHECK(g.lastClearCount == 1 && g.combo == 0, "power order: a Bomb I that finishes a row clears it, and starts a combo");
        CHECK(g.lastPower == PU_BOMB && g.lastPowerRemoved > 0, "power order: then the bomb goes off on the stack");
    }

    /* Sweep on a quad: four lines count, then three more rows go. */
    {
        Game g;
        Game_InitConfig(&g, Cfg(MODE_POWER, 10, 20, false), 8, 0, 1, 0);
        for (int y = 0; y < 4; y++) Row(&g, y, "#########.");
        Row(&g, 4, "#.........");
        Row(&g, 5, "#.........");
        Put(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
        g.curPower = PU_SWEEP;
        g.curPowerCell = 3;
        Game_HardDrop(&g);
        CHECK(g.lastClearCount == 4 && g.linesCleared == 4, "power order: a Sweep I keeps its quad");
        CHECK(Board_CountFilled(&g.board) == 0, "power order: and the sweep takes what fell to the bottom");
    }

    /* A power block in a row that cleared fires where it was; one above
     * the cleared rows fires where it fell to. */
    {
        Game g;
        Game_InitConfig(&g, Cfg(MODE_POWER, 10, 20, false), 8, 0, 1, 0);
        Row(&g, 0, "#########.");
        Row(&g, 1, "#########.");
        Put(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
        g.curPower = PU_FREEZE; /* no board change, so the reported spot is easy to check */
        g.curPowerCell = 0;     /* the top block, two rows above the two that clear */
        Game_HardDrop(&g);
        int floorRow = Board_TotalH(&g.board) - 1;
        CHECK(g.lastClearCount == 2 && g.lastPowerX == 9 && g.lastPowerY == floorRow - 1, "power order: a block above the clear fires where it fell to");
        CHECK(g.board.cells[g.lastPowerY][9] == (int8_t)PIECE_I, "power order: which is where its block now is");
    }
}

/* -------------------------------------------------------------- lock-out */

/* The stack stops two rows under the spawn; the I's in-place turn is
 * blocked, an upward kick lifts it into the hidden rows, it slides to the
 * left wall and locks partly above the top (bug 17). */
static void LockAboveTheTop(Game *g, GameMode mode) {
    Game_InitConfig(g, Cfg(mode, 10, 20, false), 13, 0, 1, 0);
    for (int y = 0; y < 18; y++) Row(g, y, "#########.");
    Spawn(g, PIECE_I);
    Game_Rotate(g, 1);
    for (int i = 0; i < 6; i++) Game_MoveLeft(g);
    SecondsToLock(g, 5.0f);
}

static void test_lock_out(void) {
    Game g;
    LockAboveTheTop(&g, MODE_MARATHON);
    CHECK(g.lastLockY < BOARD_HIDDEN, "lock-out: the set-up locks a piece partly in the hidden rows");
    CHECK(g.phase == GS_GAMEOVER && !g.won, "lock-out: locking above the top ends the run, as reaching the top does");

    LockAboveTheTop(&g, MODE_ZEN);
    CHECK(g.phase == GS_PLAYING && g.zenResets == 1 && g.justZenReset, "lock-out: in Zen it is a fresh start instead");
    bool hiddenEmpty = true;
    for (int y = 0; y < BOARD_HIDDEN; y++) for (int x = 0; x < g.board.w; x++) if (g.board.cells[y][x] != CELL_EMPTY) hiddenEmpty = false;
    CHECK(hiddenEmpty, "lock-out: nothing solid is left where it cannot be seen");

    /* A normal lock near the top, all of it on screen, carries on. */
    Game_InitConfig(&g, Cfg(MODE_MARATHON, 10, 20, false), 13, 0, 1, 0);
    for (int y = 0; y < 17; y++) Row(&g, y, ".#########");
    Put(&g, PIECE_O, 0, 7, BOARD_HIDDEN);
    Game_HardDrop(&g);
    CHECK(g.phase == GS_PLAYING, "lock-out: a lock that is all on screen is fine");
}

/* -------------------------------------------------------- the sweep hint */

static void test_sweep_hint(void) {
    Game g;
    Game_InitConfig(&g, Cfg(MODE_POWER, 10, 20, false), 8, 0, 1, 0);
    for (int y = 0; y < 6; y++) Row(&g, y, ".#########");
    Put(&g, PIECE_O, 0, 3, BOARD_HIDDEN);
    g.curPower = PU_SWEEP;
    g.curPowerCell = 0;
    Game_HardDrop(&g);
    CHECK(g.lastPower == PU_SWEEP && g.lastClearCount == 0, "sweep hint: a sweep that clears no line");
    Cell cells[CELLS_PER_PIECE];
    Tetromino_GetCells(g.lastLockType, g.lastLockRotation, cells);
    int onPiece = 0;
    for (int i = 0; i < CELLS_PER_PIECE; i++)
        if (g.board.cells[g.lastLockY + cells[i].y][g.lastLockX + cells[i].x] == (int8_t)PIECE_O) onPiece++;
    CHECK(onPiece == CELLS_PER_PIECE, "sweep hint: the landing hint points at the piece after the board moved down");
    CHECK(g.board.cells[g.lastPowerY][g.lastPowerX] == (int8_t)PIECE_O, "sweep hint: and the sweep's burst at its block, where it is now");
}

/* ------------------------------------------------------------- counters */

static void test_pieces_locked(void) {
    Game g;
    Game_InitConfig(&g, Cfg(MODE_MARATHON, 10, 20, false), 8, 0, 1, 0);
    CHECK(g.piecesLocked == 0, "count: a new run has locked nothing");
    Game_HardDrop(&g);
    Game_HardDrop(&g);
    CHECK(g.piecesLocked == 2, "count: every lock is counted");
    Game_Restart(&g, 3);
    CHECK(g.piecesLocked == 0, "count: and a restart starts again");
}

void test_rules_all(void) {
    test_lock_delay();
    test_lock_stall();
    test_lock_refresh_on_descent();
    test_gravity_curve();
    test_drop_points();
    test_classic_score_table();
    test_real_kick();
    test_ghost();
    test_zen_hold();
    test_power_order();
    test_lock_out();
    test_sweep_hint();
    test_pieces_locked();
}
