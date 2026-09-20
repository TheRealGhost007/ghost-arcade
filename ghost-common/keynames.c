#include "keynames.h"
#include "raylib.h"
#include <string.h>
#include <stddef.h>

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
