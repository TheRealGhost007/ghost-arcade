#include "setpage.h"
#include "controls.h"
#include "uikeymap.h"
#include <stdio.h>
#include <string.h>

static const char *const kTabs[SET_TABS] = {"Controls", "Feel", "Look", "Sound", "Game"};

const char *SetPage_TabLabel(int tab) { return (tab >= 0 && tab < SET_TABS) ? kTabs[tab] : ""; }

/* ------------------------------------------------------------- the caps */

bool SetPage_IsCap(int id) { return id >= SID_CAP && id < SID_CAP + CONTROL_ACTIONS * SET_CAP_COLUMNS; }
int SetPage_CapAction(int id) { return SetPage_IsCap(id) ? (id - SID_CAP) / SET_CAP_COLUMNS : -1; }
int SetPage_CapColumn(int id) { return SetPage_IsCap(id) ? (id - SID_CAP) % SET_CAP_COLUMNS : -1; }

static const int kCapX[SET_CAP_COLUMNS] = {256, 350, 444, 522};

KitRect SetPage_CapRect(int action, int column) {
    int c = column < 0 ? 0 : (column >= SET_CAP_COLUMNS ? SET_CAP_COLUMNS - 1 : column);
    return (KitRect){(float)kCapX[c], (float)(SET_CAP_Y + action * SET_CAP_PITCH), (float)(c < 2 ? SET_KEY_W : SET_PAD_W), (float)SET_CAP_H};
}

/* ------------------------------------------------------------ building */

static SetRow *AddRow(SetPage *p, float y, float h, const char *label, const char *help) {
    if (p->rowCount >= SET_MAX_ROWS) return NULL;
    SetRow *r = &p->rows[p->rowCount++];
    memset(r, 0, sizeof(*r));
    r->r = (KitRect){SET_ROW_X, y, SET_ROW_W, h};
    snprintf(r->label, sizeof(r->label), "%s", label ? label : "");
    r->help = help;
    return r;
}

static float RowY(int i) { return (float)(SET_ROW_Y + i * SET_ROW_PITCH); }

static KitItem *Control(SetPage *p, SetRow *row, int id, KitKind kind, KitRect r, const char *label) {
    KitItem *it = Kit_Add(&p->list, id, kind, r, label);
    if (it && row && row->count < SET_ROW_CONTROLS) row->controls[row->count++] = id;
    return it;
}

static KitRect ControlRect(const SetRow *row, float x, float w) {
    return (KitRect){x, row->r.y + (row->r.h - SET_CONTROL_H) / 2.0f, w, SET_CONTROL_H};
}

/* Value controls carry their row's label: never drawn, but it names them
 * (the screenshot harness asks for controls by name). */
static KitItem *Toggle(SetPage *p, SetRow *row, int id, bool on) {
    KitItem *it = Control(p, row, id, KIT_TOGGLE, ControlRect(row, SET_CONTROL_X, SET_TOGGLE_W), row->label);
    if (it) it->value = on ? 1 : 0;
    return it;
}

static KitItem *Choice(SetPage *p, SetRow *row, int id, const char *const *words, int count, int value) {
    int v = value >= 0 && value < count ? value : 0;
    KitItem *it = Control(p, row, id, KIT_STEPPER, ControlRect(row, SET_STEPPER_X, SET_STEPPER_W), words[v]);
    if (it) {
        it->value = v;
        it->min = 0;
        it->max = count - 1;
        it->flags |= KIT_WRAP;
    }
    return it;
}

static KitItem *Number(SetPage *p, SetRow *row, int id, int value, int min, int max, const char *words) {
    KitItem *it = Control(p, row, id, KIT_STEPPER, ControlRect(row, SET_STEPPER_X, SET_STEPPER_W), words);
    if (it) {
        it->value = value < min ? min : (value > max ? max : value);
        it->min = min;
        it->max = max;
    }
    return it;
}

static KitItem *Blocks(SetPage *p, SetRow *row, int id, int value) {
    KitItem *it = Control(p, row, id, KIT_SLIDER, ControlRect(row, SET_CONTROL_X, KIT_BLOCK_PITCH * 11), row->label);
    if (it) {
        it->value = value < 0 ? 0 : (value > 10 ? 10 : value);
        it->max = 10;
    }
    return it;
}

static KitItem *Button(SetPage *p, SetRow *row, int id, float x, float w, const char *label) {
    KitItem *it = Control(p, row, id, KIT_BUTTON, ControlRect(row, x, w), label);
    if (it) {
        it->style = KIT_STYLE_SMALL;
        it->textSize = 8;
    }
    return it;
}

/* ------------------------------------------------------------ Controls */

static const char *const kActionHelp[CONTROL_ACTIONS] = {
    "Slides the piece left. Held, it repeats as the Feel tab says.",
    "Slides the piece right. Held, it repeats as the Feel tab says.",
    "Drops the piece faster while held, a point a row.",
    "Drops the piece straight down and locks it, two points a row.",
    "Turns the piece clockwise.",
    "Turns the piece counter-clockwise.",
    "Puts the piece aside for later, once per piece.",
    "Opens the pause menu. Its second key and button also go back a step there.",
    "Starts the run again, asking first when it has points (see Game).",
    "Turns the piece half way round in one press. It has no key until you set one.",
    "In the Practice room, takes the last piece back, up to 50 times. It does nothing in any other run.",
};

static const char *const kColumnNames[SET_CAP_COLUMNS] = {"key", "key 2", "pad", "pad 2"};

static void LayoutControls(SetPage *p, const SetView *v) {
    p->intro = "Pick a key or a pad button, then press the new one.";
    p->footnote = "Esc and Menu always pause. M mutes, - and = set the volume, F3 shows the frame rate.";
    for (int a = 0; a < CONTROL_ACTIONS; a++) {
        SetRow *row = AddRow(p, (float)(SET_CAP_Y + a * SET_CAP_PITCH), SET_CAP_H, Controls_ActionLabel(a), kActionHelp[a]);
        if (!row) return;
        for (int c = 0; c < SET_CAP_COLUMNS; c++) {
            char label[KIT_LABEL_LEN];
            if (c < 2) {
                const char *k = Controls_KeyConst(v->s, a, c);
                if (Controls_IsNone(k)) snprintf(label, sizeof(label), "-");
                else Controls_Name(k, label, sizeof(label));
            } else {
                int b = Controls_Pad(v->s, a, c - 2);
                snprintf(label, sizeof(label), "%s", b == PAD_NONE ? "-" : Controls_PadCap(b));
            }
            KitItem *it = Control(p, row, SID_CAP_OF(a, c), KIT_BUTTON, SetPage_CapRect(a, c), label);
            if (!it) continue;
            it->style = KIT_STYLE_SMALL;
            it->textSize = 8;
            it->flags |= KIT_SELFDRAWN | (c >= 2 ? KIT_PADCAP : 0);
            snprintf(it->caption, sizeof(it->caption), "%.12s %s", Controls_ActionLabel(a), kColumnNames[c]);
        }
    }
    SetRow *row = AddRow(p, SET_RESET_Y, SET_CAP_H, "", "Every key and button back to how Blockfall came. It asks first.");
    if (!row) return;
    KitItem *it = Control(p, row, SID_RESET, KIT_BUTTON, (KitRect){SET_LABEL_X - 12, SET_RESET_Y, 200, SET_CAP_H}, "Reset controls");
    if (it) {
        it->style = KIT_STYLE_SMALL;
        it->textSize = 8;
        if (Controls_AreDefault(v->s)) Kit_Disable(it, "Every key and button is as Blockfall came.");
    }
}

/* ---------------------------------------------------------------- Feel */

static const FeelPreset kPresets[FEEL_PRESETS] = {
    {"Relaxed", 230, 50, SOFT_NORMAL},
    {"Standard", REPEAT_DELAY_DEFAULT, REPEAT_SPEED_DEFAULT, SOFT_NORMAL},
    {"Fast", 110, 15, SOFT_FAST},
};

const FeelPreset *SetPage_Preset(int i) { return (i >= 0 && i < FEEL_PRESETS) ? &kPresets[i] : NULL; }

int SetPage_PresetOf(const Settings *s) {
    for (int i = 0; i < FEEL_PRESETS; i++)
        if (s->repeatDelayMs == kPresets[i].delayMs && s->repeatSpeedMs == kPresets[i].speedMs && s->softDropSpeed == kPresets[i].softSpeed) return i;
    return -1;
}

void SetPage_MsWords(int ms, char *out, int size) { snprintf(out, (size_t)size, "%d ms", ms); }

static const char *const kSoftWords[SOFT_SPEEDS] = {"Slow", "Normal", "Fast", "Instant"};
const char *SetPage_SoftWords(int speed) { return kSoftWords[speed >= 0 && speed < SOFT_SPEEDS ? speed : SOFT_NORMAL]; }

#define DELAY_STEP 10
#define SPEED_STEP 5

static void LayoutFeel(SetPage *p, const SetView *v) {
    const Settings *s = v->s;
    p->intro = "How Left, Right and Down repeat in a run.";
    SetRow *row = AddRow(p, RowY(0), SET_ROW_H, "Presets", "Relaxed waits longer before sliding, Fast slides sooner and quicker. Standard is 170 ms and 35 ms.");
    if (row) {
        int at = SetPage_PresetOf(s);
        for (int i = 0; i < FEEL_PRESETS; i++) {
            KitItem *it = Button(p, row, SID_PRESET + i, (float)(SET_CONTROL_X + i * 84), 78, kPresets[i].name);
            if (it && i == at) it->flags |= KIT_ACCENT; /* the values are this one's */
        }
    }
    char words[16];
    row = AddRow(p, RowY(1), SET_ROW_H, "Repeat delay", "Auto-repeat: how long Left or Right is held before the piece starts to slide by itself.");
    if (row) {
        SetPage_MsWords(s->repeatDelayMs, words, sizeof(words));
        Number(p, row, SID_DELAY, s->repeatDelayMs / DELAY_STEP, REPEAT_DELAY_MIN / DELAY_STEP, REPEAT_DELAY_MAX / DELAY_STEP, words);
    }
    row = AddRow(p, RowY(2), SET_ROW_H, "Repeat speed", "Once it slides, a step every so many ms. 0 sends it straight to the wall.");
    if (row) {
        SetPage_MsWords(s->repeatSpeedMs, words, sizeof(words));
        Number(p, row, SID_SPEED, s->repeatSpeedMs / SPEED_STEP, REPEAT_SPEED_MIN / SPEED_STEP, REPEAT_SPEED_MAX / SPEED_STEP, words);
    }
    row = AddRow(p, RowY(3), SET_ROW_H, "Soft drop speed", "How fast Down drops the piece. Instant takes it to the floor without locking it.");
    if (row) Choice(p, row, SID_SOFT, kSoftWords, SOFT_SPEEDS, s->softDropSpeed);
    row = AddRow(p, RowY(4), 52, "Try it", "Hold Left or Right here to try your values. Left alone, it shows them by itself.");
    if (row) {
        KitItem *it = Control(p, row, SID_STRIP, KIT_BUTTON, (KitRect){SET_CONTROL_X - 8, RowY(4), 256, 52}, "Try it");
        if (it) it->flags |= KIT_SELFDRAWN;
    }
}

/* ---------------------------------------------------------------- Look */

static const char *const kGridWords[GRID_STYLES] = {"On", "Faint", "Off"};
static const char *const kShownWords[2] = {"Shown", "Hidden"};
static const char *const kPieceWords[2] = {"Smooth", "Snap"};
static const char *const kMotionWords[2] = {"Full", "Reduced"};
static const char *const kRestartWords[2] = {"Asks first", "At once"};
/* The steppers go Outline, Solid, Off. */
static const int kGhostOrder[GHOST_STYLES] = {GHOST_OUTLINE, GHOST_SOLID, GHOST_OFF};
static const char *const kGhostOrdered[GHOST_STYLES] = {"Outline", "Solid", "Off"};

static int GhostStep(int ghost) {
    int g = ghost == GHOST_OFF ? GHOST_OFF : (ghost == GHOST_SOLID ? GHOST_SOLID : GHOST_OUTLINE);
    for (int i = 0; i < GHOST_STYLES; i++)
        if (kGhostOrder[i] == g) return i;
    return 0;
}

static void SharedToggle(SetPage *p, int i, const char *label, const char *help, int id, bool on) {
    SetRow *row = AddRow(p, RowY(i), SET_ROW_H, label, help);
    if (!row) return;
    row->shared = true;
    Toggle(p, row, id, on);
}

static void LayoutLook(SetPage *p, const SetView *v) {
    const Settings *s = v->s;
    p->intro = "How the well and the pieces look. Rows marked * are shared.";
    p->footnote = "* Shared with every game: it changes in all of them.";
    SetRow *row = AddRow(p, RowY(0), SET_ROW_H, "Ghost piece", "Where the piece will land: an outline, a solid block, or none.");
    if (row) Choice(p, row, SID_GHOST, kGhostOrdered, GHOST_STYLES, GhostStep(s->ghostPiece));
    row = AddRow(p, RowY(1), SET_ROW_H, "Grid", "The lines in the well: on, faint or off.");
    if (row) Choice(p, row, SID_GRID, kGridWords, GRID_STYLES, s->grid);
    row = AddRow(p, RowY(2), SET_ROW_H, "Block patterns", "A mark of its own on each piece's blocks, so you can tell them apart without their colours.");
    if (row) Toggle(p, row, SID_PATTERNS, s->blockPatterns);
    row = AddRow(p, RowY(3), SET_ROW_H, "Controls card", "The card beside the well in a run that names your keys, or your pad's buttons.");
    if (row) Choice(p, row, SID_CARD, kShownWords, 2, s->hideControls ? 1 : 0);
    row = AddRow(p, RowY(4), SET_ROW_H, "Piece motion",
                 s->reducedMotion ? "Smooth lets a tap glide; Snap moves cell by cell. With Motion Reduced it snaps anyway."
                                  : "Smooth lets a tap or a fall glide a little; Snap moves the piece cell by cell.");
    if (row) Choice(p, row, SID_PIECE, kPieceWords, 2, s->pieceSnap ? 1 : 0);
    row = AddRow(p, RowY(5), SET_ROW_H, "Motion", "Reduced turns slides, glides, drops and wipes into plain cuts, and halves the bits that fly.");
    if (row) Choice(p, row, SID_MOTION, kMotionWords, 2, s->reducedMotion ? 1 : 0);
    const ArcadePrefs *pr = v->prefs;
    SharedToggle(p, 6, "Screen shake", "Lets the games shake the screen on big clears and blasts.", SID_SHAKE, pr->screenShake);
    SharedToggle(p, 7, "Reduced flashing", "No white flashes, and nothing blinks: soft tints instead, here and in every game.", SID_FLASH, pr->reducedFlashing);
    SharedToggle(p, 8, "Fullscreen", "Fills the whole screen. F11 or Alt+Enter does the same, in every game.", SID_FULL, pr->fullscreen);
}

/* --------------------------------------------------------------- Sound */

static void LayoutSound(SetPage *p, const SetView *v) {
    const Settings *s = v->s;
    p->intro = "Effects are Blockfall's own. Music volume is shared.";
    p->footnote = "* Shared with every game: it changes in all of them.";
    SetRow *row = AddRow(p, RowY(0), SET_ROW_H, "Effects volume", "How loud the drops, turns and clears are. Each block ticks at its own pitch.");
    if (row) Blocks(p, row, SID_EFFECTS, (s->volumePercent + 5) / 10);
    row = AddRow(p, RowY(1), SET_ROW_H, "Music volume", "How loud the music is, in every game and in the hall.");
    if (row) {
        row->shared = true;
        Blocks(p, row, SID_MUSIC, (v->prefs->musicVolume + 5) / 10);
    }
    row = AddRow(p, RowY(2), SET_ROW_H, "Sound", "All of Blockfall's sound on or off. M does the same in a run.");
    if (row) Toggle(p, row, SID_SOUND, s->audioEnabled);
    row = AddRow(p, RowY(3), SET_ROW_H, "Test sound", "Plays a line clear at the effects volume.");
    if (row) {
        KitItem *it = Button(p, row, SID_TEST, SET_CONTROL_X, 120, "Play");
        if (it && !s->audioEnabled) Kit_Disable(it, "Sound is off.");
        else if (it && !v->soundReady) Kit_Disable(it, "There is no sound device here.");
    }
}

/* ---------------------------------------------------------------- Game */

static void LayoutGame(SetPage *p, const SetView *v) {
    const Settings *s = v->s;
    p->intro = "What happens around a run.";
    SetRow *row = AddRow(p, RowY(0), SET_ROW_H, "Auto pause", "Pauses the run when the window loses focus, so no piece falls while you are away.");
    if (row) Toggle(p, row, SID_AUTOPAUSE, s->autoPause);
    /* "R restarts", with your key */
    char name[24], label[40];
    const char *k = Controls_IsNone(s->restart) ? s->restart2 : s->restart;
    Controls_Name(k, name, sizeof(name));
    snprintf(label, sizeof(label), "%s restarts", name);
    if (Controls_IsNone(k) || Kit_TextWidth(label, 16) > SET_STEPPER_X - SET_LABEL_X - 8) snprintf(label, sizeof(label), "Restart key");
    row = AddRow(p, RowY(1), SET_ROW_H, label, "Your Restart key in a run that has points: ask \"are you sure?\" first, or restart at once. The pad's Restart button always asks.");
    if (row) Choice(p, row, SID_QUICKR, kRestartWords, 2, s->quickRestart ? 1 : 0);
    row = AddRow(p, RowY(2), SET_ROW_H, "Ready, go", "READY, then GO as a run starts (only GO after a restart). The clock waits for GO.");
    if (row) Toggle(p, row, SID_READY, s->readyGo);
    row = AddRow(p, RowY(3), SET_ROW_H, "Resume countdown", "3, 2, 1 over the board before play goes on after a pause.");
    if (row) Toggle(p, row, SID_COUNTDOWN, s->resumeCountdown);
    /* [Show How to play on next launch] is a button, as the plan writes it:
     * the row label plus "Next launch" (the whole sentence is 556 px at 16,
     * too wide for the label column). */
    row = AddRow(p, RowY(4), SET_ROW_H, "Show How to play",
                 s->showHowTo ? "It is set to open next time: How to play, its first page, when Blockfall next starts."
                              : "Opens How to play, its first page, the next time Blockfall starts.");
    if (row) Button(p, row, SID_HOWTO, SET_CONTROL_X, 120, "Next launch");
    /* The Next queue (3.2.0): looks only, the pieces are the same. */
    char words[16];
    snprintf(words, sizeof(words), s->nextQueue == 1 ? "%d piece" : "%d pieces", s->nextQueue);
    row = AddRow(p, RowY(5), SET_ROW_H, "Next queue", "How many pieces Next shows: the next one large, up to five more small. The pieces come the same whatever it shows.");
    if (row) Number(p, row, SID_QUEUE, s->nextQueue, QUEUE_SHOWN_MIN, QUEUE_SHOWN_LIMIT, words);
}

static void AddTabs(SetPage *p, int tab) {
    KitItem probe;
    memset(&probe, 0, sizeof(probe));
    probe.kind = KIT_TABS;
    probe.textSize = 16;
    probe.cells = kTabs;
    probe.cellCount = SET_TABS;
    KitRect b = Kit_Bounds(&probe);
    float x = ((float)Kit_ScreenW() - b.w) / 2.0f;
    KitItem *it = Kit_Add(&p->list, SID_TABS, KIT_TABS, (KitRect){(float)(int)x, SET_TABS_Y, 0, SET_TABS_H}, "Tabs");
    if (!it) return;
    it->cells = kTabs;
    it->cellCount = SET_TABS;
    it->value = tab;
    it->flags |= KIT_WRAP; /* RB past the last tab comes round to the first */
}

void SetPage_Layout(const SetView *v, SetPage *p) {
    memset(p, 0, sizeof(*p));
    Kit_Clear(&p->list, 0); /* the page moves the focus itself: rows, then along a row */
    switch (v->tab) {
        case TAB_FEEL: LayoutFeel(p, v); break;
        case TAB_LOOK: LayoutLook(p, v); break;
        case TAB_SOUND: LayoutSound(p, v); break;
        case TAB_GAME: LayoutGame(p, v); break;
        default: LayoutControls(p, v); break;
    }
    AddTabs(p, v->tab);
    KitItem *back = Kit_Add(&p->list, SID_BACK, KIT_BUTTON, (KitRect){20, 14, 80, 24}, "Back");
    if (back) {
        back->style = KIT_STYLE_SMALL;
        back->textSize = 8;
        back->icon = KIT_ICON_BACK;
        back->flags |= KIT_NOFOCUS; /* Esc and pad B are the keys' way back */
    }
}

const SetRow *SetPage_RowOf(const SetPage *p, int id) {
    if (id == KIT_NONE) return NULL;
    for (int i = 0; i < p->rowCount; i++)
        for (int c = 0; c < p->rows[i].count; c++)
            if (p->rows[i].controls[c] == id) return &p->rows[i];
    return NULL;
}

int SetPage_FirstRowControl(const SetPage *p) {
    for (int i = 0; i < p->rowCount; i++)
        for (int c = 0; c < p->rows[i].count; c++)
            if (Kit_CanFocus(Kit_Get(&p->list, p->rows[i].controls[c]))) return p->rows[i].controls[c];
    return SID_TABS;
}

/* ---------------------------------------------------------- navigation */

static bool Usable(const SetPage *p, int id) { return Kit_CanFocus(Kit_Get(&p->list, id)); }

static int ColumnOf(const SetRow *r, int id) {
    for (int c = 0; c < r->count; c++) if (r->controls[c] == id) return c;
    return 0;
}

/* The control at col, or the nearest usable one beside it. */
static int PickInRow(const SetPage *p, const SetRow *r, int col) {
    if (r->count == 0) return KIT_NONE;
    if (col >= r->count) col = r->count - 1;
    for (int d = 0; d < r->count; d++) {
        if (col - d >= 0 && Usable(p, r->controls[col - d])) return r->controls[col - d];
        if (col + d < r->count && Usable(p, r->controls[col + d])) return r->controls[col + d];
    }
    return KIT_NONE;
}

static int NextRow(const SetPage *p, int i, int dir, int col) {
    for (int j = i + dir; j >= 0 && j < p->rowCount; j += dir) {
        int id = PickInRow(p, &p->rows[j], col);
        if (id != KIT_NONE) return id;
    }
    return KIT_NONE;
}

static bool SetFocus(KitState *k, int id) {
    if (id == KIT_NONE || id == k->focus) return false;
    k->focus = id;
    k->keyDown = KIT_NONE; /* a press does not follow the focus */
    return true;
}

static bool Vertical(KitState *k, const SetPage *p, int dir) {
    if (k->focus == SID_TABS) return dir > 0 && SetFocus(k, NextRow(p, -1, 1, 0));
    const SetRow *r = SetPage_RowOf(p, k->focus);
    if (!r) return SetFocus(k, SetPage_FirstRowControl(p));
    int id = NextRow(p, (int)(r - p->rows), dir, ColumnOf(r, k->focus));
    if (id == KIT_NONE && dir < 0) id = SID_TABS; /* above the first row: the tabs */
    return SetFocus(k, id);
}

static bool IsValue(const KitItem *it) {
    return it && (it->kind == KIT_TOGGLE || it->kind == KIT_STEPPER || it->kind == KIT_SLIDER || it->kind == KIT_TABS);
}

static bool Horizontal(KitState *k, const SetPage *p, int dir) {
    if (IsValue(Kit_Get(&p->list, k->focus))) return false; /* the kit changed its value */
    const SetRow *r = SetPage_RowOf(p, k->focus);
    if (!r) return false;
    for (int c = ColumnOf(r, k->focus) + dir; c >= 0 && c < r->count; c += dir) {
        if (!Usable(p, r->controls[c])) continue;
        return SetFocus(k, r->controls[c]);
    }
    return false;
}

const char *SetPage_SideKeys(const SetPage *p, int focus) {
    if (focus == SID_STRIP) return "try";
    const KitItem *it = Kit_Get(&p->list, focus);
    if (IsValue(it) && Kit_CanFocus(it)) return it->kind == KIT_TABS ? "tab" : "change";
    const SetRow *r = SetPage_RowOf(p, focus);
    int usable = 0;
    for (int c = 0; r && c < r->count; c++) usable += Usable(p, r->controls[c]);
    return usable > 1 ? "move" : NULL;
}

bool SetPage_Navigate(KitState *k, const SetPage *p, const Input *in) {
    if (Keymap_Pressed(KA_UI_UP, in)) return Vertical(k, p, -1);
    if (Keymap_Pressed(KA_UI_DOWN, in)) return Vertical(k, p, 1);
    if (k->focus == SID_STRIP) return false; /* Left and Right try the values there */
    if (Keymap_Pressed(KA_UI_LEFT, in)) return Horizontal(k, p, -1);
    if (Keymap_Pressed(KA_UI_RIGHT, in)) return Horizontal(k, p, 1);
    return false;
}

/* -------------------------------------------------------------- changes */

static SetEffect Changed(int id, bool config) {
    SetEffect e;
    memset(&e, 0, sizeof(e));
    e.kind = SE_CHANGED;
    e.id = id;
    e.config = config;
    e.prefs = !config;
    return e;
}

SetEffect SetPage_Apply(Settings *s, ArcadePrefs *pr, const KitEvent *ev) {
    SetEffect e;
    memset(&e, 0, sizeof(e));
    e.id = ev->id;
    if (ev->type == KIT_EV_NONE) return e;
    if (ev->id == SID_TABS) {
        if (ev->type != KIT_EV_CHANGE) return e;
        e.kind = SE_TAB;
        e.value = ev->value < 0 ? 0 : (ev->value >= SET_TABS ? SET_TABS - 1 : ev->value);
        return e;
    }
    if (ev->type == KIT_EV_PRESS) {
        if (SetPage_IsCap(ev->id)) {
            e.kind = SE_CAPTURE;
            e.captureAction = SetPage_CapAction(ev->id);
            e.captureColumn = SetPage_CapColumn(ev->id);
            return e;
        }
        if (ev->id == SID_RESET) { e.kind = SE_RESET_ASK; return e; }
        if (ev->id == SID_TEST) { e.kind = SE_TEST_SOUND; return e; }
        if (ev->id == SID_HOWTO) {
            /* set, never cleared here: main.c clears it as that launch opens How to play */
            s->showHowTo = true;
            e = Changed(ev->id, true);
            e.kind = SE_HOWTO_NEXT;
            return e;
        }
        if (ev->id >= SID_PRESET && ev->id < SID_PRESET + FEEL_PRESETS) {
            const FeelPreset *f = &kPresets[ev->id - SID_PRESET];
            s->repeatDelayMs = f->delayMs;
            s->repeatSpeedMs = f->speedMs;
            s->softDropSpeed = f->softSpeed;
            return Changed(ev->id, true);
        }
        return e;
    }
    int v = ev->value;
    switch (ev->id) {
        case SID_DELAY: s->repeatDelayMs = v * DELAY_STEP; return Changed(ev->id, true);
        case SID_SPEED: s->repeatSpeedMs = v * SPEED_STEP; return Changed(ev->id, true);
        case SID_SOFT: s->softDropSpeed = v >= 0 && v < SOFT_SPEEDS ? v : SOFT_NORMAL; return Changed(ev->id, true);
        case SID_GHOST: s->ghostPiece = kGhostOrder[v >= 0 && v < GHOST_STYLES ? v : 0]; return Changed(ev->id, true);
        case SID_GRID: s->grid = v >= 0 && v < GRID_STYLES ? v : GRID_ON; return Changed(ev->id, true);
        case SID_PATTERNS: s->blockPatterns = v != 0; e = Changed(ev->id, true); e.patterns = true; return e;
        case SID_CARD: s->hideControls = v != 0; return Changed(ev->id, true);
        case SID_PIECE: s->pieceSnap = v != 0; e = Changed(ev->id, true); e.pieceSnap = true; return e;
        case SID_MOTION: s->reducedMotion = v != 0; e = Changed(ev->id, true); e.motion = true; return e;
        case SID_SHAKE: pr->screenShake = v != 0; return Changed(ev->id, false);
        case SID_FLASH: pr->reducedFlashing = v != 0; return Changed(ev->id, false);
        case SID_FULL: pr->fullscreen = v != 0; return Changed(ev->id, false);
        case SID_EFFECTS:
            s->volumePercent = (v < 0 ? 0 : (v > 10 ? 10 : v)) * 10;
            e = Changed(ev->id, true);
            e.effectsVolume = true;
            e.value = v;
            return e;
        case SID_MUSIC:
            pr->musicVolume = (v < 0 ? 0 : (v > 10 ? 10 : v)) * 10;
            e = Changed(ev->id, false);
            e.musicVolume = true;
            return e;
        case SID_SOUND: s->audioEnabled = v != 0; e = Changed(ev->id, true); e.sound = true; return e;
        case SID_AUTOPAUSE: s->autoPause = v != 0; return Changed(ev->id, true);
        case SID_QUICKR: s->quickRestart = v != 0; return Changed(ev->id, true);
        case SID_READY: s->readyGo = v != 0; return Changed(ev->id, true);
        case SID_COUNTDOWN: s->resumeCountdown = v != 0; return Changed(ev->id, true);
        case SID_QUEUE: s->nextQueue = v < QUEUE_SHOWN_MIN ? QUEUE_SHOWN_MIN : (v > QUEUE_SHOWN_LIMIT ? QUEUE_SHOWN_LIMIT : v); return Changed(ev->id, true);
        default: return e;
    }
}

/* ------------------------------------------------------------- capture */

void Capture_Begin(Capture *c, int action, int column) {
    memset(c, 0, sizeof(*c));
    c->on = true;
    c->pad = column >= 2;
    c->action = action;
    c->column = column;
}

void Capture_End(Capture *c) { memset(c, 0, sizeof(*c)); }

bool Capture_Tick(Capture *c, float dt) {
    if (!c->on) return false;
    c->t += dt;
    return c->t >= CAPTURE_SECONDS;
}

float Capture_Left(const Capture *c) {
    if (!c->on) return 0.0f;
    float k = 1.0f - c->t / CAPTURE_SECONDS;
    return k < 0.0f ? 0.0f : (k > 1.0f ? 1.0f : k);
}

CaptureFeed Capture_FeedPad(Capture *c, Settings *s, const Input *in, bool keyPressed, BindResult *out) {
    if (!c->on || !c->pad) return CAPFEED_NONE;
    for (int b = 0; b < PB_COUNT; b++) {
        if (b == PB_MENU || !in->pad[b]) continue;
        BindResult r = Controls_BindPad(s, c->action, c->column - 2, b);
        if (out) *out = r;
        return CAPFEED_BOUND;
    }
    return keyPressed ? CAPFEED_KEY_WHILE_PAD : CAPFEED_NONE;
}

bool Capture_IsModifier(const char *key) {
    static const char *const kMods[] = {"LEFT_SHIFT", "RIGHT_SHIFT", "LEFT_CONTROL", "RIGHT_CONTROL", "LEFT_ALT", "RIGHT_ALT"};
    for (size_t i = 0; key && i < sizeof(kMods) / sizeof(kMods[0]); i++)
        if (strcmp(key, kMods[i]) == 0) return true;
    return false;
}

/* --------------------------------------------------------------- strip */

#define STRIP_IDLE_SHOW 1.0f /* left alone this long, it shows the values by itself */
#define STRIP_REST 0.5f      /* the show rests this long at a wall */

void FeelStrip_Reset(FeelStrip *s) {
    memset(s, 0, sizeof(*s));
    s->x = 0;
    s->dir = 1;
    s->idle = STRIP_IDLE_SHOW; /* the show starts at once */
}

void FeelStrip_Tick(FeelStrip *s, bool left, bool right, const PlayHandling *h, float dt) {
    int maxX = STRIP_CELLS - STRIP_PIECE_W;
    s->moved = s->repeat = false;
    if (left && right) left = right = false; /* together they cancel, as in a run */
    bool player = left || right;
    if (player) s->idle = 0.0f;
    else s->idle += dt;
    bool show = !player && s->idle >= STRIP_IDLE_SHOW;
    if (show) {
        if (s->rest > 0.0f) {
            s->rest -= dt;
            left = right = false;
        } else {
            left = s->dir < 0;
            right = s->dir > 0;
        }
    }
    bool leftWas = s->left.wasHeld, rightWas = s->right.wasHeld;
    bool doLeft = RepeatButton_Update(&s->left, left, dt, h->delay, h->speed);
    bool doRight = RepeatButton_Update(&s->right, right, dt, h->delay, h->speed);
    bool wall = h->speed <= 0.0f;
    if (doLeft && s->x > 0) {
        s->x--;
        s->moved = true;
        s->repeat = leftWas;
        if (leftWas && wall) s->x = 0;
    }
    if (doRight && s->x < maxX) {
        s->x++;
        s->moved = true;
        s->repeat = rightWas;
        if (rightWas && wall) s->x = maxX;
    }
    /* The show turns round at each wall, after a rest. */
    if (show && s->rest <= 0.0f && ((s->dir > 0 && s->x >= maxX) || (s->dir < 0 && s->x <= 0))) {
        s->dir = -s->dir;
        s->rest = STRIP_REST;
    }
}
