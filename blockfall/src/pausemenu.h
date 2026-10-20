#ifndef PAUSEMENU_H
#define PAUSEMENU_H

#include <stdbool.h>
#include <stddef.h>
#include "game.h"
#include "uikit.h"
#include "uiinput.h"

/* The pause menu (REWORK_PLAN.md, "Pause menu with quick settings and 'are
 * you sure?'"), raylib-free so it is tested headlessly:
 *  - Esc, P, pad Start or pad B open it, and so does the window losing focus
 *    (it says so). The screen dims over 0.12 s and a card slides up 24 px,
 *    centred on the window so it fits every board: PAUSED, then Resume
 *    (picked first), Restart, Settings, How to play and Quit to menu. Esc,
 *    pad B, P and pad Start resume.
 *  - Settings turns the card into a quick panel: Effects volume, Music
 *    volume, Sound, Ghost piece and Screen shake, each changed at once, and
 *    Done. Esc or pad B go back to the list.
 *  - Restart and Quit to menu ask first when the run has points (the caller
 *    decides): an "are you sure?" box with Keep playing picked first, which
 *    Esc and pad B choose too. R and pad View ask the same from play.
 * The caller does what the menu answers (resume, restart, leave, open How to
 * play, change a setting) and plays the sounds. */

enum { PI_RESUME = 1, PI_RESTART, PI_SETTINGS, PI_HOWTO, PI_QUIT };
enum { PQ_EFFECTS = 20, PQ_MUSIC, PQ_SOUND, PQ_GHOST, PQ_SHAKE, PQ_DONE };
#define PAUSE_ITEMS 5
#define PAUSE_QUICK_ROWS 5

typedef enum { PAUSE_MAIN, PAUSE_QUICK } PausePage;
typedef enum { PAUSE_ASK_RESTART = 1, PAUSE_ASK_QUIT } PauseAsk;

/* The card at rest, centred on the 620x720 window. */
#define PAUSE_CARD_W 360
#define PAUSE_CARD_H 336
#define PAUSE_NOTE_H 12      /* the "window lost focus" line makes it this much taller */
#define PAUSE_BUTTON_W 300
#define PAUSE_BUTTON_H 40
#define PAUSE_PITCH 48
#define PAUSE_ROW_H 32
#define PAUSE_ROW_PITCH 40
#define PAUSE_SLIDER_W 176   /* the kit's ten blocks and the "none" cell before them */
#define PAUSE_TOGGLE_W 112
#define PAUSE_IN_SECONDS 0.12f /* the dim and the slide */
#define PAUSE_SLIDE_PX 24

/* What the quick panel shows. */
typedef struct {
    int effects, music; /* 0..10 blocks */
    bool sound, ghost, shake;
} QuickValues;

/* The keys a run reads its own way (actions.h): your Pause key or pad
 * Start, your Restart key or pad View, your Back key (Esc and pad B too). */
typedef struct {
    bool pause, restart, back;
} RunKeys;

typedef enum {
    PA_NONE,
    PA_RESUME,     /* close the menu and play on */
    PA_RESTART,    /* Restart, or R: ask first when the run has points, else restart */
    PA_QUIT,       /* Quit to menu: the same */
    PA_DO_RESTART, /* the box said Restart */
    PA_DO_QUIT,    /* the box said Leave */
    PA_HOWTO,
    PA_SET,        /* a quick setting changed: id and value */
} PauseActionKind;

typedef struct {
    PauseActionKind kind;
    int id, value;
    bool moved;  /* the focus moved by keys: a tick */
    bool opened; /* the quick panel opened: confirm */
    bool closed; /* back out of the panel or the box: back */
} PauseAction;

typedef struct {
    bool open;
    float t;         /* seconds since it opened */
    bool focusLost;  /* it opened because the window lost focus */
    PausePage page;
    KitState kit;
    KitGlide glide;
    KitDialog dialog; /* the "are you sure?" box */
    KitState dlgKit;
    bool askFromPlay; /* the box came from R or View in play: Keep playing goes straight back */
} PauseMenu;

void PauseMenu_Open(PauseMenu *m, bool focusLost);
void PauseMenu_Close(PauseMenu *m);
void PauseMenu_Tick(PauseMenu *m, float dt);
/* Opens the box over the menu (which opens too, if it was not). */
void PauseMenu_Ask(PauseMenu *m, PauseAsk what, const char *title, const char *body0, const char *body1, bool fromPlay);
bool PauseMenu_Asking(const PauseMenu *m);

/* Where the card is at rest. */
KitRect PauseMenu_Card(const PauseMenu *m);
/* How far below its place the card is drawn now (whole pixels), and how far
 * the screen behind has dimmed (0..1). Motion off: in place, dimmed. */
int PauseMenu_SlideOffset(const PauseMenu *m, bool motion);
float PauseMenu_Dim(const PauseMenu *m, bool motion);
/* The controls of the page that is up, at rest. */
void PauseMenu_Layout(const PauseMenu *m, const QuickValues *q, KitList *l);
/* A quick setting's whole row (its label at the left, its control at the
 * right), and its label. */
KitRect PauseMenu_RowRect(const PauseMenu *m, int id);
const char *PauseMenu_RowLabel(int id);

/* One frame of input. */
PauseAction PauseMenu_Update(PauseMenu *m, const QuickValues *q, const RunKeys *keys, const Input *in);

/* The box's words for leaving or restarting this run: what it would count
 * for ("It counts for 12 450 on Classic 10x20.") or why it would not. */
void PauseMenu_AskWords(PauseAsk what, const Game *g, bool tuned, const char *board, char *title, size_t titleSize,
                        char *body, size_t bodySize, char *act, size_t actSize);
/* Restart and Quit ask first only when the run has points. */
bool PauseMenu_ShouldAsk(const Game *g);

#endif
