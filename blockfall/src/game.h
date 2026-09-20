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

typedef enum { MODE_MARATHON, MODE_SPRINT, MODE_ULTRA, MODE_ZEN, MODE_POWER, MODE_COUNT } GameMode;
/* Power-ups ride on one block of a piece (Power mode only) and fire when it locks. */
typedef enum { PU_NONE, PU_BOMB, PU_LASER, PU_FREEZE, PU_SLOW, PU_SWEEP, PU_COUNT } PowerUp;

typedef struct {
    GameMode mode;
    int width, height;   /* board size, clamped to the board limits */
    bool modernScoring;  /* combos and back-to-back on top of the classic table */
} GameConfig;

#define SPRINT_LINES 40
#define ULTRA_SECONDS 120.0f
#define POWER_CHANCE_PERCENT 14
#define FREEZE_SECONDS 6.0f
#define SLOW_SECONDS 12.0f
#define SLOW_FACTOR 2.5f

#define LOCK_DELAY_SECONDS 0.5f
#define MAX_LOCK_RESETS 15
#define LINES_PER_LEVEL 10

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

    PieceType next;
    Bag bag;

    GamePhase phase;

    float fallTimer;
    float fallInterval;

    bool grounded;
    float lockTimer;
    int lockResets;

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

    long highScore;
    int highLevel;
    int highLines;

    GameConfig cfg;
    PieceType hold;         /* -1 while empty */
    bool holdUsed;          /* one hold per piece */
    int combo;              /* consecutive line-clearing locks, -1 when broken */
    bool backToBack;        /* the last clear was a four-line one */
    float elapsed;          /* seconds of play */
    float timeLeft;         /* Ultra */
    bool won;               /* Sprint finished, or Ultra ran out: a completed run rather than a top-out */
    int zenResets;          /* Zen: how many times the board was swept for you */

    PowerUp curPower, nextPower, holdPower;
    int curPowerCell, nextPowerCell, holdPowerCell; /* which of the piece's four blocks carries it */
    float freezeTimer, slowTimer;

    /* Events (cleared with the rest) */
    bool justHeld;
    int lastCombo;          /* combo count on the most recent clear */
    long lastComboBonus;
    PowerUp lastPower;      /* fired by the most recent lock */
    int lastPowerX, lastPowerY;
    int lastPowerRemoved;
    bool justZenReset;

    /* Presentation hints, valid on the frame justLocked is set. The rules
     * never read these; they let the renderer flash the piece that just
     * landed and the rows that just vanished (which are already gone from
     * the board by the time it draws). */
    PieceType lastLockType;
    int lastLockRotation;
    int lastLockX, lastLockY;
    int lastDropCells;      /* rows a hard drop fell; 0 for a natural lock */
    int lastClearRows[4];   /* board rows cleared, as they were BEFORE compaction */
} Game;

void Game_Init(Game *g, uint64_t seed, long highScore, int highLevel, int highLines);
/* Same, with a mode and a board size. */
void Game_InitConfig(Game *g, GameConfig cfg, uint64_t seed, long highScore, int highLevel, int highLines);
GameConfig Game_DefaultConfig(void);
const char *Game_ModeName(GameMode m);
const char *Game_PowerName(PowerUp p);
void Game_Restart(Game *g, uint64_t seed);

/* Advances gravity/lock-delay timing. dt in seconds. Only has effect while
 * phase == GS_PLAYING. */
void Game_Update(Game *g, float dt);

bool Game_MoveLeft(Game *g);
bool Game_MoveRight(Game *g);
/* Manual soft-drop step (one row), awards score, resets gravity timer.
 * Returns false if the piece is already grounded. */
bool Game_SoftDrop(Game *g);
bool Game_Rotate(Game *g, int dir); /* dir: +1 clockwise, -1 counter-clockwise */
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

#endif
