#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include "game.h"

/* Devices a player can play with: two halves of the keyboard and four pads.
 * One keyboard player gets every keyboard key (WASD or arrows, and any of
 * the action keys); with two, the keyboard splits down the middle.
 *
 *   keyboard left : W A S D, Shoot Space, Pass Left Shift, Check E
 *   keyboard right: arrows,  Shoot /,     Pass .,          Check ,
 *   keyboard alone: also Shoot Z or J, Pass X or K, Check C or L
 *   pad           : left stick or d-pad, Shoot X (or RT), Pass A, Check B
 * The two keyboard halves can be rebound (Settings > Keyboard keys); the
 * menus always answer to the arrows, WASD, Enter, Space and Esc.
 *
 * This is the only file that reads raylib's keys and pads; the game only
 * ever sees PadInput (a direction byte and three buttons). */
typedef enum { DEV_KEYS_LEFT, DEV_KEYS_RIGHT, DEV_PAD0, DEV_PAD1, DEV_PAD2, DEV_PAD3, DEV_COUNT } Device;

/* A keyboard half's keys, in this order. */
typedef enum { KA_UP, KA_DOWN, KA_LEFT, KA_RIGHT, KA_SHOOT, KA_PASS, KA_CHECK, KA_COUNT } KeyAction;
void Input_DefaultKeys(int half, int out[KA_COUNT]); /* half 0 = WASD side, 1 = arrows side */
void Input_SetKeys(int half, const int keys[KA_COUNT]);
const int *Input_Keys(int half);
/* A key's name for the screen: "SPACE", "LEFT SHIFT", "/" ... */
const char *Input_KeyLabel(int key);
/* Keys a player may not take for the game: the menus' Esc and Enter, and F11. */
bool Input_KeyReserved(int key);
/* The key pressed this frame (for rebinding), or 0. */
int Input_KeyPressedNow(void);

typedef struct {
    bool up, down, left, right; /* with key repeat, and the stick as a d-pad */
    bool confirm, back, start;
    bool join;                  /* the device's own "I'm in" button */
} MenuPress;

void Input_Update(void); /* once a frame, before anything reads input */
bool Input_DeviceReady(int dev);
PadInput Input_Read(int dev, bool keyboardAlone);
MenuPress Input_Menu(int dev);
MenuPress Input_MenuAny(void);
/* The device behind the last confirm/start/join that Input_MenuAny saw: the
 * player who picked a one-player mode. */
int Input_MenuDevice(void);
/* The pad's View/Back button held for a second: drop out of a game. */
bool Input_HeldLeave(int dev);

const char *Input_DeviceName(int dev);
/* The key or button for an action on a device, for prompts: "SPACE", "A". */
const char *Input_ButtonName(int dev, int btn, bool keyboardAlone);
bool Input_IsPad(int dev);

#endif
