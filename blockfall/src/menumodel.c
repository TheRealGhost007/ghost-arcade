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

void Menu_RecordWords(const StatRecord *r, int kind, int who, bool all, bool anyRun, char *value, size_t vn, char *line1, size_t n1, char *line2, size_t n2) {
    if (n2) line2[0] = '\0';
    if (!r || r->value <= 0) {
        snprintf(value, vn, "-");
        snprintf(line1, n1, "%s", anyRun ? "none yet" : "no run yet");
        return;
    }
    if (kind == 0) Stats_FormatPps(r->value, value, vn);
    else Menu_Group(r->value, value, vn);
    if (all && who >= 0 && who < MODE_COUNT) {
        snprintf(line1, n1, "%s", Game_ModeName((GameMode)who));
        if (n2) snprintf(line2, n2, "%s", r->date);
    } else {
        snprintf(line1, n1, "%s", r->date);
    }
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

static const char *const kNotes[MODE_COUNT] = {"endless", "40 lines", "two minutes", "no game over", "power-ups", "rising rows"};

/* Each wraps in four lines at 8 px in Play setup's right column
 * (SETUP_RIGHT_W, 246 px since 3.6.1; tests/test_layout.c checks it). */
static const char *const kAbout[MODE_COUNT] = {
    "Endless. Every ten lines the pace climbs, and the run ends when the stack reaches the top.",
    "Clear 40 lines as fast as you can. Your time is your score, the fastest first.",
    "Two minutes on the clock. Score as much as you can before the time is up.",
    "No game over and no clock. A full well clears itself and you carry on. Zen keeps no scores.",
    "Marathon with power-ups: some pieces carry a bomb, a laser, a freeze, a slow or a sweep.",
    "Grey rows, one gap each; another pushes up every few seconds, sooner as you go. Rows cleared count as Dug.",
};

const char *Setup_ModeNote(GameMode mode) { return (mode >= 0 && mode < MODE_COUNT) ? kNotes[mode] : ""; }
const char *Setup_ModeAbout(GameMode mode) { return (mode >= 0 && mode < MODE_COUNT) ? kAbout[mode] : ""; }

static int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void Setup_Sanitize(Settings *s) {
    if (s->setupMode < 0 || s->setupMode >= MODE_COUNT) s->setupMode = 0;
    if (s->setupPreset < 0 || s->setupPreset >= BOARD_PRESET_COUNT) s->setupPreset = 0;
    s->setupWidth = Clamp(s->setupWidth, BOARD_MIN_W, BOARD_MAX_W);
    s->setupHeight = Clamp(s->setupHeight, BOARD_MIN_H, BOARD_MAX_H);
    Practice_Sanitize(s);
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
    /* Modern rules on every board since 3.2.0, the classic 10x20 Marathon
     * included (the owner's decision in REWORK_PLAN.md): its old scores
     * stay on their own board, "Classic (old rules)" (runrecord.h). */
    cfg.modernScoring = true;
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

int Setup_PreviewGap(int w, int h, int y) {
    if (w <= 1) return 0;
    if (y < 0) y = 0;
    int gap = (int)(Hash(w, h, 7, y) % (unsigned)w);
    if (y > 0) {
        int below = Setup_PreviewGap(w, h, y - 1);
        if (gap == below) gap = (gap + 1 + (int)(Hash(w, h, 11, y) % (unsigned)(w - 1))) % w;
    }
    return gap;
}

/* ------------------------------------------------- the Practice room */

static const char *const kProLabels[PRO_COUNT] = {"Start level", "Gravity", "Lock delay", "Hold", "Starting garbage", "Finesse hints"};
static const char *const kProHelp[PRO_COUNT] = {
    "The level the run begins at, 1 to 20: its speed, and the lines count up from there.",
    "Off: the piece hangs where it is until you drop it. Soft and hard drops still work.",
    "Off: a landed piece never locks by itself, so you can slide it about; a hard drop locks it.",
    "Once: one Hold per piece, as in a run. Infinite: as often as you like, to try each piece in turn.",
    "Grey rows the well starts with, one gap each (Dig's rows), to practise digging.",
    "A piece that took more presses than it needed says how many as it lands (\"2 extra presses\").",
};
/* "Once per piece" until 3.6.1: wider than the stepper. */
static const char *const kHoldWords[2] = {"Once", "Infinite"};

const char *Practice_OptionLabel(PracticeOption o) { return (o >= 0 && o < PRO_COUNT) ? kProLabels[o] : ""; }
const char *Practice_OptionHelp(PracticeOption o) { return (o >= 0 && o < PRO_COUNT) ? kProHelp[o] : ""; }

PracticeOptionKind Practice_OptionKind(PracticeOption o) {
    switch (o) {
        case PRO_LEVEL: case PRO_GARBAGE: return PRO_KIND_NUMBER;
        case PRO_HOLD: return PRO_KIND_CHOICE;
        default: return PRO_KIND_TOGGLE;
    }
}

void Practice_OptionRange(PracticeOption o, int *min, int *max) {
    int lo = 0, hi = 1;
    if (o == PRO_LEVEL) { lo = PRACTICE_LEVEL_MIN; hi = PRACTICE_LEVEL_MAX; }
    else if (o == PRO_GARBAGE) { lo = 0; hi = PRACTICE_GARBAGE_MAX; }
    if (min) *min = lo;
    if (max) *max = hi;
}

const char *Practice_ChoiceWord(PracticeOption o, int index) {
    if (o == PRO_HOLD && index >= 0 && index < 2) return kHoldWords[index];
    return NULL;
}

int Practice_OptionValue(const Settings *s, PracticeOption o) {
    int lo, hi, v;
    Practice_OptionRange(o, &lo, &hi);
    switch (o) {
        case PRO_LEVEL: v = s->practiceLevel; break;
        case PRO_GRAVITY: v = s->practiceGravity ? 1 : 0; break;
        case PRO_LOCK: v = s->practiceLock ? 1 : 0; break;
        case PRO_HOLD: v = s->practiceInfiniteHold ? 1 : 0; break;
        case PRO_GARBAGE: v = s->practiceGarbage; break;
        case PRO_HINTS: v = s->practiceHints ? 1 : 0; break;
        default: v = lo; break;
    }
    return Clamp(v, lo, hi);
}

void Practice_OptionWords(const Settings *s, PracticeOption o, char *out, size_t size) {
    int v = Practice_OptionValue(s, o);
    switch (Practice_OptionKind(o)) {
        case PRO_KIND_NUMBER:
            if (o == PRO_GARBAGE) {
                if (v == 0) snprintf(out, size, "none");
                else snprintf(out, size, v == 1 ? "%d row" : "%d rows", v);
            } else {
                snprintf(out, size, "%d", v);
            }
            break;
        case PRO_KIND_CHOICE: snprintf(out, size, "%s", Practice_ChoiceWord(o, v)); break;
        default: snprintf(out, size, "%s", v ? "On" : "Off"); break;
    }
}

bool Practice_SetOption(Settings *s, PracticeOption o, int value) {
    int lo, hi;
    Practice_OptionRange(o, &lo, &hi);
    value = Clamp(value, lo, hi);
    if (value == Practice_OptionValue(s, o)) return false;
    switch (o) {
        case PRO_LEVEL: s->practiceLevel = value; break;
        case PRO_GRAVITY: s->practiceGravity = value != 0; break;
        case PRO_LOCK: s->practiceLock = value != 0; break;
        case PRO_HOLD: s->practiceInfiniteHold = value != 0; break;
        case PRO_GARBAGE: s->practiceGarbage = value; break;
        case PRO_HINTS: s->practiceHints = value != 0; break;
        default: return false;
    }
    return true;
}

void Practice_Sanitize(Settings *s) {
    s->practiceLevel = Clamp(s->practiceLevel, PRACTICE_LEVEL_MIN, PRACTICE_LEVEL_MAX);
    s->practiceGarbage = Clamp(s->practiceGarbage, 0, PRACTICE_GARBAGE_MAX);
}

PracticeRules Practice_Rules(const Settings *s) {
    PracticeRules r;
    memset(&r, 0, sizeof(r));
    r.on = true;
    r.startLevel = Clamp(s->practiceLevel, PRACTICE_LEVEL_MIN, PRACTICE_LEVEL_MAX);
    r.noGravity = !s->practiceGravity;
    r.noLock = !s->practiceLock;
    r.infiniteHold = s->practiceInfiniteHold;
    r.garbageRows = Clamp(s->practiceGarbage, 0, PRACTICE_GARBAGE_MAX);
    r.hints = s->practiceHints;
    return r;
}

GameConfig Practice_Config(const Settings *s) {
    GameConfig cfg = Setup_Config(s);
    cfg.mode = MODE_MARATHON; /* the room is a Marathon with its rules bent (Practice_Rules), whatever Play setup had */
    return cfg;
}

void Practice_Summary(const Settings *s, char *out, size_t size) {
    char board[40];
    Setup_BoardLabel(s, board, sizeof(board));
    snprintf(out, size, "Practice on %s", board);
}

void Practice_UndoWords(const ControlWords *w, char *out, size_t size) {
    bool key = w->undoName[0] && strcmp(w->undoName, "none") != 0;
    char pad[16];
    snprintf(pad, sizeof(pad), "%s", w->padUndo);
    char *sp = strchr(pad, ' '); /* the first button of its two */
    if (sp) *sp = '\0';
    if (!key && !pad[0]) {
        snprintf(out, size, "Undo has no key or button: set one in Settings > Controls.");
        return;
    }
    if (key && pad[0]) snprintf(out, size, "%s or %s takes the last piece back, up to %d times.", w->undoName, pad, PRACTICE_UNDO_MAX);
    else snprintf(out, size, "%s takes the last piece back, up to %d times.", key ? w->undoName : pad, PRACTICE_UNDO_MAX);
}

/* ----------------------------------------------------------- the menu */

MenuBack Menu_OnBack(bool onQuit) { return onQuit ? MENU_BACK_QUIT : MENU_BACK_TO_QUIT; }
