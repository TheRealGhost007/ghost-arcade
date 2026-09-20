#define GAMEPAD_IMPLEMENTATION
#include "raylib.h"
#include "gamepad.h"
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#define VKEYS 10
static const int kKeys[VKEYS] = {KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_SPACE, KEY_ENTER, KEY_ESCAPE, KEY_X, KEY_C, KEY_P};
/* KEY_R is handled as an eleventh entry below. */
static bool sNow[VKEYS + 1], sPrev[VKEYS + 1];
static bool sAvailable = false;
static int sPad = -1; /* which raylib gamepad slot is the real controller */

static bool NameHas(const char *name, const char *needle) {
    size_t n = strlen(needle);
    for (const char *p = name; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == n) return true;
    }
    return false;
}

/* raylib lists keyboards, mice and their "consumer control" interfaces as
 * gamepads too (a wireless keyboard dongle is often slot 0), so the real
 * controller has to be found by name: known controller words win, and known
 * non-controller words are skipped. */
static int FindPad(void) {
    static const char *const kYes[] = {"xbox", "controller", "gamepad", "playstation", "dualsense", "dualshock", "wireless controller", "pro controller", "8bitdo", "joy-con", "stadia", "steam", "switch", "logitech f", NULL};
    static const char *const kNo[] = {"keyboard", "consumer", "mouse", "ornata", "receiver", "touchpad", "system control", "hotkey", "webcam", "kbd", "2.4g", NULL};
    int fallback = -1;
    for (int i = 0; i < 4; i++) {
        if (!IsGamepadAvailable(i)) continue;
        const char *name = GetGamepadName(i);
        if (!name) continue;
        bool no = false, yes = false;
        for (int k = 0; kNo[k]; k++) if (NameHas(name, kNo[k])) no = true;
        for (int k = 0; kYes[k]; k++) if (NameHas(name, kYes[k])) yes = true;
        if (yes && !NameHas(name, "consumer")) return i;
        if (!no && fallback < 0) fallback = i;
    }
    return fallback;
}

static int Slot(int key) {
    for (int i = 0; i < VKEYS; i++) if (kKeys[i] == key) return i;
    if (key == KEY_R) return VKEYS;
    return -1;
}

void Pad_Update(void) {
    for (int i = 0; i <= VKEYS; i++) sPrev[i] = sNow[i];
    /* Re-scan about once a second, and at once if the remembered slot went away. */
    static int sFrames = 0;
    if (sPad < 0 || !IsGamepadAvailable(sPad) || (++sFrames % 60) == 0) sPad = FindPad();
    sAvailable = sPad >= 0;
    for (int i = 0; i <= VKEYS; i++) sNow[i] = false;
    if (!sAvailable) return;
    /* A controller is not tied to a window the way a keyboard is: every open
     * program that reads it would react at once (the launcher moving while you
     * play a game). Only the focused window listens. */
    if (!IsWindowFocused()) return;
    const int pad = sPad;
    float ax = GetGamepadAxisMovement(pad, GAMEPAD_AXIS_LEFT_X), ay = GetGamepadAxisMovement(pad, GAMEPAD_AXIS_LEFT_Y);
    sNow[0] = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_LEFT) || ax < -0.5f;
    sNow[1] = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || ax > 0.5f;
    sNow[2] = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_UP) || ay < -0.5f;
    sNow[3] = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || ay > 0.5f;
    bool a = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    sNow[4] = a;
    sNow[5] = a;
    sNow[6] = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
    sNow[7] = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);
    sNow[8] = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_UP);
    sNow[9] = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_MIDDLE_RIGHT);
    sNow[VKEYS] = IsGamepadButtonDown(pad, GAMEPAD_BUTTON_MIDDLE_LEFT);
}

bool Pad_IsAvailable(void) { return sAvailable; }

bool Pad_KeyDown(int key) {
    if (IsKeyDown(key)) return true;
    int s = Slot(key);
    return s >= 0 && sNow[s];
}

bool Pad_KeyPressed(int key) {
    if (IsKeyPressed(key)) return true;
    int s = Slot(key);
    return s >= 0 && sNow[s] && !sPrev[s];
}
