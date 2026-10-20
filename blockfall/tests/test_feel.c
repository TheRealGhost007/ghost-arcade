/* How play feels (feel.c) and a frame of a run (playstep.c), with no window.
 *  - The board copy behind the clear animation matches the real clear row
 *    for row, on every board shape, with power-ups too, over thousands of
 *    real locks; the rows fall from exactly where they were to exactly where
 *    they are.
 *  - The clear gets shorter at high levels; each size has its own beat.
 *  - The piece glides at most half a cell, snaps on auto-repeat, soft drop
 *    and Snap; slides in; pops on a turn; darkens on the ground.
 *  - Landing, the ghost's rows, Hold's flight and ready flash, Next's slide.
 *  - READY-GO never runs the Sprint or Ultra clock, keeps a turn and a hold
 *    for GO and nothing else; after a restart only GO; off, nothing.
 *  - With READY-GO off, a run plays frame for frame exactly as 2.3.0 did. */
#include <math.h>
#include <string.h>
#include "check.h"
#include "rng.h"
#include "../src/board.h"
#include "../src/game.h"
#include "../src/feel.h"
#include "../src/layout.h"
#include "../src/playstep.h"

#define FRAME (1.0f / 60.0f)
#define NEAR(a, b) (fabsf((a) - (b)) < 0.0005f)

static const FeelFrame kFull = {true, true, false};

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
}

/* A frame as the play screen runs it: react, tick, then the rules' flags go. */
static void Frame(Feel *f, Game *g, const FeelFrame *in) {
    Feel_React(f, g, in);
    Feel_Tick(f, FRAME);
    Game_ConsumeFrameFlags(g);
}

static void Settle(Feel *f, Game *g, int frames) {
    for (int i = 0; i < frames; i++) Frame(f, g, &kFull);
}

/* ------------------------------------------------------------ the copy */

static bool IsCleared(const ClearReplay *r, int row) {
    for (int i = 0; i < r->count; i++)
        if (r->rows[i] == row) return true;
    return false;
}

/* The replay's copy against the real board after the lock, row for row. */
static bool ReplayMatches(const Feel *f, const Game *g, const char **why) {
    const ClearReplay *r = &f->clear;
    const Board *b = &r->before, *live = &g->board;
    int total = Board_TotalH(live);
    bool removes = g->lastPower == PU_BOMB || g->lastPower == PU_LASER; /* fired after the clear: cells may be gone */
    if (b->w != live->w || b->h != live->h) { *why = "the copy is another size"; return false; }
    for (int i = 0; i < r->count; i++) {
        for (int x = 0; x < b->w; x++)
            if (b->cells[r->rows[i]][x] == CELL_EMPTY) { *why = "a cleared row is not full in the copy"; return false; }
    }
    int used[BOARD_MAX_TOTAL_H] = {0}, sourced = 0, last = -1;
    for (int row = 0; row < total; row++) {
        int src = r->from[row];
        if (src < 0) {
            for (int x = 0; x < live->w; x++)
                if (live->cells[row][x] != CELL_EMPTY) { *why = "a row that came in empty is not empty"; return false; }
            continue;
        }
        if (src >= total || IsCleared(r, src)) { *why = "a row comes from a cleared row"; return false; }
        if (src <= last) { *why = "the rows changed order"; return false; }
        last = src;
        used[src]++;
        sourced++;
        for (int x = 0; x < live->w; x++) {
            int8_t was = b->cells[src][x], now = live->cells[row][x];
            if (was == now) continue;
            if (removes && now == CELL_EMPTY) continue;
            *why = "a live cell differs from where its row came from";
            return false;
        }
    }
    if (sourced != total - r->count - r->shift) { *why = "not every surviving row is drawn exactly once"; return false; }
    for (int i = 0; i < total; i++)
        if (used[i] > 1) { *why = "a row is drawn twice"; return false; }
    return true;
}

/* The fall: from where each row was to where it is, never above where it
 * started, never more than 2 px below, home at the end. */
static bool FallMatches(Feel *f, int cell, const char **why) {
    ClearReplay *r = &f->clear;
    float start = r->times.flash + r->times.zip, total = Feel_ClearTotal(r->times);
    float keep = r->t;
    bool ok = true;
    int prev[BOARD_MAX_TOTAL_H];
    for (int step = 0; step <= 40 && ok; step++) {
        r->t = start + (total - start) * (float)step / 40.0f - (step == 40 ? 0.0001f : 0.0f);
        for (int row = 0; row < BOARD_MAX_TOTAL_H && ok; row++) {
            int lift = Feel_RowLift(f, row, cell), src = r->from[row];
            int full = src >= 0 && row > src ? (row - src) * cell : 0;
            if (step == 0 && lift != full) { *why = "a row does not start its fall where it was"; ok = false; }
            if (lift > full) { *why = "a row is drawn above where it was"; ok = false; }
            if (lift < -CLEAR_SETTLE_PX) { *why = "a row settles more than 2 px"; ok = false; }
            if (full == 0 && lift != 0) { *why = "a row that did not move moves"; ok = false; }
            if (step > 0 && lift > prev[row] && lift > 0) { *why = "a row goes back up while it falls"; ok = false; }
            prev[row] = lift;
        }
    }
    r->t = total;
    for (int row = 0; row < BOARD_MAX_TOTAL_H && ok; row++)
        if (Feel_RowLift(f, row, cell) != 0) { *why = "a row is not home at the end"; ok = false; }
    r->t = keep;
    return ok;
}

/* A stack with a column left open, rows `full` complete but for it, and a
 * vertical I dropped into it: 1 to 4 lines, with or without gaps between. */
static void DropIntoColumn(Game *g, Rng *rng, int col, unsigned fullMask, PowerUp power) {
    Board_Init(&g->board, g->cfg.width, g->cfg.height);
    int w = g->board.w, stack = 4 + (int)Rng_Range(rng, (uint32_t)(g->board.h / 2 - 3));
    for (int y = 0; y < stack; y++) {
        int row = Board_TotalH(&g->board) - 1 - y;
        bool full = y < 4 && (fullMask & (1u << y));
        int hole = col;
        while (hole == col) hole = (int)Rng_Range(rng, (uint32_t)w);
        for (int x = 0; x < w; x++) {
            bool filled = x != col && (full || (x != hole && Rng_Range(rng, 3) != 0));
            if (y >= 4 && !full) filled = filled && x != col; /* the column stays open all the way */
            g->board.cells[row][x] = filled ? (int8_t)Rng_Range(rng, PIECE_COUNT) : CELL_EMPTY;
        }
    }
    Place(g, PIECE_I, 1, col - 2, BOARD_HIDDEN);
    g->curPower = power;
    g->curPowerCell = (int)Rng_Range(rng, 4);
    Game_HardDrop(g);
}

static void test_replay_matches_the_clear(void) {
    Rng rng;
    Rng_Seed(&rng, 2026);
    static const int kSizes[][2] = {{6, 12}, {10, 20}, {24, 36}, {14, 20}, {24, 12}, {8, 26}, {6, 36}};
    static const PowerUp kPowers[] = {PU_NONE, PU_NONE, PU_BOMB, PU_LASER, PU_SWEEP, PU_FREEZE};
    int trials = 0, matched = 0, fell = 0, byCount[5] = {0}, swept = 0, blasted = 0;
    const char *why = "";
    for (size_t s = 0; s < sizeof(kSizes) / sizeof(kSizes[0]); s++) {
        for (int k = 0; k < 160; k++) {
            Game g;
            Feel f;
            PowerUp power = kPowers[k % 6];
            Game_InitConfig(&g, (GameConfig){power == PU_NONE ? MODE_MARATHON : MODE_POWER, kSizes[s][0], kSizes[s][1], k & 1}, (uint64_t)k, 0, 1, 0);
            Feel_Reset(&f, &g, true);
            unsigned mask = 1u + Rng_Range(&rng, 15); /* any of the 15 ways the I's four rows can clear */
            DropIntoColumn(&g, &rng, (int)Rng_Range(&rng, (uint32_t)g.board.w), mask, power);
            Feel_React(&f, &g, &kFull);
            int want = 0;
            for (int b = 0; b < 4; b++) if (mask & (1u << b)) want++;
            trials++;
            if (g.lastClearCount != want || !f.clear.on || f.clear.count != want) { why = "the set-up did not clear what it meant to"; continue; }
            if (!ReplayMatches(&f, &g, &why)) continue;
            if (!FallMatches(&f, Layout_SideCell(g.board.w, g.board.h), &why)) continue;
            matched++;
            byCount[want]++;
            if (f.clear.shift) swept++;
            if (g.lastPower == PU_BOMB || g.lastPower == PU_LASER) blasted++;
            for (int r = 0; r < BOARD_MAX_TOTAL_H; r++) if (f.clear.from[r] >= 0 && f.clear.from[r] < r) { fell++; break; }
        }
    }
    CHECK(matched == trials, why);
    CHECK(byCount[1] && byCount[2] && byCount[3] && byCount[4] && swept > 0 && blasted > 0 && fell > 0,
          "replay: the trials cover singles to quads, a Sweep moving the rows, Bombs and Lasers, and rows that fall");
    printf("  feel: %d clears replayed row for row (%d single, %d double, %d triple, %d quad; %d with a Sweep)\n", matched, byCount[1],
           byCount[2], byCount[3], byCount[4], swept);
}

/* Puts the falling piece where it clears the most rows (then lowest), the
 * way a player hunting lines would, and leaves the drop to the caller. */
static void AimForLines(Game *g) {
    int bestRot = g->rotation, bestX = g->px, bestScore = -1;
    for (int rot = 0; rot < 4; rot++) {
        for (int x = -3; x < g->board.w; x++) {
            if (!Board_TestFit(&g->board, g->current, rot, x, g->py)) continue;
            int y = g->py;
            while (Board_TestFit(&g->board, g->current, rot, x, y + 1)) y++;
            Board b = g->board;
            Board_LockPiece(&b, g->current, rot, x, y);
            int score = Board_ClearLines(&b, NULL) * 100 + y;
            if (score > bestScore) { bestScore = score; bestRot = rot; bestX = x; }
        }
    }
    g->rotation = bestRot;
    g->px = bestX;
}

/* Whole runs: play on random boards, aiming for lines, with moves, turns,
 * holds, soft drops and natural locks among the drops; every clear's copy
 * matches. */
static void test_replay_in_real_runs(void) {
    Rng rng;
    Rng_Seed(&rng, 77);
    int clears = 0, bad = 0;
    const char *why = "";
    for (int run = 0; run < 24; run++) {
        Game g;
        Feel f;
        int w = 6 + (int)Rng_Range(&rng, 19), h = 12 + (int)Rng_Range(&rng, 25);
        GameMode mode = (GameMode)Rng_Range(&rng, MODE_COUNT);
        Game_InitConfig(&g, (GameConfig){mode, w, h, run & 1}, (uint64_t)(run * 31 + 5), 0, 1, 0);
        Feel_Reset(&f, &g, true);
        for (int frame = 0; frame < 6000; frame++) {
            switch (Rng_Range(&rng, 8)) {
                case 0: Game_MoveLeft(&g); break;
                case 1: Game_MoveRight(&g); break;
                case 2: Game_Rotate(&g, 1); break;
                case 3: AimForLines(&g); Game_HardDrop(&g); break;
                case 4: Game_SoftDrop(&g); break;
                case 5: Game_Hold(&g); break;
                default: break;
            }
            Game_Update(&g, FRAME * 4.0f);
            Feel_React(&f, &g, &kFull);
            if (g.justLocked && g.lastClearCount > 0 && !g.justZenReset) {
                clears++;
                if (!ReplayMatches(&f, &g, &why) || !FallMatches(&f, 20, &why)) bad++;
            }
            Feel_Tick(&f, FRAME);
            Game_ConsumeFrameFlags(&g);
            if (g.phase == GS_GAMEOVER) Game_Restart(&g, (uint64_t)frame);
        }
    }
    printf("  feel: %d clears in random runs replayed row for row\n", clears);
    CHECK(clears > 1000 && bad == 0, bad ? why : "replay: real runs clear lines and every clear's copy matches the board");
}

/* ------------------------------------------------------------ the beat */

static void test_clear_beat(void) {
    ClearTimes t1 = Feel_ClearTimes(1, 1, true), t9 = Feel_ClearTimes(2, 9, true), t10 = Feel_ClearTimes(3, 10, true);
    ClearTimes t14 = Feel_ClearTimes(1, 14, true), t15 = Feel_ClearTimes(2, 15, true), t30 = Feel_ClearTimes(3, 30, true);
    CHECK(NEAR(Feel_ClearTotal(t1), 0.24f) && NEAR(Feel_ClearTotal(t9), 0.24f), "beat: 0.24 s to level 9");
    CHECK(NEAR(Feel_ClearTotal(t10), 0.18f) && NEAR(Feel_ClearTotal(t14), 0.18f), "beat: 0.18 s at levels 10-14");
    CHECK(NEAR(Feel_ClearTotal(t15), 0.12f) && NEAR(Feel_ClearTotal(t30), 0.12f), "beat: 0.12 s from level 15");
    CHECK(NEAR(t1.flash, 0.06f) && NEAR(t1.zip, 0.08f) && NEAR(t1.drop, 0.10f), "beat: flash 0.06 s, zip to 0.14 s, fall to 0.24 s");
    ClearTimes q = Feel_ClearTimes(4, 1, true);
    CHECK(NEAR(q.flash, 0.10f) && NEAR(Feel_ClearTotal(q), 0.24f), "beat: a quad flashes 0.10 s, the same 0.24 s in all");
    CHECK(NEAR(Feel_ClearTotal(Feel_ClearTimes(4, 15, true)), 0.12f), "beat: a quad is shorter at high levels too");
    float prev = 1.0f;
    bool shorter = true;
    for (int level = 1; level <= 25; level++) {
        float tl = Feel_ClearTotal(Feel_ClearTimes(2, level, true));
        if (tl > prev + 0.00001f) shorter = false;
        prev = tl;
    }
    CHECK(shorter, "beat: never longer at a higher level");
    ClearTimes cut = Feel_ClearTimes(4, 1, false), cut15 = Feel_ClearTimes(1, 15, false);
    CHECK(NEAR(cut.flash, 0.08f) && cut.zip == 0.0f && cut.drop == 0.0f && cut15.flash < cut.flash,
          "beat: Reduced motion is a 0.08 s flash and the rows snap down (shorter at high levels)");
    CHECK(Feel_ClearPitch(1, false) == 1.0f && NEAR(Feel_ClearPitch(2, false), 1.12f) && NEAR(Feel_ClearPitch(3, false), 1.26f),
          "beat: a single at its own pitch, a double and a triple ringing higher");
    CHECK(Feel_ClearPitch(4, false) == 1.0f && Feel_ClearPitch(4, true) > 1.1f, "beat: a quad its own sound, back to back two semitones up");
}

/* A single from a vertical I in the right column: the stages, the zip from
 * where it landed, the end on the live board; a new lock ends a replay. */
static void test_clear_stages(void) {
    Game g;
    Feel f;
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, false}, 3, 0, 1, 0);
    Feel_Reset(&f, &g, true);
    Settle(&f, &g, 10);
    Board_Init(&g.board, 10, 20);
    Row(&g, 0, "#########.");
    Row(&g, 1, "####.####.");
    Place(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
    Game_HardDrop(&g);
    Feel_React(&f, &g, &kFull);
    const ClearReplay *r = &f.clear;
    CHECK(f.clear.on && Feel_ClearStage(&f) == CLEAR_FLASH && r->count == 1 && r->lockCol == 9, "stages: a single flashes first, from column 9");
    CHECK(!Feel_ShowGhost(&f), "stages: the ghost hides while the clear plays");
    CHECK(!Feel_ClearCellGone(r, 9, 0.05f), "stages: nothing zips out during the flash");
    float mid = r->times.flash + r->times.zip * 0.5f;
    CHECK(Feel_ClearCellGone(r, 9, mid) && Feel_ClearCellGone(r, 5, mid) && !Feel_ClearCellGone(r, 4, mid) && !Feel_ClearCellGone(r, 0, mid),
          "stages: halfway through the zip the cells nearest the piece are gone, the far wall's still there");
    CHECK(Feel_ClearCellGone(r, 0, r->times.flash + r->times.zip), "stages: every cell gone by the end of the zip");
    int frames = 0;
    ClearStage seen[4] = {0};
    while (Feel_ClearStage(&f) != CLEAR_NONE && frames < 60) {
        seen[Feel_ClearStage(&f)] = 1;
        Frame(&f, &g, &kFull);
        frames++;
    }
    CHECK(seen[CLEAR_FLASH] && seen[CLEAR_ZIP] && seen[CLEAR_DROP] && frames >= 14 && frames <= 15,
          "stages: flash, zip, fall, done in 0.24 s (frame 15)");
    CHECK(Feel_ShowGhost(&f), "stages: the ghost is back once the rows have settled");

    /* A quad bursts at once and flashes the frame gold; a lock that clears
     * nothing ends a replay at once. */
    Board_Init(&g.board, 10, 20);
    for (int y = 0; y < 4; y++) Row(&g, y, "#########.");
    Place(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
    Game_HardDrop(&g);
    Frame(&f, &g, &kFull);
    CHECK(f.clear.quad && Feel_QuadFrame(&f) > 0.9f, "stages: a quad flashes the frame gold");
    CHECK(!Feel_ClearCellGone(&f.clear, 9, f.clear.times.flash - 0.001f) && Feel_ClearCellGone(&f.clear, 0, f.clear.times.flash),
          "stages: a quad's four rows burst all at once when the flash ends");
    Settle(&f, &g, 3);
    Place(&g, PIECE_O, 0, 0, BOARD_HIDDEN);
    Game_HardDrop(&g);
    Feel_React(&f, &g, &kFull);
    CHECK(Feel_ClearStage(&f) == CLEAR_NONE && !f.clear.on, "stages: a new lock ends the replay before it at once");
    Game_ConsumeFrameFlags(&g);

    /* High levels: the whole beat is shorter. */
    Board_Init(&g.board, 10, 20);
    Row(&g, 0, "#########.");
    g.level = 16;
    g.linesCleared = 150;
    Place(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
    Game_HardDrop(&g);
    Feel_React(&f, &g, &kFull);
    CHECK(NEAR(Feel_ClearTotal(f.clear.times), 0.12f), "stages: at level 15 the clear takes 0.12 s");
    Game_ConsumeFrameFlags(&g);

    /* Reduced motion: a short flash, then the live board. */
    Board_Init(&g.board, 10, 20);
    Row(&g, 0, "#########.");
    g.level = 1;
    g.linesCleared = 0;
    Place(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
    Game_HardDrop(&g);
    FeelFrame cut = {false, false, false};
    Feel_React(&f, &g, &cut);
    CHECK(Feel_ClearStage(&f) == CLEAR_FLASH, "stages: Reduced motion still flashes");
    f.clear.t = f.clear.times.flash;
    CHECK(Feel_ClearStage(&f) == CLEAR_NONE && Feel_RowLift(&f, BOARD_HIDDEN + 10, 30) == 0, "stages: Reduced motion: then the rows snap down");
    Game_ConsumeFrameFlags(&g);

    /* A Zen fresh start swept the board: nothing to replay. */
    Board_Init(&g.board, 10, 20);
    Row(&g, 0, "#########.");
    Place(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
    Game_HardDrop(&g);
    g.justZenReset = true;
    Feel_React(&f, &g, &kFull);
    CHECK(!f.clear.on, "stages: no replay over a Zen fresh start");
    Game_ConsumeFrameFlags(&g);
}

/* ------------------------------------------------------------ the piece */

static void test_piece_motion(void) {
    Game g;
    Feel f;
    float dx, dy;
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, false}, 11, 0, 1, 0);
    Place(&g, PIECE_T, 0, 3, BOARD_HIDDEN + 4);
    Feel_Reset(&f, &g, true);
    Feel_PieceOffset(&f, &dx, &dy);
    CHECK(NEAR(dy, -0.5f) && dx == 0.0f && Feel_SpawnFill(&f) < 0.4f, "piece: a run's first piece starts half a cell up, an outline");
    Settle(&f, &g, 6);
    Feel_PieceOffset(&f, &dx, &dy);
    CHECK(dx == 0.0f && dy == 0.0f && Feel_SpawnFill(&f) == 1.0f, "piece: in place and filled after 0.08 s");

    Game_MoveRight(&g);
    Feel_React(&f, &g, &kFull);
    Feel_PieceOffset(&f, &dx, &dy);
    CHECK(NEAR(dx, -0.5f), "piece: a tap is drawn half a cell behind at first, never more");
    Feel_Tick(&f, FRAME);
    Game_ConsumeFrameFlags(&g);
    Feel_PieceOffset(&f, &dx, &dy);
    CHECK(dx < 0.0f && dx > -0.5f, "piece: ...and glides on");
    Game_MoveRight(&g); /* a second tap while gliding */
    Feel_React(&f, &g, &kFull);
    Feel_PieceOffset(&f, &dx, &dy);
    CHECK(dx >= -0.5f, "piece: a quick second tap still keeps it within half a cell");
    Feel_Tick(&f, FRAME);
    Game_ConsumeFrameFlags(&g);
    Settle(&f, &g, 3);
    Feel_PieceOffset(&f, &dx, &dy);
    CHECK(dx == 0.0f, "piece: home after 0.04 s");

    FeelFrame repeat = {true, true, true};
    Game_MoveLeft(&g);
    Feel_React(&f, &g, &repeat);
    Feel_PieceOffset(&f, &dx, &dy);
    CHECK(dx == 0.0f, "piece: auto-repeat snaps");
    Feel_Tick(&f, FRAME);
    Game_ConsumeFrameFlags(&g);

    Game_SoftDrop(&g);
    Feel_React(&f, &g, &kFull);
    Feel_PieceOffset(&f, &dx, &dy);
    CHECK(dy == 0.0f && Feel_Streak(&f) > 0.9f, "piece: soft drop snaps and leaves streaks");
    Feel_Tick(&f, FRAME);
    Game_ConsumeFrameFlags(&g);
    Settle(&f, &g, 6);
    CHECK(Feel_Streak(&f) == 0.0f, "piece: the streaks fade in 0.08 s");

    g.fallTimer = g.fallInterval - 0.001f;
    int py = g.py;
    Game_Update(&g, FRAME);
    Feel_React(&f, &g, &kFull);
    Feel_PieceOffset(&f, &dx, &dy);
    CHECK(g.py == py + 1 && NEAR(dy, -0.5f), "piece: a gravity step glides down from half a cell up");
    Feel_Tick(&f, FRAME);
    Game_ConsumeFrameFlags(&g);
    Settle(&f, &g, 3);

    FeelFrame snap = {true, false, false};
    Game_MoveRight(&g);
    Feel_React(&f, &g, &snap);
    Feel_PieceOffset(&f, &dx, &dy);
    CHECK(dx == 0.0f, "piece: Piece motion: Snap moves it cell by cell");
    Feel_Tick(&f, FRAME);
    Game_ConsumeFrameFlags(&g);

    CHECK(Game_Rotate(&g, 1), "piece: (it turns)");
    Feel_React(&f, &g, &kFull);
    CHECK(Feel_PopInset(&f) == POP_PX, "piece: a turn pops its blocks in 2 px");
    Feel_Tick(&f, FRAME);
    Game_ConsumeFrameFlags(&g);
    CHECK(Feel_PopInset(&f) == POP_PX, "piece: ...on the first frame drawn");
    Settle(&f, &g, 1);
    CHECK(Feel_PopInset(&f) == POP_PX, "piece: ...and the second");
    Settle(&f, &g, 1);
    CHECK(Feel_PopInset(&f) == 0, "piece: ...then fills out");
    Game_Rotate(&g, 1);
    Feel_React(&f, &g, &snap);
    CHECK(Feel_PopInset(&f) == 0, "piece: no pop with Snap");
    Feel_Tick(&f, FRAME);
    Game_ConsumeFrameFlags(&g);

    /* Every frame of a random walk stays within half a cell. */
    Rng rng;
    Rng_Seed(&rng, 9);
    bool within = true;
    for (int i = 0; i < 2000 && g.phase == GS_PLAYING; i++) {
        switch (Rng_Range(&rng, 6)) {
            case 0: Game_MoveLeft(&g); break;
            case 1: Game_MoveRight(&g); break;
            case 2: Game_Rotate(&g, 1); break;
            case 3: if (Rng_Range(&rng, 10) == 0) Game_HardDrop(&g); break;
            default: break;
        }
        Game_Update(&g, FRAME * 3.0f);
        Feel_React(&f, &g, &kFull);
        Feel_PieceOffset(&f, &dx, &dy);
        if (fabsf(dx) > 0.5f || fabsf(dy) > 0.5f) within = false;
        Feel_Tick(&f, FRAME);
        Feel_PieceOffset(&f, &dx, &dy);
        if (fabsf(dx) > 0.5f || fabsf(dy) > 0.5f) within = false;
        Game_ConsumeFrameFlags(&g);
        if (g.phase == GS_GAMEOVER) Game_Restart(&g, 3);
    }
    CHECK(within, "piece: never drawn more than half a cell from where it is");
}

static void test_lock_shade_and_ghost(void) {
    Game g;
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, false}, 4, 0, 1, 0);
    Board_Init(&g.board, 10, 20);
    Place(&g, PIECE_O, 0, 3, BOARD_HIDDEN + 18);
    CHECK(Feel_LockShade(&g) == 0.0f, "shade: none in the air");
    Game_Update(&g, 0.25f);
    CHECK(g.grounded && NEAR(Feel_LockShade(&g), LOCK_SHADE_MAX * 0.5f), "shade: halfway through the lock delay, half of 30% darker");
    Game_MoveLeft(&g);
    CHECK(Feel_LockShade(&g) == 0.0f, "shade: a move gives the time back and it brightens");

    Board_Init(&g.board, 10, 20);
    Row(&g, 0, "#########.");
    Row(&g, 1, "#########.");
    Row(&g, 2, "###.#####.");
    Place(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
    int rows[4];
    int n = Feel_GhostRows(&g, Game_GhostY(&g), rows);
    CHECK(n == 2 && rows[0] != rows[1], "ghost: the two rows it would complete are marked, not the one with a gap");
    Place(&g, PIECE_I, 1, 2, BOARD_HIDDEN);
    CHECK(Feel_GhostRows(&g, Game_GhostY(&g), rows) == 0, "ghost: none marked where it completes nothing");
}

static void test_landing(void) {
    Game g;
    Feel f;
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, false}, 5, 0, 1, 0);
    Feel_Reset(&f, &g, true);
    Settle(&f, &g, 6);
    Board_Init(&g.board, 10, 20);
    Place(&g, PIECE_T, 0, 3, BOARD_HIDDEN);
    Game_HardDrop(&g);
    Feel_React(&f, &g, &kFull);
    CHECK(Feel_Squash(&f) && Feel_Smear(&f) > 0.9f && Feel_Impact(&f) > 0.9f && Feel_Settle(&f) == 0 && Feel_Bevel(&f) == 0.0f,
          "landing: a hard drop smears, squashes and flashes");
    CHECK(f.land.fromY == BOARD_HIDDEN && f.land.y == g.lastLockY, "landing: the smear runs from where it started to where it landed");
    float dx, dy;
    Feel_PieceOffset(&f, &dx, &dy);
    CHECK(NEAR(dy, -0.5f) && Feel_SpawnFill(&f) < 0.4f, "landing: the next piece slides in");
    Feel_Tick(&f, FRAME);
    Game_ConsumeFrameFlags(&g);
    CHECK(Feel_Squash(&f), "landing: squashed on the first frame drawn");
    Settle(&f, &g, 1);
    CHECK(Feel_Squash(&f), "landing: ...and the second");
    Settle(&f, &g, 1);
    CHECK(!Feel_Squash(&f), "landing: ...not the third");
    Settle(&f, &g, 6);
    CHECK(Feel_Smear(&f) == 0.0f && Feel_Impact(&f) == 0.0f, "landing: smear and flash gone after 0.14 s");

    Place(&g, PIECE_O, 0, 0, BOARD_HIDDEN + 18);
    for (int i = 0; i < 40 && !g.justLocked; i++) Game_Update(&g, FRAME);
    Feel_React(&f, &g, &kFull);
    CHECK(g.justLocked && Feel_Settle(&f) == SETTLE_PX && Feel_Bevel(&f) > 0.9f && !Feel_Squash(&f) && Feel_Smear(&f) == 0.0f &&
              Feel_Impact(&f) == 0.0f,
          "landing: a natural lock settles 1 px with a brighter bevel, no flash");
    Game_ConsumeFrameFlags(&g);

    FeelFrame cut = {false, false, false};
    Place(&g, PIECE_T, 0, 5, BOARD_HIDDEN);
    Game_HardDrop(&g);
    Feel_React(&f, &g, &cut);
    Feel_PieceOffset(&f, &dx, &dy);
    CHECK(!Feel_Squash(&f) && Feel_Smear(&f) == 0.0f && dy == 0.0f && Feel_SpawnFill(&f) == 1.0f && Feel_Impact(&f) > 0.9f,
          "landing: Reduced motion: no smear, squash or slide in (the landing flash stays)");
    Game_ConsumeFrameFlags(&g);
}

static void test_hold_and_next(void) {
    Game g;
    Feel f;
    Game_InitConfig(&g, (GameConfig){MODE_POWER, 10, 20, false}, 6, 0, 1, 0);
    Feel_Reset(&f, &g, true);
    Place(&g, PIECE_L, 1, 4, BOARD_HIDDEN + 3);
    g.curPower = PU_BOMB;
    g.curPowerCell = 2;
    Settle(&f, &g, 6);
    PieceType nextBefore = g.next;
    CHECK(Game_Hold(&g), "hold: (into an empty Hold)");
    Feel_React(&f, &g, &kFull);
    float k = -1.0f;
    CHECK(Feel_HoldFlight(&f, &k) && k == 0.0f && f.flightType == PIECE_L && f.flightRot == 1 && NEAR(f.flightX, 4.0f) &&
              NEAR(f.flightY, (float)(BOARD_HIDDEN + 3)) && f.flightPower == PU_BOMB && f.flightPowerCell == 2,
          "hold: the piece flies from where it was drawn, turned as it was, its power-up with it");
    float sk;
    CHECK(Feel_NextSlide(&f, &sk) && f.slideOld == nextBefore, "hold: into an empty Hold the next piece comes in, so Next slides");
    Feel_Tick(&f, FRAME);
    Game_ConsumeFrameFlags(&g);
    Settle(&f, &g, 7);
    CHECK(!Feel_HoldFlight(&f, NULL) && !Feel_NextSlide(&f, NULL), "hold: the flight lands in 0.12 s, the slide ends in 0.10 s");

    Board_Init(&g.board, 10, 20);
    Game_HardDrop(&g);
    Feel_React(&f, &g, &kFull);
    CHECK(Feel_HoldReady(&f) > 0.9f && Feel_NextSlide(&f, NULL), "hold: the next piece makes hold ready again: the box flashes, Next slides");
    Feel_Tick(&f, FRAME);
    Game_ConsumeFrameFlags(&g);
    Settle(&f, &g, 10);
    CHECK(Feel_HoldReady(&f) == 0.0f, "hold: the flash is over in 0.15 s");

    CHECK(Game_Hold(&g), "hold: (a swap)");
    Feel_React(&f, &g, &kFull);
    CHECK(Feel_HoldFlight(&f, NULL) && !Feel_NextSlide(&f, NULL), "hold: a swap flies too, but Next stays");
    Feel_Tick(&f, FRAME);
    Game_ConsumeFrameFlags(&g);
    Settle(&f, &g, 8);

    /* A hard drop and a hold in the same frame: the piece that was drawn
     * locked, and the one held was never drawn, so nothing flies. */
    Game_HardDrop(&g);
    CHECK(Game_Hold(&g), "hold: (in the frame of a hard drop)");
    Feel_React(&f, &g, &kFull);
    CHECK(!Feel_HoldFlight(&f, NULL), "hold: a hold in the frame of a hard drop does not fly the piece that locked");
    Feel_Tick(&f, FRAME);
    Game_ConsumeFrameFlags(&g);
    Settle(&f, &g, 8);

    FeelFrame cut = {false, false, false};
    Game_HardDrop(&g);
    Feel_React(&f, &g, &cut);
    Feel_Tick(&f, FRAME);
    Game_ConsumeFrameFlags(&g);
    CHECK(Game_Hold(&g), "hold: (again, with Reduced motion)");
    Feel_React(&f, &g, &cut);
    CHECK(!Feel_HoldFlight(&f, NULL) && !Feel_NextSlide(&f, NULL), "hold: Reduced motion: no flight, no slide");
    Game_ConsumeFrameFlags(&g);
}

/* A Zen fresh start sweeps the board away: a clear still replaying, or the
 * piece that just landed, must not keep drawing blocks that are gone. */
static void test_zen_sweep(void) {
    Game g;
    Feel f;
    Game_InitConfig(&g, (GameConfig){MODE_ZEN, 10, 20, false}, 12, 0, 1, 0);
    Feel_Reset(&f, &g, true);
    /* A Hold that cannot come in, while the clear before it replays. */
    Board_Init(&g.board, 10, 20);
    Row(&g, 0, "#########.");
    Row(&g, 19, "......#..."); /* the clear moves it to row 18: an I from Hold cannot come in */
    Place(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
    g.next = PIECE_T;
    g.nextPower = PU_NONE;
    g.hold = PIECE_I;
    g.holdUsed = false;
    Game_HardDrop(&g);
    Feel_React(&f, &g, &kFull);
    CHECK(g.lastClearCount == 1 && Feel_ClearStage(&f) == CLEAR_FLASH, "zen sweep: (a single replays)");
    Feel_Tick(&f, FRAME);
    Game_ConsumeFrameFlags(&g);
    CHECK(Game_Hold(&g) && g.justZenReset, "zen sweep: (the I from Hold cannot come in: a fresh start)");
    Feel_React(&f, &g, &kFull);
    CHECK(Feel_ClearStage(&f) == CLEAR_NONE && Feel_ShowGhost(&f),
          "zen sweep: a fresh start from Hold ends the clear's replay (it drew the old stack over the empty well)");
    CHECK(Feel_Impact(&f) == 0.0f && Feel_Smear(&f) == 0.0f, "zen sweep: ...and the landed piece's flash, which was swept too");
    Feel_Tick(&f, FRAME);
    Game_ConsumeFrameFlags(&g);
    Settle(&f, &g, 20);

    /* A piece that locks partly above the well: the sweep takes it too. */
    Board_Init(&g.board, 10, 20);
    for (int y = 0; y < 18; y++) Row(&g, y, "#.........");
    Place(&g, PIECE_I, 1, -2, 0);
    Game_HardDrop(&g);
    CHECK(g.justLocked && g.justZenReset && g.lastDropCells > 0, "zen sweep: (a hard drop that locks above the well sweeps)");
    Feel_React(&f, &g, &kFull);
    CHECK(Feel_ClearStage(&f) == CLEAR_NONE && Feel_Impact(&f) == 0.0f && Feel_Smear(&f) == 0.0f && !Feel_Squash(&f),
          "zen sweep: no smear, squash or flash for a piece the sweep took");
    Feel_Tick(&f, FRAME);
    Game_ConsumeFrameFlags(&g);
}

/* ------------------------------------------------------------ READY-GO */

static PlayIntent Intent(bool left, bool rotate, bool hardDrop, bool hold) {
    PlayIntent in;
    memset(&in, 0, sizeof(in));
    in.left = left;
    in.rotate = rotate;
    in.hardDrop = hardDrop;
    in.hold = hold;
    return in;
}

static void test_ready_go(void) {
    static const GameMode kModes[] = {MODE_SPRINT, MODE_ULTRA, MODE_MARATHON};
    for (int m = 0; m < 3; m++) {
        Game g;
        PlayDriver d;
        memset(&d, 0, sizeof(d));
        Game_InitConfig(&g, (GameConfig){kModes[m], 10, 20, false}, 21, 0, 1, 0);
        Place(&g, PIECE_T, 0, 3, BOARD_HIDDEN);
        PieceType next = g.next;
        PlayDriver_Start(&d, true, false);
        bool still = true, waited = true;
        int frames = 0;
        PlayStepResult out;
        while (d.cue.stage == CUE_READY && frames < 60) {
            /* Everything at once: Left held, hard drop, a turn and a hold. */
            PlayIntent in = Intent(true, frames == 5 || frames == 9, frames % 4 == 0, frames == 12);
            Play_Step(&d, &g, &in, FRAME, 1.0f, &out);
            Game_ConsumeFrameFlags(&g);
            frames++;
            if (d.cue.stage != CUE_READY) break;
            if (!out.waiting) waited = false;
            if (g.elapsed != 0.0f || g.timeLeft != (kModes[m] == MODE_ULTRA ? ULTRA_SECONDS : 0.0f) || g.px != 3 || g.py != BOARD_HIDDEN ||
                g.piecesLocked != 0 || g.rotation != 0 || g.current != PIECE_T || g.score != 0)
                still = false;
        }
        CHECK(still && waited, "ready-go: during READY nothing moves, nothing falls, nothing locks and the clock does not run");
        CHECK(frames >= 36 && frames <= 37 && d.cue.stage == CUE_GO && d.cue.goSound, "ready-go: READY lasts 0.6 s, then GO (and its jingle)");
        CHECK(g.elapsed == 0.0f && g.piecesLocked == 0, "ready-go: the hard drops pressed during READY never happen");
        CHECK(out.held && g.current == next && (int)g.hold == PIECE_T && g.rotation == 2,
              "ready-go: the hold and both turns pressed during READY are made at GO (the hold first)");
        int px = g.px;
        PlayIntent hold = Intent(true, false, false, false);
        Play_Step(&d, &g, &hold, FRAME, 1.0f, &out);
        CHECK(g.elapsed > 0.0f && (kModes[m] != MODE_ULTRA || g.timeLeft < ULTRA_SECONDS), "ready-go: the clock starts on GO");
        CHECK(out.moved && !out.moveRepeat && g.px == px - 1, "ready-go: Left held through READY acts as a fresh press at GO");
        Game_ConsumeFrameFlags(&g);
        int goFrames = 1;
        PlayIntent none = Intent(false, false, false, false);
        while (d.cue.stage == CUE_GO && goFrames < 60) {
            Play_Step(&d, &g, &none, FRAME, 1.0f, &out);
            Game_ConsumeFrameFlags(&g);
            goFrames++;
        }
        CHECK(goFrames >= 24 && goFrames <= 25, "ready-go: GO shows for 0.4 s while play is on");
    }

    /* Pausing during READY pauses READY. */
    Game g;
    PlayDriver d;
    PlayStepResult out;
    PlayIntent none = Intent(false, false, false, false);
    memset(&d, 0, sizeof(d));
    Game_InitConfig(&g, (GameConfig){MODE_SPRINT, 10, 20, false}, 22, 0, 1, 0);
    PlayDriver_Start(&d, true, false);
    for (int i = 0; i < 10; i++) Play_Step(&d, &g, &none, FRAME, 1.0f, &out);
    float t = d.cue.t;
    Game_TogglePause(&g);
    for (int i = 0; i < 120; i++) Play_Step(&d, &g, &none, FRAME, 1.0f, &out);
    CHECK(d.cue.stage == CUE_READY && d.cue.t == t && g.elapsed == 0.0f, "ready-go: paused, READY waits too");
    Game_TogglePause(&g);
    for (int i = 0; i < 40; i++) Play_Step(&d, &g, &none, FRAME, 1.0f, &out);
    CHECK(d.cue.stage == CUE_GO && g.elapsed > 0.0f, "ready-go: ...and carries on after");

    /* After a restart only GO, and play at once; off, nothing. */
    Game_Restart(&g, 23);
    PlayDriver_Start(&d, true, true);
    CHECK(d.cue.stage == CUE_GO && d.cue.goSound, "ready-go: after R only GO, with its jingle");
    Play_Step(&d, &g, &none, FRAME, 1.0f, &out);
    CHECK(g.elapsed > 0.0f && !out.waiting, "ready-go: after R the clock runs from the first frame");
    Game_Restart(&g, 24);
    PlayDriver_Start(&d, false, false);
    CHECK(d.cue.stage == CUE_NONE && !PlayDriver_Waiting(&d), "ready-go: off, no READY and no GO");
    Play_Step(&d, &g, &none, FRAME, 1.0f, &out);
    CHECK(g.elapsed > 0.0f, "ready-go: off, the run starts at once");

    /* A turn that cannot be made says so; an O has none to make. */
    Board_Init(&g.board, 10, 20);
    for (int y = 0; y < 20; y++) Row(&g, y, "##.#######");
    Place(&g, PIECE_I, 1, 0, BOARD_HIDDEN + 2);
    PlayIntent turn = Intent(false, true, false, false);
    Play_Step(&d, &g, &turn, FRAME, 1.0f, &out);
    CHECK(!out.rotated && out.rotateBlocked, "steps: a blocked turn is reported (a dull tick)");
    Board_Init(&g.board, 10, 20);
    Place(&g, PIECE_O, 0, 3, BOARD_HIDDEN);
    Play_Step(&d, &g, &turn, FRAME, 1.0f, &out);
    CHECK(!out.rotateBlocked, "steps: an O pressed to turn is not blocked, it just has no turn");
}

/* 2.3.0's frame of a run, as scr_play.c had it. */
typedef struct { RepeatButton l, r, s; } OldPad;
static void OldStep(OldPad *p, Game *g, const PlayIntent *in, float dt) {
    bool doLeft = RepeatButton_Update(&p->l, in->left, dt, 0.17f, 0.035f);
    bool doRight = RepeatButton_Update(&p->r, in->right, dt, 0.17f, 0.035f);
    bool doSoftDrop = RepeatButton_Update(&p->s, in->softDrop, dt, 0.0f, 0.03f);
    if (g->phase == GS_PLAYING) {
        if (doLeft) Game_MoveLeft(g);
        if (doRight) Game_MoveRight(g);
        if (doSoftDrop) Game_SoftDrop(g);
        if (in->rotate) Game_Rotate(g, 1);
        if (in->hardDrop) Game_HardDrop(g);
        if (in->hold) Game_Hold(g);
        Game_Update(g, dt);
    }
}

static bool SameGame(const Game *a, const Game *b) {
    return a->phase == b->phase && a->current == b->current && a->rotation == b->rotation && a->px == b->px && a->py == b->py &&
           a->score == b->score && a->level == b->level && a->linesCleared == b->linesCleared && a->piecesLocked == b->piecesLocked &&
           a->elapsed == b->elapsed && a->timeLeft == b->timeLeft && a->hold == b->hold && a->lockTimer == b->lockTimer &&
           a->lockResets == b->lockResets && a->fallTimer == b->fallTimer && a->next == b->next &&
           memcmp(a->board.cells, b->board.cells, sizeof(a->board.cells)) == 0;
}

static void test_same_as_before(void) {
    Rng rng;
    Rng_Seed(&rng, 314);
    int frames = 0, diverged = 0, repeats = 0;
    for (int run = 0; run < 12; run++) {
        Game a, b;
        GameConfig cfg = {(GameMode)(run % MODE_COUNT), 6 + run * 2 % 19, 12 + run * 3 % 25, run & 1};
        Game_InitConfig(&a, cfg, (uint64_t)(900 + run), 0, 1, 0);
        Game_InitConfig(&b, cfg, (uint64_t)(900 + run), 0, 1, 0);
        OldPad old;
        memset(&old, 0, sizeof(old));
        PlayDriver d;
        memset(&d, 0, sizeof(d));
        PlayDriver_Start(&d, false, false);
        PlayIntent in;
        memset(&in, 0, sizeof(in));
        for (int f = 0; f < 3000; f++) {
            /* Keys held for a while, presses now and then, pauses. */
            if (Rng_Range(&rng, 12) == 0) in.left = !in.left;
            if (Rng_Range(&rng, 12) == 0) in.right = !in.right;
            if (Rng_Range(&rng, 20) == 0) in.softDrop = !in.softDrop;
            if (in.left && in.right) in.left = in.right = false;
            in.rotate = Rng_Range(&rng, 15) == 0;
            in.hardDrop = Rng_Range(&rng, 70) == 0;
            in.hold = Rng_Range(&rng, 90) == 0;
            if (Rng_Range(&rng, 400) == 0) { Game_TogglePause(&a); Game_TogglePause(&b); }
            float dt = FRAME * (0.5f + (float)Rng_Range(&rng, 100) / 100.0f);
            OldStep(&old, &a, &in, dt);
            PlayStepResult out;
            Play_Step(&d, &b, &in, dt, 1.0f, &out);
            if (out.moveRepeat) repeats++;
            if (!SameGame(&a, &b)) { diverged++; break; }
            Game_ConsumeFrameFlags(&a);
            Game_ConsumeFrameFlags(&b);
            frames++;
            if (a.phase == GS_GAMEOVER) {
                Game_Restart(&a, (uint64_t)f);
                Game_Restart(&b, (uint64_t)f);
            }
        }
    }
    CHECK(diverged == 0 && frames > 30000 && repeats > 100,
          "steps: with READY-GO off a run plays frame for frame as 2.3.0 did (auto-repeat, the order of moves, gravity)");
}

void test_feel_all(void) {
    test_replay_matches_the_clear();
    test_replay_in_real_runs();
    test_clear_beat();
    test_clear_stages();
    test_piece_motion();
    test_lock_shade_and_ghost();
    test_landing();
    test_hold_and_next();
    test_zen_sweep();
    test_ready_go();
    test_same_as_before();
}
