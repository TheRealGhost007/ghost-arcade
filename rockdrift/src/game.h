#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdint.h>
#include "rng.h"

/* Positions are in field pixels, (0,0) top-left, y down. The field WRAPS:
 * leave one edge and you come in the opposite one -- for the ship, the
 * rocks and every bullet. Angles are radians measured from straight UP,
 * increasing clockwise, so heading 0 is "north" and (sin a, -cos a) is the
 * unit vector it points along. */
#define FIELD_W 768
#define FIELD_H 480

#define ROCK_VERTS 11
#define MAX_ROCKS 48       /* worst case is 4 pieces per starting rock, 11 rocks: 44 */
#define ROCK_START_MAX 11
#define MAX_BULLETS 4
#define MAX_ENEMY_BULLETS 3
#define MAX_KILLS_PER_FRAME 12

#define SHIP_RADIUS 9.0f
#define SHIP_ROT_SPEED 4.4f
#define SHIP_THRUST 280.0f
#define SHIP_MAX_SPEED 380.0f
#define BULLET_SPEED 540.0f
#define BULLET_LIFE 0.95f
#define FIRE_COOLDOWN 0.16f
#define ENEMY_BULLET_SPEED 300.0f
#define ENEMY_BULLET_LIFE 1.7f

#define START_LIVES 3
#define MAX_LIVES 6
#define EXTRA_LIFE_EVERY 10000
#define RESPAWN_SECONDS 2.0f
#define INVULN_SECONDS 2.0f
#define RESPAWN_CLEAR_RADIUS 110.0f
#define WAVE_DELAY 2.0f
#define SPAWN_CLEAR_RADIUS 170.0f /* new rocks appear at least this far from the ship */

#define STEP_HZ 120
#define STEP_DT (1.0f / STEP_HZ)

typedef enum { ROCK_SMALL = 1, ROCK_MEDIUM = 2, ROCK_LARGE = 3 } RockSize;
typedef enum { GS_PLAYING, GS_PAUSED, GS_GAMEOVER } GamePhase;

typedef enum {
    KILL_ROCK_SMALL, KILL_ROCK_MEDIUM, KILL_ROCK_LARGE,
    KILL_SAUCER_LARGE, KILL_SAUCER_SMALL, KILL_SHIP
} KillKind;

typedef struct {
    float x, y, vx, vy;
    float angle, spin;
    int size;         /* RockSize */
    uint32_t shape;   /* seed for the jagged outline; see Game_RockShape */
    bool active;
} Rock;

typedef struct {
    float x, y, vx, vy, life;
    bool active;
} Bullet;

typedef struct {
    float x, y, vx, vy;
    bool active, small;
    float turnTimer; /* until it next changes its vertical drift */
    float fireTimer;
} Saucer;

typedef struct {
    float x, y, vx, vy, angle;
    bool alive;
    bool thrusting;
    float respawnTimer; /* > 0 while dead, counting down to a respawn attempt */
    float invuln;
    float fireCooldown;
} Ship;

typedef struct {
    float x, y;
    int kind; /* KillKind */
    int points;
} Kill;

typedef struct {
    Ship ship;
    Rock rocks[MAX_ROCKS];
    int rockCount; /* active rocks */
    Bullet bullets[MAX_BULLETS];
    Bullet enemyBullets[MAX_ENEMY_BULLETS];
    Saucer saucer;
    float saucerTimer;

    float turn; /* input: -1 left .. +1 right */
    bool thrustHeld, fireHeld, hyperHeld, hyperPrev;

    GamePhase phase;
    int lives; /* ships left INCLUDING the one in play */
    int wave;
    long score, highScore;
    long nextExtraAt;
    float waveTimer; /* > 0: field cleared, next wave on the way */
    float beatTimer;
    int beatNote;    /* 0/1: the two-note heartbeat */
    float stepAccumulator;

    /* One-frame events for sound/effects; cleared by Game_ConsumeFrameFlags. */
    bool justFired, justBeat, justHyperspace, justHyperDeath, justShipDeath;
    bool justSaucerAppeared, justSaucerFired, justWaveClear, justExtraLife, justGameOver;
    Kill kills[MAX_KILLS_PER_FRAME];
    int killCount;

    Rng rng;
} Game;

void Game_Init(Game *g, uint64_t seed, long highScore);
void Game_Restart(Game *g, uint64_t seed);
void Game_StartWave(Game *g, int wave);

/* turn in [-1,1]; the others are "held" states. Hyperspace fires on the
 * rising edge, so holding it doesn't teleport every step. */
void Game_SetInput(Game *g, float turn, bool thrust, bool fire, bool hyperspace);
void Game_Update(Game *g, float dt);
void Game_Step(Game *g); /* exactly one 1/120 s step */
void Game_TogglePause(Game *g);
void Game_ConsumeFrameFlags(Game *g);

/* --- Pure helpers, shared with the renderer and the tests --- */

/* Signed shortest difference b - a on a wrapped axis of length `size`,
 * in [-size/2, size/2]. */
float Game_WrapDelta(float a, float b, float size);
float Game_WrapDist(float x1, float y1, float x2, float y2);
/* Jagged outline for a rock: ROCK_VERTS radii as multiples of its nominal
 * radius, deterministic in `shape`. */
void Game_RockShape(uint32_t shape, float out[ROCK_VERTS]);
float Game_RockRadius(int size);
int Game_RockPoints(int size);
/* Seconds between heartbeat notes: fewer rocks left, faster beat. */
float Game_BeatInterval(int rocksLeft);
/* Half-width (radians) of the small saucer's aiming error; shrinks as the
 * score climbs. */
float Game_SaucerAimError(long score);

#endif
