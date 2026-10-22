#ifndef SESSION_H
#define SESSION_H

#include <stdbool.h>
#include <stdint.h>
#include "game.h"
#include "wire.h"

/* Rollback: how an online game feels as quick as a couch one. Each machine
 * steps the game as soon as its own presses are in, guessing the other
 * machine's (it does what it did last). When the real presses arrive and a
 * guess was wrong, the game goes back to the state before that tick (every
 * recent state is kept) and steps forward again with the truth, all inside
 * one frame. The other machine's presses arrive a few ticks late, so its
 * skater can shift a little on screen; this machine's own players never
 * wait for the network.
 *
 * Both machines fingerprint the confirmed state every two seconds and swap
 * fingerprints; a difference means the two games have parted (a bug), and
 * the game stops rather than carry on as two different games.
 *
 * Plain data and the rules core only: no sockets, no clock, no raylib, so
 * the tests can run two sessions over a pretend network. */

#define SESSION_INPUT_DELAY 2   /* this machine's presses count this many ticks later: fewer corrections */
#define SESSION_MAX_AHEAD 12    /* ticks it may run on guesses before it waits for the other machine */
#define SESSION_SNAPS 32        /* recent states kept for going back */
#define SESSION_RING 128        /* recent ticks of presses kept */
#define SESSION_HASH_EVERY 120  /* fingerprint the state every this many ticks */
#define SESSION_HASH_KEEP 8
#define SESSION_NONE 0xFFFFFFFFu

typedef struct {
    uint32_t tick;
    uint64_t hash;
} SessionHash;

typedef struct {
    Game game;                  /* the newest state: after `tick` steps */
    uint32_t tick;
    uint8_t localMask, remoteMask;      /* the slots each machine steers */
    PadInput input[SESSION_RING][MAX_PLAYERS]; /* known presses by tick (row tick % SESSION_RING) */
    PadInput used[SESSION_RING][MAX_PLAYERS];  /* what each tick was last stepped with */
    uint32_t localKnown;        /* this machine's presses are in for every tick below this */
    uint32_t remoteKnown;       /* the other machine's are in for every tick below this */
    uint32_t remoteAck;         /* the other machine has ours below this */
    uint32_t remoteTick;        /* its own tick, as it last said */
    uint32_t rollbackFrom;      /* the first tick stepped on a wrong guess, or SESSION_NONE */
    Game snap[SESSION_SNAPS];   /* snap[t % SESSION_SNAPS]: the state before tick t was stepped */
    uint32_t nextHash;          /* the next tick to fingerprint */
    SessionHash mine[SESSION_HASH_KEEP];
    int mineCount;
    SessionHash theirs;         /* their newest fingerprint (tick SESSION_NONE: none yet) */
    bool desync;
    uint32_t desyncTick;
    uint32_t rollbacks, resimTicks; /* how much correcting went on */
} Session;

void Session_Init(Session *s, const GameSetup *setup, uint8_t localMask, uint8_t remoteMask);
/* False while one more tick would run too far ahead of the other machine,
 * or after the games parted. */
bool Session_CanStep(const Session *s);
/* One tick: this machine's presses go in (for its own slots), a wrong guess
 * is put right, and the game steps once. onTick (may be NULL) sees each new
 * tick exactly once; ticks stepped again after a correction are not shown
 * twice. */
void Session_Step(Session *s, const PadInput local[MAX_PLAYERS], void (*onTick)(const Game *g, void *ctx), void *ctx);
/* Puts a wrong guess right now, without stepping a new tick (so the screen
 * shows the truth as soon as it is known). */
void Session_Catchup(Session *s);
/* The other machine's presses, its acks and its fingerprint. Anything
 * outside what it may send (slots it doesn't steer, ticks far ahead) is
 * ignored. */
void Session_Receive(Session *s, const WireInput *in);
/* What to send now: our presses it hasn't acked (the oldest first), our
 * ack of its presses, our tick and our newest fingerprint. */
void Session_Outgoing(const Session *s, WireInput *out);
/* Every tick stepped so far used the other machine's real presses. */
bool Session_Settled(const Session *s);
/* Ticks this machine is ahead of the other, allowing for a one-way trip of
 * oneWayTicks: positive means it should ease off a little. */
int Session_Advantage(const Session *s, int oneWayTicks);

#endif
