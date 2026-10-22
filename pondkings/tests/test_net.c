/* Online play's raylib-free parts: the packet format (wire.c) and the
 * rollback session (session.c), run over a pretend network with lag,
 * jitter, loss, duplicates and reordering. Compiled by inclusion from
 * test_main.c (it shares CHECK, Lcg and RandomInputs). */
#include "../src/wire.h"
#include "../src/session.h"

static WireSetup NetSetup(uint64_t seed, int venue) {
    WireSetup w;
    memset(&w, 0, sizeof(w));
    Game_DefaultSetup(&w.setup, seed);
    for (int p = 0; p < MAX_PLAYERS; p++) w.setup.slotTeam[p] = -1;
    w.setup.slotTeam[0] = 0;
    w.setup.slotTeam[1] = 1;
    w.setup.venue = (uint8_t)venue;
    w.setup.periodSeconds = 30;
    w.setup.periods = 1;
    w.rivals = 2;
    w.hostMask = 1;
    w.guestMask = 2;
    return w;
}

static bool RoundTrip(const Packet *p, Packet *back, int *len) {
    uint8_t buf[WIRE_MAX];
    *len = Wire_Encode(p, buf, sizeof(buf));
    return *len > 0 && Wire_Decode(buf, *len, back);
}

static void test_wire_packets(void) {
    Packet p, q;
    int len;
    memset(&p, 0, sizeof(p));
    p.type = PKT_HELLO;
    p.token = 0xDEADBEEF;
    p.u.hello.role = ROLE_GUEST;
    CHECK(RoundTrip(&p, &q, &len) && q.type == PKT_HELLO && q.token == 0xDEADBEEF && q.u.hello.role == ROLE_GUEST && len == WIRE_HEADER + 1,
          "wire: a hello goes there and back");

    memset(&p, 0, sizeof(p));
    p.type = PKT_SETUP;
    p.token = 7;
    p.u.setup = NetSetup(0x1234567890ABCDEFull, VENUE_ARENA);
    p.u.setup.setup.lean[1] = (AiLean){-20, 15, 30, -1};
    p.u.setup.setup.trait[0][2] = TRAIT_CANNON;
    CHECK(RoundTrip(&p, &q, &len) && memcmp(&q.u.setup, &p.u.setup, sizeof(WireSetup)) == 0, "wire: a setup arrives exactly as sent");
    CHECK(Wire_SetupCheck(&q.u.setup) == Wire_SetupCheck(&p.u.setup), "wire: and fingerprints the same");
    WireSetup other = p.u.setup;
    other.setup.seed ^= 1;
    CHECK(Wire_SetupCheck(&other) != Wire_SetupCheck(&p.u.setup), "wire: a different setup fingerprints differently");

    memset(&p, 0, sizeof(p));
    p.type = PKT_INPUT;
    p.token = 99;
    WireInput *w = &p.u.input;
    w->firstTick = 1000;
    w->count = WIRE_MAX_TICKS;
    w->mask = 0x5; /* slots 0 and 2 */
    for (int k = 0; k < w->count; k++) {
        w->in[k][0] = (PadInput){(uint8_t)(k % (DIR_COUNT + 1)), (uint8_t)(k % 8)};
        w->in[k][2] = (PadInput){(uint8_t)((k * 7) % (DIR_COUNT + 1)), (uint8_t)((k * 3) % 8)};
    }
    w->ack = 990;
    w->tick = 1002;
    w->hashTick = 960;
    w->hash = 0xFEEDFACECAFEBEEFull;
    bool same = RoundTrip(&p, &q, &len);
    CHECK(same && len == WIRE_HEADER + 6 + 40 * 2 * 2 + 20 && len <= WIRE_MAX, "wire: the biggest input packet fits");
    for (int k = 0; same && k < w->count; k++)
        same = q.u.input.in[k][0].dir == w->in[k][0].dir && q.u.input.in[k][0].buttons == w->in[k][0].buttons &&
               q.u.input.in[k][2].dir == w->in[k][2].dir && q.u.input.in[k][2].buttons == w->in[k][2].buttons &&
               q.u.input.in[k][1].dir == 0 && q.u.input.in[k][3].buttons == 0;
    CHECK(same && q.u.input.ack == 990 && q.u.input.tick == 1002 && q.u.input.hashTick == 960 && q.u.input.hash == w->hash,
          "wire: every press, ack and fingerprint arrives");

    uint8_t buf[WIRE_MAX];
    len = Wire_Encode(&p, buf, sizeof(buf));
    Packet r;
    CHECK(!Wire_Decode(buf, len - 1, &r) && !Wire_Decode(buf, WIRE_HEADER - 1, &r), "wire: a short packet is dropped");
    uint8_t longer[WIRE_MAX + 8];
    memcpy(longer, buf, (size_t)len);
    longer[len] = 0;
    CHECK(!Wire_Decode(longer, len + 1, &r), "wire: a packet with bytes left over is dropped");
    buf[0] = 'Q';
    CHECK(!Wire_Decode(buf, len, &r), "wire: someone else's packet is dropped");
    buf[0] = 'P';
    buf[2] = WIRE_VERSION + 1;
    CHECK(!Wire_Decode(buf, len, &r), "wire: another version's packet is dropped");
    buf[2] = WIRE_VERSION;
    buf[WIRE_HEADER + 6] = DIR_COUNT + 1; /* the first press's direction */
    CHECK(!Wire_Decode(buf, len, &r), "wire: a direction out of range is dropped");
    buf[WIRE_HEADER + 6] = 0;
    buf[WIRE_HEADER + 7] = BTN_ALL + 1;
    CHECK(!Wire_Decode(buf, len, &r), "wire: unknown buttons are dropped");
    buf[WIRE_HEADER + 7] = 0;
    CHECK(Wire_Decode(buf, len, &r), "wire: and the packet is fine again once mended");
    buf[WIRE_HEADER + 4] = 0; /* count 0 */
    CHECK(!Wire_Decode(buf, len, &r), "wire: an input packet with no ticks is dropped");
    buf[WIRE_HEADER + 4] = WIRE_MAX_TICKS + 1;
    CHECK(!Wire_Decode(buf, len, &r), "wire: too many ticks are dropped");
    buf[WIRE_HEADER + 4] = WIRE_MAX_TICKS;
    buf[WIRE_HEADER + 5] = 0x10;
    CHECK(!Wire_Decode(buf, len, &r), "wire: a slot that doesn't exist is dropped");

    w->count = 0;
    CHECK(Wire_Encode(&p, buf, sizeof(buf)) == 0, "wire: nothing is sent for an empty input packet");
    w->count = 3;
    w->in[1][0].buttons = 9;
    CHECK(Wire_Encode(&p, buf, sizeof(buf)) == 0, "wire: or for bad buttons");
    w->in[1][0].buttons = 1;
    CHECK(Wire_Encode(&p, buf, 20) == 0, "wire: or when it doesn't fit");

    /* setups a machine must refuse */
    WireSetup good = NetSetup(5, VENUE_POND), bad;
    CHECK(Wire_SetupValid(&good), "wire: the test setup is valid");
    bad = good; bad.guestMask = 1;
    CHECK(!Wire_SetupValid(&bad), "wire: both machines steering one slot is refused");
    bad = good; bad.setup.slotTeam[2] = 0;
    CHECK(!Wire_SetupValid(&bad), "wire: a playing slot nobody steers is refused");
    bad = good; bad.setup.venue = VENUE_COUNT;
    CHECK(!Wire_SetupValid(&bad), "wire: an unknown venue is refused");
    bad = good; bad.setup.periodSeconds = 5;
    CHECK(!Wire_SetupValid(&bad), "wire: a silly period length is refused");
    bad = good; bad.setup.lean[0].shoot = 90;
    CHECK(!Wire_SetupValid(&bad), "wire: a lean out of range is refused");
    bad = good; bad.setup.build[1][1] = BUILD_COUNT;
    CHECK(!Wire_SetupValid(&bad), "wire: an unknown build is refused");
    bad = good; bad.hostMask = 0;
    CHECK(!Wire_SetupValid(&bad), "wire: a machine with no players is refused");

    CHECK(Wire_TokenFromKey("0123456789abcdef0123456789abcdef") != Wire_TokenFromKey("0123456789abcdef0123456789abcdee") &&
          Wire_TokenFromKey("") != 0, "wire: tokens differ by room and are never 0");

    /* noise never gets through, and never crashes the reader (ASan in CI) */
    int accepted = 0;
    for (int n = 0; n < 20000; n++) {
        uint8_t junk[WIRE_MAX];
        int jl = (int)(Lcg() % WIRE_MAX) + 1;
        for (int k = 0; k < jl; k++) junk[k] = (uint8_t)Lcg();
        if (n % 2) { junk[0] = 'P'; if (jl > 1) junk[1] = 'K'; if (jl > 2) junk[2] = WIRE_VERSION; if (jl > 3) junk[3] = (uint8_t)(1 + Lcg() % 8); }
        if (Wire_Decode(junk, jl, &r)) accepted++;
    }
    /* only tiny fixed-size packets (ping, pong, hello, bye, start) can match by chance */
    CHECK(accepted < 400, "wire: random bytes are almost never a packet");
    /* a real packet with one byte changed either still decodes to something valid or is dropped */
    p.u.input.count = 10;
    len = Wire_Encode(&p, buf, sizeof(buf));
    bool fine = len > 0;
    for (int n = 0; n < 5000 && fine; n++) {
        uint8_t copy[WIRE_MAX];
        memcpy(copy, buf, (size_t)len);
        copy[Lcg() % (uint32_t)len] = (uint8_t)Lcg();
        if (Wire_Decode(copy, len, &r) && r.type == PKT_INPUT)
            for (int k = 0; k < r.u.input.count; k++)
                for (int s = 0; s < MAX_PLAYERS; s++)
                    if (r.u.input.in[k][s].dir > DIR_COUNT || r.u.input.in[k][s].buttons > BTN_ALL) fine = false;
    }
    CHECK(fine, "wire: a damaged packet never carries an impossible press");
}

/* ------------------------------------------------------------ a pretend network */

#define NETQ 8192
typedef struct {
    struct { int at; int len; uint8_t data[WIRE_MAX]; } q[NETQ];
    int count;
} PretendLink;

typedef struct {
    int baseDelay, jitter;   /* frames */
    int lossPct, dupPct;
} LinkShape;

static void LinkSend(PretendLink *l, const LinkShape *shape, int now, const uint8_t *data, int len) {
    int copies = 1;
    if ((int)(Lcg() % 100) < shape->lossPct) copies = 0;
    else if ((int)(Lcg() % 100) < shape->dupPct) copies = 2;
    for (int c = 0; c < copies && l->count < NETQ; c++) {
        int at = now + shape->baseDelay + (shape->jitter > 0 ? (int)(Lcg() % (uint32_t)(shape->jitter + 1)) : 0);
        l->q[l->count].at = at;
        l->q[l->count].len = len;
        memcpy(l->q[l->count].data, data, (size_t)len);
        l->count++;
    }
}

/* Delivers everything due by now (in whatever order it lands: jitter reorders). */
static void LinkDeliver(PretendLink *l, int now, Session *to, int *seen) {
    int keep = 0;
    for (int i = 0; i < l->count; i++) {
        if (l->q[i].at > now) { if (keep != i) l->q[keep] = l->q[i]; keep++; continue; }
        Packet p;
        if (Wire_Decode(l->q[i].data, l->q[i].len, &p) && p.type == PKT_INPUT) { Session_Receive(to, &p.u.input); (*seen)++; }
    }
    l->count = keep;
}

static void SendState(Session *from, PretendLink *l, const LinkShape *shape, int now) {
    Packet p;
    memset(&p, 0, sizeof(p));
    p.type = PKT_INPUT;
    p.token = 1;
    Session_Outgoing(from, &p.u.input);
    uint8_t buf[WIRE_MAX];
    int len = Wire_Encode(&p, buf, sizeof(buf));
    if (len > 0) LinkSend(l, shape, now, buf, len);
}

#define NET_TICKS 1500
typedef struct { PadInput in[NET_TICKS + 64][MAX_PLAYERS]; } PressLog;
static PressLog sPressLog;
static PretendLink sLinkAB, sLinkBA;
static Session sSessA, sSessB;

/* Two machines play NET_TICKS ticks over the link, then everything is
 * delivered cleanly until both have every press. Returns false if the two
 * end states differ from each other or from one machine playing every
 * press in order. */
static bool PlayOnline(uint64_t seed, int venue, LinkShape shape, uint32_t *rollbacks, bool *desync) {
    WireSetup ws = NetSetup(seed, venue);
    Session *a = &sSessA, *b = &sSessB;
    Session_Init(a, &ws.setup, ws.hostMask, ws.guestMask);
    Session_Init(b, &ws.setup, ws.guestMask, ws.hostMask);
    memset(&sPressLog, 0, sizeof(sPressLog));
    sLinkAB.count = sLinkBA.count = 0;
    int seenA = 0, seenB = 0;
    LinkShape clean = {1, 0, 0, 0};
    for (int now = 0; now < 40000; now++) {
        bool cleanNow = a->tick >= NET_TICKS && b->tick >= NET_TICKS;
        const LinkShape *use = cleanNow ? &clean : &shape;
        LinkDeliver(&sLinkAB, now, b, &seenB);
        LinkDeliver(&sLinkBA, now, a, &seenA);
        Session *both[2] = {a, b};
        for (int m = 0; m < 2; m++) {
            Session *s = both[m];
            if (s->tick >= NET_TICKS || !Session_CanStep(s)) continue;
            PadInput mash[MAX_PLAYERS];
            RandomInputs(mash);
            int slot = m == 0 ? 0 : 1;
            /* this machine's press goes to tick localKnown */
            sPressLog.in[s->localKnown][slot] = (PadInput){mash[slot].dir, (uint8_t)(mash[slot].buttons & BTN_ALL)};
            Session_Step(s, mash, NULL, NULL);
        }
        SendState(a, &sLinkAB, use, now);
        SendState(b, &sLinkBA, use, now);
        if (cleanNow) {
            Session_Catchup(a);
            Session_Catchup(b);
            if (Session_Settled(a) && Session_Settled(b) && sLinkAB.count == 0 && sLinkBA.count == 0) break;
        }
    }
    *rollbacks = a->rollbacks + b->rollbacks;
    *desync = a->desync || b->desync;
    if (a->tick != NET_TICKS || b->tick != NET_TICKS || !Session_Settled(a) || !Session_Settled(b)) return false;

    static Game ref;
    Game_Init(&ref, &ws.setup);
    for (int t = 0; t < NET_TICKS; t++) Game_Step(&ref, sPressLog.in[t]);
    uint64_t h = Game_Hash(&ref);
    return Game_Hash(&a->game) == h && Game_Hash(&b->game) == h;
}

static void test_session_online(void) {
    uint32_t rb;
    bool desync;
    LinkShape perfect = {0, 0, 0, 0};
    CHECK(PlayOnline(11, VENUE_POND, perfect, &rb, &desync) && !desync, "online: on a perfect line both games match one machine's");

    LinkShape lan = {1, 1, 0, 0};
    CHECK(PlayOnline(12, VENUE_RINK, lan, &rb, &desync) && !desync, "online: a LAN's lag changes nothing");

    LinkShape far = {5, 3, 0, 0};
    bool ok = PlayOnline(13, VENUE_ARENA, far, &rb, &desync);
    CHECK(ok && !desync && rb > 0, "online: across the country, guesses get put right and the games still match");

    LinkShape rough = {4, 6, 25, 10};
    ok = PlayOnline(14, VENUE_POND, rough, &rb, &desync);
    CHECK(ok && !desync && rb > 0, "online: a quarter of the packets lost, some twice, out of order: still the same game");

    LinkShape awful = {9, 9, 50, 20};
    ok = PlayOnline(15, VENUE_RINK, awful, &rb, &desync);
    CHECK(ok && !desync, "online: half the packets lost: slower, but still the same game");

    /* with no packets at all, a machine stops after SESSION_MAX_AHEAD guesses */
    WireSetup ws = NetSetup(16, VENUE_POND);
    Session *s = &sSessA;
    Session_Init(s, &ws.setup, ws.hostMask, ws.guestMask);
    int steps = 0;
    while (Session_CanStep(s) && steps < 100) { Session_Step(s, kNone, NULL, NULL); steps++; }
    CHECK(steps == SESSION_INPUT_DELAY + SESSION_MAX_AHEAD, "online: with nothing from the other machine, it waits after a few guesses");
    CHECK(!Session_Settled(s), "online: and knows it is not settled");

    /* presses for slots the other machine doesn't steer are ignored */
    Session_Init(s, &ws.setup, ws.hostMask, ws.guestMask);
    WireInput fake;
    memset(&fake, 0, sizeof(fake));
    fake.firstTick = SESSION_INPUT_DELAY;
    fake.count = 5;
    fake.mask = 0x1; /* our own slot */
    fake.hashTick = WIRE_NO_HASH;
    for (int k = 0; k < 5; k++) fake.in[k][0] = (PadInput){3, BTN_SHOOT};
    Session_Receive(s, &fake);
    CHECK(s->remoteKnown == SESSION_INPUT_DELAY, "online: nobody can send presses for this machine's players");

    /* the fingerprints catch two games that have parted */
    Session *b = &sSessB;
    Session_Init(s, &ws.setup, ws.hostMask, ws.guestMask);
    Session_Init(b, &ws.setup, ws.guestMask, ws.hostMask);
    bool caught = false;
    for (int t = 0; t < 400 && !caught; t++) {
        if (t == 30) b->game.score[1] = 5; /* something only one machine believes */
        if (Session_CanStep(s)) Session_Step(s, kNone, NULL, NULL);
        if (Session_CanStep(b)) Session_Step(b, kNone, NULL, NULL);
        WireInput wa, wb;
        Session_Outgoing(s, &wa);
        Session_Outgoing(b, &wb);
        Session_Receive(b, &wa);
        Session_Receive(s, &wb);
        caught = s->desync && b->desync;
    }
    CHECK(caught && !Session_CanStep(s), "online: two games that disagree are caught, and stop");

    /* bursts of presses (a packet every seventh frame) make guesses go wrong */
    Session_Init(s, &ws.setup, ws.hostMask, ws.guestMask);
    Session_Init(b, &ws.setup, ws.guestMask, ws.hostMask);
    for (int t = 0; t < 300; t++) {
        PadInput mash[MAX_PLAYERS];
        RandomInputs(mash);
        if (Session_CanStep(s)) Session_Step(s, mash, NULL, NULL);
        RandomInputs(mash);
        if (Session_CanStep(b)) Session_Step(b, mash, NULL, NULL);
        if (t % 7 == 0) {
            WireInput wa, wb;
            Session_Outgoing(s, &wa);
            Session_Outgoing(b, &wb);
            Session_Receive(b, &wa);
            Session_Receive(s, &wb);
        }
    }
    CHECK(s->rollbacks > 0 && s->resimTicks >= s->rollbacks, "online: bursts of presses make it go back and step again");
}

static int sTickCount;
static void CountTick(const Game *g, void *ctx) { (void)g; (void)ctx; sTickCount++; }

static void test_session_ticks_once(void) {
    WireSetup ws = NetSetup(21, VENUE_POND);
    Session *a = &sSessA, *b = &sSessB;
    Session_Init(a, &ws.setup, ws.hostMask, ws.guestMask);
    Session_Init(b, &ws.setup, ws.guestMask, ws.hostMask);
    sTickCount = 0;
    for (int t = 0; t < 600; t++) {
        PadInput mash[MAX_PLAYERS];
        RandomInputs(mash);
        if (Session_CanStep(a)) Session_Step(a, mash, CountTick, NULL);
        RandomInputs(mash);
        if (Session_CanStep(b)) Session_Step(b, mash, NULL, NULL);
        if (t % 5 == 0) {
            WireInput wa, wb;
            Session_Outgoing(a, &wa);
            Session_Outgoing(b, &wb);
            Session_Receive(b, &wa);
            Session_Receive(a, &wb);
        }
    }
    CHECK(a->rollbacks > 0 && sTickCount == (int)a->tick, "online: the sounds hear every tick once, corrections or not");
}
