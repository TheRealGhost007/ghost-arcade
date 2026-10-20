#include "menumodel.h"
#include "layout.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ numbers */

void Menu_Group(long value, char *out, size_t size) {
    if (size == 0) return;
    char digits[32];
    bool negative = value < 0;
    unsigned long v = negative ? (unsigned long)(-(value + 1)) + 1u : (unsigned long)value;
    snprintf(digits, sizeof(digits), "%lu", v);
    char buf[48];
    size_t n = strlen(digits), o = 0;
    if (negative) buf[o++] = '-';
    for (size_t i = 0; i < n && o + 2 < sizeof(buf); i++) {
        if (i > 0 && (n - i) % 3 == 0) buf[o++] = ' ';
        buf[o++] = digits[i];
    }
    buf[o] = '\0';
    snprintf(out, size, "%s", buf);
}

void Menu_BestText(RecordUnit unit, long value, const char *none, char *out, size_t size) {
    if (value <= 0) snprintf(out, size, "%s", none ? none : "");
    else if (unit == UNIT_TIME) Record_FormatValue(UNIT_TIME, value, out, size);
    else Menu_Group(value, out, size);
}

/* -------------------------------------------------------- the title drop */

int Title_LetterLift(int letter, float t) {
    float u = t - (float)letter * TITLE_DROP_GAP;
    if (u < 0.0f) return TITLE_DROP_FROM;
    if (u < TITLE_DROP_FALL) {
        float k = u / TITLE_DROP_FALL;
        return (int)lroundf((float)TITLE_DROP_FROM * (1.0f - k * k)); /* falls faster and faster */
    }
    u -= TITLE_DROP_FALL;
    if (u < TITLE_DROP_BOUNCE) return (int)lroundf(2.0f * sinf(3.14159265f * u / TITLE_DROP_BOUNCE));
    return 0;
}

bool Title_LetterLanded(int letter, float t) { return t - (float)letter * TITLE_DROP_GAP >= TITLE_DROP_FALL; }

float Title_DropSeconds(void) { return (float)(TITLE_LETTERS - 1) * TITLE_DROP_GAP + TITLE_DROP_FALL + TITLE_DROP_BOUNCE; }

/* ------------------------------------------------------ the screen slide */

int Slide_Offset(float t, int dir) {
    if (t >= SLIDE_SECONDS || dir == 0) return 0;
    if (t < 0.0f) t = 0.0f;
    float k = 1.0f - t / SLIDE_SECONDS;
    return (dir > 0 ? 1 : -1) * (int)lroundf((float)SLIDE_PX * k * k); /* eases in to rest */
}

/* ------------------------------------------------ a stepper held down */

int HoldStep_Update(HoldStep *h, bool pressed, bool down, float dt) {
    if (pressed) {
        h->held = 0.0f;
        h->next = HOLD_DELAY;
        h->armed = true;
        return 1;
    }
    if (!down || !h->armed) {
        h->held = 0.0f;
        h->next = HOLD_DELAY;
        h->armed = false;
        return 0;
    }
    h->held += dt;
    int steps = 0;
    while (h->held >= h->next && steps < 4) { /* a long frame gives a few steps, never a burst */
        steps++;
        h->next += h->next >= HOLD_FAST_AFTER ? HOLD_FAST : HOLD_SLOW;
    }
    if (h->next <= h->held) h->next = h->held + HOLD_FAST;
    return steps;
}

void SizeTween_Step(SizeTween *s, float w, float h, float dt, float seconds, bool motion) {
    if (!s->placed || !motion || seconds <= 0.0f) {
        s->fromW = s->toW = s->w = w;
        s->fromH = s->toH = s->h = h;
        s->t = 1.0f;
        s->placed = true;
        return;
    }
    if (w != s->toW || h != s->toH) {
        s->fromW = s->w;
        s->fromH = s->h;
        s->toW = w;
        s->toH = h;
        s->t = 0.0f;
    }
    s->t += dt / seconds;
    if (s->t > 1.0f) s->t = 1.0f;
    float e = 1.0f - (1.0f - s->t) * (1.0f - s->t);
    s->w = s->fromW + (s->toW - s->fromW) * e;
    s->h = s->fromH + (s->toH - s->fromH) * e;
}

/* --------------------------------------------------------- play setup */

static const char *const kNotes[MODE_COUNT] = {"endless", "40 lines", "two minutes", "no game over", "power-ups"};

static const char *const kAbout[MODE_COUNT] = {
    "Endless. Every ten lines the pace climbs, and the run ends when the stack reaches the top.",
    "Clear 40 lines as fast as you can. Your time is your score, the fastest first.",
    "Two minutes on the clock. Score as much as you can before the time is up.",
    "No game over and no clock. A full well clears itself and you carry on. Zen keeps no scores.",
    "Marathon with power-ups: some pieces carry a bomb, a laser, a freeze, a slow or a sweep.",
};

const char *Setup_ModeNote(GameMode mode) { return (mode >= 0 && mode < MODE_COUNT) ? kNotes[mode] : ""; }
const char *Setup_ModeAbout(GameMode mode) { return (mode >= 0 && mode < MODE_COUNT) ? kAbout[mode] : ""; }

static int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void Setup_Sanitize(Settings *s) {
    if (s->setupMode < 0 || s->setupMode >= MODE_COUNT) s->setupMode = 0;
    if (s->setupPreset < 0 || s->setupPreset >= BOARD_PRESET_COUNT) s->setupPreset = 0;
    s->setupWidth = Clamp(s->setupWidth, BOARD_MIN_W, BOARD_MAX_W);
    s->setupHeight = Clamp(s->setupHeight, BOARD_MIN_H, BOARD_MAX_H);
}

void Setup_Size(const Settings *s, int *w, int *h) {
    if (s->setupPreset >= 0 && s->setupPreset < BOARD_PRESET_CUSTOM) {
        *w = Record_Preset(s->setupPreset)->w;
        *h = Record_Preset(s->setupPreset)->h;
    } else {
        *w = s->setupWidth;
        *h = s->setupHeight;
    }
}

GameConfig Setup_Config(const Settings *s) {
    GameConfig cfg;
    cfg.mode = (GameMode)(s->setupMode % MODE_COUNT);
    Setup_Size(s, &cfg.width, &cfg.height);
    /* The classic marathon keeps the classic scoring, so its old scores still mean what they did. */
    cfg.modernScoring = !(cfg.mode == MODE_MARATHON && cfg.width == 10 && cfg.height == 20);
    return cfg;
}

void Setup_BoardLabel(const Settings *s, char *out, size_t size) {
    int w, h;
    Setup_Size(s, &w, &h);
    const char *name = (s->setupPreset >= 0 && s->setupPreset < BOARD_PRESET_CUSTOM) ? Record_Preset(s->setupPreset)->name : "Custom";
    snprintf(out, size, "%s %dx%d", name, w, h);
}

void Menu_BoardName(int w, int h, char *out, size_t size) {
    int preset = Record_PresetOf(w, h);
    snprintf(out, size, "%s %dx%d", preset >= 0 ? Record_Preset(preset)->name : "Custom", w, h);
}

void Setup_Summary(const Settings *s, char *out, size_t size) {
    char board[40];
    Setup_BoardLabel(s, board, sizeof(board));
    snprintf(out, size, "%s on %s", Game_ModeName((GameMode)(s->setupMode % MODE_COUNT)), board);
}

void Setup_StepBoard(Settings *s, int dir) {
    if (dir == 0) return;
    int n = BOARD_PRESET_COUNT;
    s->setupPreset = ((s->setupPreset + (dir > 0 ? 1 : -1)) % n + n) % n;
}

bool Setup_SetSize(Settings *s, int w, int h) {
    w = Clamp(w, BOARD_MIN_W, BOARD_MAX_W);
    h = Clamp(h, BOARD_MIN_H, BOARD_MAX_H);
    int ow, oh;
    Setup_Size(s, &ow, &oh);
    if (w == ow && h == oh) return false;
    int preset = Record_PresetOf(w, h);
    s->setupPreset = preset >= 0 ? preset : BOARD_PRESET_CUSTOM;
    s->setupWidth = w; /* Custom remembers the last size typed, preset or not */
    s->setupHeight = h;
    return true;
}

void Setup_InPlay(int w, int h, char *line1, size_t size1, char *line2, size_t size2) {
    int cell = 0;
    PanelPlace place = Layout_PlaceOf(w, h, &cell);
    snprintf(line1, size1, "In play: %d px cells,", cell);
    snprintf(line2, size2, "%s", place == PLACE_STRIP ? "panel across the top" : "panel at the side");
}

PreviewFit Setup_PreviewFit(int w, int h, int boxW, int boxH) {
    PreviewFit f = {0, 0, 0};
    if (w <= 0 || h <= 0) return f;
    int c = boxW / w < boxH / h ? boxW / w : boxH / h;
    if (c < 2) c = 2;
    f.cell = c;
    f.pw = c * w;
    f.ph = c * h;
    return f;
}

/* A small hash of the size and the cell, so each board has rubble of its own. */
static unsigned Hash(int w, int h, int x, int y) {
    unsigned v = (unsigned)(w * 73856093) ^ (unsigned)(h * 19349663) ^ (unsigned)(x * 83492791) ^ (unsigned)(y * 2654435761u);
    v ^= v >> 13;
    v *= 0x5bd1e995u;
    v ^= v >> 15;
    return v;
}

bool Setup_PreviewRubble(int w, int h, int x, int y) {
    if (x < 0 || y < 0 || x >= w || y >= h) return false;
    int rows = h / 6;
    if (rows < 2) rows = 2;
    if (y >= rows) return false;
    int gap = (int)(Hash(w, h, 0, y) % (unsigned)w); /* every row keeps one gap, so none is full */
    if (x == gap) return false;
    unsigned chance = y == 0 ? 90u : (y == 1 ? 72u : 45u); /* thinner toward the top */
    return Hash(w, h, x, y) % 100u < chance;
}

/* ----------------------------------------------------------- the menu */

MenuBack Menu_OnBack(bool onQuit) { return onQuit ? MENU_BACK_QUIT : MENU_BACK_TO_QUIT; }
