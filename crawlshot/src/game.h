#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdint.h>
#include "rng.h"

/* Crawlshot: a caterpillar winds down through a toadstool field toward you.
 * Shoot a segment and it becomes a toadstool and the chain splits in two,
 * each half with a head of its own -- the game in one sentence.
 *
 * The world is 30 columns by 32 rows of tiles. You are confined to the
 * bottom six rows (the "garden"); the caterpillar, the spider, the flea and
 * the scorpion all come at that from above and the sides. Everything on a
 * tile grid moves in whole tiles on a timer (the caterpillar) or in floats
 * (you, the shots, the other creatures). */
#define COLS 30
#define ROWS 32
#define PLAYER_TOP 26 /* first row of the garden */
#define MAX_WORMS 16
#define MAX_SEGS 16
#define WORM_TOTAL 12 /* segments in a full caterpillar */
#define MUSH_HP 4

#define PLAYER_SPEED 9.0f
#define BULLET_SPEED 42.0f
#define START_LIVES 3
#define MAX_LIVES 6
#define EXTRA_LIFE_EVERY 12000
#define DEATH_SECONDS 1.1f
#define RESTORE_INTERVAL 0.07f
#define FLEA_THRESHOLD 5 /* the flea comes when fewer toadstools than this are left in the garden */
#define MAX_KILLS_PER_FRAME 8

#define MAX_PICKUPS 3
#define PICKUP_FALL 4.0f          /* tiles per second */
#define PICKUP_DROP_ODDS 16       /* one destroyed toadstool in this many drops a spore */
#define PIERCE_SECONDS 9.0f
#define FREEZE_SECONDS 4.0f
#define BLAST_CHARGES 6

#define STEP_HZ 120
#define STEP_DT (1.0f / STEP_HZ)

typedef enum { GS_PLAYING, GS_PAUSED, GS_GAMEOVER } GamePhase;
typedef enum {
    KILL_HEAD, KILL_BODY, KILL_SPIDER, KILL_FLEA, KILL_SCORPION, KILL_MUSHROOM
} KillKind;

typedef struct { int x, y; } Cell;

typedef struct {
    bool active;
    int len;
    Cell seg[MAX_SEGS];  /* seg[0] is the head; the rest follow its path */
    int dir;             /* +1 heading right, -1 left */
    int vdir;            /* +1 dropping toward the bottom, -1 climbing back up the garden */
    bool diving;         /* poisoned: straight down to the bottom regardless of anything */
    float enterDelay;    /* a single head waiting to come on */
} Worm;

typedef struct { float x, y; bool active; } Bullet;

typedef enum { PU_PIERCE, PU_BLAST, PU_FREEZE, PU_COUNT } PowerType;
typedef struct { float x, y; PowerType type; bool active; } Pickup;

typedef struct {
    bool active;
    float x, y, vx, vy;
    float turnTimer;
} Spider;

typedef struct {
    bool active;
    int col;
    float y;
    int hits;
} Flea;

typedef struct {
    bool active;
    float x;
    int row;
    int dir;
} Scorpion;

typedef struct {
    float x, y;  /* where it happened, in tiles (centre) */
    int kind;    /* KillKind */
    int points;
} Kill;

typedef struct {
    uint8_t mush[ROWS][COLS];   /* 0 = none, 1..4 hit points */
    uint8_t poison[ROWS][COLS]; /* 1 = poisoned by a scorpion */

    Worm worms[MAX_WORMS];
    float wormTimer;

    float px, py;     /* the player's centre, in tiles */
    float moveX, moveY;
    bool fireHeld;
    Bullet bullet;
    Pickup pickups[MAX_PICKUPS];
    float pierceTimer, freezeTimer;
    int blastCharges;
    int pierceCx, pierceCy; /* the cell a piercing shot last went through */

    Spider spider;    float spiderTimer;
    Flea flea;        float fleaTimer;
    Scorpion scorpion; float scorpionTimer;

    GamePhase phase;
    int lives, wave;
    long score, highScore, nextExtraAt;
    bool alive;
    float deathTimer;
    bool restoring;
    float restoreTimer;
    float stepAccumulator;

    /* One-frame events for sound/effects; cleared by Game_ConsumeFrameFlags. */
    bool justFired, justMushroomHit, justPlayerDeath, justSpiderAppeared, justFleaAppeared, justScorpionAppeared;
    bool justPowerUp, justBlast;
    PowerType lastPower;
    bool justRestoreTick, justWaveClear, justExtraLife, justGameOver, justSplit;
    Kill kills[MAX_KILLS_PER_FRAME];
    int killCount;

    Rng rng;
} Game;

void Game_Init(Game *g, uint64_t seed, long highScore);
void Game_Restart(Game *g, uint64_t seed);
void Game_StartWave(Game *g, int wave);

/* move in [-1,1] on each axis; fire is a held state (one shot at a time). */
void Game_SetInput(Game *g, float moveX, float moveY, bool fire);
void Game_Update(Game *g, float dt);
void Game_Step(Game *g); /* exactly one 1/120 s step */
void Game_TogglePause(Game *g);
void Game_ConsumeFrameFlags(Game *g);
const char *Game_PowerName(PowerType t);

/* --- Pure helpers, shared with the renderer and the tests --- */
/* A wave's caterpillars: one long one and (wave - 1) single heads, twelve
 * segments in all, up to eight separate crawlers. */
void Game_WaveLayout(int wave, int *mainLen, int *singles);
/* Seconds between caterpillar steps; shorter every wave down to a floor. */
float Game_WormInterval(int wave);
/* Spider's value by how close it is to you when shot. */
int Game_SpiderPoints(float distance);
int Game_MushroomsInGarden(const Game *g);
int Game_WormCount(const Game *g);
int Game_SegmentCount(const Game *g);

#endif
