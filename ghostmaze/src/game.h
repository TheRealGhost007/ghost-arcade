#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdint.h>

/* Ghostmaze rules core (no raylib). You are a ghost in a haunted house, and a
 * priest with a bell walks the halls. As a ghost you are quick but see-through
 * to nothing: the priest's sight destroys you. So you POSSESS things -- a cat,
 * a suit of armor, a servant -- each of which can do what a ghost cannot, and
 * slip from one to the next to reach the exit mirror, which only a ghost can use.
 *
 * The house moves on a TICK (the game runs one every TICK_SECONDS; the tests
 * and solver call Game_Tick directly). Each tick you may move one tile or
 * possess/release; then the priests take a step. There is no randomness, so a
 * string of actions always plays out the same way, which is what lets the
 * solver prove every level can be finished. */

#define MAP_W 16
#define MAP_H 12
#define MAX_HOSTS 4
#define MAX_CRATES 4
#define MAX_PRIESTS 3
#define MAX_KEYS 3
#define SIGHT_RANGE 4
#define START_LIVES 3
#define TICK_SECONDS 0.34f
#define INTRO_SECONDS 1.6f
#define DYING_SECONDS 1.2f
#define CLEAR_SECONDS 2.0f
#define SCORE_CLEAR 500
#define SCORE_PAR_BONUS 250
#define SCORE_PER_SPARE_TICK 10

typedef enum {
    T_FLOOR = 0, T_WALL, T_GRATE, T_FLAP, T_DOOR, T_CRACK, T_PLATE, T_GATE, T_EXIT,
} Tile;

typedef enum { HOST_CAT, HOST_ARMOR, HOST_SERVANT } HostType;
typedef enum { ACT_WAIT, ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_POSSESS } Action;
typedef enum { GS_PLAYING, GS_PAUSED, GS_GAMEOVER } GamePhase;
typedef enum { ST_INTRO, ST_PLAY, ST_DYING, ST_CLEAR } LevelState;
typedef enum { CAUGHT_NONE, CAUGHT_SEEN, CAUGHT_TOUCHED, CAUGHT_EXORCISED } CaughtBy;

typedef struct { int x, y; HostType type; bool present; bool hasKey; } Host;
typedef struct { int x, y; bool present; } Crate;
typedef struct { int x, y; bool taken; } Key;
typedef struct {
    int x, y, dir;          /* dir: 0 up 1 right 2 down 3 left */
    int ax, ay, bx, by;     /* patrols between A and B */
    int toward;             /* 0 heading to B, 1 heading to A */
    int wait;               /* ticks left standing at an end */
    int stride;             /* moves once every `stride` ticks */
} Priest;

typedef struct {
    const char *name;
    const char *rows[MAP_H];
    int priestCount;
    int patrol[MAX_PRIESTS][5]; /* ax, ay, bx, by, stride */
    const char *hint;
    int par;                    /* the solver's shortest solution, in ticks (checked by the tests) */
} LevelDef;

extern LevelDef kLevels[]; /* not const: the tests swap rooms in */
extern const int kLevelCount;

typedef struct {
    GamePhase phase;
    LevelState state;
    float stateTimer;
    float tickAccumulator;

    int level;              /* 1-based index into kLevels, cycling */
    uint8_t tile[MAP_H][MAP_W];
    int gx, gy;             /* the ghost; while possessing, it sits on its host */
    int possessed;          /* host index, or -1 */
    int possessCooldown;    /* ticks before you may possess again after releasing */
    Host hosts[MAX_HOSTS];
    int hostCount;
    Crate crates[MAX_CRATES];
    int crateCount;
    Key keys[MAX_KEYS];
    int keyCount;
    Priest priests[MAX_PRIESTS];
    int priestCount;

    int ticks;              /* ticks used this attempt */
    int par;                /* the solver's shortest solution, in ticks */
    long score, highScore, nextExtra;
    int lives;
    CaughtBy caughtBy;
    long lastBonus;

    Action queued;          /* the next action, latched between ticks */

    /* One-frame events */
    bool justPossess, justRelease, justStep, justPush, justSmash, justKey, justDoor, justPlate, justGate;
    bool justCaught, justClear, justGameOver, justNewLevel, justBell, justExtraLife, justBlocked;
} Game;

void Game_Init(Game *g, long highScore);
void Game_Restart(Game *g, long highScore);
void Game_StartLevel(Game *g, int level, bool intro);
/* Latches an action for the next tick (the last one wins). */
void Game_Queue(Game *g, Action a);
void Game_Update(Game *g, float dt);
void Game_TogglePause(Game *g);
void Game_ConsumeFrameFlags(Game *g);

/* One tick with an action, regardless of timing: what the tests and the solver drive. */
void Game_Tick(Game *g, Action a);
void Game_LoadLevel(Game *g, int level);
bool Game_IsCleared(const Game *g);
bool Game_IsCaught(const Game *g);

/* Geometry the renderer and the rules share. */
bool Game_GateOpen(const Game *g);
bool Game_PlateHeld(const Game *g, int x, int y);
int Game_HostAt(const Game *g, int x, int y);
int Game_CrateAt(const Game *g, int x, int y);
/* Does any priest see (x, y) right now? Fills *by with which one. */
bool Game_SeenBy(const Game *g, int x, int y, int *by);
/* Would this mover be allowed onto that tile (ignoring other things)? */
bool Game_TileWalkable(const Game *g, int who, int x, int y); /* who: -1 ghost, else host index */

/* The solver. Returns the length of the shortest solution (in ticks) and,
 * if out is not NULL, writes it as a string of . U D L R P (NUL-terminated,
 * capacity outCap), or returns -1 if the level cannot be finished within maxTicks. */
int Solver_Solve(int level, int maxTicks, char *out, int outCap);

#endif
