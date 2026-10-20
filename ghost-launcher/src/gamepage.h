#ifndef GAMEPAGE_H
#define GAMEPAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include "kit.h"
#include "input.h"
#include "screens.h"
#include "boards.h"
#include "stats.h"
#include "profile.h"
#include "howto.h"
#include "song.h"

/* A machine's own page, without raylib: what it says and where its buttons
 * are. It opens with I, pad X or a click on the hall's card (Enter on the
 * row still plays at once). Top: the name, the description and a strip of
 * numbers (best, time played, runs, last played, trophies). Left: the
 * cabinet's attract demo drawn large, and the buttons Play (Install, Open
 * Chipforge), Daily challenge, Name for this game and Favourite. Right, in
 * tabs: About (the game's own how-to), Scores (this computer's top ten
 * beside the world's, board by board), Trophies; Chipforge has About and
 * Songs. render.c draws it; main.c does what it asks.
 *
 * Keys: Up/Down the buttons, Enter or pad A presses; Left/Right, Tab or
 * LB/RB the tabs; X or pad X the next board; PgUp/PgDn or LT/RT the machine
 * before or after on the row; D, F and Shift+U are the hall's keys for this
 * machine; Esc, pad B or I go back. */

/* Geometry, logical 960x640. */
#define PAGE_X 80
#define PAGE_TITLE_Y 56
#define PAGE_DESC_Y 108
#define PAGE_STRIP_Y 130      /* the numbers' labels; their values 14 px under */
#define PAGE_LEFT_W 288
#define PAGE_DEMO_Y 184
#define PAGE_DEMO_H 196
#define PAGE_BTN_Y 392
#define PAGE_BTN_H 32
#define PAGE_BTN_PITCH 40
#define PAGE_RIGHT_X 416
#define PAGE_RIGHT_W 464
#define PAGE_TABS_Y 184
#define PAGE_TABS_H 32
#define PAGE_BODY_Y 232
#define PAGE_BOARD_Y 228      /* Scores: the board's "< Classic >" */
#define PAGE_ROWS_Y 290       /* Scores: the first of ten rows */
#define PAGE_ROW_PITCH 18
#define PAGE_COL_W 224        /* Scores: this computer's column, then the world's */
#define PAGE_HELP_Y 568
#define PAGE_STATUS_Y 588
#define PAGE_ADMIN_Y 60       /* admin's Edit and Remove, right of the title */
#define PAGE_SONGS_Y 272      /* Chipforge's Songs: the first song row */
#define PAGE_SONG_PITCH 32
#define PAGE_SONG_H 28
#define PAGE_SONGS_VISIBLE 8
#define PAGE_LIBRARY_MAX 64

typedef enum { GT_ABOUT, GT_SCORES, GT_TROPHIES, GT_SONGS, GT_COUNT } GamePageTab;

/* Chipforge's Songs tab shows one list at a time: the songs the hall can
 * play (a press makes one the hall's), or which machine plays what. */
typedef enum { SONGS_HALL, SONGS_WHO, SONGS_VIEWS } SongsView;

/* What stays between frames and from machine to machine. */
typedef struct {
    int tab;                     /* GamePageTab; a machine without it shows About */
    char board[SCORES_MODE_LEN]; /* the board Scores shows, by its mode */
    int songsView;               /* SongsView */
    KitScroll songs;             /* the hall's songs, when there are more than fit */
} GamePage;

/* Chipforge's Songs tab: which machine plays which song. */
#define PAGE_SONG_USES 16

typedef struct {
    char machine[32];
    char song[48];
    bool custom; /* a song of the player's, not the built-in theme */
} PageSongUse;

/* A song in the player's Chipforge library. */
typedef struct {
    char file[SONG_FILE_LEN];
    char title[SONG_NAME_LEN];
} PageSong;

/* What the page shows about one machine, read from the files when it opens
 * (and whenever the hall's card is read again, so a sync or a game that
 * just closed shows up). */
typedef struct {
    int index;                    /* the catalog index it was read for, -1 for none */
    bool tool;                    /* Chipforge */
    char slug[SCORES_SLUG_LEN];   /* "" when the program name is not a valid one */
    ScoreBest best;               /* this computer's best, any board */
    double seconds;               /* time played */
    int runs;
    char last[11];                /* YYYY-MM-DD it was last played, "" for never */
    int trophies, trophyTotal;
    Boards boards;
    const HowTo *howto;           /* NULL for a machine the games do not know */
    char playsAs[64];             /* the name it plays under */
    bool ownName;                 /* ...its own, not the arcade's */
    int songs;                    /* Chipforge: songs in the library */
    char hallSong[48];            /* Chipforge: what the hall plays */
    PageSongUse uses[PAGE_SONG_USES];
    int useCount;
    PageSong library[PAGE_LIBRARY_MAX]; /* Chipforge: its library, by file name */
    int libraryCount;
    int hallEntry;                /* Chipforge: the Songs list's entry the hall plays (0 = the built-in theme), -1 for none shown */
} GamePageData;

typedef struct {
    const GameEntry *game;
    int index;
    const PlayStats *stats;
    const Profiles *profiles;
    long long played;          /* last started from the launcher (launcher.ini), 0 = never */
    const Manifest *manifest;  /* Chipforge's Songs tab lists the others */
    const char *hallSong;      /* what the hall plays: "Built-in theme" or a title */
    time_t now;
    const char *hallFile;      /* the hall's library file when it plays the player's song; NULL or "" for the theme */
} GamePageSource;

void GamePage_Load(GamePageData *d, const GamePageSource *src);

/* "daily-YYYYMMDD" for today (local time). */
void GamePage_Today(time_t now, char *out, size_t size);
/* "Today", "Yesterday", "3 days ago", "19 Sep", "19 Sep 2025"; "Never" for "". */
void GamePage_DayWords(const char *date, time_t now, char *out, size_t size);

/* The strip under the description: a word, a 16 px value and an 8 px
 * detail after it. Chipforge has four, a game five. */
#define PAGE_STRIP_MAX 5
typedef struct {
    char label[16];
    char value[48];
    char detail[32];
    bool dim;     /* "None yet": nothing to show */
    int x;
} PageStripCell;

int GamePage_Strip(const GamePageData *d, time_t now, PageStripCell out[PAGE_STRIP_MAX]);

/* --- the tabs --- */
const char *GamePage_TabLabel(int tab);
int GamePage_TabCount(bool tool);
int GamePage_TabAt(bool tool, int cell);
/* The cell of a tab, or -1 when the machine has none. */
int GamePage_CellOf(bool tool, int tab);
/* The tab shown: the page's, or About on a machine without it. */
int GamePage_ShownTab(const GamePage *p, bool tool);
/* Where the keys go once another machine is on the page (l is its layout):
 * the same button while it can still be pressed there, else the first that
 * can (a faint Daily or a missing Name would leave no button lit). */
int GamePage_KeepFocus(const KitList *l, int focus);
/* A double-click on the hall's card opens the page with its first click;
 * its second, this soon after, is not a press on whatever button lies under
 * it here (Name for this game and Favourite sit where the card was). */
#define PAGE_CLICK_SETTLE 0.4
bool GamePage_ClickSettling(double openedAt, double now);

/* --- the boards (Scores) --- */
/* The board shown (its index in b), -1 when there are none. */
int GamePage_Board(const GamePage *p, const Boards *b);
void GamePage_SetBoard(GamePage *p, const Boards *b, int index);
/* A machine just opened: the board of its best score if there is one,
 * else the first. */
void GamePage_StartBoard(GamePage *p, const Boards *b, const char *bestMode);

/* What the layout needs to know about the machine now. */
typedef struct {
    const char *name;
    bool tool;
    HallPlay play;            /* Play, Install, Installing, or neither */
    const char *dailyReason;  /* "" when its daily can start */
    bool favourite, canFavourite;
    bool admin;
    int place, count;         /* on the row: "3 / 12" */
    const Boards *boards;
    const char *today;        /* "daily-YYYYMMDD" */
    InputDevice device;       /* the keys the buttons name: the keyboard's, or the pad's */
    const GamePageData *data; /* Chipforge: its songs (NULL = none) */
    bool hallMusic;           /* Settings > Sound > Hall music is on */
} GamePageView;

/* Back, the buttons (the only ones the keys and the pad move between, so
 * Play has the focus first), the tabs, the board's "< Classic >", the
 * arrows to the next machine, admin's Edit and Remove, and the X. */
void GamePage_Layout(const GamePage *p, const GamePageView *v, KitList *l);

typedef enum {
    GA_NONE,
    GA_BACK,
    GA_PLAY,      /* Play, Install, or Open Chipforge */
    GA_DAILY,
    GA_NAME,
    GA_FAVOURITE,
    GA_EDIT,      /* admin */
    GA_REMOVE,    /* admin: asks first */
    GA_FLIP,      /* value: -1 or 1, the machine before or after on the row */
    GA_TAB,       /* done: the tab changed */
    GA_BOARD,     /* done: the board changed */
    GA_VIEW,      /* done: Chipforge's Songs shows the other list */
    GA_SONG,      /* value: a Songs entry, to be the hall's song (0 = the built-in theme) */
    GA_DESK,      /* Shift+D: the daily desk, as from the hall, whatever the machine */
} GamePageVerb;

typedef struct {
    GamePageVerb verb;
    int value;
} GamePageAction;

/* One frame: a button or click this frame, else one key. Tabs and boards
 * change here; everything else is main.c's to do. */
GamePageAction GamePage_Read(GamePage *p, const GamePageView *v, const KitEvent *ev, const Input *in);

/* --- Chipforge's Songs: the hall's song --- */
/* Entries: the built-in theme, then the library. */
int GamePage_SongCount(const GamePageData *d);
/* "Built-in theme", or the song's title (its file name if it has none). */
const char *GamePage_SongTitle(const GamePageData *d, int entry);
/* The list shows (Chipforge, Songs, the hall's song). */
bool GamePage_SongsShown(const GamePage *p, const GamePageView *v);
/* The entry a widget id is, or -1. */
int GamePage_SongOf(int widgetId);
/* The keys while that list shows: Up/Down go down the buttons, then the
 * songs, scrolling them, and round again; the wheel scrolls the songs.
 * Run after Kit_Update (the list has no navigation of its own then). True
 * if the focus moved. */
bool GamePage_SongKeys(GamePage *p, const GamePageView *v, KitState *k, const KitList *l, const Input *in, bool wheelFree);

/* The sentence under the page for the button the keys (or the pointer)
 * are on. */
void GamePage_Help(const GamePageView *v, const GamePageData *d, int id, char *out, size_t size);
/* The hint line, from the key table: the keyboard's keys or the pad's. */
void GamePage_Hint(const GamePage *p, const GamePageView *v, InputDevice hints, char *out, size_t size);

#endif
