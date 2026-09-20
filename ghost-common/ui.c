#include "ui.h"
#include "winscale.h"
#include "howto.h"
#include "prefs.h"
#include "achievements.h"
#include <time.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static Font sFont;
static bool sFontLoaded = false;
static UiTheme sTheme;
static int sWinW = 620, sWinH = 720;

UiTheme Ui_DefaultTheme(Color light) {
    return (UiTheme){
        .bg = {21, 16, 43, 255},
        .board = {13, 10, 30, 255},
        .panel = {34, 27, 70, 255},
        .rule = {62, 53, 110, 255},
        .text = {242, 236, 255, 255},
        .dim = {154, 145, 196, 255},
        .faint = {96, 88, 150, 255},
        .light = light,
        .gold = {255, 210, 63, 255},
        .danger = {255, 59, 48, 255},
    };
}

void Ui_Init(const char *assetsDir, const UiTheme *theme, int windowWidth, int windowHeight) {
    sTheme = *theme;
    sWinW = windowWidth;
    sWinH = windowHeight;

    char path[512];
    snprintf(path, sizeof(path), "%s/fonts/PressStart2P-Regular.ttf", assetsDir);
    if (FileExists(path)) {
        sFont = LoadFontEx(path, 64, NULL, 0);
        if (sFont.texture.id != 0) {
            SetTextureFilter(sFont.texture, TEXTURE_FILTER_POINT);
            sFontLoaded = true;
        }
    }
}

const UiTheme *Ui_Theme(void) { return &sTheme; }

static float Spacing(float size) { return size * 0.125f; } /* one font pixel */

void Ui_Text(const char *text, int x, int y, int size, Color color) {
    Font font = sFontLoaded ? sFont : GetFontDefault();
    DrawTextEx(font, text, (Vector2){(float)x, (float)y}, (float)size, Spacing((float)size), color);
}

int Ui_Measure(const char *text, int size) {
    Font font = sFontLoaded ? sFont : GetFontDefault();
    return (int)MeasureTextEx(font, text, (float)size, Spacing((float)size)).x;
}

void Ui_TextCentered(const char *text, int size, int centerX, int y, Color color) {
    Ui_Text(text, centerX - Ui_Measure(text, size) / 2, y, size, color);
}

void Ui_TextRight(const char *text, int size, int rightX, int y, Color color) {
    Ui_Text(text, rightX - Ui_Measure(text, size), y, size, color);
}

Color Ui_Lerp(Color a, Color b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return (Color){
        (unsigned char)((float)a.r + ((float)b.r - (float)a.r) * t),
        (unsigned char)((float)a.g + ((float)b.g - (float)a.g) * t),
        (unsigned char)((float)a.b + ((float)b.b - (float)a.b) * t),
        255,
    };
}

void Ui_BevelRect(int x, int y, int w, int h, Color base) {
    int bevel = (w >= 16 && h >= 16) ? 3 : 2;
    DrawRectangle(x, y, w, h, base);
    Color hi = Ui_Lerp(base, WHITE, 0.33f);
    Color lo = Ui_Lerp(base, BLACK, 0.33f);
    DrawRectangle(x, y, w, bevel, hi);
    DrawRectangle(x, y, bevel, h, hi);
    DrawRectangle(x, y + h - bevel, w, bevel, lo);
    DrawRectangle(x + w - bevel, y, bevel, h, lo);
}

/* ------------------------------------------------------------- particles */

#define MAX_PARTICLES 256

typedef struct {
    float x, y, vx, vy;
    float life, maxLife;
    float size;
    Color color;
} Particle;

static Particle sParticles[MAX_PARTICLES];

void Ui_Burst(float cx, float cy, Color color, int count, float speed) {
    for (int i = 0; i < MAX_PARTICLES && count > 0; i++) {
        Particle *p = &sParticles[i];
        if (p->life > 0.0f) continue;
        float angle = (float)GetRandomValue(0, 628) / 100.0f;
        float v = speed * (0.4f + (float)GetRandomValue(0, 60) / 100.0f);
        p->x = cx;
        p->y = cy;
        p->vx = cosf(angle) * v;
        p->vy = sinf(angle) * v;
        p->maxLife = p->life = 0.28f + (float)GetRandomValue(0, 22) / 100.0f;
        p->size = (float)(GetRandomValue(0, 1) ? 4 : 6); /* chunky: these are pixels, not sparks */
        p->color = color;
        count--;
    }
}

void Ui_UpdateParticles(float dt) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &sParticles[i];
        if (p->life <= 0.0f) continue;
        p->life -= dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->vx *= 1.0f - 3.0f * dt;
        p->vy *= 1.0f - 3.0f * dt;
        float k = p->life / p->maxLife;
        if (k <= 0.0f) continue;
        int s = (int)(p->size * (k > 0.5f ? 1.0f : k * 2.0f));
        if (s < 2) s = 2;
        /* Snapped to a 2px grid so debris stays in the same pixel world. */
        DrawRectangle(((int)p->x / 2) * 2 - s / 2, ((int)p->y / 2) * 2 - s / 2, s, s, p->color);
    }
}

void Ui_ClearParticles(void) {
    memset(sParticles, 0, sizeof(sParticles));
}

/* ---------------------------------------------------------------- chrome */

static Texture2D sBadge;
static bool sBadgeLoaded = false, sBadgeTried = false;

void Ui_CornerBadge(const char *iconPath, const char *versionLabel) {
    if (!sBadgeTried) {
        sBadgeTried = true;
        if (iconPath && FileExists(iconPath)) {
            sBadge = LoadTexture(iconPath);
            sBadgeLoaded = (sBadge.id != 0);
            if (sBadgeLoaded) SetTextureFilter(sBadge, TEXTURE_FILTER_POINT); /* keep pixel art crisp when scaled */
        }
    }
    if (sBadgeLoaded) {
        DrawTexturePro(sBadge, (Rectangle){0, 0, (float)sBadge.width, (float)sBadge.height},
                       (Rectangle){22, 16, 32, 32}, (Vector2){0, 0}, 0.0f, WHITE);
    }
    if (versionLabel) Ui_Text(versionLabel, 66, 28, UI_T8, sTheme.dim);
}

void Ui_Shutdown(void) {
    if (sBadgeLoaded) {
        UnloadTexture(sBadge);
        sBadgeLoaded = false;
    }
    if (sFontLoaded) {
        UnloadFont(sFont);
        sFontLoaded = false;
    }
}

static void ScreenTitle(const char *title) {
    Ui_Text(title, 40, 36, UI_T32, sTheme.text);
    DrawRectangle(40, 80, Ui_Measure(title, UI_T32), 4, sTheme.light);
}

static void SelectionBar(int x, int y, int w, int h) {
    DrawRectangle(x, y, w, h, sTheme.panel);
    DrawRectangle(x, y, 4, h, sTheme.light);
}

void Ui_DrawMenu(const UiMenu *m, float dt) {
    Win_BeginFrame();
    ClearBackground(sTheme.bg);

    if (m->backdrop) m->backdrop(dt);
    Ui_CornerBadge(m->iconPath, m->versionLabel);

    int cx = sWinW / 2;
    Ui_TextCentered(m->title, UI_T48, cx, 104, sTheme.light);
    if (m->tagline) Ui_TextCentered(m->tagline, UI_T8, cx, 168, sTheme.dim);

    /* Vertically centre the item block in the space between the tagline and
     * the footer, whatever the item count. */
    int blurbCount = 0;
    for (int i = 0; m->blurbs && i < m->itemCount; i++) if (m->blurbs[i]) blurbCount++;
    int blockH = m->itemCount * 48 + blurbCount * 18;
    int itemY = 200 + ((sWinH - 130) - 200 - blockH) / 2 + 12;

    for (int i = 0; i < m->itemCount; i++) {
        bool sel = (i == m->selected);
        if (sel) SelectionBar(cx - 170, itemY - 12, 340, 40);
        Ui_TextCentered(m->items[i], UI_T16, cx, itemY, sel ? sTheme.light : sTheme.text);
        if (m->blurbs && m->blurbs[i]) {
            Ui_TextCentered(m->blurbs[i], UI_T8, cx, itemY + 34, sTheme.dim);
            itemY += 18;
        }
        itemY += 48;
    }

    if (m->statLine) Ui_TextCentered(m->statLine, UI_T8, cx, sWinH - 112, sTheme.dim);
    if (m->username) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Playing as %.24s", m->username);
        Ui_TextCentered(buf, UI_T8, cx, sWinH - 88, sTheme.gold);
    }
    if (m->hints) Ui_TextCentered(m->hints, UI_T8, cx, sWinH - 48, sTheme.dim);
    Ui_TextCentered("Made by The Real Ghost", UI_T8, cx, sWinH - 20, sTheme.faint);

    /* A newly earned achievement, announced for a few seconds. */
    {
        static char sToast[64];
        static float sToastAge = 99.0f;
        if (sToastAge > 4.0f && Ach_PopToast(sToast, sizeof(sToast))) sToastAge = 0.0f;
        if (sToastAge <= 4.0f) {
            sToastAge += dt;
            int w = Ui_Measure(sToast, UI_T16) + 56;
            int wl = Ui_Measure("Achievement unlocked", UI_T8) + 56;
            if (wl > w) w = wl;
            int x = sWinW / 2 - w / 2, y = 10;
            float slide = sToastAge < 0.25f ? (1.0f - sToastAge / 0.25f) * 60.0f : 0.0f;
            DrawRectangle(x, y - (int)slide, w, 58, (Color){13, 10, 30, 240});
            DrawRectangle(x, y - (int)slide + 54, w, 4, sTheme.gold);
            Ui_TextCentered("Achievement unlocked", UI_T8, sWinW / 2, y - (int)slide + 10, sTheme.gold);
            Ui_TextCentered(sToast, UI_T16, sWinW / 2, y - (int)slide + 28, sTheme.text);
        }
    }
    Win_EndFrame();
}

/* The list can be longer than the window: a fixed row height, and the view
 * scrolls to keep the selected row in sight. */
void Ui_DrawSettingsTitled(const char *title, const char *note, const char *hint,
                           const UiSettingsRow *rows, int count, int selected, bool awaitingKey) {
    static int sTop = 0;
    Win_BeginFrame();
    ClearBackground(sTheme.bg);
    ScreenTitle(title);
    if (note && note[0]) Ui_Text(note, 40, 98, UI_T8, sTheme.dim);

    int left = 40, right = sWinW - 40;
    int rowH = 34;
    int firstY = 122;
    int visible = (sWinH - firstY - 50) / rowH;
    if (visible < 4) visible = 4;
    if (visible > count) visible = count;
    if (selected < sTop) sTop = selected;
    if (selected >= sTop + visible) sTop = selected - visible + 1;
    if (sTop < 0) sTop = 0;
    if (sTop > count - visible) sTop = count - visible;

    int rowY = firstY;
    for (int i = sTop; i < sTop + visible && i < count; i++) {
        const UiSettingsRow *r = &rows[i];
        bool sel = (i == selected);
        if (r->kind == UI_ROW_DONE) rowY += 8;
        if (sel) SelectionBar(left - 16, rowY - (rowH - 16) / 2, (right - left) + 32, rowH - 2);

        Ui_Text(r->label, left, rowY, UI_T16, sel ? sTheme.light : sTheme.text);
        switch (r->kind) {
            case UI_ROW_KEY:
                if (sel && awaitingKey) Ui_TextRight("Press a key", UI_T16, right, rowY, sTheme.gold);
                else Ui_TextRight((r->keyName && r->keyName[0]) ? r->keyName : "none", UI_T16, right, rowY, sTheme.dim);
                break;
            case UI_ROW_TOGGLE:
                Ui_TextRight(r->on ? "On" : "Off", UI_T16, right, rowY, r->on ? sTheme.light : sTheme.dim);
                break;
            case UI_ROW_VOLUME: {
                /* Ten blocks: each Left/Right press moves exactly one. */
                int blocks = r->percent / 10;
                for (int b = 0; b < 10; b++) {
                    DrawRectangle(right - 10 * 16 + b * 16 + 4, rowY, 12, 16, b < blocks ? sTheme.light : sTheme.rule);
                }
                break;
            }
            case UI_ROW_CHOICE: {
                char v[64];
                snprintf(v, sizeof(v), "< %s >", (r->keyName && r->keyName[0]) ? r->keyName : "");
                Ui_TextRight(v, UI_T16, right, rowY, sel ? sTheme.light : sTheme.dim);
                break;
            }
            case UI_ROW_DONE: break;
        }
        rowY += rowH;
    }
    if (sTop > 0) Ui_TextCentered("more above", UI_T8, sWinW / 2, firstY - 22, sTheme.faint);
    if (sTop + visible < count) Ui_TextCentered("more below", UI_T8, sWinW / 2, sWinH - 38, sTheme.faint);

    Ui_TextCentered(hint ? hint : "Up/Down move    Enter changes    Left/Right adjust    Esc back",
                    UI_T8, sWinW / 2, sWinH - 24, sTheme.dim);
    Win_EndFrame();
}

void Ui_DrawSettings(const UiSettingsRow *rows, int count, int selected, bool awaitingKey) {
    Ui_DrawSettingsTitled("Settings", NULL, NULL, rows, count, selected, awaitingKey);
}

/* The same screen with the arcade-wide rows (music volume, screen shake,
 * reduced flashing) slotted in before the last row, which is "Done". The
 * selected index counts those extra rows: a game's own row i is unchanged for
 * i < count - 1, the shared rows are count - 1 .. count - 2 + PREFS_ROWS, and
 * Done is count - 1 + PREFS_ROWS. */
void Ui_DrawSettingsPlus(const UiSettingsRow *rows, int count, int selected, bool awaitingKey) {
    UiSettingsRow all[48];
    int n = 0;
    ArcadePrefs *p = Prefs_Get();
    for (int i = 0; i < count - 1 && n < 44; i++) all[n++] = rows[i];
    all[n++] = (UiSettingsRow){UI_ROW_VOLUME, "Music volume", NULL, false, p->musicVolume};
    all[n++] = (UiSettingsRow){UI_ROW_TOGGLE, "Screen shake", NULL, p->screenShake, 0};
    all[n++] = (UiSettingsRow){UI_ROW_TOGGLE, "Reduced flashing", NULL, p->reducedFlashing, 0};
    all[n++] = (UiSettingsRow){UI_ROW_TOGGLE, "Fullscreen (F11)", NULL, p->fullscreen, 0};
    all[n++] = rows[count - 1];
    Ui_DrawSettings(all, n, selected, awaitingKey);
}

/* --- Leaderboard --------------------------------------------------------- */

/* Days between an ISO date (YYYY-MM-DD) and today, or -1 if it can't be read. */
static int DaysAgo(const char *iso) {
    int y, m, d;
    if (sscanf(iso, "%d-%d-%d", &y, &m, &d) != 3) return -1;
    struct tm t = {0};
    t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = d; t.tm_hour = 12; t.tm_isdst = -1;
    time_t then = mktime(&t), now = time(NULL);
    if (then == (time_t)-1) return -1;
    double days = difftime(now, then) / 86400.0;
    return days < 0 ? 0 : (int)(days + 0.5);
}

static const char *const kPeriodNames[UI_PERIOD_COUNT] = {"All time", "This week", "Today"};

int Ui_ScoresFilter(const GhostScore *in, int count, UiScorePeriod period, GhostScore *out, int maxOut) {
    int n = 0;
    for (int i = 0; i < count && n < maxOut; i++) {
        int ago = DaysAgo(in[i].date);
        if (period == UI_PERIOD_TODAY && ago != 0) continue;
        if (period == UI_PERIOD_WEEK && (ago < 0 || ago > 6)) continue;
        out[n++] = in[i];
    }
    return n;
}

void Ui_DrawScoresV2(const char *boardLabel, bool global, UiScorePeriod period, const GhostScore *entries, int count,
                     const char *username, const char *switchHint) {
    GhostScore shown[GHOSTLINK_TOP_N * 2];
    int shownCount = Ui_ScoresFilter(entries, count, period, shown, GHOSTLINK_TOP_N * 2);

    Win_BeginFrame();
    ClearBackground(sTheme.bg);
    ScreenTitle("Scores");

    int left = 40, right = sWinW - 40;
    Ui_Text(boardLabel, left, 112, UI_T16, sTheme.text);
    {   /* scope tabs, then the period, right-aligned */
        const char *tabs[2] = {"This machine", "World"};
        int x = right;
        for (int t = 1; t >= 0; t--) {
            bool active = (t == 1) == global;
            int w = Ui_Measure(tabs[t], UI_T8);
            x -= w;
            if (active) DrawRectangle(x - 8, 108, w + 16, 24, sTheme.panel);
            Ui_Text(tabs[t], x, 116, UI_T8, active ? sTheme.light : sTheme.dim);
            x -= 28;
        }
        const char *pn = kPeriodNames[period];
        Ui_Text("Showing", left, 142, UI_T8, sTheme.faint);
        Ui_Text(pn, left + Ui_Measure("Showing ", UI_T8) + 8, 142, UI_T8, sTheme.light);
    }
    DrawRectangle(left, 160, right - left, 2, sTheme.rule);

    int rowY = 176;
    if (shownCount == 0) {
        Ui_Text("Nothing here yet", left, rowY + 8, UI_T16, sTheme.dim);
        const char *why = period == UI_PERIOD_ALL
            ? (global ? "The world board downloads in the background when you are online."
                      : "Finish a run and it lands on this table.")
            : "No runs in this period. Press T for a wider one.";
        Ui_Text(why, left, rowY + 40, UI_T8, sTheme.dim);
    }

    static const Color kMedal[3] = {{255, 214, 90, 255}, {200, 210, 230, 255}, {214, 140, 90, 255}};
    int myRank = 0;
    for (int i = 0; i < shownCount; i++) {
        bool mine = strcmp(shown[i].username, username) == 0;
        if (mine && !myRank) myRank = i + 1;
        Color c = mine ? sTheme.light : (i < 3 ? kMedal[i] : sTheme.text);

        int rowH = 38;
        if (mine) DrawRectangle(left - 12, rowY - 10, right - left + 24, rowH - 2, sTheme.panel);
        else if (i % 2 == 0) DrawRectangle(left - 12, rowY - 10, right - left + 24, rowH - 2, Fade(sTheme.panel, 0.45f));
        if (mine) DrawRectangle(left - 12, rowY - 10, 4, rowH - 2, sTheme.light);

        /* rank: a small medal block for the top three, a plain number after */
        char buf[32];
        if (i < 3) {
            DrawRectangle(left, rowY - 4, 26, 24, kMedal[i]);
            snprintf(buf, sizeof(buf), "%d", i + 1);
            Ui_Text(buf, left + 5, rowY, UI_T16, sTheme.bg);
        } else {
            snprintf(buf, sizeof(buf), "%2d", i + 1);
            Ui_Text(buf, left + 2, rowY, UI_T16, sTheme.faint);
        }

        char name[20];
        snprintf(name, sizeof(name), "%.14s", shown[i].username);
        Ui_Text(name, left + 48, rowY, UI_T16, c);

        snprintf(buf, sizeof(buf), "%ld", shown[i].score);
        Ui_TextRight(buf, UI_T16, right - 112, rowY, c);
        Ui_TextRight(shown[i].date, UI_T8, right, rowY + 4, sTheme.dim);
        rowY += rowH;
    }

    /* Where you stand: your place and the gap to the score above. */
    if (shownCount > 0) {
        char line[96];
        if (myRank > 1) {
            long gap = shown[myRank - 2].score - shown[myRank - 1].score;
            snprintf(line, sizeof(line), "You are #%d, %ld behind #%d", myRank, gap, myRank - 1);
            Ui_TextCentered(line, UI_T8, sWinW / 2, sWinH - 72, sTheme.gold);
        } else if (myRank == 1) {
            Ui_TextCentered("You hold the top spot", UI_T8, sWinW / 2, sWinH - 72, sTheme.gold);
        } else {
            long best = shown[shownCount - 1].score;
            snprintf(line, sizeof(line), "Beat %ld to get on this board", best);
            Ui_TextCentered(line, UI_T8, sWinW / 2, sWinH - 72, sTheme.dim);
        }
    }
    Ui_TextCentered(global ? "The world board lists each player's best run"
                           : "Ghost Launcher reads this same table",
                    UI_T8, sWinW / 2, sWinH - 48, sTheme.faint);
    if (switchHint) Ui_TextCentered(switchHint, UI_T8, sWinW / 2, sWinH - 24, sTheme.dim);

    Win_EndFrame();
}

void Ui_DrawScores(const char *boardLabel, bool global, const GhostScore *entries, int count,
                   const char *username, const char *switchHint) {
    Ui_DrawScoresV2(boardLabel, global, UI_PERIOD_ALL, entries, count, username, switchHint);
}

#define UPDATES_HEADER_H 32
#define UPDATES_NOTE_H 16
#define UPDATES_NOTE_GAP 8
#define UPDATES_ENTRY_GAP 24
#define UPDATES_MARGIN_X 40
#define UPDATES_VIEWPORT_Y 112
#define UPDATES_MAX_WRAP_LINES 6
#define WRAP_LINE_BUF 256

/* Greedy word-wrap against the pixel font's measured width (its advance is
 * much wider than a typical UI font, so a chars-per-line guess isn't
 * reliable). Returns the number of lines produced (capped at maxLines). */
static int WrapText(const char *text, int size, int maxWidth, char outLines[][WRAP_LINE_BUF], int maxLines) {
    int lineCount = 0;
    char current[WRAP_LINE_BUF] = "";
    const char *p = text;

    while (*p && lineCount < maxLines) {
        const char *wordStart = p;
        while (*p && *p != ' ') p++;
        int wordLen = (int)(p - wordStart);
        if (wordLen >= (int)sizeof(current) - 1) wordLen = (int)sizeof(current) - 2;
        char word[WRAP_LINE_BUF];
        memcpy(word, wordStart, wordLen);
        word[wordLen] = '\0';

        char trial[2 * WRAP_LINE_BUF];
        if (current[0] == '\0') snprintf(trial, sizeof(trial), "%s", word);
        else snprintf(trial, sizeof(trial), "%s %s", current, word);

        if (current[0] == '\0' || Ui_Measure(trial, size) <= maxWidth) {
            snprintf(current, sizeof(current), "%.*s", (int)sizeof(current) - 1, trial);
        } else {
            snprintf(outLines[lineCount], WRAP_LINE_BUF, "%s", current);
            lineCount++;
            snprintf(current, sizeof(current), "%s", word);
        }
        if (*p == ' ') p++;
    }
    if (current[0] != '\0' && lineCount < maxLines) {
        snprintf(outLines[lineCount], WRAP_LINE_BUF, "%s", current);
        lineCount++;
    }
    return lineCount;
}

void Ui_DrawUpdates(const UiChangelogEntry *entries, int count, int *scrollPx) {
    int textX = UPDATES_MARGIN_X + 20;
    int noteMaxWidth = (sWinW - UPDATES_MARGIN_X) - textX;
    char wrapped[UPDATES_MAX_WRAP_LINES][WRAP_LINE_BUF];

    int contentH = 0;
    for (int e = 0; e < count; e++) {
        contentH += UPDATES_HEADER_H;
        for (int n = 0; entries[e].notes[n] != NULL; n++) {
            contentH += WrapText(entries[e].notes[n], UI_T8, noteMaxWidth, wrapped, UPDATES_MAX_WRAP_LINES) * UPDATES_NOTE_H
                        + UPDATES_NOTE_GAP;
        }
        contentH += UPDATES_ENTRY_GAP;
    }

    int viewportH = sWinH - UPDATES_VIEWPORT_Y - 50;
    int maxScroll = contentH > viewportH ? contentH - viewportH : 0;
    if (*scrollPx < 0) *scrollPx = 0;
    if (*scrollPx > maxScroll) *scrollPx = maxScroll;

    Win_BeginFrame();
    ClearBackground(sTheme.bg);
    ScreenTitle("Updates");

    BeginScissorMode(UPDATES_MARGIN_X, UPDATES_VIEWPORT_Y, sWinW - 2 * UPDATES_MARGIN_X, viewportH);
    int y = UPDATES_VIEWPORT_Y - *scrollPx;
    for (int e = 0; e < count; e++) {
        char header[64];
        snprintf(header, sizeof(header), "Version %s", entries[e].version);
        Ui_Text(header, UPDATES_MARGIN_X, y, UI_T16, sTheme.light);
        Ui_TextRight(entries[e].date, UI_T8, sWinW - UPDATES_MARGIN_X, y + 4, sTheme.dim);
        y += UPDATES_HEADER_H;

        for (int n = 0; entries[e].notes[n] != NULL; n++) {
            int nLines = WrapText(entries[e].notes[n], UI_T8, noteMaxWidth, wrapped, UPDATES_MAX_WRAP_LINES);
            DrawRectangle(UPDATES_MARGIN_X + 4, y + 2, 4, 4, sTheme.light);
            for (int li = 0; li < nLines; li++) {
                Ui_Text(wrapped[li], textX, y, UI_T8, sTheme.text);
                y += UPDATES_NOTE_H;
            }
            y += UPDATES_NOTE_GAP;
        }
        y += UPDATES_ENTRY_GAP;
    }
    EndScissorMode();

    /* Fade the top/bottom edges so scrolled-past content doesn't clip hard. */
    Color clear = sTheme.bg;
    clear.a = 0;
    DrawRectangleGradientV(UPDATES_MARGIN_X, UPDATES_VIEWPORT_Y, sWinW - 2 * UPDATES_MARGIN_X, 12, sTheme.bg, clear);
    DrawRectangleGradientV(UPDATES_MARGIN_X, UPDATES_VIEWPORT_Y + viewportH - 12, sWinW - 2 * UPDATES_MARGIN_X, 12, clear, sTheme.bg);

    Ui_TextCentered("Up/Down scroll    Esc back", UI_T8, sWinW / 2, sWinH - 24, sTheme.dim);
    Win_EndFrame();
}

void Ui_PauseOverlay(int x, int y, int w, int h) {
    DrawRectangle(x, y, w, h, (Color){13, 10, 30, 205});
    Ui_TextCentered("PAUSED", UI_T32, x + w / 2, y + h / 2 - 40, sTheme.text);
    Ui_TextCentered("P carries on", UI_T16, x + w / 2, y + h / 2 + 12, sTheme.dim);
}

/* The How to play card: goal, controls, one tip. */
void Ui_DrawHowTo(const HowTo *h) {
    Win_BeginFrame();
    ClearBackground(sTheme.bg);
    if (!h) { Win_EndFrame(); return; }
    ScreenTitle("How to play");
    Ui_Text(h->name, 40, 96, UI_T16, sTheme.light);

    int x = 40, maxW = sWinW - 80, y = 132;
    char lines[8][WRAP_LINE_BUF];
    int n = WrapText(h->goal, UI_T8, maxW, lines, 8);
    for (int i = 0; i < n; i++) { Ui_Text(lines[i], x, y, UI_T8, sTheme.text); y += 16; }
    y += 20;

    Ui_Text("Controls", x, y, UI_T8, sTheme.dim);
    y += 22;
    int keyW = 0;
    for (int i = 0; i < HOWTO_MAX_CONTROLS && h->controls[i][0]; i++) {
        int w = Ui_Measure(h->controls[i][0], UI_T8);
        if (w > keyW) keyW = w;
    }
    for (int i = 0; i < HOWTO_MAX_CONTROLS && h->controls[i][0]; i++) {
        DrawRectangle(x - 6, y - 5, sWinW - 68, 22, i % 2 ? sTheme.board : sTheme.panel);
        Ui_Text(h->controls[i][0], x, y, UI_T8, sTheme.light);
        Ui_Text(h->controls[i][1], x + keyW + 24, y, UI_T8, sTheme.text);
        y += 26;
    }
    y += 16;

    Ui_Text("Tip", x, y, UI_T8, sTheme.dim);
    y += 20;
    n = WrapText(h->tip, UI_T8, maxW, lines, 8);
    for (int i = 0; i < n; i++) { Ui_Text(lines[i], x, y, UI_T8, sTheme.gold); y += 16; }

    Ui_TextCentered("Enter or Esc to carry on", UI_T8, sWinW / 2, sWinH - 40, sTheme.dim);
    Win_EndFrame();
}
