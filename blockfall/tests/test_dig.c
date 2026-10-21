/* Dig mode (3.4.0, REWORK_PLAN.md "Dig mode"), with no window: the garbage
 * the well starts with (eight rows, one gap each, never in the column of
 * the row below's), a seed's pieces the same as in every other mode, the
 * rise timing curve (8 s down to 3 s over 150 s), the rises themselves (the
 * stack moves up, the piece is lifted only when it would overlap, a block
 * pushed above the well tops the run out), the warning bar's fill and its
 * last quarter, Dug counting and its bonus, the level that stays at 1, a
 * pause holding the rise, the board keys and what a run records, the
 * panel's spec and layout, the results card, the DUG callout and nudge, the
 * stack's rise nudge and the thud, the setup words and the demo's claim. */
#include <stdio.h>
#include <string.h>
#include "check.h"
#include "../src/board.h"
#include "../src/game.h"
#include "../src/danger.h"
#include "../src/feel.h"
#include "../src/howtodemo.h"
#include "../src/hudfx.h"
#include "../src/layout.h"
#include "../src/menumodel.h"
#include "../src/results.h"
#include "../src/runrecord.h"
#include "../src/soundplan.h"
#include "../src/stats.h"

#define FRAME (1.0f / 60.0f)
#define NEAR(a, b) ((a) - (b) < 0.001f && (b) - (a) < 0.001f)

static Game Dig(int w, int h, uint64_t seed) {
    Game g;
    Game_InitConfig(&g, (GameConfig){MODE_DIG, w, h, true}, seed, 0, 1, 0);
    return g;
}

/* The garbage rows' gap columns from the floor up; returns how many rows
 * are garbage (-1 in out[] for a row that is not plain garbage). */
static int Gaps(const Board *b, int out[BOARD_MAX_TOTAL_H]) {
    int n = 0, total = Board_TotalH(b);
    for (int row = total - 1; row >= BOARD_HIDDEN && Board_RowHasGarbage(b, row); row--) out[n++] = Game_GarbageGap(b, row);
    return n;
}

/* Holds the piece where it is (a Freeze: gravity waits, nothing locks) so a
 * run can be advanced through the rises alone. */
static void Hold(Game *g) { g->freezeTimer = 1000.0f; }

/* Frames until the next rise; -1 if none came in `max` frames. */
static int FramesToRise(Game *g, int max) {
    Game_ConsumeFrameFlags(g); /* a rise still flagged from before is not this one */
    for (int i = 1; i <= max; i++) {
        Game_Update(g, FRAME);
        if (g->justRose) return i;
        Game_ConsumeFrameFlags(g);
    }
    return -1;
}

static void test_start(void) {
    bool rows = true, oneGap = true, differ = true, clearAbove = true, playing = true, filled = true;
    static const int kSizes[][2] = {{10, 20}, {6, 12}, {24, 36}, {14, 20}, {8, 26}, {7, 13}};
    for (size_t s = 0; s < sizeof(kSizes) / sizeof(kSizes[0]); s++) {
        for (uint64_t seed = 1; seed <= 6; seed++) {
            int w = kSizes[s][0], h = kSizes[s][1];
            Game g = Dig(w, h, seed * 977);
            int gaps[BOARD_MAX_TOTAL_H];
            int n = Gaps(&g.board, gaps);
            int want = DIG_START_ROWS < h - 4 ? DIG_START_ROWS : h - 4;
            if (n != want || Game_GarbageRows(&g.board) != want) rows = false;
            for (int i = 0; i < n; i++) {
                if (gaps[i] < 0 || gaps[i] >= w) oneGap = false;
                if (i > 0 && gaps[i] == gaps[i - 1]) differ = false;
            }
            for (int row = BOARD_HIDDEN; row < Board_TotalH(&g.board) - n; row++)
                for (int x = 0; x < w; x++)
                    if (g.board.cells[row][x] != CELL_EMPTY) clearAbove = false;
            if (g.phase != GS_PLAYING || g.digRises != 0 || g.dug != 0) playing = false;
            if (Board_CountFilled(&g.board) != n * (w - 1)) filled = false;
        }
    }
    CHECK(rows, "start: the well begins with eight garbage rows (a 12-row well keeps four rows clear)");
    CHECK(oneGap && filled, "start: every garbage row is grey but for exactly one gap");
    CHECK(differ, "start: no two neighbouring rows have their gap in the same column");
    CHECK(clearAbove && playing, "start: the rows above are clear, the piece comes in and play is on");
    Game m;
    Game_InitConfig(&m, (GameConfig){MODE_MARATHON, 10, 20, true}, 5, 0, 1, 0);
    Game z;
    Game_InitConfig(&z, (GameConfig){MODE_ZEN, 10, 20, true}, 5, 0, 1, 0);
    CHECK(Game_GarbageRows(&m.board) == 0 && Game_GarbageRows(&z.board) == 0 && Board_CountFilled(&m.board) == 0, "start: no other mode has garbage");
    CHECK(Game_GarbageGap(&m.board, Board_TotalH(&m.board) - 1) == -1, "start: an empty row is not a garbage row");
    Game_GarbageRow(&m.board, 10, 3);
    CHECK(Game_GarbageGap(&m.board, 10) == 3 && Board_RowHasGarbage(&m.board, 10) && !Board_RowFull(&m.board, 10), "start: a garbage row reads back its gap");
    m.board.cells[10][3] = PIECE_T;
    CHECK(Game_GarbageGap(&m.board, 10) == -1 && Board_RowFull(&m.board, 10) && Board_RowHasGarbage(&m.board, 10), "start: filled, it is full and still garbage");
    CHECK(Tetromino_Name((PieceType)CELL_GARBAGE)[0] == 'G', "start: the garbage cell has a name of its own");
    bool grey = true;
    for (int t = 0; t < PIECE_COUNT; t++) if (Tetromino_Color((PieceType)t) == Tetromino_Color((PieceType)CELL_GARBAGE)) grey = false;
    CHECK(grey, "start: ...and a colour no piece has");
}

static void test_seed(void) {
    Game a = Dig(10, 20, 4242), b = Dig(10, 20, 4242), c = Dig(10, 20, 4243);
    int ga[BOARD_MAX_TOTAL_H], gb[BOARD_MAX_TOTAL_H], gc[BOARD_MAX_TOTAL_H];
    int na = Gaps(&a.board, ga), nb = Gaps(&b.board, gb), nc = Gaps(&c.board, gc);
    CHECK(na == nb && memcmp(ga, gb, sizeof(int) * (size_t)na) == 0 && memcmp(a.queue, b.queue, sizeof(a.queue)) == 0 && a.current == b.current,
          "seed: the same seed gives the same garbage and the same pieces");
    CHECK(na == nc && memcmp(ga, gc, sizeof(int) * (size_t)na) != 0, "seed: another seed digs another well");
    Game m;
    Game_InitConfig(&m, (GameConfig){MODE_MARATHON, 10, 20, true}, 4242, 0, 1, 0);
    CHECK(m.current == a.current && m.next == a.next && memcmp(m.queue, a.queue, sizeof(m.queue)) == 0,
          "seed: the garbage draws from its own rng, so a seed's pieces are the Marathon's");
    /* The rises too: the same seed, the same rows. */
    Hold(&a);
    Hold(&b);
    for (int i = 0; i < 3; i++) { FramesToRise(&a, 1000); FramesToRise(&b, 1000); }
    na = Gaps(&a.board, ga);
    nb = Gaps(&b.board, gb);
    CHECK(na == nb && na == DIG_START_ROWS + 3 && memcmp(ga, gb, sizeof(int) * (size_t)na) == 0 && a.next == b.next,
          "seed: three rises later the two wells are still the same");
}

static void test_curve(void) {
    CHECK(NEAR(Game_DigInterval(0.0f), DIG_RISE_START_SECONDS) && NEAR(Game_DigInterval(-3.0f), DIG_RISE_START_SECONDS), "curve: the first wait is 8 s");
    CHECK(NEAR(Game_DigInterval(DIG_RISE_RAMP_SECONDS / 2.0f), 5.5f), "curve: 5.5 s half way through the ramp");
    CHECK(NEAR(Game_DigInterval(DIG_RISE_RAMP_SECONDS), DIG_RISE_END_SECONDS) && NEAR(Game_DigInterval(9999.0f), DIG_RISE_END_SECONDS),
          "curve: 3 s at 150 s and ever after");
    bool monotone = true;
    float last = Game_DigInterval(0.0f);
    for (float t = 0.5f; t < 200.0f; t += 0.5f) {
        float v = Game_DigInterval(t);
        if (v > last + 0.0001f || v < DIG_RISE_END_SECONDS - 0.0001f || v > DIG_RISE_START_SECONDS + 0.0001f) monotone = false;
        last = v;
    }
    CHECK(monotone, "curve: the wait only ever shrinks, and stays between 3 and 8 s");
}

static void test_rises(void) {
    Game g = Dig(10, 20, 7);
    Hold(&g);
    int before[BOARD_MAX_TOTAL_H], after[BOARD_MAX_TOTAL_H];
    int nb = Gaps(&g.board, before);
    CHECK(NEAR(g.digInterval, DIG_RISE_START_SECONDS) && g.digTimer == 0.0f, "rise: the first wait begins at 8 s");
    int frames = FramesToRise(&g, 1000);
    CHECK(frames >= 479 && frames <= 481, "rise: the first row comes after 8 s of play");
    CHECK(g.justRose && g.digRises == 1 && !g.justRosePiece, "rise: it says so for the frame, and the piece up at the top was not touched");
    int na = Gaps(&g.board, after);
    bool shifted = na == nb + 1;
    for (int i = 0; shifted && i < nb; i++) if (after[i + 1] != before[i]) shifted = false;
    CHECK(shifted, "rise: the old rows are one higher, the new one under them");
    CHECK(after[0] >= 0 && after[0] != before[0], "rise: the new row has one gap, not in the column of the row above it");
    CHECK(NEAR(g.digInterval, Game_DigInterval(g.elapsed)) && g.digInterval < DIG_RISE_START_SECONDS && g.digTimer < FRAME,
          "rise: the next wait is the curve's at this moment, and starts over");
    Game_ConsumeFrameFlags(&g);
    CHECK(!g.justRose, "rise: the flag is the frame's");
    float want = g.digInterval;
    frames = FramesToRise(&g, 1000);
    CHECK(frames > 0 && (float)frames * FRAME > want - FRAME * 1.5f && (float)frames * FRAME < want + FRAME * 1.5f, "rise: the second comes after that shorter wait");
    CHECK(g.digRises == 2 && Game_GarbageRows(&g.board) == DIG_START_ROWS + 2, "rise: ten garbage rows now");

    /* Deep into a run the wait is the curve's end. */
    g.elapsed = 400.0f;
    g.digTimer = g.digInterval;
    Game_Update(&g, FRAME);
    CHECK(g.justRose && NEAR(g.digInterval, DIG_RISE_END_SECONDS), "rise: after 150 s every wait is 3 s");
}

static void test_piece_lifted(void) {
    /* A piece resting on the stack is lifted with it. */
    Game g = Dig(10, 20, 9);
    int total = Board_TotalH(&g.board), top = total - DIG_START_ROWS; /* the first garbage row */
    g.current = PIECE_O;
    g.rotation = 0;
    g.px = 4;
    g.py = top - 2; /* the O's two rows end just above the garbage */
    g.lowestRow = g.py;
    CHECK(Board_TestFit(&g.board, g.current, g.rotation, g.px, g.py) && !Board_TestFit(&g.board, g.current, g.rotation, g.px, g.py + 1), "lift: the O rests on the garbage");
    g.digTimer = g.digInterval;
    Game_Update(&g, FRAME);
    CHECK(g.justRose && g.justRosePiece && g.py == top - 3 && g.lowestRow == top - 3 && g.phase == GS_PLAYING,
          "lift: the row pushed up under it and the O went up a row with it");
    CHECK(g.grounded, "lift: ...still resting on the stack");
    /* A piece in the air stays where it is. */
    Game h = Dig(10, 20, 9);
    h.current = PIECE_O;
    h.py = 6;
    h.digTimer = h.digInterval;
    Game_Update(&h, FRAME);
    CHECK(h.justRose && !h.justRosePiece && h.py == 6, "lift: a piece in the air is not moved");
}

static void test_topout(void) {
    /* A block on the top visible row is pushed above the well: over. */
    Game g = Dig(10, 20, 11);
    g.board.cells[BOARD_HIDDEN][0] = PIECE_J;
    g.digTimer = g.digInterval;
    Game_Update(&g, FRAME);
    CHECK(g.phase == GS_GAMEOVER && !g.won && g.digRises == 1, "topout: a block pushed above the well ends the run");
    CHECK(Results_KindOf(&g) == END_TOPOUT, "topout: ...as a top-out");
    /* A piece with nowhere to go ends it too. */
    Game h = Dig(10, 20, 11);
    h.current = PIECE_O;
    h.px = 4;
    h.py = 0;
    for (int x = 0; x < 10; x++) h.board.cells[2][x] = PIECE_J; /* right under the O (rows 0 and 1) */
    h.digTimer = h.digInterval;
    Game_Update(&h, FRAME);
    CHECK(h.phase == GS_GAMEOVER, "topout: a piece already at the top with the stack rising into it ends the run");
    /* Zen is untouched by any of it: no garbage, no rises. */
    Game z;
    Game_InitConfig(&z, (GameConfig){MODE_ZEN, 10, 20, true}, 11, 0, 1, 0);
    Hold(&z);
    CHECK(FramesToRise(&z, 700) < 0 && z.digRises == 0, "topout: no rise ever comes in another mode");
}

static void test_warning(void) {
    Game g = Dig(10, 20, 13);
    Hold(&g);
    CHECK(Game_DigFill(&g) == 0.0f && !Game_DigWarning(&g) && NEAR(Game_DigSecondsLeft(&g), 8.0f), "bar: empty at the start, 8 s to go");
    for (int i = 0; i < 240; i++) Game_Update(&g, FRAME); /* 4 s */
    CHECK(Game_DigFill(&g) > 0.49f && Game_DigFill(&g) < 0.51f && !Game_DigWarning(&g), "bar: half full half way, no warning yet");
    for (int i = 0; i < 126; i++) Game_Update(&g, FRAME); /* 6.1 s */
    CHECK(Game_DigFill(&g) > 0.75f && Game_DigWarning(&g) && Game_DigSecondsLeft(&g) < 2.0f, "bar: the last quarter is the warning");
    bool grows = true;
    float last = Game_DigFill(&g);
    for (int i = 0; i < 200 && !g.justRose; i++) {
        Game_Update(&g, FRAME);
        float now = Game_DigFill(&g);
        if (!g.justRose && now < last) grows = false;
        last = now;
    }
    CHECK(grows && g.justRose && Game_DigFill(&g) < 0.05f, "bar: it only fills, and starts again as the row comes in");
    Game m;
    Game_InitConfig(&m, (GameConfig){MODE_MARATHON, 10, 20, true}, 13, 0, 1, 0);
    m.digTimer = 5.0f;
    m.digInterval = 8.0f;
    CHECK(Game_DigFill(&m) == 0.0f && !Game_DigWarning(&m) && Game_DigSecondsLeft(&m) == 0.0f, "bar: nothing outside Dig");
    /* A pause holds the wait. */
    Game p = Dig(10, 20, 13);
    p.phase = GS_PAUSED;
    for (int i = 0; i < 1200; i++) Game_Update(&p, FRAME);
    CHECK(p.digRises == 0 && p.digTimer == 0.0f, "bar: a pause holds the next row where it is");
}

/* An I stood upright, dropped down column `col`. */
static void DropI(Game *g, int col) {
    g->current = PIECE_I;
    g->rotation = 1; /* its cells in local column 2 */
    g->px = col - 2;
    g->py = BOARD_HIDDEN;
    g->lowestRow = g->py;
    g->fallTimer = g->lockTimer = 0.0f;
    g->lastMoveWasTurn = false;
    Game_HardDrop(g);
}

static void test_dug(void) {
    Game g = Dig(10, 20, 17);
    memset(g.board.cells, CELL_EMPTY, sizeof(g.board.cells));
    int total = Board_TotalH(&g.board);
    Game_GarbageRow(&g.board, total - 1, 4);
    g.score = 0;
    DropI(&g, 4);
    /* the I's bottom cell fills the gap: a single, one row dug; the drop
     * fell from row 4 to row 20 (its cells 20 to 23): 16 rows, 32 points */
    CHECK(g.lastClearCount == 1 && g.lastDug == 1 && g.dug == 1, "dug: a garbage row cleared counts as one dug");
    CHECK(g.score == 100 + DIG_ROW_BONUS + 32, "dug: the single's 100, the row's bonus 100 and the drop's 32");
    CHECK(Game_GarbageRows(&g.board) == 0 && Board_CountFilled(&g.board) == 3, "dug: the row is gone, the rest of the I stays");
    Game_ConsumeFrameFlags(&g);
    CHECK(g.lastDug == 0, "dug: lastDug is the frame's");

    /* A row of pieces is not garbage: a line, not a dig. */
    memset(g.board.cells, CELL_EMPTY, sizeof(g.board.cells));
    for (int x = 0; x < 10; x++) g.board.cells[total - 1][x] = x == 4 ? (int8_t)CELL_EMPTY : (int8_t)PIECE_J;
    g.combo = -1; /* not a combo on the clear before */
    long before = g.score;
    DropI(&g, 4);
    CHECK(g.lastClearCount == 1 && g.lastDug == 0 && g.dug == 1 && g.score == before + 100 + 32, "dug: a row of pieces clears for its 100 alone");

    /* Four garbage rows with their gaps in line, and a block above so the
     * well is not emptied: a quad, four dug, 800 and 400. */
    memset(g.board.cells, CELL_EMPTY, sizeof(g.board.cells));
    for (int i = 1; i <= 4; i++) Game_GarbageRow(&g.board, total - i, 4);
    g.board.cells[total - 5][0] = CELL_GARBAGE;
    g.combo = -1;
    g.backToBack = false;
    before = g.score;
    DropI(&g, 4);
    CHECK(g.lastClearCount == 4 && g.lastDug == 4 && g.dug == 5 && g.stats.clears[4] == 1, "dug: a quad through four garbage rows digs four");
    CHECK(g.score == before + 800 + 4 * DIG_ROW_BONUS + 2 * 16 && !g.lastPerfectClear, "dug: 800, 400 for the rows and the drop");
    CHECK(Game_GarbageRows(&g.board) == 1, "dug: the garbage block above is still there");

    /* A mixed row: a piece block in a garbage row still makes it garbage. */
    memset(g.board.cells, CELL_EMPTY, sizeof(g.board.cells));
    Game_GarbageRow(&g.board, total - 1, 4);
    g.board.cells[total - 1][0] = PIECE_T;
    DropI(&g, 4);
    CHECK(g.lastDug == 1 && g.dug == 6, "dug: a garbage row with a piece block in it is still dug");

    /* The level never moves: the rises are the pressure. */
    memset(g.board.cells, CELL_EMPTY, sizeof(g.board.cells));
    g.linesCleared = 9;
    g.level = 1;
    float fall = g.fallInterval;
    Game_GarbageRow(&g.board, total - 1, 4);
    DropI(&g, 4);
    CHECK(g.linesCleared == 10 && g.level == 1 && !g.lastWasLevelUp && g.fallInterval == fall, "dug: ten lines do not raise Dig's level or its speed");
    CHECK(Layout_Spec(MODE_DIG, true, true).pipsStat == -1, "dug: ...so the panel has no level pips");
}

static void test_records(void) {
    char key[GHOSTLINK_MODE_LEN];
    Record_BoardKey(MODE_DIG, 10, 20, key, sizeof(key));
    CHECK(strcmp(key, "dig") == 0, "keys: Dig on the classic board is dig");
    Record_BoardKey(MODE_DIG, 14, 20, key, sizeof(key));
    CHECK(strcmp(key, "dig-14x20") == 0, "keys: ...and dig-14x20 on Wide");
    Record_LogKey(MODE_DIG, 10, 20, key, sizeof(key));
    CHECK(strcmp(key, "dig") == 0, "keys: runs.log says dig too");
    GameMode m;
    bool old;
    int w, h;
    CHECK(Record_ParseKey("dig", &m, &old, &w, &h) && m == MODE_DIG && !old && w == 10 && h == 20, "keys: dig parses back");
    CHECK(Record_ParseKey("dig-24x36", &m, &old, &w, &h) && m == MODE_DIG && w == 24 && h == 36, "keys: dig-24x36 too");
    CHECK(!Record_ParseKey("dig-10x20", NULL, NULL, NULL, NULL) && !Record_ParseKey("dig40", NULL, NULL, NULL, NULL), "keys: only the Marathon carries the classic size; dig40 is nothing");
    CHECK(!Record_HasOldBoard(MODE_DIG, 10, 20) && Record_UnitOf(MODE_DIG) == UNIT_POINTS, "keys: Dig has no old board and ranks points");
    CHECK(strcmp(Stats_ModeKey(MODE_DIG), "dig") == 0, "keys: stats.dat keeps Dig under [dig]");
    CHECK(strcmp(Game_ModeName(MODE_DIG), "Dig") == 0 && MODE_DIG == 5 && MODE_COUNT == 6, "keys: Dig is the sixth mode, after Power, so an old config's setup_mode still means what it did");

    Game g = Dig(10, 20, 3);
    g.score = 4321;
    g.piecesLocked = 40;
    RunRecord r = Record_Classify(&g, false);
    CHECK(r.counts && r.toBoard && !r.local && !r.toSave && r.value == 4321 && strcmp(r.key, "dig") == 0, "record: a Dig run goes on the shared dig board by its score");
    Game c = Dig(12, 22, 3);
    c.score = 4321;
    c.piecesLocked = 40;
    r = Record_Classify(&c, false);
    CHECK(r.toBoard && r.local && strcmp(r.key, "dig-12x22") == 0, "record: a custom size stays on this machine");
    r = Record_Classify(&g, true);
    CHECK(!r.counts && !r.toBoard, "record: a tuned Dig playtest records nothing");
}

static void test_panel(void) {
    HudSpec s = Layout_Spec(MODE_DIG, true, true);
    CHECK(s.stat[0] == STAT_SCORE && s.stat[1] == STAT_DUG && s.stat[2] == STAT_LINES && s.stat[3] == STAT_BEST, "panel: Score, Dug, Lines, Best");
    CHECK(s.rise && s.chips && !s.bars && s.mainChars == 7, "panel: the Next row bar and the combo chips, no power bars");
    CHECK(!Layout_Spec(MODE_MARATHON, true, true).rise && !Layout_Spec(MODE_POWER, true, true).rise, "panel: no bar in another mode");
    CHECK(strcmp(Layout_StatLabel(STAT_DUG), "Dug") == 0, "panel: the stat is called Dug");
    PlayLayout side = Layout_Play(10, 20, &s), strip = Layout_Play(24, 12, &s);
    CHECK(!LRect_Empty(side.rise) && LRect_Inside(side.rise, side.panel) && side.rise.y > side.b2b.y, "panel: at the side the bar sits under the chips");
    CHECK(strip.place == PLACE_STRIP && !LRect_Empty(strip.rise) && LRect_Inside(strip.rise, strip.panel) && strip.rise.x > strip.b2b.x,
          "panel: in the strip it takes the slot after them");
    HudSpec m = Layout_Spec(MODE_MARATHON, true, true);
    CHECK(LRect_Empty(Layout_Play(10, 20, &m).rise), "panel: the Marathon's layout has no bar");
}

static void test_card(void) {
    Game g = Dig(10, 20, 3);
    g.score = 5000;
    g.piecesLocked = 30;
    g.dug = 7;
    g.elapsed = 60.0f;
    g.phase = GS_GAMEOVER;
    RunFacts f = {false, 8000, 2, 10, 3, NULL, "Classic 10x20"};
    ResultsCard c;
    Results_Build(&c, &g, &f);
    CHECK(c.kind == END_TOPOUT && strcmp(c.title, "GAME OVER") == 0 && strcmp(c.sub, "Dig on Classic 10x20") == 0, "card: a Dig run ends as a top-out, named");
    CHECK(strcmp(c.statLabel[9], "Dug") == 0 && strcmp(c.statValue[9], "7") == 0, "card: the last stat is the rows dug");
    CHECK(!c.timeValue && c.bigValue == 5000 && strcmp(c.bestLine, "Best 8 000, 3 000 to go") == 0, "card: the score, against the dig board's best");
}

static void test_callout(void) {
    Game g = Dig(10, 20, 3);
    HudFx h;
    HudFx_Reset(&h, &g);
    g.justLocked = true;
    g.lastClearCount = 2;
    g.lastDug = 2;
    g.score = 400;
    g.lastLockType = PIECE_I;
    HudFx_React(&h, &g, false);
    const Callout *cs[CALLOUT_MAX];
    int n = HudFx_Callouts(&h, cs);
    bool dug = false, dbl = false;
    for (int i = 0; i < n; i++) {
        if (strcmp(cs[i]->text, "DUG 2") == 0) dug = true;
        if (strcmp(cs[i]->text, "DOUBLE") == 0) dbl = true;
    }
    CHECK(n == 2 && dug && dbl && strcmp(cs[0]->text, "DOUBLE") == 0, "callout: DUG 2 under the clear's word");
    CHECK(HudFx_Nudge(&h, STAT_DUG, true) == NUDGE_PX, "callout: the Dug stat nudges");
    HudFx_Reset(&h, &g);
    g.lastClearCount = 1;
    g.lastDug = 1;
    g.score = 600;
    HudFx_React(&h, &g, false);
    n = HudFx_Callouts(&h, cs);
    CHECK(n == 0 && h.pops == 1 && HudFx_Nudge(&h, STAT_DUG, true) == NUDGE_PX, "callout: one row dug has its popup and nudge alone, no word");
    Game_ConsumeFrameFlags(&g);
}

static void test_rise_look_and_sound(void) {
    Game g = Dig(10, 20, 3);
    Feel f;
    Feel_Reset(&f, &g, true);
    FeelFrame full = {true, true, false}, reduced = {false, false, false};
    CHECK(Feel_RiseDrop(&f, 30) == 0, "look: nothing before a rise");
    g.justRose = true;
    Feel_React(&f, &g, &full);
    CHECK(Feel_RiseDrop(&f, 30) == 30, "look: as the row comes in the stack is drawn a whole cell low");
    Feel_Tick(&f, 0.06f);
    int mid = Feel_RiseDrop(&f, 30);
    CHECK(mid > 0 && mid < 30, "look: ...easing up");
    Feel_Tick(&f, 0.07f);
    CHECK(Feel_RiseDrop(&f, 30) == 0, "look: ...home by 0.12 s");
    Game_ConsumeFrameFlags(&g);
    /* A clear still replaying ends: its rows have moved. */
    g.justLocked = true;
    g.lastClearCount = 1;
    g.lastClearRows[0] = Board_TotalH(&g.board) - 1;
    g.lockBoard = g.board;
    Feel_React(&f, &g, &full);
    Game_ConsumeFrameFlags(&g);
    CHECK(f.clear.on, "look: a clear replays");
    g.justRose = true;
    Feel_React(&f, &g, &full);
    CHECK(!f.clear.on && !f.land.on, "look: the rise ends the replay and the landed piece's show");
    Feel_Reset(&f, &g, true);
    Feel_React(&f, &g, &reduced);
    CHECK(Feel_RiseDrop(&f, 30) == 0, "look: a cut with Reduced motion");
    Game_ConsumeFrameFlags(&g);

    SoundPlan p;
    SoundPlan_Reset(&p, &g);
    SoundEvents none;
    memset(&none, 0, sizeof(none));
    g.justRose = true;
    SoundPlan_React(&p, &g, &none);
    SoundCue cues[SOUND_CUES];
    int n = SoundPlan_Take(&p, cues, SOUND_CUES);
    CHECK(n == 1 && cues[0].how == SND_PITCHED && cues[0].id == SFX_HARD_DROP && NEAR(cues[0].arg, DIG_RISE_PITCH) && cues[0].weight == SND_SMALL,
          "sound: a rise thuds low, as a small sound");
    /* With a lock on the same frame the lock keeps its own sounds. */
    g.justLocked = true;
    g.justHardDropped = true;
    g.lastClearCount = 1;
    SoundPlan_React(&p, &g, &none);
    n = SoundPlan_Take(&p, cues, SOUND_CUES);
    bool thud = false, clear = false, drop = false;
    for (int i = 0; i < n; i++) {
        if (cues[i].id == SFX_HARD_DROP && cues[i].how == SND_PITCHED) thud = true;
        if (cues[i].id == SFX_HARD_DROP && cues[i].how == SND_PLAY) drop = true;
        if (cues[i].id == SFX_CLEAR) clear = true;
    }
    CHECK(thud && clear && drop, "sound: a lock on the rise's frame keeps its clear and its accent");
    Game_ConsumeFrameFlags(&g);
    SoundPlan_Reset(&p, &g);
    SoundPlan_React(&p, &g, &none);
    CHECK(SoundPlan_Take(&p, cues, SOUND_CUES) == 0, "sound: nothing on a quiet frame");

    /* Danger and the music treat Dig as a stacking mode. */
    Danger d;
    Danger_Reset(&d);
    Danger_Update(&d, &g, FRAME);
    CHECK(!d.on && d.freeRows == 20 - DIG_START_ROWS, "danger: eight rows of garbage leave twelve clear, no danger yet");
    float v = Danger_Intensity(&g, d.freeRows, false);
    CHECK(v > INTENSITY_FLOOR && v < 0.5f, "danger: the music's intensity follows the stack");
    for (int row = BOARD_HIDDEN + 3; row < Board_TotalH(&g.board); row++) Game_GarbageRow(&g.board, row, 2);
    Danger_Update(&d, &g, FRAME);
    CHECK(d.on && d.warn, "danger: a well dug up to three rows from the top is in danger");
}

static void test_words_and_demo(void) {
    CHECK(strcmp(Setup_ModeNote(MODE_DIG), "rising rows") == 0, "words: Play setup's word for Dig");
    const char *about = Setup_ModeAbout(MODE_DIG);
    CHECK(strstr(about, "Dug") && strstr(about, "gap") && strlen(about) < 200, "words: ...and what it is");
    bool gaps = true;
    for (int w = BOARD_MIN_W; w <= BOARD_MAX_W; w += 3)
        for (int h = BOARD_MIN_H; h <= BOARD_MAX_H; h += 5)
            for (int y = 0; y < 8; y++) {
                int gap = Setup_PreviewGap(w, h, y);
                if (gap < 0 || gap >= w || (y > 0 && gap == Setup_PreviewGap(w, h, y - 1))) gaps = false;
            }
    CHECK(gaps, "words: the preview's garbage has one gap a row, never the row below's");
    DemoClaim c = HowToDemo_Claim(HOWTO_MODES, MODE_DIG);
    CHECK(c.count == 1 && c.clears[0] == 4 && c.gained[0] == 800 + 4 * DIG_ROW_BONUS, "demo: How to play's Dig digs a quad for 1 200");
    DemoClaim m = HowToDemo_Claim(HOWTO_MODES, MODE_MARATHON);
    CHECK(m.count == 1 && m.clears[0] == 1 && m.gained[0] == 100, "demo: the other modes keep their single");
    static HowToDemo d;
    HowToDemo_Start(&d, HOWTO_MODES, MODE_DIG);
    HowToDemo_Tick(&d, FRAME); /* the script's first step brings the I in */
    CHECK(d.game.cfg.mode == MODE_DIG && Game_GarbageRows(&d.game.board) == 5 && d.game.current == PIECE_I, "demo: its well is the four garbage rows and a block, with the I ready");
    for (int i = 0; i < 150; i++) HowToDemo_Tick(&d, FRAME);
    CHECK(d.game.dug == 4 && d.game.digRises == 0 && strstr(d.callout, "QUAD") && d.loops == 0, "demo: in its first loop it dug the four rows, and no row rose");
}

void test_dig_all(void) {
    test_start();
    test_seed();
    test_curve();
    test_rises();
    test_piece_lifted();
    test_topout();
    test_warning();
    test_dug();
    test_records();
    test_panel();
    test_card();
    test_callout();
    test_rise_look_and_sound();
    test_words_and_demo();
}
