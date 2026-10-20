#include "controls.h"
#include "uiinput.h"
#include <stdio.h>
#include <string.h>

static const char *const kLabels[ACT_COUNT] = {
    "Move left", "Move right", "Soft drop", "Hard drop", "Rotate right", "Rotate left", "Hold", "Pause", "Restart",
};

/* persist.c mends Pause's and Restart's pad slots on load by these indices. */
_Static_assert(ACT_PAUSE == 7 && ACT_RESTART == 8, "persist.c's MendMenuKeys names Pause and Restart by index");

const char *Controls_ActionLabel(int action) { return (action >= 0 && action < ACT_COUNT) ? kLabels[action] : ""; }

char *Controls_Key(Settings *s, int action, int slot) {
    if (slot < 0 || slot >= CONTROL_SLOTS) return NULL;
    switch (action) {
        case ACT_LEFT: return slot ? s->left2 : s->left;
        case ACT_RIGHT: return slot ? s->right2 : s->right;
        case ACT_SOFT: return slot ? s->softDrop2 : s->softDrop;
        case ACT_HARD: return slot ? s->hardDrop2 : s->hardDrop;
        case ACT_ROTATE: return slot ? s->rotate2 : s->rotate;
        case ACT_ROTATE_LEFT: return slot ? s->rotateLeft2 : s->rotateLeft;
        case ACT_HOLD: return slot ? s->hold2 : s->hold;
        case ACT_PAUSE: return slot ? s->quit : s->pause;
        case ACT_RESTART: return slot ? s->restart2 : s->restart;
        default: return NULL;
    }
}

const char *Controls_KeyConst(const Settings *s, int action, int slot) { return Controls_Key((Settings *)s, action, slot); }

int Controls_Pad(const Settings *s, int action, int slot) {
    if (action < 0 || action >= ACT_COUNT || slot < 0 || slot >= CONTROL_SLOTS) return PAD_NONE;
    int b = s->pad[action][slot];
    return (b >= 0 && b < PB_COUNT) ? b : PAD_NONE;
}

bool Controls_IsNone(const char *key) { return !key || !key[0] || strcmp(key, KEY_NONE_NAME) == 0; }

/* ------------------------------------------------------------- words */

typedef struct { const char *key, *name, *shortName; } KeyWords;
static const KeyWords kWords[] = {
    {"LEFT", "Left", "Left"}, {"RIGHT", "Right", "Right"}, {"UP", "Up", "Up"}, {"DOWN", "Down", "Down"},
    {"SPACE", "Space", "Space"}, {"ESCAPE", "Esc", "Esc"}, {"ENTER", "Enter", "Enter"}, {"TAB", "Tab", "Tab"},
    {"BACKSPACE", "Backspace", "Bksp"}, {"LEFT_SHIFT", "Left Shift", "LShift"}, {"RIGHT_SHIFT", "Right Shift", "RShift"},
    {"LEFT_CONTROL", "Left Ctrl", "LCtrl"}, {"RIGHT_CONTROL", "Right Ctrl", "RCtrl"}, {"LEFT_ALT", "Left Alt", "LAlt"},
    {"RIGHT_ALT", "Right Alt", "RAlt"}, {"COMMA", ",", ","}, {"PERIOD", ".", "."}, {"SEMICOLON", ";", ";"},
    {"APOSTROPHE", "'", "'"}, {"SLASH", "/", "/"},
};

static const KeyWords *Find(const char *key) {
    for (size_t i = 0; key && i < sizeof(kWords) / sizeof(kWords[0]); i++)
        if (strcmp(kWords[i].key, key) == 0) return &kWords[i];
    return NULL;
}

static bool OwnWord(const char *key) {
    return key && key[0] && !key[1] && ((key[0] >= 'A' && key[0] <= 'Z') || (key[0] >= '0' && key[0] <= '9'));
}

bool Controls_HasWords(const char *key) { return Find(key) != NULL || OwnWord(key); }

void Controls_Name(const char *key, char *out, size_t size) {
    const KeyWords *w = Find(key);
    snprintf(out, size, "%s", w ? w->name : (Controls_IsNone(key) ? "none" : key));
}

void Controls_ShortName(const char *key, char *out, size_t size) {
    const KeyWords *w = Find(key);
    snprintf(out, size, "%s", w ? w->shortName : (Controls_IsNone(key) ? "none" : key));
}

static const char *const kPadCaps[PB_COUNT] = {
    [PB_A] = "A", [PB_B] = "B", [PB_X] = "X", [PB_Y] = "Y", [PB_LB] = "LB", [PB_RB] = "RB", [PB_LT] = "LT", [PB_RT] = "RT",
    [PB_VIEW] = "View", [PB_MENU] = "Menu", [PB_LS] = "LS", [PB_RS] = "RS",
    [PB_UP] = "Up", [PB_DOWN] = "Down", [PB_LEFT] = "Left", [PB_RIGHT] = "Right",
};
static const char *const kPadWords[PB_COUNT] = {
    [PB_A] = "A", [PB_B] = "B", [PB_X] = "X", [PB_Y] = "Y", [PB_LB] = "LB", [PB_RB] = "RB", [PB_LT] = "LT", [PB_RT] = "RT",
    [PB_VIEW] = "View", [PB_MENU] = "Menu", [PB_LS] = "LS", [PB_RS] = "RS",
    [PB_UP] = "D-pad up", [PB_DOWN] = "D-pad down", [PB_LEFT] = "D-pad left", [PB_RIGHT] = "D-pad right",
};

const char *Controls_PadCap(int button) { return (button >= 0 && button < PB_COUNT) ? kPadCaps[button] : ""; }
const char *Controls_PadWords(int button) { return (button >= 0 && button < PB_COUNT) ? kPadWords[button] : "none"; }

/* ----------------------------------------------------- slots, keys or pad */

/* Keys and pad buttons follow the same rules, so both go through one set of
 * slots holding config.ini's names ("SPACE", "LB", "NONE"). */
typedef struct {
    Settings *s;
    bool pad;
} Slots;

static void Get(const Slots *z, int a, int slot, char *out) {
    if (z->pad) snprintf(out, KEYNAME_LEN, "%s", Persist_PadName(Controls_Pad(z->s, a, slot)));
    else {
        const char *k = Controls_KeyConst(z->s, a, slot);
        snprintf(out, KEYNAME_LEN, "%s", Controls_IsNone(k) ? KEY_NONE_NAME : k);
    }
}

static void Set(const Slots *z, int a, int slot, const char *v) {
    if (z->pad) z->s->pad[a][slot] = Persist_PadFromName(v);
    else snprintf(Controls_Key(z->s, a, slot), KEYNAME_LEN, "%s", Controls_IsNone(v) ? KEY_NONE_NAME : v);
}

static bool SlotNone(const Slots *z, int a, int slot) {
    char v[KEYNAME_LEN];
    Get(z, a, slot, v);
    return Controls_IsNone(v);
}

static void Words(const Slots *z, const char *v, char *out, size_t size) {
    if (z->pad) snprintf(out, size, "%s", Controls_PadWords(Persist_PadFromName(v)));
    else Controls_Name(v, out, size);
}

/* An action's first slot never sits empty while its second has a key. The
 * Pause row is left as it is: its two slots do different things. */
static void Normalize(const Slots *z, int a) {
    if (a == ACT_PAUSE || !SlotNone(z, a, 0) || SlotNone(z, a, 1)) return;
    char v[KEYNAME_LEN];
    Get(z, a, 1, v);
    Set(z, a, 0, v);
    Set(z, a, 1, KEY_NONE_NAME);
}

/* Enter, Space and the pad's A pick menu items (the pause menu reads Pause
 * and Restart before its buttons), so Pause and Restart never hold them:
 * not pressed on a cap (Controls_KeyAllowed, Controls_PadAllowed), not
 * bound outright, and not handed over by a swap. A is the pad's press
 * button to the menus exactly as Enter and Space are the keyboard's
 * (ghost-common/uikeymap.c maps all three to KA_UI_PRESS). */
static const char kMenuKeyNote[] = "Enter and Space pick menu items, so they cannot pause or restart.";
static const char kMenuPadNote[] = "A picks menu items, so it cannot pause or restart.";

static bool MenuAction(int action) { return action == ACT_PAUSE || action == ACT_RESTART; }

/* `key` is config.ini's name: a key's ("ENTER") or a pad button's ("A"). */
static bool MenuKey(bool pad, int action, const char *key) {
    if (!MenuAction(action)) return false;
    if (pad) return strcmp(key, Persist_PadName(PB_A)) == 0;
    return strcmp(key, "ENTER") == 0 || strcmp(key, "SPACE") == 0;
}

static BindResult Bind(Slots z, int action, int slot, const char *value) {
    BindResult r = {BIND_SET, -1, -1, false, ""};
    const char *noun = z.pad ? "button" : "key", *keep = z.pad ? "MENU" : "ESCAPE", *keepWords = z.pad ? "Menu" : "Esc";
    char cur[KEYNAME_LEN], v[48], other[48];
    Get(&z, action, slot, cur);
    Words(&z, value, v, sizeof(v));
    if (strcmp(cur, value) == 0) {
        r.kind = BIND_SAME;
        snprintf(r.note, sizeof(r.note), "%s already has %s.", kLabels[action], v);
        return r;
    }
    for (int a = 0; a < ACT_COUNT && r.otherAction < 0; a++)
        for (int s = 0; s < CONTROL_SLOTS; s++) {
            char there[KEYNAME_LEN];
            Get(&z, a, s, there);
            if ((a != action || s != slot) && strcmp(there, value) == 0) {
                r.otherAction = a;
                r.otherSlot = s;
                break;
            }
        }
    if (r.otherAction < 0) {
        Set(&z, action, slot, value);
        Normalize(&z, action);
        snprintf(r.note, sizeof(r.note), "%s%s %s is now %s.", kLabels[action], slot ? "'s second" : "'s", noun, v);
        return r;
    }
    int oa = r.otherAction, os = r.otherSlot;
    if (oa == action) {
        if (Controls_IsNone(cur)) { /* its own other key: nothing moves */
            r.kind = BIND_SAME;
            snprintf(r.note, sizeof(r.note), "%s already has %s.", kLabels[action], v);
            return r;
        }
        Set(&z, action, os, cur);
        Set(&z, action, slot, value);
        r.kind = BIND_SWAPPED;
        snprintf(r.note, sizeof(r.note), "%s's two %ss swapped places.", kLabels[action], noun);
        return r;
    }
    /* Another action has it: that one takes this slot's old one, but never
     * Esc or Menu (they always pause), never Enter, Space or pad A when it
     * is Pause or Restart (they pick menu items), and never nothing at all. */
    char give[KEYNAME_LEN], curWords[48];
    bool menuKey = false;
    snprintf(give, sizeof(give), "%s", cur);
    Words(&z, cur, curWords, sizeof(curWords));
    if (strcmp(give, keep) == 0) {
        snprintf(give, sizeof(give), KEY_NONE_NAME);
        r.withheld = true;
    } else if (MenuKey(z.pad, oa, give)) {
        snprintf(give, sizeof(give), KEY_NONE_NAME);
        r.withheld = menuKey = true;
    }
    if (Controls_IsNone(give) && SlotNone(&z, oa, 1 - os)) {
        r.kind = BIND_REFUSED;
        int n = snprintf(r.note, sizeof(r.note), "%s is the only %s %s has, ", v, noun, kLabels[oa]);
        if (n > 0 && (size_t)n < sizeof(r.note)) {
            if (menuKey) snprintf(r.note + n, sizeof(r.note) - (size_t)n, "and %s picks menu items, so it stays there.", curWords);
            else snprintf(r.note + n, sizeof(r.note) - (size_t)n, "so it stays there.");
        }
        return r;
    }
    Set(&z, oa, os, give);
    Set(&z, action, slot, value);
    Normalize(&z, oa);
    Normalize(&z, action);
    r.kind = BIND_SWAPPED;
    int n = snprintf(r.note, sizeof(r.note), "%s moved from %s to %s.", v, kLabels[oa], kLabels[action]);
    if (n > 0 && (size_t)n < sizeof(r.note)) {
        if (!Controls_IsNone(give)) {
            Words(&z, give, other, sizeof(other));
            snprintf(r.note + n, sizeof(r.note) - (size_t)n, " %s took %s.", kLabels[oa], other);
        } else if (menuKey) {
            snprintf(r.note + n, sizeof(r.note) - (size_t)n, " %s picks menu items, so %s did not take it.", curWords, kLabels[oa]);
        } else if (r.withheld) {
            snprintf(r.note + n, sizeof(r.note) - (size_t)n, " %s still pauses.", keepWords);
        }
    }
    return r;
}

BindResult Controls_BindKey(Settings *s, int action, int slot, const char *key) {
    BindResult none = {BIND_REFUSED, -1, -1, false, ""};
    if (action < 0 || action >= ACT_COUNT || slot < 0 || slot >= CONTROL_SLOTS || Controls_IsNone(key) || strlen(key) >= KEYNAME_LEN) return none;
    Slots z = {s, false};
    if (MenuKey(false, action, key)) { /* the model's side of Controls_KeyAllowed's rule */
        snprintf(none.note, sizeof(none.note), "%s", kMenuKeyNote);
        return none;
    }
    return Bind(z, action, slot, key);
}

BindResult Controls_BindPad(Settings *s, int action, int slot, int button) {
    BindResult none = {BIND_REFUSED, -1, -1, false, ""};
    if (action < 0 || action >= ACT_COUNT || slot < 0 || slot >= CONTROL_SLOTS || button < 0 || button >= PB_COUNT) return none;
    if (MenuKey(true, action, Persist_PadName(button))) { /* the model's side of Controls_PadAllowed's rule */
        snprintf(none.note, sizeof(none.note), "%s", kMenuPadNote);
        return none;
    }
    return Bind((Slots){s, true}, action, slot, Persist_PadName(button));
}

bool Controls_ClearKey(Settings *s, int action, int slot) {
    char *k = Controls_Key(s, action, slot);
    /* never an action's last key (Pause's first can be empty) */
    if (!k || slot != 1 || Controls_IsNone(k) || Controls_IsNone(Controls_KeyConst(s, action, 0))) return false;
    snprintf(k, KEYNAME_LEN, KEY_NONE_NAME);
    return true;
}

bool Controls_ClearPad(Settings *s, int action, int slot) {
    if (action < 0 || action >= ACT_COUNT || slot != 1 || Controls_Pad(s, action, slot) == PAD_NONE || Controls_Pad(s, action, 0) == PAD_NONE) return false;
    s->pad[action][slot] = PAD_NONE;
    return true;
}

void Controls_Reset(Settings *s) {
    Settings d;
    Settings_Default(&d);
    for (int a = 0; a < ACT_COUNT; a++)
        for (int slot = 0; slot < CONTROL_SLOTS; slot++) snprintf(Controls_Key(s, a, slot), KEYNAME_LEN, "%s", Controls_KeyConst(&d, a, slot));
    memcpy(s->pad, d.pad, sizeof(s->pad));
}

bool Controls_AreDefault(const Settings *s) {
    Settings d;
    Settings_Default(&d);
    for (int a = 0; a < ACT_COUNT; a++)
        for (int slot = 0; slot < CONTROL_SLOTS; slot++) {
            if (strcmp(Controls_KeyConst(s, a, slot), Controls_KeyConst(&d, a, slot)) != 0) return false;
            if (Controls_Pad(s, a, slot) != Controls_Pad(&d, a, slot)) return false;
        }
    return true;
}

bool Controls_KeyAllowed(int action, const char *key, char *why, size_t size) {
    if (Controls_IsNone(key) || !Controls_HasWords(key)) {
        snprintf(why, size, "Blockfall cannot use that key. Pick another.");
        return false;
    }
    if (strcmp(key, "M") == 0) {
        snprintf(why, size, "M mutes the sound in a run. Pick another key.");
        return false;
    }
    /* The pause menu reads Pause and Restart before its buttons, so Enter
     * bound to either would resume or ask to restart instead of picking the
     * focused item. Space is the same key to the menus. (The keypad's Enter
     * has no words, so it is turned away above.) Space stays the hard drop,
     * and any other action may have either key. Bind keeps the same rule
     * for what a swap hands over (MenuKey), and Controls_PadAllowed for the
     * pad's A. */
    if (MenuKey(false, action, key)) {
        snprintf(why, size, "%s", kMenuKeyNote);
        return false;
    }
    if (why && size) why[0] = '\0';
    return true;
}

bool Controls_PadAllowed(int action, int button, char *why, size_t size) {
    if (button < 0 || button >= PB_COUNT) {
        snprintf(why, size, "Blockfall cannot use that button. Pick another.");
        return false;
    }
    /* A is the pad's Enter: in the pause menu it would resume or ask to
     * restart instead of picking the focused item. It stays the hard drop,
     * and any other action may have it. */
    if (MenuKey(true, action, Persist_PadName(button))) {
        snprintf(why, size, "%s", kMenuPadNote);
        return false;
    }
    if (why && size) why[0] = '\0';
    return true;
}

bool Controls_AllDifferent(const Settings *s) {
    for (int a = 0; a < ACT_COUNT * CONTROL_SLOTS; a++)
        for (int b = a + 1; b < ACT_COUNT * CONTROL_SLOTS; b++) {
            const char *ka = Controls_KeyConst(s, a / 2, a % 2), *kb = Controls_KeyConst(s, b / 2, b % 2);
            if (!Controls_IsNone(ka) && strcmp(ka, kb) == 0) return false;
            int pa = Controls_Pad(s, a / 2, a % 2), pb = Controls_Pad(s, b / 2, b % 2);
            if (pa != PAD_NONE && pa == pb) return false;
        }
    return true;
}

bool Controls_EscTaken(const Settings *s) {
    for (int a = 0; a < ACT_COUNT; a++)
        for (int slot = 0; slot < CONTROL_SLOTS; slot++)
            if (!(a == ACT_PAUSE && slot == 1) && strcmp(Controls_KeyConst(s, a, slot), "ESCAPE") == 0) return true;
    return false;
}

bool Controls_MenuTaken(const Settings *s) {
    for (int a = 0; a < ACT_COUNT; a++)
        for (int slot = 0; slot < CONTROL_SLOTS; slot++)
            if (!(a == ACT_PAUSE && slot == 0) && Controls_Pad(s, a, slot) == PB_MENU) return true;
    return false;
}

/* ------------------------------------------------------- what screens say */

/* An action's keys for the card: the first it has (its second if the first
 * is none), "-" when it has none. */
static void FirstShort(const Settings *s, int a, char *out, size_t size) {
    const char *k = Controls_KeyConst(s, a, 0);
    if (Controls_IsNone(k)) k = Controls_KeyConst(s, a, 1);
    if (Controls_IsNone(k)) snprintf(out, size, "-");
    else Controls_ShortName(k, out, size);
}

static void FirstName(const Settings *s, int a, char *out, size_t size) {
    const char *k = Controls_KeyConst(s, a, 0);
    if (Controls_IsNone(k)) k = Controls_KeyConst(s, a, 1);
    if (Controls_IsNone(k)) snprintf(out, size, "none");
    else Controls_Name(k, out, size);
}

static void Two(const char *a, const char *b, bool shortNames, const char *sep, char *out, size_t size) {
    char x[24], y[24];
    if (shortNames) { Controls_ShortName(a, x, sizeof(x)); Controls_ShortName(b, y, sizeof(y)); }
    else { Controls_Name(a, x, sizeof(x)); Controls_Name(b, y, sizeof(y)); }
    snprintf(out, size, "%s%s%s", x, sep, y);
}

static void Append(char *out, size_t size, const char *word) {
    if (!word || !word[0]) return;
    size_t used = strlen(out);
    snprintf(out + used, size - used, "%s%s", used ? " " : "", word);
}

/* The pad's caps for an action: both its buttons ("Y LB"). */
static void PadCaps(const Settings *s, int a, char *out, size_t size) {
    out[0] = '\0';
    for (int slot = 0; slot < CONTROL_SLOTS; slot++) Append(out, size, Controls_PadCap(Controls_Pad(s, a, slot)));
}

void Controls_Words(const Settings *s, ControlWords *out) {
    memset(out, 0, sizeof(*out));
    char l[24], r[24];
    FirstShort(s, ACT_LEFT, l, sizeof(l));
    FirstShort(s, ACT_RIGHT, r, sizeof(r));
    snprintf(out->move, sizeof(out->move), "%s %s", l, r);
    FirstShort(s, ACT_SOFT, out->softDrop, sizeof(out->softDrop));
    FirstShort(s, ACT_ROTATE, out->rotate, sizeof(out->rotate));
    FirstShort(s, ACT_ROTATE_LEFT, out->rotateLeft, sizeof(out->rotateLeft));
    FirstShort(s, ACT_HARD, out->hardDrop, sizeof(out->hardDrop));
    FirstShort(s, ACT_HOLD, out->hold, sizeof(out->hold));
    FirstShort(s, ACT_RESTART, out->restart, sizeof(out->restart));

    /* Esc always works as Back; a different Back key works too. Unless a
     * config.ini from before gave Esc to another action: then only Back's
     * own key leaves. */
    bool escTaken = Controls_EscTaken(s);
    if (strcmp(s->quit, "ESCAPE") == 0 || Controls_IsNone(s->quit)) snprintf(out->back, sizeof(out->back), "Esc");
    else if (escTaken) Controls_ShortName(s->quit, out->back, sizeof(out->back));
    else Two("ESCAPE", s->quit, true, " ", out->back, sizeof(out->back));
    /* The card's Pause row: the pause key, then Esc (or Back's own key when
     * Esc is another action's). */
    char back[24];
    if (escTaken && !Controls_IsNone(s->quit)) Controls_ShortName(s->quit, back, sizeof(back));
    else snprintf(back, sizeof(back), "Esc");
    if (strcmp(s->pause, "ESCAPE") == 0) snprintf(out->pause, sizeof(out->pause), "Esc %s", back);
    else if (Controls_IsNone(s->pause)) snprintf(out->pause, sizeof(out->pause), "%s", back);
    else {
        char p[24];
        Controls_ShortName(s->pause, p, sizeof(p));
        snprintf(out->pause, sizeof(out->pause), "%s %s", p, back);
        /* The narrowest card row holds ten characters of keys ("LShift
         * Esc"): two long names show only the first. */
        if (strlen(out->pause) > 10) snprintf(out->pause, sizeof(out->pause), "%s", p);
    }

    Controls_Name(Controls_IsNone(s->pause) ? "ESCAPE" : s->pause, out->pauseName, sizeof(out->pauseName));
    FirstName(s, ACT_RESTART, out->restartName, sizeof(out->restartName));
    /* Esc leaves, unless another action has it: then Back's own key does. */
    if (escTaken && !Controls_IsNone(s->quit)) Controls_Name(s->quit, out->backName, sizeof(out->backName));
    else snprintf(out->backName, sizeof(out->backName), "Esc");
    char ln[24], rn[24];
    FirstName(s, ACT_LEFT, ln, sizeof(ln));
    FirstName(s, ACT_RIGHT, rn, sizeof(rn));
    snprintf(out->moveName, sizeof(out->moveName), "%s / %s", ln, rn);
    FirstName(s, ACT_SOFT, out->softName, sizeof(out->softName));
    FirstName(s, ACT_ROTATE, out->rotateName, sizeof(out->rotateName));
    FirstName(s, ACT_ROTATE_LEFT, out->rotateLeftName, sizeof(out->rotateLeftName));
    FirstName(s, ACT_HARD, out->hardName, sizeof(out->hardName));
    FirstName(s, ACT_HOLD, out->holdName, sizeof(out->holdName));

    /* The pad: your buttons. Moving is the first button of each way. */
    snprintf(out->padMove, sizeof(out->padMove), "%s", Controls_PadCap(Controls_Pad(s, ACT_LEFT, 0)));
    Append(out->padMove, sizeof(out->padMove), Controls_PadCap(Controls_Pad(s, ACT_RIGHT, 0)));
    PadCaps(s, ACT_SOFT, out->padSoft, sizeof(out->padSoft));
    PadCaps(s, ACT_ROTATE, out->padRotate, sizeof(out->padRotate));
    PadCaps(s, ACT_ROTATE_LEFT, out->padRotateLeft, sizeof(out->padRotateLeft));
    PadCaps(s, ACT_HARD, out->padHard, sizeof(out->padHard));
    PadCaps(s, ACT_HOLD, out->padHold, sizeof(out->padHold));
    PadCaps(s, ACT_RESTART, out->padRestart, sizeof(out->padRestart));
    /* Menu always pauses (unless another action has it), so it leads. */
    bool menuInRow = Controls_Pad(s, ACT_PAUSE, 0) == PB_MENU || Controls_Pad(s, ACT_PAUSE, 1) == PB_MENU;
    char caps[16];
    PadCaps(s, ACT_PAUSE, caps, sizeof(caps));
    out->padPause[0] = '\0';
    if (!menuInRow && !Controls_MenuTaken(s)) Append(out->padPause, sizeof(out->padPause), "Menu");
    Append(out->padPause, sizeof(out->padPause), caps);
}
