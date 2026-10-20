#ifndef MIGRATE_H
#define MIGRATE_H

#include <stdbool.h>
#include "launcherini.h"
#include "manifest.h"
#include "stats.h"

/* One-time fixes to a player's files, each marked done in launcher.ini so
 * it never runs twice. No raylib. */

/* Gemdive was called Gemdelve once, and playtime.txt is keyed by name, so
 * the time played under the old name sits apart. It is added to the
 * machine whose program is gemdive (named Gemdive unless the catalog says
 * otherwise) and the old line goes. Not while the catalog still has a
 * machine called Gemdelve: that time is still in use. The mark
 * (migrated.gemdelve=1) is written only after playtime.txt is saved, and
 * the fold finds nothing to move a second time, so a crash in between is
 * harmless. True if time was moved. */
bool Migrate_GemdelvePlaytime(LauncherIni *ini, PlayStats *stats, const Manifest *m);

#endif
