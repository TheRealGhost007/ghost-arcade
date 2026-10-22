/* Headless tests for Phase 4, "your hall": the player's row (src/lineup.c):
 * every order with and without hidden machines, favourites first, the
 * mapping between a place in the row and a line of games.txt (and that the
 * hall draws and clicks through the same mapping), start on last played,
 * keeping the right machine lit through Remove, Add and Edit, and what each
 * change writes to launcher.ini; the one-time retired-games migration (src/migrate.c);
 * the hall music's rules (src/hallmusic.c) and the quiet hall (src/hall.c).
 * test_main.c's main() points XDG_DATA_HOME and XDG_CONFIG_HOME at a temp
 * folder before these run. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "../src/lineup.h"
#include "../src/migrate.h"
#include "../src/hallmusic.h"
#include "../src/hall.h"
#include "../src/screens.h"
#include "song.h"

/* ------------------------------------------------------------- helpers */

static Manifest sCat;
static PlayStats sStats;
static LauncherIni sIni;
static RowPrefs sPrefs;

static void IniWrite(const char *text) {
    char path[700];
    if (!LauncherIni_Path(path, sizeof(path))) return;
    FILE *f = fopen(path, "w");
    if (!f) { CHECK(false, "row: launcher.ini fixture"); return; }
    fputs(text, f);
    fclose(f);
}

static bool IniHas(const char *text) {
    char path[700], buf[8192];
    if (!LauncherIni_Path(path, sizeof(path))) return false;
    FILE *f = fopen(path, "r");
    if (!f) return false;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    return strstr(buf, text) != NULL;
}

static void Add(const char *name, const char *slug) {
    GameEntry *e = &sCat.games[sCat.count++];
    memset(e, 0, sizeof(*e));
    snprintf(e->name, sizeof(e->name), "%s", name);
    snprintf(e->exec, sizeof(e->exec), "~/.local/bin/%s", slug);
}

/* The arcade's catalog as it was until 2026-10-21, in games.txt's order:
 * sample machines for the row, and the five since retired for the
 * migration that removes them. */
static void Catalog(void) {
    memset(&sCat, 0, sizeof(sCat));
    static const char *const kNames[] = {"Blockfall", "Coilrush", "Brickburst", "Skyraid", "Ghostmaze", "Rockdrift",
                                         "Lanehop", "Crawlshot", "Moondrop", "Gemdive", "Girderclimb", "Chipforge"};
    for (int i = 0; i < 12; i++) {
        char slug[32];
        snprintf(slug, sizeof(slug), "%s", kNames[i]);
        for (char *c = slug; *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c + 32);
        Add(kNames[i], slug);
    }
    memset(&sStats, 0, sizeof(sStats));
    Stats_AddGameTime(&sStats, "Blockfall", 2296);
    Stats_AddGameTime(&sStats, "Brickburst", 1361);
    Stats_AddGameTime(&sStats, "Chipforge", 1120);
    Stats_AddGameTime(&sStats, "Coilrush", 610);
}

static void Load(const char *ini) {
    IniWrite(ini);
    LauncherIni_Load(&sIni);
    Row_Read(&sPrefs, &sIni, &sCat, &sStats);
}

/* The row's names, joined: "Blockfall,Coilrush,...". */
static const char *Names(const Lineup *l) {
    static char out[1024];
    out[0] = '\0';
    for (int k = 0; k < l->count; k++) {
        size_t used = strlen(out);
        snprintf(out + used, sizeof(out) - used, "%s%s", k ? "," : "", sCat.games[l->index[k]].name);
    }
    return out;
}

static bool Bijective(const Lineup *l, const RowPrefs *p, bool withHidden) {
    int shown = 0;
    for (int i = 0; i < p->count; i++) {
        bool on = withHidden || !p->m[i].hidden;
        shown += on;
        if (on != (l->place[i] >= 0)) return false;
        if (on && l->index[l->place[i]] != i) return false;
    }
    for (int i = p->count; i < MANIFEST_MAX_GAMES; i++) if (l->place[i] != -1) return false;
    for (int k = 0; k < l->count; k++) if (l->index[k] < 0 || l->index[k] >= p->count || l->place[l->index[k]] != k) return false;
    return shown == l->count && Lineup_IndexAt(l, -1) == -1 && Lineup_IndexAt(l, l->count) == -1 && Lineup_PlaceOf(l, -1) == -1;
}

/* ----------------------------------------------------------------- read */

static void test_row_read(void) {
    Catalog();
    Load("");
    CHECK(sPrefs.count == 12 && sPrefs.order == ORDER_YOURS && sPrefs.startOnLast && !sPrefs.lastPlayed[0],
          "row: no settings yet: your order, and start on the machine played last");
    CHECK(strcmp(sPrefs.m[0].slug, "blockfall") == 0 && sPrefs.m[3].yours == ROW_UNPLACED + 3 && !sPrefs.m[3].favourite &&
          sPrefs.m[0].seconds == 2296 && sPrefs.m[11].tool && !sPrefs.m[0].tool,
          "row: each machine by its program name, unplaced, its playtime; Chipforge is the tool");
    Load("row_order=most\nstart_on=first\nlast_played=skyraid\nfav.lanehop=1\nfav.coilrush=yes\nhide.ghostmaze=1\n"
         "place.moondrop=0\nplace.gemdive=-4\nplayed.skyraid=1760000000\nplayed.lanehop=junk\n");
    CHECK(sPrefs.order == ORDER_MOST && !sPrefs.startOnLast && strcmp(sPrefs.lastPlayed, "skyraid") == 0, "row: order, start on, last played");
    CHECK(sPrefs.m[6].favourite && sPrefs.m[1].favourite && sPrefs.m[4].hidden && !sPrefs.m[0].hidden,
          "row: favourites (1 or yes) and hidden machines, by program name");
    CHECK(sPrefs.m[8].yours == 0 && sPrefs.m[9].yours == ROW_UNPLACED + 9, "row: a place of your own; a nonsense place is no place");
    CHECK(sPrefs.m[3].played == 1760000000LL && sPrefs.m[6].played == 0, "row: when each was last started; junk is never");
    Load("row_order=sideways\nstart_on=whenever\n");
    CHECK(sPrefs.order == ORDER_YOURS && sPrefs.startOnLast, "row: words it does not know mean the defaults");
    CHECK(Row_StartOnLast(&sIni) && strcmp(Row_OrderName(ORDER_RECENT), "Recently played") == 0 && strcmp(Row_OrderName(ORDER_AZ), "A-Z") == 0 &&
          Row_OrderName(ORDER_COUNT)[0] == '\0' && Row_NextOrder(ORDER_AZ) == ORDER_YOURS && Row_NextOrder(ORDER_YOURS) == ORDER_RECENT,
          "row: the order names, and T cycles round them");

    /* Machines whose program name cannot be a key keep nothing. */
    Catalog();
    snprintf(sCat.games[2].exec, sizeof(sCat.games[2].exec), "~/bin/Bad Name");
    char longSlug[80];
    memset(longSlug, 'x', 60);
    longSlug[60] = '\0';
    snprintf(sCat.games[3].exec, sizeof(sCat.games[3].exec), "~/bin/%s", longSlug);
    Load("fav.bad name=1\n");
    CHECK(!sPrefs.m[2].slug[0] && !sPrefs.m[3].slug[0] && !sPrefs.m[2].favourite, "row: a program name that is no key, or too long for one, keeps nothing");
}

/* ---------------------------------------------------------------- order */

static void test_row_orders(void) {
    Catalog();
    /* Favourites Skyraid and Coilrush; Ghostmaze hidden; Moondrop placed
     * first and Lanehop second in your order; plays and playtimes. */
    Load("fav.skyraid=1\nfav.coilrush=1\nhide.ghostmaze=1\nplace.moondrop=0\nplace.lanehop=1\n"
         "played.blockfall=100\nplayed.gemdive=300\nplayed.skyraid=200\nplayed.ghostmaze=999\n");
    Stats_AddGameTime(&sStats, "Girderclimb", 92);
    Row_Read(&sPrefs, &sIni, &sCat, &sStats);
    static const struct {
        RowOrder order;
        bool withHidden;
        const char *want;
    } kWant[] = {
        {ORDER_YOURS, false, "Coilrush,Skyraid,Moondrop,Lanehop,Blockfall,Brickburst,Rockdrift,Crawlshot,Gemdive,Girderclimb,Chipforge"},
        {ORDER_YOURS, true, "Coilrush,Skyraid,Moondrop,Lanehop,Blockfall,Brickburst,Ghostmaze,Rockdrift,Crawlshot,Gemdive,Girderclimb,Chipforge"},
        {ORDER_RECENT, false, "Skyraid,Coilrush,Gemdive,Blockfall,Moondrop,Lanehop,Brickburst,Rockdrift,Crawlshot,Girderclimb,Chipforge"},
        {ORDER_RECENT, true, "Skyraid,Coilrush,Ghostmaze,Gemdive,Blockfall,Moondrop,Lanehop,Brickburst,Rockdrift,Crawlshot,Girderclimb,Chipforge"},
        {ORDER_MOST, false, "Coilrush,Skyraid,Blockfall,Brickburst,Chipforge,Girderclimb,Moondrop,Lanehop,Rockdrift,Crawlshot,Gemdive"},
        {ORDER_MOST, true, "Coilrush,Skyraid,Blockfall,Brickburst,Chipforge,Girderclimb,Moondrop,Lanehop,Ghostmaze,Rockdrift,Crawlshot,Gemdive"},
        {ORDER_AZ, false, "Coilrush,Skyraid,Blockfall,Brickburst,Chipforge,Crawlshot,Gemdive,Girderclimb,Lanehop,Moondrop,Rockdrift"},
        {ORDER_AZ, true, "Coilrush,Skyraid,Blockfall,Brickburst,Chipforge,Crawlshot,Gemdive,Ghostmaze,Girderclimb,Lanehop,Moondrop,Rockdrift"},
    };
    for (size_t i = 0; i < sizeof(kWant) / sizeof(kWant[0]); i++) {
        Lineup l;
        Lineup_Build(&l, &sPrefs, kWant[i].order, kWant[i].withHidden);
        char msg[160];
        snprintf(msg, sizeof(msg), "order: %s%s gives the expected row", Row_OrderName(kWant[i].order), kWant[i].withHidden ? " with hidden" : "");
        bool same = strcmp(Names(&l), kWant[i].want) == 0;
        if (!same) printf("  got  %s\n  want %s\n", Names(&l), kWant[i].want);
        CHECK(same && Bijective(&l, &sPrefs, kWant[i].withHidden), msg);
    }

    /* Every order and filter over many random rows: the two numberings stay
     * each other's inverse, favourites lead, and each order holds inside
     * its group with your order breaking ties. */
    bool bij = true, favFirst = true, sorted = true, stable = true;
    unsigned seed = 12345u;
    for (int run = 0; run < 400; run++) {
        memset(&sCat, 0, sizeof(sCat));
        memset(&sPrefs, 0, sizeof(sPrefs));
        int n = 1 + (int)((seed = seed * 1664525u + 1013904223u) >> 8) % 20;
        sPrefs.count = n;
        for (int i = 0; i < n; i++) {
            unsigned r = (seed = seed * 1664525u + 1013904223u) >> 8;
            RowMachine *m = &sPrefs.m[i];
            snprintf(m->name, sizeof(m->name), "%c%c machine", 'a' + (int)(r % 5), 'A' + (int)((r >> 3) % 3));
            snprintf(m->slug, sizeof(m->slug), "m%d", i);
            snprintf(sCat.games[i].name, sizeof(sCat.games[i].name), "%s", m->name);
            m->favourite = (r >> 5) % 4 == 0;
            m->hidden = (r >> 7) % 5 == 0;
            m->yours = (r >> 9) % 3 == 0 ? (int)((r >> 11) % 6) : ROW_UNPLACED + i;
            m->played = (r >> 13) % 3 == 0 ? 0 : (long long)((r >> 15) % 4);
            m->seconds = (double)((r >> 17) % 3);
        }
        sCat.count = n;
        for (int o = 0; o < ORDER_COUNT; o++)
            for (int h = 0; h < 2; h++) {
                Lineup l;
                Lineup_Build(&l, &sPrefs, (RowOrder)o, h == 1);
                if (!Bijective(&l, &sPrefs, h == 1)) bij = false;
                for (int k = 1; k < l.count; k++) {
                    const RowMachine *a = &sPrefs.m[l.index[k - 1]], *b = &sPrefs.m[l.index[k]];
                    if (!a->favourite && b->favourite) { favFirst = false; continue; }
                    if (a->favourite != b->favourite) continue;
                    int c = 0;
                    if (o == ORDER_RECENT) c = a->played < b->played ? 1 : (a->played > b->played ? -1 : 0);
                    if (o == ORDER_MOST) c = a->seconds < b->seconds ? 1 : (a->seconds > b->seconds ? -1 : 0);
                    if (o == ORDER_AZ) c = strcasecmp(a->name, b->name);
                    if (c > 0) sorted = false;
                    if (c == 0 && (a->yours > b->yours || (a->yours == b->yours && l.index[k - 1] > l.index[k]))) stable = false;
                }
            }
    }
    CHECK(bij, "order: in every order, with or without hidden machines, place and catalog index map one to one");
    CHECK(favFirst, "order: favourites always come first");
    CHECK(sorted, "order: recently played, most played and A-Z hold inside each group");
    CHECK(stable, "order: ties keep your own order, then the catalog's");
}

/* -------------------------------------------------- the hall's mapping */

/* What the hall draws at each place is the machine the row says, and a
 * click there reaches that place, in every order. */
static void test_row_hall_mapping(void) {
    Catalog();
    Load("fav.gemdive=1\nhide.coilrush=1\nplace.lanehop=0\nplayed.skyraid=50\nplayed.moondrop=70\n");
    bool labels = true, ids = true;
    for (int o = 0; o < ORDER_COUNT; o++) {
        Lineup l;
        Lineup_Build(&l, &sPrefs, (RowOrder)o, false);
        for (int place = 0; place < l.count; place++) {
            HallView v = {.arcadeName = "TRG", .manifest = &sCat, .lineup = &l, .place = place, .scroll = (float)place,
                          .play = HP_PLAY, .dailyReason = "", .canFavourite = true};
            KitList list;
            Screens_Hall(&list, &v);
            for (int p = place - 2; p <= place + 2; p++) {
                if (p < 0 || p >= l.count) continue;
                const KitItem *it = Kit_Get(&list, WID_CABINET + p);
                if (!it || strcmp(it->label, sCat.games[Lineup_IndexAt(&l, p)].name) != 0) labels = false;
                HallInput st = {0};
                Input in;
                memset(&in, 0, sizeof(in));
                KitEvent ev = {KIT_EV_PRESS, WID_CABINET + p, 0};
                HallIntent h = Hall_Read(&st, &in, &ev, place, l.count, false);
                if (p == place ? h.verb != HV_PLAY : h.moveTo != p) ids = false;
            }
            if (Screens_RowCount(&v) != l.count || Screens_RowIndex(&v, place) != l.index[place]) ids = false;
        }
    }
    CHECK(labels, "mapping: each cabinet drawn at a place is the machine the row puts there, in every order");
    CHECK(ids, "mapping: a click on a place plays the lit one or brings that place to the middle");
}

/* -------------------------------------------- start, and keeping a machine */

static void test_row_start_and_keep(void) {
    Catalog();
    Lineup l;
    Load("last_played=skyraid\n");
    Lineup_Build(&l, &sPrefs, sPrefs.order, false);
    CHECK(Lineup_Start(&l, &sPrefs) == 3, "start: on the machine played last");
    Load("last_played=skyraid\nstart_on=first\nfav.lanehop=1\n");
    Lineup_Build(&l, &sPrefs, sPrefs.order, false);
    CHECK(Lineup_Start(&l, &sPrefs) == 6, "start: on the first machine of the row (a favourite leads it)");
    Load("last_played=skyraid\nhide.skyraid=1\n");
    Lineup_Build(&l, &sPrefs, sPrefs.order, false);
    CHECK(Lineup_Start(&l, &sPrefs) == 0, "start: a last-played machine that is hidden: the first");
    Load("last_played=gone\n");
    Lineup_Build(&l, &sPrefs, sPrefs.order, false);
    CHECK(Lineup_Start(&l, &sPrefs) == 0, "start: one no longer in the catalog: the first");
    memset(&l, 0, sizeof(l));
    CHECK(Lineup_Start(&l, &sPrefs) == -1 && Lineup_Keep(&l, 3, 0) == -1, "start: an empty row has none");

    Load("row_order=az\n");
    Lineup_Build(&l, &sPrefs, sPrefs.order, false);
    CHECK(Lineup_Keep(&l, 7, 0) == 7, "keep: a machine still on the row stays lit");
    CHECK(Lineup_Keep(&l, -1, 3) == l.index[3] && Lineup_Keep(&l, -1, 99) == l.index[l.count - 1] && Lineup_Keep(&l, -1, -5) == l.index[0],
          "keep: otherwise the machine at that place, the last, or the first");

    /* Remove, in A-Z: the machine after it in the row comes to the middle,
     * and the catalog's later lines close up. */
    int gemdive = 9, place = Lineup_PlaceOf(&l, gemdive);
    Manifest_RemoveGame(&sCat, gemdive);
    Row_Read(&sPrefs, &sIni, &sCat, &sStats);
    Lineup_Build(&l, &sPrefs, sPrefs.order, false);
    int lit = Lineup_Keep(&l, -1, place);
    CHECK(strcmp(sCat.games[lit].name, "Ghostmaze") == 0 && Bijective(&l, &sPrefs, false), "keep: after Remove, the next machine in A-Z is lit");
    CHECK(strcmp(sCat.games[9].name, "Girderclimb") == 0 && Lineup_IndexAt(&l, Lineup_PlaceOf(&l, 9)) == 9,
          "keep: the catalog closed up after it, and the row follows the new lines");
    /* Add: the new machine is lit wherever A-Z puts it. */
    Manifest_AddGame(&sCat, "Arcadia", "~/bin/arcadia", "", "", "");
    Row_Read(&sPrefs, &sIni, &sCat, &sStats);
    Lineup_Build(&l, &sPrefs, sPrefs.order, false);
    lit = Lineup_Keep(&l, sCat.count - 1, 0);
    CHECK(lit == sCat.count - 1 && Lineup_PlaceOf(&l, lit) == 0, "keep: after Add, the new machine is lit, first in A-Z");
    /* Edit a name: same line of games.txt, a new place in A-Z. */
    snprintf(sCat.games[0].name, sizeof(sCat.games[0].name), "Zzblock");
    Row_Read(&sPrefs, &sIni, &sCat, &sStats);
    Lineup_Build(&l, &sPrefs, sPrefs.order, false);
    CHECK(Lineup_Keep(&l, 0, 0) == 0 && Lineup_PlaceOf(&l, 0) == l.count - 1, "keep: after Edit, the edited machine stays lit at its new place");
}

/* ---------------------------------------------------------------- saves */

static void test_row_saves(void) {
    Catalog();
    Load("# mine\nhall_sounds=4\n");
    CHECK(Row_SaveFavourite(&sIni, "skyraid", true) && IniHas("fav.skyraid=1") && IniHas("# mine\nhall_sounds=4\n"),
          "save: a favourite, by program name, other lines kept");
    CHECK(Row_SaveFavourite(&sIni, "skyraid", false) && !IniHas("fav.skyraid"), "save: no longer a favourite: the line goes");
    CHECK(Row_SaveHidden(&sIni, "lanehop", true) && IniHas("hide.lanehop=1") && Row_SaveHidden(&sIni, "lanehop", false) && !IniHas("hide.lanehop"),
          "save: hidden, and shown again");
    CHECK(!Row_SaveFavourite(&sIni, "", true) && !Row_SaveHidden(&sIni, "", true), "save: a machine without a key keeps nothing");
    CHECK(Row_SaveOrder(&sIni, ORDER_RECENT) && IniHas("row_order=recent") && Row_SaveOrder(&sIni, ORDER_AZ) && IniHas("row_order=az") &&
          Row_SaveOrder(&sIni, (RowOrder)99) && IniHas("row_order=yours"), "save: the order, in words");
    CHECK(Row_SaveStartOn(&sIni, false) && IniHas("start_on=first") && !Row_StartOnLast(&sIni) && Row_SaveStartOn(&sIni, true) &&
          IniHas("start_on=last"), "save: start on");
    CHECK(Row_SavePlayed(&sIni, "moondrop", 1760000123LL) && IniHas("played.moondrop=1760000123") && IniHas("last_played=moondrop"),
          "save: a machine started: when, and that it was last");
    LauncherIni_Load(&sIni);
    Row_Read(&sPrefs, &sIni, &sCat, &sStats);
    CHECK(sPrefs.m[8].played == 1760000123LL && strcmp(sPrefs.lastPlayed, "moondrop") == 0, "save: ...and it reads back");

    /* Moving in your order: every machine gets its place, the two swap. */
    Load("fav.ghostmaze=1\n");
    Lineup mine;
    Lineup_Build(&mine, &sPrefs, ORDER_YOURS, true);
    CHECK(strncmp(Names(&mine), "Ghostmaze,Blockfall,Coilrush", 28) == 0, "move: your order starts with the favourite");
    CHECK(!Row_CanMove(&sPrefs, &mine, 4, -1) && !Row_CanMove(&sPrefs, &mine, 4, 1) && !Row_CanMove(&sPrefs, &mine, 0, -1),
          "move: favourites stay first: neither the favourite nor the first of the rest can cross");
    CHECK(!Row_CanMove(&sPrefs, &mine, 11, 1) && Row_CanMove(&sPrefs, &mine, 0, 1), "move: not past the end");
    CHECK(Row_Move(&sIni, &sPrefs, &mine, 2, -1), "move: Brickburst up");
    Row_Read(&sPrefs, &sIni, &sCat, &sStats);
    Lineup_Build(&mine, &sPrefs, ORDER_YOURS, true);
    CHECK(strncmp(Names(&mine), "Ghostmaze,Blockfall,Brickburst,Coilrush,Skyraid", 47) == 0 && IniHas("place.brickburst=2") &&
          IniHas("place.coilrush=3") && IniHas("place.chipforge=11"), "move: it swaps with the one above; every place is written");
    CHECK(Row_Move(&sIni, &sPrefs, &mine, 0, 1), "move: Blockfall down");
    Row_Read(&sPrefs, &sIni, &sCat, &sStats);
    Lineup_Build(&mine, &sPrefs, ORDER_YOURS, true);
    CHECK(strncmp(Names(&mine), "Ghostmaze,Brickburst,Blockfall,Coilrush", 39) == 0, "move: and down again");
    sCat.games[3].exec[0] = '\0';
    Row_Read(&sPrefs, &sIni, &sCat, &sStats);
    Lineup_Build(&mine, &sPrefs, ORDER_YOURS, true);
    CHECK(!Row_CanMove(&sPrefs, &mine, 3, -1) && !Row_Move(&sIni, &sPrefs, &mine, 3, -1), "move: a machine without a key cannot move");

    /* One machine always stays on the row. */
    Catalog();
    Load("hide.blockfall=1\nhide.coilrush=1\nhide.brickburst=1\nhide.skyraid=1\nhide.ghostmaze=1\nhide.rockdrift=1\n"
         "hide.lanehop=1\nhide.crawlshot=1\nhide.moondrop=1\nhide.gemdive=1\nhide.girderclimb=1\n");
    CHECK(Row_VisibleCount(&sPrefs) == 1 && !Row_CanHide(&sPrefs, 11) && Row_CanHide(&sPrefs, 0) && !Row_CanHide(&sPrefs, 99),
          "hide: the last machine on the row cannot go; a hidden one can always come back");

    /* A file the launcher could not read whole is never saved over. */
    static char big[LINI_MAX_LINES * 8 + 64];
    size_t used = 0;
    for (int i = 0; i < LINI_MAX_LINES + 4; i++) used += (size_t)snprintf(big + used, sizeof(big) - used, "k%d=1\n", i);
    IniWrite(big);
    CHECK(!Row_SaveFavourite(&sIni, "skyraid", true) && !Row_SaveOrder(&sIni, ORDER_AZ) && !Row_SavePlayed(&sIni, "skyraid", 5) && !IniHas("fav.skyraid"),
          "save: a launcher.ini that did not fit is not written over");
    IniWrite("");
}

/* -------------------------------------------------------------- migrate */

/* The catalog's machines by name, joined: "Blockfall,Brickburst,...". */
static const char *CatalogNames(const Manifest *m) {
    static char out[1024];
    out[0] = '\0';
    for (int i = 0; i < m->count; i++) {
        size_t used = strlen(out);
        snprintf(out + used, sizeof(out) - used, "%s%s", i ? "," : "", m->games[i].name);
    }
    return out;
}

static void test_migrate(void) {
    /* The player's own games.txt: whatever an earlier test left is put back after. */
    const char *config = getenv("XDG_CONFIG_HOME");
    char path[700], kept[720];
    snprintf(path, sizeof(path), "%s/ghost-launcher/games.txt", config ? config : "");
    snprintf(kept, sizeof(kept), "%s.kept", path);
    bool hadCatalog = rename(path, kept) == 0;
    static Manifest disk;
    const char *const kLeft = "Blockfall,Brickburst,Skyraid,Rockdrift,Crawlshot,Moondrop,Chipforge";

    CHECK(Migrate_IsRetired("coilrush") && Migrate_IsRetired("ghostmaze") && Migrate_IsRetired("lanehop") &&
          Migrate_IsRetired("gemdive") && Migrate_IsRetired("girderclimb"), "retired: the five that left");
    CHECK(!Migrate_IsRetired("blockfall") && !Migrate_IsRetired("chipforge") && !Migrate_IsRetired("") && !Migrate_IsRetired(NULL),
          "retired: not the rest, nor nothing");

    /* The arcade as it was: the five go, the rest stay in their order, saved, then marked. */
    Catalog();
    IniWrite("hall_sounds=3\n");
    LauncherIni_Load(&sIni);
    CHECK(Migrate_RetiredGames(&sIni, &sCat, true), "retired: the five machines leave the catalog");
    CHECK(sCat.count == 7 && strcmp(CatalogNames(&sCat), kLeft) == 0, "retired: the other seven stay, in their order");
    CHECK(Manifest_Load(&disk, "/nonexistent") && strcmp(CatalogNames(&disk), kLeft) == 0, "retired: games.txt is saved without them");
    CHECK(IniHas("migrated.retired=1") && IniHas("hall_sounds=3"), "retired: then marked done, other settings kept");

    /* Once only: a machine added back by hand afterwards stays. */
    Manifest_AddGame(&sCat, "Coilrush", "~/.local/bin/coilrush", "", "", "");
    CHECK(!Migrate_RetiredGames(&sIni, &sCat, true) && sCat.count == 8, "retired: once only");

    /* By program name: a renamed machine goes, a lookalike name with its own program stays. */
    IniWrite("");
    LauncherIni_Load(&sIni);
    Catalog();
    snprintf(sCat.games[1].name, sizeof(sCat.games[1].name), "Snake");
    Add("Gemdive", "my-gemdive");
    CHECK(Migrate_RetiredGames(&sIni, &sCat, true) && sCat.count == 8 && strstr(CatalogNames(&sCat), "Snake") == NULL &&
          strcmp(sCat.games[7].name, "Gemdive") == 0, "retired: matched by program, not by name");

    /* Nothing to remove: still marked done. */
    IniWrite("");
    LauncherIni_Load(&sIni);
    CHECK(!Migrate_RetiredGames(&sIni, &sCat, true) && IniHas("migrated.retired=1"), "retired: nothing to remove is still marked done");

    /* No catalog loaded: nothing touched, nothing marked. */
    IniWrite("");
    LauncherIni_Load(&sIni);
    Catalog();
    CHECK(!Migrate_RetiredGames(&sIni, &sCat, false) && sCat.count == 12 && !IniHas("migrated"),
          "retired: without a catalog, not marked, so a later start does it");

    /* games.txt cannot be written: gone for this session, not marked, so the next start tries again. */
    unlink(path);
    mkdir(path, 0755); /* a folder in its place: the save's last step fails */
    CHECK(Migrate_RetiredGames(&sIni, &sCat, true) && sCat.count == 7 && !IniHas("migrated"),
          "retired: a save that failed leaves no mark");
    rmdir(path);

    /* Pond Kings joins a catalog made before it: once, at the end of the row. */
    IniWrite("");
    LauncherIni_Load(&sIni);
    Catalog();
    int before = sCat.count;
    CHECK(Migrate_NewGames(&sIni, &sCat, true) && sCat.count == before + 1 && strcmp(sCat.games[before].name, "Pond Kings") == 0 &&
              strcmp(sCat.games[before].exec, "~/.local/bin/pondkings") == 0 && strcmp(sCat.games[before].install, "~/Work/ghost-arcade/pondkings") == 0,
          "new games: Pond Kings joins at the end of the row");
    CHECK(Manifest_Load(&disk, "/nonexistent") && disk.count == before + 1 && strcmp(disk.games[before].name, "Pond Kings") == 0 &&
              IniHas("migrated.pondkings=1"),
          "new games: games.txt is saved with it, then marked done");
    Manifest_RemoveGame(&sCat, before);
    CHECK(!Migrate_NewGames(&sIni, &sCat, true) && sCat.count == before, "new games: once only: removed by hand, it stays removed");

    IniWrite("");
    LauncherIni_Load(&sIni);
    Catalog();
    Add("Hockey Night", "pondkings");
    CHECK(!Migrate_NewGames(&sIni, &sCat, true) && sCat.count == before + 1 && IniHas("migrated.pondkings=1"),
          "new games: a catalog that has it (by program) is only marked");

    IniWrite("");
    LauncherIni_Load(&sIni);
    Catalog();
    CHECK(!Migrate_NewGames(&sIni, &sCat, false) && sCat.count == before && !IniHas("migrated"),
          "new games: without a catalog, not marked, so a later start does it");

    IniWrite("");
    unlink(path);
    if (hadCatalog) rename(kept, path);
}

/* ---------------------------------------------------------- hall music */

static void test_hall_music(void) {
    /* Every combination: the tune plays only in front, with nothing else on. */
    bool table = true;
    for (int m = 0; m < 16; m++) {
        HallMusicState st = {m & 1, (m >> 1) & 1, (m >> 2) & 1, (m >> 3) & 1};
        MusicMode want = (m == 3) ? MUSIC_MENU : MUSIC_SILENT; /* enabled, focused, no coin, no game */
        if (HallMusic_Mode(st) != want) table = false;
    }
    CHECK(table, "music: plays (bass, arpeggio, hats) only when on, in front, with no coin dropping and no game running");
    HallMusicState play = {true, true, false, false};
    CHECK(HallMusic_Mode(play) == MUSIC_MENU, "music: the hall's menu layer");
    play.coinInFlight = true;
    CHECK(HallMusic_Mode(play) == MUSIC_SILENT, "music: the coin drop fades it out");
    play.coinInFlight = false;
    play.gameRunning = true;
    CHECK(HallMusic_Mode(play) == MUSIC_SILENT, "music: silent while a game the launcher started runs");
    play.gameRunning = false;
    play.focused = false;
    CHECK(HallMusic_Mode(play) == MUSIC_SILENT, "music: silent whenever the launcher is not in front");
    CHECK(HallMusic_Volume(100) == 60 && HallMusic_Volume(70) == 42 && HallMusic_Volume(0) == 0 && HallMusic_Volume(-5) == 0 &&
          HallMusic_Volume(250) == 60, "music: softer than the shared volume, which still sets it (0 is silent)");

    IniWrite("");
    LauncherIni_Load(&sIni);
    CHECK(HallMusic_Enabled(&sIni), "music: on unless switched off");
    CHECK(HallMusic_Save(&sIni, false) && IniHas("hall_music=off") && !HallMusic_Enabled(&sIni), "music: Off is kept in launcher.ini");
    CHECK(HallMusic_Save(&sIni, true) && IniHas("hall_music=on") && HallMusic_Enabled(&sIni), "music: and On");

    /* What it plays: the theme, or the song music.ini gives "ghostarcade". */
    HallSong song;
    HallMusic_Song(&song);
    int before = HallMusic_LibraryCount();
    CHECK(!song.custom && song.stamp == 0 && HallMusic_Stamp() == 0 && strcmp(HallMusic_SongWords(&song), "Built-in theme") == 0,
          "music: no song for the hall: the built-in theme");
    static Song s;
    Song_Init(&s, "Ghost Arcade (launcher)");
    char path[800];
    CHECK(Song_LibraryPath("ghost-arcade-launcher.gsong", path, sizeof(path), true) && Song_Save(&s, path) &&
          Song_Assign(HALL_MUSIC_SLUG, "ghost-arcade-launcher.gsong"), "music: a song saved and given to the hall (fixture)");
    HallMusic_Song(&song);
    CHECK(song.custom && song.stamp != 0 && song.stamp == HallMusic_Stamp() && strcmp(song.file, "ghost-arcade-launcher.gsong") == 0 &&
          strcmp(HallMusic_SongWords(&song), "Ghost Arcade (launcher)") == 0, "music: the hall's song, by its title");
    CHECK(HallMusic_LibraryCount() == before + 1, "music: the library counts it");
    long long stamp = song.stamp;
    Song_Init(&s, "Lobby");
    char other[800];
    Song_LibraryPath("lobby.gsong", other, sizeof(other), true);
    Song_Save(&s, other);
    Song_Assign(HALL_MUSIC_SLUG, "lobby.gsong");
    CHECK(HallMusic_Stamp() != stamp, "music: another song for the hall changes the stamp (the launcher loads it again)");
    /* A file that is no song: music.c plays the theme, so nothing may name
     * the file as what the hall plays. Its stamp still marks the choice, so
     * the launcher does not read it again and again. */
    FILE *junk = fopen(other, "w");
    if (junk) {
        fputs("not a song\n", junk);
        fclose(junk);
    }
    HallMusic_Song(&song);
    CHECK(!song.custom && song.stamp != 0 && song.stamp == HallMusic_Stamp() && strcmp(HallMusic_SongWords(&song), "Built-in theme") == 0,
          "music: a song file that does not load: the theme, and Settings says so");
    remove(other);
    CHECK(HallMusic_Stamp() == 0, "music: a song whose file is gone: the theme again");
    Song_Assign(HALL_MUSIC_SLUG, NULL);
    remove(path);
    HallMusic_Song(&song);
    CHECK(!song.custom && HallMusic_LibraryCount() == before, "music: cleared: the theme");
    IniWrite("");
}

/* ------------------------------------------------------------ quiet hall */

static void test_quiet_hall(void) {
    CHECK(Hall_Quiet(true, false) && !Hall_Quiet(true, true) && !Hall_Quiet(false, false) && !Hall_Quiet(false, true),
          "quiet: only while a game the launcher started runs and the launcher is behind it");
    CHECK(Hall_TargetFps(true) == 10 && Hall_TargetFps(false) == 60, "quiet: 10 frames a second resting, 60 otherwise");
}

void test_row_all(void) {
    test_row_read();
    test_row_orders();
    test_row_hall_mapping();
    test_row_start_and_keep();
    test_row_saves();
    test_migrate();
    test_hall_music();
    test_quiet_hall();
}
