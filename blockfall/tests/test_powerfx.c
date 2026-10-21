/* The power-ups' shows in time (powerfx.c), with no window: the Bomb's
 * blink, burst and shockwave, the Laser's charge, beam and pops, the
 * Freeze's wash, frost, blink and thaw, the Slow's afterimages, each with
 * its Reduced motion and Reduced flashing form; and the Sweep's replay
 * (feel.c): its stage, its bar crossing the bottom rows, the cells going
 * under it, the stack's drop after, and the cut with Reduced motion. */
#include <string.h>
#include "check.h"
#include "../src/board.h"
#include "../src/game.h"
#include "../src/feel.h"
#include "../src/powerfx.h"

#define FRAME (1.0f / 60.0f)
#define NEAR(a, b) ((a) - (b) < 0.001f && (b) - (a) < 0.001f)

static const FeelFrame kFull = {true, true, false};
static const FeelFrame kReduced = {false, false, false};

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
    g->fallTimer = g->lockTimer = 0.0f;
    g->lockResets = 0;
    g->lowestRow = py;
    g->grounded = false;
    g->curPower = PU_NONE; /* Power mode hands them out at random */
}

/* ------------------------------------------------------------ bomb */

static void test_bomb(void) {
    CHECK(PowerFx_BombStage(0.0f) == BOMB_BLINK && PowerFx_BombStage(0.11f) == BOMB_BLINK && PowerFx_BombStage(0.13f) == BOMB_SHOCK &&
              PowerFx_BombStage(0.46f) == BOMB_SHOCK && PowerFx_BombStage(0.48f) == BOMB_NONE && PowerFx_BombStage(-1.0f) == BOMB_NONE,
          "bomb: blinks 0.12 s, then the shockwave to 0.47 s");
    CHECK(PowerFx_BombLit(0.01f, false) && !PowerFx_BombLit(0.05f, false) && PowerFx_BombLit(0.09f, false) && !PowerFx_BombLit(0.13f, false),
          "bomb: on, off, on: three beats of 0.04 s");
    CHECK(PowerFx_BombLit(0.01f, true) && PowerFx_BombLit(0.05f, true) && PowerFx_BombLit(0.09f, true) && !PowerFx_BombLit(0.13f, true),
          "bomb: steady lit with Reduced flashing");
    CHECK(PowerFx_BombBursts(0.12f, 0.11f) && !PowerFx_BombBursts(0.11f, 0.10f) && !PowerFx_BombBursts(0.14f, 0.13f) && !PowerFx_BombBursts(0.12f, 0.12f),
          "bomb: the burst happens on the one frame the blink ends");
    bool up = true;
    float prev = -1.0f;
    for (float t = 0.12f; t < 0.47f; t += FRAME) {
        float k = PowerFx_BombShock(t);
        if (k < prev || k < 0.0f || k > 1.0f) up = false;
        prev = k;
    }
    CHECK(up && PowerFx_BombShock(0.05f) < 0.0f && PowerFx_BombShock(0.5f) < 0.0f, "bomb: the shockwave spreads from 0 to 1 and is gone after");
}

/* ------------------------------------------------------------ laser */

static void test_laser(void) {
    CHECK(PowerFx_LaserStage(0.0f) == LASER_CHARGE && PowerFx_LaserStage(0.11f) == LASER_CHARGE && PowerFx_LaserStage(0.13f) == LASER_BEAM &&
              PowerFx_LaserStage(0.39f) == LASER_BEAM && PowerFx_LaserStage(0.41f) == LASER_NONE,
          "laser: the charge for 0.12 s, the beam to 0.40 s");
    bool down = true;
    float prev = -1.0f;
    for (float t = 0.0f; t < 0.12f; t += FRAME) {
        float k = PowerFx_LaserCharge(t, true);
        if (k < prev || k > 1.0f) down = false;
        prev = k;
    }
    CHECK(down && PowerFx_LaserCharge(0.0f, true) < 0.05f && NEAR(PowerFx_LaserCharge(0.12f, true), 1.0f), "laser: the charge line runs down the column");
    CHECK(NEAR(PowerFx_LaserCharge(0.0f, false), 1.0f), "laser: ...at once with Reduced motion");
    CHECK(NEAR(PowerFx_LaserBeam(0.12f), 1.0f) && PowerFx_LaserBeam(0.26f) > 0.4f && PowerFx_LaserBeam(0.26f) < 0.6f && PowerFx_LaserBeam(0.41f) == 0.0f &&
              PowerFx_LaserBeam(0.05f) == 0.0f,
          "laser: the beam fades from 1 to 0");
    CHECK(NEAR(PowerFx_LaserPopAt(0), LASER_CHARGE_SECONDS) && NEAR(PowerFx_LaserPopAt(3), PowerFx_LaserPopAt(-3)) &&
              PowerFx_LaserPopAt(3) > PowerFx_LaserPopAt(2) && PowerFx_LaserPopAt(BOARD_MAX_TOTAL_H) < LASER_CHARGE_SECONDS + LASER_BEAM_SECONDS,
          "laser: the cells pop out from the power cell, nearest first, the farthest of a 36-tall well before the beam fades");
}

/* ------------------------------------------------------------ freeze */

static void test_freeze(void) {
    CHECK(NEAR(PowerFx_FreezeWash(0.0f, true), 0.0f) && PowerFx_FreezeWash(0.15f, true) > 0.4f && PowerFx_FreezeWash(0.15f, true) < 0.6f &&
              PowerFx_FreezeWash(0.31f, true) < 0.0f && PowerFx_FreezeWash(0.1f, false) < 0.0f,
          "freeze: the ice wash crosses in 0.30 s (none with Reduced motion)");
    bool grows = true;
    float prev = -1.0f;
    for (float t = 0.0f; t < 0.6f; t += FRAME) {
        float k = PowerFx_Frost(t, FREEZE_SECONDS - t, true);
        if (k < prev || k > 1.0f) grows = false;
        prev = k;
    }
    CHECK(grows && PowerFx_Frost(0.0f, 6.0f, true) < 0.01f && NEAR(PowerFx_Frost(0.6f, 5.4f, true), 1.0f) && NEAR(PowerFx_Frost(3.0f, 3.0f, true), 1.0f),
          "freeze: the frost grows in over 0.6 s and holds");
    CHECK(NEAR(PowerFx_Frost(0.0f, 6.0f, false), 1.0f) && PowerFx_Frost(1.0f, 0.0f, true) == 0.0f, "freeze: full at once with Reduced motion; none once the timer is out");
    CHECK(NEAR(PowerFx_FreezeLit(5.0f, false), 1.0f) && NEAR(PowerFx_FreezeLit(1.6f, false), 1.0f), "freeze: steady lit until the last 1.5 s");
    int lit = 0, dim = 0;
    for (float t = 1.49f; t > 1.24f; t -= FRAME) {
        float k = PowerFx_FreezeLit(t, false);
        if (NEAR(k, 1.0f)) lit++;
        else if (NEAR(k, FREEZE_BLINK_LOW)) dim++;
    }
    CHECK(lit > 0 && dim > 0 && lit + dim >= 14 && lit + dim <= 16, "freeze: in the last 1.5 s it blinks, 4 Hz, between full and 0.35");
    bool steady = true;
    for (float t = 1.49f; t > 0.0f; t -= FRAME) if (!NEAR(PowerFx_FreezeLit(t, true), FREEZE_CALM_LIT)) steady = false;
    CHECK(steady && PowerFx_FreezeLit(0.0f, false) == 0.0f, "freeze: steady at 0.7 with Reduced flashing; off at 0");
    CHECK(NEAR(PowerFx_Thaw(0.0f), 1.0f) && PowerFx_Thaw(0.15f) > 0.4f && PowerFx_Thaw(0.15f) < 0.6f && PowerFx_Thaw(0.31f) == 0.0f, "freeze: the frost breaks over 0.3 s");
}

/* ------------------------------------------------------------ slow */

static void test_slow(void) {
    CHECK(!PowerFx_SlowEnding(5.0f) && PowerFx_SlowEnding(1.0f) && !PowerFx_SlowEnding(0.0f), "slow: its last 1.5 s");
    CHECK(PowerFx_Afterimages(true) == 2 && PowerFx_Afterimages(false) == 0, "slow: two afterimages, none with Reduced motion");
    CHECK(PowerFx_AfterimageAlpha(0) > PowerFx_AfterimageAlpha(1) && PowerFx_AfterimageAlpha(1) > 0.0f && PowerFx_AfterimageAlpha(0) < 0.3f,
          "slow: the nearer afterimage is the stronger, both faint");
}

/* ------------------------------------------------------------ sweep */

/* A Sweep on a stack: the replay's stage, the bar, the cells going under
 * it, the drop after; with a clear in front of it; with Reduced motion. */
static void test_sweep_replay(void) {
    Game g;
    Feel f;
    Game_InitConfig(&g, (GameConfig){MODE_POWER, 10, 20, false}, 4, 0, 1, 0);
    Feel_Reset(&f, &g, true);
    for (int y = 0; y < 6; y++) Row(&g, y, ".#########");
    Place(&g, PIECE_O, 0, 3, BOARD_HIDDEN);
    g.curPower = PU_SWEEP;
    g.curPowerCell = 0;
    Game_HardDrop(&g);
    Feel_React(&f, &g, &kFull);
    const ClearReplay *r = &f.clear;
    int total = Board_TotalH(&g.board);
    CHECK(g.lastPower == PU_SWEEP && g.lastClearCount == 0 && r->on && r->count == 0 && r->shift == 3, "sweep: a Sweep alone starts a replay (no rows cleared, three shifted)");
    CHECK(r->sweepCount == 3 && r->sweepRows[0] == total - 1 && r->sweepRows[1] == total - 2 && r->sweepRows[2] == total - 3,
          "sweep: its bar crosses the bottom three rows of the board as stamped");
    CHECK(Feel_IsSweepRow(r, total - 2) && !Feel_IsSweepRow(r, total - 4), "sweep: (and only those)");
    ClearTimes t = r->times;
    CHECK(NEAR(t.flash, 0.0f) && NEAR(t.zip, 0.0f) && NEAR(t.sweep, 0.14f) && NEAR(t.drop, 0.10f) && NEAR(Feel_ClearTotal(t), 0.24f),
          "sweep: the bar takes 0.14 s and the drop 0.10 s: the clear's 0.24 s beat");
    CHECK(Feel_ClearStage(&f) == CLEAR_SWEEP && !Feel_ShowGhost(&f), "sweep: the bar's stage first, the ghost hidden");
    CHECK(NEAR(Feel_SweepBar(r, 0.0f), 0.0f) && NEAR(Feel_SweepBar(r, 0.07f), 5.0f) && Feel_SweepBar(r, 0.15f) < 0.0f,
          "sweep: the bar goes from the left wall to the right in 0.14 s");
    CHECK(!Feel_SweepCellGone(r, 0, 0.0f) && Feel_SweepCellGone(r, 0, 0.02f) && Feel_SweepCellGone(r, 4, 0.07f) && !Feel_SweepCellGone(r, 5, 0.07f) &&
              Feel_SweepCellGone(r, 9, 0.14f),
          "sweep: a cell goes once the bar has passed its middle");
    /* The stages in order, then the live board. */
    int seen[5] = {0};
    for (int i = 0; i < 20; i++) {
        seen[Feel_ClearStage(&f)]++;
        Feel_Tick(&f, FRAME);
    }
    CHECK(seen[CLEAR_FLASH] == 0 && seen[CLEAR_ZIP] == 0 && seen[CLEAR_SWEEP] >= 8 && seen[CLEAR_DROP] >= 5 && seen[CLEAR_NONE] >= 4 &&
              Feel_ClearStage(&f) == CLEAR_NONE && Feel_ShowGhost(&f),
          "sweep: the bar, then the drop, then the live board at 0.24 s");
    /* During the drop every live row that moved came from three rows up. */
    f.clear.t = 0.15f;
    bool fell = false, right = true;
    for (int row = BOARD_HIDDEN; row < total; row++) {
        int src = r->from[row];
        if (src >= 0 && src != row - 3) right = false;
        if (src >= 0 && Feel_RowLift(&f, row, 30) > 0) fell = true;
    }
    CHECK(fell && right, "sweep: the stack drops three rows, each from where it was");
    /* The landed piece: drawn where it landed while the bar crosses, three
     * rows down with the board after. */
    CHECK(f.land.on && f.land.replayed && f.land.y == f.land.liveY - 3, "sweep: the piece is drawn where it landed, then three rows down with the board");

    /* A clear in front of the Sweep: the flash and zip first, then the bar
     * over the three lowest rows that were not cleared. */
    Game_InitConfig(&g, (GameConfig){MODE_POWER, 10, 20, false}, 4, 0, 1, 0);
    Feel_Reset(&f, &g, true);
    Row(&g, 0, "#########.");
    Row(&g, 1, "..#######.");
    Row(&g, 2, "..#######.");
    Row(&g, 3, "..#######.");
    Row(&g, 4, "..#######.");
    Place(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
    g.curPower = PU_SWEEP;
    g.curPowerCell = 3;
    Game_HardDrop(&g);
    Feel_React(&f, &g, &kFull);
    CHECK(g.lastClearCount == 1 && r->on && r->count == 1 && r->shift == 3 && r->sweepCount == 3 && r->sweepRows[0] == total - 2 &&
              r->sweepRows[2] == total - 4,
          "sweep: after a single the bar crosses the three lowest rows the clear left");
    CHECK(NEAR(r->times.flash, 0.06f) && NEAR(r->times.zip, 0.08f) && NEAR(r->times.sweep, 0.14f) && NEAR(Feel_ClearTotal(r->times), 0.38f) &&
              Feel_ClearStage(&f) == CLEAR_FLASH && Feel_SweepBar(r, 0.10f) < 0.0f && NEAR(Feel_SweepBar(r, 0.14f), 0.0f) &&
              !Feel_SweepCellGone(r, 0, 0.13f) && Feel_SweepCellGone(r, 0, 0.16f),
          "sweep: the clear's flash and zip come first, the bar starts at 0.14 s");
    CHECK(NEAR(Feel_ClearTotal(Feel_SweepTimes(0, 15, true)), 0.12f), "sweep: shorter at high levels like a clear");

    /* Reduced motion: the rows flash, then cut; no bar. */
    Game_InitConfig(&g, (GameConfig){MODE_POWER, 10, 20, false}, 4, 0, 1, 0);
    Feel_Reset(&f, &g, false);
    for (int y = 0; y < 6; y++) Row(&g, y, ".#########");
    Place(&g, PIECE_O, 0, 3, BOARD_HIDDEN);
    g.curPower = PU_SWEEP;
    Game_HardDrop(&g);
    Feel_React(&f, &g, &kReduced);
    CHECK(r->on && r->sweepCount == 3 && NEAR(r->times.flash, 0.08f) && r->times.sweep == 0.0f && r->times.drop == 0.0f &&
              Feel_ClearStage(&f) == CLEAR_FLASH && Feel_SweepBar(r, 0.02f) < 0.0f && !Feel_SweepCellGone(r, 5, 0.02f) && Feel_SweepCellGone(r, 5, 0.09f),
          "sweep: with Reduced motion the three rows flash 0.08 s and cut, no bar");
    for (int i = 0; i < 6; i++) Feel_Tick(&f, FRAME);
    CHECK(Feel_ClearStage(&f) == CLEAR_NONE && Feel_RowLift(&f, total - 1, 30) == 0, "sweep: ...and the stack is simply there");

    /* A Zen fresh start is not a Sweep; a plain lock starts none. */
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, false}, 4, 0, 1, 0);
    Feel_Reset(&f, &g, true);
    Place(&g, PIECE_O, 0, 3, BOARD_HIDDEN);
    Game_HardDrop(&g);
    Feel_React(&f, &g, &kFull);
    CHECK(!r->on && Feel_SweepBar(r, 0.0f) < 0.0f && !Feel_SweepCellGone(r, 0, 1.0f), "sweep: a lock with no clear and no Sweep replays nothing");
}

void test_powerfx_all(void) {
    test_bomb();
    test_laser();
    test_freeze();
    test_slow();
    test_sweep_replay();
}
