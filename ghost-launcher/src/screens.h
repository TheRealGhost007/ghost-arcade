#ifndef SCREENS_H
#define SCREENS_H

#include <stdbool.h>
#include "kit.h"
#include "keymap.h"
#include "manifest.h"
#include "lineup.h"

/* Where every screen's buttons are: one function per screen fills a KitList,
 * and both main.c (clicks, focus) and render.c (drawing) use it, so the two
 * can never disagree. Also the Arcade menu's items. No raylib. */

/* The X, top right on every screen. A click quits at once. */
#define CLOSE_BTN_SIZE 24
#define CLOSE_BTN_X (KIT_SCREEN_W - CLOSE_BTN_SIZE - 16)
#define CLOSE_BTN_Y 16

/* [< Back], top left on every screen but the hall. */
#define BACK_BTN_X 16
#define BACK_BTN_Y 16
#define BACK_BTN_W 72
#define BACK_BTN_H 24

/* [= Menu], left of the X on the hall. */
#define MENU_CHIP_W 72
#define MENU_CHIP_X (CLOSE_BTN_X - 12 - MENU_CHIP_W)

/* The hall's header, right to left: the X, Menu, NEW VERSION (only when
 * there is one), Playing as. All are buttons, 24 px tall, 12 px apart. */
#define HEADER_GAP 12
#define PILL_W 118
#define PILL_X (MENU_CHIP_X - HEADER_GAP - PILL_W)

/* The control strip along the bottom of the hall: chips on the left (the
 * player's, then admin's), the row's "< 4 / 12 >" on the right. */
#define STRIP_Y 604
#define STRIP_H 24
#define STRIP_X 24
#define STRIP_GAP 6
#define STRIP_GROUP_GAP 16
#define STRIP_ARROW_W 24
#define STRIP_RIGHT (KIT_SCREEN_W - 24)

/* The card under the row: what the lit machine is, its scores, one status
 * line, and its Favourite button at the right of the name. */
#define HALL_CARD_X 40
#define HALL_CARD_Y 466
#define HALL_CARD_W 880
#define HALL_CARD_H 122
#define HALL_CARD_LEFT (HALL_CARD_X + 24)  /* the left column's text */
#define HALL_CARD_COL (HALL_CARD_X + 600)  /* the scores column */
#define HALL_FAV_RIGHT (HALL_CARD_LEFT + 536) /* the left column's end */
#define HALL_FAV_Y (HALL_CARD_Y + 14)

/* The chalkboard on the hall's right wall, "DAILY 2 of 7 today": above the
 * row's right-hand machines, clear of every marquee while the row slides. */
#define CHALK_X 712
#define CHALK_Y 68
#define CHALK_W 160
#define CHALK_H 92

/* The Arcade menu: a panel over the hall's empty left wall, stopping short
 * of the selected machine's marquee. */
#define MENU_PANEL_W 392
#define MENU_ROW_X 16
#define MENU_ROW_W 360
#define MENU_ROW_H 40
#define MENU_ROW_Y 128
#define MENU_ROW_PITCH 48
#define MENU_LEAVE_GAP 16 /* leaving sits a little apart from the rest */

/* Stats: rows of the playtime table that fit on the screen. */
#define STATS_ROW_Y 164
#define STATS_ROW_PITCH 26
#define STATS_VISIBLE 13

/* Trophies: one game's achievements, this many at a time. */
#define TROPHY_ROW_Y 214
#define TROPHY_ROW_PITCH 56
#define TROPHY_VISIBLE 6

enum {
    WID_CLOSE = 1,
    WID_BACK,
    WID_MENU_CHIP,
    WID_EXIT_SIGN,
    WID_UPDATE_PILL,
    WID_MENU_VEIL,
    WID_MENU_PANEL,
    WID_MENU_ITEM = 20, /* + an ArcadeMenuItem */
    WID_ONLINE_NO = 40,
    WID_ONLINE_YES,
    WID_TROPHY_GAME = 50,
    WID_CHECK_NOW = 60,
    WID_PLAYING_AS = 70,
    /* the hall's control strip */
    WID_STRIP_PLAY = 80,
    WID_STRIP_DAILY,
    WID_STRIP_THIS_GAME,
    WID_STRIP_STATS,
    WID_STRIP_MENU,
    WID_STRIP_ADD,
    WID_STRIP_EDIT,
    WID_STRIP_REMOVE,
    WID_STRIP_PREV,
    WID_STRIP_NEXT,
    WID_CARD_FAVOURITE = 90,
    WID_CARD,                /* the card itself: a click opens this machine's page */
    WID_CHALKBOARD,          /* the wall's "DAILY 2 of 7 today": the daily desk */
    /* Settings (settings.c) */
    WID_SET_EDIT_VEIL = 98,
    WID_SET_TABS = 100,
    WID_SET_NAME_FIELD = 110,
    WID_SET_NAME_EDIT,   /* Edit, or Save while typing */
    WID_SET_NAME_CANCEL, /* only while typing */
    WID_SET_START_ON,
    WID_SET_MUSIC = 120,
    WID_SET_HALL_SOUNDS,
    WID_SET_COIN_TEST,
    WID_SET_HALL_MUSIC,
    WID_SET_FULLSCREEN = 130,
    WID_SET_FLASHING,
    WID_SET_SHAKE,
    WID_SET_BUTTON_HINTS,
    WID_SET_ATTRACT,
    WID_SET_SHARING = 140,
    WID_SET_CHECKS,
    WID_SET_CHECK_NOW,
    WID_SET_UPDATE_DETAILS,
    WID_SET_SYNC_NOW,
    WID_SET_CARD = 150,
    WID_SET_ROW_ORDER = 160,
    WID_SET_MACH_ADD,        /* admin: Add a machine */
    /* a machine's page (gamepage.c) */
    WID_PAGE_TABS = 170,
    WID_PAGE_BOARD,
    WID_PAGE_PREV,
    WID_PAGE_NEXT,
    WID_PAGE_PLAY,
    WID_PAGE_DAILY,
    WID_PAGE_NAME,
    WID_PAGE_FAV,
    WID_PAGE_EDIT,
    WID_PAGE_REMOVE,
    /* admin's Add / Edit form (gameform.c): a field is + its number */
    WID_FORM_FIELD = 180,
    WID_FORM_CARRY = 186,
    WID_FORM_SAVE,
    WID_FORM_CANCEL,
    WID_PAGE_SONGS_VIEW = 190, /* Chipforge's Songs: the hall's song, or who plays what */
    /* one of each per machine: + its catalog index */
    WID_SET_GAME_FIELD = 200,
    WID_SET_GAME_EDIT = 300, /* Set or Edit, or Save while typing */
    WID_SET_GAME_CLEAR = 400, /* Clear, or Cancel while typing */
    WID_CABINET = 500,        /* + its place in the row: a machine on the hall */
    WID_SET_BOARD_PANEL = 590, /* the name board: its panel, then + a cell */
    WID_SET_BOARD = 600,
    /* Settings > Machines, one of each per machine: + its catalog index */
    WID_SET_MACH_FAV = 1000,
    WID_SET_MACH_UP = 1100,
    WID_SET_MACH_DOWN = 1200,
    WID_SET_MACH_HIDE = 1300,
    WID_SET_MACH_EDIT = 1400, /* admin */
    WID_SET_MACH_REMOVE = 1500,
    WID_PAGE_SONG = 2000,     /* Chipforge's Songs: + its place in the list (0 = the built-in theme) */
    WID_DESK_ROW = 2100,      /* the daily desk: + the row's place on the desk */
};

/* The Arcade menu, in order. */
typedef enum { AM_HALL, AM_DAILY, AM_SETTINGS, AM_STATS, AM_TROPHIES, AM_UPDATES, AM_LEAVE, AM_COUNT } ArcadeMenuItem;

const char *Screens_MenuLabel(int item);
/* The key that also opens it from the hall ("" for none), from keymap.h;
 * after pad use, the pad's button for it, or "" where the pad has none. */
const char *Screens_MenuKey(int item, InputDevice device);
/* AM_* for a menu widget id, or -1. */
int Screens_MenuItemFor(int widgetId);
/* The screen this frame's letter (or Tab) opens from the hall and the menu:
 * KA_OPEN_*, or KA_NONE. */
KeyAction Screens_Shortcut(const Input *in);

/* "Playing as TRG" in the hall's header: the words, where they go, and the
 * chip they make (it opens Settings > Player). The name gets the room
 * between the title (and admin's badge) and the next chip to the right. */
typedef struct {
    char label[16];   /* "Playing as ", or "" when there is no room */
    char name[72];    /* the name, cut with "..." if it must be */
    bool empty;       /* no name yet: name holds the prompt, in red */
    int labelX, nameX, y;
    KitRect r;        /* the chip: both parts and its padding */
} PlayingAs;

#define HEADER_TEXT_Y 24
void Screens_PlayingAs(PlayingAs *p, const char *arcadeName, bool admin, bool updateBehind);
/* Where the header's chips may begin: after the title, or admin's badge. */
int Screens_HeaderLeft(bool admin);

/* What the Play chip does on the selected machine. */
typedef enum { HP_NONE, HP_PLAY, HP_INSTALL, HP_INSTALLING, HP_CANNOT } HallPlay;

typedef struct {
    bool coinInFlight; /* only the X works while a coin drops */
    bool updateBehind; /* the update notice shows */
    const char *arcadeName;
    bool admin;
    const Manifest *manifest; /* the catalog (NULL = empty) */
    const Lineup *lineup;     /* the row, in places (NULL = the catalog's order) */
    int place;                /* the lit machine's place in the row */
    float scroll;             /* where the row is drawn now (hall.h's HallRow) */
    InputDevice device;       /* the strip names keys, or the pad's buttons */
    HallPlay play;
    const char *dailyReason;  /* why Daily cannot start here, "" when it can */
    bool tool;                /* the lit machine is Chipforge: Play says Open */
    bool favourite;           /* the lit machine wears the star */
    bool canFavourite;        /* its program name can be kept in launcher.ini */
    bool allHidden;           /* an empty row because every machine is hidden */
    const char *deskLine;     /* the chalkboard's "2 of 7 today"; NULL or "" = no chalkboard */
    float cardSlide;          /* attract mode: the card is this far down on its way out */
} HallView;

/* Machines on the row, and the catalog index at a place in it. */
int Screens_RowCount(const HallView *v);
int Screens_RowIndex(const HallView *v, int place);

/* The hall: its machines in the row's order (WID_CABINET + the place: a
 * click brings one to the middle, a click on the lit one plays it), the
 * header's chips, the EXIT sign, the card's Favourite and the control
 * strip. All are for the mouse: the keys and the pad have their own. */
void Screens_Hall(KitList *l, const HallView *v);
void Screens_Menu(KitList *l, bool updateBehind, InputDevice device);
void Screens_Online(KitList *l);
/* count = machines in the catalog; none means no stepper. */
void Screens_Trophies(KitList *l, const char *gameName, int game, int count);
/* checksOff: check_updates=0, so Check now cannot do anything. */
void Screens_Updates(KitList *l, bool checking, bool checksOff);
/* Back and the X only: Stats and the admin game form. */
void Screens_Plain(KitList *l);
/* The pieces every screen but the hall has. Back is mouse-only (keys and the
 * pad have Esc and B); the X must be added last, so it is on top. */
void Screens_AddBack(KitList *l);
void Screens_AddClose(KitList *l);
/* An "are you sure?" box (kit.h) with the X still on top: it quits at once. */
void Screens_Dialog(KitList *l, const KitDialog *d);

#endif
