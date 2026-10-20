/* The raylib half of input.h: fills an InputRaw from the real devices. This
 * file must not include gamepad.h, whose macros would make the pad's A read
 * as the Enter and Space keys. */
#include "raylib.h"
#include "input.h"
#include <ctype.h>
#include <string.h>

static const int kRaylibKey[IK_COUNT] = {
    [IK_LEFT] = KEY_LEFT, [IK_RIGHT] = KEY_RIGHT, [IK_UP] = KEY_UP, [IK_DOWN] = KEY_DOWN,
    [IK_ENTER] = KEY_ENTER, [IK_SPACE] = KEY_SPACE, [IK_ESCAPE] = KEY_ESCAPE,
    [IK_BACKSPACE] = KEY_BACKSPACE, [IK_DELETE] = KEY_DELETE, [IK_TAB] = KEY_TAB,
    [IK_HOME] = KEY_HOME, [IK_END] = KEY_END, [IK_PAGE_UP] = KEY_PAGE_UP, [IK_PAGE_DOWN] = KEY_PAGE_DOWN,
    [IK_F11] = KEY_F11,
};

static int RaylibKey(int k) {
    if (k >= IK_A && k <= IK_Z) return KEY_A + (k - IK_A);
    return kRaylibKey[k];
}

static const int kRaylibButton[PB_COUNT] = {
    [PB_A] = GAMEPAD_BUTTON_RIGHT_FACE_DOWN, [PB_B] = GAMEPAD_BUTTON_RIGHT_FACE_RIGHT,
    [PB_X] = GAMEPAD_BUTTON_RIGHT_FACE_LEFT, [PB_Y] = GAMEPAD_BUTTON_RIGHT_FACE_UP,
    [PB_LB] = GAMEPAD_BUTTON_LEFT_TRIGGER_1, [PB_RB] = GAMEPAD_BUTTON_RIGHT_TRIGGER_1,
    [PB_LT] = GAMEPAD_BUTTON_LEFT_TRIGGER_2, [PB_RT] = GAMEPAD_BUTTON_RIGHT_TRIGGER_2,
    [PB_VIEW] = GAMEPAD_BUTTON_MIDDLE_LEFT, [PB_MENU] = GAMEPAD_BUTTON_MIDDLE_RIGHT,
    [PB_LS] = GAMEPAD_BUTTON_LEFT_THUMB, [PB_RS] = GAMEPAD_BUTTON_RIGHT_THUMB,
    [PB_UP] = GAMEPAD_BUTTON_LEFT_FACE_UP, [PB_DOWN] = GAMEPAD_BUTTON_LEFT_FACE_DOWN,
    [PB_LEFT] = GAMEPAD_BUTTON_LEFT_FACE_LEFT, [PB_RIGHT] = GAMEPAD_BUTTON_LEFT_FACE_RIGHT,
};

static const int kRaylibMouse[MB_COUNT] = {
    [MB_LEFT] = MOUSE_BUTTON_LEFT, [MB_RIGHT] = MOUSE_BUTTON_RIGHT, [MB_MIDDLE] = MOUSE_BUTTON_MIDDLE,
    [MB_BACK] = MOUSE_BUTTON_SIDE, [MB_FORWARD] = MOUSE_BUTTON_EXTRA,
};

static bool NameHas(const char *name, const char *needle) {
    size_t n = strlen(needle);
    for (const char *p = name; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == n) return true;
    }
    return false;
}

/* The same choice ghost-common/gamepad.c makes (raylib also lists keyboards
 * and mice as gamepads, so the real controller is found by name). That
 * function is private to gamepad.c, and the shared code stays untouched, so
 * the rule is repeated here: keep the two lists in step. */
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

static void ReadKeyboard(InputRaw *raw) {
    for (int k = 0; k < IK_COUNT; k++) {
        int key = RaylibKey(k);
        raw->keyDown[k] = IsKeyDown(key);
        raw->keyPressed[k] = IsKeyPressed(key);
    }
    raw->shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    raw->ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    raw->alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    int c;
    while ((c = GetCharPressed()) > 0) {
        if (raw->charCount < INPUT_MAX_CHARS) raw->chars[raw->charCount++] = c;
    }
}

/* A pad is not tied to a window the way a keyboard is: every program that
 * reads it would react at once (the launcher scrolling under a game). Only
 * the focused window listens, as in gamepad.c. */
static void ReadPad(InputRaw *raw) {
    static int sPad = -1, sFrames = 0;
    if (sPad < 0 || !IsGamepadAvailable(sPad) || (++sFrames % 60) == 0) sPad = FindPad();
    if (sPad < 0 || !raw->focused) return;
    for (int b = 0; b < PB_COUNT; b++) raw->padDown[b] = IsGamepadButtonDown(sPad, kRaylibButton[b]);
    float ax = GetGamepadAxisMovement(sPad, GAMEPAD_AXIS_LEFT_X), ay = GetGamepadAxisMovement(sPad, GAMEPAD_AXIS_LEFT_Y);
    raw->padDown[PB_LEFT] |= ax < -0.5f;
    raw->padDown[PB_RIGHT] |= ax > 0.5f;
    raw->padDown[PB_UP] |= ay < -0.5f;
    raw->padDown[PB_DOWN] |= ay > 0.5f;
}

/* winscale.c maps the pointer into logical coordinates. The movement test
 * uses the raw delta, so a window resize alone does not count as moving. */
static void ReadMouse(InputRaw *raw) {
    Vector2 p = GetMousePosition();
    raw->mouseX = p.x;
    raw->mouseY = p.y;
    Vector2 d = GetMouseDelta();
    raw->mouseMoved = d.x != 0.0f || d.y != 0.0f;
    for (int b = 0; b < MB_COUNT; b++) {
        raw->mouseDown[b] = IsMouseButtonDown(kRaylibMouse[b]);
        raw->mousePressed[b] = IsMouseButtonPressed(kRaylibMouse[b]);
        raw->mouseReleased[b] = IsMouseButtonReleased(kRaylibMouse[b]);
    }
    raw->wheel = GetMouseWheelMove();
}

void InputRL_Read(InputRaw *raw) {
    memset(raw, 0, sizeof(*raw));
    raw->focused = IsWindowFocused();
    ReadKeyboard(raw);
    ReadPad(raw);
    ReadMouse(raw);
}
