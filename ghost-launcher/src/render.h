#ifndef RENDER_H
#define RENDER_H

#include <stdbool.h>
#include "manifest.h"
#include "stats.h"
#include "runstats.h"
#include "../sync/updatecheck.h"
#include "profile.h"
#include "scores.h"
#include "launch.h"
#include "kit.h"
#include "screens.h"
#include "textfield.h"
#include "settings.h"
#include "hall.h"
#include "lineup.h"
#include "gamepage.h"
#include "gameform.h"
#include "desk.h"
#include "song.h"

/* Wide enough for a row of five arcade cabinets (the selected one full
 * size in the middle, two smaller ones either side). */
#define WINDOW_WIDTH 960
#define WINDOW_HEIGHT 640

/* Every Render_* below draws one screen into the frame main.c has begun
 * (Win_BeginFrame), so an overlay (the Arcade menu, a dialog, the "Saved"
 * note) can be drawn on top before the frame ends. Each takes the screen's
 * button list (screens.h) as a KitView and draws its buttons from it. */

void Render_Init(const char *assetsDir);
void Render_Shutdown(void);

/* Icons that failed to load are remembered so a missing file isn't retried
 * every frame. Call this when files may have appeared (an install just
 * finished) to give them another chance. */
void Render_ForgetMissingIcons(void);

/* Everything the instruction card under the cabinets says about the
 * selected game. main.c refreshes it when the selection changes (and on a
 * slow timer, so a background sync shows up) -- never per frame. */
typedef struct {
    ScoreBest localBest;
    ScoreBest worldBest;
    double playSeconds;
    const char *playerName; /* name this game will play under; "" if none set */
    bool nameIsOverride;    /* playerName is a per-game override, not the arcade name */
    bool installed;         /* the catalog's exec exists */
    bool canInstall;        /* not installed, but the catalog has a source folder */
    bool supportsDaily;     /* installed, and plays a daily challenge (D) */
    bool dailyGame;         /* plays a daily challenge once installed */
    InstallStatus install;  /* state of an install OF THIS GAME (IDLE if none) */
    bool tool;              /* Chipforge: its library and the hall's song instead of scores */
    int songs;              /* songs in the player's library */
    char hallSong[48];      /* what the hall plays: "Built-in theme" or the song's title */
} GameCard;

/* Chipforge's machine is a jukebox: arched top, glowing tubes, and a screen
 * that scrolls the notes of the hall's song (jukebox.h), or with the music
 * off shows the song's title card. */
typedef struct {
    const Song *song;   /* what the hall plays, as notes */
    double seconds;     /* how long it has been playing */
    bool on;            /* Hall music is on */
    const char *title;  /* "Built-in theme" or the song's title */
} JukeView;

/* The arcade floor: one cabinet per machine on the row, in the row's order,
 * the selected one lit, where the row is now (main.c steps it before
 * drawing, so the mouse hits what is drawn). Machines that aren't installed
 * stay dark with a paper sign on the screen; favourites wear a gold star.
 * The card under them says what the selected one is, its scores, and one
 * status line; the header, the card's Favourite and the control strip are
 * the view's buttons. coinT: see COIN_ANIM_SECONDS below. */
typedef struct {
    const Manifest *m;
    const Lineup *lineup;      /* the row: place -> catalog index */
    int selected;              /* catalog index of the lit machine */
    int place;                 /* ...and its place in the row */
    int order;                 /* RowOrder: named over the row unless it is your own */
    bool allHidden;            /* the row is empty because every machine is hidden */
    const bool *installed;     /* per catalog index: its program exists */
    const RowPrefs *prefs;     /* per catalog index: which machines are favourites */
    const GameCard *card;      /* NULL when the catalog is empty */
    const char *arcadeName;    /* "" if unset */
    bool admin, updateBehind;
    const HallRow *row;
    float dt;                  /* the room's light eases toward the selected machine */
    float coinT;
    const HallStatus *status;  /* the card's line */
    int lit;                   /* catalog index of the machine lit in the middle: attract mode tours */
    float cardOut;             /* attract mode: 0 the card in place .. 1 slid away */
    float cardSlide;           /* ...that is, this far down */
    bool attract;              /* attract mode: PRESS ANY BUTTON where the card was */
    const Desk *desk;          /* the chalkboard's count (none without a daily game on the row) */
    const JukeView *juke;      /* Chipforge's screen */
} HallScene;
void Render_List(const HallScene *s, const KitView *kv);

/* The coin-drop that plays between pressing Enter and the game starting.
 * coinT runs 0..1 over COIN_ANIM_SECONDS (pass a negative value when no
 * coin is in flight); the coin reaches the slot at COIN_DING_AT, which is
 * when main.c plays the chime. */
#define COIN_ANIM_SECONDS 0.95f
#define COIN_DING_AT 0.58f

/* Admin's Add / Edit form (gameform.h): five boxes, the one with the keys
 * lit with its cursor, a path's "found" / "not found" beside its prompt;
 * under them what a rename leaves behind (machines.h words, "" for none)
 * and the "Carry them over" switch, the error, Save and Cancel. */
typedef struct {
    const char *title;
    const GameForm *form;
    const char *stays, *scores, *moves;
} FormScene;
void Render_GameForm(const FormScene *s, const KitView *kv);

/* A machine's page (gamepage.h). */
typedef struct {
    const GameEntry *game;
    const GamePageData *data;
    const GamePage *page;
    const GamePageView *view;
    bool installed;           /* its program is there: the demo plays */
    bool installing;
    bool dailyGame;           /* it has a daily: Scores says how today's went */
    const HallStatus *status; /* under the buttons: a note, an error, an install */
    const char *help;         /* the chosen button's sentence, or why it is faint */
    const char *hint;
    long long syncedAt, now;  /* when the world boards came down, in seconds; 0 = never */
    bool sharing;             /* Share my scores is on */
    const JukeView *juke;     /* Chipforge: its big screen is the jukebox's */
} PageScene;
void Render_GamePage(const PageScene *s, const KitView *kv);

/* The Daily challenge desk (desk.h): today, the streak, the last fourteen
 * days, and a row per daily game, the view's rows lit as buttons. */
typedef struct {
    const Desk *desk;
    int top;                  /* the first row shown */
    const char *help, *hint;
    long long syncedAt, now;  /* when the world boards came down, in seconds; 0 = never */
    bool sharing;
} DeskScene;
void Render_Desk(const DeskScene *s, const KitView *kv);

/* Local playtime board: total time the launcher has been open, and
 * per-game cumulative time, most-played first. top = the first row shown
 * (STATS_VISIBLE fit). */
void Render_Stats(const PlayStats *stats, const Manifest *m, const RunSummary *sums, int top, const KitView *kv);

/* Settings: the page settings.c laid out, the name being typed (if any),
 * and the buttons' state (while typing, the typing layer's). */
void Render_Settings(const SettingsPage *page, const Settings *s, const KitView *kv);
/* The trophy room: one game's achievements at a time (selected = catalog
 * index), from achievement `top` (TROPHY_VISIBLE fit). */
void Render_Achievements(const Manifest *m, int selected, int top, const KitView *kv);
/* The opt-in question for the online leaderboard. The answers are the
 * view's buttons; firstTime changes the wording of the footer; sharingNow
 * is the saved answer, said under the buttons on later visits. */
void Render_OnlineAsk(bool firstTime, bool sharingNow, const KitView *kv);

/* The details screen: this build, the newest on GitHub, how to update, and
 * Check now. */
typedef struct {
    bool checking;   /* a check is under way: the button says Checking... */
    bool noAnswer;   /* the last Check now got no answer in time */
    bool checksOff;  /* check_updates=0 */
} UpdateView;
void Render_Update(const UpdateState *u, const char *sourceDir, const UpdateView *v, const KitView *kv);

/* The Arcade menu, over the hall that was just drawn: the hall dims and the
 * panel covers its left wall. */
void Render_ArcadeMenu(const KitView *kv);
/* An "are you sure?" box over whatever was just drawn. */
void Render_Dialog(const KitDialog *d, const KitView *kv);
/* "Saved" or "Couldn't save" (bad), centred at the top, fading out with
 * alpha. onHall: the hall's header is full, so it hangs just under it. */
void Render_Toast(const char *text, bool bad, float alpha, bool onHall);


#endif
