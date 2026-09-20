#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdint.h>
#include "rng.h"

/* All positions are in field pixels: (0,0) is the top-left of the playfield,
 * x grows right, y grows down. The renderer offsets the field onto the
 * window; nothing in here knows where that is. */
#define BRICK_COLS 13
#define BRICK_ROWS 18
#define BRICK_W 44
#define BRICK_H 20
#define FIELD_W (BRICK_COLS * BRICK_W) /* 572 */
#define FIELD_H 572
#define BRICK_TOP 40 /* y of brick row 0: leaves a lane above the bricks */

#define BALL_RADIUS 5.0f
#define MAX_BALLS 12
#define PADDLE_Y 536.0f /* top edge of the paddle */
#define PADDLE_H 12.0f
#define PADDLE_W_NORMAL 88.0f
#define PADDLE_W_WIDE 136.0f
#define PADDLE_SPEED 560.0f

/* Physics runs at a fixed 120 Hz whatever the frame rate, so behaviour and
 * tests don't depend on the display. At the speed cap a ball moves under
 * 5 px per step -- less than its own diameter and far less than a brick --
 * which is what rules out tunnelling without any swept tests. */
#define STEP_HZ 120
#define STEP_DT (1.0f / STEP_HZ)
#define BALL_SPEED_BASE 300.0f
#define BALL_SPEED_PER_LEVEL 14.0f
#define BALL_SPEED_PER_HIT 3.0f
#define BALL_SPEED_CAP 540.0f
#define SLOW_FACTOR 0.68f
/* After any bounce at least this share of the speed is vertical, so a ball
 * can never settle into an endless horizontal rally between two walls. */
#define MIN_VERTICAL_SHARE 0.28f
#define MAX_PADDLE_ANGLE_DEG 62.0f

#define START_LIVES 3
#define MAX_LIVES 6
#define MAX_POWERUPS 8
#define POWERUP_FALL_SPEED 130.0f
#define POWERUP_W 32.0f
#define POWERUP_H 14.0f
#define POWERUP_CHANCE_PCT 16
#define WIDE_SECONDS 16.0f
#define SLOW_SECONDS 10.0f
#define FIRE_SECONDS 7.0f
#define MAX_BROKEN_PER_STEP 24
#define LASER_SECONDS 9.0f
#define STICKY_SECONDS 14.0f
#define SHOT_SPEED 520.0f
#define SHOT_INTERVAL 0.28f
#define MAX_SHOTS 8
#define SHIELD_Y (FIELD_H - 8.0f)
#define COMBO_STEP 5   /* every this many bricks without touching the paddle adds one to the multiplier */
#define COMBO_MAX 5

typedef enum {
    BRICK_NONE = 0,
    BRICK_ONE,      /* '1' one hit */
    BRICK_TWO,      /* '2' two hits */
    BRICK_THREE,    /* '3' three hits */
    BRICK_STEEL,    /* '#' never breaks, doesn't count toward clearing */
    BRICK_BOMB      /* '*' one hit, takes its eight neighbours with it */
} BrickType;

typedef enum {
    PU_MULTI, /* every ball in play splits in three */
    PU_WIDE,  /* wider paddle for a while */
    PU_SLOW,  /* slower balls for a while */
    PU_FIRE,  /* balls burn straight through bricks for a while */
    PU_LIFE,  /* one more paddle */
    PU_LASER, /* the paddle shoots bricks while you hold the launch key */
    PU_STICKY,/* balls stick to the paddle until you launch them */
    PU_SHIELD,/* a floor under the field that saves one ball, once */
    PU_COUNT
} PowerupType;

typedef enum {
    GS_PLAYING,
    GS_PAUSED,
    GS_GAMEOVER
} GamePhase;

typedef struct {
    float x, y, vx, vy;
    bool active;
    bool stuck;        /* riding the paddle, waiting for launch */
    float stuckOffset; /* x offset from the paddle centre while stuck */
} Ball;

typedef struct {
    float x, y;
    PowerupType type;
    bool active;
} Powerup;

typedef struct {
    uint8_t col, row, type;
} BrokenBrick;

typedef struct {
    float x, y;
    bool active;
} Shot;

typedef struct {
    uint8_t brick[BRICK_ROWS][BRICK_COLS]; /* BrickType */
    uint8_t hp[BRICK_ROWS][BRICK_COLS];
    int bricksLeft; /* breakable bricks remaining */

    Ball balls[MAX_BALLS];
    Powerup powerups[MAX_POWERUPS];

    float paddleX; /* centre */
    float paddleW;
    float moveDir;     /* input for the coming steps: -1, 0, +1 */
    bool launchHeld;   /* input: launch any stuck ball */

    float wideTimer, slowTimer, fireTimer, laserTimer, stickyTimer, shotCooldown;
    Shot shots[MAX_SHOTS];
    bool shield;         /* the floor is up */
    int combo;           /* bricks broken since a ball last touched the paddle */
    bool comboEnabled;   /* score multiplier from combos; off by default so the classic scores stand */
    float speed; /* current un-slowed ball speed */

    GamePhase phase;
    int lives;
    int level; /* 1-based, keeps counting past the last layout */
    long score;
    long highScore;
    float stepAccumulator;

    /* One-frame events for sound/effects; cleared by Game_ConsumeFrameFlags. */
    bool justPaddleHit, justWallHit, justBrickHit, justExploded;
    bool justLaunched, justLifeLost, justLevelClear, justGameOver;
    bool justShot, justShieldHit, justCatch;
    int justPowerup; /* PowerupType collected this frame, or -1 */
    BrokenBrick broken[MAX_BROKEN_PER_STEP]; /* presentation hint: what just broke, and where */
    int brokenCount;

    Rng rng;
} Game;

/* Parses a level: up to BRICK_ROWS lines of up to BRICK_COLS characters,
 * '.' or ' ' empty, '1' '2' '3' hit counts, '#' steel, '*' bomb. Missing
 * rows/columns are empty. Returns false (and leaves out zeroed) if the text
 * holds an unknown character or no breakable brick at all. */
bool Level_Parse(const char *text, uint8_t out[BRICK_ROWS][BRICK_COLS]);

/* Built-in layouts (levels.c). */
int Levels_Count(void);
const char *Levels_Text(int index);

void Game_Init(Game *g, uint64_t seed, long highScore);
void Game_Restart(Game *g, uint64_t seed);
/* Switches on the combo multiplier (main does this for real play; the classic scoring tests leave it off). */
void Game_SetComboScoring(Game *g, bool on);
/* The multiplier the next brick will be worth, 1..COMBO_MAX. */
int Game_ComboMultiplier(const Game *g);
/* Loads layout (level-1) % Levels_Count() and parks a ball on the paddle. */
void Game_StartLevel(Game *g, int level);

/* Input for the steps that follow: moveDir in [-1, 1]; launch releases any
 * ball riding the paddle. */
void Game_SetInput(Game *g, float moveDir, bool launch);

/* Runs as many fixed steps as dt covers. Only acts while GS_PLAYING. */
void Game_Update(Game *g, float dt);
/* Exactly one 1/120 s step. Exposed so tests don't care about timing. */
void Game_Step(Game *g);

void Game_TogglePause(Game *g);
int Game_BallsInPlay(const Game *g);
/* Ball speed after the slow powerup is applied. */
float Game_EffectiveSpeed(const Game *g);

void Game_ConsumeFrameFlags(Game *g);

#endif
