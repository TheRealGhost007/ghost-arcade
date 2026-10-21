#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdint.h>
#include "board.h"
#include "tetromino.h"
#include "rng.h"

typedef enum {
    GS_PLAYING,
    GS_PAUSED,
    GS_GAMEOVER
} GamePhase;

/* Dig (3.4.0) comes last so an older config.ini's setup_mode still names
 * the mode it did. */
typedef enum { MODE_MARATHON, MODE_SPRINT, MODE_ULTRA, MODE_ZEN, MODE_POWER, MODE_DIG, MODE_COUNT } GameMode;
/* Power-ups ride on one block of a piece (Power mode only) and fire when it locks. */
typedef enum { PU_NONE, PU_BOMB, PU_LASER, PU_FREEZE, PU_SLOW, PU_SWEEP, PU_COUNT } PowerUp;

/* The Practice room (3.5.0, REWORK_PLAN.md "Practice room"): a Marathon on
 * the board you set up, with rules bent for learning. Nothing a practice
 * run does is recorded (runrecord.c: it never counts). */
#define PRACTICE_LEVEL_MIN 1
#define PRACTICE_LEVEL_MAX 20
#define PRACTICE_GARBAGE_MAX 12
#define PRACTICE_UNDO_MAX 50
typedef struct {
    bool on;            /* this is a practice run */
    int startLevel;     /* PRACTICE_LEVEL_MIN .. PRACTICE_LEVEL_MAX: the level the run begins at (lines count up from it) */
    bool noGravity;     /* the piece never falls by itself (soft and hard drops still work) */
    bool noLock;        /* a landed piece never locks by itself (a hard drop locks it) */
    bool infiniteHold;  /* Hold as often as you like on one piece */
    int garbageRows;    /* grey rows the well starts with (Dig's rows, one gap each), 0 .. PRACTICE_GARBAGE_MAX */
    bool hints;         /* finesse hints: a lock that took more presses than needed says how many */
} PracticeRules;

/* The Daily challenge (3.6.0, REWORK_PLAN.md "Daily challenge"): a
 * two-minute score attack everyone plays with the same pieces (and
 * power-ups) today, seeded from the shared daily code (ghost-common/daily.h).
 * The twist changes with the date (challenge.h picks it from the seed): the
 * board, whether power-ups ride the pieces, and the level the run starts
 * at. The run plays as Ultra does on the clock, in the twist's mode (Ultra,
 * Power or Dig), and goes on the day's own board, "daily-YYYYMMDD". */
#define DAILY_SECONDS 120.0f
#define DAILY_LEVEL_MAX 20
typedef struct {
    bool on;            /* this is a daily run */
    int date;           /* YYYYMMDD: its board is daily-YYYYMMDD */
    int twist;          /* DailyTwist (challenge.h) */
    bool powerUps;      /* power-ups ride the pieces whatever the mode (Power Ultra) */
    int startLevel;     /* the level the run begins at (1, or the fast start's); lines count up from it */
} DailyRules;

typedef struct {
    GameMode mode;
    int width, height;   /* board size, clamped to the board limits */
    /* Combos, back-to-back, T-spins and perfect clears on top of the classic
     * table. Since 3.2.0 every board plays this way, the classic 10x20
     * included (REWORK_PLAN.md, "Modern rules"); false is kept for the tests
     * of the classic table alone. SRS turning does not depend on it. */
    bool modernScoring;
} GameConfig;

#define SPRINT_LINES 40
#define ULTRA_SECONDS 120.0f
#define POWER_CHANCE_PERCENT 14
#define FREEZE_SECONDS 6.0f
#define SLOW_SECONDS 12.0f
#define SLOW_FACTOR 2.5f

/* Dig (3.4.0, REWORK_PLAN.md "Dig mode"): the well starts with
 * DIG_START_ROWS grey garbage rows, each with one gap (never in the column
 * of the gap of the row under it, so no two neighbouring rows open the same
 * way), and another pushes up from the floor every DIG_RISE_START_SECONDS,
 * sooner as the run goes on: the wait shrinks evenly to DIG_RISE_END_SECONDS
 * over DIG_RISE_RAMP_SECONDS (Game_DigInterval). The warning bar fills over
 * each wait and is the warning in its last DIG_WARN_SHARE. A rise lifts the
 * falling piece with the stack when it would overlap; a block pushed above
 * the well, or a piece with nowhere to go, tops the run out. Every garbage
 * row a clear takes counts as Dug and adds DIG_ROW_BONUS times the level
 * (the level stays 1: the rises are the pressure, not gravity). The garbage
 * draws its gaps from its own rng, so a seed gives the same pieces as in
 * any other mode. */
#define DIG_START_ROWS 8
#define DIG_RISE_START_SECONDS 8.0f
#define DIG_RISE_END_SECONDS 3.0f
#define DIG_RISE_RAMP_SECONDS 150.0f
#define DIG_WARN_SHARE 0.25f
#define DIG_ROW_BONUS 100

#define LOCK_DELAY_SECONDS 0.5f
#define MAX_LOCK_RESETS 15
#define LINES_PER_LEVEL 10
/* The Next queue (3.2.0): the rules always draw this many pieces ahead
 * (next is the first of them), whatever Settings shows of it, so the
 * sequence of pieces for a seed is the same however many are shown. */
#define QUEUE_MAX 6
/* A T-spin (3.2.0): the T's last move before it locked was a turn, and
 * three of the four cells round its centre are filled (walls count). A
 * mini is one whose two front corners are not both filled, unless the turn
 * was a quarter turn that used the fifth kick (TSPIN_FULL_KICK) of the SRS
 * table (a half turn's kicks come from the 180 list, where index 4 is a
 * plain sideways step, so a half turn never gets the upgrade). */
#define TSPIN_FULL_KICK 4
typedef enum { TSPIN_NONE, TSPIN_MINI, TSPIN_FULL } TSpinKind;

/* What a run did, for the results card. Plain counters kept by the rules
 * code as it goes; the rules never read them, so they change no play. They
 * are kept on every board, the classic one included (where combos and
 * back-to-back do not score, they are still counted). */
typedef struct {
    int clears[5];     /* locks that cleared 1, 2, 3 or 4 rows ([0] is unused) */
    int combo;         /* the chain of clearing locks now, counted as the combo is: -1 none, 0 one clear, 1 two in a row */
    int bestCombo;     /* the longest chain this run, counted the same way (0 until two clears in a row) */
    bool lastQuad;     /* the last clear was difficult: a quad or a T-spin clear */
    int backToBacks;   /* difficult clears right after a difficult one (locks that clear nothing between them do not break it) */
    int powers;        /* power-ups fired */
    int holds;         /* holds made */
    int tspins;        /* T-spins locked, minis included, lines or not (3.2.0) */
    int perfectClears; /* clears that left the well empty (3.2.0) */
    int tspinClears[5];/* of clears[], the T-spin locks (minis included): 1, 2 or 3 rows ([0] and [4] unused; 3.3.0, the Stats screen's bar) */
} RunStats;

typedef struct {
    /* Bag-of-7 randomizer state. */
    PieceType bag[PIECE_COUNT];
    int bagPos; /* next unused index into bag; PIECE_COUNT means "refill" */
    Rng rng;
} Bag;

typedef struct {
    Board board;

    PieceType current;
    int rotation;
    int px, py; /* position of the piece's local-origin cell on the board */

    PieceType next;         /* the piece that comes in next: queue[0] (3.2.0: a test or the harness may set it alone, and it is honoured) */
    /* The Next queue (3.2.0): the QUEUE_MAX pieces to come, next first, each
     * with its power-up roll made as it was drawn, so the pieces and the
     * power-ups a seed gives are the same as when only one was drawn ahead. */
    PieceType queue[QUEUE_MAX];
    PowerUp queuePower[QUEUE_MAX];
    int queueCell[QUEUE_MAX];
    Bag bag;

    GamePhase phase;

    float fallTimer;
    float fallInterval;

    bool grounded;
    /* Lock delay: the piece locks after LOCK_DELAY_SECONDS on the ground. A
     * move or turn on the ground restarts that, at most MAX_LOCK_RESETS
     * times; the count comes back only when the piece reaches a row lower
     * than any it has been on (lowestRow). In the air the timer holds, and
     * once the resets are spent it keeps running there too, so lifting the
     * piece with a kick buys no time. */
    float lockTimer;
    int lockResets;
    int lowestRow;
    /* For T-spins (3.2.0): the last thing that moved the piece was a turn
     * (a step down, a slide or a drop clears it), and which kick that turn
     * used (0 = in place). lastTurnHalf (3.6.1): that turn was a half turn,
     * whose kick list is the 180 one, so its index says nothing of the
     * fifth SRS kick. */
    bool lastMoveWasTurn;
    int lastKick;
    bool lastTurnHalf;

    long score;
    int level;
    int linesCleared;

    /* Set for one frame after a lock event, consumed by the caller to
     * trigger sounds/animations without game.c depending on audio/render. */
    int lastClearCount;   /* 0..4 lines cleared by the most recent lock */
    bool lastWasLevelUp;
    bool justHardDropped;
    bool justLocked;
    bool justRotated;
    bool justMoved;
    bool justSoftDropped;   /* a soft-drop step this frame (the piece snaps down instead of gliding) */

    long highScore;
    int highLevel;
    int highLines;

    GameConfig cfg;
    PracticeRules practice; /* the Practice room's rules (3.5.0): all zero outside it; kept across Game_Restart like cfg */
    DailyRules daily;       /* the Daily challenge's (3.6.0): all zero outside it; kept across Game_Restart like cfg */
    PieceType hold;         /* -1 while empty */
    bool holdUsed;          /* one hold per piece */
    int combo;              /* consecutive line-clearing locks, -1 when broken */
    bool backToBack;        /* the last clear was a four-line one */
    float elapsed;          /* seconds of play */
    float timeLeft;         /* Ultra */
    bool won;               /* Sprint finished, or Ultra ran out: a completed run rather than a top-out */
    int zenResets;          /* Zen: how many times the board was swept for you */
    /* Dig (3.4.0) */
    Rng digRng;             /* the garbage's gaps (apart from the bag's, so the pieces stay a seed's) */
    float digTimer;         /* seconds into the wait for the next rise */
    float digInterval;      /* this wait's length (Game_DigInterval as it began) */
    int digRises;           /* rows that have pushed up */
    int dug;                /* garbage rows cleared this run */
    int digLastGap;         /* the gap column of the newest garbage row (-1: none yet) */
    int piecesLocked;       /* pieces locked this run; a run with none was never really played */
    RunStats stats;         /* what the run did (the results card); never read by the rules */
    /* Finesse (the Practice room's hints, 3.5.0): the presses made on the
     * piece falling now, counted by playstep.c (a tap or a held slide is
     * one, a turn is one; drops and Hold are not presses); 0 as each piece
     * comes in. The rules never read it. */
    int presses;

    PowerUp curPower, nextPower, holdPower;
    int curPowerCell, nextPowerCell, holdPowerCell; /* which of the piece's four blocks carries it */
    float freezeTimer, slowTimer;

    /* Events (cleared with the rest) */
    bool justHeld;
    int lastCombo;          /* combo count on the most recent clear */
    bool lastWasB2B;        /* that clear was back to back (a quad or a T-spin clear after one; paid half again); looks only, the rules never read it */
    TSpinKind lastTSpin;    /* the most recent lock was a T-spin (mini or full), lines or not (3.2.0) */
    bool lastPerfectClear;  /* the most recent lock's clear left the well empty (3.2.0) */
    long lastComboBonus;
    int lastDug;            /* garbage rows the most recent lock's clear took (Dig, 3.4.0) */
    bool justRose;          /* a garbage row pushed up this frame (Dig) */
    bool justRosePiece;     /* ...and lifted the falling piece with it */
    PowerUp lastPower;      /* fired by the most recent lock */
    int lastPowerX, lastPowerY; /* where it fired; after a Sweep, where its block went */
    int lastPowerRemoved;
    bool justZenReset;
    /* The Practice room (3.5.0). With hints on, the lock just made says how
     * many presses over the fewest it took (finesse.h): 0 was clean, -1 is
     * no hint (hints off, or a placement finesse does not count: a tuck or
     * a spin). justUndid: an undo put the game back this frame (practice.h
     * sets it on the restored game). */
    int lastFinesseExtra;
    int lastFinesseOptimal;
    int lastFinessePresses;
    bool justUndid;

    /* Presentation hints, valid on the frame justLocked is set. The rules
     * never read these; they let the renderer flash the piece that just
     * landed and the rows that just vanished (which are already gone from
     * the board by the time it draws). lastLockX/Y is where the piece's
     * blocks are after a power-up fired (a Sweep lowers them), and before
     * any line clear (which shows as the clear instead). */
    PieceType lastLockType;
    int lastLockRotation;
    int lastLockX, lastLockY;
    int lastDropCells;      /* rows a hard drop fell; 0 for a natural lock */
    int lastClearRows[4];   /* board rows cleared, as they were BEFORE compaction */
    /* The board as the piece was stamped into it, before the line clear and
     * any power-up (valid on the frame justLocked is set). The play screen
     * replays the clear from it: the full rows flash and burst, then the
     * rows above fall into the gap, ending on exactly the live board. */
    Board lockBoard;
    /* The board as it was the moment a power-up fired (after the clear,
     * before the power took anything), valid on the frame lastPower is set.
     * The play screen draws the Bomb's 3x3 blinking and the Laser's column
     * popping out from it; the rules never read it. */
    Board powerBoard;
} Game;

void Game_Init(Game *g, uint64_t seed, long highScore, int highLevel, int highLines);
/* Same, with a mode and a board size. */
void Game_InitConfig(Game *g, GameConfig cfg, uint64_t seed, long highScore, int highLevel, int highLines);
/* A Practice room run (3.5.0): the config's mode and board (the room is a
 * Marathon) with the rules bent as `rules` says, and no best. A restart
 * keeps the rules. */
void Game_InitPractice(Game *g, GameConfig cfg, PracticeRules rules, uint64_t seed);
/* A Daily challenge run (3.6.0): the twist's config with its rules and
 * the day's seed, no best. A restart keeps the rules (the caller seeds it
 * with the day's seed again, so the pieces come as they did). */
void Game_InitDaily(Game *g, GameConfig cfg, DailyRules rules, uint64_t seed);
/* The run is on a clock (Ultra, or any daily): timeLeft counts down and
 * the run ends, won, when it reaches 0. */
bool Game_Timed(const Game *g);
GameConfig Game_DefaultConfig(void);
const char *Game_ModeName(GameMode m);
const char *Game_PowerName(PowerUp p);
void Game_Restart(Game *g, uint64_t seed);

/* Advances gravity/lock-delay timing. dt in seconds. Only has effect while
 * phase == GS_PLAYING. */
void Game_Update(Game *g, float dt);

/* Seconds per row of gravity at a level: 1.0 at level 1, 0.07 less each
 * level, never under 0.08 (level 15 and up). */
float Game_FallInterval(int level);

bool Game_MoveLeft(Game *g);
bool Game_MoveRight(Game *g);
/* Manual soft-drop step (one row), awards score, resets gravity timer.
 * Returns false if the piece is already grounded. */
bool Game_SoftDrop(Game *g);
/* dir: +1 clockwise, -1 counter-clockwise, 2 a half turn (3.2.0). SRS wall
 * kicks (tetromino.h): the first offset of the table that fits is taken. */
bool Game_Rotate(Game *g, int dir);
void Game_HardDrop(Game *g);
/* Swap the falling piece with the held one (or stash it). Once per piece. */
bool Game_Hold(Game *g);

void Game_TogglePause(Game *g);

/* Cell occupied by the current piece's ghost (landing preview) at column/row
 * offsets identical to the active piece, but for the resting y. */
int Game_GhostY(const Game *g);

/* Clears the one-shot "just*" event flags after the caller has reacted to
 * them (sounds, animations) for this frame. */
void Game_ConsumeFrameFlags(Game *g);

/* A press on the piece falling now (playstep.c: a fresh Left or Right, or
 * a turn), for the Practice room's finesse hints. Counted in every run (it
 * costs nothing); only a practice run with hints on reads it. */
void Game_NotePress(Game *g);
/* The rules as the Practice room bends them (all false outside it). */
bool Game_IsPractice(const Game *g);

/* What a T locking where it is now would be (3.2.0): TSPIN_NONE unless its
 * last move was a turn and three of its corners are filled. Reads the
 * board round the piece only; the rules call it as the piece locks. */
TSpinKind Game_TSpinKind(const Game *g);
/* The points a lock earns before drop points, combo and the perfect clear
 * bonus (3.2.0), times the level: the classic table 0 100 300 500 800, a
 * T-spin 400 800 1200 1600, a mini 100 200 400. */
long Game_ClearPoints(int cleared, TSpinKind tspin);
/* The perfect clear bonus for a clear of `cleared` rows, before the level
 * (3.2.0): 800, 1200, 1800, 2000 (3200 for a back-to-back quad). */
long Game_PerfectClearBonus(int cleared, bool b2bQuad);

/* Dig (3.4.0). The wait before a rise, by how far into the run it begins:
 * DIG_RISE_START_SECONDS at 0, shrinking evenly to DIG_RISE_END_SECONDS at
 * DIG_RISE_RAMP_SECONDS and after. */
float Game_DigInterval(float elapsed);
/* How far the wait for the next rise has run, 0 to 1 (0 outside Dig): the
 * warning bar's fill. */
float Game_DigFill(const Game *g);
/* The bar is in its warning: the wait is in its last DIG_WARN_SHARE. */
bool Game_DigWarning(const Game *g);
/* Seconds until the next rise (0 outside Dig). */
float Game_DigSecondsLeft(const Game *g);
/* Fills `row` (counted with the hidden rows) with garbage but for one gap
 * at `gap`. */
void Game_GarbageRow(Board *b, int row, int gap);
/* The gap column of a row that is garbage but for one cell; -1 for any
 * other row. */
int Game_GarbageGap(const Board *b, int row);
/* Rows (visible ones) holding any garbage. */
int Game_GarbageRows(const Board *b);

#endif
