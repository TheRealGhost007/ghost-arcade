#include "safefile.h"
#include "persist.h"
#include "uiinput.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#define APP_DIRNAME "blockfall"

static void JoinPath(char *out, size_t outSize, const char *a, const char *b) {
    int n = snprintf(out, outSize, "%s/%s", a, b);
    if (n < 0 || (size_t)n >= outSize) out[0] = '\0'; /* too long: no path rather than a wrong one */
}

/* Returns $XDG_CONFIG_HOME/blockfall or $XDG_DATA_HOME/blockfall,
 * creating intermediate directories as needed. `kind` is "config" or
 * "share" and picks the XDG var + fallback under $HOME. */
static bool GetAppDir(char *out, size_t outSize, const char *kind) {
    const char *xdgVar = (strcmp(kind, "config") == 0) ? getenv("XDG_CONFIG_HOME")
                                                         : getenv("XDG_DATA_HOME");
    char base[512];
    if (xdgVar && xdgVar[0] != '\0') {
        snprintf(base, sizeof(base), "%s", xdgVar);
    } else {
        const char *home = getenv("HOME");
        if (!home || home[0] == '\0') return false;
        const char *fallback = (strcmp(kind, "config") == 0) ? ".config" : ".local/share";
        snprintf(base, sizeof(base), "%s/%s", home, fallback);
    }

    /* mkdir -p for base (it usually already exists) and base/blockfall. */
    char cursor[600] = {0};
    for (const char *p = base; ; p++) {
        if (*p == '/' || *p == '\0') {
            size_t len = (size_t)(p - base);
            if (len > 0) {
                memcpy(cursor, base, len);
                cursor[len] = '\0';
                mkdir(cursor, 0755); /* ignore EEXIST and any other error here */
            }
            if (*p == '\0') break;
        }
    }
    JoinPath(out, outSize, base, APP_DIRNAME);
    if (out[0] == '\0') return false;
    if (mkdir(out, 0755) != 0 && errno != EEXIST) {
        /* Non-fatal: writes will just fail later and we keep in-memory state. */
    }
    return true;
}

static bool GetFilePath(char *out, size_t outSize, const char *kind, const char *filename) {
    char dir[512];
    if (!GetAppDir(dir, sizeof(dir), kind)) return false;
    JoinPath(out, outSize, dir, filename);
    return out[0] != '\0';
}

bool Persist_DataFile(const char *filename, char *out, size_t outSize) {
    return GetFilePath(out, outSize, "share", filename);
}

/* ------------------------------------------------- names in the file */

static const char *const kPadNames[PB_COUNT] = {
    [PB_A] = "A", [PB_B] = "B", [PB_X] = "X", [PB_Y] = "Y", [PB_LB] = "LB", [PB_RB] = "RB", [PB_LT] = "LT", [PB_RT] = "RT",
    [PB_VIEW] = "VIEW", [PB_MENU] = "MENU", [PB_LS] = "LS", [PB_RS] = "RS",
    [PB_UP] = "UP", [PB_DOWN] = "DOWN", [PB_LEFT] = "LEFT", [PB_RIGHT] = "RIGHT",
};

const char *Persist_PadName(int button) { return (button >= 0 && button < PB_COUNT) ? kPadNames[button] : KEY_NONE_NAME; }

int Persist_PadFromName(const char *name) {
    for (int b = 0; name && b < PB_COUNT; b++)
        if (strcmp(kPadNames[b], name) == 0) return b;
    return PAD_NONE;
}

static const char *const kSoftNames[SOFT_SPEEDS] = {"slow", "normal", "fast", "instant"};
static const char *const kGridNames[GRID_STYLES] = {"on", "faint", "off"};

const char *Persist_SoftName(int speed) { return kSoftNames[speed >= 0 && speed < SOFT_SPEEDS ? speed : SOFT_NORMAL]; }
const char *Persist_GridName(int grid) { return kGridNames[grid >= 0 && grid < GRID_STYLES ? grid : GRID_ON]; }

static int Lookup(const char *const *names, int count, const char *value, int fallback) {
    for (int i = 0; i < count; i++)
        if (strcmp(names[i], value) == 0) return i;
    return fallback;
}

/* The pad's config.ini keys, in controls.h's action order. */
static const char *const kPadKeys[CONTROL_ACTIONS] = {
    "pad_left", "pad_right", "pad_soft_drop", "pad_hard_drop", "pad_rotate", "pad_rotate_left", "pad_hold", "pad_pause", "pad_restart",
};
/* The buttons the pad always had (A hard drop, d-pad Up rotate, Y hold,
 * Start pause, B the pause menu, View restart), plus X for Rotate left and
 * LB as Hold's second (REWORK_PLAN.md, decision 5). */
static const int kPadDefaults[CONTROL_ACTIONS][2] = {
    {PB_LEFT, PAD_NONE}, {PB_RIGHT, PAD_NONE}, {PB_DOWN, PAD_NONE}, {PB_A, PAD_NONE}, {PB_UP, PAD_NONE},
    {PB_X, PAD_NONE},    {PB_Y, PB_LB},        {PB_MENU, PB_B},     {PB_VIEW, PAD_NONE},
};

static int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void Settings_Default(Settings *s) {
    memset(s, 0, sizeof(*s));
    snprintf(s->left, KEYNAME_LEN, "LEFT");
    snprintf(s->left2, KEYNAME_LEN, "A");
    snprintf(s->right, KEYNAME_LEN, "RIGHT");
    snprintf(s->right2, KEYNAME_LEN, "D");
    snprintf(s->softDrop, KEYNAME_LEN, "DOWN");
    snprintf(s->softDrop2, KEYNAME_LEN, "S");
    snprintf(s->rotate, KEYNAME_LEN, "UP");
    snprintf(s->rotate2, KEYNAME_LEN, "W");
    snprintf(s->hardDrop, KEYNAME_LEN, "SPACE");
    snprintf(s->hold, KEYNAME_LEN, "C");
    snprintf(s->pause, KEYNAME_LEN, "P");
    snprintf(s->restart, KEYNAME_LEN, "R");
    snprintf(s->quit, KEYNAME_LEN, "ESCAPE");
    snprintf(s->hardDrop2, KEYNAME_LEN, KEY_NONE_NAME);
    snprintf(s->rotateLeft, KEYNAME_LEN, "Z");
    snprintf(s->rotateLeft2, KEYNAME_LEN, KEY_NONE_NAME);
    snprintf(s->hold2, KEYNAME_LEN, KEY_NONE_NAME);
    snprintf(s->restart2, KEYNAME_LEN, KEY_NONE_NAME);
    memcpy(s->pad, kPadDefaults, sizeof(s->pad));
    s->repeatDelayMs = REPEAT_DELAY_DEFAULT;
    s->repeatSpeedMs = REPEAT_SPEED_DEFAULT;
    s->softDropSpeed = SOFT_NORMAL;
    s->grid = GRID_ON;
    s->autoPause = true;
    s->volumePercent = 70;
    s->audioEnabled = true;
    s->setupMode = 0;
    s->setupPreset = 0;
    s->setupWidth = 10;
    s->setupHeight = 20;
    s->readyGo = true;
    s->ghostPiece = GHOST_OUTLINE;
}

static char *TrimInPlace(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                        s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
    return s;
}

static void SetField(Settings *s, const char *key, const char *value) {
    if (strcmp(key, "left") == 0) snprintf(s->left, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "left2") == 0) snprintf(s->left2, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "right") == 0) snprintf(s->right, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "right2") == 0) snprintf(s->right2, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "soft_drop") == 0) snprintf(s->softDrop, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "soft_drop2") == 0) snprintf(s->softDrop2, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "rotate") == 0) snprintf(s->rotate, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "rotate2") == 0) snprintf(s->rotate2, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "hard_drop") == 0) snprintf(s->hardDrop, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "hold") == 0) snprintf(s->hold, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "setup_mode") == 0) { int v = atoi(value); s->setupMode = v < 0 ? 0 : v; }
    else if (strcmp(key, "setup_preset") == 0) { int v = atoi(value); s->setupPreset = v < 0 ? 0 : v; }
    else if (strcmp(key, "setup_width") == 0) { int v = atoi(value); s->setupWidth = v < 6 ? 6 : (v > 24 ? 24 : v); }
    else if (strcmp(key, "setup_height") == 0) { int v = atoi(value); s->setupHeight = v < 12 ? 12 : (v > 36 ? 36 : v); }
    else if (strcmp(key, "pause") == 0) snprintf(s->pause, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "restart") == 0) snprintf(s->restart, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "quit") == 0) snprintf(s->quit, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "hard_drop2") == 0) snprintf(s->hardDrop2, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "rotate_left") == 0) snprintf(s->rotateLeft, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "rotate_left2") == 0) snprintf(s->rotateLeft2, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "hold2") == 0) snprintf(s->hold2, KEYNAME_LEN, "%s", value);
    else if (strcmp(key, "restart2") == 0) snprintf(s->restart2, KEYNAME_LEN, "%s", value);
    else if (strncmp(key, "pad_", 4) == 0) {
        for (int a = 0; a < CONTROL_ACTIONS; a++) {
            size_t n = strlen(kPadKeys[a]);
            if (strncmp(key, kPadKeys[a], n) != 0) continue;
            int slot = key[n] == '\0' ? 0 : (strcmp(key + n, "2") == 0 ? 1 : -1);
            if (slot >= 0) s->pad[a][slot] = Persist_PadFromName(value);
        }
    }
    else if (strcmp(key, "das_ms") == 0) s->repeatDelayMs = Clamp(atoi(value), REPEAT_DELAY_MIN, REPEAT_DELAY_MAX);
    else if (strcmp(key, "arr_ms") == 0) s->repeatSpeedMs = Clamp(atoi(value), REPEAT_SPEED_MIN, REPEAT_SPEED_MAX);
    else if (strcmp(key, "soft_drop_speed") == 0) s->softDropSpeed = Lookup(kSoftNames, SOFT_SPEEDS, value, SOFT_NORMAL);
    else if (strcmp(key, "grid") == 0) s->grid = Lookup(kGridNames, GRID_STYLES, value, GRID_ON);
    else if (strcmp(key, "block_patterns") == 0) s->blockPatterns = (atoi(value) != 0);
    else if (strcmp(key, "auto_pause") == 0) s->autoPause = (atoi(value) != 0);
    else if (strcmp(key, "show_howto") == 0) s->showHowTo = (atoi(value) != 0);
    else if (strcmp(key, "volume") == 0) {
        int v = atoi(value);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        s->volumePercent = v;
    } else if (strcmp(key, "audio_enabled") == 0) {
        s->audioEnabled = (atoi(value) != 0);
    } else if (strcmp(key, "reduced_motion") == 0) {
        s->reducedMotion = (atoi(value) != 0);
    } else if (strcmp(key, "hide_controls") == 0) {
        s->hideControls = (atoi(value) != 0);
    } else if (strcmp(key, "piece_snap") == 0) {
        s->pieceSnap = (atoi(value) != 0);
    } else if (strcmp(key, "ready_go") == 0) {
        s->readyGo = (atoi(value) != 0);
    } else if (strcmp(key, "quick_restart") == 0) {
        s->quickRestart = (atoi(value) != 0);
    } else if (strcmp(key, "resume_countdown") == 0) {
        s->resumeCountdown = (atoi(value) != 0);
    } else if (strcmp(key, "ghost_piece") == 0) {
        /* Any look a later build adds (it shows the ghost) is kept as it is,
         * so saving here never loses it. */
        int v = atoi(value);
        s->ghostPiece = v < 0 ? 0 : (v > 9 ? 9 : v);
    }
}

/* Is `name` one of the key fields other than `except`? */
static bool KeyUsed(const Settings *s, const char *name, const char *except) {
    const char *const fields[] = {s->left, s->left2, s->right, s->right2, s->softDrop, s->softDrop2, s->rotate, s->rotate2, s->hardDrop,
                                  s->hardDrop2, s->rotateLeft, s->rotateLeft2, s->hold, s->hold2, s->pause, s->quit, s->restart, s->restart2};
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++)
        if (fields[i] != except && strcmp(fields[i], name) == 0) return true;
    return false;
}

/* Enter, Space and the pad's A pick menu items (the pause menu reads Pause
 * and Restart before its buttons), so Settings never lets Pause or Restart
 * hold them (controls.c). A hand-edited config.ini may: that slot becomes
 * none. When that leaves the action with no key (or button) at all, it goes
 * back to how Blockfall came: its first key (P, R; Menu, View) in the first
 * slot, or, when another action has that one by now (the file was already
 * past mending), its second (Esc; B) in the second, or none when that one is
 * taken too. */
static bool MenuKeyName(const char *key) { return strcmp(key, "ENTER") == 0 || strcmp(key, "SPACE") == 0; }
static bool NoKeyName(const char *key) { return key[0] == '\0' || strcmp(key, KEY_NONE_NAME) == 0; }

static void MendMenuKeyPair(Settings *s, char *first, char *second, const char *firstDefault, const char *secondDefault) {
    bool mended = false;
    if (MenuKeyName(first)) { snprintf(first, KEYNAME_LEN, KEY_NONE_NAME); mended = true; }
    if (MenuKeyName(second)) { snprintf(second, KEYNAME_LEN, KEY_NONE_NAME); mended = true; }
    if (!mended || !NoKeyName(first) || !NoKeyName(second)) return;
    if (!KeyUsed(s, firstDefault, first)) snprintf(first, KEYNAME_LEN, "%s", firstDefault);
    else if (!NoKeyName(secondDefault) && !KeyUsed(s, secondDefault, second)) snprintf(second, KEYNAME_LEN, "%s", secondDefault);
}

static bool PadUsed(const Settings *s, int button, int exceptAction, int exceptSlot) {
    for (int a = 0; a < CONTROL_ACTIONS; a++)
        for (int slot = 0; slot < 2; slot++)
            if ((a != exceptAction || slot != exceptSlot) && s->pad[a][slot] == button) return true;
    return false;
}

static void MendMenuPadPair(Settings *s, int action) {
    bool mended = false;
    for (int slot = 0; slot < 2; slot++)
        if (s->pad[action][slot] == PB_A) { s->pad[action][slot] = PAD_NONE; mended = true; }
    if (!mended || s->pad[action][0] != PAD_NONE || s->pad[action][1] != PAD_NONE) return;
    for (int slot = 0; slot < 2; slot++) {
        int fallback = kPadDefaults[action][slot];
        if (fallback != PAD_NONE && !PadUsed(s, fallback, action, slot)) { s->pad[action][slot] = fallback; return; }
    }
}

static void MendMenuKeys(Settings *s) {
    enum { PAUSE = 7, RESTART = 8 }; /* controls.h's ACT_PAUSE and ACT_RESTART (kPadKeys' order) */
    Settings d;
    Settings_Default(&d);
    MendMenuKeyPair(s, s->pause, s->quit, d.pause, d.quit);
    MendMenuKeyPair(s, s->restart, s->restart2, d.restart, d.restart2);
    MendMenuPadPair(s, PAUSE);
    MendMenuPadPair(s, RESTART);
}

bool Settings_Load(Settings *s) {
    Settings_Default(s);
    char path[600];
    if (!GetFilePath(path, sizeof(path), "config", "config.ini")) return false;

    FILE *f = fopen(path, "r");
    if (!f) return false;

    char line[128];
    bool rotateLeftSeen = false;
    while (fgets(line, sizeof(line), f)) {
        char *trimmed = TrimInPlace(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#') continue;
        char *eq = strchr(trimmed, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = TrimInPlace(trimmed);
        char *value = TrimInPlace(eq + 1);
        if (strcmp(key, "rotate_left") == 0) rotateLeftSeen = true;
        if (value[0] != '\0') SetField(s, key, value);
    }
    fclose(f);
    /* A config.ini from before Rotate left: it takes Z, unless Z is already
     * one of your keys (then it starts with none, and the key you have keeps
     * doing what it did). */
    if (!rotateLeftSeen && KeyUsed(s, s->rotateLeft, s->rotateLeft)) snprintf(s->rotateLeft, KEYNAME_LEN, KEY_NONE_NAME);
    MendMenuKeys(s);
    return true;
}

bool Settings_Save(const Settings *s) {
    char path[600];
    if (!GetFilePath(path, sizeof(path), "config", "config.ini")) return false;

    FILE *f = SafeFile_Open(path);
    if (!f) return false;

    fprintf(f,
        "# blockfall settings\n"
        "# Key names: A-Z, 0-9, LEFT, RIGHT, UP, DOWN, SPACE, ESCAPE, ENTER,\n"
        "# LEFT_SHIFT, RIGHT_SHIFT, LEFT_CONTROL, TAB, etc. Each action may\n"
        "# have a primary and a secondary binding; NONE is no key. The pad's\n"
        "# buttons: A B X Y LB RB LT RT VIEW MENU LS RS UP DOWN LEFT RIGHT.\n"
        "left=%s\n"
        "left2=%s\n"
        "right=%s\n"
        "right2=%s\n"
        "soft_drop=%s\n"
        "soft_drop2=%s\n"
        "rotate=%s\n"
        "rotate2=%s\n"
        "hard_drop=%s\n"
        "hold=%s\n"
        "pause=%s\n"
        "restart=%s\n"
        "quit=%s\n"
        "volume=%d\n"
        "audio_enabled=%d\n"
        "setup_mode=%d\n"
        "setup_preset=%d\n"
        "setup_width=%d\n"
        "setup_height=%d\n"
        "reduced_motion=%d\n"
        "hide_controls=%d\n"
        "piece_snap=%d\n"
        "ready_go=%d\n"
        "quick_restart=%d\n"
        "resume_countdown=%d\n"
        "ghost_piece=%d\n"
        "hard_drop2=%s\n"
        "rotate_left=%s\n"
        "rotate_left2=%s\n"
        "hold2=%s\n"
        "restart2=%s\n",
        s->left, s->left2, s->right, s->right2, s->softDrop, s->softDrop2,
        s->rotate, s->rotate2, s->hardDrop, s->hold, s->pause, s->restart, s->quit,
        s->volumePercent, s->audioEnabled ? 1 : 0, s->setupMode, s->setupPreset, s->setupWidth, s->setupHeight,
        s->reducedMotion ? 1 : 0, s->hideControls ? 1 : 0, s->pieceSnap ? 1 : 0, s->readyGo ? 1 : 0,
        s->quickRestart ? 1 : 0, s->resumeCountdown ? 1 : 0, s->ghostPiece,
        s->hardDrop2, s->rotateLeft, s->rotateLeft2, s->hold2, s->restart2);
    /* The pad (3.0.0): pad_left=LEFT, pad_left2=NONE, ... */
    for (int a = 0; a < CONTROL_ACTIONS; a++) {
        fprintf(f, "%s=%s\n", kPadKeys[a], Persist_PadName(s->pad[a][0]));
        fprintf(f, "%s2=%s\n", kPadKeys[a], Persist_PadName(s->pad[a][1]));
    }
    fprintf(f,
        "das_ms=%d\n"
        "arr_ms=%d\n"
        "soft_drop_speed=%s\n"
        "grid=%s\n"
        "block_patterns=%d\n"
        "auto_pause=%d\n"
        "show_howto=%d\n",
        s->repeatDelayMs, s->repeatSpeedMs, Persist_SoftName(s->softDropSpeed), Persist_GridName(s->grid), s->blockPatterns ? 1 : 0,
        s->autoPause ? 1 : 0, s->showHowTo ? 1 : 0);

    SafeFile_Close(f);
    return true;
}

void SaveData_Default(SaveData *d) {
    d->highScore = 0;
    d->highLevel = 1;
    d->highLines = 0;
}

bool SaveData_Load(SaveData *d) {
    SaveData_Default(d);
    char path[600];
    if (!GetFilePath(path, sizeof(path), "share", "save.dat")) return false;

    FILE *f = fopen(path, "r");
    if (!f) return false;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char *trimmed = TrimInPlace(line);
        char *eq = strchr(trimmed, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = TrimInPlace(trimmed);
        char *value = TrimInPlace(eq + 1);
        if (strcmp(key, "high_score") == 0) d->highScore = atol(value);
        else if (strcmp(key, "high_level") == 0) d->highLevel = atoi(value);
        else if (strcmp(key, "high_lines") == 0) d->highLines = atoi(value);
    }
    fclose(f);
    return true;
}

bool SaveData_Save(const SaveData *d) {
    char path[600];
    if (!GetFilePath(path, sizeof(path), "share", "save.dat")) return false;

    FILE *f = SafeFile_Open(path);
    if (!f) return false;

    fprintf(f,
        "high_score=%ld\n"
        "high_level=%d\n"
        "high_lines=%d\n",
        d->highScore, d->highLevel, d->highLines);

    SafeFile_Close(f);
    return true;
}
