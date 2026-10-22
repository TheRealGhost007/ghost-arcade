#ifndef CREWS_H
#define CREWS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "game.h"
#include "journey.h"

/* The rival crews, minus the talking (netui.c and lobby.c do that): how
 * this machine's players play, added up over their games; a crew as one
 * short line of text the server can keep under an eight-letter code; and
 * the friends' crews added on this machine. Files live in
 * $XDG_DATA_HOME/pondkings/. No raylib, no network. */

#define CREWS_MAX_FRIENDS 8
#define CREW_TEXT_MAX 400
#define CREW_CODE_LEN 8

/* Habits added up over games (Game.habits, per player). */
typedef struct {
    uint32_t games;
    uint32_t shots, passes, checks, carryTicks;
    uint32_t sideShots[3];
} PlayStyle;

void Crews_AddGame(PlayStyle *ps, const Habits *h);
/* How the computer plays a crew to play like this: shooting, passing and
 * checking more or less than usual, and a favourite side. Neutral until
 * there are a couple of games to go on. */
AiLean Crews_LeanFrom(const PlayStyle *ps);
/* A few words for the screen: "Shoots on sight, hits hard, aims high". */
void Crews_Describe(const AiLean *lean, char *out, size_t size);

/* A crew as text for the server ("1;Zippy,0,0;Sam,1,0;Tank,2,0;10,-5,3,1")
 * and back. Decoding checks and clamps every field. */
bool Crews_Encode(const CrewCard *c, char *out, size_t size);
bool Crews_Decode(const char *name, const char *text, CrewCard *out);
/* A crew's name made safe: printable, no separators, at most 23 characters. */
void Crews_CleanName(const char *in, char *out, size_t size);
/* "AB3DEF7H": eight letters from the code alphabet (no I, O, 0 or 1). */
bool Crews_ValidCode(const char *code, int len);

bool Crews_LoadStyle(PlayStyle *ps);
bool Crews_SaveStyle(const PlayStyle *ps);
int Crews_LoadFriends(CrewCard *out, int max);
bool Crews_SaveFriends(const CrewCard *list, int count);
/* This install's crew: the code it was shared under ("" if never). */
void Crews_LoadMyCode(char *code, size_t size);
bool Crews_SaveMyCode(const char *code);
/* The secret that owns this install's crew on the server (made once, kept
 * private): 64 hex digits. */
bool Crews_Secret(char *out, size_t size);

#endif
