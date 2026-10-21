#ifndef ACTIONS_H
#define ACTIONS_H

#include <stdbool.h>
#include "persist.h"
#include "uiinput.h"

/* What the keys and the pad mean during a run, read once a frame (no raylib:
 * the keyboard is asked through two callbacks, the pad comes from the menus'
 * input reader, uiinput.h).
 *  - The keyboard: YOUR keys from config.ini (any key raylib knows), two
 *    per action. Esc always opens the pause menu too, unless a config.ini
 *    from before gave Esc to another action (Controls_EscTaken).
 *  - The pad: YOUR buttons, set apart from the keys, so a rebound key never
 *    drops off the pad. As it came: the d-pad or stick moves (Up rotates), A
 *    hard-drops, X rotates left, Y or LB hold, Menu pauses, B opens the
 *    pause menu, View restarts (asking first when the run has points). Menu
 *    always pauses too, unless another action has it.
 *  - Left and Right held together cancel each other.
 *  - M, - and = (or the keypad's - and +) mute and change the volume, F3
 *    shows the frame rate. */

/* raylib's key codes for the fixed keys (scr_play.c checks they match). */
#define ACT_KEY_SPACE 32
#define ACT_KEY_MINUS 45
#define ACT_KEY_EQUAL 61
#define ACT_KEY_M 77
#define ACT_KEY_ESCAPE 256
#define ACT_KEY_F3 292
#define ACT_KEY_KP_SUBTRACT 333
#define ACT_KEY_KP_ADD 334

typedef struct {
    int key[CONTROL_ACTIONS][2]; /* raylib key codes (0: none), controls.h's action order */
    int pad[CONTROL_ACTIONS][2]; /* PadButton or PAD_NONE */
    bool escTaken;  /* another action has Esc (controls.h) */
    bool menuTaken; /* another action has the pad's Menu */
} PlayKeys;

/* config.ini's key names into key codes; the name-to-code lookup is passed
 * in (keynames.c needs raylib). */
typedef int (*KeyCodeFn)(const char *name);
void Actions_Keys(const Settings *s, KeyCodeFn code, PlayKeys *out);

typedef bool (*KeyQueryFn)(int code);
typedef struct {
    KeyQueryFn down;    /* the key is held */
    KeyQueryFn pressed; /* it went down this frame */
} KeyReader;

typedef struct {
    bool left, right, softDrop;          /* held */
    bool rotate, rotateLeft, hardDrop, hold, pause; /* pressed this frame */
    bool rotateHalf;                     /* pressed this frame: Rotate 180 (3.2.0; nothing until a key is set) */
    bool undo;                           /* pressed this frame: Undo (3.5.0; the Practice room alone acts on it) */
    bool restartKey, restartPad;         /* your Restart keys; your Restart buttons (View) */
    bool back;                           /* the Pause row's second key or button, and Esc: the pause menu, then back */
    bool mute, volumeDown, volumeUp, fps;
} PlayInput;

/* One frame. quiet: the frame the run opened on, where only what is held
 * counts (the press that started the run never acts in it). */
void Actions_Read(const PlayKeys *k, const KeyReader *kb, const Input *pad, bool quiet, PlayInput *out);

#endif
