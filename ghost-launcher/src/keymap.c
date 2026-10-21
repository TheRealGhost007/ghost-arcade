#include "keymap.h"
#include <stdio.h>
#include <string.h>

#define K(act, key, mods, rep) {act, KB_KEY, key, mods, rep}
#define P(act, btn, rep) {act, KB_PAD, btn, 0, rep}
#define M(act, btn) {act, KB_MOUSE, btn, 0, false}

/* Order matters twice: an action's bindings are tried top to bottom (the
 * first that fired is the one a held press waits for), and the cards list
 * them in this order. */
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

    /* Up/Down and W/S still move along the row, so nobody relearns the old list. */
    K(KA_HALL_PREV, IK_LEFT, 0, true), K(KA_HALL_PREV, IK_UP, 0, true), K(KA_HALL_PREV, IK_W, 0, false),
    P(KA_HALL_PREV, PB_LEFT, true), P(KA_HALL_PREV, PB_UP, true),
    K(KA_HALL_NEXT, IK_RIGHT, 0, true), K(KA_HALL_NEXT, IK_DOWN, 0, true), K(KA_HALL_NEXT, IK_S, 0, false),
    P(KA_HALL_NEXT, PB_RIGHT, true), P(KA_HALL_NEXT, PB_DOWN, true),
    K(KA_HALL_PLAY, IK_ENTER, KM_NO_ALT, false), K(KA_HALL_PLAY, IK_SPACE, 0, false), P(KA_HALL_PLAY, PB_A, false),
    K(KA_HALL_DAILY, IK_D, KM_NO_SHIFT, false), P(KA_HALL_DAILY, PB_Y, false), /* Shift+D is the desk */
    K(KA_HALL_MENU, IK_ESCAPE, 0, false), P(KA_HALL_MENU, PB_MENU, false), P(KA_HALL_MENU, PB_B, false),
    K(KA_HALL_THIS_GAME, IK_I, 0, false), P(KA_HALL_THIS_GAME, PB_X, false),
    K(KA_HALL_JUMP_PREV, IK_PAGE_UP, 0, false), P(KA_HALL_JUMP_PREV, PB_LB, false),
    K(KA_HALL_JUMP_NEXT, IK_PAGE_DOWN, 0, false), P(KA_HALL_JUMP_NEXT, PB_RB, false),
    /* Keys only: the pad's buttons are all taken on the hall; Settings >
     * Machines has both for the pad. */
    K(KA_HALL_FAVOURITE, IK_F, 0, false),
    K(KA_HALL_ORDER, IK_T, 0, false),
    K(KA_ADMIN_ADD, IK_A, 0, false),
    K(KA_ADMIN_EDIT, IK_E, 0, false),
    K(KA_ADMIN_REMOVE, IK_DELETE, 0, false), /* never repeats: one machine per press */

    K(KA_OPEN_SETTINGS, IK_TAB, 0, false),
    K(KA_OPEN_NAME, IK_U, KM_NO_SHIFT, false),
    K(KA_OPEN_GAME_NAME, IK_U, KM_SHIFT, false),
    K(KA_OPEN_ONLINE, IK_O, 0, false),
    K(KA_OPEN_STATS, IK_L, 0, false), P(KA_OPEN_STATS, PB_VIEW, false),
    K(KA_OPEN_TROPHIES, IK_H, 0, false),
    K(KA_OPEN_UPDATES, IK_V, 0, false),
    K(KA_OPEN_DAILY, IK_D, KM_SHIFT, false),

    /* The page's d-pad and LB/RB are its buttons and tabs, so the triggers
     * turn its pages. X is the board, as the mockup has it; pad X opened the
     * page, and a page only answers a fresh press. */
    K(KA_PAGE_CLOSE, IK_I, 0, false),
    K(KA_PAGE_PREV, IK_PAGE_UP, 0, false), P(KA_PAGE_PREV, PB_LT, false),
    K(KA_PAGE_NEXT, IK_PAGE_DOWN, 0, false), P(KA_PAGE_NEXT, PB_RT, false),
    K(KA_PAGE_BOARD, IK_X, 0, false), P(KA_PAGE_BOARD, PB_X, false),

    K(KA_MENU_CLOSE, IK_ESCAPE, 0, false), P(KA_MENU_CLOSE, PB_B, false), P(KA_MENU_CLOSE, PB_MENU, false),
    M(KA_MENU_CLOSE, MB_BACK),
    /* View is the old Back/Select button, which the shared pad code read as R. */
    K(KA_CHECK_NOW, IK_R, 0, false), P(KA_CHECK_NOW, PB_VIEW, false),
    /* U updates (Updates has no name to type); pad X, as Y and View are taken nearby. */
    K(KA_UPDATE_NOW, IK_U, KM_NO_SHIFT, false), P(KA_UPDATE_NOW, PB_X, false),

    K(KA_ONLINE_FLIP, IK_A, 0, false), K(KA_ONLINE_FLIP, IK_D, 0, false),
    K(KA_ONLINE_YES, IK_Y, 0, false),
    K(KA_ONLINE_NO, IK_N, 0, false),

    K(KA_TEXT_LEFT, IK_LEFT, 0, true),
    K(KA_TEXT_RIGHT, IK_RIGHT, 0, true),
    K(KA_TEXT_HOME, IK_HOME, 0, false),
    K(KA_TEXT_END, IK_END, 0, false),
    K(KA_TEXT_BACKSPACE, IK_BACKSPACE, 0, true),
    K(KA_TEXT_DELETE, IK_DELETE, 0, true),
    K(KA_TEXT_PASTE, IK_V, KM_CTRL, false),
    K(KA_TEXT_SAVE, IK_ENTER, KM_NO_ALT, false),
    K(KA_TEXT_CANCEL, IK_ESCAPE, 0, false), P(KA_TEXT_CANCEL, PB_B, false), M(KA_TEXT_CANCEL, MB_BACK),
    K(KA_FORM_NEXT, IK_ENTER, KM_NO_ALT, false), P(KA_FORM_NEXT, PB_A, false),

    /* The pad only: the keyboard types straight into the name. */
    P(KA_BOARD_LEFT, PB_LEFT, true), P(KA_BOARD_RIGHT, PB_RIGHT, true),
    P(KA_BOARD_UP, PB_UP, true), P(KA_BOARD_DOWN, PB_DOWN, true),
    P(KA_BOARD_TYPE, PB_A, false),
    P(KA_BOARD_DELETE, PB_X, true), /* held, it keeps deleting, as Backspace does */
    P(KA_BOARD_SPACE, PB_Y, false),
    P(KA_BOARD_DONE, PB_MENU, false),
};
#define NBINDS ((int)(sizeof(kBinds) / sizeof(kBinds[0])))

int Keymap_Count(void) { return NBINDS; }

const KeyBind *Keymap_Get(int index) { return (index >= 0 && index < NBINDS) ? &kBinds[index] : NULL; }

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

int Keymap_Fired(KeyAction a, const Input *in) {
    for (int i = 0; i < NBINDS; i++) if (kBinds[i].action == a && Fired(&kBinds[i], in)) return i;
    return -1;
}

bool Keymap_Pressed(KeyAction a, const Input *in) { return Keymap_Fired(a, in) >= 0; }

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

/* ---------------------------------------------------------------- names */

static const char *KeyName(int k) {
    static const char *const kNames[] = {
        [IK_LEFT] = "Left", [IK_RIGHT] = "Right", [IK_UP] = "Up", [IK_DOWN] = "Down",
        [IK_ENTER] = "Enter", [IK_SPACE] = "Space", [IK_ESCAPE] = "Esc", [IK_BACKSPACE] = "Backspace",
        [IK_DELETE] = "Delete", [IK_TAB] = "Tab", [IK_HOME] = "Home", [IK_END] = "End",
        [IK_PAGE_UP] = "PgUp", [IK_PAGE_DOWN] = "PgDn", [IK_F11] = "F11",
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

void Keymap_Names(KeyAction a, KeyBindKind kind, char *out, size_t size) {
    if (size == 0) return;
    out[0] = '\0';
    size_t used = 0;
    for (int i = 0; i < NBINDS; i++) {
        if (kBinds[i].action != a || kBinds[i].kind != kind) continue;
        char name[32];
        Keymap_BindName(&kBinds[i], name, sizeof(name));
        int n = snprintf(out + used, size - used, "%s%s", used ? " / " : "", name);
        if (n < 0 || (size_t)n >= size - used) break; /* full: what fitted stays */
        used += (size_t)n;
    }
}

void Keymap_Hint(KeyAction a, InputDevice device, char *out, size_t size) {
    if (size == 0) return;
    out[0] = '\0';
    KeyBindKind want = device == DEV_PAD ? KB_PAD : KB_KEY;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < NBINDS; i++) {
            if (kBinds[i].action != a || kBinds[i].kind != want) continue;
            Keymap_BindName(&kBinds[i], out, size);
            return;
        }
        want = KB_KEY; /* the pad has no button for it: name the key */
    }
}

/* Caps are small: the long key names get their usual short forms. */
static const char *ShortKey(const char *name) {
    static const char *const kShort[][2] = {{"Delete", "Del"}, {"Backspace", "Bksp"}, {"Space", "Spc"}};
    for (size_t i = 0; i < sizeof(kShort) / sizeof(kShort[0]); i++) if (strcmp(name, kShort[i][0]) == 0) return kShort[i][1];
    return name;
}

void Keymap_Cap(KeyAction a, InputDevice device, char *out, size_t size, bool *pad) {
    if (size == 0) return;
    out[0] = '\0';
    *pad = false;
    KeyBindKind want = device == DEV_PAD ? KB_PAD : KB_KEY;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < NBINDS; i++) {
            if (kBinds[i].action != a || kBinds[i].kind != want) continue;
            char name[32];
            Keymap_BindName(&kBinds[i], name, sizeof(name));
            snprintf(out, size, "%s", want == KB_KEY ? ShortKey(name) : name);
            *pad = want == KB_PAD;
            return;
        }
        want = KB_KEY;
    }
}

/* ---------------------------------------------------------------- cards */

typedef struct {
    const char *what;
    KeyAction a, b; /* b joins a's keys on one line ("Up / Down"), or KA_NONE */
    bool admin;
} CardRow;

typedef struct {
    const char *title;
    const char *note;
    CardRow rows[KEYMAP_CARD_LINES];
} Card;

#define END {NULL, KA_NONE, KA_NONE, false}

static const Card kCards[] = {
    {"The hall", "Click a machine to bring it to the middle, and again to play. A click on the card opens its page.", {
        {"Previous machine", KA_HALL_PREV, KA_NONE, false},
        {"Next machine", KA_HALL_NEXT, KA_NONE, false},
        {"Five machines along", KA_HALL_JUMP_PREV, KA_HALL_JUMP_NEXT, false},
        {"Play, or install", KA_HALL_PLAY, KA_NONE, false},
        {"Daily challenge", KA_HALL_DAILY, KA_NONE, false},
        {"This game's page", KA_HALL_THIS_GAME, KA_NONE, false},
        {"Favourite this machine", KA_HALL_FAVOURITE, KA_NONE, false},
        {"Next row order", KA_HALL_ORDER, KA_NONE, false},
        {"Arcade menu", KA_HALL_MENU, KA_NONE, false},
        {"Add a machine", KA_ADMIN_ADD, KA_NONE, true},
        {"Edit this machine", KA_ADMIN_EDIT, KA_NONE, true},
        {"Remove this machine", KA_ADMIN_REMOVE, KA_NONE, true},
        END,
    }},
    {"Screens from the hall", "The Arcade menu reaches them too, and the same letters work there.", {
        {"Settings", KA_OPEN_SETTINGS, KA_NONE, false},
        {"Stats", KA_OPEN_STATS, KA_NONE, false},
        {"Trophies", KA_OPEN_TROPHIES, KA_NONE, false},
        {"Updates", KA_OPEN_UPDATES, KA_NONE, false},
        {"Your name", KA_OPEN_NAME, KA_NONE, false},
        {"Name for this game", KA_OPEN_GAME_NAME, KA_NONE, false},
        {"Online settings", KA_OPEN_ONLINE, KA_NONE, false},
        {"Daily challenge desk", KA_OPEN_DAILY, KA_NONE, false},
        END,
    }},
    {"Arcade menu", "The hall's letter keys work here too.", {
        {"Choose", KA_UI_UP, KA_UI_DOWN, false},
        {"Open", KA_UI_PRESS, KA_NONE, false},
        {"Close the menu", KA_MENU_CLOSE, KA_NONE, false},
        END,
    }},
    {"Settings", "Every change is saved at once.", {
        {"Choose a row", KA_UI_UP, KA_UI_DOWN, false},
        {"Change, or move along", KA_UI_LEFT, KA_UI_RIGHT, false},
        {"Press", KA_UI_PRESS, KA_NONE, false},
        {"Next tab", KA_UI_NEXT_TAB, KA_NONE, false},
        {"Previous tab", KA_UI_PREV_TAB, KA_NONE, false},
        {"Player tab", KA_OPEN_NAME, KA_NONE, false},
        {"This game's name", KA_OPEN_GAME_NAME, KA_NONE, false},
        {"Online tab", KA_OPEN_ONLINE, KA_NONE, false},
        {"Back", KA_BACK, KA_NONE, false},
        END,
    }},
    {"Typing a name", "Letters never open screens while you type.", {
        {"Move the cursor", KA_TEXT_LEFT, KA_TEXT_RIGHT, false},
        {"To the start or end", KA_TEXT_HOME, KA_TEXT_END, false},
        {"Delete", KA_TEXT_BACKSPACE, KA_TEXT_DELETE, false},
        {"Paste", KA_TEXT_PASTE, KA_NONE, false},
        {"Save", KA_TEXT_SAVE, KA_NONE, false},
        {"Put the old name back", KA_TEXT_CANCEL, KA_NONE, false},
        END,
    }},
    {"Name board", "Pad A on a name opens it. The mouse can click its letters too.", {
        {"Move along a row", KA_BOARD_LEFT, KA_BOARD_RIGHT, false},
        {"Move between rows", KA_BOARD_UP, KA_BOARD_DOWN, false},
        {"Type the letter", KA_BOARD_TYPE, KA_NONE, false},
        {"Delete", KA_BOARD_DELETE, KA_NONE, false},
        {"A space", KA_BOARD_SPACE, KA_NONE, false},
        {"Save the name", KA_BOARD_DONE, KA_NONE, false},
        {"Put the old name back", KA_TEXT_CANCEL, KA_NONE, false},
        END,
    }},
    {"Stats and trophies", "", {
        {"Scroll", KA_SCROLL_UP, KA_SCROLL_DOWN, false},
        {"A page at a time", KA_SCROLL_PAGE_UP, KA_SCROLL_PAGE_DOWN, false},
        {"To the top or end", KA_SCROLL_TOP, KA_SCROLL_END, false},
        {"Trophies: another game", KA_UI_LEFT, KA_UI_RIGHT, false},
        {"Close Stats", KA_OPEN_STATS, KA_NONE, false},
        {"Close Trophies", KA_OPEN_TROPHIES, KA_NONE, false},
        {"Back", KA_BACK, KA_NONE, false},
        END,
    }},
    {"Updates", "Update now runs install.sh --update for you.", {
        {"Update now, or retry", KA_UPDATE_NOW, KA_NONE, false},
        {"Check now", KA_CHECK_NOW, KA_NONE, false},
        {"Close Updates", KA_OPEN_UPDATES, KA_NONE, false},
        {"Back", KA_BACK, KA_NONE, false},
        END,
    }},
    {"Questions", "The safe answer is chosen first.", {
        {"Choose an answer", KA_UI_LEFT, KA_UI_RIGHT, false},
        {"Answer", KA_UI_PRESS, KA_NONE, false},
        {"The safe answer", KA_BACK, KA_NONE, false},
        END,
    }},
    {"Share scores?", "Asked once, the first time the arcade opens.", {
        {"Choose", KA_UI_LEFT, KA_UI_RIGHT, false},
        {"Switch answers", KA_ONLINE_FLIP, KA_NONE, false},
        {"Yes", KA_ONLINE_YES, KA_NONE, false},
        {"No", KA_ONLINE_NO, KA_NONE, false},
        {"Save the answer", KA_UI_PRESS, KA_NONE, false},
        {"No, keep it local", KA_BACK, KA_NONE, false},
        END,
    }},
    {"Everywhere", "A click on the X leaves at once.", {
        {"Back", KA_BACK, KA_NONE, false},
        {"Fullscreen on or off", KA_FULLSCREEN, KA_NONE, false},
        END,
    }},
    {"This game's page", "Pages open from the hall: a click on the card opens one too.", {
        {"Choose a button", KA_UI_UP, KA_UI_DOWN, false},
        {"Press it", KA_UI_PRESS, KA_NONE, false},
        {"Another tab", KA_UI_LEFT, KA_UI_RIGHT, false},
        {"Next tab", KA_UI_NEXT_TAB, KA_NONE, false},
        {"Previous tab", KA_UI_PREV_TAB, KA_NONE, false},
        {"Next board or list", KA_PAGE_BOARD, KA_NONE, false},
        {"Machine before, after", KA_PAGE_PREV, KA_PAGE_NEXT, false},
        {"Daily challenge desk", KA_HALL_DAILY, KA_NONE, false},
        {"Favourite", KA_HALL_FAVOURITE, KA_NONE, false},
        {"Name for this game", KA_OPEN_GAME_NAME, KA_NONE, false},
        {"Edit this machine", KA_ADMIN_EDIT, KA_NONE, true},
        {"Remove this machine", KA_ADMIN_REMOVE, KA_NONE, true},
        {"Back to the hall", KA_BACK, KA_PAGE_CLOSE, false},
        END,
    }},
    {"Daily challenge desk", "D on the hall plays the lit machine's daily at once.", {
        {"Choose a game", KA_UI_UP, KA_UI_DOWN, false},
        {"Play its daily", KA_UI_PRESS, KA_NONE, false},
        {"Close the desk", KA_OPEN_DAILY, KA_NONE, false},
        {"Back", KA_BACK, KA_NONE, false},
        END,
    }},
    {"Adding a machine", "Admin only. Settings > Machines has Add, Edit and Remove too.", {
        {"Next field, then Save", KA_FORM_NEXT, KA_NONE, true},
        {"Next field", KA_UI_NEXT_TAB, KA_NONE, true},
        {"Previous field", KA_UI_PREV_TAB, KA_NONE, true},
        {"Field above, below", KA_UI_UP, KA_UI_DOWN, true},
        {"Move the cursor", KA_TEXT_LEFT, KA_TEXT_RIGHT, true},
        {"Paste", KA_TEXT_PASTE, KA_NONE, true},
        {"Leave the form", KA_BACK, KA_NONE, true},
        END,
    }},
};
#define NCARDS ((int)(sizeof(kCards) / sizeof(kCards[0])))

int Keymap_CardCount(void) { return NCARDS; }
const char *Keymap_CardTitle(int card) { return (card >= 0 && card < NCARDS) ? kCards[card].title : ""; }
const char *Keymap_CardNote(int card) { return (card >= 0 && card < NCARDS) ? kCards[card].note : ""; }

/* a's names, then b's after " / ". */
static void Joined(KeyAction a, KeyAction b, KeyBindKind kind, char *out, size_t size) {
    Keymap_Names(a, kind, out, size);
    if (b == KA_NONE) return;
    char more[64];
    Keymap_Names(b, kind, more, sizeof(more));
    if (!more[0]) return;
    size_t used = strlen(out);
    snprintf(out + used, size - used, "%s%s", used ? " / " : "", more);
}

int Keymap_CardLines(int card, bool admin, KeymapLine *out, int max) {
    if (card < 0 || card >= NCARDS) return 0;
    int n = 0;
    for (int i = 0; i < KEYMAP_CARD_LINES && kCards[card].rows[i].what && n < max; i++) {
        const CardRow *r = &kCards[card].rows[i];
        if (r->admin && !admin) continue;
        KeymapLine *l = &out[n++];
        snprintf(l->what, sizeof(l->what), "%s", r->what);
        Joined(r->a, r->b, KB_KEY, l->keys, sizeof(l->keys));
        Joined(r->a, r->b, KB_PAD, l->pad, sizeof(l->pad));
        Joined(r->a, r->b, KB_MOUSE, l->mouse, sizeof(l->mouse));
    }
    return n;
}

int Keymap_CardCountFor(bool admin) {
    int n = 0;
    KeymapLine lines[KEYMAP_CARD_LINES];
    for (int c = 0; c < NCARDS; c++) if (Keymap_CardLines(c, admin, lines, KEYMAP_CARD_LINES) > 0) n = c + 1;
    return n;
}

bool Keymap_OnCard(KeyAction a) {
    for (int c = 0; c < NCARDS; c++)
        for (int i = 0; i < KEYMAP_CARD_LINES && kCards[c].rows[i].what; i++)
            if (kCards[c].rows[i].a == a || kCards[c].rows[i].b == a) return true;
    return false;
}
