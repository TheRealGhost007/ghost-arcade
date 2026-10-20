#ifndef MENUKEYS_H
#define MENUKEYS_H

#include "uikeymap.h"

/* Blockfall's own keys for its menus, on top of the button kit's built-in
 * table (uikeymap.h): W A S D move as the arrows do (they always have), and
 * a few actions of its own. No raylib. */
enum {
    BA_BOARD_PREV = KA_GAME, /* [ or LB: the board before (Play setup, Scores) */
    BA_BOARD_NEXT,           /* ] or RB */
    BA_PERIOD,               /* T or pad Y: Scores' next period */
    BA_BOARD_TURN,           /* X or pad X: Scores' next board (LB/RB change its tabs there) */
    BA_CLEAR,                /* Delete, Backspace or pad Y: Settings clears a second key or button */
};

/* Installs the table (Keymap_SetGameBinds) and makes W A S D repeat while
 * held in the reader (st may be NULL). */
void MenuKeys_Install(InputState *st);

#endif
