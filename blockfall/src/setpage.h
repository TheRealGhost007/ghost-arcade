#ifndef SETPAGE_H
#define SETPAGE_H

#include <stdbool.h>
#include "persist.h"
#include "prefs.h"
#include "controls.h"
#include "playstep.h"
#include "repeat.h"
#include "uikit.h"

/* Settings, without raylib (REWORK_PLAN.md, "Settings with tabs", "More
 * controls", "Ghost, grid and block-pattern options"): its tabs and rows,
 * where each control goes (a KitList, as every kit screen has), how the
 * arrows move between rows, and what each change does to the settings.
 * scr_settings.c reads the keys for a capture, saves the files, plays the
 * sounds and draws the page.
 *
 *   Controls  each action's two keys and two pad buttons, Reset controls
 *   Feel      presets, auto-repeat delay, repeat speed, soft drop speed,
 *             and a strip to try them on
 *   Look      ghost piece, grid, block patterns, controls card, piece
 *             motion, Motion, screen shake*, reduced flashing*, fullscreen*
 *   Sound     effects volume, music volume*, sound, a test sound
 *   Game      auto pause, R restarts, Ready go, resume countdown, How to
 *             play on the next launch
 * (* shared with every game: prefs.ini.) Every change saves at once. */

typedef enum { TAB_CONTROLS, TAB_FEEL, TAB_LOOK, TAB_SOUND, TAB_GAME, SET_TABS } SetTab;
const char *SetPage_TabLabel(int tab);

/* Geometry on the 620x720 screen. */
#define SET_TABS_Y 104
#define SET_TABS_H 32
#define SET_INTRO_Y 148
#define SET_ROW_X 20
#define SET_ROW_W 580
#define SET_LABEL_X 32
#define SET_CONTROL_X 352
#define SET_STEPPER_X 340 /* a stepper's "<" sits in its own 40 px cell */
#define SET_ROW_Y 172
#define SET_ROW_PITCH 38
#define SET_ROW_H 32
#define SET_CONTROL_H 28
#define SET_STEPPER_W 260
#define SET_TOGGLE_W 144
/* Controls: a table of caps under its headings. */
#define SET_HEAD_Y 166
#define SET_CAP_Y 182
#define SET_CAP_PITCH 34
#define SET_CAP_H 28
#define SET_KEY_W 88
#define SET_PAD_W 72
#define SET_RESET_Y 494
/* The lines under the rows. */
#define SET_NOTE_Y 540
#define SET_HELP_Y 576
#define SET_FOOT_Y 620
#define SET_HINTS_CY 676
/* Feel: the strip to try your values on. */
#define STRIP_CELLS 10
#define STRIP_CELL 16
#define STRIP_PIECE_W 3

/* Control ids. */
enum {
    SID_BACK = 1,
    SID_TABS,
    SID_CAP = 100, /* + action * 4 + column: 0 key, 1 second key, 2 pad, 3 second pad */
    SID_RESET = 140,
    SID_PRESET = 150, /* + 0 .. FEEL_PRESETS - 1 */
    SID_DELAY = 160, SID_SPEED, SID_SOFT, SID_STRIP,
    SID_GHOST = 170, SID_GRID, SID_PATTERNS, SID_CARD, SID_PIECE, SID_MOTION, SID_SHAKE, SID_FLASH, SID_FULL,
    SID_EFFECTS = 180, SID_MUSIC, SID_SOUND, SID_TEST,
    SID_AUTOPAUSE = 190, SID_QUICKR, SID_READY, SID_COUNTDOWN, SID_HOWTO,
};
#define SID_CAP_OF(action, col) (SID_CAP + (action) * 4 + (col))
#define SET_CAP_COLUMNS 4
bool SetPage_IsCap(int id);
int SetPage_CapAction(int id);
int SetPage_CapColumn(int id);      /* 0 key, 1 second key, 2 pad, 3 second pad */
KitRect SetPage_CapRect(int action, int column);

#define SET_MAX_ROWS 12
#define SET_ROW_CONTROLS 4

typedef struct {
    KitRect r;            /* the whole row, label to controls */
    char label[40];
    bool shared;          /* stored in prefs.ini, the same in every game (marked *) */
    const char *help;     /* the sentence under the rows while it is chosen */
    int controls[SET_ROW_CONTROLS]; /* its focusable controls, left to right */
    int count;
} SetRow;

typedef struct {
    KitList list;
    SetRow rows[SET_MAX_ROWS];
    int rowCount;
    const char *intro;    /* what the tab is for */
    const char *footnote; /* "* Shared with every game ..." or the fixed keys */
} SetPage;

/* What the page shows besides the settings themselves. */
typedef struct {
    const Settings *s;
    const ArcadePrefs *prefs;
    int tab;
    int captureId;        /* the cap waiting for a key or button, or KIT_NONE */
    bool soundReady;      /* a sound device to play the test sound on */
} SetView;

void SetPage_Layout(const SetView *v, SetPage *p);
/* The row holding a control (by id), or NULL. */
const SetRow *SetPage_RowOf(const SetPage *p, int id);
/* Up/Down from row to row (the tab bar sits above the first row), Left/
 * Right along a row (a value control's Left/Right the kit turns into a
 * change). Run after Kit_Update. True if the focus moved. */
bool SetPage_Navigate(KitState *k, const SetPage *p, const Input *in);
/* What Left/Right do where the focus is, for the hint line: "change",
 * "move", "try" (the strip), or NULL. */
const char *SetPage_SideKeys(const SetPage *p, int focus);
/* The first control of the first row: where a tab opens. */
int SetPage_FirstRowControl(const SetPage *p);

/* --- Feel's presets --- */
#define FEEL_PRESETS 3
typedef struct {
    const char *name;
    int delayMs, speedMs, softSpeed;
} FeelPreset;
const FeelPreset *SetPage_Preset(int i);
/* The preset your Feel values match, or -1 (Custom). Standard is today's. */
int SetPage_PresetOf(const Settings *s);
/* Words for the values: "170 ms", "35 ms", "Normal". */
void SetPage_MsWords(int ms, char *out, int size);
const char *SetPage_SoftWords(int speed);

/* --- what a change does --- */
typedef enum {
    SE_NONE,
    SE_TAB,        /* the tab changed: value */
    SE_CAPTURE,    /* a cap was pressed: wait for a key or button (captureAction, captureColumn) */
    SE_RESET_ASK,  /* Reset controls: ask first */
    SE_TEST_SOUND, /* play the test sound */
    SE_HOWTO_NEXT, /* Show How to play on the next launch: set (saved to config.ini) */
    SE_CHANGED,    /* a setting changed (saved per config/prefs below) */
} SetEffectKind;

typedef struct {
    SetEffectKind kind;
    int value;
    int id;
    bool config, prefs;       /* which file to save */
    bool effectsVolume;       /* the effects volume changed: tick at the new level */
    bool musicVolume, sound, motion, pieceSnap, patterns;
    int captureAction, captureColumn;
} SetEffect;
/* A kit event on the page, applied to the settings (the screen saves the
 * files and applies the rest). */
SetEffect SetPage_Apply(Settings *s, ArcadePrefs *prefs, const KitEvent *ev);

/* --- waiting for a key or a button --- */
#define CAPTURE_SECONDS 5.0f
typedef struct {
    bool on;
    bool pad;             /* a pad cap: waiting for a button */
    int action, column;
    float t;              /* seconds waited */
    char pendingMod[KEYNAME_LEN]; /* Shift, Ctrl or Alt went down: it binds when let go, unless a key comes with it */
} Capture;
void Capture_Begin(Capture *c, int action, int column);
void Capture_End(Capture *c);
/* A frame waited; true when the five seconds are up (it gives up). */
bool Capture_Tick(Capture *c, float dt);
/* How much of the wait is left, 1 .. 0 (the bar under the cap). */
float Capture_Left(const Capture *c);
bool Capture_IsModifier(const char *key);
/* A pad capture's frame, from the Input alone (scr_settings.c handles Esc,
 * Menu, B and a click, which give up, before this). The first button
 * pressed this frame binds to the cap's slot (c->column - 2), Menu aside:
 * BOUND, with Controls_BindPad's result in *out. No button but a key went
 * down (keyPressed): KEY_WHILE_PAD, so the screen can say "That is a key".
 * Otherwise NONE. A key capture: NONE. */
typedef enum { CAPFEED_NONE, CAPFEED_BOUND, CAPFEED_KEY_WHILE_PAD } CaptureFeed;
CaptureFeed Capture_FeedPad(Capture *c, Settings *s, const Input *in, bool keyPressed, BindResult *out);

/* --- Feel's strip: a piece sliding with your values --- */
typedef struct {
    int x;                /* the piece's left column, 0 .. STRIP_CELLS - STRIP_PIECE_W */
    RepeatButton left, right;
    float idle;           /* seconds since the player last held Left or Right on it */
    int dir;              /* the show's way: +1 or -1 */
    float rest;           /* the show waits at a wall */
    bool moved, repeat;   /* this frame: a step, by auto-repeat */
} FeelStrip;
void FeelStrip_Reset(FeelStrip *s);
/* One frame. left/right: held on the strip (focused). Left alone for a
 * second, it shows your values by itself: Right held to the wall, a rest,
 * Left held to the wall. */
void FeelStrip_Tick(FeelStrip *s, bool left, bool right, const PlayHandling *h, float dt);

#endif
