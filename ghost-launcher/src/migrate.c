#include "migrate.h"
#include "scores.h"
#include <stdio.h>
#include <string.h>

#define GEMDELVE_MARK "migrated.gemdelve"

/* The catalog's name for the machine whose program is `slug`, or fallback. */
static const char *NameOf(const Manifest *m, const char *slug, const char *fallback) {
    for (int i = 0; m && i < m->count; i++) {
        char s[SCORES_SLUG_LEN];
        if (Scores_SlugFromExec(m->games[i].exec, s, sizeof(s)) && strcmp(s, slug) == 0) return m->games[i].name;
    }
    return fallback;
}

static bool InCatalog(const Manifest *m, const char *name) {
    for (int i = 0; m && i < m->count; i++) if (strcmp(m->games[i].name, name) == 0) return true;
    return false;
}

bool Migrate_GemdelvePlaytime(LauncherIni *ini, PlayStats *stats, const Manifest *m) {
    if (LauncherIni_GetBool(ini, GEMDELVE_MARK, false) || InCatalog(m, "Gemdelve")) return false;
    bool moved = Stats_Fold(stats, "Gemdelve", NameOf(m, "gemdive", "Gemdive"));
    if (moved && !Stats_Save(stats)) return moved; /* not marked: the next start tries again */
    LauncherIni_Store(ini, GEMDELVE_MARK, "1");
    return moved;
}
