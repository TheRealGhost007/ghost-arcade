#ifndef LINEUP_H
#define LINEUP_H

#include <stdbool.h>
#include "manifest.h"
#include "stats.h"
#include "scores.h"
#include "launcherini.h"

/* The player's own row: which machines stand in the hall and in what order.
 * games.txt is the arcade's catalog and is never rewritten for this; each
 * player keeps their row in launcher.ini, under each machine's program name
 * (its slug), so a rename keeps it:
 *   row_order=yours|recent|most|az   start_on=last|first   last_played=SLUG
 *   fav.SLUG=1   hide.SLUG=1   place.SLUG=N   played.SLUG=SECONDS-SINCE-1970
 *
 * Two numberings meet here. A machine's place in the row is what the hall
 * moves along and draws; its catalog index (its line in games.txt) is what
 * playing, Edit, Remove, names and trophies act on. A Lineup maps one to the
 * other both ways, and nothing else converts between them. No raylib. */

typedef enum { ORDER_YOURS, ORDER_RECENT, ORDER_MOST, ORDER_AZ, ORDER_COUNT } RowOrder;

/* A machine without a place of its own in your order comes after every one
 * that has one, in catalog order. */
#define ROW_UNPLACED 100000

typedef struct {
    char slug[SCORES_SLUG_LEN]; /* "" when its program name cannot be a key */
    char name[64];              /* for A-Z */
    bool favourite, hidden, tool;
    int yours;                  /* place.SLUG, or ROW_UNPLACED + catalog index */
    long long played;           /* last started from the launcher, 0 = never */
    double seconds;             /* time played (playtime.txt) */
} RowMachine;

/* What launcher.ini (and playtime.txt) say about the row, per catalog index. */
typedef struct {
    RowMachine m[MANIFEST_MAX_GAMES];
    int count;
    RowOrder order;
    bool startOnLast;
    char lastPlayed[SCORES_SLUG_LEN];
} RowPrefs;

typedef struct {
    int count;                      /* machines on the row */
    int index[MANIFEST_MAX_GAMES];  /* place -> catalog index */
    int place[MANIFEST_MAX_GAMES];  /* catalog index -> place, -1 when it is not on the row */
} Lineup;

void Row_Read(RowPrefs *p, const LauncherIni *ini, const Manifest *m, const PlayStats *stats);
/* start_on alone: Start on changes nothing on the row until the next start. */
bool Row_StartOnLast(const LauncherIni *ini);

/* The row in an order: favourites first, then by the order (ties keep your
 * own order, then the catalog's). Hidden machines are left out unless
 * withHidden (Settings > Machines lists them all). */
void Lineup_Build(Lineup *l, const RowPrefs *p, RowOrder order, bool withHidden);
int Lineup_IndexAt(const Lineup *l, int place); /* -1 off the row */
int Lineup_PlaceOf(const Lineup *l, int index); /* -1 off the row */
/* The machine the launcher opens on: the one played last, when that is the
 * setting and it is on the row; otherwise the first. -1 for an empty row. */
int Lineup_Start(const Lineup *l, const RowPrefs *p);
/* After the row changed: `index` if it is still on it, else whatever now
 * stands at `place` (or the last). -1 for an empty row. */
int Lineup_Keep(const Lineup *l, int index, int place);

/* "Your order", "Recently played", "Most played", "A-Z". */
const char *Row_OrderName(int order);
RowOrder Row_NextOrder(RowOrder order);
int Row_VisibleCount(const RowPrefs *p);

/* --- what Settings > Machines may do (mine: ORDER_YOURS, with hidden) --- */
/* Up (-1) or Down (+1) in your order, within its group: favourites stay
 * first, so a machine never crosses from one group to the other. */
bool Row_CanMove(const RowPrefs *p, const Lineup *mine, int index, int dir);
/* One machine always stays on the row. */
bool Row_CanHide(const RowPrefs *p, int index);

/* --- saving: each re-reads launcher.ini first, as LauncherIni_Store does,
 * and leaves the fresh copy in *ini. False: nothing was kept. --- */
bool Row_SaveOrder(LauncherIni *ini, RowOrder order);
bool Row_SaveStartOn(LauncherIni *ini, bool last);
bool Row_SaveFavourite(LauncherIni *ini, const char *slug, bool on);
bool Row_SaveHidden(LauncherIni *ini, const char *slug, bool on);
/* A machine was started: Recently played and Start on follow it. */
bool Row_SavePlayed(LauncherIni *ini, const char *slug, long long when);
/* When the launcher last started the machine (played.SLUG), 0 = never. */
long long Row_Played(const LauncherIni *ini, const char *slug);
/* launcher.ini keeps a star, hiding, place or played time under this slug
 * (or it is the machine played last). */
bool Row_HasKeys(const LauncherIni *ini, const char *slug);
/* A machine's program got another file name: its row keys (and Start on's
 * last played) move from the old slug to the new one. True when nothing
 * needed moving too. */
bool Row_CarryKeys(LauncherIni *ini, const char *from, const char *to);
/* Swaps the machine with its neighbour in your order, writing every
 * machine's place (your order is then all explicit). */
bool Row_Move(LauncherIni *ini, const RowPrefs *p, const Lineup *mine, int index, int dir);

#endif
