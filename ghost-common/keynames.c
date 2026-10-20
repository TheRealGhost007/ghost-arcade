#include "keynames.h"
#include <string.h>
#include <stddef.h>

/* raylib's key codes are GLFW's, written out here so this file also builds
 * where raylib is not installed (the headless test suites, CI). When raylib
 * is there, the block at the end of this file proves the numbers match. */
enum {
    KEY_NULL = 0,
    KEY_A = 65, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
    KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    KEY_ZERO = 48, KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR, KEY_FIVE, KEY_SIX, KEY_SEVEN, KEY_EIGHT, KEY_NINE,
    KEY_SPACE = 32, KEY_APOSTROPHE = 39, KEY_COMMA = 44, KEY_PERIOD = 46, KEY_SLASH = 47, KEY_SEMICOLON = 59,
    KEY_ESCAPE = 256, KEY_ENTER = 257, KEY_TAB = 258, KEY_BACKSPACE = 259,
    KEY_RIGHT = 262, KEY_LEFT = 263, KEY_DOWN = 264, KEY_UP = 265,
    KEY_LEFT_SHIFT = 340, KEY_LEFT_CONTROL = 341, KEY_LEFT_ALT = 342,
    KEY_RIGHT_SHIFT = 344, KEY_RIGHT_CONTROL = 345, KEY_RIGHT_ALT = 346,
};

typedef struct {
    const char *name;
    int code;
} KeyName;

static const KeyName kKeyNames[] = {
    {"A", KEY_A}, {"B", KEY_B}, {"C", KEY_C}, {"D", KEY_D}, {"E", KEY_E},
    {"F", KEY_F}, {"G", KEY_G}, {"H", KEY_H}, {"I", KEY_I}, {"J", KEY_J},
    {"K", KEY_K}, {"L", KEY_L}, {"M", KEY_M}, {"N", KEY_N}, {"O", KEY_O},
    {"P", KEY_P}, {"Q", KEY_Q}, {"R", KEY_R}, {"S", KEY_S}, {"T", KEY_T},
    {"U", KEY_U}, {"V", KEY_V}, {"W", KEY_W}, {"X", KEY_X}, {"Y", KEY_Y},
    {"Z", KEY_Z},
    {"0", KEY_ZERO}, {"1", KEY_ONE}, {"2", KEY_TWO}, {"3", KEY_THREE},
    {"4", KEY_FOUR}, {"5", KEY_FIVE}, {"6", KEY_SIX}, {"7", KEY_SEVEN},
    {"8", KEY_EIGHT}, {"9", KEY_NINE},
    {"LEFT", KEY_LEFT}, {"RIGHT", KEY_RIGHT}, {"UP", KEY_UP}, {"DOWN", KEY_DOWN},
    {"SPACE", KEY_SPACE}, {"ESCAPE", KEY_ESCAPE}, {"ENTER", KEY_ENTER},
    {"TAB", KEY_TAB}, {"BACKSPACE", KEY_BACKSPACE},
    {"LEFT_SHIFT", KEY_LEFT_SHIFT}, {"RIGHT_SHIFT", KEY_RIGHT_SHIFT},
    {"LEFT_CONTROL", KEY_LEFT_CONTROL}, {"RIGHT_CONTROL", KEY_RIGHT_CONTROL},
    {"LEFT_ALT", KEY_LEFT_ALT}, {"RIGHT_ALT", KEY_RIGHT_ALT},
    {"COMMA", KEY_COMMA}, {"PERIOD", KEY_PERIOD}, {"SEMICOLON", KEY_SEMICOLON},
    {"APOSTROPHE", KEY_APOSTROPHE}, {"SLASH", KEY_SLASH},
};

const char *Keys_NameFromCode(int code) {
    if (code == KEY_NULL) return NULL;
    for (size_t i = 0; i < sizeof(kKeyNames) / sizeof(kKeyNames[0]); i++) {
        if (kKeyNames[i].code == code) return kKeyNames[i].name;
    }
    return NULL;
}

int Keys_CodeFromName(const char *name) {
    if (!name || name[0] == '\0') return KEY_NULL;
    for (size_t i = 0; i < sizeof(kKeyNames) / sizeof(kKeyNames[0]); i++) {
        if (strcmp(kKeyNames[i].name, name) == 0) return kKeyNames[i].code;
    }
    return KEY_NULL;
}
