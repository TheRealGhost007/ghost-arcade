/* The Daily challenge (3.6.0, REWORK_PLAN.md "Daily challenge",
 * challenge.h): a screen off the main menu's Daily item, and the one the
 * game opens on with --daily (Ghost Launcher's D key). It says the day,
 * today's twist and what it is, your best today, the runs you made, the
 * streak and your best streak, today's board on this machine and the
 * world's (the top five of each, your rows in gold) and where your best
 * would stand on the world board, then Start. Enter, Space or pad A start;
 * Esc, pad B, the mouse's back button or [<- Back] go back. Everything it
 * shows is read as it opens (the day's board, daily.dat), never per frame.
 * The run it starts is today's twist with the day's seed (scr_play.c). */
#include "screens.h"
#include "comfort.h"
#include "menumodel.h"
#include "sfx.h"
#include "ui.h"
#include "daily.h"
#include <stdio.h>
#include <string.h>

#define LEFT_X 40
#define RIGHT_X (WINDOW_WIDTH - 40)
#define DATE_Y 112
#define TWIST_Y 140
#define ABOUT_Y 166
#define CELLS_Y 212     /* Your best today, Runs today, Streak, Best streak */
#define CELL_W 135
#define RULE_Y 258
#define BOARD_Y 274     /* "Today's board": two columns, this machine and the world */
#define BOARD_ROW_Y 294
#define BOARD_PITCH 18
#define COL_W 260
#define WORLD_X 320
#define NOTE_Y 424
#define HINTS_CY 676

static const KitRect kStartRect = {40, 572, 540, 48};

void ScrDaily_Today(int *date, uint64_t *seed, DailyTwist *twist) {
    /* The shared daily code reads the clock as it is set up; set up again
     * so a game left open past midnight plays the new day. --daily was
     * read once, at launch (main.c). */
    Daily_Init(0, NULL);
    int d = Daily_DateFromMode(Daily_Mode());
    if (d == 0) d = 19700101; /* never: Daily_Mode names a real day */
    uint64_t s = Daily_Seed();
    if (date) *date = d;
    if (seed) *seed = s;
    if (twist) *twist = Daily_TwistOf(d);
}

void ScrDaily_Layout(const App *a, KitList *l) {
    (void)a;
    Kit_Clear(l, 0);
    Scr_AddBack(l, DI_BACK);
    KitItem *it = Kit_Add(l, DI_START, KIT_BUTTON, kStartRect, "Start");
    if (it) it->flags |= KIT_ACCENT;
}

static void CopyRows(const GhostScore *from, int n, GhostScore *to, int *count) {
    *count = n < DAILY_TOP ? n : DAILY_TOP;
    for (int i = 0; i < *count; i++) to[i] = from[i];
}

/* Today, from the files: the twist, the day's board here and in the
 * world, your best and place, the runs and the streak. */
static void Load(App *a) {
    DailyState *d = &a->daily;
    ScrDaily_Today(&d->date, &d->seed, &d->twist);
    Daily_DateWords(d->date, d->dateWords, sizeof(d->dateWords));
    char key[GHOSTLINK_MODE_LEN];
    Daily_BoardKey(d->date, key, sizeof(key));
    static GhostScore rows[GHOSTLINK_TOP_N];
    int n = Record_LoadBoard(key, false, rows, GHOSTLINK_TOP_N);
    CopyRows(rows, n, d->here, &d->hereCount);
    d->bestToday = Record_LoadYourBestKey(key, a->username, &d->rankToday);
    n = Record_LoadBoard(key, true, rows, GHOSTLINK_TOP_N);
    CopyRows(rows, n, d->world, &d->worldCount);
    d->worldRank = (d->bestToday > 0 && n > 0) ? Results_WouldRank(rows, n, d->bestToday, a->username) : 0;
    static DailyLog log;
    DailyLog_Load(&log);
    const DailyDay *today = DailyLog_Find(&log, d->date);
    d->runsToday = today ? today->runs : 0;
    d->streak = DailyLog_Streak(&log, d->date);
    d->bestStreak = DailyLog_BestStreak(&log, d->date);
}

void ScrDaily_Enter(App *a, bool back) {
    DailyState *d = &a->daily;
    (void)back;
    Kit_Enter(&d->kit, DI_START);
    Load(a); /* after a run too: the board and the streak may have changed */
    Scr_Cursor(false);
}

void ScrPlay_OpenDaily(App *a) {
    a->play.startDaily = true;
    App_Open(a, SCR_PLAY);
}

static void Start(App *a) {
    /* Re-read the profile, as Start game does, so a name change made in
     * Ghost Launcher applies without restarting the game. */
    GhostLink_GetUsername("Blockfall", a->username, sizeof(a->username));
    Sfx_Jingle(JINGLE_READY);
    ScrPlay_OpenDaily(a);
}

void ScrDaily_Update(App *a, const Input *in, float dt) {
    DailyState *d = &a->daily;
    (void)dt;
    if (Keymap_Pressed(KA_BACK, in)) {
        Scr_SoundBack();
        App_Back(a);
        return;
    }
    KitList l;
    ScrDaily_Layout(a, &l);
    KitEvent ev = Kit_Update(&d->kit, &l, in);
    if (ev.type == KIT_EV_PRESS) {
        if (ev.id == DI_BACK) { Scr_SoundBack(); App_Back(a); return; }
        if (ev.id == DI_START) { Start(a); return; }
    }
    Scr_Cursor(Kit_WantsHand(&d->kit, &l));
}

/* --------------------------------------------------------------- drawing */

/* A cell of the strip: its word, and the number under it (a dash in faint
 * for none). */
static void StripCell(int x, const char *label, long value, const char *unit) {
    const UiTheme *t = Ui_Theme();
    Ui_Text(label, x, CELLS_Y, UI_T8, t->dim);
    if (value <= 0) {
        Ui_Text("-", x, CELLS_Y + 16, UI_T16, t->faint);
        return;
    }
    char v[24];
    Menu_Group(value, v, sizeof(v));
    Ui_Text(v, x, CELLS_Y + 16, UI_T16, t->text);
    if (unit && unit[0]) Ui_Text(unit, x + Kit_TextWidth(v, UI_T16) + 8, CELLS_Y + 24, UI_T8, t->dim);
}

/* A column of today's board: its head, then up to five rows, yours in gold. */
static void Column(const App *a, int x, const char *head, const GhostScore *rows, int n, const char *empty) {
    const UiTheme *t = Ui_Theme();
    Ui_Text(head, x, BOARD_Y, UI_T8, t->dim);
    if (n <= 0) {
        Ui_Text(empty, x, BOARD_ROW_Y, UI_T8, t->faint);
        return;
    }
    for (int i = 0; i < n; i++) {
        int y = BOARD_ROW_Y + i * BOARD_PITCH;
        bool mine = strcmp(rows[i].username, a->username) == 0;
        Color c = mine ? t->gold : t->text;
        char rank[8], name[16], score[16];
        snprintf(rank, sizeof(rank), "#%d", i + 1);
        snprintf(name, sizeof(name), "%.12s", rows[i].username);
        Menu_Group(rows[i].score, score, sizeof(score));
        Ui_Text(rank, x, y, UI_T8, i == 0 ? t->gold : t->dim);
        Ui_Text(name, x + 32, y, UI_T8, c);
        Ui_TextRight(score, UI_T8, x + COL_W, y, c);
    }
}

static void DrawHints(const App *a) {
    KitHint h[2] = {{1, {KA_UI_PRESS}, "start"}, {1, {KA_BACK}, "back"}};
    KitDraw_Hints(h, 2, WINDOW_WIDTH / 2, HINTS_CY, a->input.hintDevice);
}

void ScrDaily_Draw(App *a, float dt) {
    DailyState *d = &a->daily;
    const UiTheme *t = Ui_Theme();
    (void)dt;
    Win_BeginFrame();
    ClearBackground(t->bg);
    Scr_BeginSlide(a);

    Scr_PlayingAs(a->username, WINDOW_WIDTH - 22, 22);
    Scr_Title("Daily challenge", LEFT_X, 48);
    Ui_Text(d->dateWords, LEFT_X, DATE_Y, UI_T8, t->dim);
    Ui_TextRight("Everyone gets the same pieces today", UI_T8, RIGHT_X, DATE_Y, t->dim);
    Ui_Text(Daily_TwistName(d->twist), LEFT_X, TWIST_Y, UI_T16, t->light);
    char lines[4][SCR_WRAP_LEN];
    int n = Scr_Wrap(Daily_TwistAbout(d->twist), UI_T8, WINDOW_WIDTH - 2 * LEFT_X, lines, 2);
    for (int i = 0; i < n; i++) Ui_Text(lines[i], LEFT_X, ABOUT_Y + i * 14, UI_T8, t->text);

    /* Your place on today's board is in the board below, in gold. */
    StripCell(LEFT_X, "Best today", d->bestToday, NULL);
    StripCell(LEFT_X + CELL_W, "Runs today", d->runsToday, NULL);
    StripCell(LEFT_X + 2 * CELL_W, "Streak", d->streak, d->streak == 1 ? "day" : "days");
    StripCell(LEFT_X + 3 * CELL_W, "Best streak", d->bestStreak, d->bestStreak == 1 ? "day" : "days");
    DrawRectangle(LEFT_X, RULE_Y, WINDOW_WIDTH - 2 * LEFT_X, 2, t->rule);

    Column(a, LEFT_X, "Today's board, this machine", d->here, d->hereCount, "No run yet today");
    Column(a, WORLD_X, "The world today", d->world, d->worldCount, "Waits for a sync");
    if (d->worldRank > 0) {
        char line[64];
        snprintf(line, sizeof(line), "Your best: #%d in the world", d->worldRank);
        Ui_Text(line, WORLD_X, BOARD_ROW_Y + DAILY_TOP * BOARD_PITCH + 6, UI_T8, t->gold);
    }

    /* The small print. */
    static const char kNote[] = "Retry as often as you like: your best counts. The streak is days in a row with a daily played; "
                                "it lives until a whole day passes without one. Ghost Launcher's D key opens this screen, and its "
                                "daily desk lists Blockfall too.";
    n = Scr_Wrap(kNote, UI_T8, WINDOW_WIDTH - 2 * LEFT_X, lines, 4);
    for (int i = 0; i < n; i++) Ui_Text(lines[i], LEFT_X, NOTE_Y + i * 14, UI_T8, t->dim);

    KitList l;
    ScrDaily_Layout(a, &l);
    KitView kv = Scr_View(a, &l, &d->kit, NULL);
    KitDraw_List(&kv);
    DrawHints(a);
    Scr_EndSlide();
    Scr_EndFrame(a);
}

void ScrDaily_Value(const App *a, const char *key, char *out, size_t n) {
    const DailyState *d = &a->daily;
    if (strcmp(key, "twist") == 0) snprintf(out, n, "%s", Daily_TwistName(d->twist));
    else if (strcmp(key, "date") == 0) snprintf(out, n, "%d", d->date);
    else if (strcmp(key, "datewords") == 0) snprintf(out, n, "%s", d->dateWords);
    else if (strcmp(key, "best") == 0) snprintf(out, n, "%ld", d->bestToday);
    else if (strcmp(key, "rank") == 0) snprintf(out, n, "%d", d->rankToday);
    else if (strcmp(key, "runs") == 0) snprintf(out, n, "%d", d->runsToday);
    else if (strcmp(key, "streak") == 0) snprintf(out, n, "%d", d->streak);
    else if (strcmp(key, "beststreak") == 0) snprintf(out, n, "%d", d->bestStreak);
    else if (strcmp(key, "worldrank") == 0) snprintf(out, n, "%d", d->worldRank);
    else if (strcmp(key, "here") == 0 || strcmp(key, "world") == 0) {
        bool here = key[0] == 'h';
        const GhostScore *rows = here ? d->here : d->world;
        int count = here ? d->hereCount : d->worldCount;
        out[0] = '\0';
        for (int i = 0; i < count; i++) {
            size_t used = strlen(out);
            snprintf(out + used, n - used, "%s%s:%ld", i ? "|" : "", rows[i].username, rows[i].score);
        }
        if (!out[0]) snprintf(out, n, "none");
    } else snprintf(out, n, "?unknown %.40s", key);
}
