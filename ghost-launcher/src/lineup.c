#include "lineup.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *const kOrderWords[ORDER_COUNT] = {
    [ORDER_YOURS] = "yours", [ORDER_RECENT] = "recent", [ORDER_MOST] = "most", [ORDER_AZ] = "az",
};
static const char *const kOrderNames[ORDER_COUNT] = {
    [ORDER_YOURS] = "Your order", [ORDER_RECENT] = "Recently played", [ORDER_MOST] = "Most played", [ORDER_AZ] = "A-Z",
};

const char *Row_OrderName(int order) { return order >= 0 && order < ORDER_COUNT ? kOrderNames[order] : ""; }

RowOrder Row_NextOrder(RowOrder order) { return (RowOrder)(((int)order + 1) % ORDER_COUNT); }

/* "fav." + slug: the longest prefix is "played.", so a slug that would not
 * leave a key short enough for launcher.ini is no key at all. */
static bool Key(char *out, size_t size, const char *prefix, const char *slug) {
    if (!slug || !slug[0]) return false;
    int n = snprintf(out, size, "%s%s", prefix, slug);
    return n > 0 && n < LINI_KEY_LEN && (size_t)n < size;
}

static void Slug(const GameEntry *e, char *out, size_t size) {
    char key[LINI_KEY_LEN + 8];
    if (!Scores_SlugFromExec(e->exec, out, size) || !Key(key, sizeof(key), "played.", out)) out[0] = '\0';
}

static long long GetLong(const LauncherIni *ini, const char *key) {
    const char *v = LauncherIni_Get(ini, key);
    if (!v || !v[0]) return 0;
    char *end;
    errno = 0;
    long long n = strtoll(v, &end, 10);
    return (*end == '\0' && errno == 0 && n > 0) ? n : 0;
}

static void ReadMachine(RowMachine *r, const LauncherIni *ini, const GameEntry *e, int index, const PlayStats *stats) {
    memset(r, 0, sizeof(*r));
    Slug(e, r->slug, sizeof(r->slug));
    snprintf(r->name, sizeof(r->name), "%.63s", e->name);
    r->tool = Manifest_IsTool(e);
    r->yours = ROW_UNPLACED + index;
    r->seconds = stats ? Stats_GetGameTime(stats, e->name) : 0.0;
    char key[LINI_KEY_LEN + 8];
    if (!r->slug[0]) return;
    Key(key, sizeof(key), "fav.", r->slug);
    r->favourite = LauncherIni_GetBool(ini, key, false);
    Key(key, sizeof(key), "hide.", r->slug);
    r->hidden = LauncherIni_GetBool(ini, key, false);
    Key(key, sizeof(key), "place.", r->slug);
    int place = LauncherIni_GetInt(ini, key, -1, -1, ROW_UNPLACED - 1);
    if (place >= 0) r->yours = place;
    Key(key, sizeof(key), "played.", r->slug);
    r->played = GetLong(ini, key);
}

bool Row_StartOnLast(const LauncherIni *ini) {
    const char *start = LauncherIni_Get(ini, "start_on");
    return !(start && strcmp(start, "first") == 0); /* the owner's default: where you left off */
}

void Row_Read(RowPrefs *p, const LauncherIni *ini, const Manifest *m, const PlayStats *stats) {
    memset(p, 0, sizeof(*p));
    p->count = m ? m->count : 0;
    for (int i = 0; i < p->count; i++) ReadMachine(&p->m[i], ini, &m->games[i], i, stats);
    const char *order = LauncherIni_Get(ini, "row_order");
    for (int o = 0; order && o < ORDER_COUNT; o++) if (strcmp(order, kOrderWords[o]) == 0) p->order = (RowOrder)o;
    p->startOnLast = Row_StartOnLast(ini);
    const char *last = LauncherIni_Get(ini, "last_played");
    if (last) snprintf(p->lastPlayed, sizeof(p->lastPlayed), "%.*s", (int)sizeof(p->lastPlayed) - 1, last);
}

/* --------------------------------------------------------------- order */

static int Cmp3(double a, double b) { return a < b ? -1 : (a > b ? 1 : 0); }

/* Negative when catalog machine a comes before b. */
static int Compare(const RowPrefs *p, RowOrder order, int a, int b) {
    const RowMachine *x = &p->m[a], *y = &p->m[b];
    if (x->favourite != y->favourite) return x->favourite ? -1 : 1;
    int c = 0;
    switch (order) {
        case ORDER_RECENT: c = Cmp3((double)y->played, (double)x->played); break; /* newest first; never played last */
        case ORDER_MOST: c = Cmp3(y->seconds, x->seconds); break;
        case ORDER_AZ: c = strcasecmp(x->name, y->name); break;
        default: break;
    }
    if (c) return c;
    if (x->yours != y->yours) return x->yours < y->yours ? -1 : 1;
    return a - b;
}

void Lineup_Build(Lineup *l, const RowPrefs *p, RowOrder order, bool withHidden) {
    l->count = 0;
    for (int i = 0; i < MANIFEST_MAX_GAMES; i++) l->place[i] = -1;
    for (int i = 0; i < p->count; i++) {
        if (p->m[i].hidden && !withHidden) continue;
        /* Insertion keeps it simple at 64 machines, and the order total. */
        int at = l->count;
        while (at > 0 && Compare(p, order, i, l->index[at - 1]) < 0) {
            l->index[at] = l->index[at - 1];
            at--;
        }
        l->index[at] = i;
        l->count++;
    }
    for (int k = 0; k < l->count; k++) l->place[l->index[k]] = k;
}

int Lineup_IndexAt(const Lineup *l, int place) { return place >= 0 && place < l->count ? l->index[place] : -1; }

int Lineup_PlaceOf(const Lineup *l, int index) { return index >= 0 && index < MANIFEST_MAX_GAMES ? l->place[index] : -1; }

int Lineup_Start(const Lineup *l, const RowPrefs *p) {
    if (l->count == 0) return -1;
    if (p->startOnLast && p->lastPlayed[0])
        for (int k = 0; k < l->count; k++) if (strcmp(p->m[l->index[k]].slug, p->lastPlayed) == 0) return l->index[k];
    return l->index[0];
}

int Lineup_Keep(const Lineup *l, int index, int place) {
    if (l->count == 0) return -1;
    if (Lineup_PlaceOf(l, index) >= 0) return index;
    if (place < 0) place = 0;
    if (place >= l->count) place = l->count - 1;
    return l->index[place];
}

int Row_VisibleCount(const RowPrefs *p) {
    int n = 0;
    for (int i = 0; i < p->count; i++) n += !p->m[i].hidden;
    return n;
}

/* ------------------------------------------------------ Settings' rules */

static int Neighbour(const Lineup *mine, int index, int dir) {
    int at = Lineup_PlaceOf(mine, index);
    return at < 0 ? -1 : Lineup_IndexAt(mine, at + dir);
}

bool Row_CanMove(const RowPrefs *p, const Lineup *mine, int index, int dir) {
    int other = Neighbour(mine, index, dir);
    if (index < 0 || index >= p->count || other < 0) return false;
    const RowMachine *a = &p->m[index], *b = &p->m[other];
    /* Both need a key to carry their places, and two catalog lines for one
     * program share theirs, so they cannot pass each other. */
    return a->favourite == b->favourite && a->slug[0] && b->slug[0] && strcmp(a->slug, b->slug) != 0;
}

bool Row_CanHide(const RowPrefs *p, int index) {
    if (index < 0 || index >= p->count) return false;
    return p->m[index].hidden || Row_VisibleCount(p) > 1;
}

/* --------------------------------------------------------------- saving */

static bool SetFlag(LauncherIni *ini, const char *prefix, const char *slug, bool on) {
    char key[LINI_KEY_LEN + 8];
    if (!Key(key, sizeof(key), prefix, slug) || !LauncherIni_Load(ini)) return false;
    if (on) {
        if (!LauncherIni_Set(ini, key, "1")) return false;
    } else {
        LauncherIni_Unset(ini, key); /* off is the default: the line goes */
    }
    return LauncherIni_Save(ini);
}

bool Row_SaveOrder(LauncherIni *ini, RowOrder order) {
    if ((int)order < 0 || order >= ORDER_COUNT) order = ORDER_YOURS;
    return LauncherIni_Store(ini, "row_order", kOrderWords[order]);
}

bool Row_SaveStartOn(LauncherIni *ini, bool last) { return LauncherIni_Store(ini, "start_on", last ? "last" : "first"); }

bool Row_SaveFavourite(LauncherIni *ini, const char *slug, bool on) { return SetFlag(ini, "fav.", slug, on); }

bool Row_SaveHidden(LauncherIni *ini, const char *slug, bool on) { return SetFlag(ini, "hide.", slug, on); }

bool Row_SavePlayed(LauncherIni *ini, const char *slug, long long when) {
    char key[LINI_KEY_LEN + 8], value[24];
    if (!Key(key, sizeof(key), "played.", slug) || !LauncherIni_Load(ini)) return false;
    snprintf(value, sizeof(value), "%lld", when);
    return LauncherIni_Set(ini, key, value) && LauncherIni_Set(ini, "last_played", slug) && LauncherIni_Save(ini);
}

bool Row_Move(LauncherIni *ini, const RowPrefs *p, const Lineup *mine, int index, int dir) {
    if (!Row_CanMove(p, mine, index, dir) || !LauncherIni_Load(ini)) return false;
    int at = Lineup_PlaceOf(mine, index);
    char key[LINI_KEY_LEN + 8];
    for (int k = 0; k < mine->count; k++) {
        int i = mine->index[k];
        int place = k == at ? at + dir : (k == at + dir ? at : k);
        if (Key(key, sizeof(key), "place.", p->m[i].slug) && !LauncherIni_SetInt(ini, key, place)) return false;
    }
    return LauncherIni_Save(ini);
}

static const char *const kRowPrefixes[] = {"fav.", "hide.", "place.", "played."};
#define ROW_PREFIXES ((int)(sizeof(kRowPrefixes) / sizeof(kRowPrefixes[0])))

bool Row_HasKeys(const LauncherIni *ini, const char *slug) {
    char key[LINI_KEY_LEN + 8];
    for (int i = 0; i < ROW_PREFIXES; i++)
        if (Key(key, sizeof(key), kRowPrefixes[i], slug) && LauncherIni_Get(ini, key)) return true;
    const char *last = LauncherIni_Get(ini, "last_played");
    return slug && slug[0] && last && strcmp(last, slug) == 0;
}

bool Row_CarryKeys(LauncherIni *ini, const char *from, const char *to) {
    char a[LINI_KEY_LEN + 8], b[LINI_KEY_LEN + 8];
    if (!from || !to || strcmp(from, to) == 0 || !LauncherIni_Load(ini)) return false;
    if (!Row_HasKeys(ini, from)) return true;
    for (int i = 0; i < ROW_PREFIXES; i++) {
        if (!Key(a, sizeof(a), kRowPrefixes[i], from)) continue;
        const char *v = LauncherIni_Get(ini, a);
        if (!v) continue;
        char value[LINI_LINE_LEN];
        snprintf(value, sizeof(value), "%s", v);
        LauncherIni_Unset(ini, a);
        if (Key(b, sizeof(b), kRowPrefixes[i], to) && !LauncherIni_Set(ini, b, value)) return false;
    }
    const char *last = LauncherIni_Get(ini, "last_played");
    if (last && strcmp(last, from) == 0 && !LauncherIni_Set(ini, "last_played", to)) return false;
    return LauncherIni_Save(ini);
}

long long Row_Played(const LauncherIni *ini, const char *slug) {
    char key[LINI_KEY_LEN + 8];
    return Key(key, sizeof(key), "played.", slug) ? GetLong(ini, key) : 0;
}
