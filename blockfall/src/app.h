#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include "game.h"
#include "persist.h"
#include "render.h"
#include "repeat.h"
#include "playstep.h"
#include "actions.h"
#include "controls.h"
#include "backdrop.h"
#include "menumodel.h"
#include "ghostlink.h"
#include "uiinput.h"
#include "uikit.h"
#include "uinav.h"
#include "wipe.h"
#include "pausemenu.h"
#include "results.h"
#include "setpage.h"
#include "howtodemo.h"

/* Blockfall as a stack of screens. Each screen is a file of its own
 * (scr_*.c, see screens.h); this is what they share: the player's settings
 * and save, the Back stack, and each screen's own state. The loop
 * (main.c) reads input once, updates the screen on top, and draws exactly
 * one frame per pass; a screen that opens another does so through
 * App_Open / App_Back / App_Home, and the new screen is entered, updated
 * with a quiet input (holds, no presses) and drawn in that same pass.
 * This part is raylib-free. */

typedef enum { SCR_MENU, SCR_SETUP, SCR_PLAY, SCR_SCORES, SCR_SETTINGS, SCR_HOWTO, SCR_UPDATES, SCR_COUNT } ScreenId;

/* The main menu's six items, top to bottom. */
enum { MI_START, MI_HOWTO, MI_SCORES, MI_SETTINGS, MI_UPDATES, MI_QUIT, MI_COUNT };

typedef struct {
    KitState kit;
    KitGlide glide;
    bool shown;        /* has been on screen this session */
    bool dropping;     /* the title's letters are still dropping */
    float dropT;
    int thuds;         /* letters whose thud has played */
    Skyline sky;
    bool bestsLoaded;
    char bestsBoard[40];
    char bests[4][24]; /* Marathon, Sprint, Ultra, Power on the board you last set up */
    char toast[64];    /* an achievement just earned */
    float toastAge;
    bool toastOn;
} MenuState;

/* Play setup's controls, by id. */
enum { SI_BACK = 1, SI_MODE0 = 10, SI_WIDTH = 20, SI_HEIGHT, SI_BOARD, SI_SCORES, SI_START };

typedef struct {
    KitState kit;
    KitGlide glide;
    HoldStep hold;     /* Width or Height held down */
    int holdDir;
    int startBind;     /* Enter, Space or A went down on Width or Height: it starts when let go */
    SizeTween preview; /* the preview's frame as drawn: it reshapes */
    int cacheMode, cacheW, cacheH; /* the bests below are for this */
    char yourBest[32], yourRank[24], worldBest[32], worldWho[40];
} SetupState;

typedef struct {
    Game game;
    RenderView view;
    PlayDriver driver; /* auto-repeat, READY-GO and the countdown after a pause (playstep.h) */
    int restartNonce;
    bool runSubmitted;
    bool prevGameOver;
    bool fresh;        /* the frame the run opened on: what is held counts, no press does */
    PauseMenu pause;   /* the pause menu and its "are you sure?" box (pausemenu.h) */
    Results results;   /* the end show and the results card (results.h) */
    char againChip[24]; /* the card's "again" chip as the last frame drew it ("" none): the shots harness reads it */
    char board[40];    /* the run's board: "Classic 10x20" */
    /* The run that just ended, for the Scores browser's blinking row. */
    bool endRecorded;
    long endValue;
    char endDate[GHOSTLINK_DATE_LEN];
} PlayState;

/* The Scores browser: a mode's tab, a board, this machine or the world, a
 * period. */
#define SCORES_BOARDS 24
typedef struct {
    int tab;           /* SCORE_TAB_* (scr_scores.c): Marathon, Sprint, Ultra, Power, Zen */
    int boardW, boardH;/* the board shown */
    bool old;          /* Sprint's old points board */
    bool world;
    int period;
    float reloadTimer;
    GhostScore entries[GHOSTLINK_TOP_N];
    int count;
    int boards;        /* the board picker's list for this tab */
    BoardSize board[SCORES_BOARDS];
    bool boardOld[SCORES_BOARDS];
    KitState kit;
    long oldBest;      /* save.dat's classic best, when it is above the classic table's top */
    /* A run just put on this board blinks (opened from the results card). */
    bool blinkOn;
    long blinkValue;
    char blinkName[GHOSTLINK_NAME_LEN];
    char blinkDate[GHOSTLINK_DATE_LEN];
    float blinkT;
} ScoresState;

/* Settings: its tab, the focus, a key or button being waited for, the
 * "Reset controls?" box, the note line, and Feel's strip (setpage.h). */
typedef struct {
    int tab;           /* SetTab, kept while the game runs */
    KitState kit;
    KitGlide glide;    /* the lit row's plate */
    Capture capture;
    KitDialog dialog;  /* Reset controls? */
    KitState dlgKit;
    char note[160];    /* what just happened ("C moved from Hold to Rotate left.") */
    int noteTone;      /* 0 dim, 1 gold, 2 light */
    float noteT;       /* seconds it has shown */
    FeelStrip strip;
} SettingsScreenState;

/* How to play: five pages, each with its live demo (howtodemo.h). On the
 * first launch only Basics shows. */
typedef struct {
    int page;          /* HowToPage */
    bool firstLaunch;  /* Basics only, with "More in How to play" */
    KitState kit;
    HowToDemo demo;
    int modeFocus;     /* Modes: the mode the demo plays */
    int powerFocus;    /* Power-ups: the tile the demo plays */
} HowToScreenState;

typedef struct {
    KitState kit;
    float scroll;      /* px from the top */
    float wheel;
    int contentH;      /* measured when drawn */
} UpdatesState;

typedef struct App {
    Settings settings;
    SaveData save;
    char username[GHOSTLINK_NAME_LEN];
    char iconPath[600];
    PlayKeys keys;      /* your keys, as key codes, for play */
    int ghostLook;      /* the ghost's look before the pause menu turned it off (0: none yet) */

    Nav nav;
    bool quit;
    int slideDir;       /* the screen on top slides in: 1 from the right, -1 from the left */
    float slideT;
    bool wentBack;      /* the last change of screen was a Back */
    bool changed;       /* the top changed this pass (main.c enters it) */
    Wipe wipe;          /* the block wipe into and out of a run (wipe.h) */

    InputState inputState;
    Input input;        /* this frame's */

    MenuState menu;
    SetupState setup;
    PlayState play;
    ScoresState scores;
    SettingsScreenState set;
    HowToScreenState howto;
    UpdatesState updates;
} App;

/* A screen on top of this one; Back comes here again. */
void App_Open(App *a, ScreenId s);
/* Back one screen (false at the root). */
bool App_Back(App *a);
/* Straight back to the main menu (leaving a run). */
void App_Home(App *a);
ScreenId App_Top(const App *a);
/* How far the screen on top is from its place this frame (whole pixels;
 * 0 with Reduced motion or once it is there). */
int App_SlideOffset(const App *a);
/* Going into or out of a run: the new screen is wiped in by blocks instead
 * of sliding (a plain cut with Reduced motion). */
void App_Wipe(App *a);
/* Moves the slide and the wipe on by a frame. */
void App_Tick(App *a, float dt);

#endif
