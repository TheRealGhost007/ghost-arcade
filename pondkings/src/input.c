#include "input.h"
#include "keynames.h"
#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define STICK_DEAD 0.35f
#define MENU_STICK 0.6f
#define REPEAT_FIRST 0.35f
#define REPEAT_NEXT 0.12f

typedef struct {
    int dir;        /* -1 none, else 0 up 1 down 2 left 3 right: the stick acting as a d-pad in menus */
    float held;     /* seconds that way */
    float nextFire;
    bool fired;
    float leaveHeld;
} PadMenu;

static PadMenu sPad[4];

static const int kDefaults[2][KA_COUNT] = {
    {KEY_W, KEY_S, KEY_A, KEY_D, KEY_SPACE, KEY_LEFT_SHIFT, KEY_E},
    {KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_SLASH, KEY_PERIOD, KEY_COMMA},
};
static int sKeys[2][KA_COUNT] = {
    {KEY_W, KEY_S, KEY_A, KEY_D, KEY_SPACE, KEY_LEFT_SHIFT, KEY_E},
    {KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_SLASH, KEY_PERIOD, KEY_COMMA},
};

void Input_DefaultKeys(int half, int out[KA_COUNT]) { memcpy(out, kDefaults[half ? 1 : 0], sizeof(kDefaults[0])); }
void Input_SetKeys(int half, const int keys[KA_COUNT]) {
    for (int a = 0; a < KA_COUNT; a++) sKeys[half ? 1 : 0][a] = (keys[a] > 0 && !Input_KeyReserved(keys[a])) ? keys[a] : kDefaults[half ? 1 : 0][a];
}
const int *Input_Keys(int half) { return sKeys[half ? 1 : 0]; }
bool Input_KeyReserved(int key) { return key == KEY_ESCAPE || key == KEY_ENTER || key == KEY_KP_ENTER || key == KEY_F11 || key <= 0; }

const char *Input_KeyLabel(int key) {
    static char ring[4][24]; /* a few at once, so one sentence can name two keys */
    static int next = 0;
    char *buf = ring[next];
    next = (next + 1) % 4;
    switch (key) {
        case KEY_SLASH: return "/";
        case KEY_PERIOD: return ".";
        case KEY_COMMA: return ",";
        case KEY_SEMICOLON: return ";";
        case KEY_APOSTROPHE: return "'";
        default: break;
    }
    const char *n = Keys_NameFromCode(key);
    if (!n || !n[0]) return "?";
    snprintf(buf, sizeof(ring[0]), "%s", n);
    for (char *c = buf; *c; c++) if (*c == '_') *c = ' ';
    return buf;
}

int Input_KeyPressedNow(void) {
    int k = GetKeyPressed();
    return k > 0 ? k : 0;
}

/* Extra keys for a player alone at the keyboard, unless a binding uses them. */
static bool Extra(int key) {
    for (int h = 0; h < 2; h++)
        for (int a = 0; a < KA_COUNT; a++) if (sKeys[h][a] == key) return false;
    return true;
}

bool Input_IsPad(int dev) { return dev >= DEV_PAD0 && dev <= DEV_PAD3; }
static int PadIndex(int dev) { return dev - DEV_PAD0; }

bool Input_DeviceReady(int dev) {
    if (dev == DEV_KEYS_LEFT || dev == DEV_KEYS_RIGHT) return true;
    return Input_IsPad(dev) && IsGamepadAvailable(PadIndex(dev));
}

static bool KeyHit(int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); }

void Input_Update(void) {
    float dt = GetFrameTime();
    for (int p = 0; p < 4; p++) {
        PadMenu *m = &sPad[p];
        m->fired = false;
        if (!IsGamepadAvailable(p)) { memset(m, 0, sizeof(*m)); m->dir = -1; continue; }
        float x = GetGamepadAxisMovement(p, GAMEPAD_AXIS_LEFT_X), y = GetGamepadAxisMovement(p, GAMEPAD_AXIS_LEFT_Y);
        int dir = -1;
        if (fabsf(y) >= fabsf(x) && fabsf(y) > MENU_STICK) dir = y < 0 ? 0 : 1;
        else if (fabsf(x) > MENU_STICK) dir = x < 0 ? 2 : 3;
        if (dir != m->dir) {
            m->dir = dir;
            m->held = 0.0f;
            m->nextFire = REPEAT_FIRST;
            m->fired = dir >= 0;
        } else if (dir >= 0) {
            m->held += dt;
            if (m->held >= m->nextFire) { m->fired = true; m->nextFire += REPEAT_NEXT; }
        }
        if (IsGamepadButtonDown(p, GAMEPAD_BUTTON_MIDDLE_LEFT)) m->leaveHeld += dt;
        else m->leaveHeld = 0.0f;
    }
}

/* 8-way keys to the 32 directions: right is 1, then clockwise. */
static uint8_t KeyDir(bool up, bool down, bool left, bool right) {
    int dx = (right ? 1 : 0) - (left ? 1 : 0), dy = (down ? 1 : 0) - (up ? 1 : 0);
    if (dx == 0 && dy == 0) return 0;
    static const uint8_t kEight[3][3] = {
        /* dy -1      0      1  */
        {21, 17, 13}, /* dx -1: up-left, left, down-left */
        {25, 0, 9},   /* dx  0: up, -, down */
        {29, 1, 5},   /* dx  1: up-right, right, down-right */
    };
    return kEight[dx + 1][dy + 1];
}

static uint8_t StickDir(float x, float y) {
    if (x * x + y * y < STICK_DEAD * STICK_DEAD) return 0;
    float a = atan2f(y, x); /* screen terms: y down, clockwise from right */
    int idx = (int)lroundf(a / (2.0f * PI / DIR_COUNT));
    idx = ((idx % DIR_COUNT) + DIR_COUNT) % DIR_COUNT;
    return (uint8_t)(idx + 1);
}

static bool Down(int k) { return IsKeyDown(k) || IsKeyPressed(k); } /* a tap inside one frame still counts */

PadInput Input_Read(int dev, bool alone) {
    PadInput in = {0, 0};
    if (dev == DEV_KEYS_LEFT || dev == DEV_KEYS_RIGHT) {
        bool left = dev == DEV_KEYS_LEFT || alone, right = dev == DEV_KEYS_RIGHT || alone;
        const int *L = sKeys[0], *R = sKeys[1];
        bool up = (left && Down(L[KA_UP])) || (right && Down(R[KA_UP]));
        bool down = (left && Down(L[KA_DOWN])) || (right && Down(R[KA_DOWN]));
        bool l = (left && Down(L[KA_LEFT])) || (right && Down(R[KA_LEFT]));
        bool r = (left && Down(L[KA_RIGHT])) || (right && Down(R[KA_RIGHT]));
        in.dir = KeyDir(up, down, l, r);
        bool shoot = (left && Down(L[KA_SHOOT])) || (right && Down(R[KA_SHOOT])) ||
                     (alone && ((Extra(KEY_Z) && Down(KEY_Z)) || (Extra(KEY_J) && Down(KEY_J))));
        bool pass = (left && Down(L[KA_PASS])) || (right && Down(R[KA_PASS])) ||
                    (alone && ((Extra(KEY_X) && Down(KEY_X)) || (Extra(KEY_K) && Down(KEY_K))));
        bool check = (left && Down(L[KA_CHECK])) || (right && Down(R[KA_CHECK])) ||
                     (alone && ((Extra(KEY_C) && Down(KEY_C)) || (Extra(KEY_L) && Down(KEY_L))));
        in.buttons = (uint8_t)((shoot ? BTN_SHOOT : 0) | (pass ? BTN_PASS : 0) | (check ? BTN_CHECK : 0));
        return in;
    }
    if (!Input_DeviceReady(dev)) return in;
    int p = PadIndex(dev);
    bool du = IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_UP), dd = IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_DOWN);
    bool dl = IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_LEFT), dr = IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_RIGHT);
    in.dir = KeyDir(du, dd, dl, dr);
    if (in.dir == 0) in.dir = StickDir(GetGamepadAxisMovement(p, GAMEPAD_AXIS_LEFT_X), GetGamepadAxisMovement(p, GAMEPAD_AXIS_LEFT_Y));
    bool shoot = IsGamepadButtonDown(p, GAMEPAD_BUTTON_RIGHT_FACE_LEFT) || IsGamepadButtonDown(p, GAMEPAD_BUTTON_RIGHT_TRIGGER_2) ||
                 IsGamepadButtonPressed(p, GAMEPAD_BUTTON_RIGHT_FACE_LEFT);
    bool pass = IsGamepadButtonDown(p, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) || IsGamepadButtonPressed(p, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    bool check = IsGamepadButtonDown(p, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) || IsGamepadButtonPressed(p, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
    in.buttons = (uint8_t)((shoot ? BTN_SHOOT : 0) | (pass ? BTN_PASS : 0) | (check ? BTN_CHECK : 0));
    return in;
}

MenuPress Input_Menu(int dev) {
    MenuPress m;
    memset(&m, 0, sizeof(m));
    if (dev == DEV_KEYS_LEFT) {
        m.up = KeyHit(KEY_W); m.down = KeyHit(KEY_S); m.left = KeyHit(KEY_A); m.right = KeyHit(KEY_D);
        m.confirm = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
        m.back = IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE);
        m.start = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
        m.join = IsKeyPressed(sKeys[0][KA_SHOOT]);
        return m;
    }
    if (dev == DEV_KEYS_RIGHT) {
        m.up = KeyHit(KEY_UP); m.down = KeyHit(KEY_DOWN); m.left = KeyHit(KEY_LEFT); m.right = KeyHit(KEY_RIGHT);
        m.confirm = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SLASH);
        m.back = IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE);
        m.start = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
        m.join = IsKeyPressed(sKeys[1][KA_SHOOT]);
        return m;
    }
    if (!Input_DeviceReady(dev)) return m;
    int p = PadIndex(dev);
    PadMenu *pm = &sPad[p];
    m.up = IsGamepadButtonPressed(p, GAMEPAD_BUTTON_LEFT_FACE_UP) || (pm->fired && pm->dir == 0);
    m.down = IsGamepadButtonPressed(p, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || (pm->fired && pm->dir == 1);
    m.left = IsGamepadButtonPressed(p, GAMEPAD_BUTTON_LEFT_FACE_LEFT) || (pm->fired && pm->dir == 2);
    m.right = IsGamepadButtonPressed(p, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || (pm->fired && pm->dir == 3);
    m.confirm = IsGamepadButtonPressed(p, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    m.back = IsGamepadButtonPressed(p, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
    m.start = IsGamepadButtonPressed(p, GAMEPAD_BUTTON_MIDDLE_RIGHT);
    m.join = m.confirm;
    return m;
}

static int sMenuDevice = DEV_KEYS_LEFT;
int Input_MenuDevice(void) { return sMenuDevice; }

MenuPress Input_MenuAny(void) {
    MenuPress all;
    memset(&all, 0, sizeof(all));
    for (int d = 0; d < DEV_COUNT; d++) {
        MenuPress m = Input_Menu(d);
        if ((m.confirm || m.start || m.join) && !(all.confirm || all.start || all.join)) sMenuDevice = d;
        all.up |= m.up; all.down |= m.down; all.left |= m.left; all.right |= m.right;
        all.confirm |= m.confirm; all.back |= m.back; all.start |= m.start; all.join |= m.join;
    }
    return all;
}

bool Input_HeldLeave(int dev) { return Input_IsPad(dev) && sPad[PadIndex(dev)].leaveHeld >= 1.0f; }

const char *Input_DeviceName(int dev) {
    switch (dev) {
        case DEV_KEYS_LEFT: return "Keyboard (WASD)";
        case DEV_KEYS_RIGHT: return "Keyboard (arrows)";
        case DEV_PAD0: return "Pad 1";
        case DEV_PAD1: return "Pad 2";
        case DEV_PAD2: return "Pad 3";
        case DEV_PAD3: return "Pad 4";
        default: return "?";
    }
}

const char *Input_ButtonName(int dev, int btn, bool alone) {
    if (Input_IsPad(dev)) return btn == BTN_SHOOT ? "X" : btn == BTN_PASS ? "A" : "B";
    (void)alone;
    const int *k = sKeys[dev == DEV_KEYS_RIGHT ? 1 : 0];
    return Input_KeyLabel(btn == BTN_SHOOT ? k[KA_SHOOT] : btn == BTN_PASS ? k[KA_PASS] : k[KA_CHECK]);
}
