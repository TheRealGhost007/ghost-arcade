/* How to play with moving examples (REWORK_PLAN.md, phase 6): five pages,
 * Basics, Hold and next, Scoring, Modes and Power-ups, each with a small
 * well running a looping demo on the real rules (howtodemo.c). The captions
 * name YOUR keys, or your pad's buttons once the pad was used last, and the
 * key the demo presses lights up as it presses it.
 *  - Pages: the tabs (a click), < and >, Left/Right, LB/RB or Tab. Enter,
 *    Space or pad A go on to the next page (the last one goes back).
 *  - Modes and Power-ups: Up/Down (or a click) pick which mode or power-up
 *    the demo plays.
 *  - The first launch shows Basics alone, with "More in How to play" and
 *    a Let's play button.
 * Esc, pad B, the mouse's back button or [<- Back] go back. Leaving marks
 * it seen, so the first launch's page does not come again. */
#include "screens.h"
#include "blockdraw.h"
#include "comfort.h"
#include "controls.h"
#include "howto.h"
#include "menukeys.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

enum { HI_BACK = 1, HI_TABS, HI_PREV, HI_NEXT, HI_GO, HI_ROW0 = 10 };

#define TABS_Y 104
#define WELL_X 40
#define WELL_Y 152
#define WELL_CELL 18
#define BOX_Y (WELL_Y + DEMO_H * WELL_CELL + 16)
#define BOX_W 68
#define BOX_H 60
#define TEXT_X 216
#define TEXT_W (WINDOW_WIDTH - TEXT_X - 32)
#define ROW_H 28
#define POWER_ROW_H 34 /* a power-up's row has its name and a line under it */
#define HINTS_CY 676

static const char *const kPageCells[HOWTO_PAGES] = {"Basics", "Hold and next", "Scoring", "Modes", "Power-ups"};

static const GameMode kModes[MODE_COUNT] = {MODE_MARATHON, MODE_SPRINT, MODE_ULTRA, MODE_ZEN, MODE_POWER, MODE_DIG};
static const char *const kModeNotes[MODE_COUNT] = {"endless", "40 lines", "two minutes", "no game over", "power-ups", "rising rows"};
static const char *const kModeText[MODE_COUNT] = {
    "Endless. Every 10 lines the level and the speed go up, and the run ends when the stack reaches the top.",
    "Clear 40 lines as fast as you can. It is ranked by time, and the clock starts on GO.",
    "Two minutes on the clock: score as much as you can before it runs out.",
    "No clock and no game over: a full well is swept away for a fresh start. Zen keeps no scores.",
    "Marathon with power-ups riding on some pieces (see Power-ups).",
    "Eight grey rows, one gap each; another pushes up every few seconds, sooner as you go (the Next row bar fills first). Clearing garbage counts as Dug, 100 a row on top. It ends when the stack is pushed past the top.",
};
static const PowerUp kPowers[PU_COUNT - 1] = {PU_BOMB, PU_LASER, PU_FREEZE, PU_SLOW, PU_SWEEP};
static const char *const kPowerText[PU_COUNT - 1] = {
    "Blows up the blocks around it.",
    "Clears its whole column.",
    "Stops the fall for 6 seconds.",
    "Pieces fall slower for 12 seconds.",
    "Takes away the bottom three rows.",
};

static int Variant(const App *a) {
    const HowToScreenState *h = &a->howto;
    if (h->page == HOWTO_MODES) return (int)kModes[h->modeFocus];
    if (h->page == HOWTO_POWER) return (int)kPowers[h->powerFocus];
    return 0;
}

static void StartDemo(App *a) { HowToDemo_Start(&a->howto.demo, a->howto.page, Variant(a)); }

void ScrHowTo_Open(App *a) {
    a->howto.firstLaunch = false;
    App_Open(a, SCR_HOWTO);
}

void ScrHowTo_Enter(App *a, bool back) {
    HowToScreenState *h = &a->howto;
    Scr_Cursor(false);
    if (back) return;
    if (h->firstLaunch || h->page < 0 || h->page >= HOWTO_PAGES) h->page = HOWTO_BASICS;
    if (h->modeFocus < 0 || h->modeFocus >= MODE_COUNT) h->modeFocus = 0;
    if (h->powerFocus < 0 || h->powerFocus >= PU_COUNT - 1) h->powerFocus = 0;
    Kit_Enter(&h->kit, h->firstLaunch ? HI_GO : HI_TABS);
    StartDemo(a);
}

/* --------------------------------------------------------------- layout */

static KitRect RowRect(int i, int h) { return (KitRect){(float)TEXT_X - 12, (float)(WELL_Y + i * h), (float)(WINDOW_WIDTH - TEXT_X - 8), (float)(h - 2)}; }

void ScrHowTo_Layout(const App *a, KitList *l) {
    const HowToScreenState *h = &a->howto;
    Kit_Clear(l, 0);
    Scr_AddBack(l, HI_BACK);
    if (h->firstLaunch) {
        KitItem *go = Kit_Add(l, HI_GO, KIT_BUTTON, (KitRect){WINDOW_WIDTH / 2 - 120, 596, 240, 40}, "Let's play");
        if (go) go->flags |= KIT_ACCENT;
        return;
    }
    KitItem probe;
    memset(&probe, 0, sizeof(probe));
    probe.kind = KIT_TABS;
    probe.textSize = 8;
    probe.cells = kPageCells;
    probe.cellCount = HOWTO_PAGES;
    float w = Kit_Bounds(&probe).w, x = (float)(int)(((float)WINDOW_WIDTH - w) / 2.0f);
    KitItem *tabs = Kit_Add(l, HI_TABS, KIT_TABS, (KitRect){x, TABS_Y, 0, 24}, "Pages");
    if (tabs) {
        tabs->cells = kPageCells;
        tabs->cellCount = HOWTO_PAGES;
        tabs->textSize = 8;
        tabs->value = h->page;
        tabs->flags |= KIT_WRAP;
    }
    KitItem *prev = Kit_Add(l, HI_PREV, KIT_BUTTON, (KitRect){x - 36, TABS_Y, 28, 24}, "<");
    KitItem *next = Kit_Add(l, HI_NEXT, KIT_BUTTON, (KitRect){x + w + 8, TABS_Y, 28, 24}, ">");
    if (prev) { prev->style = KIT_STYLE_SMALL; prev->textSize = 8; prev->flags |= KIT_NOFOCUS; }
    if (next) { next->style = KIT_STYLE_SMALL; next->textSize = 8; next->flags |= KIT_NOFOCUS; }
    if (h->page == HOWTO_MODES) {
        for (int i = 0; i < MODE_COUNT; i++) {
            KitItem *it = Kit_Add(l, HI_ROW0 + i, KIT_ROW, RowRect(i, ROW_H), Game_ModeName(kModes[i]));
            if (!it) continue;
            it->textSize = 8;
            it->flags |= KIT_NOFOCUS | (i == h->modeFocus ? KIT_CHOSEN : 0);
            snprintf(it->note, sizeof(it->note), "%s", kModeNotes[i]);
            it->noteX = 140;
        }
    } else if (h->page == HOWTO_POWER) {
        for (int i = 0; i < PU_COUNT - 1; i++) {
            KitItem *it = Kit_Add(l, HI_ROW0 + i, KIT_ROW, RowRect(i, POWER_ROW_H), Game_PowerName(kPowers[i]));
            if (!it) continue;
            it->textSize = 8;
            it->flags |= KIT_NOFOCUS | (i == h->powerFocus ? KIT_CHOSEN : 0);
            snprintf(it->sub, sizeof(it->sub), "%s", kPowerText[i]);
        }
    }
}

/* ---------------------------------------------------------------- input */

static void Leave(App *a) {
    HowTo_MarkSeen(RECORD_SLUG);
    a->howto.firstLaunch = false;
    Scr_SoundBack();
    App_Back(a);
}

static void GoToPage(App *a, int page) {
    HowToScreenState *h = &a->howto;
    page = ((page % HOWTO_PAGES) + HOWTO_PAGES) % HOWTO_PAGES;
    if (page == h->page) return;
    h->page = page;
    StartDemo(a);
    Scr_SoundMove();
}

static void Pick(App *a, int i) {
    HowToScreenState *h = &a->howto;
    int *focus = h->page == HOWTO_MODES ? &h->modeFocus : &h->powerFocus;
    int count = h->page == HOWTO_MODES ? MODE_COUNT : PU_COUNT - 1;
    i = ((i % count) + count) % count;
    if (i == *focus) return;
    *focus = i;
    StartDemo(a);
    Scr_SoundMove();
}

void ScrHowTo_Update(App *a, const Input *in, float dt) {
    HowToScreenState *h = &a->howto;
    HowToDemo_Tick(&h->demo, dt);
    if (Keymap_Pressed(KA_BACK, in)) { Leave(a); return; }
    KitList l;
    ScrHowTo_Layout(a, &l);
    KitEvent ev = Kit_Update(&h->kit, &l, in);
    if (ev.type == KIT_EV_PRESS) {
        if (ev.id == HI_BACK || ev.id == HI_GO) { Leave(a); return; }
        if (ev.id == HI_PREV) GoToPage(a, h->page - 1);
        if (ev.id == HI_NEXT) GoToPage(a, h->page + 1);
        if (ev.id >= HI_ROW0 && ev.id < HI_ROW0 + MODE_COUNT) Pick(a, ev.id - HI_ROW0);
        if (ev.id == HI_TABS) { /* Enter on the tabs: on to the next page, the last one goes back */
            if (h->page == HOWTO_PAGES - 1) { Leave(a); return; }
            GoToPage(a, h->page + 1);
        }
    } else if (ev.type == KIT_EV_CHANGE && ev.id == HI_TABS) {
        GoToPage(a, ev.value); /* a click, Left/Right, LB/RB or Tab */
    }
    if (!h->firstLaunch && (h->page == HOWTO_MODES || h->page == HOWTO_POWER)) {
        int i = h->page == HOWTO_MODES ? h->modeFocus : h->powerFocus;
        if (Keymap_Pressed(KA_UI_UP, in)) Pick(a, i - 1);
        if (Keymap_Pressed(KA_UI_DOWN, in)) Pick(a, i + 1);
    }
    Scr_Cursor(Kit_WantsHand(&h->kit, &l));
}

/* -------------------------------------------------------------- drawing */

/* The mini well: the real board, the ghost, the piece and its power-up. */
static void DrawWell(const App *a) {
    const UiTheme *t = Ui_Theme();
    const HowToDemo *d = &a->howto.demo;
    const Game *g = &d->game;
    int w = DEMO_W * WELL_CELL, hgt = DEMO_H * WELL_CELL;
    DrawRectangle(WELL_X - 3, WELL_Y - 3, w + 6, hgt + 6, t->rule);
    DrawRectangle(WELL_X, WELL_Y, w, hgt, t->board);
    if (a->settings.grid != GRID_OFF) {
        Color lines = Ui_Lerp(t->board, t->rule, a->settings.grid == GRID_FAINT ? 0.25f : 0.5f);
        for (int c = 1; c < DEMO_W; c++) DrawRectangle(WELL_X + c * WELL_CELL, WELL_Y, 1, hgt, lines);
        for (int r = 1; r < DEMO_H; r++) DrawRectangle(WELL_X, WELL_Y + r * WELL_CELL, w, 1, lines);
    }
    for (int row = BOARD_HIDDEN; row < Board_TotalH(&g->board); row++)
        for (int col = 0; col < g->board.w; col++) {
            int8_t v = g->board.cells[row][col];
            if (v == CELL_EMPTY) continue;
            Render_PieceBlock(WELL_X + col * WELL_CELL, WELL_Y + (row - BOARD_HIDDEN) * WELL_CELL, WELL_CELL, (PieceType)v, Render_PieceColor((PieceType)v), 1.0f);
        }
    if (g->phase != GS_PLAYING || d->ended) return;
    Cell cells[CELLS_PER_PIECE];
    Tetromino_GetCells(g->current, g->rotation, cells);
    Color pc = Render_PieceColor(g->current);
    int ghostY = Game_GhostY(g), ghost = a->settings.ghostPiece;
    if (ghost != GHOST_OFF && ghostY != g->py) {
        for (int i = 0; i < CELLS_PER_PIECE; i++) {
            int row = ghostY + cells[i].y;
            if (row < BOARD_HIDDEN) continue;
            int x = WELL_X + (g->px + cells[i].x) * WELL_CELL, y = WELL_Y + (row - BOARD_HIDDEN) * WELL_CELL;
            if (ghost == GHOST_SOLID) Render_PieceBlock(x, y, WELL_CELL, g->current, Ui_Lerp(pc, t->board, 0.55f), 1.0f);
            else DrawRectangleLinesEx((Rectangle){(float)x + 2, (float)y + 2, WELL_CELL - 4, WELL_CELL - 4}, 2, Fade(pc, 0.6f));
        }
    }
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        int row = g->py + cells[i].y;
        if (row < BOARD_HIDDEN) continue;
        int x = WELL_X + (g->px + cells[i].x) * WELL_CELL, y = WELL_Y + (row - BOARD_HIDDEN) * WELL_CELL;
        Render_PieceBlock(x, y, WELL_CELL, g->current, pc, 1.0f);
        if (g->curPower != PU_NONE && i == g->curPowerCell % CELLS_PER_PIECE) Render_PowerMark(x, y, WELL_CELL, g->curPower);
    }
}

/* What the last lock did, over the well: "QUAD" over "+800". */
static void DrawCallout(const HowToDemo *d) {
    if (!d->callout[0] || d->calloutAge > 1.3f) return;
    const UiTheme *t = Ui_Theme();
    char first[40], second[24] = "";
    snprintf(first, sizeof(first), "%s", d->callout);
    char *plus = strstr(first, " +");
    if (plus) {
        snprintf(second, sizeof(second), "%s", plus + 1);
        *plus = '\0';
    }
    int rise = Comfort_Motion() && d->calloutAge < 0.15f ? (int)(6.0f * (1.0f - d->calloutAge / 0.15f)) : 0;
    int cx = WELL_X + DEMO_W * WELL_CELL / 2, y = WELL_Y + 52 + rise;
    int lines = second[0] ? 2 : 1, w = DEMO_W * WELL_CELL - 16;
    DrawRectangle(cx - w / 2, y - 6, w, lines * 14 + 10, Fade(t->bg, 0.85f));
    Color c = strncmp(d->callout, "BACK", 4) == 0 || strncmp(d->callout, "QUAD", 4) == 0 ? t->gold : t->light;
    Ui_TextCentered(first, UI_T8, cx, y, c);
    if (second[0]) Ui_TextCentered(second, UI_T8, cx, y + 14, c);
}

/* Next and Hold under the well, as the play screen shows them. */
static void DrawBox(int x, int y, const char *label, PieceType piece, PowerUp power, int powerCell, bool used, bool empty) {
    const UiTheme *t = Ui_Theme();
    DrawRectangle(x, y, BOX_W, BOX_H, t->board);
    DrawRectangleLinesEx((Rectangle){(float)x, (float)y, BOX_W, BOX_H}, 2.0f, t->rule);
    Ui_Text(label, x + 8, y + 7, UI_T8, used ? t->faint : t->dim);
    if (empty) return;
    Cell cells[CELLS_PER_PIECE];
    Tetromino_GetCells(piece, 0, cells);
    int minX = 4, maxX = 0, minY = 4, maxY = 0;
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        if (cells[i].x < minX) minX = cells[i].x;
        if (cells[i].x > maxX) maxX = cells[i].x;
        if (cells[i].y < minY) minY = cells[i].y;
        if (cells[i].y > maxY) maxY = cells[i].y;
    }
    const int c = 12;
    int ox = x + (BOX_W - (maxX - minX + 1) * c) / 2, oy = y + 20 + (BOX_H - 24 - (maxY - minY + 1) * c) / 2;
    Color col = Render_PieceColor(piece);
    if (used) col = Ui_Lerp(col, t->rule, 0.7f);
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        int bx = ox + (cells[i].x - minX) * c, by = oy + (cells[i].y - minY) * c;
        Render_PieceBlock(bx, by, c, piece, col, 1.0f);
        if (power != PU_NONE && i == powerCell % CELLS_PER_PIECE) Render_PowerMark(bx, by, c, power);
    }
}

/* Your key (or pad button) for a press, lit while the demo presses it. */
typedef struct {
    const char *label;
    DemoPress press[2];   /* the presses that light its caps (one per cap) */
    char caps[2][24];
    bool pad;
    int count;
} CapRow;

static void Caps(const App *a, int action, CapRow *row) {
    bool pad = a->input.hintDevice == DEV_PAD;
    row->pad = pad;
    if (pad) {
        int b = Controls_Pad(&a->settings, action, 0);
        if (b == PAD_NONE) b = Controls_Pad(&a->settings, action, 1);
        snprintf(row->caps[row->count++], sizeof(row->caps[0]), "%s", b == PAD_NONE ? "-" : Controls_PadCap(b));
    } else {
        const char *k = Controls_KeyConst(&a->settings, action, 0);
        if (Controls_IsNone(k)) k = Controls_KeyConst(&a->settings, action, 1);
        if (Controls_IsNone(k)) snprintf(row->caps[row->count++], sizeof(row->caps[0]), "-");
        else Controls_ShortName(k, row->caps[row->count++], sizeof(row->caps[0]));
    }
}

static int DrawCapRows(const App *a, const CapRow *rows, int n, int y) {
    const UiTheme *t = Ui_Theme();
    const HowToDemo *d = &a->howto.demo;
    bool lit = d->pressAge < 0.25f;
    for (int i = 0; i < n; i++, y += 22) {
        Ui_Text(rows[i].label, TEXT_X, y, UI_T8, t->dim);
        int x = TEXT_X + 112;
        for (int c = 0; c < rows[i].count; c++) {
            const char *cap = rows[i].caps[c];
            int w = strcmp(cap, "-") == 0 ? 16 : Kit_CapWidth(cap, rows[i].pad);
            if (lit && rows[i].press[c] != DOP_NONE && rows[i].press[c] == d->press)
                DrawRectangle(x - 3, y - 4 - 3, w + 6, KIT_CAP_H + 6, t->light); /* the demo is pressing it */
            if (strcmp(cap, "-") == 0) Ui_Text("-", x + 4, y, UI_T8, t->faint);
            else KitDraw_Cap(cap, rows[i].pad, x, y + 4);
            x += w + 8;
        }
    }
    return y;
}

static int Para(const char *text, int y, Color c) {
    char lines[6][SCR_WRAP_LEN];
    int n = Scr_Wrap(text, 8, TEXT_W, lines, 6);
    for (int i = 0; i < n; i++) Ui_Text(lines[i], TEXT_X, y + i * 14, UI_T8, c);
    return y + n * 14 + 8;
}

static void DrawBasics(const App *a) {
    const UiTheme *t = Ui_Theme();
    int y = Para("Pieces fall into the well. Slide and turn them to fill whole rows: a full row clears, and everything above it drops down.", WELL_Y, t->text);
    y = Para("The run ends when the stack reaches the top. Zen never ends.", y, t->text);
    CapRow rows[5];
    memset(rows, 0, sizeof(rows));
    rows[0].label = "Move";
    Caps(a, ACT_LEFT, &rows[0]);
    Caps(a, ACT_RIGHT, &rows[0]);
    rows[0].press[0] = DOP_LEFT;
    rows[0].press[1] = DOP_RIGHT;
    rows[1].label = "Rotate";
    Caps(a, ACT_ROTATE, &rows[1]);
    rows[1].press[0] = DOP_TURN;
    rows[2].label = "Rotate left";
    Caps(a, ACT_ROTATE_LEFT, &rows[2]);
    rows[2].press[0] = DOP_TURN_LEFT;
    rows[3].label = "Soft drop";
    Caps(a, ACT_SOFT, &rows[3]);
    rows[3].press[0] = DOP_SOFT;
    rows[4].label = "Hard drop";
    Caps(a, ACT_HARD, &rows[4]);
    rows[4].press[0] = DOP_HARD;
    y = DrawCapRows(a, rows, 5, y + 8);
    if (a->howto.firstLaunch) {
        Para("More in How to play, on the menu: hold, scoring, modes and power-ups.", y + 12, t->gold);
    } else {
        CapRow more[2];
        memset(more, 0, sizeof(more));
        more[0].label = "Pause";
        Caps(a, ACT_PAUSE, &more[0]);
        more[1].label = "Restart";
        Caps(a, ACT_RESTART, &more[1]);
        DrawCapRows(a, more, 2, y + 8);
    }
}

static void DrawHold(const App *a) {
    const UiTheme *t = Ui_Theme();
    int y = Para("Hold puts the falling piece aside and brings in the next one, or the one you held before.", WELL_Y, t->text);
    y = Para("Once per piece: Hold greys until the piece lands.", y, t->text);
    y = Para("Next shows the piece that comes after this one, large, and the ones after it small (Settings > Game sets how many).", y, t->text);
    CapRow row;
    memset(&row, 0, sizeof(row));
    row.label = "Hold";
    Caps(a, ACT_HOLD, &row);
    row.press[0] = DOP_HOLD;
    DrawCapRows(a, &row, 1, y + 8);
}

static void DrawScoring(const App *a) {
    const UiTheme *t = Ui_Theme();
    (void)a;
    int y = Para("Rows cleared at once, times the level:", WELL_Y, t->text);
    static const char *const kNames[4] = {"Single", "Double", "Triple", "Quad"};
    static const char *const kPoints[4] = {"100", "300", "500", "800"};
    for (int i = 0; i < 4; i++) {
        int x = TEXT_X + (i % 2) * 176, row = y + (i / 2) * 18;
        Ui_Text(kNames[i], x, row, UI_T8, t->dim);
        Ui_TextRight(kPoints[i], UI_T8, x + 120, row, i == 3 ? t->gold : t->text);
    }
    y += 2 * 18 + 8;
    y = Para("Every 10 lines the level goes up, and the pieces fall faster.", y, t->text);
    y = Para("Combo: clears in a row add 50 x the combo x the level.", y, t->text);
    y = Para("Back-to-back: a quad or a T-spin clear right after one pays half again.", y, t->text);
    y = Para("T-spin: a T turned into a tight spot: 400, 800, 1 200 or 1 600 (a mini 100, 200 or 400). Perfect clear: an empty well adds 800 to 2 000.", y, t->text);
    y = Para("Soft drop: 1 a row. Hard drop: 2 a row.", y, t->text);
    Para("Every board scores the same way, the classic 10x20 too.", y, t->dim);
}

static void DrawList(const App *a, int count, const char *text, const char *foot) {
    const UiTheme *t = Ui_Theme();
    KitList l;
    ScrHowTo_Layout(a, &l);
    KitView kv = Scr_View(a, &l, &a->howto.kit, NULL);
    for (int i = 0; i < l.count; i++) {
        const KitItem *it = &l.items[i];
        if (it->id < HI_ROW0 || it->id >= HI_ROW0 + count) continue;
        KitDraw_Item(it, KitDraw_LookOf(&kv, it), KIT_NONE, KIT_NONE);
        if (a->howto.page == HOWTO_POWER) /* its mark, as it shows on a block */
            Render_PowerMark((int)(it->r.x + it->r.w) - 30, (int)(it->r.y + (it->r.h - 18.0f) / 2.0f), 18, kPowers[it->id - HI_ROW0]);
    }
    int y = WELL_Y + count * (a->howto.page == HOWTO_POWER ? POWER_ROW_H : ROW_H) + 10;
    y = Para(text, y, t->text);
    if (foot) Para(foot, y, t->dim);
}

/* Under the well: the mode's own number, or the power-up's bar. */
static void DrawUnder(const App *a) {
    const UiTheme *t = Ui_Theme();
    const Game *g = &a->howto.demo.game;
    char line[48] = "";
    int y = BOX_Y + BOX_H + 12;
    if (a->howto.page == HOWTO_MODES) {
        int s = (int)g->elapsed;
        switch (g->cfg.mode) {
            case MODE_SPRINT: snprintf(line, sizeof(line), "Lines left %d", SPRINT_LINES - g->linesCleared); break;
            case MODE_ULTRA: snprintf(line, sizeof(line), "Time left %d:%02d", (int)g->timeLeft / 60, (int)g->timeLeft % 60); break;
            case MODE_ZEN: snprintf(line, sizeof(line), "Fresh starts %d", g->zenResets); break;
            case MODE_DIG: snprintf(line, sizeof(line), "Dug %d  Next row %.0f s", g->dug, (double)Game_DigSecondsLeft(g) + 0.49); break;
            default: snprintf(line, sizeof(line), "Level %d  Lines %d", g->level, g->linesCleared); break;
        }
        (void)s;
    } else if (a->howto.page == HOWTO_POWER) {
        float left = g->freezeTimer > 0.0f ? g->freezeTimer : g->slowTimer;
        if (left > 0.0f) snprintf(line, sizeof(line), "%s %.0f s", g->freezeTimer > 0.0f ? "Freeze" : "Slow", left + 0.49f);
    } else {
        Menu_Group(g->score, line, sizeof(line));
        char score[48];
        snprintf(score, sizeof(score), "Score %s", line);
        snprintf(line, sizeof(line), "%s", score);
    }
    if (line[0]) Ui_Text(line, WELL_X, y, UI_T8, t->dim);
}

static void DrawHints(const App *a) {
    InputDevice d = a->input.hintDevice;
    const HowToScreenState *h = &a->howto;
    if (h->firstLaunch) {
        KitHint k[2] = {{1, {KA_UI_PRESS}, "let's play"}, {1, {KA_BACK}, "back"}};
        KitDraw_Hints(k, 2, WINDOW_WIDTH / 2, HINTS_CY, d);
        return;
    }
    KitHint k[4];
    int n = 0;
    if (d == DEV_PAD) k[n++] = (KitHint){2, {KA_UI_PREV_TAB, KA_UI_NEXT_TAB}, "page"};
    else k[n++] = (KitHint){2, {KA_UI_LEFT, KA_UI_RIGHT}, "page"};
    if (h->page == HOWTO_MODES || h->page == HOWTO_POWER) k[n++] = (KitHint){2, {KA_UI_UP, KA_UI_DOWN}, "choose"};
    k[n++] = (KitHint){1, {KA_UI_PRESS}, h->page == HOWTO_PAGES - 1 ? "done" : "next"};
    k[n++] = (KitHint){1, {KA_BACK}, "back"};
    KitDraw_Hints(k, n, WINDOW_WIDTH / 2, HINTS_CY, d);
}

void ScrHowTo_Draw(App *a, float dt) {
    (void)dt;
    HowToScreenState *h = &a->howto;
    const UiTheme *t = Ui_Theme();
    Win_BeginFrame();
    ClearBackground(t->bg);
    Scr_BeginSlide(a);
    Scr_PlayingAs(a->username, WINDOW_WIDTH - 22, 22);
    Scr_Title("How to play", 40, 44);
    KitList l;
    ScrHowTo_Layout(a, &l);
    KitView kv = Scr_View(a, &l, &h->kit, NULL);
    for (int i = 0; i < l.count; i++) {
        const KitItem *it = &l.items[i];
        if (it->id >= HI_ROW0) continue; /* the pages draw their rows */
        KitDraw_Item(it, KitDraw_LookOf(&kv, it), Kit_HoverCell(&h->kit, it, kv.device), Kit_PressedCell(&h->kit, it));
    }
    if (h->firstLaunch) Ui_Text("Basics", 40, TABS_Y + 6, UI_T16, t->light);

    DrawWell(a);
    const Game *g = &h->demo.game;
    DrawBox(WELL_X, BOX_Y, "Next", g->next, g->nextPower, g->nextPowerCell, false, false);
    DrawBox(WELL_X + BOX_W + 8, BOX_Y, "Hold", g->hold, g->holdPower, g->holdPowerCell, g->holdUsed, (int)g->hold < 0);
    DrawCallout(&h->demo);
    DrawUnder(a);
    switch (h->page) {
        case HOWTO_HOLD: DrawHold(a); break;
        case HOWTO_SCORING: DrawScoring(a); break;
        case HOWTO_MODES:
            DrawList(a, MODE_COUNT, kModeText[h->modeFocus], "Every mode plays on any board from 6x12 to 24x36 (Play setup). Custom sizes keep their scores on this machine.");
            break;
        case HOWTO_POWER:
            DrawList(a, PU_COUNT - 1, "A power-up rides on one block of a piece, its letter showing, and fires when that piece locks: 150 points, and more for what it clears. Power mode only.", NULL);
            break;
        default: DrawBasics(a); break;
    }
    DrawHints(a);
    Scr_EndSlide();
    Scr_EndFrame(a);
}
