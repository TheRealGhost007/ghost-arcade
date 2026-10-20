#ifndef KEYMAP_H
#define KEYMAP_H

#include <stdbool.h>
#include <stddef.h>
#include "input.h"

/* Which key, pad button or mouse button does what: one table for the whole
 * launcher. The input code never names a key itself; it asks
 * Keymap_Pressed(action), and the Controls tab in Settings prints the same
 * table as reference cards, so the cards cannot drift from what the keys
 * really do. No raylib. */

typedef enum {
    /* everywhere */
    KA_BACK,       /* one level back */
    KA_FULLSCREEN, /* winscale.c acts on it; the launcher re-reads prefs first */

    /* any list of buttons: menus, Settings, the "are you sure?" box */
    KA_UI_LEFT,
    KA_UI_RIGHT,
    KA_UI_UP,
    KA_UI_DOWN,
    KA_UI_PRESS,
    KA_UI_PREV_TAB,
    KA_UI_NEXT_TAB,

    /* a list longer than the screen: Stats, Trophies */
    KA_SCROLL_UP,
    KA_SCROLL_DOWN,
    KA_SCROLL_PAGE_UP,
    KA_SCROLL_PAGE_DOWN,
    KA_SCROLL_TOP,
    KA_SCROLL_END,

    /* the hall */
    KA_HALL_PREV,
    KA_HALL_NEXT,
    KA_HALL_PLAY,
    KA_HALL_DAILY,
    KA_HALL_MENU,
    KA_HALL_THIS_GAME, /* this machine's page */
    KA_HALL_JUMP_PREV, /* five machines along */
    KA_HALL_JUMP_NEXT,
    KA_HALL_FAVOURITE, /* a gold star: favourites come first on the row */
    KA_HALL_ORDER,     /* the next row order: yours, recently played, most played, A-Z */
    KA_ADMIN_ADD,
    KA_ADMIN_EDIT,
    KA_ADMIN_REMOVE,

    /* the screens the letters open, from the hall and the Arcade menu; the
     * letter that opened Stats, Trophies or Updates also closes it */
    KA_OPEN_SETTINGS,
    KA_OPEN_NAME,
    KA_OPEN_GAME_NAME,
    KA_OPEN_ONLINE,
    KA_OPEN_STATS,
    KA_OPEN_TROPHIES,
    KA_OPEN_UPDATES,
    KA_OPEN_DAILY, /* the Daily challenge desk (D alone still plays the lit machine's daily) */

    /* a machine's page */
    KA_PAGE_CLOSE, /* the key that opened it (I) closes it */
    KA_PAGE_PREV,  /* the machine before it on the row */
    KA_PAGE_NEXT,
    KA_PAGE_BOARD, /* Scores: the next board */

    KA_MENU_CLOSE,
    KA_CHECK_NOW,

    /* the first-run "Share scores online?" question */
    KA_ONLINE_FLIP,
    KA_ONLINE_YES,
    KA_ONLINE_NO,

    /* typing in a text field */
    KA_TEXT_LEFT,
    KA_TEXT_RIGHT,
    KA_TEXT_HOME,
    KA_TEXT_END,
    KA_TEXT_BACKSPACE,
    KA_TEXT_DELETE,
    KA_TEXT_PASTE,
    KA_TEXT_SAVE,
    KA_TEXT_CANCEL,
    KA_FORM_NEXT, /* the admin game form: the next field */

    /* the pad's name board: a letter grid under the name being typed */
    KA_BOARD_LEFT,
    KA_BOARD_RIGHT,
    KA_BOARD_UP,
    KA_BOARD_DOWN,
    KA_BOARD_TYPE,
    KA_BOARD_DELETE,
    KA_BOARD_SPACE,
    KA_BOARD_DONE,

    KA_COUNT,
    KA_NONE = -1
} KeyAction;

typedef enum { KB_KEY, KB_PAD, KB_MOUSE } KeyBindKind;

/* Modifier rules for a key. Without one, the modifier does not matter. */
enum {
    KM_SHIFT = 1u << 0,
    KM_NO_SHIFT = 1u << 1,
    KM_CTRL = 1u << 2,
    KM_NO_ALT = 1u << 3, /* Enter: Alt+Enter is the fullscreen key and nothing else */
    KM_ALT = 1u << 4,
};

typedef struct {
    KeyAction action;
    KeyBindKind kind;
    int code;      /* InputKey, PadButton or InputMouseButton */
    unsigned mods; /* keys only */
    bool repeat;   /* acts again while held (arrows, d-pad, Backspace, Delete) */
} KeyBind;

int Keymap_Count(void);
const KeyBind *Keymap_Get(int index);

/* Any of the action's bindings fired this frame. */
bool Keymap_Pressed(KeyAction a, const Input *in);
/* Which binding (its index) fired, or -1. A button that acts when let go
 * remembers it, so only that same key or button finishes the press. */
int Keymap_Fired(KeyAction a, const Input *in);
/* That binding's key or button is still held. */
bool Keymap_Held(int index, const Input *in);

/* "Shift+U", "Enter", "D-pad left", "A", "Back button". */
void Keymap_BindName(const KeyBind *b, char *out, size_t size);
/* Every binding of one kind for the action, joined with " / "; "" if none. */
void Keymap_Names(KeyAction a, KeyBindKind kind, char *out, size_t size);
/* The first key (or, after pad use, pad button) for a hint line. */
void Keymap_Hint(KeyAction a, InputDevice device, char *out, size_t size);
/* The same, short enough for a key cap or a pad button on a button ("Del",
 * "A", "View"); *pad says it is a pad button. "" when there is none. */
void Keymap_Cap(KeyAction a, InputDevice device, char *out, size_t size, bool *pad);

/* --- the reference cards (Settings > Controls) --- */
#define KEYMAP_CARD_LINES 16
#define KEYMAP_WHAT_LEN 28
#define KEYMAP_KEYS_LEN 40
#define KEYMAP_PAD_LEN 32
#define KEYMAP_MOUSE_LEN 20

typedef struct {
    char what[KEYMAP_WHAT_LEN];
    char keys[KEYMAP_KEYS_LEN];
    char pad[KEYMAP_PAD_LEN];
    char mouse[KEYMAP_MOUSE_LEN];
} KeymapLine;

int Keymap_CardCount(void);
const char *Keymap_CardTitle(int card);
/* A sentence under the table ("" if none). */
const char *Keymap_CardNote(int card);
/* The card's lines; admin-only actions are left out unless admin. */
int Keymap_CardLines(int card, bool admin, KeymapLine *out, int max);
/* Cards with something to show: admin's own cards (last in the list) only
 * for admin. */
int Keymap_CardCountFor(bool admin);
/* Some card lists the action (the tests check every action is on one). */
bool Keymap_OnCard(KeyAction a);

#endif
