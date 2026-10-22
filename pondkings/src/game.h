#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdint.h>
#include "fixed.h"
#include "rink.h"
#include "rng.h"

/* Pond Kings' rules core: the pond, the town rink and the arena, three on
 * three, with goalies on the rinks. No raylib, no float, no clock but the
 * tick count, no randomness but g->rng.
 *
 * Three promises online play depends on, kept everywhere in here:
 *   1. The same setup and the same button presses give the same game, tick
 *      for tick, on any machine (fixed.h says why the maths is integer).
 *   2. Button presses are the only input. Everything else, every computer
 *      player and goalie included, is worked out inside Game_Step.
 *   3. The whole state is one plain struct: copying it saves the game and
 *      copying it back restores it, as rollback does many times a second.
 * Game_Hash fingerprints the state so both machines can check they still
 * agree. */

#define TICK_HZ 60
#define TEAM_SIZE 3
#define SKATERS (2 * TEAM_SIZE)
#define MAX_PLAYERS 4 /* humans in one game: four pads, keyboards, or two machines' worth */

/* --- one player's input for one tick --- */
enum { BTN_SHOOT = 1, BTN_PASS = 2, BTN_CHECK = 4, BTN_ALL = 7 };
typedef struct {
    uint8_t dir;     /* 0 none, 1..DIR_COUNT a stick direction (fixed.h) */
    uint8_t buttons; /* BTN_* held this tick; presses and releases are worked out inside */
} PadInput;

typedef enum { BUILD_QUICK, BUILD_BALANCED, BUILD_BIG, BUILD_COUNT } Build;
typedef enum { DIFF_EASY, DIFF_NORMAL, DIFF_HARD, DIFF_COUNT } Difficulty;
typedef enum { VENUE_POND, VENUE_RINK, VENUE_ARENA, VENUE_COUNT } Venue;
typedef enum { MODE_MATCH, MODE_SHOOTOUT } GameMode;
/* Signature moves: what a signed player brings. */
typedef enum { TRAIT_NONE, TRAIT_CANNON, TRAIT_SNIPER, TRAIT_DANGLER, TRAIT_PEST, TRAIT_PLAYMAKER, TRAIT_WALL, TRAIT_COUNT } Trait;
/* The daily pond's one twist. */
typedef enum { TWIST_NONE, TWIST_WIND, TWIST_BLIZZARD, TWIST_GLASS_ICE, TWIST_TINY_NETS, TWIST_COUNT } Twist;
typedef enum { GOALIE_SCRAMBLER, GOALIE_BUTTERFLY, GOALIE_POKER, GOALIE_STYLE_COUNT } GoalieStyle;

/* How a team's computer players lean: a rival crew's habits, or a grudge. */
typedef struct {
    int8_t shoot, pass, check; /* percent points added to taking a shot, a pass, a check */
    int8_t side;               /* -1 shoots for the upper post (screen), +1 the lower one, 0 either */
} AiLean;

typedef struct {
    uint64_t seed;
    int32_t periodSeconds;          /* 30..600 */
    int32_t periods;                /* 1..3; a tie after the last goes to sudden death */
    uint8_t venue, mode, twist;
    uint8_t teamLevel[2];           /* Difficulty of each team's computer skaters and goalie */
    int8_t slotTeam[MAX_PLAYERS];   /* each player's team, 0 or 1, or -1 when not playing */
    uint8_t build[2][TEAM_SIZE];
    uint8_t trait[2][TEAM_SIZE];
    uint8_t goalieStyle[2];
    AiLean lean[2];
} GameSetup;

/* The ice for a venue: the shape, the nets and what the edges and the rules do. */
typedef struct {
    fx hl, hw, r;                   /* half length, half width, corner radius */
    fx goalX, netHalfW, netDepth;
    uint8_t boards;                 /* boards bounce the puck; a snowbank swallows it */
    uint8_t goalies, slapshots, snow;
} Ice;

typedef enum { PH_FACEOFF, PH_PLAY, PH_GOAL, PH_STOPPAGE, PH_BREAK, PH_SO_READY, PH_SO_RESULT, PH_OVER } Phase;

/* One-tick flags for the renderer and the sounds (cleared every Game_Step). */
enum {
    EV_PASS = 1u << 0,
    EV_SHOT = 1u << 1,
    EV_ONE_TIMER = 1u << 2,
    EV_GOAL = 1u << 3,
    EV_POST = 1u << 4,
    EV_NET = 1u << 5,          /* the puck hit the outside of a net */
    EV_BANK = 1u << 6,         /* a hard puck buried itself in a snowbank */
    EV_BANK_SOFT = 1u << 7,    /* a slow puck bumped a snowbank */
    EV_DIG_OUT = 1u << 8,      /* a buried puck is back on the ice */
    EV_CHECK = 1u << 9,        /* a body check knocked the puck loose */
    EV_POKE = 1u << 10,        /* a poke check knocked it loose */
    EV_WHIFF = 1u << 11,       /* a check that missed */
    EV_PICKUP = 1u << 12,
    EV_STOP = 1u << 13,        /* a skater dug in for a hard stop: snow spray */
    EV_DROP = 1u << 14,        /* the faceoff puck dropped */
    EV_FALSE_START = 1u << 15, /* someone went for it before the drop */
    EV_FACEOFF_WON = 1u << 16,
    EV_PERIOD_END = 1u << 17,
    EV_GAME_OVER = 1u << 18,
    EV_SWITCH = 1u << 19,      /* a player took over another skater */
    EV_BREAK_DONE = 1u << 20,  /* the ice is clean and the next period starts */
    EV_BLOCK = 1u << 21,       /* a shot or pass hit an opponent's body and dropped loose */
    EV_BOARDS = 1u << 22,      /* the puck rattled off the boards */
    EV_SAVE = 1u << 23,        /* a goalie stopped a shot */
    EV_FREEZE = 1u << 24,      /* a goalie held on: whistle, faceoff */
    EV_WINDUP = 1u << 25,      /* a slapshot started winding up */
    EV_SLAPSHOT = 1u << 26,
    EV_SO_MISS = 1u << 27,     /* a shootout attempt ended without a goal */
};

/* Per skater, for the renderer: what happened to it this tick. */
enum { SKF_STOPPED = 1, SKF_HIT = 2, SKF_CHECKED = 4, SKF_SHOT = 8, SKF_PASSED = 16 };

typedef struct {
    V2 pos, vel;
    V2 face;            /* unit: where the stick points */
    uint8_t team, build, trait;
    uint8_t benched;    /* off the ice (the shootout): no physics, not drawn */
    int8_t player;      /* the human slot steering it, or -1 for the computer */
    uint8_t flags;      /* SKF_*, this tick only */
    int16_t stun;       /* ticks it can do nothing (knocked over by a check) */
    int16_t noPickup;   /* ticks before it may take a loose puck */
    int16_t actCool;    /* ticks before its next shot, pass or check */
    int16_t shootBuf;   /* Shoot pressed without the puck: a one-timer waiting */
    int16_t charge;     /* ticks a slapshot has been winding up, 0 if not */
    int16_t aiWait;     /* ticks before the computer decides its next action */
    int16_t aiCharge;   /* how long the computer means to wind up */
    uint16_t goals, assists, shots;
} Skater;

typedef struct {
    V2 pos, vel;
    uint8_t present, style;
    int16_t recover;    /* ticks still getting up after a save */
    int16_t hold;       /* ticks holding the puck before the whistle */
    int16_t lean;       /* shootout: how far it cheats toward the shooter's favourite side (world units) */
    uint16_t saves;
} Goalie;

typedef enum { FLIGHT_NONE, FLIGHT_PASS, FLIGHT_SHOT } Flight;

typedef struct {
    V2 pos, vel;
    int8_t carrier;     /* skater index, or -1 when loose */
    int8_t lastTouch;   /* who last had it (goal credit), -1 at a faceoff */
    int8_t prevTouch;   /* who had it before that, on the same team: the assist */
    int8_t passTo;      /* who a pass in flight is meant for, or -1 */
    uint8_t flight;     /* Flight: what the loose puck is doing */
    uint8_t oneTimer;   /* the shot in flight was a one-timer */
    int8_t heldBy;      /* the team whose goalie has it, or -1 */
    int16_t buried;     /* ticks left in a snowbank; 0 on the ice */
    int16_t age;        /* ticks since it left a stick */
} Puck;

/* What a player does, for the rival crews (their habits) and the results. */
typedef struct {
    uint16_t shots, passes, checks, carryTicks;
    uint16_t sideShots[3]; /* aimed at the upper post, the middle, the lower post */
    int32_t shotDistance;  /* summed, world units */
} Habits;

typedef struct {
    GameSetup setup;
    Ice ice;
    Skater sk[SKATERS];
    Goalie gk[2];           /* gk[t] guards team t's net */
    Puck puck;
    Rng rng;
    V2 wind;                /* the WIND twist: a steady push on the puck */
    uint32_t tick;
    uint8_t phase;          /* Phase */
    int32_t phaseTicks;     /* ticks spent in this phase */
    int32_t period;         /* 1-based; above setup.periods is sudden death */
    int32_t clock;          /* ticks left in the period (unused in sudden death) */
    int32_t score[2];
    int32_t oneTimerGoals[2];
    fx snow;                /* 0..FX_ONE: snow lying on the pond slows everything */
    fx snowPerTick;
    int32_t dropAt;         /* phaseTicks when the faceoff puck drops */
    int8_t centre[2];       /* each team's skater in the faceoff circle */
    int16_t falseStart[2];  /* ticks a centre who jumped the drop must wait */
    int16_t aiFaceoff[2];   /* ticks after the drop a computer centre reacts */
    int8_t slotSkater[MAX_PLAYERS]; /* which skater each player steers, -1 none */
    uint8_t prevButtons[MAX_PLAYERS];
    Habits habits[MAX_PLAYERS];
    /* shootout */
    int32_t soAttempts, soMisses, soStreak, soBestStreak;
    int8_t soLastSide;      /* where the last attempt went: -1, 0, +1 */
    uint32_t events;        /* EV_*, this tick only */
    int8_t lastGoalTeam, lastScorer, lastAssist; /* the latest goal, -1 before any */
    int8_t winner;          /* at PH_OVER: 0 or 1 (in a shootout, 0) */
} Game;

void Game_DefaultSetup(GameSetup *s, uint64_t seed);
void Game_Init(Game *g, const GameSetup *setup);
/* One tick. in[] has MAX_PLAYERS entries; players not in the game are ignored. */
void Game_Step(Game *g, const PadInput in[MAX_PLAYERS]);
/* A fingerprint of everything that decides what happens next. */
uint64_t Game_Hash(const Game *g);

/* Couch drop-in: a player joins a team mid-game (taking over one of its
 * computer skaters) or leaves (the computer takes theirs back). Returns
 * false if the team has no free skater, or the slot was not playing. */
bool Game_JoinPlayer(Game *g, int slot, int team);
bool Game_LeavePlayer(Game *g, int slot);

/* --- read-only helpers for the renderer, the sounds and the tests --- */
int Game_TeamOf(int skater);                         /* 0 for 0..2, 1 for 3..5 */
fx Game_GoalLineX(const Game *g, int defendingTeam); /* the x of the net a team defends */
V2 Game_StickPoint(const Skater *s);
bool Game_OnIce(const Game *g, V2 p, fx radius);     /* inside the ice with room for radius */
V2 Game_ClampToIce(const Game *g, V2 p, fx radius);  /* p, or the nearest point on the ice with that room */
void Game_IceFor(int venue, int twist, Ice *out);
const char *Game_BuildName(int build);               /* "Quick", "Balanced", "Big" */
const char *Game_TraitName(int trait);               /* "Cannon", ... ("" for none) */
const char *Game_TraitWords(int trait);              /* one line: what it does */
const char *Game_VenueName(int venue);               /* "The pond", ... */
const char *Game_TwistName(int twist);
int Game_ClockSeconds(const Game *g);                /* rounded up; 0 in sudden death */
bool Game_SuddenDeath(const Game *g);
bool Game_PuckDropped(const Game *g);                /* the faceoff puck has dropped */
V2 Game_GoalieHome(const Game *g, int team);         /* where a goalie stands when the puck is far */

#endif
