#ifndef PROFILE_H
#define PROFILE_H

#include <stdbool.h>
#include "manifest.h"

#define PROFILE_USERNAME_LEN 64
/* Reserved "game name" holding the player's arcade-wide name. Games fall
 * back to it when they have no entry of their own (see ghostlink.c in each
 * game), so one name covers every game, present and future. */
#define PROFILE_DEFAULT_KEY "__DEFAULT__"

typedef struct {
    char gameName[MANIFEST_FIELD_LEN];
    char username[PROFILE_USERNAME_LEN];
} ProfileEntry;

typedef struct {
    ProfileEntry entries[MANIFEST_MAX_GAMES];
    int count;
} Profiles;

/* Loads from $XDG_DATA_HOME/ghost-launcher/profiles.txt, which the games
 * read too: `Game Name|username` overrides, plus one PROFILE_DEFAULT_KEY
 * row for the arcade-wide name. */
bool Profiles_Load(Profiles *p);
bool Profiles_Save(const Profiles *p);

void Profiles_SetUsername(Profiles *p, const char *gameName, const char *username);
/* The name a game will actually play under: its own override if set,
 * otherwise the arcade-wide name. Returns "" (never NULL) if neither. */
const char *Profiles_GetUsername(const Profiles *p, const char *gameName);
/* Only the entry stored under exactly this key (a game name or
 * PROFILE_DEFAULT_KEY), with no fallback -- what the edit screens show. */
const char *Profiles_GetOwnUsername(const Profiles *p, const char *key);

#endif
