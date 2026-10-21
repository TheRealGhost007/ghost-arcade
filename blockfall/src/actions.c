#include "actions.h"
#include "controls.h"
#include <string.h>

void Actions_Keys(const Settings *s, KeyCodeFn code, PlayKeys *out) {
    memset(out, 0, sizeof(*out));
    for (int a = 0; a < ACT_COUNT; a++)
        for (int slot = 0; slot < CONTROL_SLOTS; slot++) {
            const char *k = Controls_KeyConst(s, a, slot);
            out->key[a][slot] = Controls_IsNone(k) ? 0 : code(k);
            out->pad[a][slot] = Controls_Pad(s, a, slot);
        }
    out->escTaken = Controls_EscTaken(s);
    out->menuTaken = Controls_MenuTaken(s);
}

static bool Down(const KeyReader *kb, int code) { return code > 0 && kb->down(code); }
static bool Pressed(const KeyReader *kb, int code) { return code > 0 && kb->pressed(code); }
static bool PadDown(const Input *in, int b) { return b >= 0 && b < PB_COUNT && in->padDown[b]; }
static bool PadPressed(const Input *in, int b) { return b >= 0 && b < PB_COUNT && in->pad[b]; }

/* An action's keys or buttons: held, or pressed this frame. */
static bool Held(const PlayKeys *k, const KeyReader *kb, const Input *in, int a) {
    return Down(kb, k->key[a][0]) || Down(kb, k->key[a][1]) || PadDown(in, k->pad[a][0]) || PadDown(in, k->pad[a][1]);
}
static bool Hit(const PlayKeys *k, const KeyReader *kb, const Input *in, int a) {
    return Pressed(kb, k->key[a][0]) || Pressed(kb, k->key[a][1]) || PadPressed(in, k->pad[a][0]) || PadPressed(in, k->pad[a][1]);
}

void Actions_Read(const PlayKeys *k, const KeyReader *kb, const Input *pad, bool quiet, PlayInput *out) {
    memset(out, 0, sizeof(*out));
    out->left = Held(k, kb, pad, ACT_LEFT);
    out->right = Held(k, kb, pad, ACT_RIGHT);
    if (out->left && out->right) out->left = out->right = false;
    out->softDrop = Held(k, kb, pad, ACT_SOFT);
    if (quiet) return;

    out->rotate = Hit(k, kb, pad, ACT_ROTATE);
    out->rotateLeft = Hit(k, kb, pad, ACT_ROTATE_LEFT);
    out->rotateHalf = Hit(k, kb, pad, ACT_ROTATE_180);
    out->undo = Hit(k, kb, pad, ACT_UNDO);
    out->hardDrop = Hit(k, kb, pad, ACT_HARD);
    out->hold = Hit(k, kb, pad, ACT_HOLD);
    /* The Pause row: its first key and button pause, its second ones also
     * go back. Esc and Menu always pause too, unless a config.ini from
     * before gave them to another action: then they do that action, as
     * Settings and the card say. */
    out->pause = Pressed(kb, k->key[ACT_PAUSE][0]) || PadPressed(pad, k->pad[ACT_PAUSE][0]) || (PadPressed(pad, PB_MENU) && !k->menuTaken);
    out->back = Pressed(kb, k->key[ACT_PAUSE][1]) || (Pressed(kb, ACT_KEY_ESCAPE) && !k->escTaken) || PadPressed(pad, k->pad[ACT_PAUSE][1]);
    out->restartKey = Pressed(kb, k->key[ACT_RESTART][0]) || Pressed(kb, k->key[ACT_RESTART][1]);
    out->restartPad = PadPressed(pad, k->pad[ACT_RESTART][0]) || PadPressed(pad, k->pad[ACT_RESTART][1]);
    out->mute = Pressed(kb, ACT_KEY_M);
    out->volumeDown = Pressed(kb, ACT_KEY_MINUS) || Pressed(kb, ACT_KEY_KP_SUBTRACT);
    out->volumeUp = Pressed(kb, ACT_KEY_EQUAL) || Pressed(kb, ACT_KEY_KP_ADD);
    out->fps = Pressed(kb, ACT_KEY_F3);
}
