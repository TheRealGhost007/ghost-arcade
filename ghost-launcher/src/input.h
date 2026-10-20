#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>

/* The launcher's one input reader. Each frame the keyboard, the pad and the
 * mouse are read once into an Input, which every screen then asks instead of
 * calling raylib itself. That gives three things raylib alone does not:
 *   - pad buttons are real buttons (ghost-common's gamepad.c turns A into
 *     Enter AND Space, so a launcher button would fire twice);
 *   - arrows, the d-pad, Backspace, Delete and pad X (the name board's
 *     delete) repeat while held: after INPUT_REPEAT_DELAY, every
 *     INPUT_REPEAT_INTERVAL;
 *   - the device used last is remembered, so hints can show the right keys.
 *
 * input.c is raylib-free (InputRaw in, Input out) and covered by the
 * headless tests; input_rl.c fills an InputRaw from raylib. */

#define INPUT_REPEAT_DELAY 0.4f
#define INPUT_REPEAT_INTERVAL 0.1f
#define INPUT_MAX_CHARS 32

/* Keyboard keys the launcher reads. Letters are contiguous, IK_A..IK_Z. */
typedef enum {
    IK_LEFT, IK_RIGHT, IK_UP, IK_DOWN,
    IK_ENTER, IK_SPACE, IK_ESCAPE, IK_BACKSPACE, IK_DELETE, IK_TAB,
    IK_HOME, IK_END, IK_PAGE_UP, IK_PAGE_DOWN, IK_F11,
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
    float mouseX, mouseY;      /* logical 960x640 coordinates */
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
     * changes no device or pointer; attract mode wakes on it all the same. */
    bool modPressed;
} Input;

void Input_Init(InputState *st);
/* Turns this frame's raw state into an Input. dt is the frame time. */
void Input_Step(InputState *st, const InputRaw *raw, float dt, Input *out);
/* Call when the screen changes: a key or d-pad still held from the last
 * screen stops repeating until it is pressed again, so a hold made on the
 * hall never scrolls the next screen or eats a name with Backspace. */
void Input_DropHolds(InputState *st);

/* True for the controls that repeat while held. */
bool Input_KeyRepeats(InputKey k);
bool Input_PadRepeats(PadButton b);

/* The same frame with the keyboard and pad taken out: while a text field
 * has the keys, the buttons around it still answer the mouse. What each key
 * means is keymap.h's business. */
void Input_MouseOnly(const Input *in, Input *out);

/* Implemented in input_rl.c (raylib): reads every device into *raw. */
void InputRL_Read(InputRaw *raw);

#endif
