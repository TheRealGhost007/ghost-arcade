#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdint.h>
#include "rng.h"

/* Girderclimb rules core (no raylib). You climb a tower of girders with a
 * grapple hook: fire at a ring above you, reel in or let out the rope, pump
 * your swing with left and right, and let go at the right moment to fly to the
 * next one. Cursed fire rises from below and gets quicker; ghost orbs drift
 * across the shaft. Reach the top ledge to clear the tower.
 *
 * World: WORLD_W wide and as tall as the tower needs, y pointing down. Fixed
 * 120 Hz step. The rope is a hard distance constraint: when you would be
 * further from the ring than the rope is long, you are pulled back onto the
 * circle and lose the outward part of your velocity, which is exactly what
 * turns a fall into a swing. */

#define WORLD_W 560
#define STEP_HZ 120
#define STEP_DT (1.0f / STEP_HZ)

#define PLAYER_R 9.0f
#define GRAVITY 900.0f
#define WALK_SPEED 110.0f
#define JUMP_SPEED 330.0f
#define PUMP_ACCEL 300.0f
#define REEL_SPEED 150.0f
#define ROPE_MIN 30.0f
#define ROPE_MAX 240.0f
#define GRAPPLE_RANGE 250.0f
#define ANCHOR_REACH 36.0f      /* close enough to a ring to count it */
#define WALL_BOUNCE 0.35f
#define AIR_DRAG 0.15f          /* per second, on horizontal speed */

#define MAX_ANCHORS 80
#define MAX_LEDGES 12
#define MAX_ORBS 16

#define START_LIVES 3
#define MAX_LIVES 6
#define EXTRA_LIFE_EVERY 8000
#define RING_POINTS 50
#define CHECKPOINT_POINTS 200
#define ORB_RADIUS 12.0f

#define INTRO_SECONDS 1.6f
#define DYING_SECONDS 1.3f
#define CLEAR_SECONDS 2.2f

typedef enum { GS_PLAYING, GS_PAUSED, GS_GAMEOVER } GamePhase;
typedef enum { LS_INTRO, LS_PLAY, LS_DYING, LS_CLEAR } LevelState;
typedef enum { P_STAND, P_AIR, P_ROPE } PlayerMode;
typedef enum { DEATH_NONE, DEATH_FIRE, DEATH_ORB } DeathCause;

typedef struct { float x, y; bool touched; } Anchor;
typedef struct { float x, y, w; bool checkpoint, goal; bool reached; } Ledge; /* y = the top surface; x = left edge */
typedef struct { float cx, cy, amp, freq, phase; float x; } Orb;

typedef struct {
    float x, y, vx, vy;
    PlayerMode mode;
    int anchor;          /* while on the rope */
    int lastAnchor;      /* the ring you just left; not targeted again at once */
    float ropeLen;
    int facing;
} Player;

typedef struct {
    GamePhase phase;
    LevelState state;
    float stateTimer;
    Rng rng;
    uint64_t seed;

    int level;              /* 1-based; towers get taller and the fire quicker */
    float towerTop;         /* y of the goal ledge's surface (small = high) */
    float groundY;          /* y of the floor's surface */
    Anchor anchors[MAX_ANCHORS];
    int anchorCount;
    Ledge ledges[MAX_LEDGES];
    int ledgeCount;
    Orb orbs[MAX_ORBS];
    int orbCount;
    bool orbsEnabled;

    Player p;
    float fireY;            /* the surface of the rising fire */
    float fireSpeed;
    float time;             /* seconds of this attempt */
    int checkpoint;         /* index of the ledge you respawn on */
    float bestY;            /* the highest you have been this tower (small = high) */
    float climbAcc;

    int moveX, moveY;       /* held direction: pumping and reeling */
    bool grappleQueued, jumpQueued;
    bool grappleHeld, jumpHeld;

    long score, highScore, nextExtra;
    int lives;
    DeathCause death;
    long lastBonus;
    int heightMeters;       /* how high you are, for the HUD */

    float stepAccumulator;
    long stepCount;

    bool justFire, justAttach, justRelease, justJump, justLand, justRing, justCheckpoint;
    bool justDeath, justClear, justGameOver, justNewLevel, justExtraLife, justWall;
} Game;

void Game_Init(Game *g, uint64_t seed, long highScore);
void Game_Restart(Game *g, uint64_t seed, long highScore);
void Game_StartLevel(Game *g, int level);
/* held direction, and grapple / jump buttons (edges are detected inside) */
void Game_SetInput(Game *g, int moveX, int moveY, bool grapple, bool jump);
void Game_Step(Game *g);
void Game_Update(Game *g, float dt);
void Game_TogglePause(Game *g);
void Game_ConsumeFrameFlags(Game *g);

/* Builds the tower for `level` from the seed (without touching lives or score). */
void Game_BuildTower(Game *g, int level);
/* The ring the grapple would take from (x, y) with this input, or -1. */
int Game_PickAnchor(const Game *g, float x, float y, int moveX);
float Game_TowerHeight(int level);
float Game_FireSpeed(int level);
float Game_OrbX(const Orb *o, float time);

#endif
