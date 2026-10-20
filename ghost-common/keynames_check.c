/* Builds only where raylib is installed (every game and the launcher, not
 * the headless tests): proves the key numbers keynames.c carries so it can
 * build without raylib are raylib's. A mismatch stops the build. */
#include "raylib.h"
#include "keynames.h"

#define SAME(name, value) _Static_assert(name == (value), #name " in keynames.c is not raylib's")
SAME(KEY_NULL, 0);
SAME(KEY_A, 65); SAME(KEY_Z, 90);
SAME(KEY_ZERO, 48); SAME(KEY_NINE, 57);
SAME(KEY_SPACE, 32); SAME(KEY_APOSTROPHE, 39); SAME(KEY_COMMA, 44); SAME(KEY_PERIOD, 46);
SAME(KEY_SLASH, 47); SAME(KEY_SEMICOLON, 59);
SAME(KEY_ESCAPE, 256); SAME(KEY_ENTER, 257); SAME(KEY_TAB, 258); SAME(KEY_BACKSPACE, 259);
SAME(KEY_RIGHT, 262); SAME(KEY_LEFT, 263); SAME(KEY_DOWN, 264); SAME(KEY_UP, 265);
SAME(KEY_LEFT_SHIFT, 340); SAME(KEY_LEFT_CONTROL, 341); SAME(KEY_LEFT_ALT, 342);
SAME(KEY_RIGHT_SHIFT, 344); SAME(KEY_RIGHT_CONTROL, 345); SAME(KEY_RIGHT_ALT, 346);

