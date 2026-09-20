#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdint.h>
#include "cave.h"

/* Gemdive rules core (no raylib). Everything happens on a TICK: the player
 * moves first, then a cellular pass scans the cave top-left to bottom-right
 * moving boulders and gems (each at most once), then the crawlers move. There
 * is no randomness anywhere, so a string of inputs always plays out the same
 * way -- which is what lets every shipped cave carry a recorded solution. */

#define COLS CAVE_COLS
#define ROWS CAVE_ROWS
#define MAX_CRAWLERS 14
#define MAX_BLASTS 24
#define START_LIVES 3
#define MAX_LIVES 6
#define EXTRA_LIFE_EVERY 3000
#define TICK_SECONDS 0.11f
#define INTRO_SECONDS 1.5f
#define DYING_SECONDS 1.5f
#define CLEAR_SECONDS 2.2f
#define GEM_POINTS 10
#define GEM_POINTS_OPEN 15
#define TIME_BONUS_PER_SECOND 2
#define CLEAR_BONUS 100

typedef enum { GS_PLAYING, GS_PAUSED, GS_GAMEOVER } GamePhase;
typedef enum { ST_INTRO, ST_PLAY, ST_DYING, ST_CLEAR } LevelState;
typedef enum { DEATH_NONE, DEATH_CRUSHED, DEATH_BLAST, DEATH_TIME, DEATH_CRAWLER } DeathCause;

typedef enum {
    C_EMPTY = 0, C_DIRT, C_STEEL, C_BRICK, C_BOULDER, C_GEM, C_EXIT,
} Cell;

typedef struct { int x, y, dir, kind; bool alive; } Crawler; /* dir 0 up 1 right 2 down 3 left; kind 0 fire, 1 gem */
typedef struct { int x, y, kind; } Blast;                    /* kind 1 = turned to gems */

typedef struct {
    GamePhase phase;
    LevelState state;
    float stateTimer;

    const Cave *set;
    int setCount;
    int level;            /* 1-based, keeps counting past the last cave */
    int round;            /* how many times the set has been cycled */
    const Cave *cave;

    uint8_t cell[ROWS][COLS];
    uint8_t falling[ROWS][COLS];
    uint8_t moved[ROWS][COLS];

    int px, py, facing;
    bool playerAlive;
    DeathCause death;
    Crawler crawlers[MAX_CRAWLERS];
    int crawlerCount;

    int gems, quota;
    bool exitOpen;
    float timeLeft, timeLimit;
    long tickCount;

    int lives;
    long score, highScore, nextExtra;
    long lastClearBonus;

    int heldDx, heldDy;       /* direction currently held */
    int pendingDx, pendingDy; /* a tap that has not had its tick yet */
    float tickAccumulator;

    /* One-frame events, cleared by Game_ConsumeFrameFlags. */
    bool justGem, justDig, justPush, justRockLand, justExplode, justExitOpen, justStep;
    bool justDeath, justClear, justGameOver, justExtraLife, justTimeWarning, justNewLevel;
    Blast blasts[MAX_BLASTS];
    int blastCount;
} Game;

/* `set` must outlive the Game (it is only pointed at, never copied). */
void Game_Init(Game *g, const Cave *set, int setCount, long highScore);
void Game_Restart(Game *g, long highScore);
void Game_StartLevel(Game *g, int level);
void Game_SetInput(Game *g, int dx, int dy);
void Game_Update(Game *g, float dt);
void Game_Tick(Game *g);
void Game_TogglePause(Game *g);
void Game_ConsumeFrameFlags(Game *g);

/* Seconds per tick: it speeds up a little each time the whole set is cycled. */
float Game_TickSeconds(const Game *g);
/* A tick using one solution character: U D L R or anything else to wait. */
void Game_TickWithMove(Game *g, char move);
bool Game_IsCleared(const Game *g);

#endif
