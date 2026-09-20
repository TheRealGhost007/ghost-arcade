#ifndef GAMEPAD_H
#define GAMEPAD_H

/* Controller support for every Ghost Arcade game, without touching any game's
 * input code: a connected gamepad is mapped onto the DEFAULT keys, and the
 * games' key queries are routed through here by the macros at the bottom.
 *
 *   D-pad or left stick  -> Left / Right / Up / Down arrows
 *   A (south)            -> Space and Enter  (confirm, fire, grapple, possess)
 *   B (east)             -> Escape           (back)
 *   X (west)             -> X                (jump in Girderclimb)
 *   Y (north)            -> C                (hold in Blockfall)
 *   Start                -> P                (pause)
 *   Back / Select        -> R                (restart)
 *
 * A game whose player has rebound a key away from its default simply does not
 * respond to the pad for that action; the keyboard always works. Call
 * Pad_Update() once at the top of each frame. */

void Pad_Update(void);
bool Pad_IsAvailable(void);
/* The keyboard query, or the pad's mapped equivalent. */
bool Pad_KeyDown(int key);
bool Pad_KeyPressed(int key);

/* Include this header AFTER raylib.h in a game's main.c: from there on
 * IsKeyDown / IsKeyPressed also see the controller. gamepad.c itself is
 * compiled without these macros. */
#ifndef GAMEPAD_IMPLEMENTATION
#define IsKeyDown(key) Pad_KeyDown(key)
#define IsKeyPressed(key) Pad_KeyPressed(key)
#endif

#endif
