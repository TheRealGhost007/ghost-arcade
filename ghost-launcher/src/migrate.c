#include "migrate.h"
#include "scores.h"
#include <string.h>

#define RETIRED_MARK "migrated.retired"

static const char *const kRetired[] = {"coilrush", "ghostmaze", "lanehop", "gemdive", "girderclimb"};

bool Migrate_IsRetired(const char *slug) {
    for (size_t i = 0; slug && i < sizeof(kRetired) / sizeof(kRetired[0]); i++)
        if (strcmp(slug, kRetired[i]) == 0) return true;
    return false;
}

#define NEW_GAMES_MARK "migrated.pondkings"

bool Migrate_NewGames(LauncherIni *ini, Manifest *m, bool catalogLoaded) {
    if (!m || !catalogLoaded || LauncherIni_GetBool(ini, NEW_GAMES_MARK, false)) return false;
    bool present = false;
    for (int i = 0; i < m->count && !present; i++) {
        char slug[SCORES_SLUG_LEN];
        present = Scores_SlugFromExec(m->games[i].exec, slug, sizeof(slug)) && strcmp(slug, "pondkings") == 0;
    }
    bool added = false;
    if (!present) {
        added = Manifest_AddGame(m, "Pond Kings", "~/.local/bin/pondkings", "~/.local/share/pondkings/assets/icons/pondkings.png",
                                 "Pond hockey with your friends, three on three", "~/Work/ghost-arcade/pondkings");
        if (!added) return false; /* a full catalog: try again next start */
        if (!Manifest_Save(m)) return added;
    }
    LauncherIni_Store(ini, NEW_GAMES_MARK, "1");
    return added;
}

bool Migrate_RetiredGames(LauncherIni *ini, Manifest *m, bool catalogLoaded) {
    if (!m || !catalogLoaded || LauncherIni_GetBool(ini, RETIRED_MARK, false)) return false;
    bool removed = false;
    for (int i = m->count - 1; i >= 0; i--) {
        char slug[SCORES_SLUG_LEN];
        if (Scores_SlugFromExec(m->games[i].exec, slug, sizeof(slug)) && Migrate_IsRetired(slug))
            removed = Manifest_RemoveGame(m, i) || removed;
    }
    if (removed && !Manifest_Save(m)) return removed; /* not marked: the next start tries again */
    LauncherIni_Store(ini, RETIRED_MARK, "1");
    return removed;
}
