#ifndef CONTROLS_H
#define CONTROLS_H

#include <stdbool.h>
#include <stddef.h>
#include "persist.h"

/* What the keys and the pad do in a run, as Settings > Controls shows it:
 * ten actions, each with two keys and two pad buttons (config.ini's names:
 * "LEFT", "SPACE", "A"; the pad as uiinput.h's PadButton). No raylib.
 * Rotate 180 (3.2.0) is the optional one: it comes with no key or button,
 * and unlike every other action it may be left with none (its only key
 * can be cleared, or taken by a swap).
 *
 * The rules (REWORK_PLAN.md, "Settings with tabs", "More controls"):
 *  - A key (or button) another action has swaps places: that action takes
 *    the one this slot had. Unless that would leave it with no key (or no
 *    button) at all: then nothing changes and the note says why.
 *  - Esc and the pad's Menu (Start) always pause, and they are never handed
 *    to another action by a swap: that action gets none instead (Esc can
 *    never be lost). A config.ini from before that already gave Esc to an
 *    action still works as it did (Controls_EscTaken).
 *  - Enter, Space and the pad's A pick menu items, so Pause and Restart
 *    never hold them: turned away on a cap, refused outright, withheld in a
 *    swap, and set right when config.ini is read (persist.c).
 *  - The Pause row's two slots differ: the first pauses (P, Menu), the
 *    second also goes back (Esc, B): out of the pause menu's settings, or
 *    from the results card to the menu. It was "Back to menu" before 3.0.0
 *    (config.ini's quit). */
enum {
    ACT_LEFT, ACT_RIGHT, ACT_SOFT, ACT_HARD, ACT_ROTATE, ACT_ROTATE_LEFT, ACT_HOLD, ACT_PAUSE, ACT_RESTART,
    ACT_ROTATE_180, /* 3.2.0, after the nine so config.ini's pad order and the table's rows before it stay as they were */
    ACT_UNDO,       /* 3.5.0, the Practice room's undo (U, RB as it comes): last, for the same reason */
    ACT_COUNT
};
_Static_assert(ACT_COUNT == CONTROL_ACTIONS, "persist.h keeps a pad pair for every action");
#define CONTROL_SLOTS 2

/* "Move left" ... "Restart". */
const char *Controls_ActionLabel(int action);
/* An action that may be left with no key and no button at all (its only
 * key can be cleared, or taken by a swap): Rotate 180 (3.2.0) and Undo
 * (3.5.0), which only the Practice room reads. */
bool Controls_Optional(int action);
/* A key slot's field in Settings (NULL out of range). */
char *Controls_Key(Settings *s, int action, int slot);
const char *Controls_KeyConst(const Settings *s, int action, int slot);
/* A pad slot's button, or PAD_NONE. */
int Controls_Pad(const Settings *s, int action, int slot);
/* No key: NULL, "" or NONE. */
bool Controls_IsNone(const char *key);

typedef enum {
    BIND_SAME,    /* the slot already had it */
    BIND_SET,     /* a free key: it just binds */
    BIND_SWAPPED, /* another slot had it: the two swapped (see other*) */
    BIND_REFUSED, /* it is the only key (or button) another action has, or
                   * Enter, Space or pad A for Pause or Restart (they pick
                   * menu items) */
} BindKind;

typedef struct {
    BindKind kind;
    int otherAction, otherSlot; /* the slot that had it (-1 none) */
    bool withheld;              /* the other slot would have got Esc or Menu (or Enter,
                                 * Space or pad A, when it is Pause's or Restart's): it got none */
    char note[128];             /* what the note line says ("C moved from Hold to Rotate left.") */
} BindResult;

/* Binds a key (config.ini's name) to an action's slot, by the rules above. */
BindResult Controls_BindKey(Settings *s, int action, int slot, const char *key);
/* The same for a pad button. */
BindResult Controls_BindPad(Settings *s, int action, int slot, int button);
/* Clears a second slot (a first slot always keeps its key, and an action's
 * last key or button is never cleared; Rotate 180 is the exception: any of
 * its slots clears). True if it changed. */
bool Controls_ClearKey(Settings *s, int action, int slot);
bool Controls_ClearPad(Settings *s, int action, int slot);
/* Every key and button back to how Blockfall came (Settings > Controls >
 * Reset controls, which asks first). */
void Controls_Reset(Settings *s);
/* The keys and buttons are as Blockfall came. */
bool Controls_AreDefault(const Settings *s);

/* Whether a captured key may be bound to `action`, and if not, why (M is
 * the mute key in a run; a key Blockfall cannot store; Enter and Space pick
 * menu items, so Pause and Restart cannot have them). Esc is not asked: it
 * cancels. */
bool Controls_KeyAllowed(int action, const char *key, char *why, size_t size);
/* The same for a captured pad button: A picks menu items, so Pause and
 * Restart cannot have it. Menu is not asked: it cancels. */
bool Controls_PadAllowed(int action, int button, char *why, size_t size);

/* No two key slots share a key, and no two pad slots a button (NONE aside). */
bool Controls_AllDifferent(const Settings *s);
/* An action other than Pause holds Esc (a config.ini from before 3.0.0 may
 * say so): Esc then does that action in a run, as Settings shows, and
 * Pause's own keys pause. Otherwise Esc always pauses too. */
bool Controls_EscTaken(const Settings *s);
/* The same for the pad's Menu button. */
bool Controls_MenuTaken(const Settings *s);

/* Words for a key: "Left", "Space", "Esc", "Left Shift"; "none" for none.
 * Short ones fit the narrow panel: "LShift", "Bksp". */
void Controls_Name(const char *key, char *out, size_t size);
void Controls_ShortName(const char *key, char *out, size_t size);
/* Every name config.ini can hold has words of its own (A-Z and 0-9 are
 * their own words). */
bool Controls_HasWords(const char *key);
/* A pad button as the key caps draw it ("A", "LB", "Menu", "View", "Up"
 * for the d-pad), and in words ("A", "LB", "Menu", "D-pad up"). "" / "none"
 * for none. */
const char *Controls_PadCap(int button);
const char *Controls_PadWords(int button);

/* What the play screen, the hints and How to play say: your keys, not the
 * defaults, and your pad's buttons. */
typedef struct {
    /* short, for the controls card */
    char move[48], softDrop[24], rotate[24], rotateLeft[24], hardDrop[24], hold[24], pause[48], restart[24], back[52];
    char rotateHalf[24]; /* "-" until a key is set (3.2.0) */
    char undo[24];       /* the Practice room's undo, "-" with none (3.5.0) */
    /* full, for the hints and How to play */
    char pauseName[24], restartName[24], backName[24];
    char moveName[52], softName[24], rotateName[24], rotateLeftName[24], hardName[24], holdName[24], rotateHalfName[24], undoName[24];
    /* the pad's caps for the card, space-separated ("Left Right", "Y LB") */
    char padMove[24], padSoft[16], padRotate[16], padRotateLeft[16], padHard[16], padHold[16], padPause[16], padRestart[16], padRotateHalf[16];
    char padUndo[16];
} ControlWords;
void Controls_Words(const Settings *s, ControlWords *out);

#endif
