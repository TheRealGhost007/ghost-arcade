#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdint.h>
#include "rng.h"

/* Lanehop: a hare crosses five lanes of traffic and five of river to reach
 * one of five burrows. The world is 13 columns by 13 rows of tiles. Rows,
 * top to bottom:
 *   0      home: five burrows in a hedge
 *   1-5    river: logs and turtles (you must ride them)
 *   6      the verge: safe
 *   7-11   road: anything touching you is fatal
 *   12     the start: safe
 * A lane is DATA: a direction, a speed and a pattern string, one character
 * per tile ('.' empty, anything else solid). The pattern repeats forever and
 * scrolls, so the tests can inspect every lane the way the game plays it.
 * The hare stands on continuous x (tiles), because riding a log leaves it
 * between tiles; hops are exactly one tile. */
#define LANE_COLS 13
#define LANE_ROWS 13
#define HOME_ROW 0
#define RIVER_FIRST 1
#define RIVER_LAST 5
#define MEDIAN_ROW 6
#define ROAD_FIRST 7
#define ROAD_LAST 11
#define START_ROW 12
#define START_X 6.0f

#define BAY_COUNT 5
#define BAY_TOLERANCE 0.55f /* how far off a burrow's centre you may land */

#define HOP_TIME 0.11f
#define LIFE_SECONDS 30.0f
#define DEATH_SECONDS 1.0f
#define HOME_PAUSE_SECONDS 0.4f
#define START_LIVES 3
#define MAX_LIVES 6
#define EXTRA_LIFE_EVERY 10000
#define HARE_HALF_WIDTH 0.30f /* hitbox: the hare is narrower than a tile */

#define GOODIE_SECONDS 8.0f
#define GOODIE_CARROT_POINTS 300
#define GOODIE_CLOCK_SECONDS 10.0f

#define STEP_HZ 120
#define STEP_DT (1.0f / STEP_HZ)

typedef enum { LANE_HOME, LANE_LOG, LANE_TURTLE, LANE_SAFE, LANE_ROAD } LaneKind;
typedef enum { DIR_UP, DIR_LEFT, DIR_DOWN, DIR_RIGHT, DIR_NONE } Dir;
typedef enum { GS_PLAYING, GS_PAUSED, GS_GAMEOVER } GamePhase;
typedef enum {
    DEATH_NONE, DEATH_CAR, DEATH_WATER, DEATH_OFFSCREEN, DEATH_TIME,
    DEATH_WALL, DEATH_BAY_FULL, DEATH_CROC
} DeathCause;

typedef enum { GOODIE_CARROT, GOODIE_CLOCK, GOODIE_SHIELD, GOODIE_COUNT } GoodieType;

typedef struct {
    LaneKind kind;
    int dir;              /* +1 scrolls right, -1 left */
    float speed;          /* tiles per second at level 1 */
    const char *pattern;  /* one char per tile; length = the period */
    bool dives;           /* turtle lanes only: the groups sink now and then */
} LaneDef;

typedef struct {
    float x;              /* left edge, in tiles; the centre is x + 0.5 */
    int row;
    Dir facing;
    bool alive;
    bool hopping;
    float hopT;
    float fromX, toX;
    int fromRow, toRow;
} Hare;

typedef struct {
    Hare hare;
    Dir queued;           /* one buffered hop, so quick taps chain */

    float laneOffset[LANE_ROWS]; /* how far each lane has scrolled, in [0, period) */
    float time;

    bool bays[BAY_COUNT];
    int flyBay;  float flyTimer, flyDelay;
    int crocBay; float crocTimer, crocDelay;
    /* Something good waits on the verge for a few seconds now and then. */
    bool goodieOn; int goodieCol; GoodieType goodieType; float goodieTimer, goodieDelay;
    bool shield;        /* the next fatal mishap (other than the clock) is undone */

    GamePhase phase;
    int lives;
    int level;
    long score, highScore;
    long nextExtraAt;
    float lifeTimer;
    int bestRow;          /* furthest row reached this life: forward progress scores once */
    float deathTimer;
    DeathCause deathCause;
    float homePause;
    float stepAccumulator;

    /* One-frame events for sound/effects; cleared by Game_ConsumeFrameFlags. */
    bool justHop, justDeath, justHome, justFly, justLevelClear, justGameOver, justExtraLife;
    bool justGoodie, justShieldSave;
    GoodieType lastGoodie;
    int justTimeWarning; /* seconds left, when it ticks under six; else 0 */
    int homeBay;         /* which burrow was just filled (valid with justHome) */
    int lastSecond;

    Rng rng;
} Game;

void Game_Init(Game *g, uint64_t seed, long highScore);
void Game_Restart(Game *g, uint64_t seed);

/* Asks for a hop. One is buffered while another is in flight, so quick taps
 * chain into a run without dropped inputs. */
void Game_Hop(Game *g, Dir d);
void Game_Update(Game *g, float dt);
void Game_Step(Game *g); /* exactly one 1/120 s step */
void Game_TogglePause(Game *g);
void Game_ConsumeFrameFlags(Game *g);
const char *Game_GoodieName(GoodieType t);

/* --- Lanes: the world as data, shared with the renderer and the tests --- */
const LaneDef *Game_Lane(int row);
float Game_SpeedMult(int level);
/* Signed tiles per second the lane moves at this level. */
float Game_LaneVelocity(const Game *g, int row);
/* Is the point x (tiles) inside a solid pattern cell of this lane right now? */
bool Game_LaneSolid(const Game *g, int row, float x);
/* Does any solid cell overlap [x0, x1]? */
bool Game_LaneOverlap(const Game *g, int row, float x0, float x1);
/* The solid run containing x, if any: fills its first pattern index and
 * length (in tiles) and returns true. */
bool Game_LaneRun(const Game *g, int row, float x, int *startIdx, int *len);
/* Turtles in a diving lane sink in a cycle, each group at its own phase.
 * Sinking is the warning (still safe); submerged is fatal to stand on. */
bool Game_TurtleSinking(const Game *g, int row, int runStartIdx);
bool Game_TurtleSubmerged(const Game *g, int row, int runStartIdx);
/* Centre column of burrow i. */
int Game_BayColumn(int i);
/* The hare's position in tiles, interpolated through a hop. */
void Game_HarePos(const Game *g, float *x, float *row);

#endif
