/* Headless tests for what Blockfall's menus decide (menumodel.c): the
 * title's letter drop, the screens' slide, Width and Height held down, the
 * play setup's board and size rules, the preview and the menus' words; the
 * main menu's skyline (backdrop.c); and the Back stack the screens sit on
 * (app.c with ghost-common's uinav.c). */
#include <stdio.h>
#include <string.h>
#include "check.h"
#include "app.h"
#include "backdrop.h"
#include "comfort.h"
#include "layout.h"
#include "menumodel.h"

#define DT (1.0f / 60.0f)

static void test_numbers(void) {
    char b[32];
    Menu_Group(74372, b, sizeof(b));
    CHECK(strcmp(b, "74 372") == 0, "numbers: thousands set apart");
    Menu_Group(999, b, sizeof(b));
    CHECK(strcmp(b, "999") == 0, "numbers: under a thousand as it is");
    Menu_Group(1234567, b, sizeof(b));
    CHECK(strcmp(b, "1 234 567") == 0, "numbers: millions too");
    Menu_Group(0, b, sizeof(b));
    CHECK(strcmp(b, "0") == 0, "numbers: zero");
    Menu_BestText(UNIT_POINTS, 0, "no run yet", b, sizeof(b));
    CHECK(strcmp(b, "no run yet") == 0, "bests: none gives the words");
    Menu_BestText(UNIT_TIME, Record_SprintValue(72.4f), "x", b, sizeof(b));
    CHECK(strcmp(b, "1:12.40") == 0, "bests: Sprint shows a time");
    Menu_BestText(UNIT_POINTS, 24680, "x", b, sizeof(b));
    CHECK(strcmp(b, "24 680") == 0, "bests: points, grouped");
}

static void test_title_drop(void) {
    CHECK(Title_DropSeconds() > 0.55f && Title_DropSeconds() < 0.7f, "title: the drop takes about 0.6 s in all");
    CHECK(Title_LetterLift(0, 0.0f) == TITLE_DROP_FROM && Title_LetterLift(8, 0.2f) == TITLE_DROP_FROM, "title: letters start off the top");
    CHECK(96 - TITLE_DROP_FROM + 48 <= 0, "title: ...wholly above the screen (48 px letters at y 96)");
    bool falling = true;
    int last = TITLE_DROP_FROM + 1;
    for (float t = 0.0f; t < TITLE_DROP_FALL; t += DT) {
        int lift = Title_LetterLift(0, t);
        if (lift > last) falling = false;
        last = lift;
    }
    CHECK(falling, "title: a letter only ever comes down while it falls");
    CHECK(!Title_LetterLanded(3, 3 * TITLE_DROP_GAP + TITLE_DROP_FALL - 0.01f) && Title_LetterLanded(3, 3 * TITLE_DROP_GAP + TITLE_DROP_FALL),
          "title: each lands 0.22 s after its start, 0.04 s after the one before");
    int bounce = 0;
    for (float t = TITLE_DROP_FALL; t < TITLE_DROP_FALL + TITLE_DROP_BOUNCE; t += DT) {
        int l = Title_LetterLift(0, t);
        if (l > bounce) bounce = l;
    }
    CHECK(bounce == 2, "title: then a 2 px bounce");
    CHECK(Title_LetterLift(8, Title_DropSeconds()) == 0 && Title_LetterLift(0, 5.0f) == 0, "title: all in place at the end");
}

static void test_slide(void) {
    CHECK(Slide_Offset(0.0f, 1) == SLIDE_PX && Slide_Offset(0.0f, -1) == -SLIDE_PX, "slide: 16 px from the right forward, the left back");
    CHECK(Slide_Offset(SLIDE_SECONDS, 1) == 0 && Slide_Offset(1.0f, -1) == 0, "slide: in place after 0.14 s");
    bool whole = true, settling = true;
    int last = SLIDE_PX + 1;
    for (float t = 0.0f; t < SLIDE_SECONDS; t += DT) {
        int o = Slide_Offset(t, 1);
        if (o > last) settling = false;
        if (o < 0 || o > SLIDE_PX) whole = false;
        last = o;
    }
    CHECK(whole && settling, "slide: whole pixels (no blurred text), only ever toward its place");
    CHECK(Slide_Offset(0.0f, 0) == 0, "slide: no direction, no slide");

    SizeTween tw = {0};
    SizeTween_Step(&tw, 100, 200, DT, PREVIEW_RESHAPE_SECONDS, true);
    CHECK(tw.w == 100.0f && tw.h == 200.0f, "reshape: placed at once the first time");
    SizeTween_Step(&tw, 200, 100, DT, PREVIEW_RESHAPE_SECONDS, true);
    CHECK(tw.w > 100.0f && tw.w < 200.0f && tw.h < 200.0f && tw.h > 100.0f, "reshape: on its way a frame later");
    for (int i = 0; i < 9; i++) SizeTween_Step(&tw, 200, 100, DT, PREVIEW_RESHAPE_SECONDS, true);
    CHECK(tw.w == 200.0f && tw.h == 100.0f, "reshape: done in 0.15 s");
    SizeTween_Step(&tw, 60, 60, DT, PREVIEW_RESHAPE_SECONDS, false);
    CHECK(tw.w == 60.0f && tw.h == 60.0f, "reshape: Reduced motion is a cut");
}

/* Width or Height held: a step on the press, then 10 a second from 0.4 s,
 * 25 a second from 1 s. */
static void test_hold_steps(void) {
    HoldStep h = {0};
    int total = HoldStep_Update(&h, true, true, DT), firstRepeat = -1;
    for (int f = 1; f <= 60; f++) {
        int s = HoldStep_Update(&h, false, true, DT);
        if (s && firstRepeat < 0) firstRepeat = f;
        total += s;
    }
    CHECK(firstRepeat >= 24 && firstRepeat <= 25, "hold: the first repeat after 0.4 s");
    CHECK(total >= 7 && total <= 8, "hold: about ten a second until a second has gone");
    int second = 0;
    for (int f = 0; f < 60; f++) second += HoldStep_Update(&h, false, true, DT);
    CHECK(second >= 23 && second <= 26, "hold: then about 25 a second");
    CHECK(HoldStep_Update(&h, false, false, DT) == 0 && !h.armed, "hold: let go, it stops");
    CHECK(HoldStep_Update(&h, false, true, DT) == 0, "hold: a key still down without a press seen here does nothing");
    h = (HoldStep){0};
    HoldStep_Update(&h, true, true, DT);
    CHECK(HoldStep_Update(&h, false, true, 5.0f) <= 4, "hold: a long frame gives a few steps, never a burst");

    /* 12 to 36 in under two seconds, where it was 24 presses. */
    Settings s;
    Settings_Default(&s);
    s.setupPreset = BOARD_PRESET_CUSTOM;
    s.setupWidth = 10;
    s.setupHeight = 12;
    h = (HoldStep){0};
    int frames = 0;
    for (bool first = true; s.setupHeight < 36 && frames < 600; first = false, frames++) {
        int n = HoldStep_Update(&h, first, true, DT);
        int w, hh;
        Setup_Size(&s, &w, &hh);
        if (n) Setup_SetSize(&s, w, hh + n);
    }
    CHECK(s.setupHeight == 36 && frames < 120, "hold: Height 12 to 36 held takes under two seconds");
}

static void test_setup_rules(void) {
    Settings s;
    Settings_Default(&s);
    int w, h;
    Setup_Size(&s, &w, &h);
    CHECK(w == 10 && h == 20 && s.setupPreset == 0, "setup: Classic 10x20 to start");
    char label[48];
    Setup_BoardLabel(&s, label, sizeof(label));
    CHECK(strcmp(label, "Classic 10x20") == 0, "setup: the board's name and size");
    Setup_Summary(&s, label, sizeof(label));
    CHECK(strcmp(label, "Marathon on Classic 10x20") == 0, "setup: the menu's second line for Start game");

    for (int i = 0; i < BOARD_PRESET_COUNT; i++) Setup_StepBoard(&s, 1);
    CHECK(s.setupPreset == 0, "setup: the board picker goes round");
    Setup_StepBoard(&s, -1);
    Setup_BoardLabel(&s, label, sizeof(label));
    CHECK(s.setupPreset == BOARD_PRESET_CUSTOM && strcmp(label, "Custom 10x20") == 0, "setup: before Classic is Custom, with its own size");

    Settings_Default(&s);
    CHECK(Setup_SetSize(&s, 11, 20) && s.setupPreset == BOARD_PRESET_CUSTOM && s.setupWidth == 11 && s.setupHeight == 20,
          "setup: Width off a preset makes it Custom, keeping the height");
    CHECK(Setup_SetSize(&s, 14, 20) && s.setupPreset == 1, "setup: a size that matches a preset picks it (Wide 14x20)");
    CHECK(!Setup_SetSize(&s, 14, 20), "setup: the same size is no change");
    CHECK(Setup_SetSize(&s, 99, 2) && s.setupWidth == BOARD_MAX_W && s.setupHeight == BOARD_MIN_H, "setup: clamped to 24 x 12");
    Setup_SetSize(&s, 6, 14);
    CHECK(s.setupPreset == 4, "setup: 6x14 is Tiny");
    Setup_StepBoard(&s, 1);
    Setup_Size(&s, &w, &h);
    CHECK(s.setupPreset == BOARD_PRESET_CUSTOM && w == 6 && h == 14, "setup: Custom after Tiny keeps the last size typed");

    s.setupMode = 99;
    s.setupPreset = -3;
    s.setupWidth = 1000;
    Setup_Sanitize(&s);
    CHECK(s.setupMode == 0 && s.setupPreset == 0 && s.setupWidth == BOARD_MAX_W, "setup: a hand-edited config comes back inside the tables");

    Settings_Default(&s);
    GameConfig cfg = Setup_Config(&s);
    CHECK(cfg.mode == MODE_MARATHON && cfg.width == 10 && cfg.height == 20 && !cfg.modernScoring,
          "setup: the classic Marathon keeps the classic scoring");
    s.setupMode = MODE_SPRINT;
    cfg = Setup_Config(&s);
    CHECK(cfg.mode == MODE_SPRINT && cfg.modernScoring, "setup: every other board scores the modern way");

    bool notes = true, abouts = true;
    for (int m = 0; m < MODE_COUNT; m++) {
        if (!Setup_ModeNote((GameMode)m)[0] || strlen(Setup_ModeNote((GameMode)m)) > 12) notes = false;
        const char *a = Setup_ModeAbout((GameMode)m);
        if (strlen(a) < 20 || strstr(a, "--") || strstr(a, "\xE2\x80\x94")) abouts = false;
    }
    CHECK(notes, "setup: every mode has a short word (fits the list)");
    CHECK(abouts, "setup: every mode says what it is, with no dashes");

    char l1[48], l2[48];
    Setup_InPlay(10, 20, l1, sizeof(l1), l2, sizeof(l2));
    CHECK(strcmp(l1, "In play: 30 px cells,") == 0 && strcmp(l2, "panel at the side") == 0, "setup: In play names the cell size");
    Setup_InPlay(24, 12, l1, sizeof(l1), l2, sizeof(l2));
    char want[48];
    snprintf(want, sizeof(want), "In play: %d px cells,", Layout_Play(24, 12, NULL).cell);
    CHECK(strcmp(l1, want) == 0 && strcmp(want, "In play: 22 px cells,") == 0 && strcmp(l2, "panel across the top") == 0,
          "setup: ...the same the play screen will use, with the panel across the top of a flat board");
}

static void test_preview(void) {
    bool fits = true, notTiny = true, rubbleOk = true;
    for (int w = BOARD_MIN_W; w <= BOARD_MAX_W; w++) {
        for (int h = BOARD_MIN_H; h <= BOARD_MAX_H; h++) {
            PreviewFit f = Setup_PreviewFit(w, h, 250, 184);
            if (f.pw > 250 || f.ph > 184) fits = false;
            if (f.cell < 4) notTiny = false;
            for (int y = 0; y < h; y++) {
                int filled = 0;
                for (int x = 0; x < w; x++) filled += Setup_PreviewRubble(w, h, x, y);
                if (filled == w) rubbleOk = false; /* a full row would be a clear */
                if (y > h / 2 && filled) rubbleOk = false;
            }
        }
    }
    CHECK(fits, "preview: every board fits its box at its true shape");
    CHECK(notTiny, "preview: cells never smaller than 4 px");
    CHECK(rubbleOk, "preview: rubble stays low and never fills a row");
    CHECK(Setup_PreviewRubble(10, 20, 3, 0) == Setup_PreviewRubble(10, 20, 3, 0), "preview: the same rubble every time");
    PreviewFit tall = Setup_PreviewFit(6, 36, 250, 184), wide = Setup_PreviewFit(24, 12, 250, 184);
    CHECK(tall.ph > tall.pw && wide.pw > wide.ph, "preview: tall boards look tall, wide ones wide");
}

static void test_skyline(void) {
    Skyline a, b;
    Sky_Init(&a, 7);
    Sky_Init(&b, 7);
    CHECK(memcmp(a.cells, b.cells, sizeof(a.cells)) == 0, "skyline: the same seed, the same skyline");
    CHECK(Sky_MaxHeight(&a) > 0 && Sky_MaxHeight(&a) <= SKY_ROWS, "skyline: there from the first frame, and low");
    bool belowHints = true, neverTooTall = true, inGrid = true;
    int tallest = 0;
    for (int f = 0; f < 60 * 240; f++) {
        Sky_Step(&a, DT);
        int h = Sky_MaxHeight(&a);
        if (h > tallest) tallest = h;
        if (h > SKY_ROWS + 4) neverTooTall = false;
        for (int i = 0; i < SKY_PIECES; i++) {
            Cell c[CELLS_PER_PIECE];
            Tetromino_GetCells(a.p[i].type, a.p[i].rot, c);
            for (int k = 0; k < CELLS_PER_PIECE; k++) {
                if ((int)a.p[i].y + c[k].y * SKY_CELL + SKY_CELL > SKY_BOTTOM) belowHints = false;
                int x = a.p[i].col + c[k].x;
                if (x < 0 || x >= SKY_COLS) inGrid = false;
            }
        }
        if (h > SKY_ROWS && a.sweepT <= 0.0f && a.clearT <= 0.0f) neverTooTall = false;
    }
    CHECK(SKY_X0 >= 0 && SKY_X0 + SKY_COLS * SKY_CELL <= 620 && SKY_BOTTOM <= 650, "skyline: inside the screen, above the hint line");
    CHECK(belowHints, "skyline: a falling piece never goes below the skyline's bottom (the hints stay clear)");
    CHECK(inGrid, "skyline: pieces fall inside its columns");
    CHECK(neverTooTall, "skyline: too tall, it is swept away (or a clear is already bringing it down)");
    CHECK(a.landed > 50, "skyline: pieces keep landing");
    int clears = a.clears;
    const uint64_t seeds[] = {0x426c6f636bULL, 11, 23}; /* the menu's own seed first */
    for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
        Sky_Init(&b, seeds[i]);
        for (int f = 0; f < 60 * 240; f++) Sky_Step(&b, DT);
        clears += b.clears;
        if (i == 0) CHECK(b.clears > 0, "skyline: on the menu's seed, rows clear within four minutes");
    }
    CHECK(clears >= 4, "skyline: full rows clear, as in play (a few times in four minutes)");
    CHECK(tallest <= SKY_GRID_ROWS, "skyline: never past its grid");
    Skyline c;
    Sky_Init(&c, 11);
    for (int r = 0; r < SKY_ROWS + 1; r++) c.cells[r][0] = 0; /* made too tall by hand */
    Sky_Step(&c, DT);
    CHECK(c.sweepT > 0.0f && c.sweeps == 1, "skyline: a skyline too tall starts a sweep");
    for (int f = 0; f < 60; f++) Sky_Step(&c, DT);
    CHECK(Sky_MaxHeight(&c) <= 1, "skyline: ...and then it is gone (a piece may land after)");
}

/* The Back stack: Back always goes to the screen you came from. */
static void test_back_stack(void) {
    static App a;
    memset(&a, 0, sizeof(a));
    Nav_Init(&a.nav, SCR_MENU);
    Nav_Push(&a.nav, SCR_HOWTO); /* the first launch */
    CHECK(App_Top(&a) == SCR_HOWTO && Nav_Under(&a.nav) == SCR_MENU, "stack: the first launch shows How to play over the menu");
    CHECK(App_Back(&a) && App_Top(&a) == SCR_MENU && a.changed && a.wentBack, "stack: Back from it is the menu");
    CHECK(!App_Back(&a) && App_Top(&a) == SCR_MENU, "stack: Back on the menu goes nowhere (the menu itself moves to Quit)");
    a.changed = false;
    App_Open(&a, SCR_SETUP);
    CHECK(App_Top(&a) == SCR_SETUP && a.changed && !a.wentBack && a.slideDir == 1, "stack: Start game opens Play setup, sliding in from the right");
    App_Open(&a, SCR_SCORES);
    CHECK(App_Top(&a) == SCR_SCORES && Nav_Depth(&a.nav) == 3, "stack: Scores for this board opens over Play setup");
    App_Back(&a);
    CHECK(App_Top(&a) == SCR_SETUP && a.slideDir == -1, "stack: Back from those Scores is Play setup again, from the left");
    App_Open(&a, SCR_PLAY);
    CHECK(App_Top(&a) == SCR_PLAY && Nav_Depth(&a.nav) == 3, "stack: Start opens the run");
    App_Home(&a);
    CHECK(App_Top(&a) == SCR_MENU && Nav_Depth(&a.nav) == 1 && a.wentBack, "stack: leaving the run goes to the menu");
    App_Open(&a, SCR_SCORES);
    App_Back(&a);
    CHECK(App_Top(&a) == SCR_MENU, "stack: Scores from the menu goes back to the menu");
    App_Open(&a, SCR_SETTINGS);
    App_Open(&a, SCR_SETTINGS);
    CHECK(Nav_Depth(&a.nav) == 2, "stack: opening what is open does not stack it twice");
    App_Back(&a);

    Comfort_SetReducedMotion(false);
    App_Open(&a, SCR_UPDATES);
    CHECK(App_SlideOffset(&a) == SLIDE_PX, "stack: a screen that opens starts 16 px off");
    for (int f = 0; f < 9; f++) App_Tick(&a, DT);
    CHECK(App_SlideOffset(&a) == 0, "stack: ...and is in place after 0.14 s");
    App_Back(&a);
    Comfort_SetReducedMotion(true);
    CHECK(App_SlideOffset(&a) == 0, "stack: Reduced motion: no slide, a plain cut");
    Comfort_SetReducedMotion(false);
    CHECK(Menu_OnBack(false) == MENU_BACK_TO_QUIT && Menu_OnBack(true) == MENU_BACK_QUIT,
          "menu: Esc or B moves to Quit first; on Quit it quits");
}

void test_menus_all(void) {
    test_numbers();
    test_title_drop();
    test_slide();
    test_hold_steps();
    test_setup_rules();
    test_preview();
    test_skyline();
    test_back_stack();
}
