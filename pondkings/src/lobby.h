#ifndef LOBBY_H
#define LOBBY_H

#include <stdbool.h>
#include "net.h"

/* Calls to the arcade's server for room codes and crew codes, one at a
 * time on a worker thread so the game never waits on the internet (the
 * screens ask for the answer every frame), and the STUN servers' address
 * lookup, which blocks too. Uses libcurl; the server and key come from
 * netmsg.c. */

typedef enum { LOBBY_IDLE, LOBBY_BUSY, LOBBY_DONE } LobbyState;

typedef struct {
    bool ok;            /* the server answered and said yes (HTTP 2xx) */
    bool missing;       /* the server has no such call: it isn't set up for Pond Kings yet */
    long status;        /* HTTP status, 0 when nothing came back */
    char body[2048];    /* the answer */
    NetAddr stun[3];    /* a STUN lookup's answers */
    int stunCount;
} LobbyReply;

/* Once, from the main thread, before any other network call (curl's own setup). */
void NetCurl_Init(void);
void NetCurl_Shutdown(void);

/* POST <server>/rest/v1/rpc/<fn> with a JSON body. False if a call is
 * already running. */
bool Lobby_Call(const char *fn, const char *jsonBody);
/* A call whose answer nobody needs (closing a room): queued behind the
 * others, up to four at a time. */
bool Lobby_CallDetached(const char *fn, const char *jsonBody);
/* Looks up the STUN servers. False if a call is already running. */
bool Lobby_LookupStun(void);
/* LOBBY_DONE once per finished call, with its reply; then LOBBY_IDLE. */
LobbyState Lobby_Poll(LobbyReply *reply);
/* Forget the call in flight: its answer, when it comes, is thrown away. */
void Lobby_Forget(void);
void Lobby_Shutdown(void);

#endif
