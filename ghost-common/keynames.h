#ifndef KEYNAMES_H
#define KEYNAMES_H

/* Key bindings are stored in config files as human-readable names ("LEFT",
 * "A", "SPACE") so they are easy to hand-edit. This is the one table that
 * maps those names to raylib key codes and back; each game builds its own
 * KeyMap struct from it. */

/* Unknown or empty names give KEY_NULL (0), which raylib treats as never
 * pressed, so a typo in a hand-edited config degrades safely. */
int Keys_CodeFromName(const char *name);

/* Reverse lookup for in-game rebinding. NULL if the key isn't in the table
 * (e.g. function keys), so the caller can keep waiting for a mappable one. */
const char *Keys_NameFromCode(int code);

#endif
