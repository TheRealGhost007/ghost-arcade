/* The screenshot harness: runs the real launcher (src/main.c, renamed
 * launcher_main by main_wrap.c) in a HIDDEN window with input from a script,
 * so screens can be checked without a window on the desktop or a key sent to
 * it. Every raylib input query the launcher makes is linked to a __wrap_
 * function here (-Wl,--wrap=..., see the Makefile's `shots` target), the
 * audio device is never opened, fullscreen only flips a flag, and
 * fake_launch.c stands in for launch.c so no game, install or ghost-sync
 * ever runs. Time is the script's: every frame is exactly 1/60 s, so two
 * runs of one script draw the same pixels.
 *
 * Pictures come from winscale's logical 960x640 render texture, read just
 * before it is closed each frame, so they work although the window is never
 * shown. run.sh builds the temp HOME this needs and checks with hyprctl that
 * the window was never mapped.
 *
 * Env: SHOTS_SCRIPT (the script), SHOTS_OUT (where shots go, default .),
 * SHOTS_ADMIN=1|0 (admin mode on or off whatever this machine's lock says;
 * unset = the real lock),
 * SHOTS_FPS (real frame cap, default 0 = as fast as possible), SHOTS_HOLD_MS
 * (pause this long after the first frame, default 400, so run.sh has time
 * to look for the window), SHOTS_VERBOSE=1 (log every event).
 *
 * Script, one command per line, '#' comments. Time only moves with `wait`
 * and the commands that hold something down; an event lands on the frame it
 * is scheduled for, and `shot` / `expect` see that frame's result:
 *   wait N                 N frames pass
 *   tap KEY [KEY...]       press together (modifiers first), release a frame later
 *   hold KEY N             hold for N frames (hold-to-repeat)
 *   down KEY / up KEY
 *   text STRING            type it, key by key
 *   pad on|off             plug a fake Xbox pad in (slot 0) or out
 *   pad tap|down|up BTN    A B X Y LB RB LT RT VIEW MENU LS RS UP DOWN LEFT RIGHT
 *   pad hold BTN N
 *   stick X Y              the left stick, -1..1
 *   mouse X Y              move the pointer (logical 960x640 coordinates)
 *   click [BTN] [X Y]      left (default), right, middle, back or forward
 *   mdown BTN / mup BTN
 *   wheel N                N notches, positive = away from you
 *   focus on|off           the window gains or loses focus
 *   clip TEXT              what the clipboard holds
 *   write PATH TEXT        write a file under the temp HOME ("\n" = newline),
 *                          e.g. a game changing prefs.ini behind the launcher
 *   gameexit               the pretend game the launcher started exits
 *   install ok|fail        the pretend install finishes
 *   shot NAME              save this frame as NAME.png
 *   expect ui NAME | expect selected N | expect fullscreen 0|1
 *   expect trophy N        the game the Trophies screen shows
 *   expect profile TEXT    the name being typed (TEXT may be empty)
 *   expect file PATH TEXT  | expect nofile PATH TEXT (the file lacks TEXT)
 *   expect launched TEXT | expect notlaunched TEXT
 *   expect launches N      how many games were started in all
 *   expect count N TEXT    how many fake launch/install/sync lines hold TEXT
 *   expect prefs KEY N     the launcher's copy of a prefs.ini value
 *                          (music_volume, screen_shake, reduced_flashing, fullscreen)
 *   expect app KEY VALUE   main_wrap.c's drv_app: dialog, dialogtitle, focus,
 *                          hover, toast, checking, noanswer, statstop,
 *                          trophytop, depth, under, cursor, choice, games,
 *                          device, look:LABEL (normal hover focused pressed
 *                          disabled absent; LABEL "X" is the close button),
 *                          chip:LABEL (a strip button: "CAP LOOK"), status
 *                          (the card's line), note, pointer (hidden|shown),
 *                          hint (0 keys, 1 pad), hints (the same after
 *                          Settings > Button hints), settled (the row stopped),
 *                          board, boardcell (main_wrap.c lists them all)
 *   chmod PATH MODE        change a file's mode under the temp HOME (octal)
 *   quits                  this script makes the launcher quit before its end
 *                          (without it, quitting early fails the run)
 *   log TEXT               print where the launcher is
 *   close                  the window manager's close button
 *
 * SHOTS_QUIT_OK=1 lets any script quit early (the monkey's random input can
 * find the way out); SHOTS_GALLERY=1 runs the button kit's gallery
 * (gallery.c) instead of the launcher.
 */
#define _GNU_SOURCE
#include "raylib.h"
#include "rlgl.h"
#include "fake_launch.h"
#include "prefs.h"
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int launcher_main(void);
const char *drv_ui_name(void);
int drv_selected(void);
int drv_trophy_game(void);
const char *drv_profile_text(void);
void drv_describe(char *buf, size_t n);
void drv_app(const char *key, char *out, size_t n);
int gallery_main(void);

void __real_EndDrawing(void);
void __real_EndTextureMode(void);
void __real_PollInputEvents(void);
void __real_SetConfigFlags(unsigned int flags);
void __real_InitWindow(int width, int height, const char *title);
void __real_SetTargetFPS(int fps);
bool __real_IsWindowState(unsigned int flag);
RenderTexture2D __real_LoadRenderTexture(int width, int height);

/* ------------------------------------------------------------- the log */
static long sTick = 0;

static void Log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void Log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[shots t=%ld] ", sTick);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static void Where(const char *why) {
    char buf[600];
    drv_describe(buf, sizeof(buf));
    Log("%s: %s", why, buf);
}

/* ---------------------------------------------------------------- names */
typedef struct { const char *name; int key; char ch, shch; } KeyName;
static const KeyName kKeys[] = {
    {"A", KEY_A, 'a', 'A'}, {"B", KEY_B, 'b', 'B'}, {"C", KEY_C, 'c', 'C'}, {"D", KEY_D, 'd', 'D'},
    {"E", KEY_E, 'e', 'E'}, {"F", KEY_F, 'f', 'F'}, {"G", KEY_G, 'g', 'G'}, {"H", KEY_H, 'h', 'H'},
    {"I", KEY_I, 'i', 'I'}, {"J", KEY_J, 'j', 'J'}, {"K", KEY_K, 'k', 'K'}, {"L", KEY_L, 'l', 'L'},
    {"M", KEY_M, 'm', 'M'}, {"N", KEY_N, 'n', 'N'}, {"O", KEY_O, 'o', 'O'}, {"P", KEY_P, 'p', 'P'},
    {"Q", KEY_Q, 'q', 'Q'}, {"R", KEY_R, 'r', 'R'}, {"S", KEY_S, 's', 'S'}, {"T", KEY_T, 't', 'T'},
    {"U", KEY_U, 'u', 'U'}, {"V", KEY_V, 'v', 'V'}, {"W", KEY_W, 'w', 'W'}, {"X", KEY_X, 'x', 'X'},
    {"Y", KEY_Y, 'y', 'Y'}, {"Z", KEY_Z, 'z', 'Z'},
    {"0", KEY_ZERO, '0', ')'}, {"1", KEY_ONE, '1', '!'}, {"2", KEY_TWO, '2', '@'}, {"3", KEY_THREE, '3', '#'},
    {"4", KEY_FOUR, '4', '$'}, {"5", KEY_FIVE, '5', '%'}, {"6", KEY_SIX, '6', '^'}, {"7", KEY_SEVEN, '7', '&'},
    {"8", KEY_EIGHT, '8', '*'}, {"9", KEY_NINE, '9', '('},
    {"SPACE", KEY_SPACE, ' ', ' '}, {"MINUS", KEY_MINUS, '-', '_'}, {"EQUAL", KEY_EQUAL, '=', '+'},
    {"PERIOD", KEY_PERIOD, '.', '>'}, {"COMMA", KEY_COMMA, ',', '<'}, {"SLASH", KEY_SLASH, '/', '?'},
    {"BACKSLASH", KEY_BACKSLASH, '\\', '|'}, {"SEMICOLON", KEY_SEMICOLON, ';', ':'}, {"APOSTROPHE", KEY_APOSTROPHE, '\'', '"'},
    {"GRAVE", KEY_GRAVE, '`', '~'}, {"LEFT_BRACKET", KEY_LEFT_BRACKET, '[', '{'}, {"RIGHT_BRACKET", KEY_RIGHT_BRACKET, ']', '}'},
    {"F1", KEY_F1, 0, 0}, {"F2", KEY_F2, 0, 0}, {"F3", KEY_F3, 0, 0}, {"F4", KEY_F4, 0, 0}, {"F5", KEY_F5, 0, 0},
    {"F6", KEY_F6, 0, 0}, {"F7", KEY_F7, 0, 0}, {"F8", KEY_F8, 0, 0}, {"F9", KEY_F9, 0, 0}, {"F10", KEY_F10, 0, 0},
    {"F11", KEY_F11, 0, 0}, {"F12", KEY_F12, 0, 0},
    {"ENTER", KEY_ENTER, 0, 0}, {"KP_ENTER", KEY_KP_ENTER, 0, 0}, {"ESCAPE", KEY_ESCAPE, 0, 0}, {"TAB", KEY_TAB, 0, 0},
    {"BACKSPACE", KEY_BACKSPACE, 0, 0}, {"DELETE", KEY_DELETE, 0, 0}, {"INSERT", KEY_INSERT, 0, 0},
    {"HOME", KEY_HOME, 0, 0}, {"END", KEY_END, 0, 0}, {"PAGE_UP", KEY_PAGE_UP, 0, 0}, {"PAGE_DOWN", KEY_PAGE_DOWN, 0, 0},
    {"UP", KEY_UP, 0, 0}, {"DOWN", KEY_DOWN, 0, 0}, {"LEFT", KEY_LEFT, 0, 0}, {"RIGHT", KEY_RIGHT, 0, 0},
    {"LEFT_SHIFT", KEY_LEFT_SHIFT, 0, 0}, {"RIGHT_SHIFT", KEY_RIGHT_SHIFT, 0, 0},
    {"LEFT_CONTROL", KEY_LEFT_CONTROL, 0, 0}, {"RIGHT_CONTROL", KEY_RIGHT_CONTROL, 0, 0},
    {"LEFT_ALT", KEY_LEFT_ALT, 0, 0}, {"RIGHT_ALT", KEY_RIGHT_ALT, 0, 0},
};
#define NKEYNAMES ((int)(sizeof(kKeys) / sizeof(kKeys[0])))

static const KeyName *KeyByName(const char *s) {
    static const char *const kAlias[][2] = {{"SHIFT", "LEFT_SHIFT"}, {"CTRL", "LEFT_CONTROL"}, {"ALT", "LEFT_ALT"}, {"ESC", "ESCAPE"}, {"DEL", "DELETE"}};
    for (size_t i = 0; i < sizeof(kAlias) / sizeof(kAlias[0]); i++) if (strcasecmp(s, kAlias[i][0]) == 0) s = kAlias[i][1];
    for (int i = 0; i < NKEYNAMES; i++) if (strcasecmp(kKeys[i].name, s) == 0) return &kKeys[i];
    return NULL;
}

static const KeyName *KeyForChar(char c, bool *shift) {
    for (int i = 0; i < NKEYNAMES; i++) {
        if (kKeys[i].ch == c) { *shift = false; return &kKeys[i]; }
        if (kKeys[i].shch == c) { *shift = true; return &kKeys[i]; }
    }
    return NULL;
}

static bool IsModifier(int key) {
    return key == KEY_LEFT_SHIFT || key == KEY_RIGHT_SHIFT || key == KEY_LEFT_CONTROL || key == KEY_RIGHT_CONTROL ||
           key == KEY_LEFT_ALT || key == KEY_RIGHT_ALT;
}

static const struct { const char *name; int button; } kPadNames[] = {
    {"A", GAMEPAD_BUTTON_RIGHT_FACE_DOWN}, {"B", GAMEPAD_BUTTON_RIGHT_FACE_RIGHT}, {"X", GAMEPAD_BUTTON_RIGHT_FACE_LEFT},
    {"Y", GAMEPAD_BUTTON_RIGHT_FACE_UP}, {"LB", GAMEPAD_BUTTON_LEFT_TRIGGER_1}, {"RB", GAMEPAD_BUTTON_RIGHT_TRIGGER_1},
    {"LT", GAMEPAD_BUTTON_LEFT_TRIGGER_2}, {"RT", GAMEPAD_BUTTON_RIGHT_TRIGGER_2}, {"VIEW", GAMEPAD_BUTTON_MIDDLE_LEFT},
    {"MENU", GAMEPAD_BUTTON_MIDDLE_RIGHT}, {"LS", GAMEPAD_BUTTON_LEFT_THUMB}, {"RS", GAMEPAD_BUTTON_RIGHT_THUMB},
    {"UP", GAMEPAD_BUTTON_LEFT_FACE_UP}, {"DOWN", GAMEPAD_BUTTON_LEFT_FACE_DOWN}, {"LEFT", GAMEPAD_BUTTON_LEFT_FACE_LEFT},
    {"RIGHT", GAMEPAD_BUTTON_LEFT_FACE_RIGHT},
};

static int PadByName(const char *s) {
    for (size_t i = 0; i < sizeof(kPadNames) / sizeof(kPadNames[0]); i++) if (strcasecmp(s, kPadNames[i].name) == 0) return kPadNames[i].button;
    return -1;
}

static int MouseByName(const char *s) {
    static const struct { const char *name; int button; } k[] = {
        {"left", MOUSE_BUTTON_LEFT}, {"right", MOUSE_BUTTON_RIGHT}, {"middle", MOUSE_BUTTON_MIDDLE},
        {"back", MOUSE_BUTTON_SIDE}, {"forward", MOUSE_BUTTON_EXTRA},
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) if (strcasecmp(s, k[i].name) == 0) return k[i].button;
    return -1;
}

/* ------------------------------------------------------- device state */
#define NK 512
#define NPAD 32
#define NMOUSE 8
static unsigned char sKey[NK], sKeyPrev[NK];
static int sChars[64], sCharN, sCharI;
static int sKeyQueue[64], sKeyQN, sKeyQI;
static bool sPadOn = false;
static unsigned char sPad[NPAD], sPadPrev[NPAD];
static float sAxis[8];
static Vector2 sMouse = {480.0f, 320.0f}, sMousePrev = {480.0f, 320.0f};
static unsigned char sMouseBtn[NMOUSE], sMouseBtnPrev[NMOUSE];
static float sWheel = 0.0f;
static bool sFocused = true;
static char sClip[1024] = "";
static bool sBorderless = false;
static bool sCloseFlag = false;
static bool sVerbose = false;

/* -------------------------------------------------------------- events */
typedef enum {
    EV_KEYDOWN, EV_KEYUP, EV_CHAR, EV_PADON, EV_PADOFF, EV_PADDOWN, EV_PADUP, EV_STICK, EV_MOVE, EV_MDOWN, EV_MUP,
    EV_WHEEL, EV_FOCUS, EV_CLIP, EV_WRITE, EV_CHMOD, EV_GAMEEXIT, EV_INSTALL, EV_SHOT, EV_EXPECT, EV_LOG, EV_CLOSE,
} EvType;
typedef struct { long tick; int order; EvType type; int arg; float x, y; char text[512]; } Event;
static Event *sEv = NULL;
static int sEvN = 0, sEvCap = 0, sEvI = 0;
static long sT = 3;       /* where the script is writing; a few frames for the hall to settle */
static long sEndTick = 0;
static int sExpectFails = 0, sExpectsAsked = 0, sExpectsRun = 0, sShotsAsked = 0, sShotsWritten = 0;
static bool sMustQuit = false; /* the script's `quits` */

static Event *AddEv(long tick, EvType type, int arg, const char *text) {
    if (sEvN == sEvCap) {
        sEvCap = sEvCap ? sEvCap * 2 : 256;
        sEv = realloc(sEv, (size_t)sEvCap * sizeof(Event));
        if (!sEv) { perror("realloc"); exit(2); }
    }
    Event *e = &sEv[sEvN];
    memset(e, 0, sizeof(*e));
    e->tick = tick;
    e->order = sEvN++;
    e->type = type;
    e->arg = arg;
    snprintf(e->text, sizeof(e->text), "%s", text ? text : "");
    return e;
}

static int EvCmp(const void *a, const void *b) {
    const Event *x = a, *y = b;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    return x->order - y->order;
}

static void Die(int line, const char *what, const char *arg) {
    fprintf(stderr, "script line %d: %s%s%s\n", line, what, arg ? ": " : "", arg ? arg : "");
    exit(2);
}

/* Modifiers go down first and come up last, as fingers do; the key's
 * character is typed unless Ctrl or Alt is held. */
static void Tap(const KeyName **keys, int n, int holdTicks) {
    bool shift = false, chord = false;
    for (int i = 0; i < n - 1; i++) {
        AddEv(sT, EV_KEYDOWN, keys[i]->key, NULL);
        if (keys[i]->key == KEY_LEFT_SHIFT || keys[i]->key == KEY_RIGHT_SHIFT) shift = true;
        else if (IsModifier(keys[i]->key)) chord = true;
    }
    long t = sT + (n > 1 ? 1 : 0);
    const KeyName *last = keys[n - 1];
    AddEv(t, EV_KEYDOWN, last->key, NULL);
    char ch = shift ? last->shch : last->ch;
    if (ch && !chord) AddEv(t, EV_CHAR, (unsigned char)ch, NULL);
    AddEv(t + holdTicks, EV_KEYUP, last->key, NULL);
    for (int i = 0; i < n - 1; i++) AddEv(t + holdTicks + 1, EV_KEYUP, keys[i]->key, NULL);
    sT = t + holdTicks + (n > 1 ? 2 : 1);
}

static void ParseKeys(char *rest, int line, bool hold) {
    const KeyName *keys[8];
    int n = 0, holdTicks = 1;
    for (char *tok = strtok(rest, " \t"); tok; tok = strtok(NULL, " \t")) {
        if (hold && n > 0 && isdigit((unsigned char)tok[0])) { holdTicks = atoi(tok); break; }
        const KeyName *k = KeyByName(tok);
        if (!k) Die(line, "unknown key", tok);
        if (n < 8) keys[n++] = k;
    }
    if (n == 0) Die(line, "no key", NULL);
    if (holdTicks < 1) holdTicks = 1;
    Tap(keys, n, holdTicks);
}

static void ParseText(const char *s) {
    for (const char *c = s; *c; c++) {
        bool shift = false;
        const KeyName *k = KeyForChar(*c, &shift);
        if (!k) { AddEv(sT, EV_CHAR, (unsigned char)*c, NULL); sT += 2; continue; }
        const KeyName *keys[2];
        int n = 0;
        if (shift) keys[n++] = KeyByName("LEFT_SHIFT");
        keys[n++] = k;
        Tap(keys, n, 1);
    }
}

static void ParsePad(char *rest, int line) {
    char verb[16] = "", btn[16] = "";
    int ticks = 1;
    if (sscanf(rest, "%15s %15s %d", verb, btn, &ticks) < 1) Die(line, "pad what?", NULL);
    if (strcmp(verb, "on") == 0) { AddEv(sT++, EV_PADON, 0, NULL); return; }
    if (strcmp(verb, "off") == 0) { AddEv(sT++, EV_PADOFF, 0, NULL); return; }
    int b = PadByName(btn);
    if (b < 0) Die(line, "unknown pad button", btn);
    if (strcmp(verb, "down") == 0) { AddEv(sT++, EV_PADDOWN, b, NULL); return; }
    if (strcmp(verb, "up") == 0) { AddEv(sT++, EV_PADUP, b, NULL); return; }
    if (strcmp(verb, "tap") == 0) ticks = 1;
    else if (strcmp(verb, "hold") != 0) Die(line, "pad tap|hold|down|up|on|off", verb);
    if (ticks < 1) ticks = 1;
    AddEv(sT, EV_PADDOWN, b, NULL);
    AddEv(sT + ticks, EV_PADUP, b, NULL);
    sT += ticks + 1;
}

static void ParseClick(char *rest, int line) {
    char a[16] = "left";
    float x, y;
    int button = MOUSE_BUTTON_LEFT;
    if (sscanf(rest, "%f %f", &x, &y) == 2) {
        Event *e = AddEv(sT, EV_MOVE, 0, NULL); e->x = x; e->y = y;
    } else if (sscanf(rest, "%15s %f %f", a, &x, &y) >= 1) {
        button = MouseByName(a);
        if (button < 0) Die(line, "unknown mouse button", a);
        if (sscanf(rest, "%15s %f %f", a, &x, &y) == 3) { Event *e = AddEv(sT, EV_MOVE, 0, NULL); e->x = x; e->y = y; }
    }
    AddEv(sT + 1, EV_MDOWN, button, NULL);
    AddEv(sT + 2, EV_MUP, button, NULL);
    sT += 3;
}

static void ParseLine(char *p, int line) {
    while (*p == ' ' || *p == '\t') p++;
    p[strcspn(p, "\r\n")] = '\0';
    if (*p == '\0' || *p == '#') return;
    char cmd[32];
    int used = 0;
    if (sscanf(p, "%31s%n", cmd, &used) != 1) return;
    char *rest = p + used;
    while (*rest == ' ' || *rest == '\t') rest++;
    float x = 0.0f, y = 0.0f;

    if (strcmp(cmd, "wait") == 0) sT += atol(rest);
    else if (strcmp(cmd, "tap") == 0) ParseKeys(rest, line, false);
    else if (strcmp(cmd, "hold") == 0) ParseKeys(rest, line, true);
    else if (strcmp(cmd, "down") == 0 || strcmp(cmd, "up") == 0) {
        const KeyName *k = KeyByName(rest);
        if (!k) Die(line, "unknown key", rest);
        AddEv(sT++, cmd[0] == 'd' ? EV_KEYDOWN : EV_KEYUP, k->key, NULL);
    } else if (strcmp(cmd, "text") == 0) ParseText(rest);
    else if (strcmp(cmd, "pad") == 0) ParsePad(rest, line);
    else if (strcmp(cmd, "stick") == 0) {
        if (sscanf(rest, "%f %f", &x, &y) != 2) Die(line, "stick X Y", NULL);
        Event *e = AddEv(sT++, EV_STICK, 0, NULL); e->x = x; e->y = y;
    } else if (strcmp(cmd, "mouse") == 0) {
        if (sscanf(rest, "%f %f", &x, &y) != 2) Die(line, "mouse X Y", NULL);
        Event *e = AddEv(sT++, EV_MOVE, 0, NULL); e->x = x; e->y = y;
    } else if (strcmp(cmd, "click") == 0) ParseClick(rest, line);
    else if (strcmp(cmd, "mdown") == 0 || strcmp(cmd, "mup") == 0) {
        int b = MouseByName(rest[0] ? rest : "left");
        if (b < 0) Die(line, "unknown mouse button", rest);
        AddEv(sT++, cmd[1] == 'd' ? EV_MDOWN : EV_MUP, b, NULL);
    } else if (strcmp(cmd, "wheel") == 0) { Event *e = AddEv(sT++, EV_WHEEL, 0, NULL); e->y = (float)atof(rest); }
    else if (strcmp(cmd, "focus") == 0) AddEv(sT++, EV_FOCUS, strcmp(rest, "on") == 0, NULL);
    else if (strcmp(cmd, "clip") == 0) AddEv(sT, EV_CLIP, 0, rest);
    else if (strcmp(cmd, "write") == 0) AddEv(sT, EV_WRITE, line, rest);
    else if (strcmp(cmd, "chmod") == 0) AddEv(sT, EV_CHMOD, line, rest);
    else if (strcmp(cmd, "quits") == 0) sMustQuit = true;
    else if (strcmp(cmd, "gameexit") == 0) AddEv(sT++, EV_GAMEEXIT, 0, NULL);
    else if (strcmp(cmd, "install") == 0) AddEv(sT++, EV_INSTALL, strcmp(rest, "ok") == 0, NULL);
    else if (strcmp(cmd, "shot") == 0) { AddEv(sT, EV_SHOT, line, rest); sShotsAsked++; sT++; }
    else if (strcmp(cmd, "expect") == 0) { AddEv(sT, EV_EXPECT, line, rest); sExpectsAsked++; }
    else if (strcmp(cmd, "log") == 0) AddEv(sT, EV_LOG, line, rest);
    else if (strcmp(cmd, "close") == 0) AddEv(sT++, EV_CLOSE, 0, NULL);
    else Die(line, "unknown command", cmd);
}

static void LoadScript(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(2); }
    char line[1024];
    int n = 0;
    while (fgets(line, sizeof(line), f)) ParseLine(line, ++n);
    fclose(f);
    qsort(sEv, (size_t)sEvN, sizeof(Event), EvCmp);
    sEndTick = sT + 5;
    Log("script %s: %d events, %ld frames", path, sEvN, sEndTick);
}

/* ------------------------------------------------------- script actions */

static void Unescape(const char *in, char *out, size_t size) {
    size_t o = 0;
    for (const char *p = in; *p && o + 1 < size; p++) {
        if (p[0] == '\\' && p[1] == 'n') { out[o++] = '\n'; p++; }
        else if (p[0] == '\\' && p[1] == 't') { out[o++] = '\t'; p++; }
        else out[o++] = *p;
    }
    out[o] = '\0';
}

/* Only inside the temp HOME, so a script can never reach real files. */
static bool HomePath(const char *rel, char *out, size_t size) {
    const char *home = getenv("HOME");
    if (!home || !home[0] || rel[0] == '/' || strstr(rel, "..")) return false;
    snprintf(out, size, "%s/%s", home, rel);
    return true;
}

static void MakeParents(char *path) {
    for (char *p = path + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        mkdir(path, 0755);
        *p = '/';
    }
}

static void SplitFirst(const char *text, char *first, size_t firstSize, const char **rest) {
    size_t n = strcspn(text, " \t");
    snprintf(first, firstSize, "%.*s", (int)n, text);
    *rest = text + n;
    while (**rest == ' ' || **rest == '\t') (*rest)++;
}

static void DoWrite(const Event *e) {
    char rel[300], path[800], body[1024];
    const char *rest;
    SplitFirst(e->text, rel, sizeof(rel), &rest);
    if (!HomePath(rel, path, sizeof(path))) { Log("WRITE REFUSED (line %d): %s", e->arg, rel); sExpectFails++; return; }
    Unescape(rest, body, sizeof(body));
    MakeParents(path);
    FILE *f = fopen(path, "w");
    if (!f) { Log("WRITE FAILED (line %d): %s: %s", e->arg, path, strerror(errno)); sExpectFails++; return; }
    fputs(body, f);
    fclose(f);
    Log("wrote %s", rel);
}

static void DoChmod(const Event *e) {
    char rel[300], path[800];
    const char *rest;
    SplitFirst(e->text, rel, sizeof(rel), &rest);
    unsigned mode = (unsigned)strtoul(rest, NULL, 8);
    if (!HomePath(rel, path, sizeof(path)) || chmod(path, (mode_t)mode) != 0) {
        Log("CHMOD FAILED (line %d): %s", e->arg, e->text);
        sExpectFails++;
        return;
    }
    Log("chmod %o %s", mode, rel);
}

static bool FileHas(const char *rel, const char *want) {
    char path[800], buf[16384];
    if (!HomePath(rel, path, sizeof(path))) return false;
    FILE *f = fopen(path, "r");
    if (!f) return false;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    return strstr(buf, want) != NULL;
}

static int CountLaunches(void) {
    int n = 0;
    for (const char *p = FakeLaunch_Log(); (p = strstr(p, "launch ")) != NULL; p++) if (p == FakeLaunch_Log() || p[-1] == '\n') n++;
    return n;
}

static int CountInLog(const char *text) {
    int n = 0;
    if (!text[0]) return 0;
    for (const char *p = FakeLaunch_Log(); (p = strstr(p, text)) != NULL; p += strlen(text)) n++;
    return n;
}

static bool PrefIs(const char *spec) {
    char key[32];
    int want = 0;
    if (sscanf(spec, "%31s %d", key, &want) != 2) return false;
    const ArcadePrefs *p = Prefs_Get();
    if (strcmp(key, "music_volume") == 0) return p->musicVolume == want;
    if (strcmp(key, "screen_shake") == 0) return p->screenShake == (want != 0);
    if (strcmp(key, "reduced_flashing") == 0) return p->reducedFlashing == (want != 0);
    if (strcmp(key, "fullscreen") == 0) return p->fullscreen == (want != 0);
    return false;
}

static void DoExpect(const Event *e) {
    sExpectsRun++;
    char what[32], arg[300], text[600];
    const char *rest;
    SplitFirst(e->text, what, sizeof(what), &rest);
    bool ok;
    if (strcmp(what, "ui") == 0) ok = strcmp(drv_ui_name(), rest) == 0;
    else if (strcmp(what, "selected") == 0) ok = drv_selected() == atoi(rest);
    else if (strcmp(what, "trophy") == 0) ok = drv_trophy_game() == atoi(rest);
    else if (strcmp(what, "profile") == 0) ok = strcmp(drv_profile_text(), rest) == 0;
    else if (strcmp(what, "fullscreen") == 0) ok = sBorderless == (atoi(rest) != 0);
    else if (strcmp(what, "launched") == 0) ok = strstr(FakeLaunch_Log(), rest) != NULL;
    else if (strcmp(what, "notlaunched") == 0) ok = strstr(FakeLaunch_Log(), rest) == NULL;
    else if (strcmp(what, "launches") == 0) ok = CountLaunches() == atoi(rest);
    else if (strcmp(what, "count") == 0) {
        SplitFirst(rest, arg, sizeof(arg), &rest);
        ok = CountInLog(rest) == atoi(arg);
    }
    else if (strcmp(what, "prefs") == 0) ok = PrefIs(rest);
    else if (strcmp(what, "app") == 0) {
        char got[300];
        const char *last = strrchr(rest, ' ');
        if (strncmp(rest, "look:", 5) == 0 && last) { /* a label may have spaces; the look is one word */
            snprintf(arg, sizeof(arg), "%.*s", (int)(last - rest), rest);
            rest = last + 1;
        } else {
            SplitFirst(rest, arg, sizeof(arg), &rest);
        }
        drv_app(arg, got, sizeof(got));
        ok = strcmp(got, rest) == 0;
        if (!ok) Log("app %s is '%s', not '%s'", arg, got, rest);
    }
    else if (strcmp(what, "file") == 0 || strcmp(what, "nofile") == 0) {
        SplitFirst(rest, arg, sizeof(arg), &rest);
        Unescape(rest, text, sizeof(text));
        char path[800];
        bool exists = HomePath(arg, path, sizeof(path)) && access(path, R_OK) == 0;
        ok = what[0] == 'f' ? FileHas(arg, text) : (exists && !FileHas(arg, text));
    } else {
        Log("EXPECT FAIL (line %d): unknown check %s", e->arg, what);
        sExpectFails++;
        return;
    }
    char why[700];
    snprintf(why, sizeof(why), "%s (line %d: %s)", ok ? "expect ok" : "EXPECT FAIL", e->arg, e->text);
    if (!ok) sExpectFails++;
    if (!ok || sVerbose) Where(why);
}

static char sShotName[512] = "";
static const char *sShotDir = ".";

static void ApplyEvent(const Event *e) {
    if (sVerbose) Log("event %d arg %d %s", (int)e->type, e->arg, e->text);
    switch (e->type) {
        case EV_KEYDOWN: if (e->arg > 0 && e->arg < NK) sKey[e->arg] = 1; if (sKeyQN < 64) sKeyQueue[sKeyQN++] = e->arg; break;
        case EV_KEYUP: if (e->arg > 0 && e->arg < NK) sKey[e->arg] = 0; break;
        case EV_CHAR: if (sCharN < 64) sChars[sCharN++] = e->arg; break;
        case EV_PADON: sPadOn = true; break;
        case EV_PADOFF: sPadOn = false; memset(sPad, 0, sizeof(sPad)); memset(sAxis, 0, sizeof(sAxis)); break;
        case EV_PADDOWN: if (e->arg >= 0 && e->arg < NPAD) sPad[e->arg] = 1; break;
        case EV_PADUP: if (e->arg >= 0 && e->arg < NPAD) sPad[e->arg] = 0; break;
        case EV_STICK: sAxis[GAMEPAD_AXIS_LEFT_X] = e->x; sAxis[GAMEPAD_AXIS_LEFT_Y] = e->y; break;
        case EV_MOVE: sMouse = (Vector2){e->x, e->y}; break;
        case EV_MDOWN: if (e->arg >= 0 && e->arg < NMOUSE) sMouseBtn[e->arg] = 1; break;
        case EV_MUP: if (e->arg >= 0 && e->arg < NMOUSE) sMouseBtn[e->arg] = 0; break;
        case EV_WHEEL: sWheel += e->y; break;
        case EV_FOCUS: sFocused = e->arg != 0; Log("focus %s", sFocused ? "on" : "off"); break;
        case EV_CLIP: snprintf(sClip, sizeof(sClip), "%s", e->text); break;
        case EV_WRITE: DoWrite(e); break;
        case EV_CHMOD: DoChmod(e); break;
        case EV_GAMEEXIT: FakeLaunch_ExitGame(); break;
        case EV_INSTALL: FakeLaunch_FinishInstall(e->arg != 0); break;
        case EV_SHOT: snprintf(sShotName, sizeof(sShotName), "%s", e->text); break;
        case EV_EXPECT: DoExpect(e); break;
        case EV_LOG: { char why[600]; snprintf(why, sizeof(why), "log (line %d) %s", e->arg, e->text); Where(why); break; }
        case EV_CLOSE: sCloseFlag = true; Where("close requested"); break;
    }
}

/* ------------------------------------------------------ the clock moves */
static int sHoldMs = 400;
static bool sClosing = false;
static long sCloseAt = 0;

/* One frame went by: last frame's state becomes "previous", and the events
 * of the new frame are applied. */
static void Advance(void) {
    sTick++;
    sCloseFlag = false; /* raylib 6: a close request lasts one poll */
    memcpy(sKeyPrev, sKey, sizeof(sKey));
    memcpy(sPadPrev, sPad, sizeof(sPad));
    memcpy(sMouseBtnPrev, sMouseBtn, sizeof(sMouseBtn));
    sMousePrev = sMouse;
    sWheel = 0.0f;
    sCharN = sCharI = 0;
    sKeyQN = sKeyQI = 0;
    while (sEvI < sEvN && sEv[sEvI].tick <= sTick) ApplyEvent(&sEv[sEvI++]);

    if (sTick == 1 && sHoldMs > 0) usleep((useconds_t)sHoldMs * 1000u);
    if (!sClosing && sTick >= sEndTick) {
        sClosing = true;
        sCloseAt = sTick;
        sCloseFlag = true;
        Where("end of script, closing");
    }
    if (sClosing && sTick - sCloseAt > 0 && (sTick - sCloseAt) % 60 == 0) sCloseFlag = true;
    if (sClosing && sTick - sCloseAt > 600) {
        Where("HANG: still open 600 frames after the close request");
        fflush(stderr);
        _exit(4);
    }
}

/* ------------------------------------------------------------ the wraps */
bool __wrap_IsKeyPressed(int key) { return key > 0 && key < NK && sKey[key] && !sKeyPrev[key]; }
bool __wrap_IsKeyPressedRepeat(int key) { (void)key; return false; }
bool __wrap_IsKeyDown(int key) { return key > 0 && key < NK && sKey[key]; }
bool __wrap_IsKeyReleased(int key) { return key > 0 && key < NK && !sKey[key] && sKeyPrev[key]; }
bool __wrap_IsKeyUp(int key) { return !__wrap_IsKeyDown(key); }
int __wrap_GetKeyPressed(void) { return sKeyQI < sKeyQN ? sKeyQueue[sKeyQI++] : 0; }
int __wrap_GetCharPressed(void) { return sCharI < sCharN ? sChars[sCharI++] : 0; }

bool __wrap_IsGamepadAvailable(int gamepad) { return sPadOn && gamepad == 0; }
const char *__wrap_GetGamepadName(int gamepad) { return (sPadOn && gamepad == 0) ? "Xbox Wireless Controller" : ""; }
static bool PadOk(int gamepad, int button) { return sPadOn && gamepad == 0 && button >= 0 && button < NPAD; }
bool __wrap_IsGamepadButtonDown(int gamepad, int button) { return PadOk(gamepad, button) && sPad[button]; }
bool __wrap_IsGamepadButtonUp(int gamepad, int button) { return !__wrap_IsGamepadButtonDown(gamepad, button); }
bool __wrap_IsGamepadButtonPressed(int gamepad, int button) { return PadOk(gamepad, button) && sPad[button] && !sPadPrev[button]; }
bool __wrap_IsGamepadButtonReleased(int gamepad, int button) { return PadOk(gamepad, button) && !sPad[button] && sPadPrev[button]; }
int __wrap_GetGamepadButtonPressed(void) {
    for (int b = 0; b < NPAD; b++) if (sPadOn && sPad[b] && !sPadPrev[b]) return b;
    return 0;
}
int __wrap_GetGamepadAxisCount(int gamepad) { return (sPadOn && gamepad == 0) ? 6 : 0; }
float __wrap_GetGamepadAxisMovement(int gamepad, int axis) { return (sPadOn && gamepad == 0 && axis >= 0 && axis < 8) ? sAxis[axis] : 0.0f; }

/* Logical coordinates straight from the script: winscale's mapping is
 * raylib's business and is not being tested here. */
Vector2 __wrap_GetMousePosition(void) { return sMouse; }
int __wrap_GetMouseX(void) { return (int)sMouse.x; }
int __wrap_GetMouseY(void) { return (int)sMouse.y; }
Vector2 __wrap_GetMouseDelta(void) { return (Vector2){sMouse.x - sMousePrev.x, sMouse.y - sMousePrev.y}; }
static bool MouseOk(int button) { return button >= 0 && button < NMOUSE; }
bool __wrap_IsMouseButtonDown(int button) { return MouseOk(button) && sMouseBtn[button]; }
bool __wrap_IsMouseButtonUp(int button) { return !__wrap_IsMouseButtonDown(button); }
bool __wrap_IsMouseButtonPressed(int button) { return MouseOk(button) && sMouseBtn[button] && !sMouseBtnPrev[button]; }
bool __wrap_IsMouseButtonReleased(int button) { return MouseOk(button) && !sMouseBtn[button] && sMouseBtnPrev[button]; }
float __wrap_GetMouseWheelMove(void) { return sWheel; }
Vector2 __wrap_GetMouseWheelMoveV(void) { return (Vector2){0.0f, sWheel}; }
void __wrap_SetMousePosition(int x, int y) { sMouse = (Vector2){(float)x, (float)y}; }
void __wrap_SetMouseCursor(int cursor) { if (sVerbose) Log("cursor %d", cursor); }
static bool sCursorHidden = false;
void __wrap_ShowCursor(void) { sCursorHidden = false; }
void __wrap_HideCursor(void) { sCursorHidden = true; }
bool __wrap_IsCursorHidden(void) { return sCursorHidden; }
bool __wrap_IsCursorOnScreen(void) { return sMouse.x >= 0 && sMouse.y >= 0 && sMouse.x < 960 && sMouse.y < 640; }

const char *__wrap_GetClipboardText(void) { return sClip; }
void __wrap_SetClipboardText(const char *text) { snprintf(sClip, sizeof(sClip), "%s", text ? text : ""); Log("clipboard set: %s", sClip); }

/* Admin mode follows SHOTS_ADMIN when it is set, so a script that edits
 * the catalog behaves the same on every machine. */
bool __real_AdminLock_IsUnlocked(void);
bool __wrap_AdminLock_IsUnlocked(void) {
    const char *e = getenv("SHOTS_ADMIN");
    return (e && e[0]) ? atoi(e) != 0 : __real_AdminLock_IsUnlocked();
}

bool __wrap_WindowShouldClose(void) { return sCloseFlag; }
bool __wrap_IsWindowFocused(void) { return sFocused; }

/* A title no real window has, so run.sh can spot this window by name too:
 * Hyprland may report an X11 client under the Xwayland process's pid. */
void __wrap_InitWindow(int width, int height, const char *title) {
    static char unique[96];
    (void)title;
    snprintf(unique, sizeof(unique), "ghost-launcher-shots-%d", (int)getpid());
    __real_InitWindow(width, height, unique);
}

void __wrap_SetConfigFlags(unsigned int flags) {
    const char *fps = getenv("SHOTS_FPS");
    if (!fps || atoi(fps) != 60) flags &= ~(unsigned)FLAG_VSYNC_HINT;
    __real_SetConfigFlags(flags | FLAG_WINDOW_HIDDEN);
}
void __wrap_SetTargetFPS(int fps) {
    const char *e = getenv("SHOTS_FPS");
    (void)fps;
    __real_SetTargetFPS(e ? atoi(e) : 0);
}

/* The window itself never changes: fullscreen is a flag the script can check. */
void __wrap_ToggleBorderlessWindowed(void) { sBorderless = !sBorderless; Log("fullscreen %s", sBorderless ? "on" : "off"); }
void __wrap_ToggleFullscreen(void) { __wrap_ToggleBorderlessWindowed(); }
bool __wrap_IsWindowState(unsigned int flag) {
    if (flag == FLAG_BORDERLESS_WINDOWED_MODE || flag == FLAG_FULLSCREEN_MODE) return sBorderless;
    return __real_IsWindowState(flag);
}
bool __wrap_IsWindowFullscreen(void) { return sBorderless; }
void __wrap_SetWindowState(unsigned int flags) { Log("SetWindowState %u ignored", flags); }
void __wrap_ClearWindowState(unsigned int flags) { Log("ClearWindowState %u ignored", flags); }
void __wrap_MaximizeWindow(void) { }
void __wrap_MinimizeWindow(void) { }
void __wrap_RestoreWindow(void) { }
void __wrap_SetWindowFocused(void) { }
void __wrap_SetWindowSize(int w, int h) { (void)w; (void)h; }
void __wrap_SetWindowPosition(int x, int y) { (void)x; (void)y; }

double __wrap_GetTime(void) { return (double)sTick / 60.0; }
float __wrap_GetFrameTime(void) { return 1.0f / 60.0f; }

static unsigned long long sRandState = 0x243F6A8885A308D3ull;
int __wrap_GetRandomValue(int min, int max) {
    if (min > max) { int t = min; min = max; max = t; }
    sRandState ^= sRandState << 13; sRandState ^= sRandState >> 7; sRandState ^= sRandState << 17;
    unsigned long long span = (unsigned long long)((long long)max - (long long)min + 1);
    return (int)((long long)min + (long long)((sRandState >> 11) % span));
}

/* The logical frame is winscale's render texture, the first one made (Win_Init
 * runs before Render_Init): remember it, so a texture the launcher draws into
 * for itself is never mistaken for the screen. */
static int sLogicalW = 0, sLogicalH = 0;
static unsigned int sLogicalFbo = 0;
RenderTexture2D __wrap_LoadRenderTexture(int width, int height) {
    RenderTexture2D rt = __real_LoadRenderTexture(width, height);
    if (!sLogicalW) { sLogicalW = width; sLogicalH = height; sLogicalFbo = rt.id; }
    return rt;
}

/* Still bound to the logical texture here, and drawn in full. */
void __wrap_EndTextureMode(void) {
    if (sShotName[0] && sLogicalW > 0 && rlGetActiveFramebuffer() == sLogicalFbo) {
        rlDrawRenderBatchActive();
        unsigned char *px = rlReadScreenPixels(sLogicalW, sLogicalH);
        Image img = {px, sLogicalW, sLogicalH, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        char path[800];
        snprintf(path, sizeof(path), "%s/%s.png", sShotDir, sShotName);
        bool ok = px && ExportImage(img, path);
        if (ok) sShotsWritten++;
        char why[900];
        snprintf(why, sizeof(why), "%s %s", ok ? "shot" : "SHOT FAILED", path);
        Where(why);
        MemFree(px);
        sShotName[0] = '\0';
    }
    __real_EndTextureMode();
}

void __wrap_EndDrawing(void) {
    __real_EndDrawing(); /* polls raylib's own input, which nothing reads */
    Advance();
}

void __wrap_PollInputEvents(void) {
    __real_PollInputEvents();
    Advance();
}

/* No audio device, ever: a run must be silent and must not grab the card. */
void __wrap_InitAudioDevice(void) { }
void __wrap_CloseAudioDevice(void) { }
bool __wrap_IsAudioDeviceReady(void) { return false; }
void __wrap_SetMasterVolume(float v) { (void)v; }
Sound __wrap_LoadSound(const char *f) { (void)f; return (Sound){0}; }
Sound __wrap_LoadSoundFromWave(Wave w) { (void)w; return (Sound){0}; }
Sound __wrap_LoadSoundAlias(Sound s) { (void)s; return (Sound){0}; }
bool __wrap_IsSoundValid(Sound s) { (void)s; return false; }
void __wrap_UnloadSound(Sound s) { (void)s; }
void __wrap_UnloadSoundAlias(Sound s) { (void)s; }
void __wrap_PlaySound(Sound s) { (void)s; if (sVerbose) Log("sound"); }
void __wrap_StopSound(Sound s) { (void)s; }
void __wrap_PauseSound(Sound s) { (void)s; }
void __wrap_ResumeSound(Sound s) { (void)s; }
bool __wrap_IsSoundPlaying(Sound s) { (void)s; return false; }
void __wrap_SetSoundVolume(Sound s, float v) { (void)s; (void)v; }
void __wrap_SetSoundPitch(Sound s, float v) { (void)s; (void)v; }
void __wrap_SetSoundPan(Sound s, float v) { (void)s; (void)v; }
AudioStream __wrap_LoadAudioStream(unsigned int r, unsigned int s, unsigned int c) { (void)r; (void)s; (void)c; return (AudioStream){0}; }
bool __wrap_IsAudioStreamValid(AudioStream s) { (void)s; return false; }
void __wrap_UnloadAudioStream(AudioStream s) { (void)s; }
void __wrap_UpdateAudioStream(AudioStream s, const void *d, int n) { (void)s; (void)d; (void)n; }
bool __wrap_IsAudioStreamProcessed(AudioStream s) { (void)s; return false; }
void __wrap_PlayAudioStream(AudioStream s) { (void)s; }
void __wrap_PauseAudioStream(AudioStream s) { (void)s; }
void __wrap_ResumeAudioStream(AudioStream s) { (void)s; }
void __wrap_StopAudioStream(AudioStream s) { (void)s; }
bool __wrap_IsAudioStreamPlaying(AudioStream s) { (void)s; return false; }
void __wrap_SetAudioStreamVolume(AudioStream s, float v) { (void)s; (void)v; }
void __wrap_SetAudioStreamPitch(AudioStream s, float v) { (void)s; (void)v; }
void __wrap_SetAudioStreamPan(AudioStream s, float v) { (void)s; (void)v; }
void __wrap_SetAudioStreamBufferSizeDefault(int n) { (void)n; }
void __wrap_SetAudioStreamCallback(AudioStream s, AudioCallback cb) { (void)s; (void)cb; }

/* --------------------------------------------------------------- main */
int main(void) {
    sVerbose = getenv("SHOTS_VERBOSE") != NULL;
    if (getenv("SHOTS_OUT")) sShotDir = getenv("SHOTS_OUT");
    if (getenv("SHOTS_HOLD_MS")) sHoldMs = atoi(getenv("SHOTS_HOLD_MS"));
    const char *script = getenv("SHOTS_SCRIPT");
    if (!script) { fprintf(stderr, "SHOTS_SCRIPT is not set (see tools/shots/run.sh)\n"); return 2; }
    /* Refuse to run against a real home: everything the launcher writes goes there. */
    const char *home = getenv("HOME"), *real = getenv("SHOTS_REAL_HOME");
    if (!home || !getenv("XDG_DATA_HOME") || !getenv("XDG_CONFIG_HOME") || (real && strcmp(home, real) == 0) || !strstr(home, "ghost-shots.")) {
        fprintf(stderr, "refusing to run: HOME and XDG_*_HOME must point at a run.sh temp folder\n");
        return 2;
    }
    LoadScript(script);
    SetTraceLogLevel(LOG_WARNING);
    int rc = getenv("SHOTS_GALLERY") ? gallery_main() : launcher_main();
    bool quitEarly = !sClosing; /* it left before the script's end asked it to */
    bool quitOk = getenv("SHOTS_QUIT_OK") != NULL;
    Where("launcher exited");
    Log("summary: rc=%d frames=%ld shots=%d/%d expects=%d/%d expect_failures=%d quit=%s", rc, sTick, sShotsWritten, sShotsAsked,
        sExpectsRun, sExpectsAsked, sExpectFails, quitEarly ? "early" : "at the end");
    if (rc != 0) return rc;
    if (sExpectFails || sExpectsRun != sExpectsAsked) return 3; /* a check the launcher quit before reaching failed too */
    if (quitEarly && quitOk) return 0;
    if (sShotsWritten != sShotsAsked) return 5;
    if (quitEarly != sMustQuit) {
        Log("QUIT MISMATCH: the launcher %s, the script %s", quitEarly ? "quit early" : "stayed open to the end",
            sMustQuit ? "says it quits" : "does not say `quits`");
        return 6;
    }
    return 0;
}
