#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdint.h>
#include "rng.h"

#define GRID_W 24
#define GRID_H 24
#define MAX_SNAKE (GRID_W * GRID_H)

#define START_LENGTH 4
#define SPAWN_HEAD_X 6
#define SPAWN_Y 12

#define TURN_QUEUE_LEN 2

#define FOOD_PER_LEVEL 5   /* classic/wrap: level goes up every N food */
#define FOOD_PER_STAGE 8   /* maze: food needed to clear a stage */
#define BONUS_EVERY 5      /* a bonus pickup appears after every Nth food */
#define BONUS_LIFETIME_STEPS 40
#define MAZE_LAYOUT_COUNT 6
#define RELIC_EVERY 7        /* a spirit relic appears after every Nth food */
#define RELIC_LIFETIME_STEPS 60
#define PHASE_STEPS 24       /* how long the snake can slide through itself */
#define SLOW_STEPS 30
#define SURGE_STEPS 36
#define SLOW_FACTOR 1.6f     /* step interval multiplier while slowed */
#define SURGE_MULT 2

/* Pause before the snake starts moving on a new game or a new maze stage,
 * so the player gets a look at the board first. */
#define START_DELAY_SECONDS 0.7f

typedef enum { DIR_UP, DIR_RIGHT, DIR_DOWN, DIR_LEFT } Dir;

typedef enum {
    MODE_CLASSIC, /* edges kill */
    MODE_WRAP,    /* edges wrap around */
    MODE_MAZE,    /* edges kill, wall layouts, stage-based */
    MODE_COUNT
} GameMode;

typedef enum {
    GS_PLAYING,
    GS_PAUSED,
    GS_GAMEOVER
} GamePhase;

typedef enum {
    CELL_EMPTY,
    CELL_SNAKE,
    CELL_WALL,
    CELL_FOOD,
    CELL_BONUS,
    CELL_RELIC
} CellType;

typedef enum { RELIC_PHASE, RELIC_SLOW, RELIC_SURGE, RELIC_COUNT } RelicType;

typedef enum {
    DEATH_NONE,
    DEATH_EDGE,
    DEATH_WALL,
    DEATH_SELF
} DeathCause;

typedef struct { int8_t x, y; } Cell;

/* Swallowed food shows as a bulge travelling down the body, one segment per
 * step. A handful can be in flight at once on a long snake. */
#define MAX_BULGES 6

typedef struct {
    uint8_t grid[GRID_H][GRID_W]; /* CellType per cell, kept in sync with body */

    /* Ring buffer: body[headIdx] is the head, older segments sit at
     * decreasing indices (mod MAX_SNAKE). */
    Cell body[MAX_SNAKE];
    int headIdx;
    int length;
    int growPending;

    Dir dir; /* direction of the most recent step */
    Dir turnQueue[TURN_QUEUE_LEN];
    int turnCount;

    Cell food;
    bool bonusActive;
    Cell bonus;
    int bonusStepsLeft;

    bool relicActive;
    Cell relic;
    RelicType relicType;
    int relicStepsLeft;
    int phaseSteps, slowSteps, surgeSteps; /* remaining steps of each spell; 0 = off */
    RelicType lastRelic;

    GameMode mode;
    GamePhase phase;
    DeathCause deathCause;
    bool won; /* board completely filled */

    float stepTimer;
    float stepInterval;
    float startDelay;

    long score;
    int level;      /* in maze mode this is the stage number */
    int foodEaten;
    int stageFood;  /* maze: food eaten on the current stage */

    /* Presentation hints. The rules never read these; they exist so the
     * renderer can glide the snake between cells instead of teleporting it
     * (the logic is always one step ahead of what is drawn). */
    int stepsOnBoard;        /* steps taken since the board was (re)built; 0 = nothing to glide from */
    Cell prevTail;           /* the cell the tail was in before the last step */
    bool tailMoved;          /* false on a step where the snake grew */
    int eatCount;            /* food + bonus eaten this run; bumps exactly once per pickup */
    Cell lastEatCell;        /* where the most recent pickup was */
    bool lastEatWasBonus;
    int stepsSinceEat;       /* 0 during the step interval right after eating */
    int bulgeAge[MAX_BULGES]; /* segments from the head; -1 = unused slot */

    /* Set for one frame, consumed by the caller to trigger sounds without
     * game.c depending on audio/render. */
    bool justAte;
    bool justAteBonus;
    bool justBonusSpawned;
    bool justLeveledUp;
    bool justDied;
    bool justRelicSpawned, justRelic, justPhaseEnded;

    long highScore;
    Rng rng;
} Game;

void Game_Init(Game *g, uint64_t seed, GameMode mode, long highScore);
/* Same mode and high score, fresh board. */
void Game_Restart(Game *g, uint64_t seed);

/* Advances timing and runs as many steps as dt covers. Only has effect
 * while phase == GS_PLAYING. dt in seconds. */
void Game_Update(Game *g, float dt);

/* Moves the snake exactly one cell. Exposed so tests can drive the game
 * without caring about timing. */
void Game_Step(Game *g);

/* Buffers a turn (up to TURN_QUEUE_LEN deep) so quick double-taps like
 * "up, then left" both register even within a single step. Reversals and
 * repeats of the direction already headed are rejected. Returns true if
 * the turn was queued. */
bool Game_QueueTurn(Game *g, Dir d);

void Game_TogglePause(Game *g);

/* Segment i of the snake, 0 = head, length-1 = tail. */
Cell Game_Segment(const Game *g, int i);

/* Seconds per step at a given level; shrinks as the level rises, floored so
 * the game stays humanly playable. */
float Game_StepIntervalForLevel(int level);

/* Which of the MAZE_LAYOUT_COUNT wall layouts a maze stage uses. */
int Game_LayoutForStage(int stage);
/* Pure wall-layout lookup, so render previews and tests don't need a Game. */
bool Maze_IsWall(int layout, int x, int y);

const char *Game_ModeName(GameMode mode);
const char *Game_RelicName(RelicType t);
/* Seconds per step right now (level pace, slowed by the Slow relic). */
float Game_CurrentInterval(const Game *g);
/* Lowercase identifier used in config/save/score files ("classic", ...). */
const char *Game_ModeKey(GameMode mode);

/* Clears the one-shot "just*" event flags after the caller has reacted to
 * them for this frame. */
void Game_ConsumeFrameFlags(Game *g);

#endif
