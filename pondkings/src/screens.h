#ifndef SCREENS_H
#define SCREENS_H

#include <stdbool.h>
#include "game.h"
#include "journey.h"
#include "persist.h"
#include "render.h"

/* The menus, in Pond Kings' own look: drawn over the night-time pond (or
 * the venue you're about to play). Pure drawing from the state main.c
 * passes in, so tools/shots.c can draw every screen too. */

typedef struct {
    bool joined;
    int device; /* Device */
    int team;   /* 0 the Rink Rats, 1 the rivals */
} JoinSlot;

typedef enum {
    MI_JOURNEY, MI_QUICK, MI_SHOOTOUT, MI_DAILY, MI_ONLINE, MI_CREWS, MI_HOWTO, MI_SETTINGS, MI_QUIT, MENU_ITEMS
} MenuItem;

/* Today's pond: everyone plays the same game. */
typedef struct {
    char date[16];     /* "2026-10-22" */
    char mode[24];     /* the board: "daily-20261022" */
    uint64_t seed;
    int rival, twist;
    long best;         /* your best today, 0 if none */
} DailyInfo;

/* A finished run's place on the boards. */
typedef struct {
    long score;
    int rank;          /* 1..10 on your local board, 0 if it didn't make it */
    bool newBest;
    char toast[48];    /* a trophy just won, or "" */
} RunScore;

void Screen_Title(void);
void Screen_Menu(int sel);
/* teams=false: everyone plays for the Rink Rats (the Journey), or one player only (maxPlayers). */
void Screen_Join(const JoinSlot slots[MAX_PLAYERS], int rivals, const bool deviceFree[], int deviceCount, bool teams, const char *title);
void Screen_Setup(int sel, const Settings *s, const JoinSlot slots[MAX_PLAYERS]);
void Screen_ShootoutSetup(int sel, const Settings *s);
void Screen_Daily(const DailyInfo *d, int sel);
void Screen_JourneyHub(const Journey *j, bool haveRun, int sel, bool confirmNew);
void Screen_Sign(const Journey *j, int step, int theirs, int sel);
void Screen_JourneyResult(const Journey *j, const JourneyResult *r, const Game *g, const RenderInfo *ri);
void Screen_JourneyEnd(const Journey *j, const RunScore *rs);
void Screen_ShootoutResult(const Game *g, const RunScore *rs, int sel);
void Screen_DailyResult(const Game *g, const DailyInfo *d, const RunScore *rs, int sel);
void Screen_HowTo(int page);
void Screen_Settings(int sel, const Settings *s);
void Screen_Keys(int row, int col, const Settings *s, bool waiting);
void Screen_Pause(int sel, bool online);
void Screen_Results(const Game *g, const RenderInfo *ri, int sel);

#define SETUP_ROWS 5      /* Venue, Rivals, Periods, Computer players, Face off */
#define SO_SETUP_ROWS 2   /* Rink, Start */
#define DAILY_ITEMS 2     /* Play, Back */
#define HUB_ITEMS 3       /* Play (or Start), New Journey, Back */
#define SETTINGS_ROWS 6   /* Effects volume, Music volume, Sound, Fullscreen, Keyboard keys, Back */
#define KEYS_ROWS (KA_COUNT + 1) /* the seven keys, then Reset */
#define PAUSE_ITEMS 3     /* Resume, Restart, Quit to menu */
#define RESULT_ITEMS 2    /* Play again, Menu */
#define HOWTO_PAGES 3

/* ------------------------------------------------------------ online and the rival crews
 * netui.c runs these screens; it fills a view and these draw it. */

#define CODE_ALPHABET "ABCDEFGHJKLMNPQRSTUVWXYZ23456789" /* room and crew codes: no I, O, 0 or 1 */
#define CODE_GRID_COLS 8
#define NETVIEW_LINES 7
#define NETVIEW_ROWS 5
#define NETVIEW_ITEMS 3

typedef struct {
    const char *title;
    int backdrop;               /* a Venue to show, or -1 for the pond at night */
    const char *lines[NETVIEW_LINES];
    int lineCount;
    bool error;                 /* the lines are bad news */
    const char *rowLabel[NETVIEW_ROWS], *rowValue[NETVIEW_ROWS];
    int rowCount;
    const char *items[NETVIEW_ITEMS];
    int itemCount;
    int sel;                    /* the rows first, then the items */
    const char *code;           /* a code shown or being typed (NULL: none) */
    int codeLen;                /* its boxes */
    bool typing;                /* a cursor in the next box, and the letter grid for pads */
    int gridSel;                /* the grid's chosen letter */
    bool busy;                  /* waiting on the network: a spinner by the status */
    const char *status;
    const char *hint;
} NetView;

void Screen_Net(const NetView *v);

#define CREWS_VIEW_MAX 8
typedef struct {
    const CrewCard *mine;
    const char *myStyle;        /* how this machine's players play, in words */
    const char *myCode;         /* "" until shared */
    const CrewCard *friends;
    const char *friendStyle[CREWS_VIEW_MAX];
    int friendCount;
    const char *items[3];       /* the actions (share, add, stop sharing) */
    int itemCount;
    int sel;                    /* the actions, then the friends, then Back */
    int removing;               /* the friend being asked about, or -1 */
    const char *status;
    bool busy, error;
} CrewsView;

void Screen_Crews(const CrewsView *v);

#endif
