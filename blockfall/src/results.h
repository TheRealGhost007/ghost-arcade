#ifndef RESULTS_H
#define RESULTS_H

#include <stdbool.h>
#include <stddef.h>
#include "game.h"
#include "runrecord.h"
#include "pausemenu.h"
#include "uikit.h"
#include "uiinput.h"

/* The end of a run and the results card (REWORK_PLAN.md, "Results card
 * after every run"), raylib-free so it is tested headlessly:
 *  - A short end show on the board. A top-out blinks the blocked piece red,
 *    then the rows turn grey from the bottom up; a Sprint clear bursts the
 *    rows upward in their own colours; Ultra's time up dims them.
 *  - Then a card drops in, centred on the window so it fits every board:
 *    GAME OVER (red), SPRINT CLEAR (violet) or TIME UP (gold). The score
 *    counts up with rising ticks (a finished Sprint shows its time). A new
 *    best stamps a gold NEW BEST with confetti and the new-best jingle;
 *    otherwise it says how far the best is ("Best 26 714, 11 694 to go").
 *    Its place on this machine and where it would stand on the world board,
 *    a trophy just earned, the run in numbers, and the buttons Play again
 *    (picked first, also R or pad View), Change setup, Scores and Menu
 *    (also Esc or pad B).
 *  - Input is ignored for the first 0.4 s, so a hard drop still held as the
 *    stack tops out does not skip it. After that any press during the show
 *    jumps to its end, and R or Esc act at once.
 * Motion: Reduced: no drop, a 0.2 s count-up. Reduced flashing: the blocked
 * piece holds steady red. */

typedef enum { END_TOPOUT, END_SPRINT_CLEAR, END_TIME_UP } EndKind;

#define END_INPUT_GATE 0.4f
#define END_PIECE_BLINK 0.3f   /* the blocked piece blinks red twice */
#define END_ROWS_START 0.3f
#define END_ROWS_SECONDS 0.6f  /* the rows turn (grey, burst, dim) over this */
#define END_ROW_STEP 0.025f    /* a row each, capped so a tall well still takes 0.6 s */
#define END_CARD_AT 0.9f
#define END_CARD_DROP 0.18f
#define END_CARD_DROP_PX 24
#define END_COUNT_AT 1.1f
#define END_COUNT_SECONDS 0.6f
#define END_COUNT_REDUCED 0.2f
#define END_TICK_STEP 0.05f
#define END_DIM 0.7f           /* how dark the screen gets behind the card */

/* The card at rest, centred on the 620x720 window. */
#define RESULTS_CARD_W 480
#define RESULTS_CARD_H 512
#define RESULTS_STATS 10

enum { RI_AGAIN = 1, RI_SETUP, RI_SCORES, RI_MENU };

/* What the card says. */
typedef struct {
    EndKind kind;
    char title[16];        /* GAME OVER, SPRINT CLEAR, TIME UP */
    char sub[64];          /* "Marathon on Classic 10x20" */
    bool timeValue;        /* the big number is a time (a finished Sprint) */
    long bigValue;         /* the score, or the time in hundredths */
    bool newBest;
    char bestLine[64];     /* "Best 26 714, 11 694 to go"; under NEW BEST, "Old best 24 680" */
    char rankLine[80];     /* "#3 on this machine    World: would be #14" */
    char note[80];         /* a trophy just earned, or why the run is not ranked */
    bool noteGold;
    char statLabel[RESULTS_STATS][16];
    char statValue[RESULTS_STATS][16];
} ResultsCard;

/* What the play screen knows about the run as it ends. */
typedef struct {
    bool tuned;
    long best;          /* the board's best when the run began (the panel's Best); 0 = none */
    int rankHere;       /* its place on this machine's board after it was put there; 0 = not in the top ten */
    int worldRows;      /* rows on the world board ghost-sync last downloaded (0: none yet) */
    int worldRank;      /* where it would stand among them */
    const char *trophy; /* a trophy this run earned, or NULL */
    const char *board;  /* "Classic 10x20" */
} RunFacts;

void Results_Build(ResultsCard *c, const Game *g, const RunFacts *f);
EndKind Results_KindOf(const Game *g);
/* Where a run that ranks `value` would stand on a board: 1 + the rows above
 * it that are not the player's own (the world board keeps one row a player). */
int Results_WouldRank(const GhostScore *rows, int count, long value, const char *username);
/* The run's place on this machine's board (rows, best first, the run among
 * them): its own row's place, one lower when a best with no row of its own
 * is above it (the classic Marathon's save.dat best, from before the score
 * tables, which Play setup counts the same way). 0 when it is not in the
 * top ten. rowlessBest: that best, or 0. */
int Results_RankHere(const GhostScore *rows, int count, const char *username, long value, const char *date, long rowlessBest);

typedef struct {
    bool on;
    float t;       /* seconds since the run ended */
    bool motion;   /* Motion was Full as it ended */
    bool counted;  /* the count-up has finished (the stamp went off) */
    float lastTick;
    KitState kit;
    KitGlide glide;
    ResultsCard card;
} Results;

void Results_Begin(Results *r, const ResultsCard *card, bool motion);
void Results_End(Results *r);

/* The show's timeline. */
float Results_CountSeconds(const Results *r);
float Results_DoneAt(const Results *r);       /* the count-up's end: the stamp */
bool Results_InputOpen(const Results *r);
bool Results_Done(const Results *r);
/* On the board: the rows from the bottom that have turned by now (grey for a
 * top-out, burst for a Sprint clear), the time-up dim (0..1), and whether
 * the blocked piece is lit red this frame. */
float Results_RowStep(int rows);
int Results_RowsTurned(const Results *r, int rows);
float Results_RowsDim(const Results *r);
bool Results_PieceRed(const Results *r, bool calm);
/* Behind the card: how far the screen has dimmed (0..1), and how far above
 * its place the card still is (whole pixels). The card shows from
 * END_CARD_AT. */
bool Results_CardShown(const Results *r);
float Results_ScreenDim(const Results *r);
int Results_CardDrop(const Results *r);
/* The number as it counts up. */
long Results_Shown(const Results *r);

KitRect Results_CardRect(void);
void Results_Layout(const Results *r, KitList *l);
/* A stat's cell (label at the left, value at the right). */
KitRect Results_StatRect(int i);

typedef enum { RA_NONE, RA_AGAIN, RA_SETUP, RA_SCORES, RA_MENU } ResultsActionKind;
typedef struct {
    ResultsActionKind action;
    bool skipped;   /* a press jumped to the end of the show */
    bool moved;     /* the focus moved by keys */
    bool tick;      /* a count-up tick this frame */
    float tickPitch;
    bool stamp;     /* the count-up just finished: NEW BEST stamps now, if it is one */
} ResultsStep;
/* One frame: input (after the first 0.4 s), then the clock. */
ResultsStep Results_Update(Results *r, const RunKeys *keys, const Input *in, float dt);

#endif
