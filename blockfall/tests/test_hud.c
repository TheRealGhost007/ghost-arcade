/* The play screen's lively parts (hudfx.c), with no window: which callouts a
 * lock raises and in what order, the three-line cap, their rise and cut, the
 * score popups, the count-up and its snap, the stat nudges, the level-up
 * pips, Ultra's colours and tick, and when a run has passed its best. The
 * events come from real locks through the rules' public calls. */
#include <string.h>
#include "check.h"
#include "../src/board.h"
#include "../src/game.h"
#include "../src/hudfx.h"

#define FRAME (1.0f / 60.0f)

static void Row(Game *g, int y, const char *pattern) {
    int row = Board_TotalH(&g->board) - 1 - y;
    for (int x = 0; x < g->board.w; x++) {
        char c = x < (int)strlen(pattern) ? pattern[x] : '.';
        g->board.cells[row][x] = c == '#' ? (int8_t)PIECE_J : CELL_EMPTY;
    }
}

/* A vertical I dropped into column 9 over `rows` rows that miss only that column. */
static void ClearWithI(Game *g, int rows) {
    Board_Init(&g->board, g->cfg.width, g->cfg.height);
    for (int y = 0; y < rows; y++) Row(g, y, "#########.");
    g->current = PIECE_I;
    g->rotation = 1;
    g->px = 7;
    g->py = BOARD_HIDDEN;
    g->curPower = PU_NONE;
    Game_HardDrop(g);
}

static void Frame(HudFx *h, Game *g, bool passed) {
    HudFx_React(h, g, passed);
    HudFx_Tick(h, FRAME, g->phase == GS_PLAYING);
    Game_ConsumeFrameFlags(g);
}

static int Texts(const HudFx *h, char out[][24]) {
    const Callout *c[CALLOUT_MAX];
    int n = HudFx_Callouts(h, c);
    for (int i = 0; i < n; i++) memcpy(out[i], c[i]->text, 24);
    return n;
}

static void test_callouts(void) {
    Game g;
    HudFx h;
    char t[3][24];
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, true}, 7, 0, 1, 0);
    HudFx_Reset(&h, &g);

    ClearWithI(&g, 1);
    Frame(&h, &g, false);
    CHECK(Texts(&h, t) == 0 && h.pops == 1 && strcmp(h.pop[0].text, "+100") != 0 && !h.pop[0].gold,
          "callouts: a single gets no word, only a violet popup (the lock's points, drop points too)");
    long single = g.score;
    CHECK(strncmp(h.pop[0].text, "+1", 2) == 0 && single > 100, "callouts: the popup says what the lock earned");

    ClearWithI(&g, 2);
    Frame(&h, &g, false);
    CHECK(Texts(&h, t) == 1 && strcmp(t[0], "DOUBLE") == 0, "callouts: a double says DOUBLE");
    ClearWithI(&g, 3);
    Frame(&h, &g, false);
    CHECK(Texts(&h, t) == 2 && strcmp(t[0], "TRIPLE") == 0 && strcmp(t[1], "COMBO 2") == 0,
          "callouts: the next clear's lines replace the ones showing; a third clear in a row adds COMBO 2");
    const Callout *cs[3];
    HudFx_Callouts(&h, cs);
    CHECK(HudFx_CalloutMaxSize(&h, cs[0]) == 32 && HudFx_CalloutMaxSize(&h, cs[1]) == 16, "callouts: TRIPLE is the headline, COMBO 2 under it");

    /* A back-to-back quad with a combo: QUAD / BACK-TO-BACK / COMBO n, the
     * older lines pushed out. */
    g.backToBack = true;
    ClearWithI(&g, 4);
    CHECK(g.lastWasB2B, "callouts: (the set-up is a back-to-back quad)");
    HudFx_React(&h, &g, false);
    int n = Texts(&h, t);
    CHECK(n == 3 && strcmp(t[0], "QUAD") == 0 && strcmp(t[1], "BACK-TO-BACK") == 0 && strncmp(t[2], "COMBO ", 6) == 0,
          "callouts: QUAD / BACK-TO-BACK / COMBO, in that order, the older lines gone");
    HudFx_Callouts(&h, cs);
    CHECK(cs[0]->tint == TINT_GOLD && cs[1]->tint == TINT_GOLD && strcmp(cs[1]->shortText, "B2B") == 0, "callouts: QUAD and BACK-TO-BACK are gold");
    CHECK(HudFx_CalloutMaxSize(&h, cs[0]) == 32 && HudFx_CalloutMaxSize(&h, cs[1]) == 16 && HudFx_CalloutMaxSize(&h, cs[2]) == 16,
          "callouts: QUAD is the 32 px headline, the lines under it 16 px at most");
    CHECK(h.pop[h.pops - 1].gold, "callouts: a quad's popup is gold (800 and up)");
    HudFx_Tick(&h, FRAME, true);
    Game_ConsumeFrameFlags(&g);

    /* More arriving at once than fit: the most important three. */
    HudFx_Reset(&h, &g);
    g.lastWasLevelUp = true;
    g.justLocked = true;
    g.lastClearCount = 4;
    g.lastWasB2B = true;
    g.lastCombo = 5;
    g.justZenReset = true;
    HudFx_React(&h, &g, true);
    n = Texts(&h, t);
    CHECK(n == 3 && strcmp(t[0], "QUAD") == 0 && strcmp(t[1], "NEW BEST") == 0 && strcmp(t[2], "FRESH START") == 0,
          "callouts: of six at once the three that matter most show (FRESH START, NEW BEST, QUAD), top to bottom");
    Game_ConsumeFrameFlags(&g);

    /* Rise and cut. */
    HudFx_Reset(&h, &g);
    g.lastWasLevelUp = true;
    HudFx_React(&h, &g, false);
    Game_ConsumeFrameFlags(&g);
    HudFx_Callouts(&h, cs);
    CHECK(HudFx_CalloutRise(cs[0], true) == 0 && HudFx_CalloutRise(cs[0], false) == CALLOUT_RISE, "callouts: start 8 px low (in place with Reduced motion)");
    for (int i = 0; i < 6; i++) HudFx_Tick(&h, FRAME, true);
    HudFx_Callouts(&h, cs);
    int mid = HudFx_CalloutRise(cs[0], true);
    for (int i = 0; i < 2; i++) HudFx_Tick(&h, FRAME, true);
    HudFx_Callouts(&h, cs);
    CHECK(mid > 0 && mid < CALLOUT_RISE && HudFx_CalloutRise(cs[0], true) == CALLOUT_RISE, "callouts: rise 8 px in 0.12 s, then hold");
    int frames = 8;
    while (h.calls > 0 && frames < 200) { HudFx_Tick(&h, FRAME, true); frames++; }
    CHECK(frames >= 53 && frames <= 55, "callouts: gone at 0.9 s (54 frames)");

    /* Power-ups name themselves in their own colour. */
    g.justLocked = true;
    g.lastPower = PU_LASER;
    HudFx_React(&h, &g, false);
    Game_ConsumeFrameFlags(&g);
    HudFx_Callouts(&h, cs);
    CHECK(h.calls == 1 && strcmp(cs[0]->text, "LASER") == 0 && cs[0]->tint == TINT_POWER && cs[0]->power == PU_LASER,
          "callouts: a power-up says its name in its colour");
    CHECK(HudFx_CalloutMaxSize(&h, cs[0]) == 32, "callouts: a line alone is a headline");
    g.justLocked = true;
    g.lastPower = PU_FREEZE;
    g.lastWasLevelUp = true;
    HudFx_React(&h, &g, true);
    Game_ConsumeFrameFlags(&g);
    HudFx_Callouts(&h, cs);
    CHECK(h.calls == 3 && strcmp(cs[0]->text, "FREEZE") == 0 && strncmp(cs[1]->text, "LEVEL ", 6) == 0 && strcmp(cs[2]->text, "NEW BEST") == 0 &&
              HudFx_CalloutMaxSize(&h, cs[2]) == 32 && HudFx_CalloutMaxSize(&h, cs[0]) == 16 && HudFx_CalloutMaxSize(&h, cs[1]) == 16,
          "callouts: NEW BEST outranks a power-up and LEVEL as the headline, wherever it sits");

    /* A QUAD, then a DOUBLE half a second later: the DOUBLE replaces it (it
     * used to shrink the QUAD to 16 px, then jump up into its place when the
     * QUAD's time ran out). A single in between changes nothing. */
    HudFx_Reset(&h, &g);
    g.backToBack = false;
    g.combo = -1;
    ClearWithI(&g, 4);
    Frame(&h, &g, false);
    for (int i = 0; i < 15; i++) HudFx_Tick(&h, FRAME, true);
    ClearWithI(&g, 1);
    Frame(&h, &g, false);
    char t2[3][24];
    CHECK(Texts(&h, t2) == 1 && strcmp(t2[0], "QUAD") == 0, "callouts: a single (no word) leaves the QUAD showing");
    for (int i = 0; i < 15; i++) HudFx_Tick(&h, FRAME, true);
    g.combo = -1;
    ClearWithI(&g, 2);
    Frame(&h, &g, false);
    HudFx_Callouts(&h, cs);
    CHECK(Texts(&h, t2) == 1 && strcmp(t2[0], "DOUBLE") == 0 && HudFx_CalloutMaxSize(&h, cs[0]) == 32 && HudFx_CalloutRise(cs[0], true) < CALLOUT_RISE,
          "callouts: the DOUBLE replaces the QUAD and rises as a new headline");
    int lifeFrames = 0, sizeChanges = 0, firstSize = HudFx_CalloutMaxSize(&h, cs[0]);
    while (h.calls > 0 && lifeFrames < 200) {
        HudFx_Callouts(&h, cs);
        if (HudFx_CalloutMaxSize(&h, cs[0]) != firstSize) sizeChanges++;
        HudFx_Tick(&h, FRAME, true);
        lifeFrames++;
    }
    CHECK(sizeChanges == 0 && lifeFrames >= 53 && lifeFrames <= 55, "callouts: it keeps its size its whole 0.9 s");
}

static void test_popups(void) {
    Game g;
    HudFx h;
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, false}, 3, 0, 1, 0);
    HudFx_Reset(&h, &g);
    g.level = 2;
    ClearWithI(&g, 4);
    HudFx_React(&h, &g, false);
    CHECK(h.pops == 1 && h.pop[0].gold && h.pop[0].text[0] == '+', "popups: a quad at level 2 pays 1 600 and up: gold");
    CHECK(h.pop[0].row > 15.0f && h.pop[0].row < 19.0f && h.pop[0].col > 9.0f && h.pop[0].col < 10.0f,
          "popups: start on the cleared rows (the bottom four), at the column the piece landed in");
    Game_ConsumeFrameFlags(&g);
    CHECK(HudFx_PopupRise(&h.pop[0], true) == 0 && HudFx_PopupRise(&h.pop[0], false) == 0, "popups: start where the rows were");
    int frames = 0;
    while (h.pops > 0 && frames < 100) {
        if (frames == 21) CHECK(HudFx_PopupRise(&h.pop[0], true) >= 11 && HudFx_PopupRise(&h.pop[0], true) <= 12, "popups: halfway up at 0.35 s");
        HudFx_Tick(&h, FRAME, true);
        frames++;
    }
    CHECK(frames >= 42 && frames <= 43, "popups: risen 24 px and gone at 0.7 s");
    CHECK(HudFx_PopupRise(&(Popup){.age = 0.69f}, false) == 0, "popups: hold still with Reduced motion");
}

static void test_countup(void) {
    Game g;
    HudFx h;
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, false}, 3, 0, 1, 0);
    g.score = 1000;
    HudFx_Reset(&h, &g);
    CHECK(h.shown == 1000, "count-up: a new run shows its score as it is");
    g.score = 1800;
    HudFx_React(&h, &g, false);
    HudFx_Tick(&h, FRAME, true);
    long first = h.shown;
    for (int i = 0; i < 9; i++) HudFx_Tick(&h, FRAME, true);
    long mid = h.shown;
    CHECK(first > 1000 && first < mid && mid < 1800, "count-up: climbs towards the new score");
    for (int i = 0; i < 12; i++) HudFx_Tick(&h, FRAME, true);
    CHECK(h.shown == 1800, "count-up: there in 0.35 s");
    g.score = 2600;
    HudFx_React(&h, &g, false);
    HudFx_Tick(&h, FRAME, false);
    CHECK(h.shown == 2600, "count-up: snaps to the real score while paused or over");
    g.score = 0;
    HudFx_React(&h, &g, false);
    CHECK(h.shown == 0, "count-up: a score that went down (a restart) is shown at once");
}

static void test_nudges_and_level(void) {
    Game g;
    HudFx h;
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, false}, 3, 0, 1, 0);
    HudFx_Reset(&h, &g);
    /* A hard drop that clears nothing: its few points do not nudge the score. */
    Board_Init(&g.board, 10, 20);
    g.current = PIECE_O; g.rotation = 0; g.px = 3; g.py = BOARD_HIDDEN;
    Game_HardDrop(&g);
    Frame(&h, &g, false);
    CHECK(HudFx_Nudge(&h, STAT_SCORE, true) == 0, "nudge: drop points alone do not nudge the score");
    g.linesCleared = 9;
    ClearWithI(&g, 1);
    CHECK(g.lastWasLevelUp && g.level == 2, "nudge: (the set-up levels up)");
    HudFx_React(&h, &g, false);
    Game_ConsumeFrameFlags(&g);
    CHECK(HudFx_Nudge(&h, STAT_SCORE, true) == NUDGE_PX && HudFx_Nudge(&h, STAT_LINES, true) == NUDGE_PX && HudFx_Nudge(&h, STAT_LEVEL, true) == NUDGE_PX,
          "nudge: a clear that levels up nudges Score, Lines and Level up 2 px");
    CHECK(HudFx_Nudge(&h, STAT_LEVEL, false) == 0, "nudge: never with Reduced motion");
    CHECK(HudFx_PipsLit(&h, 0, true) == 1 && HudFx_PipsLit(&h, 0, false) == LINES_PER_LEVEL, "level up: the pips start lighting one by one");
    CHECK(HudFx_FrameFlash(&h) == 1.0f && HudFx_GridGlow(&h) == 1.0f, "level up: the frame flashes and the grid brightens");
    for (int i = 0; i < 5; i++) HudFx_Tick(&h, FRAME, true);
    CHECK(HudFx_Nudge(&h, STAT_LEVEL, true) == 0, "nudge: lasts 0.08 s");
    int lit = HudFx_PipsLit(&h, 0, true);
    CHECK(lit > 1 && lit < LINES_PER_LEVEL, "level up: halfway through the pips");
    for (int i = 0; i < 5; i++) HudFx_Tick(&h, FRAME, true);
    CHECK(HudFx_PipsLit(&h, 0, true) == LINES_PER_LEVEL, "level up: all ten lit at 0.15 s");
    for (int i = 0; i < 9; i++) HudFx_Tick(&h, FRAME, true);
    CHECK(HudFx_PipsLit(&h, 0, true) == 0 && HudFx_FrameFlash(&h) == 0.0f && HudFx_GridGlow(&h) > 0.0f,
          "level up: then they empty, the frame is back at 0.3 s, the grid at 0.4 s");
    for (int i = 0; i < 7; i++) HudFx_Tick(&h, FRAME, true);
    CHECK(HudFx_GridGlow(&h) == 0.0f && HudFx_PipsLit(&h, 3, true) == 3, "level up: all back to normal");
}

static void test_ultra_and_best(void) {
    CHECK(Hud_UltraTint(120.0f) == ULTRA_NORMAL && Hud_UltraTint(31.0f) == ULTRA_NORMAL, "ultra: plain until 0:30");
    CHECK(Hud_UltraTint(30.99f) == ULTRA_GOLD && Hud_UltraTint(11.0f) == ULTRA_GOLD, "ultra: gold from 0:30");
    CHECK(Hud_UltraTint(10.99f) == ULTRA_RED && Hud_UltraTint(0.0f) == ULTRA_RED, "ultra: red from 0:10");
    CHECK(Hud_UltraSeconds(119.99f) == 119 && Hud_UltraSeconds(-1.0f) == 0, "ultra: the clock shows whole seconds");
    Game g;
    HudFx h;
    Game_InitConfig(&g, (GameConfig){MODE_ULTRA, 10, 20, true}, 3, 0, 1, 0);
    g.timeLeft = 12.5f;
    HudFx_Reset(&h, &g);
    int ticks = 0;
    for (int f = 0; f < 60 * 5; f++) {
        g.timeLeft -= FRAME;
        HudFx_React(&h, &g, false);
        if (h.nudge[STAT_TIME_LEFT] == 0.0f) ticks++;
        HudFx_Tick(&h, FRAME, true);
    }
    CHECK(ticks == 4, "ultra: the countdown ticks each second from 0:10 (10, 9, 8, 7 from 12.5 s to 7.5 s), not before");

    CHECK(Hud_PassesBest(24681, 24680, UNIT_POINTS, MODE_MARATHON, true, false), "best: passing it counts the first time");
    CHECK(!Hud_PassesBest(24681, 24680, UNIT_POINTS, MODE_MARATHON, true, true), "best: only once a run");
    CHECK(!Hud_PassesBest(24680, 24680, UNIT_POINTS, MODE_MARATHON, true, false), "best: equal is not past it");
    CHECK(!Hud_PassesBest(500, 0, UNIT_POINTS, MODE_POWER, true, false), "best: no best yet, nothing to pass");
    CHECK(!Hud_PassesBest(99999, 100, UNIT_POINTS, MODE_ZEN, true, false), "best: Zen keeps no scores");
    CHECK(!Hud_PassesBest(99999, 100, UNIT_TIME, MODE_SPRINT, true, false), "best: a Sprint is beaten by its time, at the end");
    CHECK(!Hud_PassesBest(99999, 100, UNIT_POINTS, MODE_ULTRA, false, false), "best: a tuned playtest is never recorded");
}

void test_hud_all(void) {
    test_callouts();
    test_popups();
    test_countup();
    test_nudges_and_level();
    test_ultra_and_best();
}
