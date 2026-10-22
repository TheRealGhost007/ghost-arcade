#define _POSIX_C_SOURCE 200809L
#include "netui.h"
#include "crews.h"
#include "lobby.h"
#include "net.h"
#include "netmsg.h"
#include "relay.h"
#include "session.h"
#include "sounds.h"
#include "teams.h"
#include "wire.h"
#include "sfx.h"
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* How online play finds the other machine:
 *   1. Each machine opens a UDP socket and asks a STUN server what it looks
 *      like from the internet (the address and port its router shows), and
 *      notes its LAN address too.
 *   2. The host asks the arcade's server for a room: it gets a six-letter
 *      code and leaves its two addresses and a random relay key there. The
 *      guest types the code, leaves its addresses and gets the host's.
 *   3. Both send HELLOs at every address of the other's ten times a second;
 *      most home routers let the answers through (hole punching). A HELLO_ACK
 *      proves both directions work.
 *   4. When that fails, both use the relay: the server's broadcast channel
 *      named after the relay key.
 *   5. The host sends the game's setup; the guest starts and acks; then the
 *      two sessions swap presses (session.c) until the final whistle.
 * Timings are in milliseconds. */
#define STUN_WAIT 2500
#define STUN_RETRY 600
#define POLL_EVERY 2000
#define ROOM_LIFE (14 * 60 * 1000)
#define KEEP_MAPPING 15000
#define HELLO_EVERY 100
#define HELLO_IN_GAME 500
#define HOST_RELAY_AFTER 5000
#define GUEST_RELAY_AFTER 3000
#define GUEST_GIVE_UP 25000
#define START_GIVE_UP 15000
#define SETUP_EVERY 200
#define SILENCE_LIMIT 8000
#define PING_EVERY 1000
#define DIRECT_RESEND 50
#define RELAY_SEND_EVERY 50

typedef enum { NU_CLOSED, NU_NOTICE, NU_MENU, NU_HOST_SETUP, NU_JOIN_CODE, NU_CONNECT, NU_ERROR, NU_CREWS, NU_CREW_CODE, NU_IN_GAME } NuScreen;
typedef enum { ST_NONE, ST_ADDRESSES, ST_OPENING, ST_WAITING, ST_JOINING, ST_PUNCHING, ST_STARTING } Stage;
typedef enum { CALL_NONE, CALL_STUN, CALL_OPEN, CALL_POLL, CALL_JOIN, CALL_CREW_SAVE, CALL_CREW_GET, CALL_CREW_DELETE } CallKind;

#define HOST_ROWS 5 /* Where, Play, Rivals, Periods, Computer players */

static struct {
    NuScreen screen, afterNotice;
    int sel;
    char username[64];
    Settings *settings;
    int together;            /* the host's choice: 0 head to head, 1 the same team against the computer */
    int localDevice;

    /* the call to the server: wanted, then started, then answered */
    CallKind call;
    bool callStarted;
    char callFn[32];
    char callBody[1200];

    /* connecting */
    NetRole role;
    Stage stage;
    uint32_t stageAt;
    int fd;
    NetAddr lan, pub;
    NetAddr stunServer[3];
    int stunCount, stunTries;
    bool stunLooked, havePub;
    uint8_t stunTx[STUN_TXID];
    uint32_t stunSentAt, lastKeep;
    char secret[72], relayKey[40], code[8];
    NetAddr peerPub, peerLan, peer;
    bool directOk, relayStarted;
    uint32_t token;
    uint32_t lastHello, lastSetup, lastPoll, lastHeard, lastPing, lastSent;
    WireSetup setup;
    bool haveSetup, startPlay, closeRoom;

    /* playing */
    Session session;
    bool playing;
    float acc;
    int stalled;
    int pingMs;
    uint32_t pingId, pingAt;
    bool peerLeft;
    uint8_t byeReason;

    /* bad news */
    char err[3][120];
    int errCount;
    NuScreen errBack;

    /* typing a code */
    char typed[12];
    int codeLen, gridSel;

    /* the rival crews */
    CrewCard mine;
    char myStyle[96], myCode[16];
    CrewCard friends[CREWS_MAX_FRIENDS];
    char friendStyle[CREWS_MAX_FRIENDS][96];
    int friendCount, removing;
    char crewStatus[96];
    bool crewBusy, crewError;
    bool haveJourneyCrew;
    CrewKid journeyCrew[TEAM_SIZE];
} N = {.fd = -1};

static uint32_t Now(void) { return Net_Millis(); }

/* ------------------------------------------------------------ the server */

static void Want(CallKind kind, const char *fn, const char *body) {
    N.call = kind;
    N.callStarted = false;
    snprintf(N.callFn, sizeof(N.callFn), "%s", fn ? fn : "");
    snprintf(N.callBody, sizeof(N.callBody), "%s", body ? body : "{}");
}

static void Cancel(void) {
    Lobby_Forget();
    N.call = CALL_NONE;
}

static bool Limited(const LobbyReply *r) {
    bool l = false;
    return Json_GetBool(r->body, "limited", &l) && l;
}

/* ------------------------------------------------------------ bad news */

static void TearDown(void) {
    Cancel();
    if (N.closeRoom) {
        char a[64], b[96], body[240];
        Json_Quote(N.code, a, sizeof(a));
        Json_Quote(N.secret, b, sizeof(b));
        snprintf(body, sizeof(body), "{\"p_code\":%s,\"p_secret\":%s}", a, b);
        Lobby_CallDetached("pk_room_close", body);
        N.closeRoom = false;
    }
    if (N.relayStarted) Relay_Stop();
    N.relayStarted = false;
    Net_Close(N.fd);
    N.fd = -1;
    N.stage = ST_NONE;
    N.haveSetup = N.directOk = N.havePub = N.stunLooked = false;
    N.peer = N.peerPub = N.peerLan = (NetAddr){0, 0};
    N.playing = false;
}

static void Fail(const char *a, const char *b, const char *c, NuScreen back) {
    TearDown();
    const char *lines[3] = {a, b, c};
    N.errCount = 0;
    for (int i = 0; i < 3; i++)
        if (lines[i]) snprintf(N.err[N.errCount++], sizeof(N.err[0]), "%s", lines[i]);
    N.errBack = back;
    N.screen = NU_ERROR;
    N.sel = 0;
    Sfx_Play(SFX_FALSE);
}

static void ServerTrouble(const LobbyReply *r, NuScreen back) {
    if (r->missing)
        Fail("The arcade's server isn't ready for this yet.", "Online rooms and crew codes need a one-time update on the server.",
             "Couch play works as it is.", back);
    else if (r->status == 0)
        Fail("Couldn't reach the arcade's server.", "Check the internet connection and try again.", NULL, back);
    else {
        char line[80];
        snprintf(line, sizeof(line), "(It answered with error %ld.)", r->status);
        Fail("The arcade's server couldn't do that.", line, "Try again in a little while.", back);
    }
}

/* ------------------------------------------------------------ packets */

static void SendDirectAll(const uint8_t *buf, int n) {
    if (Net_AddrUsable(N.peer)) {
        Net_Send(N.fd, N.peer, buf, n);
        return;
    }
    if (Net_AddrUsable(N.peerPub)) Net_Send(N.fd, N.peerPub, buf, n);
    if (Net_AddrUsable(N.peerLan) && !Net_AddrEqual(N.peerLan, N.peerPub)) Net_Send(N.fd, N.peerLan, buf, n);
}

/* By the proven direct path; without one, through the relay and at every
 * address we have (a direct path may still work one way). */
static void SendPkt(Packet *p) {
    uint8_t buf[WIRE_MAX];
    p->token = N.token;
    int n = Wire_Encode(p, buf, sizeof(buf));
    if (n <= 0) return;
    if (N.directOk) {
        Net_Send(N.fd, N.peer, buf, n);
        return;
    }
    if (N.relayStarted && Relay_State() == RELAY_READY) Relay_Send(buf, n);
    SendDirectAll(buf, n);
}

static void SendHello(void) {
    Packet p;
    memset(&p, 0, sizeof(p));
    p.type = PKT_HELLO;
    p.token = N.token;
    p.u.hello.role = (uint8_t)N.role;
    uint8_t buf[WIRE_MAX];
    int n = Wire_Encode(&p, buf, sizeof(buf));
    if (n > 0) SendDirectAll(buf, n);
}

static void SendInput(bool stepped) {
    uint32_t now = Now();
    if (N.directOk) {
        if (!stepped && now - N.lastSent < DIRECT_RESEND) return;
    } else if (now - N.lastSent < RELAY_SEND_EVERY) {
        return; /* the relay is metered: twenty a second, each carrying every unacked tick */
    }
    Packet p;
    memset(&p, 0, sizeof(p));
    p.type = PKT_INPUT;
    Session_Outgoing(&N.session, &p.u.input);
    SendPkt(&p);
    N.lastSent = now;
}

static void SendBye(uint8_t reason) {
    Packet p;
    memset(&p, 0, sizeof(p));
    p.type = PKT_BYE;
    p.u.bye.reason = reason;
    for (int k = 0; k < 3; k++) SendPkt(&p);
}

static void StartGame(void) {
    uint8_t mine = N.role == ROLE_HOST ? N.setup.hostMask : N.setup.guestMask;
    uint8_t theirs = N.role == ROLE_HOST ? N.setup.guestMask : N.setup.hostMask;
    Session_Init(&N.session, &N.setup.setup, mine, theirs);
    N.stage = ST_NONE;
    N.playing = true;
    N.screen = NU_IN_GAME;
    N.startPlay = true;
    N.acc = 0;
    N.stalled = 0;
    N.pingMs = -1;
    N.lastHeard = N.lastPing = N.lastSent = Now();
    N.peerLeft = false;
}

static void Handle(const Packet *p, NetAddr from, bool viaRelay) {
    N.lastHeard = Now();
    switch (p->type) {
        case PKT_HELLO: {
            if (p->u.hello.role == N.role) break;
            Packet a;
            memset(&a, 0, sizeof(a));
            a.type = PKT_HELLO_ACK;
            a.token = N.token;
            a.u.hello.role = (uint8_t)N.role;
            uint8_t buf[WIRE_MAX];
            int n = Wire_Encode(&a, buf, sizeof(buf));
            if (n <= 0) break;
            if (viaRelay) Relay_Send(buf, n);
            else Net_Send(N.fd, from, buf, n); /* answer where it came from: that's the path that works */
            break;
        }
        case PKT_HELLO_ACK:
            if (viaRelay || p->u.hello.role == N.role) break;
            if (!N.directOk) {
                N.directOk = true;
                N.peer = from;
            }
            break;
        case PKT_SETUP:
            if (N.role != ROLE_GUEST) break;
            if (!N.haveSetup) {
                if (N.stage != ST_PUNCHING) break;
                N.setup = p->u.setup;
                N.haveSetup = true;
                if (!viaRelay && !N.directOk && !Net_AddrUsable(N.peer)) N.peer = from;
                StartGame();
            } else if (Wire_SetupCheck(&p->u.setup) != Wire_SetupCheck(&N.setup)) {
                break;
            }
            {
                Packet a;
                memset(&a, 0, sizeof(a));
                a.type = PKT_START_ACK;
                a.u.startAck.setupCheck = Wire_SetupCheck(&N.setup);
                SendPkt(&a);
            }
            break;
        case PKT_START_ACK:
            if (N.role == ROLE_HOST && N.stage == ST_STARTING && p->u.startAck.setupCheck == Wire_SetupCheck(&N.setup)) StartGame();
            break;
        case PKT_INPUT:
            if (N.role == ROLE_HOST && N.stage == ST_STARTING) StartGame(); /* the ack got lost, the presses didn't */
            if (N.playing) Session_Receive(&N.session, &p->u.input);
            break;
        case PKT_PING: {
            Packet a = *p;
            a.type = PKT_PONG;
            SendPkt(&a);
            break;
        }
        case PKT_PONG:
            if (p->u.ping.id == N.pingId) {
                int rtt = (int)(Now() - p->u.ping.ms);
                if (rtt >= 0 && rtt < 10000) N.pingMs = N.pingMs < 0 ? rtt : (N.pingMs * 3 + rtt) / 4;
            }
            break;
        case PKT_BYE:
            N.peerLeft = true;
            N.byeReason = p->u.bye.reason;
            break;
        default: break;
    }
}

/* Everything waiting: STUN answers, the other machine's packets (any
 * address, as long as the room's token is right), the relay's. */
static void Pump(void) {
    uint8_t buf[WIRE_MAX + 64];
    NetAddr from;
    int n;
    while (N.fd >= 0 && (n = Net_Recv(N.fd, &from, buf, sizeof(buf))) > 0) {
        if (Stun_IsMessage(buf, n)) {
            NetAddr mapped;
            if (Stun_ParseResponse(buf, n, N.stunTx, &mapped)) {
                N.pub = mapped;
                N.havePub = true;
            }
            continue;
        }
        Packet p;
        if (N.token == 0 || !Wire_Decode(buf, n, &p) || p.token != N.token) continue;
        Handle(&p, from, false);
    }
    if (N.relayStarted)
        while ((n = Relay_Recv(buf, sizeof(buf))) > 0) {
            Packet p;
            if (!Wire_Decode(buf, n, &p) || p.token != N.token) continue;
            Handle(&p, (NetAddr){0, 0}, true);
        }
}

static void SendStun(void) {
    uint8_t req[32];
    int n = Stun_Request(req, sizeof(req), N.stunTx);
    for (int k = 0; k < N.stunCount; k++) Net_Send(N.fd, N.stunServer[k], req, n);
    N.stunSentAt = Now();
    N.stunTries++;
}

static void StartRelay(void) {
    if (N.relayStarted) return;
    N.relayStarted = Relay_Start(N.relayKey);
}

/* ------------------------------------------------------------ the steps */

static void SetStage(Stage s) {
    N.stage = s;
    N.stageAt = Now();
}

static void BeginAddresses(NetRole role) {
    TearDown();
    N.role = role;
    N.token = 0;
    N.fd = Net_Open();
    N.screen = NU_CONNECT;
    N.sel = 0;
    if (N.fd < 0) {
        Fail("Couldn't open a network connection on this computer.", NULL, NULL, NU_MENU);
        return;
    }
    uint32_t ip;
    N.lan = Net_LanIp(&ip) ? (NetAddr){ip, Net_LocalPort(N.fd)} : (NetAddr){0, 0};
    N.stunCount = N.stunTries = 0;
    if (!Net_RandomBytes(N.stunTx, STUN_TXID)) memset(N.stunTx, 7, STUN_TXID);
    Want(CALL_STUN, NULL, NULL);
    SetStage(ST_ADDRESSES);
}

static void AddrJson(NetAddr a, char *out, size_t size) {
    char text[32] = "";
    if (Net_AddrUsable(a)) Net_AddrString(a, text, sizeof(text));
    Json_Quote(text, out, size);
}

static void BeginOpen(void) {
    if (!Net_RandomHex(N.secret, sizeof(N.secret), 32) || !Net_RandomHex(N.relayKey, sizeof(N.relayKey), 16)) {
        Fail("Couldn't make the room's keys on this computer.", NULL, NULL, NU_MENU);
        return;
    }
    N.token = Wire_TokenFromKey(N.relayKey);
    char s[96], pub[48], lan[48], relay[64], body[400];
    Json_Quote(N.secret, s, sizeof(s));
    AddrJson(N.pub, pub, sizeof(pub));
    AddrJson(N.lan, lan, sizeof(lan));
    Json_Quote(N.relayKey, relay, sizeof(relay));
    snprintf(body, sizeof(body), "{\"p_secret\":%s,\"p_addr\":%s,\"p_lan\":%s,\"p_relay\":%s}", s, pub, lan, relay);
    Want(CALL_OPEN, "pk_room_open", body);
    SetStage(ST_OPENING);
}

static void BeginJoin(void) {
    char c[32], pub[48], lan[48], body[300];
    Json_Quote(N.code, c, sizeof(c));
    AddrJson(N.pub, pub, sizeof(pub));
    AddrJson(N.lan, lan, sizeof(lan));
    snprintf(body, sizeof(body), "{\"p_code\":%s,\"p_addr\":%s,\"p_lan\":%s}", c, pub, lan);
    Want(CALL_JOIN, "pk_room_join", body);
    SetStage(ST_JOINING);
}

static void BeginPunch(void) {
    N.directOk = false;
    N.peer = (NetAddr){0, 0};
    N.lastHello = 0;
    N.lastHeard = Now();
    SetStage(ST_PUNCHING);
}

static void BuildSetup(void) {
    WireSetup *w = &N.setup;
    memset(w, 0, sizeof(*w));
    uint64_t seed = 0;
    if (!Net_RandomBytes(&seed, sizeof(seed))) seed = (uint64_t)Now() * 2654435761u;
    Game_DefaultSetup(&w->setup, seed);
    const Settings *st = N.settings;
    w->setup.venue = (uint8_t)st->venue;
    w->setup.periodSeconds = st->periodMinutes * 60;
    w->setup.teamLevel[0] = w->setup.teamLevel[1] = (uint8_t)st->difficulty;
    for (int p = 0; p < MAX_PLAYERS; p++) w->setup.slotTeam[p] = -1;
    w->setup.slotTeam[0] = 0;
    w->setup.slotTeam[1] = N.together ? 0 : 1;
    const TeamStyle *r = Team_Rival(st->rivals);
    if (N.together) { /* the computer's team plays with its signature moves */
        for (int k = 0; k < TEAM_SIZE; k++) w->setup.trait[1][k] = r->traits[k];
        w->setup.goalieStyle[1] = r->goalieStyle;
    } else { /* head to head: the same kids' skills on both sides, only the colours differ */
        w->setup.goalieStyle[1] = w->setup.goalieStyle[0];
    }
    w->rivals = (uint8_t)(st->rivals % RIVAL_COUNT);
    w->hostMask = 1;
    w->guestMask = 2;
}

static void BeginStarting(void) {
    BuildSetup();
    N.haveSetup = true;
    N.lastSetup = 0;
    SetStage(ST_STARTING);
}

/* An answer from the server. */
static void OnReply(CallKind kind, const LobbyReply *r) {
    char text[128];
    switch (kind) {
        case CALL_STUN:
            N.stunLooked = true;
            N.stunCount = r->stunCount;
            for (int k = 0; k < r->stunCount && k < 3; k++) N.stunServer[k] = r->stun[k];
            if (N.stunCount > 0) SendStun();
            break;
        case CALL_OPEN:
            if (!r->ok) { ServerTrouble(r, NU_MENU); break; }
            if (Limited(r)) { Fail("Lots of rooms have been opened from this connection.", "Try again in an hour.", NULL, NU_MENU); break; }
            if (!Json_GetString(r->body, "code", text, sizeof(text)) || strlen(text) != 6 || strspn(text, CODE_ALPHABET) != 6) {
                Fail("The arcade's server couldn't open a room.", "Try again in a little while.", NULL, NU_MENU);
                break;
            }
            snprintf(N.code, sizeof(N.code), "%.6s", text);
            N.closeRoom = true;
            N.lastPoll = Now();
            N.lastKeep = Now();
            SetStage(ST_WAITING);
            Sfx_Play(SFX_CONFIRM);
            break;
        case CALL_POLL: {
            if (!r->ok) {
                if (r->missing) ServerTrouble(r, NU_MENU);
                break; /* a hiccup: the next poll tries again */
            }
            bool waiting = false;
            if (Json_GetBool(r->body, "waiting", &waiting) && waiting) break;
            if (Json_GetString(r->body, "error", text, sizeof(text))) {
                N.closeRoom = false;
                Fail("The room has closed.", "Rooms last fifteen minutes. Open a new one to try again.", NULL, NU_MENU);
                break;
            }
            char guest[48] = "", guestLan[48] = "";
            Json_GetString(r->body, "guest_addr", guest, sizeof(guest));
            Json_GetString(r->body, "guest_lan", guestLan, sizeof(guestLan));
            N.peerPub = N.peerLan = (NetAddr){0, 0};
            Net_ParseAddr(guest, &N.peerPub);
            Net_ParseAddr(guestLan, &N.peerLan);
            if (!Net_AddrUsable(N.peerPub) && !Net_AddrUsable(N.peerLan)) break;
            {
                /* the room did its job: free the code */
                char a[32], b[96], body[200];
                Json_Quote(N.code, a, sizeof(a));
                Json_Quote(N.secret, b, sizeof(b));
                snprintf(body, sizeof(body), "{\"p_code\":%s,\"p_secret\":%s}", a, b);
                Lobby_CallDetached("pk_room_close", body);
                N.closeRoom = false;
            }
            Sfx_Play(SFX_CONFIRM);
            BeginPunch();
            break;
        }
        case CALL_JOIN: {
            if (!r->ok) { ServerTrouble(r, NU_JOIN_CODE); break; }
            if (Limited(r)) { Fail("Lots of tries from this connection.", "Try again in an hour.", NULL, NU_MENU); break; }
            if (Json_GetString(r->body, "error", text, sizeof(text))) {
                if (!strcmp(text, "room full")) Fail("Someone has already joined that room.", "Ask your friend to open a new one.", NULL, NU_MENU);
                else Fail("No room has that code.", "Check it with your friend. Rooms last fifteen minutes.", NULL, NU_JOIN_CODE);
                break;
            }
            char host[48] = "", hostLan[48] = "", relay[48] = "";
            Json_GetString(r->body, "host_addr", host, sizeof(host));
            Json_GetString(r->body, "host_lan", hostLan, sizeof(hostLan));
            Json_GetString(r->body, "relay", relay, sizeof(relay));
            N.peerPub = N.peerLan = (NetAddr){0, 0};
            Net_ParseAddr(host, &N.peerPub);
            Net_ParseAddr(hostLan, &N.peerLan);
            if (strlen(relay) != 32 || strspn(relay, "0123456789abcdef") != 32) {
                Fail("The room's details didn't make sense.", "Ask your friend to open a new room.", NULL, NU_MENU);
                break;
            }
            snprintf(N.relayKey, sizeof(N.relayKey), "%.32s", relay);
            N.token = Wire_TokenFromKey(N.relayKey);
            Sfx_Play(SFX_CONFIRM);
            BeginPunch();
            break;
        }
        case CALL_CREW_SAVE:
        case CALL_CREW_GET:
        case CALL_CREW_DELETE: {
            N.crewBusy = false;
            N.crewError = true;
            if (!r->ok) {
                snprintf(N.crewStatus, sizeof(N.crewStatus), "%s", r->missing ? "The server isn't ready for crew codes yet."
                                                                 : r->status == 0 ? "Couldn't reach the arcade's server."
                                                                                  : "The server couldn't do that.");
                break;
            }
            if (Limited(r)) { snprintf(N.crewStatus, sizeof(N.crewStatus), "Lots of tries this hour: try again later."); break; }
            if (kind == CALL_CREW_SAVE) {
                if (!Json_GetString(r->body, "code", text, sizeof(text)) || !Crews_ValidCode(text, CREW_CODE_LEN)) {
                    snprintf(N.crewStatus, sizeof(N.crewStatus), "The server couldn't keep your crew.");
                    break;
                }
                snprintf(N.myCode, sizeof(N.myCode), "%.8s", text);
                Crews_SaveMyCode(N.myCode);
                snprintf(N.crewStatus, sizeof(N.crewStatus), "Shared. Give your friends the code %s.", N.myCode);
                N.crewError = false;
                Sfx_Play(SFX_CONFIRM);
            } else if (kind == CALL_CREW_DELETE) {
                N.myCode[0] = '\0';
                Crews_SaveMyCode("");
                snprintf(N.crewStatus, sizeof(N.crewStatus), "Your crew is off the server.");
                N.crewError = false;
            } else {
                char name[64], crew[CREW_TEXT_MAX + 8];
                if (Json_GetString(r->body, "error", text, sizeof(text))) {
                    snprintf(N.crewStatus, sizeof(N.crewStatus), "No crew has that code.");
                    break;
                }
                CrewCard c;
                if (!Json_GetString(r->body, "name", name, sizeof(name)) || !Json_GetString(r->body, "crew", crew, sizeof(crew)) ||
                    !Crews_Decode(name, crew, &c)) {
                    snprintf(N.crewStatus, sizeof(N.crewStatus), "That crew didn't make sense.");
                    break;
                }
                snprintf(c.code, sizeof(c.code), "%s", N.typed);
                if (N.friendCount < CREWS_MAX_FRIENDS) {
                    N.friends[N.friendCount] = c;
                    Crews_Describe(&c.lean, N.friendStyle[N.friendCount], sizeof(N.friendStyle[0]));
                    N.friendCount++;
                    Crews_SaveFriends(N.friends, N.friendCount);
                }
                snprintf(N.crewStatus, sizeof(N.crewStatus), "Added %s.", c.name);
                N.crewError = false;
                Sfx_Play(SFX_CONFIRM);
            }
            break;
        }
        default: break;
    }
}

static void PumpCall(void) {
    if (N.call == CALL_NONE) return;
    if (!N.callStarted) {
        bool ok = N.call == CALL_STUN ? Lobby_LookupStun() : Lobby_Call(N.callFn, N.callBody);
        if (ok) N.callStarted = true;
        return;
    }
    static LobbyReply r;
    if (Lobby_Poll(&r) != LOBBY_DONE) return;
    CallKind k = N.call;
    N.call = CALL_NONE;
    OnReply(k, &r);
}

/* One frame of whatever connecting step is running. */
static void Connecting(void) {
    uint32_t now = Now();
    switch (N.stage) {
        case ST_ADDRESSES: {
            if (N.stunCount > 0 && !N.havePub && now - N.stunSentAt >= STUN_RETRY && N.stunTries < 4) SendStun();
            bool lookedAndNothing = N.stunLooked && N.stunCount == 0;
            bool timeUp = N.stunLooked && now - N.stageAt > STUN_WAIT;
            if (!N.havePub && !lookedAndNothing && !timeUp) break;
            if (!N.havePub) {
                if (!Net_AddrUsable(N.lan)) {
                    Fail("This computer doesn't seem to be on a network.", "Check the connection and try again.", NULL, NU_MENU);
                    break;
                }
                N.pub = N.lan; /* no STUN answer: the LAN address will do for a game in the same house */
            }
            if (N.role == ROLE_HOST) BeginOpen();
            else BeginJoin();
            break;
        }
        case ST_WAITING:
            if (N.call == CALL_NONE && now - N.lastPoll >= POLL_EVERY) {
                char a[32], b[96], body[200];
                Json_Quote(N.code, a, sizeof(a));
                Json_Quote(N.secret, b, sizeof(b));
                snprintf(body, sizeof(body), "{\"p_code\":%s,\"p_secret\":%s}", a, b);
                Want(CALL_POLL, "pk_room_poll", body);
                N.lastPoll = now;
            }
            if (now - N.lastKeep >= KEEP_MAPPING && N.stunCount > 0) { /* keep the router's door for this socket open */
                SendStun();
                N.lastKeep = now;
            }
            if (now - N.stageAt > ROOM_LIFE) {
                N.closeRoom = false;
                Fail("Nobody joined in time.", "The room has closed. Open a new one to try again.", NULL, NU_MENU);
            }
            break;
        case ST_PUNCHING:
            if (now - N.lastHello >= HELLO_EVERY) {
                SendHello();
                N.lastHello = now;
            }
            if (N.role == ROLE_HOST) {
                if (N.directOk) BeginStarting();
                else if (now - N.stageAt > HOST_RELAY_AFTER) {
                    StartRelay();
                    RelayState rs = N.relayStarted ? Relay_State() : RELAY_FAILED;
                    if (rs == RELAY_READY) BeginStarting();
                    else if (rs == RELAY_FAILED)
                        Fail("Couldn't reach your friend's computer.", "A direct link didn't work, and neither did the relay.", NULL, NU_MENU);
                }
            } else {
                if (!N.directOk && now - N.stageAt > GUEST_RELAY_AFTER) StartRelay();
                if (now - N.stageAt > GUEST_GIVE_UP)
                    Fail("Couldn't reach your friend's computer.", "Check that both of you are online, then try a new room.", NULL, NU_MENU);
            }
            break;
        case ST_STARTING:
            if (now - N.lastSetup >= SETUP_EVERY) {
                Packet p;
                memset(&p, 0, sizeof(p));
                p.type = PKT_SETUP;
                p.u.setup = N.setup;
                SendPkt(&p);
                N.lastSetup = now;
            }
            if (!N.directOk && now - N.stageAt > 3000) StartRelay();
            if (!N.directOk && now - N.lastHello >= HELLO_EVERY) {
                SendHello();
                N.lastHello = now;
            }
            if (now - N.stageAt > START_GIVE_UP)
                Fail("Your friend's computer stopped answering.", "Try a new room.", NULL, NU_MENU);
            break;
        default: break;
    }
}

/* ------------------------------------------------------------ typing a code */

/* 1: typed in full and sent, -1: backed out, 0: still typing. */
static int CodeEntry(void) {
    const char *alpha = CODE_ALPHABET;
    int n = (int)strlen(alpha), len = (int)strlen(N.typed);
    int ch;
    while ((ch = GetCharPressed()) != 0) {
        if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
        if (ch > 0 && ch < 128 && strchr(alpha, ch) && len < N.codeLen) {
            N.typed[len++] = (char)ch;
            N.typed[len] = '\0';
            Sfx_Play(SFX_SELECT);
        }
    }
    if (IsKeyPressed(KEY_BACKSPACE) && len > 0) {
        N.typed[--len] = '\0';
        Sfx_Play(SFX_BACK);
    }
    if (IsKeyPressed(KEY_ESCAPE)) return -1;
    if ((IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) && len == N.codeLen) {
        N.localDevice = DEV_KEYS_LEFT;
        return 1;
    }
    int dx = (IsKeyPressed(KEY_RIGHT) ? 1 : 0) - (IsKeyPressed(KEY_LEFT) ? 1 : 0);
    int dy = (IsKeyPressed(KEY_DOWN) ? 1 : 0) - (IsKeyPressed(KEY_UP) ? 1 : 0);
    for (int d = DEV_PAD0; d < DEV_COUNT; d++) {
        if (!Input_DeviceReady(d)) continue;
        MenuPress m = Input_Menu(d);
        dx += (m.right ? 1 : 0) - (m.left ? 1 : 0);
        dy += (m.down ? 1 : 0) - (m.up ? 1 : 0);
        if (m.confirm && len < N.codeLen) {
            N.typed[len++] = alpha[N.gridSel];
            N.typed[len] = '\0';
            Sfx_Play(SFX_SELECT);
        }
        if (m.back) {
            if (len == 0) return -1;
            N.typed[--len] = '\0';
            Sfx_Play(SFX_BACK);
        }
        if (m.start && len == N.codeLen) {
            N.localDevice = d;
            return 1;
        }
    }
    if (dx || dy) {
        int cols = CODE_GRID_COLS, rows = n / cols;
        int c = (N.gridSel % cols + dx + cols) % cols, r = (N.gridSel / cols + dy + rows) % rows;
        N.gridSel = r * cols + c;
        Sfx_Play(SFX_SELECT);
    }
    return 0;
}

static void OpenCode(NuScreen screen, int len) {
    N.screen = screen;
    N.typed[0] = '\0';
    N.codeLen = len;
    N.gridSel = 0;
    while (GetCharPressed() != 0) {} /* nothing typed before counts */
}

/* ------------------------------------------------------------ the crews */

static void LoadCrews(void) {
    PlayStyle ps;
    Crews_LoadStyle(&ps);
    memset(&N.mine, 0, sizeof(N.mine));
    char name[80];
    snprintf(name, sizeof(name), "%s's Crew", N.username);
    Crews_CleanName(name, N.mine.name, sizeof(N.mine.name));
    if (!N.mine.name[0]) snprintf(N.mine.name, sizeof(N.mine.name), "The Rink Rats");
    const TeamStyle *crew = Team_Crew();
    for (int k = 0; k < TEAM_SIZE; k++) {
        if (N.haveJourneyCrew) N.mine.kids[k] = N.journeyCrew[k];
        else {
            Journey_CleanName(crew->kids[k], N.mine.kids[k].name, JOURNEY_NAME_LEN);
            N.mine.kids[k].build = (uint8_t)k;
            N.mine.kids[k].trait = TRAIT_NONE;
            N.mine.kids[k].from = -1;
        }
    }
    N.mine.lean = Crews_LeanFrom(&ps);
    if (ps.games < 2) snprintf(N.myStyle, sizeof(N.myStyle), "Play a few games and your crew learns how you play.");
    else Crews_Describe(&N.mine.lean, N.myStyle, sizeof(N.myStyle));
    Crews_LoadMyCode(N.myCode, sizeof(N.myCode));
    N.friendCount = Crews_LoadFriends(N.friends, CREWS_MAX_FRIENDS);
    for (int i = 0; i < N.friendCount; i++) Crews_Describe(&N.friends[i].lean, N.friendStyle[i], sizeof(N.friendStyle[0]));
}

static int CrewItems(const char *items[3]) {
    int n = 0;
    items[n++] = N.myCode[0] ? "Update my crew" : "Share my crew";
    items[n++] = "Add a crew";
    if (N.myCode[0]) items[n++] = "Stop sharing";
    return n;
}

static NetUiStep UpdateCrews(void) {
    const char *items[3];
    int itemCount = CrewItems(items);
    int total = itemCount + N.friendCount + 1;
    MenuPress m = Input_MenuAny();
    if (m.up || m.left) { N.sel = (N.sel + total - 1) % total; N.removing = -1; Sfx_Play(SFX_SELECT); }
    if (m.down || m.right) { N.sel = (N.sel + 1) % total; N.removing = -1; Sfx_Play(SFX_SELECT); }
    if (m.back) {
        if (N.removing >= 0) { N.removing = -1; return NETUI_STAY; }
        Sfx_Play(SFX_BACK);
        Cancel();
        N.screen = NU_CLOSED;
        return NETUI_BACK;
    }
    if (!m.confirm) return NETUI_STAY;
    if (N.sel == total - 1) {
        Sfx_Play(SFX_BACK);
        Cancel();
        N.screen = NU_CLOSED;
        return NETUI_BACK;
    }
    if (N.sel >= itemCount) {
        int f = N.sel - itemCount;
        if (N.removing != f) { N.removing = f; Sfx_Play(SFX_SELECT); return NETUI_STAY; }
        for (int i = f; i < N.friendCount - 1; i++) {
            N.friends[i] = N.friends[i + 1];
            memcpy(N.friendStyle[i], N.friendStyle[i + 1], sizeof(N.friendStyle[0]));
        }
        N.friendCount--;
        Crews_SaveFriends(N.friends, N.friendCount);
        N.removing = -1;
        if (N.sel >= itemCount + N.friendCount + 1) N.sel = itemCount + N.friendCount;
        snprintf(N.crewStatus, sizeof(N.crewStatus), "Removed.");
        N.crewError = false;
        Sfx_Play(SFX_BACK);
        return NETUI_STAY;
    }
    if (N.crewBusy) return NETUI_STAY;
    Sfx_Play(SFX_CONFIRM);
    const char *what = items[N.sel];
    if (!strcmp(what, "Add a crew")) {
        if (N.friendCount >= CREWS_MAX_FRIENDS) {
            snprintf(N.crewStatus, sizeof(N.crewStatus), "That's eight crews: remove one first.");
            N.crewError = true;
            return NETUI_STAY;
        }
        OpenCode(NU_CREW_CODE, CREW_CODE_LEN);
        return NETUI_STAY;
    }
    char secret[80];
    if (!Crews_Secret(secret, sizeof(secret))) {
        snprintf(N.crewStatus, sizeof(N.crewStatus), "Couldn't make this computer's crew key.");
        N.crewError = true;
        return NETUI_STAY;
    }
    char s[96], body[1100];
    Json_Quote(secret, s, sizeof(s));
    if (!strcmp(what, "Stop sharing")) {
        snprintf(body, sizeof(body), "{\"p_secret\":%s}", s);
        Want(CALL_CREW_DELETE, "pk_crew_delete", body);
        snprintf(N.crewStatus, sizeof(N.crewStatus), "Taking your crew off the server...");
    } else {
        char crew[CREW_TEXT_MAX + 8], qn[64], qc[CREW_TEXT_MAX * 2 + 8];
        if (!Crews_Encode(&N.mine, crew, sizeof(crew))) {
            snprintf(N.crewStatus, sizeof(N.crewStatus), "Your crew couldn't be written down.");
            N.crewError = true;
            return NETUI_STAY;
        }
        Json_Quote(N.mine.name, qn, sizeof(qn));
        Json_Quote(crew, qc, sizeof(qc));
        snprintf(body, sizeof(body), "{\"p_secret\":%s,\"p_name\":%s,\"p_crew\":%s}", s, qn, qc);
        Want(CALL_CREW_SAVE, "pk_crew_save", body);
        snprintf(N.crewStatus, sizeof(N.crewStatus), "Sharing your crew...");
    }
    N.crewBusy = true;
    N.crewError = false;
    return NETUI_STAY;
}

/* ------------------------------------------------------------ the screens */

static void Move(int count, const MenuPress *m, bool sideways) {
    bool prev = m->up || (sideways && m->left), next = m->down || (sideways && m->right);
    if (prev) { N.sel = (N.sel + count - 1) % count; Sfx_Play(SFX_SELECT); }
    if (next) { N.sel = (N.sel + 1) % count; Sfx_Play(SFX_SELECT); }
}

static NetUiStep Close(void) {
    TearDown();
    N.screen = NU_CLOSED;
    return NETUI_BACK;
}

void NetUi_Init(const char *username) {
    snprintf(N.username, sizeof(N.username), "%s", username && username[0] ? username : "PLAYER");
    N.fd = -1;
    N.screen = NU_CLOSED;
    N.pingMs = -1;
    N.localDevice = DEV_KEYS_LEFT;
}

void NetUi_Shutdown(void) {
    if (N.playing) SendBye(BYE_LEFT);
    TearDown();
    Lobby_Shutdown();
    NetCurl_Shutdown();
}

static void Open(Settings *s, NuScreen screen) {
    N.settings = s;
    Lobby_Forget(); /* an old answer must not be taken for a new one */
    N.call = CALL_NONE;
    N.sel = 0;
    N.crewStatus[0] = '\0';
    N.crewBusy = N.crewError = false;
    N.removing = -1;
    if (!s->onlineOk) {
        N.afterNotice = screen;
        N.screen = NU_NOTICE;
        return;
    }
    N.screen = screen;
    if (screen == NU_CREWS) LoadCrews();
}

void NetUi_OpenOnline(Settings *s, const JoinSlot slots[MAX_PLAYERS]) {
    (void)slots;
    Open(s, NU_MENU);
}

void NetUi_OpenCrews(Settings *s, const Journey *j) {
    N.haveJourneyCrew = j != NULL;
    if (j)
        for (int k = 0; k < TEAM_SIZE; k++) N.journeyCrew[k] = j->crew[k];
    Open(s, NU_CREWS);
}

NetUiStep NetUi_Update(Settings *s) {
    N.settings = s;
    Pump();
    PumpCall();
    if (N.stage != ST_NONE) Connecting();
    if (N.startPlay) {
        N.startPlay = false;
        return NETUI_PLAY;
    }
    MenuPress m;
    switch (N.screen) {
        case NU_NOTICE:
            m = Input_MenuAny();
            Move(2, &m, true);
            if (m.back || (m.confirm && N.sel == 1)) { Sfx_Play(SFX_BACK); return Close(); }
            if (m.confirm) {
                Sfx_Play(SFX_CONFIRM);
                s->onlineOk = true;
                Settings_Save(s);
                N.sel = 0;
                N.screen = N.afterNotice;
                if (N.screen == NU_CREWS) LoadCrews();
            }
            return NETUI_STAY;
        case NU_MENU:
            m = Input_MenuAny();
            Move(3, &m, true);
            if (m.back || (m.confirm && N.sel == 2)) { Sfx_Play(SFX_BACK); return Close(); }
            if (m.confirm) {
                Sfx_Play(SFX_CONFIRM);
                if (N.sel == 0) { N.screen = NU_HOST_SETUP; N.sel = HOST_ROWS; }
                else OpenCode(NU_JOIN_CODE, 6);
            }
            return NETUI_STAY;
        case NU_HOST_SETUP: {
            m = Input_MenuAny();
            int count = HOST_ROWS + 2;
            if (m.up) { N.sel = (N.sel + count - 1) % count; Sfx_Play(SFX_SELECT); }
            if (m.down) { N.sel = (N.sel + 1) % count; Sfx_Play(SFX_SELECT); }
            int d = (m.right ? 1 : 0) - (m.left ? 1 : 0);
            if (d && N.sel < HOST_ROWS) {
                Sfx_Play(SFX_SELECT);
                switch (N.sel) {
                    case 0: s->venue = (s->venue + VENUE_COUNT + d) % VENUE_COUNT; break;
                    case 1: N.together = !N.together; break;
                    case 2: s->rivals = (s->rivals + RIVAL_COUNT + d) % RIVAL_COUNT; break;
                    case 3: s->periodMinutes = 1 + (s->periodMinutes - 1 + 3 + d) % 3; break;
                    default: s->difficulty = (s->difficulty + DIFF_COUNT + d) % DIFF_COUNT; break;
                }
            } else if (d && N.sel >= HOST_ROWS) {
                N.sel = N.sel == HOST_ROWS ? HOST_ROWS + 1 : HOST_ROWS;
                Sfx_Play(SFX_SELECT);
            }
            if (m.back || (m.confirm && N.sel == HOST_ROWS + 1)) { Sfx_Play(SFX_BACK); N.screen = NU_MENU; N.sel = 0; return NETUI_STAY; }
            if ((m.confirm || m.start) && (N.sel == HOST_ROWS || m.start)) {
                Sfx_Play(SFX_CONFIRM);
                Settings_Save(s);
                N.localDevice = Input_MenuDevice();
                BeginAddresses(ROLE_HOST);
            }
            return NETUI_STAY;
        }
        case NU_JOIN_CODE: {
            int r = CodeEntry();
            if (r < 0) { Sfx_Play(SFX_BACK); N.screen = NU_MENU; N.sel = 1; }
            else if (r > 0) {
                Sfx_Play(SFX_CONFIRM);
                snprintf(N.code, sizeof(N.code), "%.6s", N.typed);
                BeginAddresses(ROLE_GUEST);
            }
            return NETUI_STAY;
        }
        case NU_CONNECT:
            m = Input_MenuAny();
            if (m.back || m.confirm) {
                Sfx_Play(SFX_BACK);
                TearDown();
                N.screen = NU_MENU;
                N.sel = 0;
            }
            return NETUI_STAY;
        case NU_ERROR:
            m = Input_MenuAny();
            if (m.back || m.confirm || m.start) {
                Sfx_Play(SFX_BACK);
                N.sel = 0;
                if (N.errBack == NU_CLOSED) return Close();
                if (N.errBack == NU_JOIN_CODE) OpenCode(NU_JOIN_CODE, 6);
                else N.screen = N.errBack;
                if (N.screen == NU_CREWS) LoadCrews();
            }
            return NETUI_STAY;
        case NU_CREWS: return UpdateCrews();
        case NU_CREW_CODE: {
            int r = CodeEntry();
            if (r < 0) { Sfx_Play(SFX_BACK); N.screen = NU_CREWS; return NETUI_STAY; }
            if (r > 0) {
                N.screen = NU_CREWS;
                if (!strcmp(N.typed, N.myCode)) {
                    snprintf(N.crewStatus, sizeof(N.crewStatus), "That's your own crew's code.");
                    N.crewError = true;
                    return NETUI_STAY;
                }
                for (int i = 0; i < N.friendCount; i++)
                    if (!strcmp(N.friends[i].code, N.typed)) {
                        snprintf(N.crewStatus, sizeof(N.crewStatus), "You have that crew already.");
                        N.crewError = true;
                        return NETUI_STAY;
                    }
                char c[32], body[64];
                Json_Quote(N.typed, c, sizeof(c));
                snprintf(body, sizeof(body), "{\"p_code\":%s}", c);
                Want(CALL_CREW_GET, "pk_crew_get", body);
                snprintf(N.crewStatus, sizeof(N.crewStatus), "Looking up %s...", N.typed);
                N.crewBusy = true;
                N.crewError = false;
                Sfx_Play(SFX_CONFIRM);
            }
            return NETUI_STAY;
        }
        case NU_IN_GAME: return NETUI_STAY;
        default: return Close();
    }
}

/* ------------------------------------------------------------ drawing */

static const char *VenueWords(int v) {
    static const char *const kWords[VENUE_COUNT] = {"The pond", "The town rink", "The arena"};
    return kWords[(v % VENUE_COUNT + VENUE_COUNT) % VENUE_COUNT];
}

void NetUi_Draw(void) {
    NetView v;
    memset(&v, 0, sizeof(v));
    v.backdrop = -1;
    v.sel = N.sel;
    static char status[96], line[160], rv[HOST_ROWS][48];
    switch (N.screen) {
        case NU_NOTICE:
            v.title = "BEFORE YOU GO ONLINE";
            v.lines[0] = "Online play links your computer straight to your friend's.";
            v.lines[1] = "Each computer sees the other's internet address.";
            v.lines[2] = "The arcade's server keeps the room code and both addresses for up to fifteen minutes, to introduce the two.";
            v.lines[3] = "Rival crews keeps your crew's names and play style on the server, under a code.";
            v.lines[4] = "Only share codes with friends.";
            v.lineCount = 5;
            v.items[0] = "Go online";
            v.items[1] = "Back";
            v.itemCount = 2;
            v.hint = "Left/Right choose   A/Enter pick   B/Esc back";
            break;
        case NU_MENU:
            v.title = "ONLINE";
            v.lines[0] = "Play a friend on their own computer.";
            v.lines[1] = "Host: open a room and tell your friend its code.";
            v.lines[2] = "Join: type the code your friend gives you.";
            v.lineCount = 3;
            v.items[0] = "Host";
            v.items[1] = "Join";
            v.items[2] = "Back";
            v.itemCount = 3;
            v.hint = "Left/Right choose   A/Enter pick   B/Esc back";
            break;
        case NU_HOST_SETUP: {
            const Settings *s = N.settings;
            v.title = "HOST A GAME";
            v.backdrop = s ? s->venue : VENUE_POND;
            snprintf(line, sizeof(line), N.together ? "You and your friend on the Rink Rats, against the %s" : "You on the Rink Rats, your friend on the %s",
                     Team_Rival(s ? s->rivals : 0)->name);
            v.lines[0] = line;
            v.lineCount = 1;
            v.rowLabel[0] = "Where";
            snprintf(rv[0], sizeof(rv[0]), "%s", VenueWords(s ? s->venue : 0));
            v.rowLabel[1] = "Play";
            snprintf(rv[1], sizeof(rv[1]), "%s", N.together ? "Same team" : "Head to head");
            v.rowLabel[2] = N.together ? "Rivals" : "Their colours";
            snprintf(rv[2], sizeof(rv[2]), "%s", Team_Rival(s ? s->rivals : 0)->name);
            v.rowLabel[3] = "Periods";
            snprintf(rv[3], sizeof(rv[3]), "%d minute%s x 2", s ? s->periodMinutes : 2, s && s->periodMinutes == 1 ? "" : "s");
            v.rowLabel[4] = "Computer players";
            snprintf(rv[4], sizeof(rv[4]), "%s", s && s->difficulty == DIFF_EASY ? "Easy" : s && s->difficulty == DIFF_HARD ? "Hard" : "Normal");
            for (int r = 0; r < HOST_ROWS; r++) v.rowValue[r] = rv[r];
            v.rowCount = HOST_ROWS;
            v.items[0] = "Open room";
            v.items[1] = "Back";
            v.itemCount = 2;
            v.hint = "Up/Down choose   Left/Right change   A/Enter pick   B/Esc back";
            break;
        }
        case NU_JOIN_CODE:
        case NU_CREW_CODE:
            v.title = N.screen == NU_JOIN_CODE ? "JOIN A GAME" : "ADD A CREW";
            v.lines[0] = N.screen == NU_JOIN_CODE ? "Type the room code your friend gave you." : "Type the crew code your friend gave you.";
            v.lineCount = 1;
            v.code = N.typed;
            v.codeLen = N.codeLen;
            v.typing = true;
            v.gridSel = N.gridSel;
            v.sel = -1;
            v.hint = "Keyboard: type it, Enter.   Pad: pick letters with A, B deletes, Start sends.";
            break;
        case NU_CONNECT: {
            v.title = N.role == ROLE_HOST ? "HOST A GAME" : "JOIN A GAME";
            v.items[0] = "Cancel";
            v.itemCount = 1;
            v.sel = 0;
            v.busy = true;
            v.status = status;
            const char *what = "Connecting...";
            switch (N.stage) {
                case ST_ADDRESSES: what = "Finding this computer's address..."; break;
                case ST_OPENING: what = "Opening a room..."; break;
                case ST_WAITING: what = "Waiting for your friend to join..."; break;
                case ST_JOINING: what = "Looking for the room..."; break;
                case ST_PUNCHING: what = N.relayStarted ? "Trying the relay..." : "Reaching your friend's computer..."; break;
                case ST_STARTING: what = "Starting the game..."; break;
                default: break;
            }
            snprintf(status, sizeof(status), "%s", what);
            if (N.stage == ST_WAITING) {
                v.lines[0] = "ROOM CODE";
                v.lines[1] = "Tell your friend this code. They pick Online, then Join.";
                v.lineCount = 2;
                v.code = N.code;
                v.codeLen = 6;
            } else {
                v.lines[0] = N.role == ROLE_HOST ? "Your friend will play on their own computer." : "Joining your friend's game.";
                v.lineCount = 1;
            }
            v.hint = "A/Enter or B/Esc cancel";
            break;
        }
        case NU_ERROR:
            v.title = "ONLINE";
            v.error = true;
            for (int i = 0; i < N.errCount; i++) v.lines[i] = N.err[i];
            v.lineCount = N.errCount;
            v.items[0] = "OK";
            v.itemCount = 1;
            v.sel = 0;
            v.hint = "A/Enter OK";
            break;
        case NU_CREWS: {
            CrewsView c;
            memset(&c, 0, sizeof(c));
            c.mine = &N.mine;
            c.myStyle = N.myStyle;
            c.myCode = N.myCode;
            c.friends = N.friends;
            c.friendCount = N.friendCount;
            for (int i = 0; i < N.friendCount && i < CREWS_VIEW_MAX; i++) c.friendStyle[i] = N.friendStyle[i];
            c.itemCount = CrewItems(c.items);
            c.sel = N.sel;
            c.removing = N.removing;
            c.status = N.crewStatus;
            c.busy = N.crewBusy;
            c.error = N.crewError;
            Screen_Crews(&c);
            return;
        }
        default: return;
    }
    Screen_Net(&v);
}

/* ------------------------------------------------------------ playing */

bool NetUi_StepGame(const PadInput local[MAX_PLAYERS], float dt, void (*onTick)(const Game *g, void *ctx), void *ctx) {
    if (!N.playing) return false;
    Pump();
    PumpCall();
    uint32_t now = Now();
    if (N.session.desync) {
        SendBye(BYE_DESYNC);
        Fail("The two computers' games stopped matching.", "That's a bug in Pond Kings, sorry: the game has to stop.", NULL, NU_MENU);
        return false;
    }
    if (N.peerLeft) {
        if (N.session.game.phase == PH_OVER) return true; /* after the final whistle: the result stands */
        if (N.byeReason == BYE_DESYNC)
            Fail("The two computers' games stopped matching.", "That's a bug in Pond Kings, sorry: the game has to stop.", NULL, NU_MENU);
        else
            Fail("Your friend left the game.", NULL, NULL, NU_MENU);
        return false;
    }
    if (now - N.lastHeard > SILENCE_LIMIT) {
        if (N.session.game.phase == PH_OVER && Session_Settled(&N.session)) return true;
        Fail("Lost the connection to your friend.", "Nothing has come from their computer for a while.", NULL, NU_MENU);
        return false;
    }
    if (!N.directOk && N.relayStarted && Relay_State() == RELAY_FAILED && now - N.lastHeard > 2000) {
        Fail("The relay dropped out.", "Try a new room.", NULL, NU_MENU);
        return false;
    }

    Session_Catchup(&N.session);
    /* keep pace with the other machine: whoever is ahead eases off a little */
    int oneWay = N.pingMs > 0 ? (N.pingMs * TICK_HZ / 2000) : 0;
    int adv = Session_Advantage(&N.session, oneWay);
    float tick = 1.0f / TICK_HZ;
    if (adv > 1) tick *= 1.0f + 0.025f * (float)(adv - 1 > 4 ? 4 : adv - 1);
    N.acc += dt > 0.25f ? 0.25f : dt;
    int steps = 0;
    bool waiting = false;
    while (N.acc >= tick && steps < 8) {
        if (!Session_CanStep(&N.session)) {
            waiting = true;
            if (N.acc > tick) N.acc = tick;
            break;
        }
        Session_Step(&N.session, local, onTick, ctx);
        N.acc -= tick;
        steps++;
        SendInput(true);
    }
    N.stalled = waiting ? N.stalled + 1 : 0;
    SendInput(false);
    if (now - N.lastPing >= PING_EVERY) {
        Packet p;
        memset(&p, 0, sizeof(p));
        p.type = PKT_PING;
        p.u.ping.id = ++N.pingId;
        p.u.ping.ms = now;
        SendPkt(&p);
        N.lastPing = now;
    }
    if (!N.directOk && now - N.lastHello >= HELLO_IN_GAME) { /* a direct path may still open up */
        SendHello();
        N.lastHello = now;
    }
    return true;
}

const Game *NetUi_Game(void) { return &N.session.game; }
int NetUi_PingMs(void) { return N.playing ? N.pingMs : -1; }
bool NetUi_Waiting(void) { return N.playing && N.stalled > 10; }

int NetUi_SlotFor(int localIndex) {
    if (localIndex != 0) return -1;
    uint8_t mask = N.role == ROLE_HOST ? N.setup.hostMask : N.setup.guestMask;
    for (int p = 0; p < MAX_PLAYERS; p++)
        if (mask & (1u << p)) return p;
    return -1;
}

int NetUi_Rivals(void) { return N.setup.rivals % RIVAL_COUNT; }
int NetUi_LocalDevice(void) { return N.localDevice; }

bool NetUi_Settled(void) {
    if (!N.playing) return true;
    return Session_Settled(&N.session) || (N.peerLeft && N.session.game.phase == PH_OVER);
}

void NetUi_Leave(void) {
    if (N.playing) SendBye(BYE_LEFT);
    TearDown();
    N.screen = NU_CLOSED;
}

int NetUi_FriendCrews(CrewCard *out, int max) { return Crews_LoadFriends(out, max); }

/* ------------------------------------------------------------ pictures */

static void PreviewCrew(CrewCard *c, const char *name, const char *a, const char *b, const char *d, AiLean lean) {
    memset(c, 0, sizeof(*c));
    snprintf(c->name, sizeof(c->name), "%s", name);
    const char *kids[TEAM_SIZE] = {a, b, d};
    for (int k = 0; k < TEAM_SIZE; k++) {
        snprintf(c->kids[k].name, sizeof(c->kids[k].name), "%s", kids[k]);
        c->kids[k].build = (uint8_t)k;
        c->kids[k].trait = (uint8_t)(k == 1 ? TRAIT_SNIPER : k == 2 ? TRAIT_WALL : TRAIT_NONE);
    }
    c->lean = lean;
}

void NetUi_Preview(int which, Settings *s) {
    N.settings = s;
    N.sel = 0;
    N.stage = ST_NONE;
    N.relayStarted = false;
    switch (which) {
        case NETUI_PREVIEW_NOTICE: N.screen = NU_NOTICE; break;
        case NETUI_PREVIEW_MENU: N.screen = NU_MENU; break;
        case NETUI_PREVIEW_HOST: N.screen = NU_HOST_SETUP; N.sel = 1; N.together = 0; break;
        case NETUI_PREVIEW_JOIN_CODE:
            N.screen = NU_JOIN_CODE;
            snprintf(N.typed, sizeof(N.typed), "K7Q");
            N.codeLen = 6;
            N.gridSel = 12;
            break;
        case NETUI_PREVIEW_WAITING:
            N.screen = NU_CONNECT;
            N.role = ROLE_HOST;
            N.stage = ST_WAITING;
            snprintf(N.code, sizeof(N.code), "K7QM4X");
            break;
        case NETUI_PREVIEW_CONNECTING:
            N.screen = NU_CONNECT;
            N.role = ROLE_GUEST;
            N.stage = ST_PUNCHING;
            break;
        case NETUI_PREVIEW_ERROR: {
            LobbyReply r;
            memset(&r, 0, sizeof(r));
            r.missing = true;
            ServerTrouble(&r, NU_MENU);
            break;
        }
        case NETUI_PREVIEW_CREWS:
        case NETUI_PREVIEW_CREW_CODE:
            PreviewCrew(&N.mine, "TRG's Crew", "Zippy", "Flurry", "Moose", (AiLean){14, -12, 4, -1});
            Crews_Describe(&N.mine.lean, N.myStyle, sizeof(N.myStyle));
            snprintf(N.myCode, sizeof(N.myCode), "H4RV9PQ2");
            PreviewCrew(&N.friends[0], "Maya's Crew", "Dash", "Jo", "Hank", (AiLean){-10, 22, 18, 0});
            PreviewCrew(&N.friends[1], "The Zamboni Boys", "Pip", "Bea", "Grizz", (AiLean){20, 0, -14, 1});
            N.friendCount = 2;
            for (int i = 0; i < N.friendCount; i++) Crews_Describe(&N.friends[i].lean, N.friendStyle[i], sizeof(N.friendStyle[0]));
            N.removing = -1;
            N.crewBusy = false;
            N.crewError = false;
            snprintf(N.crewStatus, sizeof(N.crewStatus), "Shared. Give your friends the code %s.", N.myCode);
            N.screen = NU_CREWS;
            if (which == NETUI_PREVIEW_CREW_CODE) {
                N.screen = NU_CREW_CODE;
                snprintf(N.typed, sizeof(N.typed), "B7W2");
                N.codeLen = CREW_CODE_LEN;
                N.gridSel = 20;
            }
            break;
        default: N.screen = NU_CLOSED; break;
    }
}
