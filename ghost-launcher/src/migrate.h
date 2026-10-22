#ifndef MIGRATE_H
#define MIGRATE_H

#include <stdbool.h>
#include "launcherini.h"
#include "manifest.h"

/* One-time fixes to a player's files, each marked done in launcher.ini so
 * it never runs twice. No raylib. */

/* Coilrush, Ghostmaze, Lanehop, Gemdive and Girderclimb left the arcade on
 * 2026-10-21. True for their program names. */
bool Migrate_IsRetired(const char *slug);

/* Their machines leave the player's catalog (games.txt), matched by program
 * name, so a machine renamed by hand goes too. Their scores, playtime,
 * trophies and saves stay on disk. Runs once: a machine added back by hand
 * afterwards stays. catalogLoaded is what Manifest_Load said; without a
 * catalog nothing is marked, so a later start still does it. The mark
 * (migrated.retired=1) is written only after games.txt is saved, so a save
 * that failed tries again next start (the machines are gone from this
 * session either way). True if a machine was removed. */
bool Migrate_RetiredGames(LauncherIni *ini, Manifest *m, bool catalogLoaded);

/* Pond Kings joined the arcade on 2026-10-22. A catalog made before then
 * gets its machine once, at the end of the row, unless a machine for the
 * same program is already there. Marked (migrated.pondkings=1) only after
 * games.txt is saved, like the one above: a machine removed by hand
 * afterwards stays removed. True if the machine was added. */
bool Migrate_NewGames(LauncherIni *ini, Manifest *m, bool catalogLoaded);

#endif
