#ifndef NETUI_H
#define NETUI_H

#include <stdbool.h>
#include "game.h"
#include "input.h"
#include "journey.h"
#include "persist.h"
#include "screens.h"

/* Online play and the rival crews: their screens and everything behind
 * them (the lobby's room codes, the connection, the rollback session, the
 * crew codes). main.c hands over to this module from the menu and asks it
 * for the game to draw while an online game runs. */

typedef enum { NETUI_STAY, NETUI_BACK, NETUI_PLAY } NetUiStep;

/* Online games and crew uploads/downloads talk to the internet; nothing
 * here does anything until the player picks Online or Rival crews. */
void NetUi_Init(const char *username);
void NetUi_Shutdown(void);

/* The online menu (host or join) / the rival crews screen. */
void NetUi_OpenOnline(Settings *s, const JoinSlot slots[MAX_PLAYERS]);
void NetUi_OpenCrews(Settings *s, const Journey *j);
/* One frame of whichever screen is open. NETUI_PLAY: an online game starts
 * (main switches to drawing NetUi_Game()). */
NetUiStep NetUi_Update(Settings *s);
void NetUi_Draw(void);

/* While an online game runs: this machine's players' input in, ticks out.
 * onTick is called once for every tick that is new (not for ticks re-run
 * after a correction), for sounds and effects. False when the game is over
 * or the connection is gone (NetUi_Draw then says why). */
bool NetUi_StepGame(const PadInput local[MAX_PLAYERS], float dt, void (*onTick)(const Game *g, void *ctx), void *ctx);
const Game *NetUi_Game(void);
int NetUi_PingMs(void);
bool NetUi_Waiting(void);
/* Which game slot this machine's n-th joined player plays as. */
int NetUi_SlotFor(int localIndex);
int NetUi_Rivals(void); /* the rival team index the host picked */
/* The device this machine's player picked Host or Join with. */
int NetUi_LocalDevice(void);
/* Every tick on screen used the other machine's real presses (or it left
 * after the final whistle): the result is the result. */
bool NetUi_Settled(void);
void NetUi_Leave(void);

/* For tools/shots.c: puts the online screens in a state worth a picture,
 * with made-up rooms and crews and no network. Then NetUi_Draw. */
enum {
    NETUI_PREVIEW_NOTICE, NETUI_PREVIEW_MENU, NETUI_PREVIEW_HOST, NETUI_PREVIEW_JOIN_CODE, NETUI_PREVIEW_WAITING,
    NETUI_PREVIEW_CONNECTING, NETUI_PREVIEW_ERROR, NETUI_PREVIEW_CREWS, NETUI_PREVIEW_CREW_CODE, NETUI_PREVIEW_COUNT
};
void NetUi_Preview(int which, Settings *s);

/* Friends' crews added on this machine, for the Journey's final. */
int NetUi_FriendCrews(CrewCard *out, int max);

#endif
