#include "uikeymap.h"
#include <stdio.h>
#include <string.h>

#define K(act, key, mods, rep) {act, KB_KEY, key, mods, rep}
#define P(act, btn, rep) {act, KB_PAD, btn, 0, rep}
#define M(act, btn) {act, KB_MOUSE, btn, 0, false}

/* Order matters twice: an action's bindings are tried top to bottom (the
 * first that fired is the one a held press waits for), and hints name the
 * first one of the device. */
static const KeyBind kBinds[] = {
    K(KA_BACK, IK_ESCAPE, 0, false), P(KA_BACK, PB_B, false), M(KA_BACK, MB_BACK),
    K(KA_FULLSCREEN, IK_F11, 0, false), K(KA_FULLSCREEN, IK_ENTER, KM_ALT, false),

    K(KA_UI_LEFT, IK_LEFT, 0, true), P(KA_UI_LEFT, PB_LEFT, true),
    K(KA_UI_RIGHT, IK_RIGHT, 0, true), P(KA_UI_RIGHT, PB_RIGHT, true),
    K(KA_UI_UP, IK_UP, 0, true), P(KA_UI_UP, PB_UP, true),
    K(KA_UI_DOWN, IK_DOWN, 0, true), P(KA_UI_DOWN, PB_DOWN, true),
    K(KA_UI_PRESS, IK_ENTER, KM_NO_ALT, false), K(KA_UI_PRESS, IK_SPACE, 0, false), P(KA_UI_PRESS, PB_A, false),
    K(KA_UI_PREV_TAB, IK_TAB, KM_SHIFT, false), P(KA_UI_PREV_TAB, PB_LB, false),
    K(KA_UI_NEXT_TAB, IK_TAB, KM_NO_SHIFT, false), P(KA_UI_NEXT_TAB, PB_RB, false),

    K(KA_SCROLL_UP, IK_UP, 0, true), P(KA_SCROLL_UP, PB_UP, true),
    K(KA_SCROLL_DOWN, IK_DOWN, 0, true), P(KA_SCROLL_DOWN, PB_DOWN, true),
    K(KA_SCROLL_PAGE_UP, IK_PAGE_UP, 0, false), P(KA_SCROLL_PAGE_UP, PB_LB, false),
    K(KA_SCROLL_PAGE_DOWN, IK_PAGE_DOWN, 0, false), P(KA_SCROLL_PAGE_DOWN, PB_RB, false),
    K(KA_SCROLL_TOP, IK_HOME, 0, false),
    K(KA_SCROLL_END, IK_END, 0, false),

    K(KA_TEXT_LEFT, IK_LEFT, 0, true),
    K(KA_TEXT_RIGHT, IK_RIGHT, 0, true),
    K(KA_TEXT_HOME, IK_HOME, 0, false),
    K(KA_TEXT_END, IK_END, 0, false),
    K(KA_TEXT_BACKSPACE, IK_BACKSPACE, 0, true),
    K(KA_TEXT_DELETE, IK_DELETE, 0, true),
    K(KA_TEXT_PASTE, IK_V, KM_CTRL, false),
    K(KA_TEXT_SAVE, IK_ENTER, KM_NO_ALT, false),
    K(KA_TEXT_CANCEL, IK_ESCAPE, 0, false), P(KA_TEXT_CANCEL, PB_B, false), M(KA_TEXT_CANCEL, MB_BACK),
};
#define NBUILTIN ((int)(sizeof(kBinds) / sizeof(kBinds[0])))

static const KeyBind *sGame = NULL;
static int sGameCount = 0;

void Keymap_SetGameBinds(const KeyBind *binds, int count) {
    sGame = binds;
    sGameCount = binds && count > 0 ? count : 0;
}

int Keymap_BuiltinCount(void) { return NBUILTIN; }

int Keymap_Count(void) { return NBUILTIN + sGameCount; }

const KeyBind *Keymap_Get(int index) {
    if (index >= 0 && index < NBUILTIN) return &kBinds[index];
    if (index >= NBUILTIN && index < NBUILTIN + sGameCount) return &sGame[index - NBUILTIN];
    return NULL;
}

static bool ModsMatch(unsigned mods, const Input *in) {
    if ((mods & KM_SHIFT) && !in->shift) return false;
    if ((mods & KM_NO_SHIFT) && in->shift) return false;
    if ((mods & KM_CTRL) && !in->ctrl) return false;
    if ((mods & KM_ALT) && !in->alt) return false;
    if ((mods & KM_NO_ALT) && in->alt) return false;
    return true;
}

static bool Fired(const KeyBind *b, const Input *in) {
    switch (b->kind) {
        case KB_KEY:
            if (b->code < 0 || b->code >= IK_COUNT) return false;
            return (b->repeat ? in->keyRepeat[b->code] : in->key[b->code]) && ModsMatch(b->mods, in);
        case KB_PAD:
            if (b->code < 0 || b->code >= PB_COUNT) return false;
            return b->repeat ? in->padRepeat[b->code] : in->pad[b->code];
        case KB_MOUSE: return b->code >= 0 && b->code < MB_COUNT && in->mousePressed[b->code];
    }
    return false;
}

int Keymap_Fired(int action, const Input *in) {
    int n = Keymap_Count();
    for (int i = 0; i < n; i++) {
        const KeyBind *b = Keymap_Get(i);
        if (b->action == action && Fired(b, in)) return i;
    }
    return -1;
}

bool Keymap_Pressed(int action, const Input *in) { return Keymap_Fired(action, in) >= 0; }

bool Keymap_PressedEdge(int action, const Input *in) {
    int n = Keymap_Count();
    for (int i = 0; i < n; i++) {
        KeyBind b = *Keymap_Get(i);
        if (b.action != action) continue;
        b.repeat = false;
        if (Fired(&b, in)) return true;
    }
    return false;
}

bool Keymap_Held(int index, const Input *in) {
    const KeyBind *b = Keymap_Get(index);
    if (!b) return false;
    switch (b->kind) {
        case KB_KEY: return b->code >= 0 && b->code < IK_COUNT && in->keyDown[b->code];
        case KB_PAD: return b->code >= 0 && b->code < PB_COUNT && in->padDown[b->code];
        case KB_MOUSE: return b->code >= 0 && b->code < MB_COUNT && in->mouseDown[b->code];
    }
    return false;
}

bool Keymap_AnyHeld(int action, const Input *in) {
    int n = Keymap_Count();
    for (int i = 0; i < n; i++) {
        const KeyBind *b = Keymap_Get(i);
        if (b->action == action && Keymap_Held(i, in) && (b->kind != KB_KEY || ModsMatch(b->mods, in))) return true;
    }
    return false;
}

/* ---------------------------------------------------------------- names */

static const char *KeyName(int k) {
    static const char *const kNames[] = {
        [IK_LEFT] = "Left", [IK_RIGHT] = "Right", [IK_UP] = "Up", [IK_DOWN] = "Down",
        [IK_ENTER] = "Enter", [IK_SPACE] = "Space", [IK_ESCAPE] = "Esc", [IK_BACKSPACE] = "Backspace",
        [IK_DELETE] = "Delete", [IK_TAB] = "Tab", [IK_HOME] = "Home", [IK_END] = "End",
        [IK_PAGE_UP] = "PgUp", [IK_PAGE_DOWN] = "PgDn", [IK_F11] = "F11",
        [IK_LEFT_BRACKET] = "[", [IK_RIGHT_BRACKET] = "]",
    };
    static char letter[2];
    if (k >= IK_A && k <= IK_Z) {
        letter[0] = (char)('A' + (k - IK_A));
        letter[1] = '\0';
        return letter;
    }
    if (k >= 0 && k < (int)(sizeof(kNames) / sizeof(kNames[0])) && kNames[k]) return kNames[k];
    return "?";
}

static const char *PadName(int b) {
    static const char *const kNames[PB_COUNT] = {
        [PB_A] = "A", [PB_B] = "B", [PB_X] = "X", [PB_Y] = "Y", [PB_LB] = "LB", [PB_RB] = "RB",
        [PB_LT] = "LT", [PB_RT] = "RT", [PB_VIEW] = "View", [PB_MENU] = "Menu", [PB_LS] = "LS", [PB_RS] = "RS",
        [PB_UP] = "D-pad up", [PB_DOWN] = "D-pad down", [PB_LEFT] = "D-pad left", [PB_RIGHT] = "D-pad right",
    };
    return (b >= 0 && b < PB_COUNT && kNames[b]) ? kNames[b] : "?";
}

static const char *MouseName(int b) {
    static const char *const kNames[MB_COUNT] = {
        [MB_LEFT] = "Left button", [MB_RIGHT] = "Right button", [MB_MIDDLE] = "Middle button",
        [MB_BACK] = "Back button", [MB_FORWARD] = "Forward button",
    };
    return (b >= 0 && b < MB_COUNT) ? kNames[b] : "?";
}

void Keymap_BindName(const KeyBind *b, char *out, size_t size) {
    if (!b || size == 0) return;
    switch (b->kind) {
        case KB_KEY:
            snprintf(out, size, "%s%s%s%s", (b->mods & KM_CTRL) ? "Ctrl+" : "", (b->mods & KM_ALT) ? "Alt+" : "",
                     (b->mods & KM_SHIFT) ? "Shift+" : "", KeyName(b->code));
            break;
        case KB_PAD: snprintf(out, size, "%s", PadName(b->code)); break;
        case KB_MOUSE: snprintf(out, size, "%s", MouseName(b->code)); break;
    }
}

void Keymap_Names(int action, KeyBindKind kind, char *out, size_t size) {
    if (size == 0) return;
    out[0] = '\0';
    size_t used = 0;
    int n = Keymap_Count();
    for (int i = 0; i < n; i++) {
        const KeyBind *b = Keymap_Get(i);
        if (b->action != action || b->kind != kind) continue;
        char name[32];
        Keymap_BindName(b, name, sizeof(name));
        int w = snprintf(out + used, size - used, "%s%s", used ? " / " : "", name);
        if (w < 0 || (size_t)w >= size - used) break; /* full: what fitted stays */
        used += (size_t)w;
    }
}

static const KeyBind *FirstOf(int action, KeyBindKind want) {
    int n = Keymap_Count();
    for (int i = 0; i < n; i++) {
        const KeyBind *b = Keymap_Get(i);
        if (b->action == action && b->kind == want) return b;
    }
    return NULL;
}

void Keymap_Hint(int action, InputDevice device, char *out, size_t size) {
    if (size == 0) return;
    out[0] = '\0';
    const KeyBind *b = FirstOf(action, device == DEV_PAD ? KB_PAD : KB_KEY);
    if (!b) b = FirstOf(action, KB_KEY); /* the pad has no button for it: name the key */
    if (b) Keymap_BindName(b, out, size);
}

/* Caps are small: long key names get their usual short forms, and the
 * d-pad is drawn as its direction. */
static const char *ShortName(const char *name) {
    static const char *const kShort[][2] = {
        {"Delete", "Del"}, {"Backspace", "Bksp"}, {"Space", "Spc"},
        {"D-pad up", "Up"}, {"D-pad down", "Down"}, {"D-pad left", "Left"}, {"D-pad right", "Right"},
    };
    for (size_t i = 0; i < sizeof(kShort) / sizeof(kShort[0]); i++) if (strcmp(name, kShort[i][0]) == 0) return kShort[i][1];
    return name;
}

void Keymap_Cap(int action, InputDevice device, char *out, size_t size, bool *pad) {
    if (size == 0) return;
    out[0] = '\0';
    *pad = false;
    KeyBindKind want = device == DEV_PAD ? KB_PAD : KB_KEY;
    const KeyBind *b = FirstOf(action, want);
    if (!b) { b = FirstOf(action, KB_KEY); want = KB_KEY; }
    if (!b) return;
    char name[32];
    Keymap_BindName(b, name, sizeof(name));
    snprintf(out, size, "%s", ShortName(name));
    *pad = want == KB_PAD;
}
