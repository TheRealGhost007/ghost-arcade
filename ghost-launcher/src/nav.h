#ifndef NAV_H
#define NAV_H

#include <stdbool.h>

/* The one rule for Back: every screen remembers the one it was opened from,
 * and Back (Esc, pad B, the [< Back] button, the mouse's back button) goes
 * there. Stats opened from the Arcade menu goes back to the menu; opened
 * with L on the hall, back to the hall. The bottom is always the hall.
 * Screens are the caller's numbers. No raylib. */

#define NAV_DEPTH 8

typedef struct {
    int screens[NAV_DEPTH];
    int depth;
} Nav;

void Nav_Init(Nav *n, int root);
int Nav_Top(const Nav *n);
int Nav_Depth(const Nav *n);
/* The screen under the top one (the root when there is only one). */
int Nav_Under(const Nav *n);
bool Nav_Contains(const Nav *n, int screen);
/* Opens a screen on top. One that is already open lower down is gone back
 * to instead, so the stack never loops. False if it is full. */
bool Nav_Push(Nav *n, int screen);
/* One level back; false (and nothing changes) at the root. */
bool Nav_Back(Nav *n);
/* Straight back to the root. */
void Nav_Home(Nav *n);

#endif
