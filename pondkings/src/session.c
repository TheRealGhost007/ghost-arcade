#include "session.h"
#include <string.h>

void Session_Init(Session *s, const GameSetup *setup, uint8_t localMask, uint8_t remoteMask) {
    memset(s, 0, sizeof(*s));
    Game_Init(&s->game, setup);
    s->localMask = localMask;
    s->remoteMask = remoteMask;
    /* the first SESSION_INPUT_DELAY ticks have no presses on either machine */
    s->localKnown = SESSION_INPUT_DELAY;
    s->remoteKnown = SESSION_INPUT_DELAY;
    s->remoteAck = SESSION_INPUT_DELAY;
    s->rollbackFrom = SESSION_NONE;
    s->theirs.tick = SESSION_NONE;
}

bool Session_CanStep(const Session *s) {
    if (s->desync) return false;
    if (s->tick >= s->remoteKnown + SESSION_MAX_AHEAD) return false;
    /* never write over presses the other machine has not acked */
    if (s->localKnown + 1 - s->remoteAck >= SESSION_RING) return false;
    return true;
}

/* The presses tick t is stepped with: the truth where it is known, else the
 * guess (the other machine keeps doing what it last did). */
static void InputsFor(const Session *s, uint32_t t, PadInput in[MAX_PLAYERS]) {
    const PadInput *row = s->input[t % SESSION_RING];
    const PadInput *last = s->input[(s->remoteKnown - 1) % SESSION_RING];
    for (int p = 0; p < MAX_PLAYERS; p++) {
        uint8_t bit = (uint8_t)(1u << p);
        if (s->localMask & bit) in[p] = row[p];
        else if (s->remoteMask & bit) in[p] = t < s->remoteKnown ? row[p] : last[p];
        else in[p] = (PadInput){0, 0};
    }
}

static void StepOnce(Session *s, uint32_t t) {
    PadInput in[MAX_PLAYERS];
    InputsFor(s, t, in);
    memcpy(s->used[t % SESSION_RING], in, sizeof(in));
    s->snap[t % SESSION_SNAPS] = s->game;
    Game_Step(&s->game, in);
}

static void Remember(Session *s, uint32_t tick, uint64_t hash) {
    if (s->mineCount < SESSION_HASH_KEEP) s->mine[s->mineCount++] = (SessionHash){tick, hash};
    else {
        memmove(s->mine, s->mine + 1, sizeof(s->mine[0]) * (SESSION_HASH_KEEP - 1));
        s->mine[SESSION_HASH_KEEP - 1] = (SessionHash){tick, hash};
    }
}

static void Compare(Session *s, uint32_t tick, uint64_t theirs) {
    for (int i = 0; i < s->mineCount; i++)
        if (s->mine[i].tick == tick && s->mine[i].hash != theirs && !s->desync) {
            s->desync = true;
            s->desyncTick = tick;
        }
}

/* Fingerprints each SESSION_HASH_EVERY-th state once every press before it
 * is the real one (and the stepping used them: no correction pending). */
static void Fingerprint(Session *s) {
    while (s->nextHash < s->tick && s->nextHash <= s->remoteKnown && s->rollbackFrom == SESSION_NONE) {
        if (s->tick - s->nextHash < SESSION_SNAPS) {
            uint64_t h = Game_Hash(&s->snap[s->nextHash % SESSION_SNAPS]);
            Remember(s, s->nextHash, h);
            if (s->theirs.tick == s->nextHash) Compare(s, s->nextHash, s->theirs.hash);
        }
        s->nextHash += SESSION_HASH_EVERY;
    }
}

void Session_Catchup(Session *s) {
    if (s->rollbackFrom < s->tick) {
        uint32_t from = s->rollbackFrom;
        s->game = s->snap[from % SESSION_SNAPS];
        for (uint32_t t = from; t < s->tick; t++) StepOnce(s, t);
        s->rollbacks++;
        s->resimTicks += s->tick - from;
    }
    s->rollbackFrom = SESSION_NONE;
    Fingerprint(s);
}

void Session_Step(Session *s, const PadInput local[MAX_PLAYERS], void (*onTick)(const Game *g, void *ctx), void *ctx) {
    PadInput *row = s->input[s->localKnown % SESSION_RING];
    for (int p = 0; p < MAX_PLAYERS; p++)
        if (s->localMask & (1u << p)) {
            PadInput in = local[p];
            if (in.dir > DIR_COUNT) in.dir = 0;
            in.buttons &= BTN_ALL;
            row[p] = in;
        }
    s->localKnown++;

    Session_Catchup(s);
    StepOnce(s, s->tick);
    s->tick++;
    if (onTick) onTick(&s->game, ctx);
    Fingerprint(s);
}

void Session_Receive(Session *s, const WireInput *w) {
    if (!w) return;
    if (w->mask == s->remoteMask) {
        for (int k = 0; k < w->count; k++) {
            uint32_t t = w->firstTick + (uint32_t)k;
            if (t < s->remoteKnown) continue;                 /* had it */
            if (t > s->remoteKnown) break;                    /* a gap: a later packet resends from it */
            if (t >= s->tick + SESSION_RING / 2) break;       /* further ahead than any real game gets */
            PadInput *row = s->input[t % SESSION_RING];
            bool wrong = false;
            for (int p = 0; p < MAX_PLAYERS; p++) {
                if (!(s->remoteMask & (1u << p))) continue;
                row[p] = w->in[k][p];
                if (t < s->tick) {
                    const PadInput *u = &s->used[t % SESSION_RING][p];
                    if (u->dir != row[p].dir || u->buttons != row[p].buttons) wrong = true;
                }
            }
            if (wrong && t < s->rollbackFrom) s->rollbackFrom = t;
            s->remoteKnown = t + 1;
        }
    }
    if (w->ack > s->remoteAck && w->ack <= s->localKnown) s->remoteAck = w->ack;
    if (w->tick > s->remoteTick && w->tick <= s->tick + SESSION_RING) s->remoteTick = w->tick;
    if (w->hashTick != WIRE_NO_HASH) {
        Compare(s, w->hashTick, w->hash);
        if (s->theirs.tick == SESSION_NONE || w->hashTick > s->theirs.tick) s->theirs = (SessionHash){w->hashTick, w->hash};
    }
}

void Session_Outgoing(const Session *s, WireInput *out) {
    memset(out, 0, sizeof(*out));
    uint32_t from = s->remoteAck;
    if (from >= s->localKnown) from = s->localKnown - 1; /* nothing new: repeat the last tick, as a keep-alive */
    uint32_t count = s->localKnown - from;
    if (count > WIRE_MAX_TICKS) count = WIRE_MAX_TICKS;
    out->firstTick = from;
    out->count = (uint8_t)count;
    out->mask = s->localMask;
    for (uint32_t k = 0; k < count; k++)
        for (int p = 0; p < MAX_PLAYERS; p++)
            if (s->localMask & (1u << p)) out->in[k][p] = s->input[(from + k) % SESSION_RING][p];
    out->ack = s->remoteKnown;
    out->tick = s->tick;
    if (s->mineCount > 0) {
        out->hashTick = s->mine[s->mineCount - 1].tick;
        out->hash = s->mine[s->mineCount - 1].hash;
    } else {
        out->hashTick = WIRE_NO_HASH;
    }
}

bool Session_Settled(const Session *s) {
    return s->remoteKnown >= s->tick && s->rollbackFrom == SESSION_NONE;
}

int Session_Advantage(const Session *s, int oneWayTicks) {
    return (int)s->tick - ((int)s->remoteTick + oneWayTicks);
}
