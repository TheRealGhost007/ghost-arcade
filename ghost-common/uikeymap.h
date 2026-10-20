#ifndef UIKEYMAP_H
#define UIKEYMAP_H

#include <stdbool.h>
#include <stddef.h>
#include "uiinput.h"

/* Which key, pad button or mouse button does what in a game's menus: one
 * table the button kit (uikit.h) and its text field (uitextfield.h) ask
 * instead of naming keys themselves. The kit's own actions come built in; a
 * game adds a table of its own (Keymap_SetGameBinds): actions of its own
 * numbered from KA_GAME, and extra keys for the built-in ones (Blockfall's
 * W A S D move through its menus too). Opt-in, like the kit. No raylib.
 *
 * The built-in part is Ghost Launcher's src/keymap.c cut down to the menus;
 * the launcher keeps its own table. */

typedef enum {
    /* everywhere */
    KA_BACK,       /* one level back */
    KA_FULLSCREEN, /* winscale.c acts on it: read so nothing else does */

    /* any list of buttons: menus, settings, the "are you sure?" box */
    KA_UI_LEFT,
    KA_UI_RIGHT,
    KA_UI_UP,
    KA_UI_DOWN,
    KA_UI_PRESS,
    KA_UI_PREV_TAB,
    KA_UI_NEXT_TAB,

    /* a list longer than the screen */
    KA_SCROLL_UP,
    KA_SCROLL_DOWN,
    KA_SCROLL_PAGE_UP,
    KA_SCROLL_PAGE_DOWN,
    KA_SCROLL_TOP,
    KA_SCROLL_END,

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

    KA_KIT_COUNT,
    KA_GAME = 64, /* a game's own actions start here */
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
    int action;    /* a KeyAction, or the game's own (KA_GAME and up) */
    KeyBindKind kind;
    int code;      /* InputKey, PadButton or InputMouseButton */
    unsigned mods; /* keys only */
    bool repeat;   /* acts again while held (arrows, d-pad, Backspace, Delete) */
} KeyBind;

/* The game's own bindings, tried after the built-in ones. The table must
 * outlive every use (a static array); NULL, 0 removes it. */
void Keymap_SetGameBinds(const KeyBind *binds, int count);

/* Every binding: the built-in ones first (0 .. Keymap_BuiltinCount() - 1),
 * then the game's. */
int Keymap_Count(void);
int Keymap_BuiltinCount(void);
const KeyBind *Keymap_Get(int index);

/* Any of the action's bindings fired this frame. */
bool Keymap_Pressed(int action, const Input *in);
/* Which binding (its index) fired, or -1. A button that acts when let go
 * remembers it, so only that same key or button finishes the press. */
int Keymap_Fired(int action, const Input *in);
/* A binding of the action went down this frame: a repeat step of a held
 * arrow does not count (a screen that times its own holds). */
bool Keymap_PressedEdge(int action, const Input *in);
/* That binding's key or button is still held. */
bool Keymap_Held(int index, const Input *in);
/* Some binding of the action is held now (a stepper held down). */
bool Keymap_AnyHeld(int action, const Input *in);

/* "Shift+Tab", "Enter", "D-pad left", "A", "Back button". */
void Keymap_BindName(const KeyBind *b, char *out, size_t size);
/* Every binding of one kind for the action, joined with " / "; "" if none. */
void Keymap_Names(int action, KeyBindKind kind, char *out, size_t size);
/* The first key (or, after pad use, pad button) for a hint line. */
void Keymap_Hint(int action, InputDevice device, char *out, size_t size);
/* The same, short enough for a key cap or a pad button ("Del", "A", "View",
 * "Up" for the d-pad); *pad says it is a pad button. "" when there is none. */
void Keymap_Cap(int action, InputDevice device, char *out, size_t size, bool *pad);

#endif
