#ifndef HALL_H
#define HALL_H

#include <stdbool.h>
#include <stddef.h>
#include "kit.h"
#include "input.h"
#include "keymap.h"

/* The hall without raylib: where each cabinet stands while the row slides
 * (render.c draws them there and the mouse is tested against the very same
 * rectangles, so a click hits what is on screen), what a key, a pad button,
 * the wheel or a click on the hall asks for, and the one status line on the
 * card. main.c does what is asked. */

#define HALL_FLOOR_Y 436
#define HALL_SLOT_SPACING 200 /* centre to centre along the row */
#define HALL_CAB_W_UNITS 48
#define HALL_CAB_H_UNITS 84
#define HALL_ROW_REACH 3.2f   /* machines further from the middle than this are not drawn */
#define HALL_JUMP 5           /* LB/RB, PgUp/PgDn */
#define HALL_FPS 60
#define HALL_QUIET_FPS 10     /* behind a game the launcher started */

/* --- the row ---
 * Everything here counts places in the row (lineup.h maps them to the
 * catalog). */
typedef struct {
    float scroll;      /* where the row is: the selected machine's place once it settles */
    int lastSelected;  /* -1 until the first frame, which starts settled */
    float sinceSelect; /* seconds since the selection changed: the marquee's warm-up */
} HallRow;

void Hall_RowInit(HallRow *r);
/* One frame: the row eases toward the selected machine. A machine further
 * than a jump (LB/RB) from where the row is drawn is jumped to instead: a
 * wrap from the last machine to the first never slides past the whole row. */
void Hall_RowStep(HallRow *r, int selected, float dt);
/* The row was put in another order: the same machine stays lit and the row
 * stands at its new place at once. */
void Hall_RowSnap(HallRow *r, int selected);

/* While a game the launcher started runs and the launcher is behind it, the
 * hall rests: a few frames a second, nothing moving. */
bool Hall_Quiet(bool gameRunning, bool focused);
int Hall_TargetFps(bool quiet);
/* The newly lit marquee stutters on like a fluorescent tube. */
float Hall_MarqueePower(const HallRow *r);

/* 1 in the middle, 0 a slot or more away. */
float Hall_Focus(float offset);
float Hall_CabinetCenterX(float scroll, int index);
/* The whole cabinet as drawn, marquee overhang included, unclipped. */
KitRect Hall_CabinetRect(float scroll, int index);
/* Drawn, and at least partly on the screen. */
bool Hall_CabinetShown(float scroll, int index);

/* LB/RB: five along, stopping at the first or last machine; from there a
 * jump the same way comes round to the other end, so a press is never dead. */
int Hall_Jump(int selected, int count, int delta);

/* --- what the hall is asked to do --- */
typedef enum {
    HV_NONE,
    HV_PLAY,      /* Enter, Space, pad A, Play, a click on the lit machine: play or install */
    HV_DAILY,     /* D, pad Y, Daily */
    HV_THIS_GAME, /* I, pad X, This game, a click on the card: the machine's page */
    HV_MENU,      /* Esc, pad Menu or B, either Menu button */
    HV_OPEN,      /* a letter (or pad View): the screen in `open` */
    HV_LEAVE,     /* the EXIT sign */
    HV_UPDATES,   /* the NEW VERSION notice */
    HV_PLAYER,    /* Playing as: Settings > Player */
    HV_FAVOURITE, /* F, or the card's Favourite: a gold star, first on the row */
    HV_ORDER,     /* T: the next row order */
    HV_DESK,      /* the chalkboard: the Daily challenge desk */
    HV_ADD,       /* admin */
    HV_EDIT,
    HV_REMOVE,
} HallVerb;

typedef struct {
    int moveTo;      /* the machine to bring to the middle, or -1 */
    HallVerb verb;   /* at most one a frame, after the move */
    KeyAction open;  /* HV_OPEN: KA_OPEN_* */
} HallIntent;

/* What survives between frames: part notches of the wheel, so a touchpad
 * moves one machine at a time. */
typedef struct {
    float wheel;
} HallInput;

/* The hall's widgets (screens.c lays them out) map to verbs here. Order, as
 * the hall always had it: a button pressed this frame; else the menu key;
 * else a letter; else a move (keys, d-pad, LB/RB, the wheel) and then one
 * action. Admin's keys and buttons do nothing without admin. */
HallIntent Hall_Read(HallInput *st, const Input *in, const KitEvent *ev, int selected, int count, bool admin);

/* --- the card's status line --- */
typedef enum { HT_PLAY, HT_INSTALL, HT_BUSY, HT_BAD, HT_DIM, HT_NOTE } HallTone;

/* An update that ended while the Updates screen was not up, until it is. */
typedef enum { HUN_NONE, HUN_DONE, HUN_FAILED } HallUpdateNews;

typedef struct {
    bool installing, installFailed; /* an install of this machine */
    bool updating;                  /* Ghost Arcade is updating itself: nothing plays */
    HallUpdateNews updateNews;      /* "...Press V to see why", in place of the prompt */
    const char *error;              /* a launch that failed, or "" */
    const char *note;               /* why the last press did nothing (Daily), or "" */
    bool installed, canInstall;
    bool tool;                      /* Chipforge: "to open Chipforge", not "to play" */
    InputDevice device;             /* names the play key: Enter, or pad A */
    const char *installLog;         /* where the install wrote its log, ~ for home */
} HallStatusIn;

#define HALL_STATUS_W 536 /* the card's left column: never into the scores */
#define HALL_STATUS_LEN 96

typedef struct {
    char text[HALL_STATUS_LEN];
    int size;       /* 16 for a prompt, 8 for a message */
    HallTone tone;
    bool blink;     /* the attract-mode prompt */
    bool dots;      /* "Installing" + dots that count up */
} HallStatus;

/* The one line under the description, cut to fit HALL_STATUS_W. */
void Hall_Status(const HallStatusIn *in, HallStatus *out);
/* text is cut to maxW pixels at size; a path loses its start
 * (".../ghost-launcher/install.log"), anything else its end ("..."). */
void Hall_Fit(char *text, size_t size, int maxW, int textSize, bool path);
/* Why Daily cannot start on this machine, or "" when it can. */
void Hall_DailyReason(const char *name, bool installed, bool hasDaily, char *out, size_t size);
/* path with the home folder written as ~. */
void Hall_Tilde(const char *path, const char *home, char *out, size_t size);

#endif
