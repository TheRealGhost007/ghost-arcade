#ifndef PERSIST_H
#define PERSIST_H

#include <stdbool.h>
#include "game.h"

#define KEYNAME_LEN 16

/* Key bindings are stored as human-readable names (e.g. "LEFT", "A") rather
 * than raw keycodes so the config file in $XDG_CONFIG_HOME is easy for a
 * user to hand-edit. input.c maps names <-> raylib key codes. */
typedef struct {
    char up[KEYNAME_LEN];
    char up2[KEYNAME_LEN];
    char down[KEYNAME_LEN];
    char down2[KEYNAME_LEN];
    char left[KEYNAME_LEN];
    char left2[KEYNAME_LEN];
    char right[KEYNAME_LEN];
    char right2[KEYNAME_LEN];
    char jump[KEYNAME_LEN];
    char grapple[KEYNAME_LEN];
    char pause[KEYNAME_LEN];
    char restart[KEYNAME_LEN];
    char quit[KEYNAME_LEN];
    int volumePercent; /* 0-100 */
    bool audioEnabled;
    bool scanlines;    /* CRT scanline overlay on the field */
} Settings;

typedef struct {
    long highScore;
    int bestLevel;
} SaveData;

void Settings_Default(Settings *s);
/* Loads from $XDG_CONFIG_HOME/girderclimb/config.ini, falling back to
 * defaults for any missing/invalid fields. Returns true if a file was
 * found and read. */
bool Settings_Load(Settings *s);
bool Settings_Save(const Settings *s);

void SaveData_Default(SaveData *d);
/* Loads from $XDG_DATA_HOME/girderclimb/save.dat. Returns true if a file
 * was found and read. */
bool SaveData_Load(SaveData *d);
bool SaveData_Save(const SaveData *d);

#endif
