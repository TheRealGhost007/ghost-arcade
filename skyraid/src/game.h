#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdint.h>
#include "rng.h"

/* All positions are in field pixels: (0,0) is the top-left of the playfield,
 * y grows down. The renderer places the field on the window. */
#define FIELD_W 572
#define FIELD_H 572

#define INV_COLS 11
#define INV_ROWS 5
#define INV_W 24          /* sprite: 12x8 art pixels at 2x */
#define INV_H 16
#define INV_CELL_W 40
#define INV_CELL_H 32
#define MARCH_DX 8
#define MARCH_DROP 16
#define FORMATION_MARGIN 8
#define FORMATION_START_Y 64
#define WAVE_LOWER_STEP 16  /* each wave starts this much lower... */
#define WAVE_LOWER_MAX 6    /* ...up to this many times */

#define PLAYER_W 26
#define PLAYER_H 16
#define PLAYER_Y 524
#define PLAYER_SPEED 270.0f
#define SHOT_W 4
#define SHOT_H 12
#define PLAYER_SHOT_SPEED 640.0f
#define MAX_ENEMY_SHOTS 3

#define BUNKER_COUNT 4
#define BUNKER_COLS 22
#define BUNKER_ROWS 16
#define BUNKER_CELL 3
#define BUNKER_Y 440
#define BUNKER_FIRST_X 62
#define BUNKER_SPACING 128

#define UFO_W 32
#define UFO_H 14
#define UFO_Y 28
#define UFO_SPEED 120.0f

#define START_LIVES 3
#define MAX_LIVES 5
#define EXTRA_LIFE_SCORE 1500
#define RESPAWN_SECONDS 1.2f
#define MAX_KILLS_PER_FRAME 4

#define MAX_EXTRA_SHOTS 4
#define MAX_CAPSULES 3
#define CAPSULE_W 20
#define CAPSULE_H 12
#define CAPSULE_SPEED 90.0f
#define CAPSULE_DROP_ODDS 14   /* one invader kill in this many drops a capsule */
#define POWER_SECONDS 9.0f
#define RAPID_COOLDOWN 0.16f

#define STEP_HZ 120
#define STEP_DT (1.0f / STEP_HZ)

typedef enum { GS_PLAYING, GS_PAUSED, GS_GAMEOVER } GamePhase;

typedef enum { PU_RAPID, PU_TWIN, PU_SHIELD, PU_NOVA, PU_COUNT } PowerType;

typedef struct {
    float x, y;
    bool active;
} Shot;

typedef struct {
    float x, y; /* centre */
    PowerType type;
    bool active;
} Capsule;

typedef struct {
    float x, y; /* centre of what died */
    int row;    /* invader row 0-4, or -1 for the mystery ship */
    int points;
} Kill;

typedef struct {
    bool alive[INV_ROWS][INV_COLS];
    int aliveCount;
    float formX, formY; /* top-left of column 0 / row 0 */
    int marchDir;       /* +1 right, -1 left */
    float marchTimer;
    int marchFrame;     /* 0/1: which of the two sprite poses */
    int marchNote;      /* 0-3: the four-note bass line */

    float playerX; /* centre */
    float moveDir;
    bool fireHeld;
    Shot playerShot;
    Shot extraShots[MAX_EXTRA_SHOTS]; /* only used while a power-up lets more than one fly */
    Capsule capsules[MAX_CAPSULES];
    float rapidTimer, twinTimer, fireCooldown;
    bool shield;
    int shotsFired; /* drives the mystery ship's score, as in the original */
    Shot enemyShots[MAX_ENEMY_SHOTS];
    float enemyFireTimer;

    uint8_t bunker[BUNKER_COUNT][BUNKER_ROWS][BUNKER_COLS]; /* 1 = solid */

    bool ufoActive;
    float ufoX;
    int ufoDir;
    float ufoTimer;

    GamePhase phase;
    int lives;
    int wave;
    long score;
    long highScore;
    bool extraLifeGiven;
    bool invaded;        /* game over because the formation reached the ground */
    float respawnTimer;  /* > 0: the player just died and the world is frozen */
    float stepAccumulator;

    /* One-frame events for sound/effects; cleared by Game_ConsumeFrameFlags. */
    bool justFired, justMarched, justPlayerHit, justBunkerHit;
    bool justUfoAppeared, justWaveClear, justGameOver, justExtraLife;
    bool justPowerUp, justShieldHit, justNova;
    PowerType lastPower;
    Kill kills[MAX_KILLS_PER_FRAME];
    int killCount;

    Rng rng;
} Game;

/* Live-tuning hook: point these at knobs (the F2 panel); NULL leaves the constant. */
void Game_SetTuning(const float *fireDelayMult, const float *playerSpeed);

void Game_Init(Game *g, uint64_t seed, long highScore);
void Game_Restart(Game *g, uint64_t seed);
void Game_StartWave(Game *g, int wave);

void Game_SetInput(Game *g, float moveDir, bool fire);
void Game_Update(Game *g, float dt);
void Game_Step(Game *g); /* exactly one 1/120 s step */
void Game_TogglePause(Game *g);
void Game_ConsumeFrameFlags(Game *g);
const char *Game_PowerName(PowerType t);
int Game_ActiveShots(const Game *g);

/* Seconds between march steps. The formation speeds up as it thins out --
 * the whole game's tension curve -- and a little more each wave. */
float Game_MarchInterval(int aliveCount, int wave);
/* Points for an invader in a given row (top row is worth most). */
int Game_RowPoints(int row);
/* Mystery ship value: decided by how many shots the player has fired. */
int Game_UfoPoints(int shotsFired);
/* Pixel rect of invader (row, col) in field coordinates. */
void Game_InvaderRect(const Game *g, int row, int col, float *x, float *y);
float Game_BunkerX(int index);
/* Pristine bunker shape: chamfered top, arch cut out underneath. */
bool Game_BunkerShape(int row, int col);

#endif
