/* Scores, a browser on the button kit (REWORK_PLAN.md, "Scores you can
 * browse by mode and board"):
 *  - tabs for Marathon, Sprint, Ultra and Power (a click, Left/Right or
 *    LB/RB), and a faint Zen tab that says Zen keeps no scores;
 *  - a board picker "< Classic 10x20 >" under them ([ and ], X or pad X, a
 *    click or the wheel): the five presets, the size set up in Play setup,
 *    and every other size this machine or the world has rows on. Sprint's
 *    old points boards, from before it was ranked by time, are listed last
 *    as "(old)";
 *  - This machine | World (a click, Up/Down) and All time | This week |
 *    Today (a click, T or pad Y);
 *  - the table with its medals and your rows, where you stand, and the
 *    classic best from before the tables existed;
 *  - Play this board (Enter, Space or pad A) starts a run in that mode on
 *    that board.
 * Opened from the results card it lands on the run's board with its new
 * row blinking. Esc, pad B, the mouse's back button or [<- Back] go back to
 * where it was opened from. The table is read when the view changes and
 * every 2 seconds (a sync that just finished shows up). */
#include "screens.h"
#include "comfort.h"
#include "menukeys.h"
#include "scoresview.h"
#include "sfx.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

enum { SCORE_TAB_MARATHON, SCORE_TAB_SPRINT, SCORE_TAB_ULTRA, SCORE_TAB_POWER, SCORE_TAB_ZEN, SCORE_TABS };
enum { SC_BACK = 1, SC_BOARD, SC_SCOPE, SC_PERIOD, SC_PLAY, SC_TAB0 = 10 /* .. SC_TAB0 + SCORE_TABS - 1 */ };

static const char *const kTabs[SCORE_TABS] = {"Marathon", "Sprint", "Ultra", "Power", "Zen"};
static const GameMode kTabModes[SCORE_TABS] = {MODE_MARATHON, MODE_SPRINT, MODE_ULTRA, MODE_POWER, MODE_ZEN};
static const char *const kScopes[2] = {"This machine", "World"};
static const char *const kPeriods[UI_PERIOD_COUNT] = {"All time", "This week", "Today"};

#define TITLE_Y 44
#define TABS_Y 104
#define BOARD_Y 146
#define SCOPE_Y 186
#define RULE_Y 218
#define PLAY_RECT ((KitRect){200, 614, 220, 28})
#define TAB_PAD 6  /* tighter than the kit's tab bar, so five 16 px tabs fit the window */
#define TAB_GAP 4
#define HINTS1_CY 662
#define HINTS2_CY 688

static int TabOf(GameMode mode) {
    for (int i = 0; i < SCORE_TABS; i++) if (kTabModes[i] == mode) return i;
    return 0;
}

static GameMode TabMode(const ScoresState *s) { return kTabModes[s->tab >= 0 && s->tab < SCORE_TABS ? s->tab : 0]; }

/* ------------------------------------------------------------- boards */

static int FindBoard(const ScoresState *s, int w, int h, bool old) {
    for (int i = 0; i < s->boards; i++)
        if (s->board[i].w == w && s->board[i].h == h && s->boardOld[i] == old) return i;
    return -1;
}

static void AddBoard(ScoresState *s, int w, int h, bool old) {
    if (s->boards >= SCORES_BOARDS || FindBoard(s, w, h, old) >= 0) return;
    s->board[s->boards] = (BoardSize){w, h};
    s->boardOld[s->boards] = old;
    s->boards++;
}

/* The picker's list for the tab: the presets, the custom sizes (the one set
 * up in Play setup and any with rows) by width then height, then Sprint's
 * old points boards. The board shown stays if the tab has it. */
static void BuildBoards(App *a) {
    ScoresState *s = &a->scores;
    GameMode mode = TabMode(s);
    s->boards = 0;
    for (int i = 0; i < BOARD_PRESET_CUSTOM; i++) AddBoard(s, Record_Preset(i)->w, Record_Preset(i)->h, false);
    BoardSize custom[SCORES_BOARDS];
    int n = mode == MODE_ZEN ? 0 : Record_BoardsWithRows(mode, false, custom, SCORES_BOARDS - 1);
    int sw, sh;
    Setup_Size(&a->settings, &sw, &sh);
    bool have = false;
    for (int i = 0; i < n; i++) if (custom[i].w == sw && custom[i].h == sh) have = true;
    if (Record_PresetOf(sw, sh) < 0 && !have) {
        /* in its place by width and height */
        int at = n;
        for (int i = 0; i < n; i++) {
            if (Record_PresetOf(custom[i].w, custom[i].h) >= 0) continue;
            if (custom[i].w > sw || (custom[i].w == sw && custom[i].h > sh)) { at = i; break; }
        }
        for (int i = n; i > at; i--) custom[i] = custom[i - 1];
        custom[at] = (BoardSize){sw, sh};
        n++;
    }
    for (int i = 0; i < n; i++) AddBoard(s, custom[i].w, custom[i].h, false);
    if (mode == MODE_SPRINT) {
        BoardSize old[SCORES_BOARDS];
        int m = Record_BoardsWithRows(MODE_SPRINT, true, old, SCORES_BOARDS);
        for (int i = 0; i < m; i++) AddBoard(s, old[i].w, old[i].h, true);
    }
    if (FindBoard(s, s->boardW, s->boardH, s->old) < 0) {
        if (FindBoard(s, s->boardW, s->boardH, false) < 0) { s->boardW = s->board[0].w; s->boardH = s->board[0].h; }
        s->old = false;
    }
}

static int BoardIndex(const ScoresState *s) {
    int i = FindBoard(s, s->boardW, s->boardH, s->old);
    return i < 0 ? 0 : i;
}

static void BoardLabel(const ScoresState *s, int i, char *out, size_t size) {
    char name[40];
    Menu_BoardName(s->board[i].w, s->board[i].h, name, sizeof(name));
    snprintf(out, size, "%s%s", name, s->boardOld[i] ? " (old)" : "");
}

static void SetBoard(ScoresState *s, int i) {
    if (s->boards <= 0) return;
    i = ((i % s->boards) + s->boards) % s->boards;
    s->boardW = s->board[i].w;
    s->boardH = s->board[i].h;
    s->old = s->boardOld[i];
}

/* ------------------------------------------------------------ opening */

static void Opened(App *a) {
    a->scores.blinkOn = false;
    App_Open(a, SCR_SCORES);
}

void ScrScores_OpenFor(App *a, GameMode mode, int w, int h) {
    ScoresState *s = &a->scores;
    s->tab = TabOf(mode);
    s->boardW = w;
    s->boardH = h;
    s->old = false;
    Opened(a);
}

void ScrScores_OpenFromMenu(App *a) {
    ScoresState *s = &a->scores;
    Setup_Size(&a->settings, &s->boardW, &s->boardH);
    s->old = false;
    Opened(a);
}

void ScrScores_OpenForRun(App *a, GameMode mode, int w, int h, long value, const char *date) {
    ScoresState *s = &a->scores;
    s->tab = TabOf(mode);
    s->boardW = w;
    s->boardH = h;
    s->old = false;
    s->world = false;
    s->period = UI_PERIOD_ALL;
    App_Open(a, SCR_SCORES);
    s->blinkOn = value > 0;
    s->blinkValue = value;
    snprintf(s->blinkName, sizeof(s->blinkName), "%s", a->username);
    snprintf(s->blinkDate, sizeof(s->blinkDate), "%s", date ? date : "");
    s->blinkT = 0.0f;
}

void ScrScores_Enter(App *a, bool back) {
    (void)back;
    ScoresState *s = &a->scores;
    if (s->tab < 0 || s->tab >= SCORE_TABS) s->tab = 0;
    if (s->period < 0 || s->period >= UI_PERIOD_COUNT) s->period = 0;
    if (s->boardW < BOARD_MIN_W || s->boardH < BOARD_MIN_H) { s->boardW = BOARD_W; s->boardH = BOARD_H; }
    BuildBoards(a);
    Kit_Enter(&s->kit, SC_PLAY);
    s->reloadTimer = 99.0f; /* load on the first frame */
    Scr_Cursor(false);
}

/* -------------------------------------------------------------- layout */

/* The mode tabs: one button each, drawn here as tabs (Zen faint), centred
 * on the window. */
static KitRect TabRect(int i) {
    int total = 0, x = 0;
    for (int t = 0; t < SCORE_TABS; t++) total += Kit_TextWidth(kTabs[t], 16) + 2 * TAB_PAD + (t ? TAB_GAP : 0);
    x = (WINDOW_WIDTH - total) / 2;
    for (int t = 0; t < i; t++) x += Kit_TextWidth(kTabs[t], 16) + 2 * TAB_PAD + TAB_GAP;
    return (KitRect){(float)x, TABS_Y, (float)(Kit_TextWidth(kTabs[i], 16) + 2 * TAB_PAD), 32};
}

void ScrScores_Layout(const App *a, KitList *l) {
    const ScoresState *s = &a->scores;
    Kit_Clear(l, 0);
    Scr_AddBack(l, SC_BACK);
    KitItem *it;
    for (int i = 0; i < SCORE_TABS; i++) {
        it = Kit_Add(l, SC_TAB0 + i, KIT_BUTTON, TabRect(i), kTabs[i]);
        if (it) it->flags |= KIT_NOFOCUS | KIT_SELFDRAWN | (i == s->tab ? KIT_CHOSEN : 0); /* drawn here: Zen is faint */
    }
    char label[KIT_LABEL_LEN];
    int at = BoardIndex(s);
    if (s->boards > 0) BoardLabel(s, at, label, sizeof(label));
    else snprintf(label, sizeof(label), "Classic 10x20");
    it = Kit_Add(l, SC_BOARD, KIT_STEPPER, (KitRect){40, BOARD_Y, WINDOW_WIDTH - 80, 28}, label); /* room for "Classic 10x20 (old)" between its arrows */
    if (it) {
        it->value = at;
        it->min = 0;
        it->max = s->boards > 0 ? s->boards - 1 : 0;
        it->flags |= KIT_NOFOCUS | KIT_WRAP;
    }
    it = Kit_Add(l, SC_SCOPE, KIT_TABS, (KitRect){40, SCOPE_Y, 0, 24}, "Scope");
    if (it) {
        it->cells = kScopes;
        it->cellCount = 2;
        it->value = s->world ? 1 : 0;
        it->textSize = 8;
        it->flags |= KIT_NOFOCUS;
    }
    float periodW = 0.0f;
    for (int i = 0; i < UI_PERIOD_COUNT; i++) periodW += (float)(Kit_TextWidth(kPeriods[i], 8) + 20) + (i ? 6.0f : 0.0f);
    it = Kit_Add(l, SC_PERIOD, KIT_TABS, (KitRect){(float)(WINDOW_WIDTH - 40) - periodW, SCOPE_Y, 0, 24}, "Period");
    if (it) {
        it->cells = kPeriods;
        it->cellCount = UI_PERIOD_COUNT;
        it->value = s->period;
        it->textSize = 8;
        it->flags |= KIT_NOFOCUS;
    }
    it = Kit_Add(l, SC_PLAY, KIT_BUTTON, PLAY_RECT, "Play this board");
    if (it) {
        it->style = KIT_STYLE_SMALL;
        it->textSize = 8;
    }
}

/* --------------------------------------------------------------- input */

/* [ and ] change the board; the pad's LB and RB (the same actions) change
 * the tab here, so only the keyboard's count. */
static bool KeyboardFired(int action, const Input *in) {
    int i = Keymap_Fired(action, in);
    return i >= 0 && Keymap_Get(i)->kind == KB_KEY;
}

static void ChangeTab(App *a, int tab) {
    ScoresState *s = &a->scores;
    s->tab = ((tab % SCORE_TABS) + SCORE_TABS) % SCORE_TABS;
    BuildBoards(a);
}

/* Play this board: the mode and board become Play setup's, and the run
 * starts (the wipe comes with it). */
static void PlayBoard(App *a) {
    ScoresState *s = &a->scores;
    Settings *set = &a->settings;
    set->setupMode = (int)TabMode(s);
    int preset = Record_PresetOf(s->boardW, s->boardH);
    if (preset >= 0) {
        set->setupPreset = preset;
    } else {
        set->setupPreset = BOARD_PRESET_CUSTOM;
        set->setupWidth = s->boardW;
        set->setupHeight = s->boardH;
    }
    Settings_Save(set);
    Sfx_Jingle(JINGLE_READY);
    /* The run goes on the menu, as one from Play setup does: Scores must
     * not stay under it, or the results card's Scores button would go back
     * down to this one (the stack never holds a screen twice) and lose the
     * card. */
    App_Home(a);
    App_Open(a, SCR_PLAY);
}

void ScrScores_Update(App *a, const Input *in, float dt) {
    ScoresState *s = &a->scores;
    if (Keymap_Pressed(KA_BACK, in)) {
        Scr_SoundBack();
        App_Back(a);
        return;
    }
    if (s->blinkOn) s->blinkT += dt;
    bool changed = false, periodChanged = false;
    int tab = s->tab, board = BoardIndex(s);

    int modeDir = (Keymap_Pressed(KA_UI_RIGHT, in) ? 1 : 0) - (Keymap_Pressed(KA_UI_LEFT, in) ? 1 : 0) +
                  (Keymap_Pressed(KA_UI_NEXT_TAB, in) ? 1 : 0) - (Keymap_Pressed(KA_UI_PREV_TAB, in) ? 1 : 0); /* LB/RB, Tab */
    if (modeDir) { ChangeTab(a, s->tab + modeDir); changed = true; }
    int boardDir = (KeyboardFired(BA_BOARD_NEXT, in) ? 1 : 0) - (KeyboardFired(BA_BOARD_PREV, in) ? 1 : 0) +
                   (Keymap_Pressed(BA_BOARD_TURN, in) ? 1 : 0);
    if (boardDir) { SetBoard(s, BoardIndex(s) + boardDir); changed = true; }
    if (Keymap_Pressed(KA_UI_UP, in) || Keymap_Pressed(KA_UI_DOWN, in)) { s->world = !s->world; changed = true; }
    if (Keymap_Pressed(BA_PERIOD, in)) { s->period = (s->period + 1) % UI_PERIOD_COUNT; periodChanged = true; }

    KitList l;
    ScrScores_Layout(a, &l);
    KitEvent ev = Kit_Update(&s->kit, &l, in);
    /* LB/RB and Tab change the mode tab (above). The kit gives them to the
     * first row of tabs on the screen too, which here is This machine |
     * World: only a click (or Up/Down, above) changes that. */
    bool shoulder = Keymap_Pressed(KA_UI_NEXT_TAB, in) || Keymap_Pressed(KA_UI_PREV_TAB, in);
    if (ev.type == KIT_EV_CHANGE && ev.id == SC_SCOPE && shoulder && !(in->mouseReleased[MB_LEFT] && s->kit.hover == SC_SCOPE))
        ev.type = KIT_EV_NONE;
    if (ev.type == KIT_EV_PRESS) {
        if (ev.id == SC_BACK) { Scr_SoundBack(); App_Back(a); return; }
        if (ev.id == SC_PLAY) { PlayBoard(a); return; }
        if (ev.id >= SC_TAB0 && ev.id < SC_TAB0 + SCORE_TABS && ev.id - SC_TAB0 != s->tab) { ChangeTab(a, ev.id - SC_TAB0); changed = true; }
    } else if (ev.type == KIT_EV_CHANGE) {
        switch (ev.id) {
            case SC_BOARD: SetBoard(s, ev.value); changed = true; break;
            case SC_SCOPE: s->world = ev.value == 1; changed = true; break;
            case SC_PERIOD: s->period = ev.value; periodChanged = true; break;
            default: break;
        }
    }
    if (s->tab != tab || BoardIndex(s) != board) s->blinkOn = false; /* it was that board's */
    if (changed || periodChanged) Scr_SoundMove();
    Scr_Cursor(Kit_WantsHand(&s->kit, &l));

    /* Read the table when the view changes, and every 2 seconds so a sync
     * that just finished shows up; never every frame. */
    s->reloadTimer += dt;
    if (changed || s->reloadTimer >= 2.0f) {
        char key[GHOSTLINK_MODE_LEN];
        if (s->old) Record_OldSprintKey(s->boardW, s->boardH, key, sizeof(key));
        else Record_BoardKey(TabMode(s), s->boardW, s->boardH, key, sizeof(key));
        s->count = TabMode(s) == MODE_ZEN ? 0 : Record_LoadBoard(key, s->world, s->entries, GHOSTLINK_TOP_N);
        /* save.dat's classic best is older than the table and may be above it. */
        long top = s->count > 0 ? s->entries[0].score : 0;
        bool classic = TabMode(s) == MODE_MARATHON && s->boardW == BOARD_W && s->boardH == BOARD_H && !s->world;
        s->oldBest = classic && a->save.highScore > top ? a->save.highScore : 0;
        s->reloadTimer = 0.0f;
    }
}

/* -------------------------------------------------------------- drawing */

/* The mode tabs, as the kit draws tabs (the open one on the panel colour
 * with a 4 px bar under it), with Zen faint. */
static void DrawModeTabs(const App *a, const KitList *l) {
    const UiTheme *t = Ui_Theme();
    const KitState *k = &a->scores.kit;
    InputDevice dev = a->input.lastDevice;
    for (int c = 0; c < SCORE_TABS; c++) {
        const KitItem *it = Kit_Get(l, SC_TAB0 + c);
        if (!it) continue;
        KitRect r = it->r;
        KitLook look = Kit_Look(k, it, dev);
        bool open = a->scores.tab == c, zen = c == SCORE_TAB_ZEN;
        Color text = zen ? t->faint : (open ? t->light : t->dim), bar = zen ? t->faint : t->light;
        int sink = 0;
        if (open) {
            DrawRectangle((int)r.x, (int)r.y, (int)r.w, (int)r.h, t->panel);
            DrawRectangle((int)r.x, (int)(r.y + r.h) - 4, (int)r.w, 4, bar);
            if (zen) text = t->dim;
        }
        if (look == KIT_LOOK_HOVER && !open) {
            DrawRectangle((int)r.x, (int)r.y, (int)r.w, (int)r.h, Ui_Lerp(t->panel, t->rule, 0.5f));
            text = zen ? t->dim : t->text;
        }
        if (look == KIT_LOOK_PRESSED) {
            DrawRectangle((int)r.x, (int)r.y, (int)r.w, (int)r.h, t->light);
            text = t->bg;
            sink = 2;
        }
        Ui_TextCentered(kTabs[c], UI_T16, (int)(r.x + r.w / 2), (int)(r.y + (r.h - 16) / 2) - 2 + sink, text);
    }
}

static void DrawHints(const App *a) {
    InputDevice d = a->input.hintDevice;
    if (d == DEV_PAD) {
        KitHint h1[3] = {{2, {KA_UI_PREV_TAB, KA_UI_NEXT_TAB}, "mode"}, {1, {BA_BOARD_TURN}, "board"}, {2, {KA_UI_UP, KA_UI_DOWN}, "world"}};
        KitHint h2[3] = {{1, {BA_PERIOD}, "period"}, {1, {KA_UI_PRESS}, "play this board"}, {1, {KA_BACK}, "back"}};
        KitDraw_Hints(h1, 3, WINDOW_WIDTH / 2, HINTS1_CY, d);
        KitDraw_Hints(h2, 3, WINDOW_WIDTH / 2, HINTS2_CY, d);
    } else {
        KitHint h1[3] = {{2, {KA_UI_LEFT, KA_UI_RIGHT}, "mode"}, {2, {BA_BOARD_PREV, BA_BOARD_NEXT}, "board"}, {2, {KA_UI_UP, KA_UI_DOWN}, "world"}};
        KitHint h2[3] = {{1, {BA_PERIOD}, "period"}, {1, {KA_UI_PRESS}, "play this board"}, {1, {KA_BACK}, "back"}};
        KitDraw_Hints(h1, 3, WINDOW_WIDTH / 2, HINTS1_CY, d);
        KitDraw_Hints(h2, 3, WINDOW_WIDTH / 2, HINTS2_CY, d);
    }
}

void ScrScores_Draw(App *a, float dt) {
    (void)dt;
    ScoresState *s = &a->scores;
    const UiTheme *t = Ui_Theme();
    GameMode mode = TabMode(s);
    Win_BeginFrame();
    ClearBackground(t->bg);
    Scr_BeginSlide(a);
    Scr_PlayingAs(a->username, WINDOW_WIDTH - 22, 22);
    Scr_Title("Scores", 40, TITLE_Y);

    KitList l;
    ScrScores_Layout(a, &l);
    DrawModeTabs(a, &l);
    KitView kv = Scr_View(a, &l, &s->kit, NULL);
    KitDraw_List(&kv);
    DrawRectangle(40, RULE_Y, WINDOW_WIDTH - 80, 2, t->rule);

    char board[KIT_LABEL_LEN];
    Menu_BoardName(s->boardW, s->boardH, board, sizeof(board));
    bool custom = Record_PresetOf(s->boardW, s->boardH) < 0;
    const char *modeLabel = mode == MODE_SPRINT ? (s->old ? "Sprint (old)" : "Sprint 40") : Game_ModeName(mode);
    ScoresViewInfo info = {
        .modeLabel = modeLabel, .boardLabel = board, .world = s->world, .period = (UiScorePeriod)s->period,
        .entries = s->entries, .count = s->count, .username = a->username,
        .unit = (mode == MODE_SPRINT && !s->old) ? UNIT_TIME : UNIT_POINTS,
        .periodKey = a->input.hintDevice == DEV_PAD ? "pad Y" : "T",
        .oldBest = s->oldBest,
        .blink = s->blinkOn, .blinkValue = s->blinkValue, .blinkDate = s->blinkDate,
        /* it blinks for 3 s (steady with Reduced flashing), then stays marked */
        .blinkLit = Comfort_Calm() || s->blinkT >= 3.0f || ((int)(s->blinkT / 0.25f) % 2) == 0,
    };
    if (mode == MODE_ZEN) info.empty = "Zen keeps no scores: a run there never ends.";
    else if (custom && s->world) info.empty = "Custom sizes stay on this machine, off the world board.";
    if (mode == MODE_ZEN) info.footnote = "Zen is for playing on, not for scores";
    else if (s->old) info.footnote = "Sprint before 2.1, ranked by points rather than time";
    else if (mode == MODE_SPRINT) info.footnote = "Sprint 40: finished runs only, the fastest first";
    else if (custom && !s->world) info.footnote = "Custom sizes stay on this machine";
    ScoresView_Draw(&info);

    DrawHints(a);
    Scr_EndSlide();
    Scr_EndFrame(a);
}
