#ifndef UIINPUT_H
#define UIINPUT_H

#include <stdbool.h>

/* One input reader for a game's menus (opt-in: only games that list
 * uiinput in COMMON_MODULES build it; Blockfall is the first). Each frame the
 * keyboard, the pad and the mouse are read once into an Input, which every
 * screen then asks instead of calling raylib itself. That gives what raylib
 * and gamepad.h alone do not:
 *   - pad buttons are real buttons (gamepad.h turns A into Enter AND Space,
 *     so a button would fire twice, and a rebound key drops off the pad);
 *   - arrows, the d-pad, Backspace and Delete repeat while held: after
 *     INPUT_REPEAT_DELAY, every INPUT_REPEAT_INTERVAL (a game may add keys,
 *     Input_SetKeyRepeat);
 *   - the device used last is remembered, so hints can show the right keys.
 *
 * A copy of Ghost Launcher's src/input.c, made general: the logical screen
 * size is the game's (Input_Init), and the keys a game's menus use were
 * added. The launcher keeps its own copy until it switches over.
 *
 * uiinput.c is raylib-free (InputRaw in, Input out) and covered by headless
 * tests; uiinput_rl.c fills an InputRaw from raylib. */

#define INPUT_REPEAT_DELAY 0.4f
#define INPUT_REPEAT_INTERVAL 0.1f
#define INPUT_MAX_CHARS 32

/* Keyboard keys the menus read. Letters are contiguous, IK_A..IK_Z. */
typedef enum {
    IK_LEFT, IK_RIGHT, IK_UP, IK_DOWN,
    IK_ENTER, IK_SPACE, IK_ESCAPE, IK_BACKSPACE, IK_DELETE, IK_TAB,
    IK_HOME, IK_END, IK_PAGE_UP, IK_PAGE_DOWN, IK_F11,
    IK_LEFT_BRACKET, IK_RIGHT_BRACKET,
    IK_A, IK_B, IK_C, IK_D, IK_E, IK_F, IK_G, IK_H, IK_I, IK_J, IK_K, IK_L, IK_M,
    IK_N, IK_O, IK_P, IK_Q, IK_R, IK_S, IK_T, IK_U, IK_V, IK_W, IK_X, IK_Y, IK_Z,
    IK_COUNT
} InputKey;

/* Xbox-style pad buttons. The d-pad entries also follow the left stick, so
 * the stick and the d-pad are one control and never fire twice. */
typedef enum {
    PB_A, PB_B, PB_X, PB_Y, PB_LB, PB_RB, PB_LT, PB_RT,
    PB_VIEW, PB_MENU, PB_LS, PB_RS,
    PB_UP, PB_DOWN, PB_LEFT, PB_RIGHT,
    PB_COUNT
} PadButton;

typedef enum { MB_LEFT, MB_RIGHT, MB_MIDDLE, MB_BACK, MB_FORWARD, MB_COUNT } InputMouseButton;

typedef enum { DEV_KEYBOARD, DEV_PAD, DEV_MOUSE } InputDevice;

/* One frame of raw device state, as raylib (or a test) reports it. */
typedef struct {
    bool keyDown[IK_COUNT];
    bool keyPressed[IK_COUNT]; /* raylib's own edge: also catches a tap shorter than a frame */
    bool shift, ctrl, alt;
    bool focused;              /* the window has keyboard focus */
    bool padDown[PB_COUNT];    /* all false while unfocused or with no pad */
    float mouseX, mouseY;      /* logical coordinates (winscale maps the pointer) */
    bool mouseMoved;           /* the pointer itself moved (not a rescale of the window) */
    bool mouseDown[MB_COUNT], mousePressed[MB_COUNT], mouseReleased[MB_COUNT];
    float wheel;               /* notches this frame, positive = away from the player */
    int chars[INPUT_MAX_CHARS];
    int charCount;
} InputRaw;

typedef struct {
    float held, next;
    bool armed; /* the hold began with a press seen here */
} InputRepeat;

/* What survives between frames. */
typedef struct {
    float screenW, screenH; /* the logical screen: outside it is the letterbox */
    bool keyRepeats[IK_COUNT];
    bool padPrev[PB_COUNT];
    bool shiftPrev, ctrlPrev, altPrev;
    bool wasFocused;
    InputRepeat keyRep[IK_COUNT];
    InputRepeat padRep[PB_COUNT];
    InputDevice lastDevice;
    InputDevice hintDevice;
} InputState;

/* This frame's input. key/pad are presses (edges); keyRepeat/padRepeat are
 * presses plus the repeat steps of a held arrow, d-pad, Backspace or Delete
 * (for any other control they equal the press). */
typedef struct {
    bool key[IK_COUNT], keyRepeat[IK_COUNT], keyDown[IK_COUNT];
    bool shift, ctrl, alt;
    bool focused;
    bool pad[PB_COUNT], padRepeat[PB_COUNT], padDown[PB_COUNT];
    float mouseX, mouseY;
    bool mouseInside; /* over the logical screen, not the letterbox bars */
    bool mouseMoved;
    bool mousePressed[MB_COUNT], mouseReleased[MB_COUNT], mouseDown[MB_COUNT];
    float wheel;
    int chars[INPUT_MAX_CHARS];
    int charCount;
    InputDevice lastDevice;
    /* The keyboard or the pad, whichever was used last: what hints name. A
     * nudge of the mouse does not turn a pad player's hints into keys. */
    InputDevice hintDevice;
    bool any; /* any key, button, click, wheel or pointer move this frame */
    /* Shift, Ctrl or Alt went down this frame (one held as the window
     * regains focus does not count). Kept out of `any`, so a modifier alone
     * changes no device or pointer. */
    bool modPressed;
} Input;

/* screenW x screenH is the game's logical screen (620x720 for Blockfall). */
void Input_Init(InputState *st, int screenW, int screenH);
/* Turns this frame's raw state into an Input. dt is the frame time. */
void Input_Step(InputState *st, const InputRaw *raw, float dt, Input *out);
/* Call when the screen changes: a key or d-pad still held from the last
 * screen stops repeating until it is pressed again, so a hold made on one
 * screen never scrolls the next. */
void Input_DropHolds(InputState *st);

/* The controls that repeat while held by default: the arrows, Backspace and
 * Delete; the d-pad and pad X. A game can make more keys repeat (Blockfall's
 * W A S D move through its menus as the arrows do). */
bool Input_KeyRepeats(InputKey k);
bool Input_PadRepeats(PadButton b);
void Input_SetKeyRepeat(InputState *st, InputKey k, bool repeats);

/* The same frame with the keyboard and pad taken out: while a text field
 * has the keys, the buttons around it still answer the mouse. */
void Input_MouseOnly(const Input *in, Input *out);
/* The same frame with every press taken out, but what is held still held:
 * what a screen sees on the frame it opens, so the press that opened it
 * never acts on it too. */
void Input_Quiet(const Input *in, Input *out);

/* Implemented in uiinput_rl.c (raylib): reads every device into *raw. */
void InputRL_Read(InputRaw *raw);

#endif
