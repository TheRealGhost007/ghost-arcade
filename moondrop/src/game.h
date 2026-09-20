#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdint.h>
#include "rng.h"

/* Moondrop rules core. No raylib: everything here runs headless in the tests.
 *
 * The world is FIELD_W x FIELD_H units with y pointing down; the moon is a
 * polyline of TERR_N points TERR_STEP apart that wraps (the first and last
 * heights are equal). Angle 0 is nose-up, positive is clockwise. Physics
 * runs on a fixed 120 Hz step, and constant acceleration is integrated
 * exactly (x += v*dt + a*dt^2/2), so a free fall matches the closed form. */

#define FIELD_W 768
#define FIELD_H 480
#define TERR_STEP 12
#define TERR_N 65            /* points; the last one is a copy of the first */
#define MAX_PADS 4

#define GRAVITY 22.0f        /* units/s^2, down */
#define THRUST 58.0f         /* units/s^2 along the nose */
#define ROT_RATE 2.3f        /* rad/s */
#define FUEL_START 1000.0f
#define FUEL_MAX 1500.0f
#define FUEL_BURN 32.0f      /* per second of thrust */
#define FUEL_LOW 200.0f
#define LAND_REFUEL 150.0f
#define CRASH_FUEL_PENALTY 200.0f
#define MAX_LAND_VY 28.0f    /* downward speed limit at touchdown */
#define MAX_LAND_VX 16.0f
#define MAX_LAND_TILT 0.1047f /* 6 degrees */
#define FOOT_X 8.0f          /* the two feet sit at +-FOOT_X, FOOT_Y below the centre */
#define FOOT_Y 10.0f
#define LAND_HOLD 2.4f       /* seconds the landing card stays up */
#define CRASH_HOLD 1.9f
#define MAX_PODS 3
#define POD_RADIUS 16.0f
#define POD_FUEL 130.0f
#define POD_POINTS 50
#define WIND_FIRST_LEVEL 3   /* the moon starts to have weather here */
#define WIND_MAX 6.0f        /* units/s^2 at its strongest: about what a legal 6-degree landing tilt can cancel */

#define STEP_HZ 120
#define STEP_DT (1.0f / STEP_HZ)

typedef enum { GS_PLAYING, GS_PAUSED, GS_GAMEOVER } GamePhase;
typedef enum { SHIP_FLYING, SHIP_LANDED, SHIP_CRASHED } ShipState;

typedef enum {
    LAND_OK,
    LAND_TOO_FAST_DOWN,
    LAND_TOO_FAST_SIDEWAYS,
    LAND_TILTED,
    LAND_OFF_PAD,
    LAND_HIT_BODY,   /* the hull, not a foot, met the ground */
} LandResult;

typedef struct {
    int start;  /* first terrain point of the flat */
    int points; /* how many points it spans; length is (points-1)*TERR_STEP */
    int mult;   /* score multiplier: 1, 2, 3 or 5 */
} Pad;

typedef struct { float x, y; bool taken; } Pod;

typedef struct {
    GamePhase phase;
    ShipState state;
    float stateTimer;

    float terrain[TERR_N];   /* y of each point */
    Pad pads[MAX_PADS];
    int padCount;

    Pod pods[MAX_PODS];      /* floating fuel pods */
    int podCount;
    float windBase, windPhase, flightTime; /* gusts: see Game_Wind */
    float x, y, vx, vy, angle;
    float fuel;
    float rotInput;          /* -1 left, +1 right */
    bool thrustHeld;
    bool thrusting;          /* true if the engine actually fired this step */

    int level;
    long score, highScore;
    long lastAward;          /* what the last landing scored */
    int lastPad;             /* index of the pad it landed on */
    LandResult lastResult;   /* how the last touchdown went */
    float touchVx, touchVy, touchAngle; /* what it hit with, for the message */

    float stepAccumulator;
    uint64_t baseSeed;
    Rng rng;

    /* One-frame events, cleared by Game_ConsumeFrameFlags. */
    bool justLand, justCrash, justLowFuel, justNewLevel, justGameOver, justRespawn, justPod;
} Game;

/* Live-tuning hook: point these at knobs (the F2 panel); NULL leaves the constant. */
void Game_SetTuning(const float *gravity, const float *thrust, const float *fuelBurn);

void Game_Init(Game *g, uint64_t seed, long highScore);
void Game_Restart(Game *g, uint64_t seed);
void Game_SetInput(Game *g, float rot, bool thrust);
void Game_Step(Game *g);
void Game_Update(Game *g, float dt);
void Game_TogglePause(Game *g);
void Game_ConsumeFrameFlags(Game *g);

/* Builds level `level`'s moon from `seed` (pads first, then midpoint
 * displacement between them) and puts the ship at its start. Exposed for
 * the tests and the renderer's menu. */
void Game_BuildLevel(Game *g, int level, uint64_t seed);
void Game_ResetShip(Game *g);

/* Sideways acceleration from the weather right now (0 on the calm early moons). */
float Game_Wind(const Game *g);

/* Terrain height at any x (wraps), by linear interpolation. */
float Game_TerrainY(const Game *g, float x);
/* Distance from the lowest foot to the ground below it. */
float Game_Altitude(const Game *g);
/* World position of one of the ship's contact points, local (lx, ly). */
void Game_ShipPoint(const Game *g, float lx, float ly, float *wx, float *wy);
/* The pad whose flat contains both x positions, or -1. */
int Game_PadUnder(const Game *g, float xa, float xb);
float Game_PadX0(const Pad *p);
float Game_PadX1(const Pad *p);
/* The classifier: would this be a clean landing? On success *padOut is the pad. */
LandResult Game_Classify(const Game *g, float vx, float vy, float angle, float footLx, float footRx, int *padOut);
/* Points for a landing on a pad with `mult`, with `fuel` left. */
long Game_LandingScore(int mult, float fuel, float vy);

#endif
