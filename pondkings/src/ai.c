#include "ai.h"

/* How each difficulty plays. Reaction times are in ticks (60 a second). */
typedef struct {
    int reactMin, reactRand;  /* between decisions: shoot, pass, check */
    int checkPct;             /* chance of going for a check when in range */
    int shootPct, passPct;    /* chance of taking a good shot / an open pass */
    int slapPct;              /* chance a shot with time to spare is a slapshot (rinks) */
    fx shootRange;            /* from the goal line */
    fx speed;                 /* top-speed factor */
    int faceoffMin, faceoffRand;
    int scatter;              /* extra shot spread, world units */
} AiLevel;

static const AiLevel kLevels[DIFF_COUNT] = {
    /* easy   */ {20, 14, 35, 55, 45, 25, FX_INT(210), FX_RATIO(90, 100), 24, 14, 7},
    /* normal */ {12, 10, 50, 70, 60, 40, FX_INT(240), FX_RATIO(96, 100), 15, 10, 3},
    /* hard   */ {6, 8, 65, 85, 75, 50, FX_INT(265), FX_ONE, 9, 8, 0},
};

static const AiLevel *Level(int level) { return &kLevels[(level >= 0 && level < DIFF_COUNT) ? level : DIFF_NORMAL]; }
static const AiLevel *TeamLevel(const Game *g, int team) { return Level(g->setup.teamLevel[team]); }

fx Ai_SpeedFactor(int level) { return Level(level)->speed; }
int Ai_ShotScatter(int level) { return Level(level)->scatter; }
int Ai_FaceoffReaction(Game *g, int team) {
    const AiLevel *L = TeamLevel(g, team);
    return L->faceoffMin + (int)Rng_Range(&g->rng, (uint32_t)L->faceoffRand);
}

static int Clamp100(int v) { return v < 0 ? 0 : v > 100 ? 100 : v; }
static bool Roll(Game *g, int pct) { return (int)Rng_Range(&g->rng, 100) < Clamp100(pct); }

typedef enum { ROLE_CARRY, ROLE_CHASE, ROLE_PRESSURE, ROLE_GUARD, ROLE_COVER, ROLE_SLOT, ROLE_TRAIL, ROLE_RECEIVE } Role;

static int Sign(int team) { return team == 0 ? 1 : -1; }
static fx AttackX(const Game *g, int team) { return team == 0 ? g->ice.goalX : -g->ice.goalX; }
static fx DefendX(const Game *g, int team) { return team == 0 ? -g->ice.goalX : g->ice.goalX; }
static int64_t D2(V2 a, V2 b) { return V2_Len2(V2_Sub(a, b)); }

static V2 PuckSoon(const Game *g, V2 from) {
    const Puck *p = &g->puck;
    fx t = Fx_Min(Fx_Div(V2_Dist(from, p->pos), FX_INT(4)), FX_INT(30));
    V2 at = V2_Add(p->pos, V2_Scale(p->vel, Fx_Mul(t, FX_RATIO(7, 10))));
    if (!Game_OnIce(g, at, FX_INT(20))) at = p->pos;
    return at;
}

/* The team's roles this tick, worked out the same way for every member. */
static void TeamRoles(const Game *g, int team, Role roles[SKATERS]) {
    int first = team * TEAM_SIZE;
    const Puck *p = &g->puck;
    int carrier = p->carrier;
    bool ours = carrier >= 0 && Game_TeamOf(carrier) == team;
    bool theirs = carrier >= 0 && !ours;
    bool free[SKATERS] = {false};
    for (int i = first; i < first + TEAM_SIZE; i++) free[i] = !g->sk[i].benched;

    V2 ownMouth = V2_Make(DefendX(g, team), 0);
    if (ours) {
        roles[carrier] = ROLE_CARRY;
        free[carrier] = false;
        int sign = Sign(team);
        V2 slot = V2_Make(AttackX(g, team) - FX_INT(110) * sign, g->sk[carrier].pos.y > 0 ? FX_INT(-55) : FX_INT(55));
        int best = -1;
        for (int i = first; i < first + TEAM_SIZE; i++) {
            if (!free[i]) continue;
            if (best < 0 || D2(g->sk[i].pos, slot) < D2(g->sk[best].pos, slot)) best = i;
        }
        if (best >= 0) { roles[best] = ROLE_SLOT; free[best] = false; }
        for (int i = first; i < first + TEAM_SIZE; i++) if (free[i]) roles[i] = ROLE_TRAIL;
        return;
    }
    if (!theirs && p->flight == FLIGHT_PASS && p->passTo >= 0 && Game_TeamOf(p->passTo) == team) {
        roles[p->passTo] = ROLE_RECEIVE;
        free[p->passTo] = false;
    }
    int chaser = -1;
    V2 target = theirs ? g->sk[carrier].pos : p->pos;
    for (int i = first; i < first + TEAM_SIZE; i++) {
        if (!free[i]) continue;
        if (chaser < 0 || D2(g->sk[i].pos, target) < D2(g->sk[chaser].pos, target)) chaser = i;
    }
    if (chaser >= 0) { roles[chaser] = theirs ? ROLE_PRESSURE : ROLE_CHASE; free[chaser] = false; }
    int guard = -1;
    for (int i = first; i < first + TEAM_SIZE; i++) {
        if (!free[i]) continue;
        if (guard < 0 || D2(g->sk[i].pos, ownMouth) < D2(g->sk[guard].pos, ownMouth)) guard = i;
    }
    if (guard >= 0) { roles[guard] = ROLE_GUARD; free[guard] = false; }
    for (int i = first; i < first + TEAM_SIZE; i++) if (free[i]) roles[i] = theirs ? ROLE_COVER : ROLE_TRAIL;
}

static bool LaneClear(const Game *g, int team, V2 a, V2 b, fx margin) {
    V2 ab = V2_Sub(b, a);
    int64_t len2 = V2_Len2(ab);
    fx lenAb = V2_Len(ab);
    if (len2 == 0 || lenAb == 0) return true;
    for (int j = 0; j < SKATERS; j++) {
        if (Game_TeamOf(j) == team || g->sk[j].benched) continue;
        V2 ap = V2_Sub(g->sk[j].pos, a);
        int64_t along = (int64_t)ap.x * ab.x + (int64_t)ap.y * ab.y;
        if (along <= 0 || along >= len2) continue;
        int64_t cross = ((int64_t)ap.x * ab.y - (int64_t)ap.y * ab.x) / 65536;
        fx dist = (fx)((cross < 0 ? -cross : cross) * 65536 / lenAb);
        if (dist < margin) return false;
    }
    return true;
}

static fx NearestOpponent(const Game *g, int team, V2 at) {
    fx best = FX_INT(9999);
    for (int j = 0; j < SKATERS; j++) {
        if (Game_TeamOf(j) == team || g->sk[j].benched) continue;
        fx d = V2_Dist(g->sk[j].pos, at);
        if (d < best) best = d;
    }
    return best;
}

static V2 Steer(const Game *g, int i, V2 target) {
    const Skater *s = &g->sk[i];
    V2 to = V2_Sub(target, s->pos);
    if (V2_Len2(to) < (int64_t)FX_INT(8) * FX_INT(8)) return V2_Make(0, 0);
    V2 dir = V2_Norm(to);
    for (int j = s->team * TEAM_SIZE; j < (s->team + 1) * TEAM_SIZE; j++) {
        if (j == i || g->sk[j].benched) continue;
        V2 away = V2_Sub(s->pos, g->sk[j].pos);
        fx d = V2_Len(away);
        if (d >= FX_INT(40) || d == 0) continue;
        fx w = Fx_Div(FX_INT(40) - d, FX_INT(40));
        dir = V2_Add(dir, V2_Scale(V2_Norm(away), w));
    }
    return V2_Norm(dir);
}

static V2 Clamp(const Game *g, V2 p) { return Game_ClampToIce(g, p, SKATER_R); }

/* Where to put a shot: the side of the mouth the goalie isn't covering (or
 * the team's favourite side), else the middle. */
static V2 ShotTarget(const Game *g, int team) {
    fx gx = AttackX(g, team);
    fx inside = g->ice.netHalfW - FX_INT(6);
    const Goalie *k = &g->gk[1 - team];
    int side = g->setup.lean[team].side;
    if (k->present) {
        if (side == 0) side = k->pos.y > 0 ? -1 : 1;
        return V2_Make(gx, inside * side);
    }
    return V2_Make(gx, side == 0 ? 0 : (inside / 2) * side);
}

static void Carry(Game *g, int i, Cmd *out) {
    Skater *s = &g->sk[i];
    const AiLevel *L = TeamLevel(g, s->team);
    const AiLean *lean = &g->setup.lean[s->team];
    int sign = Sign(s->team);
    V2 mouth = V2_Make(AttackX(g, s->team), 0);
    V2 aimAt = ShotTarget(g, s->team);
    V2 toNet = V2_Sub(mouth, s->pos);
    fx d = V2_Len(toNet);
    fx depth = Fx_Mul(s->pos.x, FX_INT(sign));
    bool inFront = depth < g->ice.goalX - FX_INT(16);
    V2 aimDir = V2_Norm(V2_Sub(aimAt, g->puck.pos));

    /* winding up a slapshot: hold until it's charged, or someone closes in */
    if (s->charge > 0) {
        bool threat = NearestOpponent(g, s->team, s->pos) < FX_INT(30);
        out->move = aimDir;
        if (s->charge < s->aiCharge && !threat) out->hold = BTN_SHOOT;
        return;
    }

    fx range = L->shootRange + (s->trait == TRAIT_SNIPER ? FX_INT(30) : 0);
    if (s->aiWait == 0) {
        s->aiWait = (int16_t)(L->reactMin + (int)Rng_Range(&g->rng, (uint32_t)L->reactRand));
        bool lane = LaneClear(g, s->team, g->puck.pos, mouth, FX_INT(13));
        bool angle = Fx_Abs(s->pos.y) < Fx_Mul(Fx_Abs(mouth.x - s->pos.x), FX_RATIO(9, 10)) + FX_INT(20);
        if (inFront && angle && d < range && (lane || d < FX_INT(90)) && Roll(g, L->shootPct + lean->shoot)) {
            out->move = aimDir;
            out->press = BTN_SHOOT;
            bool time = NearestOpponent(g, s->team, s->pos) > FX_INT(60);
            int slap = L->slapPct + (s->trait == TRAIT_CANNON ? 30 : 0);
            if (g->ice.slapshots && time && d > FX_INT(110) && Roll(g, slap)) {
                s->aiCharge = (int16_t)(22 + Rng_Range(&g->rng, 16));
                out->hold = BTN_SHOOT;
            } else {
                s->aiCharge = 0; /* on a rink this starts a wind-up that is let go next tick: a wrist shot */
            }
            return;
        }
        fx pressure = NearestOpponent(g, s->team, s->pos);
        int passPct = L->passPct + lean->pass + (s->trait == TRAIT_PLAYMAKER ? 15 : 0);
        if (pressure < FX_INT(55) || (s->trait == TRAIT_PLAYMAKER && pressure < FX_INT(90))) {
            int best = -1;
            fx bestScore = 0;
            for (int j = s->team * TEAM_SIZE; j < (s->team + 1) * TEAM_SIZE; j++) {
                if (j == i || g->sk[j].benched) continue;
                Skater *t = &g->sk[j];
                if (!LaneClear(g, s->team, g->puck.pos, t->pos, FX_INT(12))) continue;
                fx open = Fx_Min(NearestOpponent(g, s->team, t->pos), FX_INT(120));
                fx ahead = Fx_Mul(t->pos.x - s->pos.x, FX_INT(sign));
                fx score = open + ahead / 2 + (t->player >= 0 ? FX_INT(40) : 0);
                if (best < 0 || score > bestScore) { best = j; bestScore = score; }
            }
            if (best >= 0 && Roll(g, passPct)) {
                out->move = V2_Norm(V2_Sub(g->sk[best].pos, s->pos));
                out->press = BTN_PASS;
                return;
            }
        }
    }
    V2 target = V2_Make(AttackX(g, s->team) - FX_INT(85) * sign, Fx_Mul(s->pos.y, FX_RATIO(3, 10)));
    if (!inFront) target = V2_Make(AttackX(g, s->team) - FX_INT(90) * sign, s->pos.y > 0 ? FX_INT(60) : FX_INT(-60));
    V2 dir = V2_Norm(V2_Sub(target, s->pos));
    for (int j = 0; j < SKATERS; j++) {
        if (Game_TeamOf(j) == s->team || g->sk[j].benched) continue;
        V2 v = V2_Sub(s->pos, g->sk[j].pos);
        fx dist = V2_Len(v);
        if (dist >= FX_INT(70) || dist == 0) continue;
        if (V2_Dot(V2_Norm(v), dir) > 0) continue;
        V2 side = V2_Perp(dir);
        if (V2_Dot(side, v) < 0) side = V2_Neg(side);
        dir = V2_Add(dir, V2_Scale(side, Fx_Div(FX_INT(70) - dist, FX_INT(70))));
    }
    /* in a shootout, cut across in front of the goalie before shooting */
    if (g->setup.mode == MODE_SHOOTOUT && d < FX_INT(160) && d > FX_INT(70)) {
        fx cut = g->gk[1].pos.y > 0 ? FX_INT(-50) : FX_INT(50);
        dir = V2_Norm(V2_Sub(V2_Make(AttackX(g, s->team) - FX_INT(70) * sign, cut), s->pos));
    }
    out->move = V2_Norm(dir);
}

void Ai_Command(Game *g, int i, Cmd *out) {
    out->move = V2_Make(0, 0);
    out->press = 0;
    out->hold = 0;
    Skater *s = &g->sk[i];
    if (s->stun > 0 || s->benched) return;
    const AiLevel *L = TeamLevel(g, s->team);
    Role roles[SKATERS];
    for (int k = 0; k < SKATERS; k++) roles[k] = ROLE_TRAIL;
    TeamRoles(g, s->team, roles);
    const Puck *p = &g->puck;
    int sign = Sign(s->team);
    V2 ownMouth = V2_Make(DefendX(g, s->team), 0);
    V2 target = s->pos;

    switch (roles[i]) {
        case ROLE_CARRY:
            Carry(g, i, out);
            return;
        case ROLE_CHASE:
            target = p->buried > 0 ? Clamp(g, p->pos) : PuckSoon(g, s->pos);
            break;
        case ROLE_RECEIVE: {
            target = PuckSoon(g, s->pos);
            fx depth = Fx_Mul(s->pos.x, FX_INT(sign));
            if (depth > FX_INT(120) && depth < g->ice.goalX - FX_INT(30) && V2_Within(p->pos, s->pos, FX_INT(60)) && s->shootBuf == 0 &&
                Roll(g, L->shootPct + g->setup.lean[s->team].shoot))
                out->press = BTN_SHOOT;
            break;
        }
        case ROLE_PRESSURE: {
            Skater *c = &g->sk[p->carrier];
            target = V2_Add(c->pos, V2_Scale(V2_Norm(V2_Sub(ownMouth, c->pos)), FX_INT(20)));
            fx reach = FX_INT(26) + (s->trait == TRAIT_PEST ? FX_INT(6) : 0);
            if (s->aiWait == 0 && s->actCool == 0 && V2_Within(c->pos, Game_StickPoint(s), reach)) {
                s->aiWait = (int16_t)(L->reactMin + (int)Rng_Range(&g->rng, (uint32_t)L->reactRand));
                int pct = L->checkPct + g->setup.lean[s->team].check + (s->trait == TRAIT_PEST ? 20 : 0);
                if (Roll(g, pct)) out->press = BTN_CHECK;
            }
            break;
        }
        case ROLE_GUARD: {
            /* no goalie: a step out from the goal line; with one: in front of the crease */
            V2 toPuck = V2_Norm(V2_Sub(p->pos, ownMouth));
            if (Fx_Mul(toPuck.x, FX_INT(sign)) < FX_RATIO(2, 10)) toPuck = V2_Make(FX_INT(sign), 0);
            fx stand = g->gk[s->team].present ? FX_INT(85) : FX_INT(38);
            target = V2_Add(ownMouth, V2_Scale(toPuck, stand));
            break;
        }
        case ROLE_COVER: {
            int mark = -1;
            for (int j = 0; j < SKATERS; j++) {
                if (Game_TeamOf(j) == s->team || j == p->carrier || g->sk[j].benched) continue;
                if (mark < 0 || D2(g->sk[j].pos, ownMouth) < D2(g->sk[mark].pos, ownMouth)) mark = j;
            }
            if (mark >= 0) {
                V2 o = g->sk[mark].pos;
                target = V2_Add(o, V2_Scale(V2_Norm(V2_Sub(ownMouth, o)), FX_INT(26)));
            }
            break;
        }
        case ROLE_SLOT: {
            int c = p->carrier;
            fx y = (c >= 0 && g->sk[c].pos.y > 0) ? FX_INT(-55) : FX_INT(55);
            target = V2_Make(AttackX(g, s->team) - FX_INT(110) * sign, y);
            break;
        }
        case ROLE_TRAIL:
        default: {
            fx x = p->pos.x - FX_INT(120) * sign;
            if (Fx_Mul(x, FX_INT(sign)) < -FX_INT(230)) x = -FX_INT(230) * sign;
            target = V2_Make(x, Fx_Mul(p->pos.y, FX_RATIO(-3, 10)));
            break;
        }
    }
    out->move = Steer(g, i, Clamp(g, target));
}
