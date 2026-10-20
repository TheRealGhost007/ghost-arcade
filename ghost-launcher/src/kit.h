#ifndef KIT_H
#define KIT_H

#include <stdbool.h>
#include "input.h"

/* The button kit: the controls every launcher screen is built from, the
 * raylib-free half (kitdraw.c draws them). A screen describes its controls
 * once per frame as a KitList of items with a rectangle in the logical
 * 960x640 screen. The same list is used twice: Kit_Update hit-tests the
 * pointer, moves the focus and turns presses into events, and the screen's
 * drawing draws exactly those rectangles, so what you click is what you see.
 *
 * The rules the kit keeps, so every screen behaves alike:
 *  - a click acts when the button is let go over the same control (drag off
 *    first and nothing happens); Enter, Space and pad A act when let go too,
 *    and the control shows pressed while they are held;
 *  - moving the mouse hands the focus to the control under it; the first key
 *    or pad press carries on from there;
 *  - arrows and the d-pad move the focus to the nearest control that way,
 *    skipping disabled ones; at an edge the list either stops or wraps round
 *    (KIT_WRAP_X / KIT_WRAP_Y);
 *  - on a value control (switch, stepper, ten blocks, tabs) Left/Right change
 *    the value instead of moving the focus.
 * Text is Press Start 2P, which is monospaced, so widths are known here
 * without the font (Kit_TextWidth). */

#define KIT_SCREEN_W 960
#define KIT_SCREEN_H 640
#define KIT_MAX_ITEMS 64 /* the name board alone is 43 letters and keys */
#define KIT_NONE (-1)
#define KIT_LABEL_LEN 48
#define KIT_REASON_LEN 80

typedef struct {
    float x, y, w, h;
} KitRect;

typedef enum {
    KIT_BUTTON,  /* acts when pressed */
    KIT_ROW,     /* a full-width list row: acts when pressed */
    KIT_TOGGLE,  /* an Off|On pair: value 0 or 1, either half can be clicked */
    KIT_STEPPER, /* "< value >": value in [min, max], the arrows can be clicked */
    KIT_SLIDER,  /* ten blocks: value in [0, max]; click, drag or wheel */
    KIT_TABS,    /* a row of tabs: value is the open one */
    KIT_AREA,    /* invisible: takes clicks (a panel, the dimmed screen behind a dialog) */
} KitKind;

/* How a button is drawn; what it does never depends on this. */
typedef enum {
    KIT_STYLE_BLOCK, /* bevelled block, 16 px label */
    KIT_STYLE_SMALL, /* bevelled block, 8 px label (Back, header chips) */
    KIT_STYLE_CLOSE, /* the X */
    KIT_STYLE_SIGN,  /* the EXIT sign on the hall wall */
    KIT_STYLE_PILL,  /* the update notice on the hall */
    KIT_STYLE_CHIP,  /* the hall's control strip: a key cap (or pad button) and an 8 px label */
} KitStyle;

/* KIT_ICON_STAR: a favourite's star, gold when the item's value is 1. */
typedef enum { KIT_ICON_NONE, KIT_ICON_BACK, KIT_ICON_MENU, KIT_ICON_STAR } KitIcon;

/* Item flags. */
enum {
    KIT_DISABLED = 1u << 0,  /* faint; cannot be pressed or focused; reason says why */
    KIT_NOFOCUS = 1u << 1,   /* mouse only: keys and the pad never land on it */
    KIT_WRAP = 1u << 2,      /* stepper, tabs: past one end comes round to the other */
    KIT_SELFDRAWN = 1u << 3, /* its screen draws it in place (KitDraw_List skips it) */
    KIT_PADCAP = 1u << 4,    /* KIT_STYLE_CHIP: its key is a pad button, drawn as one */
    KIT_ALIGN_LEFT = 1u << 5, /* a block's label at its left, as with a key, so a column of them lines up */
};

typedef struct {
    int id;
    KitKind kind;
    KitStyle style;
    KitIcon icon;
    KitRect r;
    unsigned flags;
    int textSize;                  /* 8 or 16 */
    char label[KIT_LABEL_LEN];
    char key[8];                   /* a row's keyboard key, shown at its right ("L", "Esc"); a chip's cap */
    char badge[16];                /* a short note after a row's label ("1 new") */
    char reason[KIT_REASON_LEN];   /* why it is disabled */
    int value, min, max;
    const char *const *cells;      /* KIT_TABS: the tab labels; KIT_TOGGLE: NULL = "Off", "On" */
    int cellCount;                 /* KIT_TABS only */
} KitItem;

/* List-wide navigation rules. Without KIT_NAV_X / KIT_NAV_Y the arrows on
 * that axis are left to the screen (Trophies' Left/Right change the game). */
enum {
    KIT_NAV_X = 1u << 0,
    KIT_NAV_Y = 1u << 1,
    KIT_WRAP_X = 1u << 2,
    KIT_WRAP_Y = 1u << 3,
};

typedef struct {
    KitItem items[KIT_MAX_ITEMS];
    int count;
    unsigned nav;
} KitList;

/* What a screen's controls remember between frames. Items are known by id,
 * so a list rebuilt every frame (labels change, rows scroll) keeps its focus. */
typedef struct {
    int focus;
    int hover, hoverCell;      /* under the pointer this frame */
    int mouseDown, mouseCell;  /* where the left button went down */
    int keyDown;               /* held by Enter, Space or pad A */
    int keyBind;               /* which of those (a keymap.h binding): only it finishes the press */
    float wheel;               /* part notches, so a touchpad moves a slider one step at a time */
} KitState;

typedef enum { KIT_EV_NONE, KIT_EV_PRESS, KIT_EV_CHANGE } KitEventType;

typedef struct {
    KitEventType type;
    int id;
    int value; /* KIT_EV_CHANGE: the new value */
} KitEvent;

typedef enum { KIT_LOOK_NORMAL, KIT_LOOK_HOVER, KIT_LOOK_FOCUSED, KIT_LOOK_PRESSED, KIT_LOOK_DISABLED } KitLook;

typedef enum { KIT_LEFT, KIT_RIGHT, KIT_UP, KIT_DOWN } KitDir;

/* What a drawing needs: the list, its state (NULL = everything at rest, for
 * a screen dimmed behind an overlay), the device used last (for the looks)
 * and the one hint lines name: the keyboard, or the pad after pad use (a
 * nudge of the mouse leaves it, as on the hall's strip). */
typedef struct {
    const KitList *list;
    const KitState *state;
    InputDevice device;
    InputDevice hints;
} KitView;

/* Width in pixels of Press Start 2P text at 8/16/24 px. */
int Kit_TextWidth(const char *text, int size);

/* A chip's cap: a key cap as wide as its name, or a pad button: a disc for
 * A B X Y, a pill for the rest (View and Menu are drawn as their icons). */
#define KIT_CAP_H 16
int Kit_CapWidth(const char *cap, bool pad);
/* A chip: 4 px, the cap, 6 px, the 8 px label, 8 px. */
int Kit_ChipWidth(const char *cap, bool pad, const char *label);
#define KIT_CHIP_ICON_W 14 /* an icon before a chip's label, and its gap */

/* --- building a list --- */
void Kit_Clear(KitList *l, unsigned nav);
/* Adds an enabled, focusable item with a 16 px label; NULL when the list is
 * full. The caller sets anything else on the item it returns. */
KitItem *Kit_Add(KitList *l, int id, KitKind kind, KitRect r, const char *label);
const KitItem *Kit_Get(const KitList *l, int id);
void Kit_Disable(KitItem *it, const char *reason);

/* --- geometry, all pure --- */
bool Kit_Inside(KitRect r, float x, float y);
/* Where an item can be hit: its rectangle; for tabs, as wide as their cells. */
KitRect Kit_Bounds(const KitItem *it);
/* The topmost item at the point (the one added last), or KIT_NONE. */
int Kit_HitTest(const KitList *l, float x, float y);
bool Kit_CanFocus(const KitItem *it);
int Kit_FirstFocus(const KitList *l);
/* The id the focus moves to from fromId (by where it is, even if it is
 * disabled now), or fromId when there is nowhere to go; the first control
 * when fromId is not in the list. */
int Kit_Navigate(const KitList *l, int fromId, KitDir dir);
/* Cells: the halves of a switch, the arrows of a stepper (0 left, 1 right),
 * the tabs of a tab bar. Other kinds are one cell, the whole item. */
int Kit_CellCount(const KitItem *it);
KitRect Kit_CellRect(const KitItem *it, int cell);
int Kit_CellAt(const KitItem *it, float x, float y);
/* The ten blocks: the value a click or drag at x means. The slider's
 * rectangle starts with one block's width that means 0 (mute). */
int Kit_SliderValueAt(const KitItem *it, float x);
#define KIT_BLOCK_PITCH 16
#define KIT_ARROW_W 40

/* --- the state --- */
void Kit_Init(KitState *s);
/* A screen opens or comes back: the pointer and held keys are forgotten
 * (a press made on the last screen never acts here); focus is focusId, or
 * the first control when KIT_NONE. */
void Kit_Enter(KitState *s, int focusId);
/* One frame: at most one event. */
KitEvent Kit_Update(KitState *s, const KitList *l, const Input *in);

/* --- for drawing --- */
KitLook Kit_Look(const KitState *s, const KitItem *it, InputDevice device);
int Kit_PressedCell(const KitState *s, const KitItem *it);
int Kit_HoverCell(const KitState *s, const KitItem *it, InputDevice device);
/* The disabled control under the pointer says why: its reason, or "". */
const char *Kit_Reason(const KitState *s, const KitList *l, InputDevice device);
/* The pointer is over something that can be pressed: show the hand. */
bool Kit_WantsHand(const KitState *s, const KitList *l);
/* The wheel belongs to the control under the pointer (a slider or stepper),
 * not to the screen's scrolling. */
bool Kit_OwnsWheel(const KitState *s, const KitList *l);

/* --- scrolling lists --- */
typedef struct {
    int top;     /* first row shown */
    float wheel; /* part notches */
} KitScroll;
/* Up/Down and the d-pad move a row (with repeat), PgUp/PgDn and LB/RB a
 * page, Home/End to the ends (keymap.h's KA_SCROLL_*), the wheel a row per
 * notch. True if it moved. */
bool Kit_Scroll(KitScroll *sc, int count, int visible, const Input *in, bool useWheel);
void Kit_ScrollClamp(KitScroll *sc, int count, int visible);
/* The wheel alone, for a list whose keys move a focus instead of the view
 * (the daily desk, Chipforge's songs). */
bool Kit_ScrollWheel(KitScroll *sc, int count, int visible, float wheel);
/* Scrolls just enough for row `at` to show. */
void Kit_ScrollShow(KitScroll *sc, int at, int count, int visible);

/* --- the "are you sure?" box --- */
#define KIT_ID_DLG_VEIL 900
#define KIT_ID_DLG_BOX 901
#define KIT_ID_DLG_SAFE 902
#define KIT_ID_DLG_ACT 903
#define KIT_DLG_X 160
#define KIT_DLG_Y 200
#define KIT_DLG_W 640
#define KIT_DLG_H 208

typedef struct {
    bool open;
    int kind;  /* the caller's: what is being asked */
    int arg;   /* the caller's: e.g. which machine */
    char title[64];
    char body[2][80];
    char safe[24], act[24];
} KitDialog;

typedef enum { KIT_DLG_OPEN, KIT_DLG_SAFE, KIT_DLG_ACT } KitDialogResult;

/* The safe answer is on the left and has the focus. */
void Kit_DialogOpen(KitDialog *d, KitState *s, int kind, int arg, const char *title, const char *body0,
                    const char *body1, const char *safe, const char *act);
void Kit_DialogLayout(const KitDialog *d, KitList *l);
/* Esc, pad B, the mouse's back button and a click outside the box all
 * choose the safe answer. Closes the dialog on an answer. */
KitDialogResult Kit_DialogAnswer(KitDialog *d, const KitEvent *ev, const Input *in);
/* The device to draw a box's buttons for. The safe answer is picked from
 * the start, so a box opened with the mouse shows that too (as keys would),
 * until the pointer is on one of its buttons. */
InputDevice Kit_DialogDevice(const KitState *s, const KitList *l, InputDevice device);

#endif
