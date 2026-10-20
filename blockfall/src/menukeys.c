#include "menukeys.h"

#define K(act, key, mods, rep) {act, KB_KEY, key, mods, rep}
#define P(act, btn, rep) {act, KB_PAD, btn, 0, rep}

static const KeyBind kBinds[] = {
    K(KA_UI_UP, IK_W, 0, true), K(KA_UI_DOWN, IK_S, 0, true),
    K(KA_UI_LEFT, IK_A, 0, true), K(KA_UI_RIGHT, IK_D, 0, true),
    K(KA_SCROLL_UP, IK_W, 0, true), K(KA_SCROLL_DOWN, IK_S, 0, true),
    K(BA_BOARD_PREV, IK_LEFT_BRACKET, 0, false), P(BA_BOARD_PREV, PB_LB, false),
    K(BA_BOARD_NEXT, IK_RIGHT_BRACKET, 0, false), P(BA_BOARD_NEXT, PB_RB, false),
    K(BA_PERIOD, IK_T, 0, false), P(BA_PERIOD, PB_Y, false),
    K(BA_BOARD_TURN, IK_X, 0, false), P(BA_BOARD_TURN, PB_X, false),
    K(BA_CLEAR, IK_DELETE, 0, false), K(BA_CLEAR, IK_BACKSPACE, 0, false), P(BA_CLEAR, PB_Y, false),
};

void MenuKeys_Install(InputState *st) {
    Keymap_SetGameBinds(kBinds, (int)(sizeof(kBinds) / sizeof(kBinds[0])));
    if (!st) return;
    Input_SetKeyRepeat(st, IK_W, true);
    Input_SetKeyRepeat(st, IK_A, true);
    Input_SetKeyRepeat(st, IK_S, true);
    Input_SetKeyRepeat(st, IK_D, true);
}
