#include "wire.h"
#include <string.h>

/* ------------------------------------------------------------ bytes */

typedef struct {
    uint8_t *p;
    int len, cap;
    bool bad;
} Out;

static void PutU8(Out *o, uint32_t v) {
    if (o->len + 1 > o->cap) { o->bad = true; return; }
    o->p[o->len++] = (uint8_t)v;
}
static void PutU16(Out *o, uint32_t v) { PutU8(o, v & 0xFF); PutU8(o, (v >> 8) & 0xFF); }
static void PutU32(Out *o, uint32_t v) { PutU16(o, v & 0xFFFF); PutU16(o, v >> 16); }
static void PutU64(Out *o, uint64_t v) { PutU32(o, (uint32_t)v); PutU32(o, (uint32_t)(v >> 32)); }

typedef struct {
    const uint8_t *p;
    int len, at;
    bool bad;
} In;

static uint32_t GetU8(In *r) {
    if (r->at + 1 > r->len) { r->bad = true; return 0; }
    return r->p[r->at++];
}
static uint32_t GetU16(In *r) { uint32_t lo = GetU8(r); return lo | (GetU8(r) << 8); }
static uint32_t GetU32(In *r) { uint32_t lo = GetU16(r); return lo | (GetU16(r) << 16); }
static uint64_t GetU64(In *r) { uint64_t lo = GetU32(r); return lo | ((uint64_t)GetU32(r) << 32); }

static int Bits(uint32_t mask) {
    int n = 0;
    for (; mask; mask &= mask - 1) n++;
    return n;
}

/* ------------------------------------------------------------ setup */

#define LEAN_LIMIT 50

static void PutSetup(Out *o, const WireSetup *w) {
    const GameSetup *s = &w->setup;
    PutU64(o, s->seed);
    PutU16(o, (uint32_t)s->periodSeconds);
    PutU8(o, (uint32_t)s->periods);
    PutU8(o, s->venue);
    PutU8(o, s->mode);
    PutU8(o, s->twist);
    for (int t = 0; t < 2; t++) PutU8(o, s->teamLevel[t]);
    for (int p = 0; p < MAX_PLAYERS; p++) PutU8(o, (uint8_t)s->slotTeam[p]);
    for (int t = 0; t < 2; t++)
        for (int k = 0; k < TEAM_SIZE; k++) PutU8(o, s->build[t][k]);
    for (int t = 0; t < 2; t++)
        for (int k = 0; k < TEAM_SIZE; k++) PutU8(o, s->trait[t][k]);
    for (int t = 0; t < 2; t++) PutU8(o, s->goalieStyle[t]);
    for (int t = 0; t < 2; t++) {
        PutU8(o, (uint8_t)s->lean[t].shoot);
        PutU8(o, (uint8_t)s->lean[t].pass);
        PutU8(o, (uint8_t)s->lean[t].check);
        PutU8(o, (uint8_t)s->lean[t].side);
    }
    PutU8(o, w->rivals);
    PutU8(o, w->hostMask);
    PutU8(o, w->guestMask);
}

static void GetSetup(In *r, WireSetup *w) {
    memset(w, 0, sizeof(*w));
    GameSetup *s = &w->setup;
    s->seed = GetU64(r);
    s->periodSeconds = (int32_t)GetU16(r);
    s->periods = (int32_t)GetU8(r);
    s->venue = (uint8_t)GetU8(r);
    s->mode = (uint8_t)GetU8(r);
    s->twist = (uint8_t)GetU8(r);
    for (int t = 0; t < 2; t++) s->teamLevel[t] = (uint8_t)GetU8(r);
    for (int p = 0; p < MAX_PLAYERS; p++) s->slotTeam[p] = (int8_t)(uint8_t)GetU8(r);
    for (int t = 0; t < 2; t++)
        for (int k = 0; k < TEAM_SIZE; k++) s->build[t][k] = (uint8_t)GetU8(r);
    for (int t = 0; t < 2; t++)
        for (int k = 0; k < TEAM_SIZE; k++) s->trait[t][k] = (uint8_t)GetU8(r);
    for (int t = 0; t < 2; t++) s->goalieStyle[t] = (uint8_t)GetU8(r);
    for (int t = 0; t < 2; t++) {
        s->lean[t].shoot = (int8_t)(uint8_t)GetU8(r);
        s->lean[t].pass = (int8_t)(uint8_t)GetU8(r);
        s->lean[t].check = (int8_t)(uint8_t)GetU8(r);
        s->lean[t].side = (int8_t)(uint8_t)GetU8(r);
    }
    w->rivals = (uint8_t)GetU8(r);
    w->hostMask = (uint8_t)GetU8(r);
    w->guestMask = (uint8_t)GetU8(r);
}

static bool LeanOk(const AiLean *l) {
    return l->shoot >= -LEAN_LIMIT && l->shoot <= LEAN_LIMIT && l->pass >= -LEAN_LIMIT && l->pass <= LEAN_LIMIT &&
           l->check >= -LEAN_LIMIT && l->check <= LEAN_LIMIT && l->side >= -1 && l->side <= 1;
}

bool Wire_SetupValid(const WireSetup *w) {
    if (!w) return false;
    const GameSetup *s = &w->setup;
    if (s->periodSeconds < 30 || s->periodSeconds > 600 || s->periods < 1 || s->periods > 3) return false;
    if (s->venue >= VENUE_COUNT || s->mode > MODE_SHOOTOUT || s->twist >= TWIST_COUNT) return false;
    for (int t = 0; t < 2; t++) {
        if (s->teamLevel[t] >= DIFF_COUNT || s->goalieStyle[t] >= GOALIE_STYLE_COUNT || !LeanOk(&s->lean[t])) return false;
        for (int k = 0; k < TEAM_SIZE; k++)
            if (s->build[t][k] >= BUILD_COUNT || s->trait[t][k] >= TRAIT_COUNT) return false;
    }
    if (w->rivals >= 8) return false;
    const uint8_t all = (1u << MAX_PLAYERS) - 1;
    if (w->hostMask == 0 || w->guestMask == 0 || (w->hostMask & w->guestMask) != 0) return false;
    if ((w->hostMask | w->guestMask) & ~all) return false;
    int perTeam[2] = {0, 0};
    for (int p = 0; p < MAX_PLAYERS; p++) {
        bool owned = ((w->hostMask | w->guestMask) >> p) & 1;
        int8_t team = s->slotTeam[p];
        if (team < -1 || team > 1) return false;
        if (owned != (team >= 0)) return false; /* a slot plays exactly when a machine steers it */
        if (team >= 0) perTeam[team]++;
    }
    return perTeam[0] <= TEAM_SIZE && perTeam[1] <= TEAM_SIZE;
}

uint32_t Wire_SetupCheck(const WireSetup *s) {
    uint8_t buf[96];
    Out o = {buf, 0, (int)sizeof(buf), false};
    PutSetup(&o, s);
    uint32_t h = 2166136261u;
    for (int i = 0; i < o.len; i++) h = (h ^ buf[i]) * 16777619u;
    return h;
}

uint32_t Wire_TokenFromKey(const char *key) {
    uint32_t h = 2166136261u;
    for (const char *c = key ? key : ""; *c; c++) h = (h ^ (uint8_t)*c) * 16777619u;
    return h ? h : 1;
}

/* ------------------------------------------------------------ packets */

int Wire_Encode(const Packet *p, uint8_t *buf, int cap) {
    if (!p || !buf || p->type < PKT_HELLO || p->type >= PKT_TYPE_END) return 0;
    Out o = {buf, 0, cap, false};
    PutU8(&o, 'P');
    PutU8(&o, 'K');
    PutU8(&o, WIRE_VERSION);
    PutU8(&o, p->type);
    PutU32(&o, p->token);
    switch (p->type) {
        case PKT_HELLO:
        case PKT_HELLO_ACK:
            if (p->u.hello.role > ROLE_GUEST) return 0;
            PutU8(&o, p->u.hello.role);
            break;
        case PKT_SETUP:
            if (!Wire_SetupValid(&p->u.setup)) return 0;
            PutSetup(&o, &p->u.setup);
            break;
        case PKT_START_ACK: PutU32(&o, p->u.startAck.setupCheck); break;
        case PKT_INPUT: {
            const WireInput *w = &p->u.input;
            if (w->count < 1 || w->count > WIRE_MAX_TICKS || w->mask == 0 || w->mask >= (1u << MAX_PLAYERS)) return 0;
            if (w->firstTick > WIRE_MAX_TICK - w->count || w->ack > WIRE_MAX_TICK || w->tick > WIRE_MAX_TICK) return 0;
            PutU32(&o, w->firstTick);
            PutU8(&o, w->count);
            PutU8(&o, w->mask);
            for (int k = 0; k < w->count; k++)
                for (int s = 0; s < MAX_PLAYERS; s++) {
                    if (!((w->mask >> s) & 1)) continue;
                    if (w->in[k][s].dir > DIR_COUNT || w->in[k][s].buttons > BTN_ALL) return 0;
                    PutU8(&o, w->in[k][s].dir);
                    PutU8(&o, w->in[k][s].buttons);
                }
            PutU32(&o, w->ack);
            PutU32(&o, w->tick);
            PutU32(&o, w->hashTick);
            PutU64(&o, w->hash);
            break;
        }
        case PKT_PING:
        case PKT_PONG:
            PutU32(&o, p->u.ping.id);
            PutU32(&o, p->u.ping.ms);
            break;
        case PKT_BYE:
            if (p->u.bye.reason >= BYE_REASON_END) return 0;
            PutU8(&o, p->u.bye.reason);
            break;
        default: return 0;
    }
    return o.bad ? 0 : o.len;
}

bool Wire_Decode(const uint8_t *buf, int len, Packet *out) {
    if (!buf || !out || len < WIRE_HEADER || len > WIRE_MAX) return false;
    In r = {buf, len, 0, false};
    if (GetU8(&r) != 'P' || GetU8(&r) != 'K' || GetU8(&r) != WIRE_VERSION) return false;
    memset(out, 0, sizeof(*out));
    out->type = (uint8_t)GetU8(&r);
    out->token = GetU32(&r);
    switch (out->type) {
        case PKT_HELLO:
        case PKT_HELLO_ACK:
            out->u.hello.role = (uint8_t)GetU8(&r);
            if (out->u.hello.role > ROLE_GUEST) return false;
            break;
        case PKT_SETUP:
            GetSetup(&r, &out->u.setup);
            if (r.bad || !Wire_SetupValid(&out->u.setup)) return false;
            break;
        case PKT_START_ACK: out->u.startAck.setupCheck = GetU32(&r); break;
        case PKT_INPUT: {
            WireInput *w = &out->u.input;
            w->firstTick = GetU32(&r);
            w->count = (uint8_t)GetU8(&r);
            w->mask = (uint8_t)GetU8(&r);
            if (r.bad || w->count < 1 || w->count > WIRE_MAX_TICKS || w->mask == 0 || w->mask >= (1u << MAX_PLAYERS)) return false;
            if (w->firstTick > WIRE_MAX_TICK - w->count) return false;
            /* the exact length is known now: check it before reading on */
            if (len != WIRE_HEADER + 6 + w->count * Bits(w->mask) * 2 + 20) return false;
            for (int k = 0; k < w->count; k++)
                for (int s = 0; s < MAX_PLAYERS; s++) {
                    if (!((w->mask >> s) & 1)) continue;
                    uint32_t dir = GetU8(&r), buttons = GetU8(&r);
                    if (dir > DIR_COUNT || buttons > BTN_ALL) return false;
                    w->in[k][s].dir = (uint8_t)dir;
                    w->in[k][s].buttons = (uint8_t)buttons;
                }
            w->ack = GetU32(&r);
            w->tick = GetU32(&r);
            w->hashTick = GetU32(&r);
            w->hash = GetU64(&r);
            if (w->ack > WIRE_MAX_TICK || w->tick > WIRE_MAX_TICK) return false;
            if (w->hashTick != WIRE_NO_HASH && w->hashTick > WIRE_MAX_TICK) return false;
            break;
        }
        case PKT_PING:
        case PKT_PONG:
            out->u.ping.id = GetU32(&r);
            out->u.ping.ms = GetU32(&r);
            break;
        case PKT_BYE:
            out->u.bye.reason = (uint8_t)GetU8(&r);
            if (out->u.bye.reason >= BYE_REASON_END) return false;
            break;
        default: return false;
    }
    return !r.bad && r.at == len; /* nothing missing, nothing left over */
}
