/* Headless tests for what happens around a run (REWORK_PLAN.md, phase 5):
 * the run's counters (game.c), the pause menu and its "are you sure?" box
 * (pausemenu.c), the end show and the results card (results.c), the block
 * wipe (wipe.c), the countdown after a pause (playstep.c), the boards the
 * Scores browser lists (runrecord.c) and the new settings (persist.c).
 * Inputs are built by hand, one frame at a time, as uiinput.c hands them
 * over; files live in the temp HOME test_main.c made. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "check.h"
#include "../src/game.h"
#include "../src/pausemenu.h"
#include "../src/results.h"
#include "../src/wipe.h"
#include "../src/playstep.h"
#include "../src/runrecord.h"
#include "../src/localtable.h"
#include "../src/persist.h"
#include "rng.h"
#include "uikit.h"
#include "uikeymap.h"

#define DT (1.0f / 60.0f)

/* ------------------------------------------------------------- helpers */

static Input None(void) {
    Input in;
    memset(&in, 0, sizeof(in));
    in.lastDevice = DEV_KEYBOARD;
    in.focused = true;
    in.mouseX = in.mouseY = -100.0f;
    return in;
}

static Input Key(InputKey k) {
    Input in = None();
    in.key[k] = in.keyRepeat[k] = in.keyDown[k] = true;
    return in;
}

static Input Pad(PadButton b) {
    Input in = None();
    in.pad[b] = in.padRepeat[b] = in.padDown[b] = true;
    in.lastDevice = in.hintDevice = DEV_PAD;
    return in;
}

static Input Pointer(float x, float y, bool moved) {
    Input in = None();
    in.mouseX = x;
    in.mouseY = y;
    in.mouseInside = true;
    in.mouseMoved = moved;
    in.lastDevice = DEV_MOUSE;
    return in;
}

static const RunKeys kNoKeys = {false, false, false};

/* Enter pressed then let go: what the pause menu answers on the release. */
static PauseAction PauseEnter(PauseMenu *m, const QuickValues *q) {
    Input down = Key(IK_ENTER), up = None();
    PauseAction a = PauseMenu_Update(m, q, &kNoKeys, &down);
    if (a.kind != PA_NONE) return a;
    return PauseMenu_Update(m, q, &kNoKeys, &up);
}

static PauseAction PauseKey(PauseMenu *m, const QuickValues *q, InputKey k) {
    Input in = Key(k);
    PauseAction a = PauseMenu_Update(m, q, &kNoKeys, &in);
    Input none = None();
    PauseMenu_Update(m, q, &kNoKeys, &none);
    return a;
}

/* A fresh run on a board, the stack empty. */
static void NewRun(Game *g, GameMode mode, int w, int h, bool modern) {
    GameConfig cfg = {mode, w, h, modern};
    Game_InitConfig(g, cfg, 4242, 0, 1, 0);
}

/* Fills the bottom `rows` rows but the rightmost column and drops a vertical
 * I there: it clears exactly `rows` rows (none for 0). */
static void ClearRows(Game *g, int rows) {
    Board_Init(&g->board, g->cfg.width, g->cfg.height);
    int bottom = BOARD_HIDDEN + g->board.h - 1;
    for (int r = 0; r < rows; r++)
        for (int x = 0; x < g->board.w - 1; x++) g->board.cells[bottom - r][x] = (int8_t)PIECE_T;
    g->current = PIECE_I;
    g->rotation = 1;
    g->px = g->board.w - 3; /* the vertical I's blocks are at x + 2 */
    g->py = BOARD_HIDDEN;
    g->curPower = PU_NONE;
    Game_HardDrop(g);
}

/* --------------------------------------------------------- run counters */

static void test_run_counters(void) {
    Game g;
    NewRun(&g, MODE_MARATHON, 10, 20, false);
    CHECK(g.stats.combo == -1 && g.stats.bestCombo == 0 && g.stats.backToBacks == 0, "counters: a run starts with none");
    ClearRows(&g, 1);
    Game_ConsumeFrameFlags(&g);
    CHECK(g.stats.clears[1] == 1 && g.stats.combo == 0 && g.stats.bestCombo == 0, "counters: a single, a chain of one");
    ClearRows(&g, 2);
    Game_ConsumeFrameFlags(&g);
    CHECK(g.stats.clears[2] == 1 && g.stats.combo == 1 && g.stats.bestCombo == 1, "counters: a double right after: combo 1");
    ClearRows(&g, 0);
    Game_ConsumeFrameFlags(&g);
    CHECK(g.stats.combo == -1 && g.stats.bestCombo == 1, "counters: a lock that clears nothing breaks the chain, the best stays");
    ClearRows(&g, 4);
    Game_ConsumeFrameFlags(&g);
    CHECK(g.stats.clears[4] == 1 && g.stats.backToBacks == 0 && g.stats.lastQuad, "counters: a first quad is not back-to-back");
    ClearRows(&g, 0);
    ClearRows(&g, 4);
    CHECK(g.stats.clears[4] == 2 && g.stats.backToBacks == 1, "counters: a quad after a quad is back-to-back, a lock between does not break it");
    ClearRows(&g, 1);
    ClearRows(&g, 4);
    CHECK(g.stats.clears[4] == 3 && g.stats.backToBacks == 1, "counters: a single between two quads breaks back-to-back");
    CHECK(g.stats.bestCombo == 2, "counters: quad, single, quad is a chain of three: combo 2, the best");
    CHECK(g.piecesLocked == 8, "counters: pieces counts every lock");
    CHECK(g.lastWasB2B == false, "counters: the classic board never pays back-to-back, the counter still counts");
    CHECK(Game_Hold(&g) && g.stats.holds == 1, "counters: a hold");
    Game_Restart(&g, 7);
    CHECK(g.stats.clears[4] == 0 && g.stats.holds == 0 && g.stats.combo == -1 && g.piecesLocked == 0, "counters: a restart clears them");

    /* Power-ups fired. */
    NewRun(&g, MODE_POWER, 10, 20, true);
    Board_Init(&g.board, 10, 20);
    g.current = PIECE_O;
    g.rotation = 0;
    g.px = 3;
    g.py = BOARD_HIDDEN;
    g.curPower = PU_FREEZE;
    Game_HardDrop(&g);
    CHECK(g.stats.powers == 1, "counters: a power-up fired");

    /* Zen's fresh start breaks the chain as the rules' combo does. */
    NewRun(&g, MODE_ZEN, 10, 20, true);
    ClearRows(&g, 1);
    ClearRows(&g, 1);
    CHECK(g.stats.combo == 1, "counters: Zen chain");
    for (int r = 0; r < 4; r++)
        for (int x = 0; x < 10; x++) g.board.cells[BOARD_HIDDEN + r][x] = (int8_t)PIECE_S; /* the spawn rows blocked */
    Game_Hold(&g); /* the next piece cannot come in: a fresh start */
    CHECK(g.zenResets == 1 && g.phase == GS_PLAYING && g.stats.combo == -1 && g.stats.holds == 1,
          "counters: Zen's fresh start breaks the chain, as the rules' combo");

    /* Against the rules' own combo and back-to-back on a modern board, over
     * a long scripted run of random clear sizes. */
    NewRun(&g, MODE_MARATHON, 14, 20, true);
    Rng rng;
    Rng_Seed(&rng, 99);
    int maxCombo = -1, b2b = 0, bad = 0;
    long quads = 0;
    for (int i = 0; i < 3000; i++) {
        int k = (int)Rng_Range(&rng, 5);
        ClearRows(&g, k);
        if (g.phase != GS_PLAYING) { NewRun(&g, MODE_MARATHON, 14, 20, true); maxCombo = -1; b2b = 0; quads = 0; continue; }
        if (g.combo > maxCombo) maxCombo = g.combo;
        if (g.lastWasB2B) b2b++;
        if (k == 4) quads++;
        Game_ConsumeFrameFlags(&g);
        if (g.stats.combo != g.combo || g.stats.bestCombo != (maxCombo < 0 ? 0 : maxCombo) || g.stats.backToBacks != b2b ||
            g.stats.clears[4] != quads)
            bad++;
    }
    CHECK(bad == 0, "counters: combo, best combo, back-to-backs and quads match what modern scoring paid, lock for lock");
    printf("  around: 3000 scripted locks checked against modern scoring\n");
}

/* ---------------------------------------------------------- pause menu */

static void test_pause_menu(void) {
    Kit_SetScreen(620, 720);
    PauseMenu m;
    QuickValues q = {7, 7, true, true, true};
    PauseMenu_Open(&m, false);
    CHECK(m.open && m.page == PAUSE_MAIN && m.kit.focus == PI_RESUME, "pause: opens on Resume");

    KitList l;
    PauseMenu_Layout(&m, &q, &l);
    KitRect card = PauseMenu_Card(&m);
    CHECK(card.x >= 0 && card.y >= 0 && card.x + card.w <= 620 && card.y + card.h <= 720, "pause: the card is inside the window");
    bool inside = l.count == PAUSE_ITEMS, apart = true;
    for (int i = 0; i < l.count; i++) {
        KitRect r = l.items[i].r;
        if (r.x < card.x || r.y < card.y + 60 || r.x + r.w > card.x + card.w || r.y + r.h > card.y + card.h - 12) inside = false;
        for (int j = i + 1; j < l.count; j++) {
            KitRect s = l.items[j].r;
            if (r.y < s.y + s.h && s.y < r.y + r.h) apart = false;
        }
    }
    CHECK(inside && apart, "pause: five buttons inside the card, under the title, apart");
    CHECK(strcmp(Kit_Get(&l, PI_QUIT)->label, "Quit to menu") == 0 && strcmp(Kit_Get(&l, PI_RESUME)->label, "Resume") == 0, "pause: the labels");

    /* Enter acts when let go: Resume. */
    Input down = Key(IK_ENTER);
    PauseAction a = PauseMenu_Update(&m, &q, &kNoKeys, &down);
    CHECK(a.kind == PA_NONE, "pause: Enter acts only when let go");
    Input up = None();
    a = PauseMenu_Update(&m, &q, &kNoKeys, &up);
    CHECK(a.kind == PA_RESUME, "pause: ...then Resume");

    /* Pad A on Resume the same. */
    PauseMenu_Open(&m, false);
    Input padA = Pad(PB_A);
    a = PauseMenu_Update(&m, &q, &kNoKeys, &padA);
    Input padNone = None();
    padNone.lastDevice = DEV_PAD;
    a = PauseMenu_Update(&m, &q, &kNoKeys, &padNone);
    CHECK(a.kind == PA_RESUME, "pause: pad A on Resume resumes when let go");

    /* Esc, pad B, your Pause key and your Back key resume; Restart asks. */
    PauseMenu_Open(&m, false);
    Input esc = Key(IK_ESCAPE);
    CHECK(PauseMenu_Update(&m, &q, &kNoKeys, &esc).kind == PA_RESUME, "pause: Esc resumes");
    Input b = Pad(PB_B);
    CHECK(PauseMenu_Update(&m, &q, &kNoKeys, &b).kind == PA_RESUME, "pause: pad B resumes");
    RunKeys pauseKey = {true, false, false}, restartKey = {false, true, false}, backKey = {false, false, true};
    Input none = None();
    CHECK(PauseMenu_Update(&m, &q, &pauseKey, &none).kind == PA_RESUME, "pause: P or pad Start resume");
    CHECK(PauseMenu_Update(&m, &q, &backKey, &none).kind == PA_RESUME, "pause: your Back key resumes");
    CHECK(PauseMenu_Update(&m, &q, &restartKey, &none).kind == PA_RESTART, "pause: R or pad View ask to restart");
    Input escAsRestart = Key(IK_ESCAPE);
    CHECK(PauseMenu_Update(&m, &q, &restartKey, &escAsRestart).kind == PA_RESUME, "pause: Esc is Back in the menu even when a swap made it Restart");

    /* Walk down: each step ticks; Quit to menu. */
    PauseMenu_Open(&m, false);
    int moves = 0;
    for (int i = 0; i < 4; i++) if (PauseKey(&m, &q, IK_DOWN).moved) moves++;
    CHECK(moves == 4 && m.kit.focus == PI_QUIT, "pause: Down walks to Quit to menu, a tick a step");
    CHECK(PauseEnter(&m, &q).kind == PA_QUIT, "pause: Quit to menu");
    PauseKey(&m, &q, IK_DOWN);
    CHECK(m.kit.focus == PI_RESUME, "pause: the list goes round");
    PauseKey(&m, &q, IK_UP);
    PauseKey(&m, &q, IK_UP);
    CHECK(m.kit.focus == PI_HOWTO && PauseEnter(&m, &q).kind == PA_HOWTO, "pause: How to play");
    PauseKey(&m, &q, IK_UP);
    CHECK(m.kit.focus == PI_SETTINGS, "pause: Settings");
    a = PauseEnter(&m, &q);
    CHECK(a.opened && m.page == PAUSE_QUICK && m.kit.focus == PQ_EFFECTS, "pause: Settings opens the quick panel on Effects volume");

    /* The quick panel. */
    PauseMenu_Layout(&m, &q, &l);
    card = PauseMenu_Card(&m);
    inside = l.count == PAUSE_QUICK_ROWS + 1;
    for (int i = 0; i < l.count; i++) {
        KitRect r = l.items[i].r;
        if (r.x < card.x || r.y < card.y + 48 || r.x + r.w > card.x + card.w || r.y + r.h > card.y + card.h - 12) inside = false;
        if (l.items[i].id <= PQ_SHAKE) {
            KitRect row = PauseMenu_RowRect(&m, l.items[i].id);
            float labelEnd = row.x + 8 + 8.0f * (float)strlen(PauseMenu_RowLabel(l.items[i].id));
            if (r.x < labelEnd + 8) inside = false; /* the label and its control never touch */
        }
    }
    CHECK(inside, "pause: the quick panel's rows and Done fit the card, each label clear of its control");
    a = PauseKey(&m, &q, IK_RIGHT);
    CHECK(a.kind == PA_SET && a.id == PQ_EFFECTS && a.value == 8, "pause: Right raises Effects volume a block");
    a = PauseKey(&m, &q, IK_DOWN);
    CHECK(a.moved && m.kit.focus == PQ_MUSIC, "pause: Down to Music volume");
    a = PauseKey(&m, &q, IK_LEFT);
    CHECK(a.kind == PA_SET && a.id == PQ_MUSIC && a.value == 6, "pause: Left lowers Music volume");
    PauseKey(&m, &q, IK_DOWN);
    a = PauseEnter(&m, &q);
    CHECK(a.kind == PA_SET && a.id == PQ_SOUND && a.value == 0, "pause: Enter switches Sound off");
    PauseKey(&m, &q, IK_DOWN);
    a = PauseKey(&m, &q, IK_LEFT);
    CHECK(a.kind == PA_SET && a.id == PQ_GHOST && a.value == 0, "pause: Left turns the ghost off");
    PauseKey(&m, &q, IK_DOWN);
    a = PauseEnter(&m, &q);
    CHECK(a.kind == PA_SET && a.id == PQ_SHAKE && a.value == 0, "pause: Enter turns screen shake off");
    a = PauseKey(&m, &q, IK_ESCAPE);
    CHECK(a.closed && a.kind == PA_NONE && m.page == PAUSE_MAIN && m.kit.focus == PI_SETTINGS, "pause: Esc goes back to the list, on Settings");
    PauseEnter(&m, &q);
    for (int i = 0; i < 5; i++) PauseKey(&m, &q, IK_DOWN);
    CHECK(m.kit.focus == PQ_DONE, "pause: Done at the bottom");
    a = PauseEnter(&m, &q);
    CHECK(a.closed && m.page == PAUSE_MAIN, "pause: Done goes back to the list");

    /* The mouse: a click on Restart acts on release. */
    PauseMenu_Open(&m, false);
    PauseMenu_Layout(&m, &q, &l);
    KitRect rr = Kit_Get(&l, PI_RESTART)->r;
    float cx = rr.x + rr.w / 2, cy = rr.y + rr.h / 2;
    Input p = Pointer(cx, cy, true);
    PauseMenu_Update(&m, &q, &kNoKeys, &p);
    Input press = Pointer(cx, cy, false);
    press.mousePressed[MB_LEFT] = press.mouseDown[MB_LEFT] = true;
    CHECK(PauseMenu_Update(&m, &q, &kNoKeys, &press).kind == PA_NONE, "pause: a click acts on release");
    Input rel = Pointer(cx, cy, false);
    rel.mouseReleased[MB_LEFT] = true;
    CHECK(PauseMenu_Update(&m, &q, &kNoKeys, &rel).kind == PA_RESTART, "pause: ...Restart");

    /* The box: Keep playing first; Esc, pad B and your Back key keep playing. */
    PauseMenu_Ask(&m, PAUSE_ASK_QUIT, "Leave this run?", "It counts for 12 450.", "", false);
    CHECK(PauseMenu_Asking(&m) && m.dlgKit.focus == KIT_ID_DLG_SAFE, "pause: the box opens on Keep playing");
    a = PauseEnter(&m, &q);
    CHECK(a.closed && a.kind == PA_NONE && !PauseMenu_Asking(&m) && m.open, "pause: Keep playing from the menu stays in the menu");
    PauseMenu_Ask(&m, PAUSE_ASK_QUIT, "Leave this run?", "", "", false);
    a = PauseKey(&m, &q, IK_RIGHT);
    CHECK(a.moved && m.dlgKit.focus == KIT_ID_DLG_ACT, "pause: Right to Leave, with a tick");
    a = PauseEnter(&m, &q);
    CHECK(a.kind == PA_DO_QUIT, "pause: Leave leaves");
    PauseMenu_Ask(&m, PAUSE_ASK_RESTART, "Restart this run?", "", "", false);
    PauseKey(&m, &q, IK_RIGHT);
    CHECK(PauseEnter(&m, &q).kind == PA_DO_RESTART, "pause: Restart restarts");
    PauseMenu_Ask(&m, PAUSE_ASK_RESTART, "Restart this run?", "", "", false);
    PauseKey(&m, &q, IK_RIGHT);
    a = PauseKey(&m, &q, IK_ESCAPE);
    CHECK(a.closed && !PauseMenu_Asking(&m), "pause: Esc in the box keeps playing even on Restart");
    PauseMenu_Ask(&m, PAUSE_ASK_QUIT, "Leave this run?", "", "", false);
    a = PauseMenu_Update(&m, &q, &backKey, &none);
    CHECK(a.closed && !PauseMenu_Asking(&m), "pause: your Back key keeps playing");
    PauseMenu_Ask(&m, PAUSE_ASK_QUIT, "Leave this run?", "", "", false);
    a = PauseMenu_Update(&m, &q, &pauseKey, &none);
    CHECK(a.kind == PA_NONE && PauseMenu_Asking(&m), "pause: P does nothing while the box asks");
    a = PauseMenu_Update(&m, &q, &restartKey, &none);
    CHECK(a.kind == PA_NONE && PauseMenu_Asking(&m), "pause: R does nothing while the box asks");
    Input bb = Pad(PB_B);
    a = PauseMenu_Update(&m, &q, &kNoKeys, &bb);
    CHECK(a.closed && !PauseMenu_Asking(&m), "pause: pad B keeps playing");

    /* Asked from play (R or View): Keep playing goes straight back to play. */
    PauseMenu_Close(&m);
    PauseMenu_Ask(&m, PAUSE_ASK_RESTART, "Restart this run?", "", "", true);
    CHECK(m.open && PauseMenu_Asking(&m), "pause: R in play opens the box over the menu");
    a = PauseEnter(&m, &q);
    CHECK(a.kind == PA_RESUME, "pause: ...and Keep playing plays on");

    /* The dim and the slide. */
    PauseMenu_Open(&m, false);
    CHECK(PauseMenu_SlideOffset(&m, true) == PAUSE_SLIDE_PX && PauseMenu_Dim(&m, true) == 0.0f, "pause: the card starts 24 px low, nothing dimmed");
    CHECK(PauseMenu_SlideOffset(&m, false) == 0 && PauseMenu_Dim(&m, false) == 1.0f, "pause: Reduced motion: in place, dimmed at once");
    int last = 99;
    bool down2 = true;
    for (int f = 0; f < 10; f++) {
        PauseMenu_Tick(&m, DT);
        int o = PauseMenu_SlideOffset(&m, true);
        if (o > last) down2 = false;
        last = o;
    }
    CHECK(down2 && last == 0 && PauseMenu_Dim(&m, true) == 1.0f, "pause: it slides up into place and the dim is full by 0.12 s");
    PauseMenu_Open(&m, true);
    KitRect withNote = PauseMenu_Card(&m);
    CHECK(withNote.h == PAUSE_CARD_H + PAUSE_NOTE_H, "pause: a focus loss makes room for its note");

    /* The box's words. */
    Game g;
    NewRun(&g, MODE_MARATHON, 10, 20, false);
    CHECK(!PauseMenu_ShouldAsk(&g), "pause: a run with no points never asks");
    g.score = 12450;
    g.piecesLocked = 3;
    CHECK(PauseMenu_ShouldAsk(&g), "pause: a run with points asks");
    char title[64], body[96], act[24];
    PauseMenu_AskWords(PAUSE_ASK_QUIT, &g, false, "Classic 10x20", title, sizeof(title), body, sizeof(body), act, sizeof(act));
    CHECK(strcmp(title, "Leave this run?") == 0 && strcmp(body, "It counts for 12 450 on Classic 10x20.") == 0 && strcmp(act, "Leave") == 0,
          "pause: Leave this run? It counts for 12 450");
    PauseMenu_AskWords(PAUSE_ASK_RESTART, &g, false, "Classic 10x20", title, sizeof(title), body, sizeof(body), act, sizeof(act));
    CHECK(strcmp(title, "Restart this run?") == 0 && strcmp(act, "Restart") == 0, "pause: Restart this run?");
    PauseMenu_AskWords(PAUSE_ASK_QUIT, &g, true, "Classic 10x20", title, sizeof(title), body, sizeof(body), act, sizeof(act));
    CHECK(strstr(body, "tuned") != NULL, "pause: a tuned run says it is never recorded");
    NewRun(&g, MODE_SPRINT, 10, 20, false);
    g.score = 900;
    g.piecesLocked = 2;
    PauseMenu_AskWords(PAUSE_ASK_QUIT, &g, false, "Classic 10x20", title, sizeof(title), body, sizeof(body), act, sizeof(act));
    CHECK(strstr(body, "not ranked") != NULL, "pause: an unfinished Sprint is not ranked");
    NewRun(&g, MODE_ZEN, 10, 20, false);
    g.score = 900;
    g.piecesLocked = 2;
    PauseMenu_AskWords(PAUSE_ASK_QUIT, &g, false, "Classic 10x20", title, sizeof(title), body, sizeof(body), act, sizeof(act));
    CHECK(strstr(body, "Zen keeps no scores") != NULL, "pause: Zen keeps no scores");
    NewRun(&g, MODE_POWER, 13, 21, false);
    g.score = 1500;
    g.piecesLocked = 2;
    PauseMenu_AskWords(PAUSE_ASK_QUIT, &g, false, "Custom 13x21", title, sizeof(title), body, sizeof(body), act, sizeof(act));
    CHECK(strcmp(body, "It counts for 1 500 on Custom 13x21.") == 0, "pause: a custom board names itself");
}

/* --------------------------------------------------------- results card */

static RunFacts Facts(long best) {
    RunFacts f = {false, best, 0, 0, 0, NULL, "Classic 10x20"};
    return f;
}

static void test_results_card(void) {
    Game g;
    ResultsCard c;
    NewRun(&g, MODE_MARATHON, 10, 20, false);
    g.phase = GS_GAMEOVER;
    g.score = 15020;
    g.piecesLocked = 120;
    g.linesCleared = 47;
    g.level = 5;
    g.elapsed = 240.0f;
    RunFacts f = Facts(26714);
    f.rankHere = 3;
    f.worldRows = 8;
    f.worldRank = 14;
    Results_Build(&c, &g, &f);
    CHECK(c.kind == END_TOPOUT && strcmp(c.title, "GAME OVER") == 0 && !c.timeValue && c.bigValue == 15020, "results: GAME OVER, the score");
    CHECK(strcmp(c.sub, "Marathon on Classic 10x20") == 0, "results: the mode and board");
    CHECK(!c.newBest && strcmp(c.bestLine, "Best 26 714, 11 694 to go") == 0, "results: Best 26 714, 11 694 to go");
    CHECK(strcmp(c.rankLine, "#3 on this machine    World: would be #14") == 0, "results: #3 on this machine, world would be #14");
    CHECK(strcmp(c.statLabel[0], "Lines") == 0 && strcmp(c.statValue[0], "47") == 0, "results: lines");
    CHECK(strcmp(c.statLabel[2], "Pieces") == 0 && strcmp(c.statValue[2], "120") == 0, "results: pieces");
    CHECK(strcmp(c.statLabel[3], "Per second") == 0 && strcmp(c.statValue[3], "0.50") == 0, "results: pieces per second");
    CHECK(strcmp(c.statLabel[4], "Time") == 0 && strcmp(c.statValue[4], "4:00") == 0, "results: time");
    CHECK(strcmp(c.statLabel[9], "Lines/min") == 0 && strcmp(c.statValue[9], "11.8") == 0, "results: lines a minute");

    g.score = 30000;
    Results_Build(&c, &g, &f);
    CHECK(c.newBest && strcmp(c.bestLine, "Old best 26 714") == 0, "results: a new best, and the old one under it");
    g.score = 26714;
    Results_Build(&c, &g, &f);
    CHECK(!c.newBest && strcmp(c.bestLine, "Best 26 714, matched") == 0, "results: matching the best is not a new one");
    f.best = 0;
    g.score = 120;
    Results_Build(&c, &g, &f);
    CHECK(c.newBest && strcmp(c.bestLine, "No best before this one") == 0, "results: the first run on a board is its best");
    f.tuned = true;
    g.score = 99999;
    f.best = 5;
    Results_Build(&c, &g, &f);
    CHECK(!c.newBest && strstr(c.bestLine, "tuned") && c.rankLine[0] == '\0', "results: a tuned run is never a new best, nor ranked");
    f.tuned = false;
    g.score = 0;
    Results_Build(&c, &g, &f);
    CHECK(!c.newBest && strcmp(c.bestLine, "No points, so nothing is recorded") == 0 && c.rankLine[0] == '\0', "results: no points, nothing recorded");
    g.score = 40;
    g.piecesLocked = 0;
    Results_Build(&c, &g, &f);
    CHECK(!c.newBest && strcmp(c.bestLine, "Nothing landed, so nothing is recorded") == 0, "results: points from soft drops but nothing landed: not a run either");
    g.piecesLocked = 120;

    /* A trophy shows in gold. */
    g.score = 200;
    f.best = 100;
    f.trophy = "Picking Up Speed";
    Results_Build(&c, &g, &f);
    CHECK(c.noteGold && strcmp(c.note, "Trophy earned: Picking Up Speed") == 0, "results: a trophy just earned");
    f.trophy = NULL;
    f.worldRows = 0;
    f.rankHere = 0;
    Results_Build(&c, &g, &f);
    CHECK(strstr(c.rankLine, "World: waits for a sync") != NULL && strstr(c.rankLine, "Not in the top ten here") != NULL,
          "results: no world board yet; not in the top ten");

    /* Sprint: cleared, faster and slower; unfinished. */
    NewRun(&g, MODE_SPRINT, 10, 20, false);
    g.phase = GS_GAMEOVER;
    g.won = true;
    g.linesCleared = 40;
    g.score = 9000;
    g.piecesLocked = 100;
    g.elapsed = 84.02f;
    f = Facts(Record_SprintValue(72.40f));
    f.rankHere = 2;
    f.worldRows = 2;
    f.worldRank = 3;
    Results_Build(&c, &g, &f);
    CHECK(c.kind == END_SPRINT_CLEAR && strcmp(c.title, "SPRINT CLEAR") == 0 && c.timeValue && c.bigValue == 8402, "results: SPRINT CLEAR shows the time");
    CHECK(!c.newBest && strcmp(c.bestLine, "Best 1:12.40, 11.62 s behind") == 0, "results: a slower Sprint is behind by seconds");
    CHECK(strcmp(c.statLabel[4], "Points") == 0 && strcmp(c.statValue[4], "9 000") == 0, "results: a Sprint's points in the numbers");
    g.elapsed = 70.0f;
    Results_Build(&c, &g, &f);
    CHECK(c.newBest && strcmp(c.bestLine, "Old best 1:12.40") == 0, "results: a faster Sprint is a new best");
    g.won = false;
    g.linesCleared = 31;
    Results_Build(&c, &g, &f);
    CHECK(c.kind == END_TOPOUT && !c.timeValue && c.bigValue == 9000 && c.rankLine[0] == '\0', "results: an unfinished Sprint: the points, no rank");
    CHECK(strcmp(c.note, "Only a finished Sprint goes on the board") == 0, "results: ...and why");
    CHECK(strcmp(c.statLabel[9], "Lines left") == 0 && strcmp(c.statValue[9], "9") == 0, "results: lines left");

    /* Ultra, Zen, Power, a custom size. */
    NewRun(&g, MODE_ULTRA, 10, 20, false);
    g.won = true;
    g.score = 5000;
    g.piecesLocked = 10;
    g.elapsed = 120.0f;
    f = Facts(4000);
    Results_Build(&c, &g, &f);
    CHECK(c.kind == END_TIME_UP && strcmp(c.title, "TIME UP") == 0 && c.newBest, "results: TIME UP, a new best");
    NewRun(&g, MODE_ZEN, 10, 20, false);
    g.score = 5000;
    g.piecesLocked = 10;
    g.zenResets = 3;
    Results_Build(&c, &g, &f);
    CHECK(!c.newBest && strcmp(c.bestLine, "Zen keeps no scores") == 0 && strcmp(c.statLabel[9], "Fresh starts") == 0 &&
              strcmp(c.statValue[9], "3") == 0, "results: Zen keeps no scores, counts its fresh starts");
    NewRun(&g, MODE_POWER, 13, 21, false);
    g.score = 5000;
    g.piecesLocked = 10;
    g.stats.powers = 4;
    f = Facts(1000);
    f.board = "Custom 13x21";
    f.rankHere = 1;
    Results_Build(&c, &g, &f);
    CHECK(strcmp(c.statLabel[9], "Power-ups") == 0 && strcmp(c.statValue[9], "4") == 0, "results: power-ups fired");
    CHECK(strcmp(c.rankLine, "#1 on this machine    Custom sizes stay here") == 0, "results: a custom size stays on this machine");

    /* Where it would stand on the world board: the player's own rows aside. */
    GhostScore rows[4] = {{"marathon", "Ghost", 90000, "2026-10-01"}, {"marathon", "TRG", 50000, "2026-10-01"},
                          {"marathon", "Moth", 40000, "2026-10-01"}, {"marathon", "Pix", 100, "2026-10-01"}};
    CHECK(Results_WouldRank(rows, 4, 45000, "TRG") == 2, "results: would be #2 (your own better row aside)");
    CHECK(Results_WouldRank(rows, 4, 95000, "TRG") == 1, "results: would be #1");
    CHECK(Results_WouldRank(rows, 4, 50, "TRG") == 4, "results: would be last");
    CHECK(Record_FindRow(rows, 4, "Moth", 40000, "2026-10-01") == 3 && Record_FindRow(rows, 4, "Moth", 40001, NULL) == 0,
          "results: the run's own row is found by name, value and date");
    /* the classic best from before the tables (save.dat, no row) is a place */
    GhostScore classic[2] = {{"marathon", "Ghost", 15020, "2026-10-14"}, {"marathon", "Ghost", 12248, "2026-09-19"}};
    CHECK(Results_RankHere(classic, 2, "Ghost", 15020, "2026-10-14", 74372) == 2, "results: a row-less 74 372 above the run: #2 here");
    CHECK(Results_RankHere(classic, 2, "Ghost", 15020, "2026-10-14", 0) == 1, "results: no row-less best: the table's place");
    CHECK(Results_RankHere(classic, 2, "Ghost", 15020, "2026-10-14", 15020) == 1, "results: the run is that best: no extra place");
    CHECK(Results_RankHere(classic, 2, "Ghost", 15020, "2026-10-14", 12248) == 1, "results: a save.dat best with its row is counted once");
    GhostScore fixture[3] = {{"marathon", "TRG", 24680, "2026-10-14"}, {"marathon", "TRG", 15020, "2026-10-14"}, {"marathon", "TRG", 9100, "2026-10-05"}};
    CHECK(Results_RankHere(fixture, 3, "TRG", 15020, "2026-10-14", 24680) == 2, "results: save.dat's best that is also the table's top is counted once");
    CHECK(Results_RankHere(fixture, 3, "TRG", 1, "2026-10-14", 99999) == 0, "results: not in the top ten stays 0");

    /* The layout: the card fits the window, the buttons and numbers fit the
     * card and none overlap. */
    KitRect card = Results_CardRect();
    CHECK(card.x >= 0 && card.y >= 0 && card.x + card.w <= 620 && card.y + card.h <= 720, "results: the card fits the window");
    Results res;
    Results_Begin(&res, &c, true);
    KitList l;
    Results_Layout(&res, &l);
    bool ok = l.count == 4;
    for (int i = 0; i < l.count; i++) {
        KitRect r = l.items[i].r;
        if (r.x < card.x + 8 || r.x + r.w > card.x + card.w - 8 || r.y + r.h > card.y + card.h - 8) ok = false;
        if ((int)strlen(l.items[i].label) * l.items[i].textSize > (int)r.w - 16) ok = false;
        for (int j = i + 1; j < l.count; j++) {
            KitRect s = l.items[j].r;
            if (r.x < s.x + s.w && s.x < r.x + r.w && r.y < s.y + s.h && s.y < r.y + r.h) ok = false;
        }
    }
    /* every number fits beside its label, at 8 px at least (16 when it can) */
    bool statsFit = true;
    for (int i = 0; i < RESULTS_STATS; i++) {
        KitRect r = Results_StatRect(i);
        if (Kit_TextWidth(c.statLabel[i], 8) + 8 + Kit_TextWidth("999 999", 8) > (int)r.w) statsFit = false;
        if (Kit_TextWidth("Back-to-back", 8) + 8 + Kit_TextWidth("9 999", 16) > (int)r.w) statsFit = false;
    }
    CHECK(statsFit, "results: the widest labels hold a 6-digit number at 8 px, and a 4-digit one at 16");
    KitRect lastStat = Results_StatRect(RESULTS_STATS - 1);
    CHECK(ok && lastStat.y + lastStat.h < Kit_Get(&l, RI_AGAIN)->r.y && lastStat.x + lastStat.w <= card.x + card.w - 16,
          "results: four buttons and ten numbers inside the card, apart, every label fits");
    CHECK(res.kit.focus == RI_AGAIN, "results: Play again is picked first");
}

static void test_results_timeline(void) {
    Kit_SetScreen(620, 720);
    Game g;
    ResultsCard c;
    NewRun(&g, MODE_MARATHON, 10, 20, false);
    g.score = 12000;
    g.piecesLocked = 40;
    RunFacts f = Facts(10000);
    Results_Build(&c, &g, &f);
    Results r;
    Results_Begin(&r, &c, true);
    Input none = None();

    /* The first 0.4 s ignore everything, so a held hard drop does nothing. */
    Input space = Key(IK_SPACE);
    RunKeys restart = {false, true, false}, back = {false, false, true};
    bool ignored = true;
    for (int f2 = 0; f2 < 20; f2++) {
        ResultsStep s = Results_Update(&r, &restart, &space, DT);
        if (s.action != RA_NONE || s.skipped) ignored = false;
    }
    CHECK(ignored && r.t < END_INPUT_GATE + DT && !Results_Done(&r), "results: input is ignored for the first 0.4 s");
    while (!Results_InputOpen(&r)) Results_Update(&r, &kNoKeys, &none, DT);
    ResultsStep s = Results_Update(&r, &kNoKeys, &space, DT);
    CHECK(s.skipped && s.stamp && Results_Done(&r) && Results_Shown(&r) == 12000, "results: a press then jumps to the end, the stamp goes off");
    s = Results_Update(&r, &kNoKeys, &none, DT);
    CHECK(!s.stamp && s.action == RA_NONE, "results: the stamp goes off once, and the skip's press presses nothing");

    /* After the show: Play again picked first, Enter on release. */
    Input enter = Key(IK_ENTER);
    s = Results_Update(&r, &kNoKeys, &enter, DT);
    CHECK(s.action == RA_NONE, "results: Enter acts when let go");
    s = Results_Update(&r, &kNoKeys, &none, DT);
    CHECK(s.action == RA_AGAIN, "results: ...Play again");
    Input right = Key(IK_DOWN);
    s = Results_Update(&r, &kNoKeys, &right, DT);
    CHECK(s.moved && r.kit.focus != RI_AGAIN, "results: Down moves to the buttons below");
    Input esc = Key(IK_ESCAPE);
    CHECK(Results_Update(&r, &kNoKeys, &esc, DT).action == RA_MENU, "results: Esc is Menu");
    CHECK(Results_Update(&r, &back, &none, DT).action == RA_MENU, "results: your Back key is Menu");
    CHECK(Results_Update(&r, &restart, &none, DT).action == RA_AGAIN, "results: R or pad View play again");
    Input escR = Key(IK_ESCAPE);
    CHECK(Results_Update(&r, &restart, &escR, DT).action == RA_MENU, "results: Esc is Menu even when a swap made it Restart");
    Input b = Pad(PB_B);
    CHECK(Results_Update(&r, &kNoKeys, &b, DT).action == RA_MENU, "results: pad B is Menu");
    /* Every button by keys. */
    Results_Begin(&r, &c, true);
    r.t = Results_DoneAt(&r) + 0.1f;
    r.counted = true;
    int got[5] = {0};
    Input rightKey = Key(IK_RIGHT), downKey = Key(IK_DOWN), leftKey = Key(IK_LEFT);
    Results_Update(&r, &kNoKeys, &downKey, DT);
    Results_Update(&r, &kNoKeys, &leftKey, DT);
    Results_Update(&r, &kNoKeys, &leftKey, DT);
    CHECK(r.kit.focus == RI_SETUP, "results: Down, then Left to Change setup");
    for (int i = 0; i < 3; i++) {
        Input ent = Key(IK_ENTER);
        Results_Update(&r, &kNoKeys, &ent, DT);
        s = Results_Update(&r, &kNoKeys, &none, DT);
        if (s.action > RA_NONE && s.action <= RA_MENU) got[s.action]++;
        Results_Update(&r, &kNoKeys, &rightKey, DT);
    }
    CHECK(got[RA_SETUP] + got[RA_SCORES] + got[RA_MENU] == 3 && got[RA_SETUP] && got[RA_SCORES] && got[RA_MENU],
          "results: Change setup, Scores and Menu each act");

    /* Esc and R act at once during the show too, once input is open. */
    Results_Begin(&r, &c, true);
    while (!Results_InputOpen(&r)) Results_Update(&r, &kNoKeys, &none, DT);
    CHECK(Results_Update(&r, &restart, &none, DT).action == RA_AGAIN, "results: R during the show plays again at once");

    /* The count-up: it rises, ticks rise in pitch at most every 0.05 s, the
     * stamp once at the end with the exact value. */
    Results_Begin(&r, &c, true);
    long last = -1;
    bool rising = true;
    int ticks = 0, stamps = 0;
    float lastPitch = 0.0f, lastTickAt = -1.0f;
    bool pitchRises = true, spaced = true;
    for (int f2 = 0; f2 < 200; f2++) {
        s = Results_Update(&r, &kNoKeys, &none, DT);
        long v = Results_Shown(&r);
        if (v < last) rising = false;
        last = v;
        if (s.tick) {
            ticks++;
            if (s.tickPitch < lastPitch) pitchRises = false;
            lastPitch = s.tickPitch;
            if (lastTickAt >= 0.0f && r.t - lastTickAt < END_TICK_STEP - 0.001f) spaced = false;
            lastTickAt = r.t;
        }
        if (s.stamp) stamps++;
    }
    CHECK(rising && last == 12000 && stamps == 1, "results: the score counts up to exactly the score, one stamp");
    CHECK(ticks >= 8 && ticks <= 13 && pitchRises && spaced, "results: rising ticks, at most one each 0.05 s");
    CHECK(fabsf(Results_DoneAt(&r) - (END_COUNT_AT + END_COUNT_SECONDS)) < 0.001f, "results: the count ends at 1.7 s");
    /* At 60 fps, frame for frame: the gate opens on frame 24, ticks on
     * frames 66, 69, ... 99, the stamp on frame 102. */
    Results_Begin(&r, &c, true);
    int gateAt = -1, stampAt = -1, tickFrames[32], nt = 0;
    for (int f2 = 1; f2 <= 130; f2++) {
        s = Results_Update(&r, &kNoKeys, &none, DT);
        if (gateAt < 0 && Results_InputOpen(&r)) gateAt = f2 + 1; /* input is read before the clock moves */
        if (s.tick && nt < 32) tickFrames[nt++] = f2;
        if (s.stamp) stampAt = f2;
    }
    bool every3 = nt == 12 && tickFrames[0] == 66;
    for (int i = 1; i < nt; i++) if (tickFrames[i] - tickFrames[i - 1] != 3) every3 = false;
    CHECK(gateAt == 25 && stampAt == 102 && every3, "results: at 60 fps the gate opens after 24 frames, ticks every third frame from 66, the stamp on 102");

    /* Reduced motion: no drop, a 0.2 s count. */
    Results_Begin(&r, &c, false);
    r.t = END_CARD_AT;
    CHECK(Results_CardDrop(&r) == 0 && Results_ScreenDim(&r) == 1.0f, "results: Reduced motion: no drop, dimmed at once");
    CHECK(fabsf(Results_DoneAt(&r) - (END_COUNT_AT + END_COUNT_REDUCED)) < 0.001f, "results: Reduced motion: a 0.2 s count");
    Results_Begin(&r, &c, true);
    r.t = END_CARD_AT;
    CHECK(Results_CardDrop(&r) == END_CARD_DROP_PX && Results_ScreenDim(&r) == 0.0f, "results: the card starts 24 px high");
    r.t = END_CARD_AT + END_CARD_DROP + 0.001f;
    CHECK(Results_CardDrop(&r) == 0 && Results_ScreenDim(&r) == 1.0f, "results: ...and is in place by 0.18 s");
    r.t = END_CARD_AT - 0.01f;
    CHECK(!Results_CardShown(&r), "results: no card before 0.9 s");

    /* The end show on the board. */
    bool allRows = true, monotone = true, stepOk = true;
    for (int h = BOARD_MIN_H; h <= BOARD_MAX_H; h++) {
        Results_Begin(&r, &c, true);
        if (Results_RowStep(h) > END_ROW_STEP + 1e-6f) stepOk = false;
        int prev = 0;
        r.t = 0.0f;
        if (Results_RowsTurned(&r, h) != 0) allRows = false;
        for (float t = 0.0f; t <= END_CARD_AT + 0.001f; t += 0.005f) {
            r.t = t;
            int n = Results_RowsTurned(&r, h);
            if (n < prev) monotone = false;
            prev = n;
        }
        r.t = END_CARD_AT;
        if (Results_RowsTurned(&r, h) != h) allRows = false;
    }
    CHECK(allRows && monotone && stepOk, "results: on every height the rows turn one by one from 0.3 s, all by 0.9 s");
    Results_Begin(&r, &c, true);
    int on = 0, flips = 0;
    bool was = false;
    for (float t = 0.0f; t < END_PIECE_BLINK; t += 0.005f) {
        r.t = t;
        bool red = Results_PieceRed(&r, false);
        if (red) on++;
        if (red != was) flips++;
        was = red;
    }
    r.t = 0.1f;
    bool calm = Results_PieceRed(&r, true);
    r.t = END_PIECE_BLINK + 0.01f;
    CHECK(flips == 4 && on > 0 && calm && !Results_PieceRed(&r, false), "results: the blocked piece blinks red twice (steady with Reduced flashing)");
    ResultsCard sprint = c;
    sprint.kind = END_SPRINT_CLEAR;
    Results_Begin(&r, &sprint, true);
    r.t = 0.1f;
    CHECK(!Results_PieceRed(&r, false), "results: a Sprint clear has no blocked piece");
    r.t = END_ROWS_START;
    float d0 = Results_RowsDim(&r);
    r.t = END_ROWS_START + END_ROWS_SECONDS;
    CHECK(d0 == 0.0f && Results_RowsDim(&r) == 1.0f, "results: time up dims the rows from 0.3 s to 0.9 s");
}

/* ------------------------------------------------------------------ wipe */

static void test_wipe(void) {
    Wipe w;
    Wipe_Start(&w, false);
    CHECK(!Wipe_Active(&w) && Wipe_Cell(&w, 0, 0) == WIPE_GONE, "wipe: Reduced motion: none, a plain cut");
    Wipe_Start(&w, true);
    CHECK(Wipe_Active(&w), "wipe: it begins");
    bool covered = true;
    for (int r = 0; r < WIPE_ROWS; r++)
        for (int c = 0; c < WIPE_COLS; c++) if (r > 0 && Wipe_Cell(&w, r, c) != WIPE_COVERED) covered = false;
    CHECK(covered && Wipe_Cell(&w, 0, 7) == WIPE_FLASH, "wipe: at first the wall covers everything, the top row lit");
    bool ordered = true;
    for (int r = 1; r < WIPE_ROWS; r++) if (Wipe_RowStart(r) <= Wipe_RowStart(r - 1)) ordered = false;
    CHECK(ordered && Wipe_RowStart(0) == 0.0f && fabsf(Wipe_RowStart(WIPE_ROWS - 1) + WIPE_ROW_FLASH + WIPE_ROW_ZIP - WIPE_SECONDS) < 1e-5f,
          "wipe: rows clear top to bottom, the last gone at 0.25 s");
    w.t = WIPE_ROW_FLASH + WIPE_ROW_ZIP * 0.5f;
    bool symmetric = true;
    for (int c = 0; c < WIPE_COLS / 2; c++) if (Wipe_Cell(&w, 0, c) != Wipe_Cell(&w, 0, WIPE_COLS - 1 - c)) symmetric = false;
    CHECK(symmetric && Wipe_Cell(&w, 0, 7) == WIPE_GONE && Wipe_Cell(&w, 0, 8) == WIPE_GONE && Wipe_Cell(&w, 0, 0) == WIPE_FLASH,
          "wipe: a row zips out from the middle to both sides, like a line clear");
    int frames = 0;
    Wipe_Start(&w, true);
    while (Wipe_Active(&w) && frames < 100) { Wipe_Tick(&w, DT); frames++; }
    CHECK(frames == 15, "wipe: 15 frames at 60 fps");
    CHECK(Wipe_Cell(&w, WIPE_ROWS - 1, 0) == WIPE_GONE, "wipe: then nothing is left");
}

/* ------------------------------------------------------------- countdown */

static void test_countdown(void) {
    Game g;
    NewRun(&g, MODE_ULTRA, 10, 20, false);
    PlayDriver d;
    memset(&d, 0, sizeof(d));
    PlayDriver_Start(&d, false, false);
    PlayDriver_Countdown(&d);
    CHECK(d.cue.stage == CUE_COUNT && PlayDriver_Waiting(&d) && PlayDriver_CountNumber(&d.cue) == 3, "countdown: 3");
    PlayIntent idle = {0}, turn = {0}, drop = {0};
    turn.rotate = true;
    drop.hardDrop = true;
    PlayStepResult res;
    float left = g.timeLeft;
    int py = g.py;
    int frames = 0, seen2 = 0, seen1 = 0;
    bool goSound = false;
    Play_Step(&d, &g, &turn, DT, 1.0f, &res);
    Play_Step(&d, &g, &drop, DT, 1.0f, &res);
    frames = 2;
    int rotationAt = g.rotation;
    while (d.cue.stage == CUE_COUNT && frames < 200) {
        if (PlayDriver_CountNumber(&d.cue) == 2) seen2++;
        if (PlayDriver_CountNumber(&d.cue) == 1) seen1++;
        Play_Step(&d, &g, &idle, DT, 1.0f, &res);
        if (d.cue.goSound) goSound = true;
        frames++;
    }
    CHECK(frames >= 53 && frames <= 56, "countdown: 0.9 s");
    CHECK(seen2 > 15 && seen1 > 15, "countdown: then 2, then 1");
    CHECK(fabsf(g.timeLeft - left) < 1e-4f && g.py == py && g.piecesLocked == 0, "countdown: no clock, no fall, the hard drop is not kept");
    CHECK(rotationAt == 0 && g.rotation == 1 && d.cue.stage == CUE_NONE && !goSound, "countdown: the turn happens at its end, no GO");
    PlayDriver_Start(&d, true, false);
    PlayDriver_Countdown(&d);
    CHECK(d.cue.stage == CUE_READY, "countdown: a pause during READY goes back to READY");
}

/* ---------------------------------------------------- boards the browser lists */

static void WriteText(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs(text, f);
    fclose(f);
}

static void test_board_list(void) {
    GameMode m;
    bool old;
    int w, h;
    CHECK(Record_ParseKey("marathon", &m, &old, &w, &h) && m == MODE_MARATHON && !old && w == 10 && h == 20, "boards: marathon is the classic 10x20");
    CHECK(Record_ParseKey("sprint40-14x20", &m, &old, &w, &h) && m == MODE_SPRINT && !old && w == 14 && h == 20, "boards: sprint40-14x20");
    CHECK(Record_ParseKey("sprint", &m, &old, &w, &h) && m == MODE_SPRINT && old && w == 10 && h == 20, "boards: sprint is Sprint's old board");
    CHECK(Record_ParseKey("power-24x36", &m, &old, &w, &h) && m == MODE_POWER && w == 24 && h == 36, "boards: power-24x36");
    CHECK(!Record_ParseKey("marathon-10x20", NULL, NULL, NULL, NULL) && !Record_ParseKey("ultra-5x20", NULL, NULL, NULL, NULL) &&
              !Record_ParseKey("ultra-12x", NULL, NULL, NULL, NULL) && !Record_ParseKey("daily-20261014", NULL, NULL, NULL, NULL) &&
              !Record_ParseKey("sprint4", NULL, NULL, NULL, NULL) && !Record_ParseKey("ultra-12x20x", NULL, NULL, NULL, NULL),
          "boards: other keys are not boards");

    const char *data = getenv("XDG_DATA_HOME");
    char dir[700], shared[800], world[800], local[800];
    snprintf(dir, sizeof(dir), "%s/ghost-launcher", data);
    mkdir(dir, 0755);
    snprintf(dir, sizeof(dir), "%s/ghost-launcher/scores", data);
    mkdir(dir, 0755);
    snprintf(dir, sizeof(dir), "%s/ghost-launcher/scores/global", data);
    mkdir(dir, 0755);
    snprintf(shared, sizeof(shared), "%s/ghost-launcher/scores/blockfall.txt", data);
    snprintf(world, sizeof(world), "%s/ghost-launcher/scores/global/blockfall.txt", data);
    Persist_DataFile(LOCALTABLE_FILE, local, sizeof(local));
    WriteText(shared, "# scores\nmarathon|TRG|100|2026-10-01\nmarathon-18x30|TRG|90|2026-10-01\nsprint|TRG|7620|2026-10-01\n"
                      "sprint40-8x26|TRG|9990000|2026-10-01\npower-14x20|TRG|50|2026-10-01\n");
    WriteText(local, "marathon-12x22|TRG|80|2026-10-01\nmarathon-7x30|TRG|70|2026-10-01\nmarathon-12x13|TRG|60|2026-10-01\nbad line\n");
    WriteText(world, "sprint-14x20|Moth|5000|2026-10-01\nmarathon-6x14|Ghost|10|2026-10-01\n");
    BoardSize b[16];
    int n = Record_BoardsWithRows(MODE_MARATHON, false, b, 16);
    CHECK(n == 6 && b[0].w == 10 && b[1].w == 18 && b[2].w == 6 && b[2].h == 14 && b[3].w == 7 && b[4].w == 12 && b[4].h == 13 &&
              b[5].w == 12 && b[5].h == 22,
          "boards: Marathon's boards: presets first in their order, then the custom sizes by width and height");
    n = Record_BoardsWithRows(MODE_SPRINT, true, b, 16);
    CHECK(n == 2 && b[0].w == 10 && b[1].w == 14, "boards: Sprint's old boards, this machine's and the world's");
    n = Record_BoardsWithRows(MODE_SPRINT, false, b, 16);
    CHECK(n == 1 && b[0].w == 8 && b[0].h == 26, "boards: Sprint 40's boards");
    n = Record_BoardsWithRows(MODE_ULTRA, false, b, 16);
    CHECK(n == 0, "boards: none for Ultra");
    n = Record_BoardsWithRows(MODE_MARATHON, false, b, 2);
    CHECK(n == 2, "boards: never more than asked for");
    remove(shared);
    remove(world);
    remove(local);
}

/* ------------------------------------------------------------- settings */

static void test_new_settings(void) {
    Settings s;
    Settings_Default(&s);
    CHECK(!s.quickRestart && !s.resumeCountdown && s.ghostPiece == 1, "settings: R asks first, no countdown, the ghost shows");
    char path[700];
    snprintf(path, sizeof(path), "%s/blockfall/config.ini", getenv("XDG_CONFIG_HOME"));
    /* A 2.4.0 config.ini: none of the new lines. */
    WriteText(path, "left=LEFT\nvolume=50\nready_go=1\npiece_snap=0\n");
    CHECK(Settings_Load(&s) && !s.quickRestart && !s.resumeCountdown && s.ghostPiece == 1 && s.volumePercent == 50,
          "settings: an older config.ini loads with the new defaults");
    s.quickRestart = true;
    s.resumeCountdown = true;
    s.ghostPiece = 0;
    Settings_Save(&s);
    Settings t;
    CHECK(Settings_Load(&t) && t.quickRestart && t.resumeCountdown && t.ghostPiece == 0, "settings: they are kept");
    WriteText(path, "ghost_piece=2\n");
    Settings_Load(&t);
    Settings_Save(&t);
    Settings_Load(&t);
    CHECK(t.ghostPiece == 2, "settings: a ghost look from a later build is kept as it is");
    WriteText(path, "ghost_piece=-3\n");
    Settings_Load(&t);
    CHECK(t.ghostPiece == 0, "settings: nonsense is hidden, never a crash");
    remove(path);
}

void test_around_all(void) {
    test_run_counters();
    test_pause_menu();
    test_results_card();
    test_results_timeline();
    test_wipe();
    test_countdown();
    test_board_list();
    test_new_settings();
}
