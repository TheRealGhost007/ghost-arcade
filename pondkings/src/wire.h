#ifndef WIRE_H
#define WIRE_H

#include <stdbool.h>
#include <stdint.h>
#include "game.h"

/* Pond Kings' online packets: a fixed little-endian layout with every field
 * checked on the way in, so a stray or hostile packet is dropped, never
 * believed. Only button presses, tick numbers and state fingerprints ever
 * travel; no names, no files, nothing free-form. No raylib, no sockets.
 *
 * Every packet starts with 'P' 'K', the version, the type and a token both
 * machines worked out from their room's random relay key (packets from any
 * other game are ignored). */

#define WIRE_VERSION 1
#define WIRE_HEADER 8
#define WIRE_MAX 512          /* no packet is longer */
#define WIRE_MAX_TICKS 40     /* ticks of input in one INPUT packet */
#define WIRE_NO_HASH 0xFFFFFFFFu
#define WIRE_MAX_TICK 0x7FFFFFFFu /* far beyond any game (over a year at 60 a second) */

typedef enum {
    PKT_HELLO = 1,  /* "I'm here": sent at every address the other machine might have */
    PKT_HELLO_ACK,  /* "I hear you": both ends now know which address works */
    PKT_SETUP,      /* the host's game: rules, teams, who steers which slot */
    PKT_START_ACK,  /* the guest has the setup and has started */
    PKT_INPUT,      /* button presses for a run of ticks, plus acks and a fingerprint */
    PKT_PING,
    PKT_PONG,
    PKT_BYE,        /* leaving (or something went wrong) */
    PKT_TYPE_END
} PacketType;

typedef enum { BYE_LEFT, BYE_DESYNC, BYE_BUSY, BYE_REASON_END } ByeReason;
typedef enum { ROLE_HOST, ROLE_GUEST } NetRole;

typedef struct {
    GameSetup setup;
    uint8_t rivals;           /* the rival team's colours (looks only) */
    uint8_t hostMask;         /* the slots the host's machine steers */
    uint8_t guestMask;        /* the slots the guest's machine steers */
} WireSetup;

typedef struct {
    uint32_t firstTick;
    uint8_t count;            /* 1..WIRE_MAX_TICKS */
    uint8_t mask;             /* the slots whose presses follow */
    PadInput in[WIRE_MAX_TICKS][MAX_PLAYERS]; /* in[k][slot], slots outside mask unused */
    uint32_t ack;             /* the sender has every one of the receiver's ticks below this */
    uint32_t tick;            /* the sender's own tick, for keeping the two in step */
    uint32_t hashTick;        /* WIRE_NO_HASH, or the tick the fingerprint is of */
    uint64_t hash;
} WireInput;

typedef struct {
    uint8_t type;             /* PacketType */
    uint32_t token;
    union {
        struct { uint8_t role; } hello;            /* HELLO, HELLO_ACK */
        WireSetup setup;                           /* SETUP */
        struct { uint32_t setupCheck; } startAck;  /* START_ACK: Wire_SetupCheck of what it got */
        WireInput input;                           /* INPUT */
        struct { uint32_t id, ms; } ping;          /* PING, PONG (echoed) */
        struct { uint8_t reason; } bye;            /* BYE */
    } u;
} Packet;

/* Bytes written to buf, or 0 if the packet is invalid or does not fit. */
int Wire_Encode(const Packet *p, uint8_t *buf, int cap);
/* False when buf is not a well-formed Pond Kings packet of this version. */
bool Wire_Decode(const uint8_t *buf, int len, Packet *out);

/* A setup a machine may play: every field in range, the two machines'
 * slots disjoint and each slot that plays on a team. */
bool Wire_SetupValid(const WireSetup *s);
/* A fingerprint of a setup, so the host knows the guest got the same one. */
uint32_t Wire_SetupCheck(const WireSetup *s);
/* The packet token for a room, from its relay key; never 0. */
uint32_t Wire_TokenFromKey(const char *relayKey);

#endif
