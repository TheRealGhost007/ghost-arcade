#ifndef PERSIST_H
#define PERSIST_H

#include <stdbool.h>

#define KEYNAME_LEN 16

/* Key bindings are stored as human-readable names (e.g. "LEFT", "A") rather
 * than raw keycodes so the config file in $XDG_CONFIG_HOME is easy for a
 * user to hand-edit. input.c maps names <-> raylib key codes. */
typedef struct {
    char left[KEYNAME_LEN];
    char left2[KEYNAME_LEN];
    char right[KEYNAME_LEN];
    char right2[KEYNAME_LEN];
    char softDrop[KEYNAME_LEN];
    char softDrop2[KEYNAME_LEN];
    char rotate[KEYNAME_LEN];
    char rotate2[KEYNAME_LEN];
    char hardDrop[KEYNAME_LEN];
    char hold[KEYNAME_LEN];
    char pause[KEYNAME_LEN];
    char restart[KEYNAME_LEN];
    char quit[KEYNAME_LEN];
    int volumePercent; /* 0-100 */
    bool audioEnabled;
    /* The last play setup, so "Start game" opens on what you played last. */
    int setupMode;    /* GameMode */
    int setupPreset;  /* index into the board presets in main.c; the last one is Custom */
    int setupWidth, setupHeight; /* used when the preset is Custom */
} Settings;

typedef struct {
    long highScore;
    int highLevel;
    int highLines;
} SaveData;

void Settings_Default(Settings *s);
/* Loads from $XDG_CONFIG_HOME/blockfall/config.ini, falling back to
 * defaults for any missing/invalid fields. Returns true if a file was
 * found and read. */
bool Settings_Load(Settings *s);
bool Settings_Save(const Settings *s);

void SaveData_Default(SaveData *d);
/* Loads from $XDG_DATA_HOME/blockfall/save.dat. Returns true if a file
 * was found and read. */
bool SaveData_Load(SaveData *d);
bool SaveData_Save(const SaveData *d);

#endif
