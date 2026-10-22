#include "game.h"
#include "ai.h"
#include <string.h>

/* ------------------------------------------------------------------ tuning */

typedef struct {
    fx maxSpeed;   /* units per tick */
    fx accel;
    fx shotSpeed;
    int checkPower; /* percent: how often a body check knocks the puck loose */
    int scatter;    /* world units of spread on a wrist shot */
} BuildStats;

static const BuildStats kBuild[BUILD_COUNT] = {
    /* quick    */ {FX_RATIO(43, 10), FX_RATIO(30, 100), FX_RATIO(96, 10), 40, 7},
    /* balanced */ {FX_RATIO(39, 10), FX_RATIO(25, 100), FX_RATIO(104, 10), 55, 6},
    /* big      */ {FX_RATIO(35, 10), FX_RATIO(20, 100), FX_RATIO(114, 10), 75, 9},
};

#define ICE_FRICTION FX_RATIO(12, 1000)  /* a gliding skater loses 1.2% a tick */
#define SNOW_FRICTION FX_RATIO(18, 1000) /* and up to 1.8% more on a snowy pond */
#define SKATE_DRAG FX_RATIO(3, 1000)     /* a little even while skating */
#define CARRY_SPEED FX_RATIO(92, 100)
#define WINDUP_SPEED FX_RATIO(45, 100)   /* winding up a slapshot, you barely move */
#define STOP_SPEED FX_RATIO(16, 10)      /* fast enough that reversing means digging in */
#define STOP_ALONG FX_RATIO(-45, 100)    /* stick pushed this far against the motion */
#define STOP_BRAKE FX_RATIO(18, 100)
#define TURN_RATE FX_RATIO(35, 100)
#define GLASS_ICE FX_RATIO(55, 100)      /* the GLASS_ICE twist: everything glides further */

#define PUCK_FRICTION FX_RATIO(6, 1000)
#define PUCK_SNOW_FRICTION FX_RATIO(10, 1000)
#define PASS_SPEED FX_RATIO(76, 10)
#define ONE_TIMER_BOOST FX_RATIO(122, 100)
#define SLAP_ONE_TIMER_BOOST FX_RATIO(135, 100) /* on a rink, a one-timer is a slapshot off the pass */
#define CHARGE_MIN 9                     /* a shorter wind-up is just a wrist shot */
#define CHARGE_FULL 40
#define CHARGE_MAX 48
#define BURY_SPEED FX_INT(5)             /* into a snowbank faster than this, and it's buried */
#define BURY_TICKS 70
#define BANK_BOUNCE FX_RATIO(25, 100)
#define BOARD_BOUNCE FX_RATIO(72, 100)
#define NET_BOUNCE FX_RATIO(30, 100)
#define POST_BOUNCE FX_RATIO(75, 100)
#define CHECK_RANGE FX_INT(26)           /* carrier's body to the checker's blade */
#define BODY_CHECK_SPEED FX_RATIO(22, 10)
#define WIND_PUSH FX_RATIO(12, 1000)

#define REBOUND_SPEED FX_INT(4)          /* slower than this at a goalie, and it's held */
#define GOALIE_RECOVER 14
#define SO_TIMEOUT 420                   /* a shootout attempt: seven seconds */

#define SHOT_COOL 20
#define PASS_COOL 12
#define CHECK_COOL 24
#define WHIFF_COOL 36
#define RELEASE_NO_PICKUP 14
#define LOOSE_NO_PICKUP 30
#define STUN_TICKS 40
#define ONE_TIMER_WINDOW 14

#define GOAL_TICKS 150
#define ARENA_GOAL_TICKS 330 /* long enough for the big screen's replay */
#define STOPPAGE_TICKS 50
#define BREAK_TICKS 240
#define SO_READY_TICKS 50
#define SO_RESULT_TICKS 70
#define DROP_MIN 50
#define DROP_RAND 46
#define FALSE_START_LOCK 24
#define FACEOFF_GIVE_UP 100

/* ------------------------------------------------------------- the ices */

void Game_IceFor(int venue, int twist, Ice *o) {
    memset(o, 0, sizeof(*o));
    if (venue == VENUE_POND) {
        o->hl = FX_INT(420); o->hw = FX_INT(210); o->r = FX_INT(110);
        o->goalX = FX_INT(350); o->netHalfW = FX_INT(30); o->netDepth = FX_INT(22);
        o->snow = 1;
        if (twist == TWIST_TINY_NETS) o->netHalfW = FX_INT(22);
    } else {
        o->hl = FX_INT(420); o->hw = FX_INT(200); o->r = FX_INT(70);
        o->goalX = FX_INT(340); o->netHalfW = FX_INT(40); o->netDepth = FX_INT(24);
        o->boards = 1;
        o->goalies = 1;
        o->slapshots = 1;
    }
}

/* ----------------------------------------------------------------- helpers */

int Game_TeamOf(int skater) { return skater < TEAM_SIZE ? 0 : 1; }
fx Game_GoalLineX(const Game *g, int defendingTeam) { return defendingTeam == 0 ? -g->ice.goalX : g->ice.goalX; }
static int AttackSign(int team) { return team == 0 ? 1 : -1; }
V2 Game_StickPoint(const Skater *s) { return V2_Add(s->pos, V2_Scale(s->face, STICK_REACH)); }

static V2 InnerPoint(const Game *g, V2 p) {
    fx hx = g->ice.hl - g->ice.r, hy = g->ice.hw - g->ice.r;
    return V2_Make(Fx_Clamp(p.x, -hx, hx), Fx_Clamp(p.y, -hy, hy));
}

bool Game_OnIce(const Game *g, V2 p, fx radius) {
    fx room = g->ice.r - radius;
    return V2_Len2(V2_Sub(p, InnerPoint(g, p))) <= (int64_t)room * room;
}

V2 Game_ClampToIce(const Game *g, V2 p, fx radius) {
    if (Game_OnIce(g, p, radius)) return p;
    V2 c = InnerPoint(g, p);
    V2 n = V2_Norm(V2_Sub(p, c));
    if (V2_IsZero(n)) n = V2_Make(0, FX_ONE);
    return V2_Add(c, V2_Scale(n, g->ice.r - radius));
}

const char *Game_BuildName(int build) {
    static const char *const kNames[BUILD_COUNT] = {"Quick", "Balanced", "Big"};
    return (build >= 0 && build < BUILD_COUNT) ? kNames[build] : "?";
}
const char *Game_TraitName(int t) {
    static const char *const kNames[TRAIT_COUNT] = {"", "Cannon", "Sniper", "Dangler", "Pest", "Playmaker", "Wall"};
    return (t >= 0 && t < TRAIT_COUNT) ? kNames[t] : "";
}
const char *Game_TraitWords(int t) {
    static const char *const kWords[TRAIT_COUNT] = {
        "", "The hardest shot on the ice", "Puts it where they aim", "Carries the puck at full speed",
        "A poke check that never stops", "Passes that find you", "Bounces checks off, blocks everything",
    };
    return (t >= 0 && t < TRAIT_COUNT) ? kWords[t] : "";
}
const char *Game_VenueName(int v) {
    static const char *const kNames[VENUE_COUNT] = {"The pond", "The town rink", "The arena"};
    return (v >= 0 && v < VENUE_COUNT) ? kNames[v] : "?";
}
const char *Game_TwistName(int t) {
    static const char *const kNames[TWIST_COUNT] = {"", "Wind", "Blizzard", "Glass ice", "Tiny nets"};
    return (t >= 0 && t < TWIST_COUNT) ? kNames[t] : "";
}

int Game_ClockSeconds(const Game *g) {
    if (Game_SuddenDeath(g) || g->setup.mode == MODE_SHOOTOUT) return 0;
    return (g->clock + TICK_HZ - 1) / TICK_HZ;
}
bool Game_SuddenDeath(const Game *g) { return g->setup.mode == MODE_MATCH && g->period > g->setup.periods; }
bool Game_PuckDropped(const Game *g) { return g->phase != PH_FACEOFF || g->phaseTicks >= g->dropAt; }

static fx Friction(const Game *g, fx base, fx snowExtra) {
    fx f = base + Fx_Mul(g->snow, snowExtra);
    if (g->setup.twist == TWIST_GLASS_ICE) f = Fx_Mul(f, GLASS_ICE);
    return f;
}

static int HumansOnTeam(const Game *g, int team) {
    int n = 0;
    for (int s = 0; s < MAX_PLAYERS; s++) if (g->slotSkater[s] >= 0 && Game_TeamOf(g->slotSkater[s]) == team) n++;
    return n;
}

static void GiveControl(Game *g, int slot, int skater) {
    int old = g->slotSkater[slot];
    if (old == skater) return;
    if (old >= 0) g->sk[old].player = -1;
    g->sk[skater].player = (int8_t)slot;
    g->slotSkater[slot] = (int8_t)skater;
    g->events |= EV_SWITCH;
}

/* --------------------------------------------------------------- goalies */

typedef struct { fx reach, depth, speedBonus; } GoalieStats;
static const GoalieStats kGoalie[GOALIE_STYLE_COUNT] = {
    /* scrambler */ {FX_INT(10), FX_INT(20), FX_RATIO(4, 10)},
    /* butterfly */ {FX_INT(12), FX_INT(11), FX_RATIO(-2, 10)},
    /* poker     */ {FX_INT(10), FX_INT(15), 0},
};
static const fx kGoalieSpeed[DIFF_COUNT] = {FX_RATIO(15, 10), FX_RATIO(20, 10), FX_RATIO(25, 10)};
static const int kGoalieReact[DIFF_COUNT] = {10, 7, 5}; /* ticks after a shot leaves the stick */

static const GoalieStats *GStats(const Goalie *k) { return &kGoalie[k->style < GOALIE_STYLE_COUNT ? k->style : 0]; }

V2 Game_GoalieHome(const Game *g, int team) {
    int inward = team == 0 ? 1 : -1;
    return V2_Make(Game_GoalLineX(g, team) + GStats(&g->gk[team])->depth * inward, 0);
}

static fx GoalieSpeed(const Game *g, int team) {
    const Goalie *k = &g->gk[team];
    fx s = kGoalieSpeed[g->setup.teamLevel[team] < DIFF_COUNT ? g->setup.teamLevel[team] : DIFF_NORMAL] + GStats(k)->speedBonus;
    if (g->setup.mode == MODE_SHOOTOUT) s += Fx_Min(FX_RATIO(5, 10), FX_RATIO(1, 10) * (g->score[0] / 3)); /* sharper as you score */
    if (k->recover > 0) s = Fx_Mul(s, FX_RATIO(4, 10));
    return s;
}

/* Where the puck is (on a blade if carried). */
static V2 PuckAt(const Game *g) { return g->puck.pos; }

static void MoveGoalies(Game *g) {
    for (int t = 0; t < 2; t++) {
        Goalie *k = &g->gk[t];
        if (!k->present) continue;
        if (k->recover > 0) k->recover--;
        if (k->hold > 0) continue;
        fx netX = Game_GoalLineX(g, t);
        int inward = t == 0 ? 1 : -1;
        fx depth = GStats(k)->depth;
        V2 p = PuckAt(g);
        const Puck *pk = &g->puck;
        bool incoming = pk->flight == FLIGHT_SHOT && Fx_Mul(pk->vel.x, FX_INT(-inward)) > 0;
        if (incoming) {
            /* a shot is coming: a moment to react, then a push toward where it
             * will cross, not as quick as following the play */
            if (pk->age < kGoalieReact[g->setup.teamLevel[t] < DIFF_COUNT ? g->setup.teamLevel[t] : DIFF_NORMAL]) continue;
            fx gx = netX + depth * inward;
            fx across = pk->vel.x != 0 ? Fx_Mul(pk->vel.y, Fx_Div(gx - p.x, pk->vel.x)) : 0;
            fx inside = g->ice.netHalfW - FX_INT(4);
            fx ty = Fx_Clamp(p.y + across, -inside, inside);
            V2 d = V2_Sub(V2_Make(gx, ty), k->pos);
            k->vel = V2_ClampLen(d, Fx_Mul(GoalieSpeed(g, t), FX_RATIO(7, 10)));
            k->pos = V2_Add(k->pos, k->vel);
            continue;
        }
        fx out = Fx_Mul(p.x - netX, FX_INT(inward)); /* how far the puck is out from the goal line */
        fx inside = g->ice.netHalfW - FX_INT(4);
        fx ty;
        if (out < FX_INT(8)) ty = Fx_Clamp(p.y, -inside, inside); /* at or behind the line: hug the post */
        else ty = Fx_Mul(p.y, Fx_Div(depth, out));                /* on the line from the puck to the middle of the net */
        ty += FX_INT(k->lean);
        ty = Fx_Clamp(ty, -inside, inside);
        V2 target = V2_Make(netX + depth * inward, ty);
        V2 d = V2_Sub(target, k->pos);
        fx max = GoalieSpeed(g, t);
        k->vel = V2_ClampLen(d, max);
        k->pos = V2_Add(k->pos, k->vel);
    }
}

/* ---------------------------------------------------------------- set-up */

void Game_DefaultSetup(GameSetup *s, uint64_t seed) {
    memset(s, 0, sizeof(*s));
    s->seed = seed;
    s->periodSeconds = 120;
    s->periods = 2;
    s->venue = VENUE_POND;
    s->mode = MODE_MATCH;
    s->twist = TWIST_NONE;
    s->teamLevel[0] = s->teamLevel[1] = DIFF_NORMAL;
    for (int i = 0; i < MAX_PLAYERS; i++) s->slotTeam[i] = -1;
    s->slotTeam[0] = 0;
    for (int t = 0; t < 2; t++) {
        s->build[t][0] = BUILD_QUICK;
        s->build[t][1] = BUILD_BALANCED;
        s->build[t][2] = BUILD_BIG;
        s->goalieStyle[t] = GOALIE_BUTTERFLY;
    }
}

static void ResetPuck(Puck *p) {
    memset(p, 0, sizeof(*p));
    p->carrier = -1;
    p->lastTouch = -1;
    p->prevTouch = -1;
    p->passTo = -1;
    p->heldBy = -1;
}

static void ResetGoalies(Game *g) {
    for (int t = 0; t < 2; t++) {
        Goalie *k = &g->gk[t];
        if (!k->present) continue;
        k->pos = Game_GoalieHome(g, t);
        k->vel = V2_Make(0, 0);
        k->recover = 0;
        k->hold = 0;
    }
}

static void ClearSkater(Skater *s) {
    s->vel = V2_Make(0, 0);
    s->stun = s->noPickup = s->actCool = s->shootBuf = s->charge = s->aiWait = s->aiCharge = 0;
    s->flags = 0;
}

static void EnterFaceoff(Game *g) {
    g->phase = PH_FACEOFF;
    g->phaseTicks = 0;
    for (int t = 0; t < 2; t++) {
        int first = t * TEAM_SIZE, centre = first, best = MAX_PLAYERS;
        for (int i = first; i < first + TEAM_SIZE; i++) {
            if (g->sk[i].player >= 0 && g->sk[i].player < best) { best = g->sk[i].player; centre = i; }
        }
        g->centre[t] = (int8_t)centre;
        fx mx = t == 0 ? FX_ONE : -FX_ONE;
        V2 spots[3] = {
            V2_Make(Fx_Mul(FORM_CENTRE_X, mx), 0),
            V2_Make(Fx_Mul(FORM_WING_X, mx), Fx_Mul(FORM_WING_Y, mx)),
            V2_Make(Fx_Mul(FORM_BACK_X, mx), Fx_Mul(FORM_BACK_Y, mx)),
        };
        int next = 1;
        for (int i = first; i < first + TEAM_SIZE; i++) {
            Skater *s = &g->sk[i];
            s->pos = i == centre ? spots[0] : spots[next++];
            s->face = V2_Make(t == 0 ? FX_ONE : -FX_ONE, 0);
            ClearSkater(s);
        }
        g->falseStart[t] = 0;
    }
    ResetPuck(&g->puck);
    ResetGoalies(g);
    g->dropAt = DROP_MIN + (int32_t)Rng_Range(&g->rng, DROP_RAND);
    for (int t = 0; t < 2; t++) g->aiFaceoff[t] = (int16_t)Ai_FaceoffReaction(g, t);
}

/* A shootout attempt: the shooter at centre ice with the puck, the goalie set. */
static void EnterShootoutReady(Game *g) {
    g->phase = PH_SO_READY;
    g->phaseTicks = 0;
    Skater *s = &g->sk[0];
    s->pos = V2_Make(FX_INT(-40), 0);
    s->face = V2_Make(FX_ONE, 0);
    ClearSkater(s);
    ResetPuck(&g->puck);
    g->puck.carrier = 0;
    g->puck.lastTouch = 0;
    g->puck.pos = Game_StickPoint(s);
    ResetGoalies(g);
}

void Game_Init(Game *g, const GameSetup *setup) {
    memset(g, 0, sizeof(*g)); /* padding too: the state is copied and compared whole */
    g->setup = *setup;
    GameSetup *s = &g->setup;
    if (s->periodSeconds < 30) s->periodSeconds = 30;
    if (s->periodSeconds > 600) s->periodSeconds = 600;
    if (s->periods < 1) s->periods = 1;
    if (s->periods > 3) s->periods = 3;
    if (s->venue >= VENUE_COUNT) s->venue = VENUE_POND;
    if (s->mode > MODE_SHOOTOUT) s->mode = MODE_MATCH;
    if (s->twist >= TWIST_COUNT) s->twist = TWIST_NONE;
    if (s->mode == MODE_SHOOTOUT && s->venue == VENUE_POND) s->venue = VENUE_RINK; /* a shootout needs a goalie */
    for (int t = 0; t < 2; t++) {
        if (s->teamLevel[t] >= DIFF_COUNT) s->teamLevel[t] = DIFF_NORMAL;
        if (s->goalieStyle[t] >= GOALIE_STYLE_COUNT) s->goalieStyle[t] = GOALIE_BUTTERFLY;
    }
    Game_IceFor(s->venue, s->twist, &g->ice);
    Rng_Seed(&g->rng, s->seed);
    for (int i = 0; i < SKATERS; i++) {
        Skater *k = &g->sk[i];
        k->team = (uint8_t)Game_TeamOf(i);
        uint8_t b = s->build[k->team][i % TEAM_SIZE];
        k->build = b < BUILD_COUNT ? b : BUILD_BALANCED;
        uint8_t tr = s->trait[k->team][i % TEAM_SIZE];
        k->trait = tr < TRAIT_COUNT ? tr : TRAIT_NONE;
        k->player = -1;
        k->benched = s->mode == MODE_SHOOTOUT && i != 0;
    }
    for (int t = 0; t < 2; t++) {
        g->gk[t].present = g->ice.goalies && (s->mode == MODE_MATCH || t == 1);
        g->gk[t].style = s->goalieStyle[t];
    }
    for (int p = 0; p < MAX_PLAYERS; p++) g->slotSkater[p] = -1;
    if (s->mode == MODE_SHOOTOUT) {
        /* the shooter is the first player, if there is one */
        for (int p = 0; p < MAX_PLAYERS; p++) s->slotTeam[p] = -1;
        if (setup->slotTeam[0] >= 0) {
            s->slotTeam[0] = 0;
            g->sk[0].player = 0;
            g->slotSkater[0] = 0;
        }
    } else {
        for (int p = 0; p < MAX_PLAYERS; p++) {
            int team = s->slotTeam[p];
            if (team == 0 || team == 1) Game_JoinPlayer(g, p, team);
            else s->slotTeam[p] = -1;
        }
    }
    if (s->twist == TWIST_WIND) {
        V2 dir = Fx_DirVector((uint8_t)(1 + 4 * Rng_Range(&g->rng, 8))); /* one of eight ways */
        g->wind = V2_Scale(dir, WIND_PUSH);
    }
    g->events = 0;
    g->period = 1;
    g->clock = s->periodSeconds * TICK_HZ;
    g->snowPerTick = g->ice.snow ? FX_RATIO(7, 10) / g->clock : 0;
    if (s->twist == TWIST_BLIZZARD) g->snowPerTick = Fx_Mul(g->snowPerTick, FX_RATIO(22, 10));
    g->lastGoalTeam = g->lastScorer = g->lastAssist = -1;
    g->winner = -1;
    if (s->mode == MODE_SHOOTOUT) EnterShootoutReady(g);
    else EnterFaceoff(g);
}

bool Game_JoinPlayer(Game *g, int slot, int team) {
    if (slot < 0 || slot >= MAX_PLAYERS || (team != 0 && team != 1)) return false;
    if (g->setup.mode == MODE_SHOOTOUT) return false;
    if (g->slotSkater[slot] >= 0) Game_LeavePlayer(g, slot);
    int best = -1;
    int64_t bestD = 0;
    for (int i = team * TEAM_SIZE; i < (team + 1) * TEAM_SIZE; i++) {
        if (g->sk[i].player >= 0 || g->sk[i].benched) continue;
        int64_t d = V2_Len2(V2_Sub(g->sk[i].pos, g->puck.pos));
        if (best < 0 || d < bestD) { best = i; bestD = d; }
    }
    if (best < 0) return false;
    g->setup.slotTeam[slot] = (int8_t)team;
    g->sk[best].player = (int8_t)slot;
    g->slotSkater[slot] = (int8_t)best;
    return true;
}

bool Game_LeavePlayer(Game *g, int slot) {
    if (slot < 0 || slot >= MAX_PLAYERS || g->slotSkater[slot] < 0) return false;
    g->sk[g->slotSkater[slot]].player = -1;
    g->slotSkater[slot] = -1;
    g->setup.slotTeam[slot] = -1;
    return true;
}

/* ------------------------------------------------------------ the puck */

static void Release(Game *g, int i, V2 vel, uint8_t flight) {
    Puck *p = &g->puck;
    p->carrier = -1;
    p->vel = vel;
    p->flight = flight;
    p->age = 0;
    p->oneTimer = 0;
    p->passTo = -1;
    g->sk[i].noPickup = RELEASE_NO_PICKUP;
    g->sk[i].charge = 0;
}

/* Which side of the net a shot went for: 0 upper post, 1 middle, 2 lower post. */
static int ShotSide(fx ty) { return ty < FX_INT(-10) ? 0 : ty > FX_INT(10) ? 2 : 1; }

/* A shot. charge 0 is a wrist shot (or a one-timer), more is a slapshot. */
static void Shoot(Game *g, int i, V2 aim, bool oneTimer, int charge) {
    Skater *s = &g->sk[i];
    Puck *p = &g->puck;
    if (V2_IsZero(aim)) aim = s->face;
    fx speed = kBuild[s->build].shotSpeed;
    int spread = kBuild[s->build].scatter;
    bool slap = charge >= CHARGE_MIN;
    if (slap) {
        int c = charge > CHARGE_FULL ? CHARGE_FULL : charge;
        speed += Fx_Mul(speed, FX_RATIO(60, 100) * c / CHARGE_FULL);
        spread += spread * 8 * c / (10 * CHARGE_FULL);
        if (s->trait == TRAIT_CANNON) speed = Fx_Mul(speed, FX_RATIO(125, 100));
    } else if (s->trait == TRAIT_CANNON) {
        speed = Fx_Mul(speed, FX_RATIO(112, 100));
    }
    if (oneTimer) speed = Fx_Mul(speed, g->ice.slapshots ? SLAP_ONE_TIMER_BOOST : ONE_TIMER_BOOST);
    if (s->trait == TRAIT_SNIPER) spread /= 2;

    int sign = AttackSign(s->team);
    fx gx = sign > 0 ? g->ice.goalX : -g->ice.goalX;
    V2 toNet = V2_Sub(V2_Make(gx, 0), p->pos);
    fx dist = V2_Len(toNet);
    bool inFront = Fx_Mul(p->pos.x, sign * FX_ONE) < g->ice.goalX - FX_INT(4);
    fx ty = 0;
    if (inFront && V2_Dot(aim, V2_Norm(toNet)) > FX_RATIO(766, 1000) && Fx_Abs(aim.x) > FX_RATIO(3, 10)) {
        /* aimed anywhere near the net: it goes for the mouth, where the aim
         * line meets the goal line, inside the posts, plus some spread */
        fx inside = g->ice.netHalfW - FX_INT(7);
        ty = p->pos.y + Fx_Mul(aim.y, Fx_Div(gx - p->pos.x, aim.x));
        ty = Fx_Clamp(ty, -inside, inside);
        spread += dist / FX_INT(40);
        if (s->player < 0) spread += Ai_ShotScatter(g->setup.teamLevel[s->team]);
        if (oneTimer) spread /= 2;
        if (spread > 0) ty += FX_INT((int)Rng_Range(&g->rng, (uint32_t)(2 * spread + 1)) - spread);
        aim = V2_Norm(V2_Sub(V2_Make(gx, ty), p->pos));
    }
    if (s->player >= 0) {
        Habits *h = &g->habits[s->player];
        h->shots++;
        h->sideShots[ShotSide(ty)]++;
        h->shotDistance += dist / FX_ONE;
    }
    if (g->setup.mode == MODE_SHOOTOUT) g->soLastSide = (int8_t)(ShotSide(ty) - 1);
    Release(g, i, V2_Scale(aim, speed), FLIGHT_SHOT);
    p->oneTimer = oneTimer ? 1 : 0;
    s->actCool = SHOT_COOL;
    s->shots++;
    s->flags |= SKF_SHOT;
    g->events |= EV_SHOT | (oneTimer ? EV_ONE_TIMER : 0) | (slap ? EV_SLAPSHOT : 0);
}

static void Pass(Game *g, int i, V2 aim) {
    Skater *s = &g->sk[i];
    if (V2_IsZero(aim)) aim = s->face;
    int first = s->team * TEAM_SIZE, best = -1;
    fx bestScore = 0;
    for (int j = first; j < first + TEAM_SIZE; j++) {
        if (j == i || g->sk[j].benched) continue;
        V2 v = V2_Sub(g->sk[j].pos, s->pos);
        fx d = V2_Len(v);
        fx align = V2_Dot(V2_Norm(v), aim);
        fx score = 2 * align - Fx_Div(d, FX_INT(300));
        if (g->sk[j].player >= 0) score += FX_RATIO(1, 4);
        if (align < FX_RATIO(-2, 10)) score -= FX_RATIO(3, 2);
        if (best < 0 || score > bestScore) { best = j; bestScore = score; }
    }
    if (best < 0) return;
    Skater *to = &g->sk[best];
    fx speed = s->trait == TRAIT_PLAYMAKER ? Fx_Mul(PASS_SPEED, FX_RATIO(115, 100)) : PASS_SPEED;
    fx d = V2_Dist(to->pos, g->puck.pos);
    fx lead = Fx_Min(Fx_Div(d, speed), FX_INT(40));
    V2 target = V2_Add(to->pos, V2_Scale(to->vel, lead));
    V2 dir = V2_Norm(V2_Sub(target, g->puck.pos));
    if (V2_IsZero(dir)) dir = aim;
    Release(g, i, V2_Scale(dir, speed), FLIGHT_PASS);
    g->puck.passTo = (int8_t)best;
    s->actCool = PASS_COOL;
    s->flags |= SKF_PASSED;
    if (s->player >= 0) g->habits[s->player].passes++;
    g->events |= EV_PASS;
}

static void Pickup(Game *g, int i) {
    Puck *p = &g->puck;
    Skater *s = &g->sk[i];
    bool fromPass = p->flight == FLIGHT_PASS && p->passTo == i;
    if (p->lastTouch >= 0 && p->lastTouch != i && Game_TeamOf(p->lastTouch) == s->team) p->prevTouch = p->lastTouch;
    else if (p->lastTouch < 0 || Game_TeamOf(p->lastTouch) != s->team) p->prevTouch = -1;
    p->lastTouch = (int8_t)i;
    p->carrier = (int8_t)i;
    p->flight = FLIGHT_NONE;
    p->passTo = -1;
    p->age = 0;
    p->vel = s->vel;
    g->events |= EV_PICKUP;

    if (s->player < 0 && HumansOnTeam(g, s->team) == 1 && g->setup.mode == MODE_MATCH) {
        for (int slot = 0; slot < MAX_PLAYERS; slot++) {
            int k = g->slotSkater[slot];
            if (k >= 0 && Game_TeamOf(k) == s->team) { GiveControl(g, slot, i); break; }
        }
    }
    if (fromPass && s->shootBuf > 0) {
        /* straight off the pass, for the side of the net the goalie is
         * leaving (the middle on a pond, where there's no goalie) */
        int sign = AttackSign(s->team);
        fx gx = sign > 0 ? g->ice.goalX : -g->ice.goalX, ty = 0;
        const Goalie *k = &g->gk[1 - s->team];
        if (k->present) ty = Fx_Mul(g->ice.netHalfW - FX_INT(8), k->pos.y > 0 ? -FX_ONE : FX_ONE);
        V2 aim = V2_Norm(V2_Sub(V2_Make(gx, ty), p->pos));
        s->shootBuf = 0;
        Shoot(g, i, aim, true, 0);
    }
}

static void Goal(Game *g, int scoringTeam) {
    Puck *p = &g->puck;
    g->score[scoringTeam]++;
    g->lastGoalTeam = (int8_t)scoringTeam;
    int scorer = p->lastTouch >= 0 && Game_TeamOf(p->lastTouch) == scoringTeam ? p->lastTouch : -1;
    int assist = scorer >= 0 && p->prevTouch >= 0 && p->prevTouch != scorer && Game_TeamOf(p->prevTouch) == scoringTeam ? p->prevTouch : -1;
    g->lastScorer = (int8_t)scorer;
    g->lastAssist = (int8_t)assist;
    if (scorer >= 0) g->sk[scorer].goals++;
    if (assist >= 0) g->sk[assist].assists++;
    if (p->oneTimer) g->oneTimerGoals[scoringTeam]++;
    p->carrier = -1;
    p->vel = V2_Scale(p->vel, FX_RATIO(15, 100));
    p->flight = FLIGHT_NONE;
    p->passTo = -1;
    g->events |= EV_GOAL;
    g->phaseTicks = 0;
    if (g->setup.mode == MODE_SHOOTOUT) {
        g->soStreak++;
        if (g->soStreak > g->soBestStreak) g->soBestStreak = g->soStreak;
        Goalie *k = &g->gk[1];
        k->lean = (int16_t)(k->lean + 4 * g->soLastSide); /* he remembers where you beat him */
        if (k->lean > 12) k->lean = 12;
        if (k->lean < -12) k->lean = -12;
        g->phase = PH_SO_RESULT;
    } else {
        g->phase = PH_GOAL;
    }
}

static void ShootoutMiss(Game *g);

/* The goalie has it: a whistle and a faceoff (or the end of a shootout try). */
static void Freeze(Game *g, int team) {
    Puck *p = &g->puck;
    Goalie *k = &g->gk[team];
    p->carrier = -1;
    p->vel = V2_Make(0, 0);
    p->flight = FLIGHT_NONE;
    p->passTo = -1;
    p->heldBy = (int8_t)team;
    int inward = team == 0 ? 1 : -1;
    p->pos = V2_Add(k->pos, V2_Make(FX_INT(6) * inward, 0));
    k->hold = 1;
    k->saves++;
    g->events |= EV_FREEZE | EV_SAVE;
    g->phaseTicks = 0;
    if (g->setup.mode == MODE_SHOOTOUT) {
        ShootoutMiss(g); /* a save he holds on to is a miss like any other */
        g->phase = PH_SO_RESULT;
    } else {
        g->phase = PH_STOPPAGE;
    }
}

static void ShootoutMiss(Game *g) {
    if (g->setup.mode != MODE_SHOOTOUT || g->phase != PH_PLAY) return;
    g->soMisses++;
    g->soStreak = 0;
    Goalie *k = &g->gk[1];
    k->lean = (int16_t)(k->lean + 2 * g->soLastSide); /* and where you like to shoot */
    if (k->lean > 12) k->lean = 12;
    if (k->lean < -12) k->lean = -12;
    g->events |= EV_SO_MISS;
    g->phase = PH_SO_RESULT;
    g->phaseTicks = 0;
}

/* A loose puck meets a goalie: a fast one rebounds, a slow one is held. */
static bool GoalieCollide(Game *g) {
    Puck *p = &g->puck;
    for (int t = 0; t < 2; t++) {
        Goalie *k = &g->gk[t];
        if (!k->present || k->hold > 0) continue;
        fx reach = GStats(k)->reach + PUCK_R;
        if (!V2_Within(p->pos, k->pos, reach)) continue;
        V2 n = V2_Norm(V2_Sub(p->pos, k->pos));
        if (V2_IsZero(n)) n = V2_Make(t == 0 ? FX_ONE : -FX_ONE, 0);
        fx speed = V2_Len(p->vel);
        bool shot = p->flight == FLIGHT_SHOT;
        if (speed < REBOUND_SPEED) {
            Freeze(g, t);
            return true;
        }
        fx vn = V2_Dot(p->vel, n);
        if (vn < 0) p->vel = V2_Sub(p->vel, V2_Scale(n, vn + Fx_Mul(-vn, FX_RATIO(35, 100))));
        V2 side = V2_Perp(n);
        fx jitter = FX_INT((int)Rng_Range(&g->rng, 5) - 2) / 2;
        p->vel = V2_Add(V2_Scale(p->vel, FX_RATIO(6, 10)), V2_Scale(side, jitter));
        p->pos = V2_Add(k->pos, V2_Scale(n, reach));
        p->flight = FLIGHT_NONE;
        p->passTo = -1;
        k->recover = GOALIE_RECOVER;
        if (shot) { k->saves++; g->events |= EV_SAVE; }
        else g->events |= EV_BLOCK;
        if (g->setup.mode == MODE_SHOOTOUT) ShootoutMiss(g);
        return true;
    }
    return false;
}

/* The nets, for a puck that moved from prev to p->pos this sub-step. Each net
 * is a box behind its goal line, open at the front; anything that gets into
 * it except through the mouth is pushed back out of the side it came in by.
 * True if that was a goal. */
static bool NetCollide(Game *g, V2 prev) {
    Puck *p = &g->puck;
    fx NW = g->ice.netHalfW, ND = g->ice.netDepth;
    for (int net = 0; net < 2; net++) {
        fx out = net == 0 ? -FX_ONE : FX_ONE;
        fx gx = net == 0 ? -g->ice.goalX : g->ice.goalX;
        fx lx = Fx_Mul(p->pos.x - gx, out), ly = p->pos.y;
        fx plx = Fx_Mul(prev.x - gx, out), ply = prev.y;
        fx r = PUCK_R;
        if (lx < -r || lx > ND + r || Fx_Abs(ly) > NW + r) continue;
        if (lx < 0 && Fx_Abs(ly) < NW - r) continue; /* in front of the open mouth */
        bool wasInside = plx >= 0 && plx <= ND && Fx_Abs(ply) < NW - r;
        if (wasInside) {
            p->pos.x = gx + Fx_Mul(Fx_Clamp(lx, 0, ND - r), out);
            p->pos.y = Fx_Clamp(ly, -(NW - r), NW - r);
            p->vel = V2_Make(0, 0);
            return false;
        }
        if (plx < 0 && lx >= 0 && Fx_Abs(ly) < NW - r && Fx_Abs(ply) < NW - r) {
            Goal(g, net == 0 ? 1 : 0); /* the left net is team 0's, so team 1 scored */
            return true;
        }
        if (plx > ND + r - FX_ONE) {
            p->pos.x = gx + Fx_Mul(ND + r, out);
            p->vel.x = -Fx_Mul(p->vel.x, NET_BOUNCE);
            g->events |= EV_NET;
        } else if (Fx_Abs(ply) >= NW + r - FX_ONE && plx >= -r + FX_ONE) {
            fx side = ply < 0 ? -FX_ONE : FX_ONE;
            p->pos.y = Fx_Mul(NW + r, side);
            p->vel.y = -Fx_Mul(p->vel.y, NET_BOUNCE);
            g->events |= EV_NET;
        } else {
            p->pos.x = gx - Fx_Mul(r, out);
            p->vel.x = -Fx_Mul(p->vel.x, POST_BOUNCE);
            g->events |= EV_POST;
        }
        p->flight = FLIGHT_NONE;
        p->passTo = -1;
    }
    return false;
}

/* The edge of the ice: a snowbank buries a hard puck, boards bounce it. */
static bool EdgeCollide(Game *g) {
    Puck *p = &g->puck;
    if (Game_OnIce(g, p->pos, PUCK_R)) return false;
    V2 c = InnerPoint(g, p->pos);
    V2 n = V2_Norm(V2_Sub(p->pos, c));
    if (V2_IsZero(n)) n = V2_Make(0, FX_ONE);
    p->pos = V2_Add(c, V2_Scale(n, g->ice.r - PUCK_R));
    fx vn = V2_Dot(p->vel, n);
    if (!g->ice.boards && vn > BURY_SPEED) {
        p->vel = V2_Make(0, 0);
        p->buried = BURY_TICKS;
        p->carrier = -1;
        p->flight = FLIGHT_NONE;
        p->passTo = -1;
        g->events |= EV_BANK;
        return true;
    }
    if (vn > 0) {
        fx bounce = g->ice.boards ? BOARD_BOUNCE : BANK_BOUNCE;
        p->vel = V2_Sub(p->vel, V2_Scale(n, vn + Fx_Mul(vn, bounce)));
        p->vel = V2_Scale(p->vel, g->ice.boards ? FX_RATIO(95, 100) : FX_RATIO(90, 100));
        if (vn > FX_RATIO(1, 2)) g->events |= g->ice.boards ? EV_BOARDS : EV_BANK_SOFT;
    }
    return false;
}

static bool BodyBlock(Game *g) {
    Puck *p = &g->puck;
    if (p->flight == FLIGHT_NONE || p->lastTouch < 0) return false;
    int shooterTeam = Game_TeamOf(p->lastTouch);
    for (int i = 0; i < SKATERS; i++) {
        Skater *s = &g->sk[i];
        if (s->team == shooterTeam || s->stun > 0 || s->benched) continue;
        fx reach = SKATER_R + PUCK_R + (s->trait == TRAIT_WALL ? FX_INT(4) : 0);
        if (!V2_Within(p->pos, s->pos, reach)) continue;
        V2 n = V2_Norm(V2_Sub(p->pos, s->pos));
        if (V2_IsZero(n)) n = V2_Neg(V2_Norm(p->vel));
        fx vn = V2_Dot(p->vel, n);
        if (vn < 0) p->vel = V2_Sub(p->vel, V2_Scale(n, vn + Fx_Mul(-vn, FX_RATIO(3, 10))));
        p->vel = V2_Scale(p->vel, FX_RATIO(4, 10));
        p->pos = V2_Add(s->pos, V2_Scale(n, reach));
        p->flight = FLIGHT_NONE;
        p->passTo = -1;
        g->events |= EV_BLOCK;
        return true;
    }
    return false;
}

static bool TryPickup(Game *g) {
    Puck *p = &g->puck;
    int best = -1;
    int64_t bestD = 0;
    for (int i = 0; i < SKATERS; i++) {
        Skater *s = &g->sk[i];
        if (s->stun > 0 || s->noPickup > 0 || s->benched) continue;
        if (p->flight == FLIGHT_SHOT && p->lastTouch >= 0 && Game_TeamOf(p->lastTouch) == s->team && p->age < 30) continue;
        fx r = (p->flight == FLIGHT_PASS && p->passTo == i) ? RECEIVE_R : PICKUP_R;
        if (p->flight == FLIGHT_PASS && p->passTo == i && p->lastTouch >= 0 && g->sk[p->lastTouch].trait == TRAIT_PLAYMAKER) r += FX_INT(3);
        int64_t d = V2_Len2(V2_Sub(p->pos, Game_StickPoint(s)));
        if (d <= (int64_t)r * r && (best < 0 || d < bestD)) { best = i; bestD = d; }
    }
    if (best < 0) return false;
    Pickup(g, best);
    return true;
}

/* A carried puck sits on the blade: it can cross a goal line (a tuck-in),
 * but it can't go through a net, a goalie, or the edge of the ice. A goalie
 * can smother a puck dangled into his pads. */
static void CarryPuck(Game *g) {
    Puck *p = &g->puck;
    Skater *c = &g->sk[p->carrier];
    V2 prev = p->pos;
    p->pos = Game_StickPoint(c);
    p->vel = c->vel;
    if (c->player >= 0) g->habits[c->player].carryTicks++;
    if (NetCollide(g, prev)) return;
    for (int t = 0; t < 2; t++) {
        Goalie *k = &g->gk[t];
        if (!k->present || k->hold > 0) continue;
        fx reach = GStats(k)->reach + PUCK_R;
        if (!V2_Within(p->pos, k->pos, reach)) continue;
        int chance = (k->style == GOALIE_POKER ? 12 : 6) + 4 * g->setup.teamLevel[t];
        if (c->team != t && (int)Rng_Range(&g->rng, 100) < chance) { Freeze(g, t); return; }
        V2 n = V2_Norm(V2_Sub(p->pos, k->pos));
        if (V2_IsZero(n)) n = V2_Make(0, FX_ONE);
        p->pos = V2_Add(k->pos, V2_Scale(n, reach));
    }
    if (!Game_OnIce(g, p->pos, PUCK_R)) p->pos = Game_ClampToIce(g, p->pos, PUCK_R);
}

static void UpdatePuck(Game *g) {
    Puck *p = &g->puck;
    if (p->buried > 0) {
        if (--p->buried == 0) {
            V2 c = InnerPoint(g, p->pos);
            V2 n = V2_Norm(V2_Sub(p->pos, c));
            p->pos = V2_Add(c, V2_Scale(n, g->ice.r - FX_INT(24)));
            p->vel = V2_Scale(n, -FX_ONE);
            p->age = 0;
            g->events |= EV_DIG_OUT;
        }
        return;
    }
    if (p->carrier >= 0) {
        if (g->sk[p->carrier].stun > 0) p->carrier = -1;
        else { CarryPuck(g); return; }
    }
    if (!V2_IsZero(g->wind)) p->vel = V2_Add(p->vel, g->wind);
    fx speed = V2_Len(p->vel);
    int steps = 1 + speed / FX_INT(3);
    if (steps > 8) steps = 8;
    for (int k = 0; k < steps; k++) {
        V2 prev = p->pos;
        p->pos = V2_Add(p->pos, V2_Make(p->vel.x / steps, p->vel.y / steps));
        if (NetCollide(g, prev)) return;
        if (GoalieCollide(g)) return;
        if (EdgeCollide(g)) return;
        if (BodyBlock(g)) break;
        if (TryPickup(g)) return;
    }
    p->vel = V2_Scale(p->vel, FX_ONE - Friction(g, PUCK_FRICTION, PUCK_SNOW_FRICTION));
    /* a puck this slow has stopped (unless the wind is pushing it along) */
    if (V2_IsZero(g->wind) && V2_Len2(p->vel) < (int64_t)FX_RATIO(5, 100) * FX_RATIO(5, 100)) p->vel = V2_Make(0, 0);
    if (p->age < 30000) p->age++;
    if (p->flight == FLIGHT_PASS && p->age > 50) { p->flight = FLIGHT_NONE; p->passTo = -1; }
    if (p->flight == FLIGHT_SHOT && p->age > 70) p->flight = FLIGHT_NONE;
}

/* --------------------------------------------------------------- skaters */

static void Switch(Game *g, int i) {
    Skater *s = &g->sk[i];
    if (s->player < 0) return;
    int best = -1;
    int64_t bestD = 0;
    for (int j = s->team * TEAM_SIZE; j < (s->team + 1) * TEAM_SIZE; j++) {
        if (j == i || g->sk[j].player >= 0 || g->sk[j].benched) continue;
        int64_t d = V2_Len2(V2_Sub(g->sk[j].pos, g->puck.pos));
        if (best < 0 || d < bestD) { best = j; bestD = d; }
    }
    if (best >= 0) GiveControl(g, s->player, best);
}

static void Check(Game *g, int i) {
    Skater *s = &g->sk[i];
    int c = g->puck.carrier;
    fx range = CHECK_RANGE + (s->trait == TRAIT_PEST ? FX_INT(6) : 0);
    if (c < 0 || g->sk[c].team == s->team || !V2_Within(g->sk[c].pos, Game_StickPoint(s), range)) {
        Switch(g, i);
        return;
    }
    Skater *t = &g->sk[c];
    if (s->player >= 0) g->habits[s->player].checks++;
    fx speed = V2_Len(s->vel);
    V2 toward = V2_Norm(V2_Sub(t->pos, s->pos));
    if (V2_IsZero(toward)) toward = s->face;
    bool body = speed > BODY_CHECK_SPEED && V2_Dot(V2_Norm(s->vel), toward) > FX_RATIO(1, 2);
    int chance = body ? kBuild[s->build].checkPower - kBuild[t->build].checkPower / 3 + 15 : 45;
    if (s->trait == TRAIT_PEST) chance += 20;
    if (t->trait == TRAIT_DANGLER) chance -= 15;
    if (t->trait == TRAIT_WALL) chance -= 20;
    if ((int)Rng_Range(&g->rng, 100) >= chance) {
        s->actCool = WHIFF_COOL;
        if (body) s->vel = V2_Scale(s->vel, FX_RATIO(6, 10));
        g->events |= EV_WHIFF;
        return;
    }
    Puck *p = &g->puck;
    p->carrier = -1;
    p->flight = FLIGHT_NONE;
    p->passTo = -1;
    p->age = 0;
    t->charge = 0;
    if (body) {
        V2 side = V2_Perp(toward);
        if (Rng_Range(&g->rng, 2)) side = V2_Neg(side);
        p->vel = V2_Add(V2_Scale(t->vel, FX_RATIO(1, 2)), V2_Add(V2_Scale(toward, FX_RATIO(15, 10)), V2_Scale(side, FX_INT(2))));
        t->stun = STUN_TICKS;
        t->vel = V2_Add(V2_Scale(t->vel, FX_RATIO(1, 2)), V2_Scale(toward, FX_INT(2)));
        t->flags |= SKF_HIT;
        g->events |= EV_CHECK;
    } else {
        p->vel = V2_Scale(V2_Neg(toward), FX_RATIO(15, 10));
        g->events |= EV_POKE;
    }
    t->noPickup = LOOSE_NO_PICKUP;
    s->actCool = CHECK_COOL;
    s->flags |= SKF_CHECKED;
}

/* Shoot: a tap is a wrist shot; on a rink, holding winds up a slapshot that
 * goes when you let go (where the stick points then). */
static void Act(Game *g, int i, const Cmd *cmd) {
    Skater *s = &g->sk[i];
    if (s->stun > 0 || s->benched) { s->charge = 0; return; }
    bool has = g->puck.carrier == i;
    bool held = (cmd->hold & BTN_SHOOT) != 0;
    if (s->charge > 0) {
        if (!has) {
            s->charge = 0;
        } else if (held) {
            if (s->charge < CHARGE_MAX) s->charge++;
            if (s->charge == CHARGE_MIN) g->events |= EV_WINDUP; /* held long enough: this one is a slapshot */
            return;
        } else {
            int c = s->charge;
            s->charge = 0;
            Shoot(g, i, cmd->move, false, c >= CHARGE_MIN ? c : 0);
            return;
        }
    }
    if (cmd->press & BTN_SHOOT) {
        if (has && s->actCool == 0) {
            if (g->ice.slapshots) {
                s->charge = 1; /* a tap let go at once is still a wrist shot */
            } else {
                Shoot(g, i, cmd->move, false, 0);
            }
            return;
        }
        if (!has) s->shootBuf = ONE_TIMER_WINDOW;
    }
    if ((cmd->press & BTN_PASS) && has && s->actCool == 0) { Pass(g, i, cmd->move); return; }
    if ((cmd->press & BTN_CHECK) && !has && s->actCool == 0) Check(g, i);
}

static void KeepOffNets(const Game *g, Skater *s) {
    for (int net = 0; net < 2; net++) {
        fx gx = net == 0 ? -g->ice.goalX : g->ice.goalX;
        fx x0 = net == 0 ? gx - g->ice.netDepth : gx, x1 = net == 0 ? gx : gx + g->ice.netDepth;
        fx r = SKATER_R, NW = g->ice.netHalfW;
        if (s->pos.x < x0 - r || s->pos.x > x1 + r || Fx_Abs(s->pos.y) > NW + r) continue;
        fx dl = s->pos.x - (x0 - r), dr = (x1 + r) - s->pos.x, dy = NW + r - Fx_Abs(s->pos.y);
        if (dy <= dl && dy <= dr) {
            fx side = s->pos.y < 0 ? -FX_ONE : FX_ONE;
            s->pos.y = Fx_Mul(NW + r, side);
            s->vel.y = 0;
        } else if (dl < dr) {
            s->pos.x = x0 - r;
            if (s->vel.x > 0) s->vel.x = 0;
        } else {
            s->pos.x = x1 + r;
            if (s->vel.x < 0) s->vel.x = 0;
        }
    }
}

static void KeepOffGoalies(const Game *g, Skater *s) {
    for (int t = 0; t < 2; t++) {
        const Goalie *k = &g->gk[t];
        if (!k->present) continue;
        fx reach = GStats(k)->reach + SKATER_R - FX_INT(2);
        if (!V2_Within(s->pos, k->pos, reach)) continue;
        V2 n = V2_Norm(V2_Sub(s->pos, k->pos));
        if (V2_IsZero(n)) n = V2_Make(t == 0 ? FX_ONE : -FX_ONE, 0);
        s->pos = V2_Add(k->pos, V2_Scale(n, reach));
        fx vn = V2_Dot(s->vel, n);
        if (vn < 0) s->vel = V2_Sub(s->vel, V2_Scale(n, vn));
    }
}

static void Contain(const Game *g, Skater *s) {
    KeepOffNets(g, s);
    KeepOffGoalies(g, s);
    if (!Game_OnIce(g, s->pos, SKATER_R)) {
        V2 c = InnerPoint(g, s->pos);
        V2 n = V2_Norm(V2_Sub(s->pos, c));
        if (V2_IsZero(n)) n = V2_Make(0, FX_ONE);
        s->pos = V2_Add(c, V2_Scale(n, g->ice.r - SKATER_R));
        fx vn = V2_Dot(s->vel, n);
        if (vn > 0) s->vel = V2_Sub(s->vel, V2_Scale(n, vn));
    }
}

static void Move(Game *g, int i, const Cmd *cmd) {
    Skater *s = &g->sk[i];
    if (s->benched) return;
    const BuildStats *b = &kBuild[s->build];
    V2 m = s->stun > 0 ? V2_Make(0, 0) : cmd->move;
    if (V2_IsZero(m)) {
        s->vel = V2_Scale(s->vel, FX_ONE - Friction(g, ICE_FRICTION, SNOW_FRICTION));
    } else {
        fx max = b->maxSpeed;
        if (g->puck.carrier == i && s->trait != TRAIT_DANGLER) max = Fx_Mul(max, CARRY_SPEED);
        if (s->charge > 0) max = Fx_Mul(max, WINDUP_SPEED);
        if (s->player < 0) max = Fx_Mul(max, Ai_SpeedFactor(g->setup.teamLevel[s->team]));
        fx speed = V2_Len(s->vel);
        if (speed > STOP_SPEED && V2_Dot(V2_Norm(s->vel), m) < STOP_ALONG) {
            s->vel = V2_Scale(s->vel, FX_ONE - STOP_BRAKE);
            s->flags |= SKF_STOPPED;
            g->events |= EV_STOP;
        } else {
            s->vel = V2_Add(s->vel, V2_Scale(m, b->accel));
            s->vel = V2_Scale(s->vel, FX_ONE - SKATE_DRAG);
            if (V2_Len2(s->vel) > (int64_t)max * max) {
                fx len = V2_Len(s->vel);
                fx eased = Fx_Max(max, len - b->accel);
                s->vel = V2_Scale(V2_Norm(s->vel), eased);
            }
        }
        V2 f = V2_Norm(V2_Add(s->face, V2_Scale(m, TURN_RATE)));
        if (!V2_IsZero(f)) s->face = f;
    }
    s->pos = V2_Add(s->pos, s->vel);
    Contain(g, s);
}

static void Bump(Game *g) {
    fx reach = 2 * SKATER_R;
    for (int i = 0; i < SKATERS; i++) {
        for (int j = i + 1; j < SKATERS; j++) {
            Skater *a = &g->sk[i], *b = &g->sk[j];
            if (a->benched || b->benched) continue;
            V2 d = V2_Sub(b->pos, a->pos);
            if (V2_Len2(d) >= (int64_t)reach * reach) continue;
            fx len = V2_Len(d);
            V2 n = len > 0 ? V2_Norm(d) : V2_Make(FX_ONE, 0);
            if (V2_IsZero(n)) n = V2_Make(FX_ONE, 0);
            V2 push = V2_Scale(n, (reach - len) / 2);
            a->pos = V2_Sub(a->pos, push);
            b->pos = V2_Add(b->pos, push);
            fx closing = V2_Dot(V2_Sub(a->vel, b->vel), n);
            if (closing > 0) {
                V2 imp = V2_Scale(n, closing / 2);
                a->vel = V2_Sub(a->vel, imp);
                b->vel = V2_Add(b->vel, imp);
            }
        }
    }
    for (int i = 0; i < SKATERS; i++) if (!g->sk[i].benched) Contain(g, &g->sk[i]);
}

static void TickTimers(Skater *s) {
    if (s->stun > 0) s->stun--;
    if (s->noPickup > 0) s->noPickup--;
    if (s->actCool > 0) s->actCool--;
    if (s->shootBuf > 0) s->shootBuf--;
    if (s->aiWait > 0) s->aiWait--;
}

/* ---------------------------------------------------------------- phases */

static void GameOver(Game *g) {
    g->phase = PH_OVER;
    g->phaseTicks = 0;
    g->winner = (int8_t)(g->setup.mode == MODE_SHOOTOUT ? 0 : g->score[0] > g->score[1] ? 0 : 1);
    g->events |= EV_GAME_OVER;
}

static void EndPeriod(Game *g) {
    g->events |= EV_PERIOD_END;
    if (g->period >= g->setup.periods && g->score[0] != g->score[1]) {
        GameOver(g);
        return;
    }
    g->phase = PH_BREAK;
    g->phaseTicks = 0;
    g->puck.carrier = -1;
}

static void CommandsFor(Game *g, const uint8_t press[MAX_PLAYERS], const PadInput in[MAX_PLAYERS], Cmd cmd[SKATERS]) {
    for (int i = 0; i < SKATERS; i++) {
        Skater *s = &g->sk[i];
        if (s->benched) { cmd[i] = (Cmd){{0, 0}, 0, 0}; continue; }
        if (s->player >= 0) {
            cmd[i].move = Fx_DirVector(in[s->player].dir);
            cmd[i].press = press[s->player];
            cmd[i].hold = in[s->player].buttons;
        } else {
            Ai_Command(g, i, &cmd[i]);
        }
    }
}

static void Glide(Game *g) {
    Cmd none = {{0, 0}, 0, 0};
    for (int i = 0; i < SKATERS; i++) Move(g, i, &none);
    Bump(g);
}

static void FaceoffTick(Game *g, const uint8_t press[MAX_PLAYERS]) {
    g->phaseTicks++;
    bool dropped = g->phaseTicks >= g->dropAt;
    if (g->phaseTicks == g->dropAt) g->events |= EV_DROP;
    bool goes[2] = {false, false};
    for (int t = 0; t < 2; t++) {
        Skater *c = &g->sk[g->centre[t]];
        bool pressed = c->player >= 0 ? (press[c->player] & BTN_PASS) != 0 : dropped && g->phaseTicks >= g->dropAt + g->aiFaceoff[t];
        if (!pressed) continue;
        if (!dropped) {
            g->falseStart[t] = FALSE_START_LOCK;
            g->events |= EV_FALSE_START;
        } else if (g->falseStart[t] == 0) {
            goes[t] = true;
        }
    }
    if (dropped) {
        for (int t = 0; t < 2; t++) if (g->falseStart[t] > 0) g->falseStart[t]--;
    }
    int winner = goes[0] && goes[1] ? (int)Rng_Range(&g->rng, 2) : goes[0] ? 0 : goes[1] ? 1 : -1;
    if (winner >= 0) {
        int c = g->centre[winner];
        g->puck.carrier = (int8_t)c;
        g->puck.lastTouch = (int8_t)c;
        g->phase = PH_PLAY;
        g->phaseTicks = 0;
        g->events |= EV_FACEOFF_WON;
    } else if (dropped && g->phaseTicks > g->dropAt + FACEOFF_GIVE_UP) {
        g->phase = PH_PLAY;
        g->phaseTicks = 0;
    }
}

/* Has this shootout try come to nothing? */
static void ShootoutChecks(Game *g) {
    if (g->setup.mode != MODE_SHOOTOUT || g->phase != PH_PLAY) return;
    const Puck *p = &g->puck;
    bool loose = p->carrier < 0 && p->buried == 0;
    bool stopped = loose && V2_Len2(p->vel) < (int64_t)FX_RATIO(1, 2) * FX_RATIO(1, 2) && g->phaseTicks > 30;
    bool behind = p->pos.x > g->ice.goalX + FX_INT(2);
    if (g->phaseTicks > SO_TIMEOUT || stopped || behind) ShootoutMiss(g);
}

static void PlayTick(Game *g, const uint8_t press[MAX_PLAYERS], const PadInput in[MAX_PLAYERS]) {
    g->phaseTicks++;
    for (int i = 0; i < SKATERS; i++) TickTimers(&g->sk[i]);
    Cmd cmd[SKATERS];
    CommandsFor(g, press, in, cmd);
    for (int i = 0; i < SKATERS; i++) Act(g, i, &cmd[i]);
    if (g->phase != PH_PLAY) return; /* a one-timer straight in can end the tick */
    for (int i = 0; i < SKATERS; i++) Move(g, i, &cmd[i]);
    Bump(g);
    MoveGoalies(g);
    UpdatePuck(g);
    if (g->phase != PH_PLAY) return; /* a goal, a save held, a shootout try over */
    ShootoutChecks(g);
    if (g->phase != PH_PLAY || g->setup.mode == MODE_SHOOTOUT) return;
    g->snow = Fx_Min(FX_ONE, g->snow + g->snowPerTick);
    if (!Game_SuddenDeath(g) && --g->clock <= 0) {
        g->clock = 0;
        EndPeriod(g);
    }
}

static int GoalTicks(const Game *g) { return g->setup.venue == VENUE_ARENA ? ARENA_GOAL_TICKS : GOAL_TICKS; }

void Game_Step(Game *g, const PadInput in[MAX_PLAYERS]) {
    g->events = 0;
    for (int i = 0; i < SKATERS; i++) g->sk[i].flags = 0;
    uint8_t press[MAX_PLAYERS];
    for (int p = 0; p < MAX_PLAYERS; p++) {
        uint8_t b = (uint8_t)(in[p].buttons & BTN_ALL);
        press[p] = (uint8_t)(b & ~g->prevButtons[p]);
        g->prevButtons[p] = b;
    }
    switch (g->phase) {
        case PH_FACEOFF: FaceoffTick(g, press); break;
        case PH_PLAY: PlayTick(g, press, in); break;
        case PH_GOAL:
            g->phaseTicks++;
            Glide(g);
            if (g->phaseTicks >= GoalTicks(g)) {
                if (Game_SuddenDeath(g)) GameOver(g);
                else if (g->clock <= 0) EndPeriod(g);
                else EnterFaceoff(g);
            }
            break;
        case PH_STOPPAGE:
            g->phaseTicks++;
            Glide(g);
            if (g->phaseTicks >= STOPPAGE_TICKS) {
                if (!Game_SuddenDeath(g) && g->clock <= 0) EndPeriod(g);
                else EnterFaceoff(g);
            }
            break;
        case PH_BREAK:
            /* the ice gets cleaned: shovels on the pond, a tractor on the rinks */
            g->phaseTicks++;
            Glide(g);
            g->snow = Fx_Max(0, g->snow - Fx_Max(g->snow / 40, FX_RATIO(1, 1000)));
            if (g->phaseTicks >= BREAK_TICKS) {
                g->snow = 0;
                g->period++;
                g->clock = Game_SuddenDeath(g) ? 0 : g->setup.periodSeconds * TICK_HZ;
                g->events |= EV_BREAK_DONE;
                EnterFaceoff(g);
            }
            break;
        case PH_SO_READY:
            g->phaseTicks++;
            if (g->phaseTicks >= SO_READY_TICKS) {
                g->phase = PH_PLAY;
                g->phaseTicks = 0;
                g->soAttempts++;
                g->events |= EV_DROP;
            }
            break;
        case PH_SO_RESULT:
            g->phaseTicks++;
            Glide(g);
            if (g->phaseTicks >= SO_RESULT_TICKS) {
                if (g->soMisses >= 3) GameOver(g);
                else EnterShootoutReady(g);
            }
            break;
        default: break;
    }
    g->tick++;
}

/* ------------------------------------------------------------- the hash */

static void H(uint64_t *h, int64_t v) {
    uint64_t u = (uint64_t)v;
    for (int b = 0; b < 8; b++) {
        *h ^= (u >> (8 * b)) & 0xffu;
        *h *= 1099511628211ull; /* FNV-1a, field by field so padding never counts */
    }
}

uint64_t Game_Hash(const Game *g) {
    uint64_t h = 1469598103934665603ull;
    H(&h, (int64_t)g->rng.state);
    H(&h, g->setup.venue); H(&h, g->setup.mode); H(&h, g->setup.twist);
    H(&h, g->tick);
    H(&h, g->phase);
    H(&h, g->phaseTicks);
    H(&h, g->period);
    H(&h, g->clock);
    H(&h, g->score[0]); H(&h, g->score[1]);
    H(&h, g->oneTimerGoals[0]); H(&h, g->oneTimerGoals[1]);
    H(&h, g->snow);
    H(&h, g->wind.x); H(&h, g->wind.y);
    H(&h, g->dropAt);
    for (int t = 0; t < 2; t++) {
        H(&h, g->centre[t]);
        H(&h, g->falseStart[t]);
        H(&h, g->aiFaceoff[t]);
        const Goalie *k = &g->gk[t];
        H(&h, k->present); H(&h, k->pos.x); H(&h, k->pos.y); H(&h, k->vel.x); H(&h, k->vel.y);
        H(&h, k->recover); H(&h, k->hold); H(&h, k->lean); H(&h, k->saves);
    }
    for (int p = 0; p < MAX_PLAYERS; p++) {
        H(&h, g->slotSkater[p]);
        H(&h, g->prevButtons[p]);
        H(&h, g->setup.slotTeam[p]);
    }
    for (int i = 0; i < SKATERS; i++) {
        const Skater *s = &g->sk[i];
        H(&h, s->pos.x); H(&h, s->pos.y);
        H(&h, s->vel.x); H(&h, s->vel.y);
        H(&h, s->face.x); H(&h, s->face.y);
        H(&h, s->player); H(&h, s->benched);
        H(&h, s->stun); H(&h, s->noPickup); H(&h, s->actCool); H(&h, s->shootBuf); H(&h, s->charge);
        H(&h, s->aiWait); H(&h, s->aiCharge);
        H(&h, s->goals); H(&h, s->assists); H(&h, s->shots);
    }
    const Puck *p = &g->puck;
    H(&h, p->pos.x); H(&h, p->pos.y);
    H(&h, p->vel.x); H(&h, p->vel.y);
    H(&h, p->carrier); H(&h, p->lastTouch); H(&h, p->prevTouch); H(&h, p->passTo);
    H(&h, p->flight); H(&h, p->oneTimer); H(&h, p->heldBy); H(&h, p->buried); H(&h, p->age);
    H(&h, g->soAttempts); H(&h, g->soMisses); H(&h, g->soStreak); H(&h, g->soLastSide);
    H(&h, g->lastGoalTeam); H(&h, g->lastScorer); H(&h, g->lastAssist); H(&h, g->winner);
    return h;
}
