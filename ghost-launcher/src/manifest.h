#ifndef MANIFEST_H
#define MANIFEST_H

#include <stdbool.h>
#include <stddef.h>

#define MANIFEST_MAX_GAMES 64
#define MANIFEST_FIELD_LEN 256

typedef struct {
    char name[MANIFEST_FIELD_LEN];
    char exec[MANIFEST_FIELD_LEN];
    char icon[MANIFEST_FIELD_LEN];
    char desc[MANIFEST_FIELD_LEN];
    /* Optional 5th catalog field: a source folder the launcher can install
     * the game from (it runs `make install` there) when exec is missing. */
    char install[MANIFEST_FIELD_LEN];
} GameEntry;

#define MANIFEST_FIELD_COUNT 5

typedef struct {
    GameEntry games[MANIFEST_MAX_GAMES];
    int count;
} Manifest;

/* Loads the user's catalog from $XDG_CONFIG_HOME/ghost-launcher/games.txt.
 * If it doesn't exist yet, seeds it by copying assetsDir/games.txt (the
 * bundled default catalog) there first. */
bool Manifest_Load(Manifest *m, const char *assetsDir);
bool Manifest_Save(const Manifest *m);

bool Manifest_AddGame(Manifest *m, const char *name, const char *exec,
                       const char *icon, const char *desc, const char *install);
/* Removes by index (0-based). Returns false if index is out of range. */
bool Manifest_RemoveGame(Manifest *m, int index);

/* Expands a leading "~/" to $HOME; otherwise copies the path unchanged. */
void Manifest_ExpandPath(const char *in, char *out, size_t outSize);

#endif
