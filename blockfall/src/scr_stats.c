/* Stats (3.3.0), on the button kit: the lifetime file (stats.h) as a
 * screen. A row for All modes and one per mode (Dig since 3.4.0), each with its runs, lines
 * and time played; Up/Down, the d-pad, a hover or a click put the focus
 * bar on a row, and the rest of the screen is that row's: the three records
 * (best pieces per second, biggest combo, most quads in a run, each with
 * the day it was set and, under All modes, the mode) and the lines-by-clear
 * bar (single, double, triple, quad and T-spin lines) with its legend and
 * the other counters in a line. Esc, pad B, the mouse's back button or
 * [<- Back] go back.
 * Motion: the bar grows in from the left over 0.35 s when the screen opens
 * and when the row changes (there at once with Reduced motion). Nothing
 * flashes. */
#include "screens.h"
#include "blockdraw.h"
#include "comfort.h"
#include "layout.h"
#include "menumodel.h"
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { ST_BACK = 1, ST_ROW0 = 10 /* .. ST_ROW0 + STATS_ROWS - 1 */ };

#define TITLE_Y 44
#define SINCE_Y 100
#define HEAD_Y 124
#define ROW_X 40
#define ROW_W 540
#define ROW_Y0 140
#define ROW_H 32
#define ROW_PITCH 36
/* The columns' right edges. At 16 px (18 px a glyph) the widest of each,
 * "9999" runs, "123 456" lines and "12h 05m", and "All modes" at the left
 * all keep clear of each other. */
#define RUNS_R 296
#define LINES_R 432
#define TIME_R 572
/* Everything under the rows moved down a row (36 px) in 3.4.0 for Dig's. */
#define RULE1_Y 400
#define REC_HEAD_Y 416
/* The record tiles are layout.h's STATS_TILE_*. Since 3.6.1 a record's
 * mode and day take a line each under All modes, so the rule under the
 * tiles and everything below it moved down 8 px. */
#define RULE2_Y STATS_RULE2_Y
#define BAR_HEAD_Y 536
#define BAR_X 40
#define BAR_Y 552
#define BAR_W 540
#define BAR_H 24
#define LEGEND_Y 588
#define LEGEND_PITCH 20
#define LEGEND_COL 180
#define EXTRA_Y 640
#define HINTS_CY 676
#define BAR_GROW_SECONDS 0.35f

static const char *const kRows[STATS_ROWS] = {"All modes", "Marathon", "Sprint", "Ultra", "Power", "Dig", "Zen"};
static const int kRowModes[STATS_ROWS] = {-1, MODE_MARATHON, MODE_SPRINT, MODE_ULTRA, MODE_POWER, MODE_DIG, MODE_ZEN};
/* The bar's parts in piece colours, so the legend reads like the well. */
static const PieceType kBarPieces[STATS_BAR_PARTS] = {PIECE_J, PIECE_S, PIECE_O, PIECE_I, PIECE_T};

static KitRect RowRect(int i) { return (KitRect){ROW_X, (float)(ROW_Y0 + i * ROW_PITCH), ROW_W, ROW_H}; }

/* The row's numbers: All modes is every mode together. */
static void RowStats(const StatsState *s, int row, ModeStats *out, int who[3]) {
    if (row <= 0 || row >= STATS_ROWS) {
        Stats_Total(&s->life, out, who);
        return;
    }
    *out = s->life.mode[kRowModes[row]];
    if (who) who[0] = who[1] = who[2] = kRowModes[row];
}

static float Grow(const StatsState *s) {
    if (!Comfort_Motion()) return 1.0f;
    float k = s->age / BAR_GROW_SECONDS;
    if (k >= 1.0f) return 1.0f;
    if (k < 0.0f) k = 0.0f;
    return 1.0f - (1.0f - k) * (1.0f - k); /* eases out */
}

void ScrStats_Enter(App *a, bool back) {
    (void)back;
    StatsState *s = &a->stats;
    s->have = Stats_Load(&s->life); /* read when it opens, never per frame */
    s->shown = 0;
    s->age = 0.0f;
    Kit_Enter(&s->kit, ST_ROW0);
    Kit_GlideReset(&s->glide);
    Scr_Cursor(false);
}

void ScrStats_Layout(const App *a, KitList *l) {
    const StatsState *s = &a->stats;
    Kit_Clear(l, KIT_NAV_Y | KIT_WRAP_Y);
    Scr_AddBack(l, ST_BACK);
    for (int i = 0; i < STATS_ROWS; i++) {
        KitItem *it = Kit_Add(l, ST_ROW0 + i, KIT_ROW, RowRect(i), kRows[i]);
        if (it && i == s->shown) it->flags |= KIT_CHOSEN;
    }
}

void ScrStats_Update(App *a, const Input *in, float dt) {
    StatsState *s = &a->stats;
    if (Keymap_Pressed(KA_BACK, in)) {
        Scr_SoundBack();
        App_Back(a);
        return;
    }
    KitList l;
    ScrStats_Layout(a, &l);
    int before = s->kit.focus;
    KitEvent ev = Kit_Update(&s->kit, &l, in);
    bool byKeys = Keymap_Pressed(KA_UI_UP, in) || Keymap_Pressed(KA_UI_DOWN, in);
    if (s->kit.focus != before && byKeys) Scr_SoundMove(); /* the pointer moves the bar silently */
    if (ev.type == KIT_EV_PRESS && ev.id == ST_BACK) {
        Scr_SoundBack();
        App_Back(a);
        return;
    }
    int row = s->kit.focus - ST_ROW0;
    if (row >= 0 && row < STATS_ROWS && row != s->shown) {
        s->shown = row;
        s->age = 0.0f; /* the bar grows in again for the new row */
    }
    s->age += dt;
    Scr_Cursor(Kit_WantsHand(&s->kit, &l));
}

/* -------------------------------------------------------------- drawing */

static void Plural(long n, const char *one, const char *many, char *out, size_t size) {
    char v[24];
    Menu_Group(n, v, sizeof(v));
    snprintf(out, size, "%s %s", v, n == 1 ? one : many);
}

static void DrawRows(App *a, const KitList *l, float dt) {
    StatsState *s = &a->stats;
    const UiTheme *t = Ui_Theme();
    Ui_Text("Mode", ROW_X + 20, HEAD_Y, UI_T8, t->dim);
    Ui_TextRight("Runs", UI_T8, RUNS_R, HEAD_Y, t->dim);
    Ui_TextRight("Lines", UI_T8, LINES_R, HEAD_Y, t->dim);
    Ui_TextRight("Time", UI_T8, TIME_R, HEAD_Y, t->dim);
    /* The plate sits under the row shown, whatever the pointer is over. */
    KitRect plate = Kit_GlideTo(&s->glide, RowRect(s->shown), dt, Comfort_Motion());
    KitView kv = Scr_View(a, l, &s->kit, &plate);
    KitDraw_List(&kv);
    for (int i = 0; i < STATS_ROWS; i++) {
        ModeStats m;
        RowStats(s, i, &m, NULL);
        KitRect r = RowRect(i);
        int y = (int)r.y + (ROW_H - 16) / 2;
        const KitItem *it = Kit_Get(l, ST_ROW0 + i);
        KitLook look = it ? KitDraw_LookOf(&kv, it) : KIT_LOOK_NORMAL;
        Color c = look == KIT_LOOK_PRESSED ? t->bg : t->text;
        int sink = look == KIT_LOOK_PRESSED ? 2 : 0;
        if (m.runs <= 0) {
            Color f = look == KIT_LOOK_PRESSED ? t->bg : t->faint;
            Ui_TextRight("-", UI_T16, RUNS_R, y + sink, f);
            Ui_TextRight("-", UI_T16, LINES_R, y + sink, f);
            Ui_TextRight("-", UI_T16, TIME_R, y + sink, f);
            continue;
        }
        char v[24];
        snprintf(v, sizeof(v), "%ld", m.runs); /* never grouped: the column is narrow and runs stay small */
        Ui_TextRight(v, UI_T16, RUNS_R, y + sink, c);
        Menu_Group(m.lines, v, sizeof(v));
        Ui_TextRight(v, UI_T16, LINES_R, y + sink, c);
        Stats_FormatTime(m.centis, v, sizeof(v));
        Ui_TextRight(v, UI_T16, TIME_R, y + sink, c);
    }
}

static void DrawRecords(const StatsState *s) {
    const UiTheme *t = Ui_Theme();
    ModeStats m;
    int who[3];
    RowStats(s, s->shown, &m, who);
    bool all = s->shown == 0;
    char head[48];
    snprintf(head, sizeof(head), all ? "Records, %s" : "Records in %s", kRows[s->shown]);
    Ui_Text(head, STATS_TILE_X0, REC_HEAD_Y, UI_T8, t->dim);
    static const char *const kLabels[3] = {"Pieces per second", "Biggest combo", "Most quads in a run"};
    const StatRecord *recs[3] = {&m.pps, &m.combo, &m.quads};
    for (int i = 0; i < 3; i++) {
        int x = Layout_StatsTileX(i);
        char value[24], sub[40], sub2[24];
        Menu_RecordWords(recs[i], i, who[i], all, m.runs > 0, value, sizeof(value), sub, sizeof(sub), sub2, sizeof(sub2));
        Color c = recs[i]->value > 0 ? t->text : t->faint;
        Ui_Text(kLabels[i], x, STATS_TILE_Y, UI_T8, t->dim);
        Ui_Text(value, x, STATS_TILE_VALUE_Y, UI_T32, recs[i]->value > 0 ? t->light : t->faint);
        Ui_Text(sub, x, STATS_TILE_SUB_Y, UI_T8, c);
        if (sub2[0]) Ui_Text(sub2, x, STATS_TILE_SUB_Y + STATS_TILE_SUB_PITCH, UI_T8, c);
    }
}

static void DrawBar(const StatsState *s) {
    const UiTheme *t = Ui_Theme();
    ModeStats m;
    RowStats(s, s->shown, &m, NULL);
    long parts[STATS_BAR_PARTS];
    Stats_Bar(&m, parts);
    long total = Stats_BarTotal(parts);
    Ui_Text("Lines by clear", BAR_X, BAR_HEAD_Y, UI_T8, t->dim);
    char v[32], line[48];
    if (total > 0) {
        Plural(total, "line", "lines", line, sizeof(line));
        Ui_TextRight(line, UI_T8, BAR_X + BAR_W, BAR_HEAD_Y, t->text);
    }
    DrawRectangleLines(BAR_X, BAR_Y, BAR_W, BAR_H, t->rule);
    if (total <= 0) {
        Ui_TextCentered("No lines yet", UI_T8, BAR_X + BAR_W / 2, BAR_Y + (BAR_H - 8) / 2, t->faint);
    } else {
        float grow = Grow(s), span = (float)(BAR_W - 2) * grow;
        long cum = 0;
        int x0 = BAR_X + 1;
        for (int i = 0; i < STATS_BAR_PARTS; i++) {
            if (parts[i] <= 0) continue;
            int xa = x0 + (int)lroundf(span * (float)cum / (float)total);
            cum += parts[i];
            int xb = x0 + (int)lroundf(span * (float)cum / (float)total);
            if (xb - xa < 2 && xb < x0 + (int)span) xb = xa + 2; /* a few lines still show as a sliver */
            if (xb > xa) DrawRectangle(xa, BAR_Y + 1, xb - xa, BAR_H - 2, Render_PieceColor(kBarPieces[i]));
        }
    }
    for (int i = 0; i < STATS_BAR_PARTS; i++) {
        int x = BAR_X + (i % 3) * LEGEND_COL, y = LEGEND_Y + (i / 3) * LEGEND_PITCH;
        Color c = Render_PieceColor(kBarPieces[i]);
        if (parts[i] <= 0) c = Fade(c, 0.35f);
        DrawRectangle(x, y, 8, 8, c);
        Menu_Group(parts[i], v, sizeof(v));
        if (total > 0) snprintf(line, sizeof(line), "%s %s (%ld%%)", Stats_BarName(i), v, (parts[i] * 100 + total / 2) / total);
        else snprintf(line, sizeof(line), "%s %s", Stats_BarName(i), v);
        Ui_Text(line, x + 14, y, UI_T8, parts[i] > 0 ? t->text : t->faint);
    }
    /* The other counters in a line. */
    char a[32], b[32], c2[32], d[32];
    Plural(m.tspins, "T-spin", "T-spins", a, sizeof(a));
    Plural(m.perfects, "perfect clear", "perfect clears", b, sizeof(b));
    Plural(m.powers, "power-up", "power-ups", c2, sizeof(c2));
    Plural(m.holds, "hold", "holds", d, sizeof(d));
    char extra[160];
    if (m.runs > 0) snprintf(extra, sizeof(extra), "Also %s, %s, %s and %s", a, b, c2, d);
    else snprintf(extra, sizeof(extra), "%s", "Play a run: it counts the moment it ends");
    Ui_Text(extra, BAR_X, EXTRA_Y, UI_T8, m.runs > 0 ? t->dim : t->faint);
}

static void DrawHints(const App *a) {
    KitHint h[2] = {
        {2, {KA_UI_UP, KA_UI_DOWN}, "choose"},
        {1, {KA_BACK}, "back"},
    };
    KitDraw_Hints(h, 2, WINDOW_WIDTH / 2, HINTS_CY, a->input.hintDevice);
}

void ScrStats_Draw(App *a, float dt) {
    StatsState *s = &a->stats;
    const UiTheme *t = Ui_Theme();
    Win_BeginFrame();
    ClearBackground(t->bg);
    Scr_BeginSlide(a);
    Scr_PlayingAs(a->username, WINDOW_WIDTH - 22, 22);
    Scr_Title("Stats", 40, TITLE_Y);
    char since[80];
    if (s->have && s->life.since[0]) snprintf(since, sizeof(since), "Counting since %s on this machine", s->life.since);
    else snprintf(since, sizeof(since), "%s", "Nothing counted yet: a run counts the moment it ends");
    Ui_Text(since, 40, SINCE_Y, UI_T8, t->dim);

    KitList l;
    ScrStats_Layout(a, &l);
    DrawRows(a, &l, dt);
    DrawRectangle(40, RULE1_Y, WINDOW_WIDTH - 80, 2, t->rule);
    DrawRecords(s);
    DrawRectangle(40, RULE2_Y, WINDOW_WIDTH - 80, 2, t->rule);
    DrawBar(s);
    DrawHints(a);
    Scr_EndSlide();
    Scr_EndFrame(a);
}

/* ------------------------------------------------------- for the harness */

void ScrStats_Value(const App *a, const char *key, char *out, size_t n) {
    const StatsState *s = &a->stats;
    ModeStats m;
    int who[3];
    RowStats(s, s->shown, &m, who);
    bool all = s->shown == 0;
    char value[24], sub[40], sub2[24];
    if (strcmp(key, "row") == 0) snprintf(out, n, "%s", kRows[s->shown]);
    else if (strcmp(key, "runs") == 0) snprintf(out, n, "%ld", m.runs);
    else if (strcmp(key, "lines") == 0) snprintf(out, n, "%ld", m.lines);
    else if (strcmp(key, "time") == 0) Stats_FormatTime(m.centis, out, n);
    else if (strcmp(key, "pps") == 0) { Menu_RecordWords(&m.pps, 0, who[0], all, m.runs > 0, value, sizeof(value), sub, sizeof(sub), sub2, sizeof(sub2)); snprintf(out, n, "%s", value); }
    else if (strcmp(key, "combo") == 0) { Menu_RecordWords(&m.combo, 1, who[1], all, m.runs > 0, value, sizeof(value), sub, sizeof(sub), sub2, sizeof(sub2)); snprintf(out, n, "%s", value); }
    else if (strcmp(key, "quads") == 0) { Menu_RecordWords(&m.quads, 2, who[2], all, m.runs > 0, value, sizeof(value), sub, sizeof(sub), sub2, sizeof(sub2)); snprintf(out, n, "%s", value); }
    else if (strncmp(key, "sub:", 4) == 0) {
        /* A tile's lines under its value, "Marathon|2026-10-18" (3.6.1) */
        int i = strcmp(key + 4, "pps") == 0 ? 0 : (strcmp(key + 4, "combo") == 0 ? 1 : 2);
        const StatRecord *r = i == 0 ? &m.pps : (i == 1 ? &m.combo : &m.quads);
        Menu_RecordWords(r, i, who[i], all, m.runs > 0, value, sizeof(value), sub, sizeof(sub), sub2, sizeof(sub2));
        snprintf(out, n, "%s|%s", sub, sub2);
    } else if (strncmp(key, "who:", 4) == 0) {
        int i = strcmp(key + 4, "pps") == 0 ? 0 : (strcmp(key + 4, "combo") == 0 ? 1 : 2);
        const StatRecord *r = i == 0 ? &m.pps : (i == 1 ? &m.combo : &m.quads);
        snprintf(out, n, "%s", r->value > 0 && who[i] >= 0 ? Game_ModeName((GameMode)who[i]) : "-");
    } else if (strcmp(key, "bar") == 0) {
        long parts[STATS_BAR_PARTS];
        Stats_Bar(&m, parts);
        snprintf(out, n, "%ld|%ld|%ld|%ld|%ld", parts[0], parts[1], parts[2], parts[3], parts[4]);
    } else if (strcmp(key, "since") == 0) snprintf(out, n, "%s", s->life.since[0] ? s->life.since : "none");
    else if (strcmp(key, "have") == 0) snprintf(out, n, "%d", (int)s->have);
    else if (strcmp(key, "grow") == 0) snprintf(out, n, "%.2f", (double)Grow(s));
    else snprintf(out, n, "?unknown %.40s", key);
}
