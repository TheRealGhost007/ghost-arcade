#include "runrecord.h"
#include "localtable.h"
#include "persist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const BoardPreset kPresets[BOARD_PRESET_COUNT] = {
    {"Classic", 10, 20}, {"Wide", 14, 20}, {"Tall", 8, 26}, {"Huge", 18, 30}, {"Tiny", 6, 14}, {"Custom", 0, 0},
};

const BoardPreset *Record_Preset(int i) {
    return (i >= 0 && i < BOARD_PRESET_COUNT) ? &kPresets[i] : &kPresets[0];
}

int Record_PresetOf(int w, int h) {
    for (int i = 0; i < BOARD_PRESET_CUSTOM; i++)
        if (kPresets[i].w == w && kPresets[i].h == h) return i;
    return -1;
}

/* ---------------------------------------------------------------- values */

long Record_SprintValue(float seconds) {
    if (!(seconds >= 0.0f)) seconds = 0.0f;
    double centis = (double)seconds * 100.0 + 0.5;
    if (centis >= (double)RECORD_TIME_BASE) return 1; /* over a day: still a finish, just the slowest */
    long value = RECORD_TIME_BASE - (long)centis;
    return value < 1 ? 1 : value;
}

long Record_ValueCentis(long value) {
    if (value <= 0 || value > RECORD_TIME_BASE) return 0;
    return RECORD_TIME_BASE - value;
}

RecordUnit Record_UnitOf(GameMode mode) { return mode == MODE_SPRINT ? UNIT_TIME : UNIT_POINTS; }

void Record_FormatTime(long centis, char *out, size_t size) {
    if (centis < 0) centis = 0;
    snprintf(out, size, "%ld:%02ld.%02ld", centis / 6000, (centis / 100) % 60, centis % 100);
}

void Record_FormatValue(RecordUnit unit, long value, char *out, size_t size) {
    if (unit == UNIT_TIME) Record_FormatTime(Record_ValueCentis(value), out, size);
    else snprintf(out, size, "%ld", value);
}

void Record_FormatGap(RecordUnit unit, long better, long worse, char *out, size_t size) {
    long gap = better - worse;
    if (gap < 0) gap = -gap;
    if (unit == UNIT_TIME) snprintf(out, size, "%ld.%02ld s", gap / 100, gap % 100); /* both are hundredths */
    else snprintf(out, size, "%ld", gap);
}

/* ------------------------------------------------------------------ keys */

static void Key(const char *name, int w, int h, char *out, size_t size) {
    if (w == BOARD_W && h == BOARD_H) snprintf(out, size, "%s", name);
    else snprintf(out, size, "%s-%dx%d", name, w, h);
}

void Record_BoardKey(GameMode mode, int w, int h, char *out, size_t size) {
    static const char *const kNames[MODE_COUNT] = {"marathon", "sprint40", "ultra", "zen", "power"};
    Key(kNames[(int)mode >= 0 && mode < MODE_COUNT ? mode : MODE_MARATHON], w, h, out, size);
}

void Record_OldSprintKey(int w, int h, char *out, size_t size) { Key("sprint", w, h, out, size); }

void Record_LogKey(GameMode mode, int w, int h, char *out, size_t size) {
    static const char *const kNames[MODE_COUNT] = {"marathon", "sprint", "ultra", "zen", "power"};
    Key(kNames[(int)mode >= 0 && mode < MODE_COUNT ? mode : MODE_MARATHON], w, h, out, size);
}

/* ---------------------------------------------------------------- a run */

RunRecord Record_Classify(const Game *g, bool tuned) {
    RunRecord r;
    memset(&r, 0, sizeof(r));
    GameMode mode = g->cfg.mode;
    int w = g->cfg.width, h = g->cfg.height;
    r.unit = Record_UnitOf(mode);
    Record_BoardKey(mode, w, h, r.key, sizeof(r.key));

    /* A restart before anything landed, or a run that never scored, is not a
     * run (R used to farm the run-count trophies); a playtest never counts. */
    r.counts = !tuned && g->piecesLocked > 0 && g->score > 0;
    if (!r.counts) return r;

    r.toSave = mode == MODE_MARATHON && w == BOARD_W && h == BOARD_H;
    if (mode == MODE_ZEN) return r;           /* Zen keeps no scores */
    if (mode == MODE_SPRINT) {
        if (!g->won) return r;                /* only a finished Sprint has a time */
        r.value = Record_SprintValue(g->elapsed);
    } else {
        r.value = g->score;
    }
    r.toBoard = r.value > 0;
    r.local = Record_PresetOf(w, h) < 0;
    return r;
}

bool Record_Beats(long value, long best) { return value > 0 && value > best; }

long Record_ClassicBest(long saveDatBest, long tableBest) { return saveDatBest > tableBest ? saveDatBest : tableBest; }

EndSound Record_EndSound(bool won, bool newBest) {
    if (newBest) return END_SOUND_NEW_BEST;
    return won ? END_SOUND_CLEARED : END_SOUND_GAME_OVER;
}

/* ----------------------------------------------------------------- files */

/* ghostlink's scores/<slug>.txt (or scores/global/<slug>.txt, the world
 * board), found the way ghostlink finds them. */
static bool ScoresPath(bool global, char *out, size_t size) {
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *sub = global ? "/global" : "";
    int n;
    if (xdg && xdg[0]) n = snprintf(out, size, "%s/ghost-launcher/scores%s/%s.txt", xdg, sub, RECORD_SLUG);
    else {
        const char *home = getenv("HOME");
        if (!home || !home[0]) return false;
        n = snprintf(out, size, "%s/.local/share/ghost-launcher/scores%s/%s.txt", home, sub, RECORD_SLUG);
    }
    return n > 0 && (size_t)n < size;
}

static bool SharedPath(char *out, size_t size) { return ScoresPath(false, out, size); }

static bool LocalPath(char *out, size_t size) { return Persist_DataFile(LOCALTABLE_FILE, out, size); }

int Record_SharedRows(void) {
    char path[700];
    if (!SharedPath(path, sizeof(path))) return 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[512];
    int rows = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r' || line[0] == '\0') continue;
        /* As ghostlink: four fields and a positive score make a row. */
        char *p = line;
        int seps = 0;
        const char *score = NULL;
        for (; *p && seps < 3; p++) {
            if (*p == '|') { seps++; if (seps == 2) score = p + 1; }
        }
        if (seps == 3 && score && atol(score) > 0) rows++;
    }
    fclose(f);
    return rows;
}

bool Record_SharedHasRoom(void) { return Record_SharedRows() < GHOSTLINK_MAX_ENTRIES; }

long Record_LoadBest(GameMode mode, int w, int h, long saveDatBest) {
    if (mode == MODE_ZEN) return 0;
    char key[GHOSTLINK_MODE_LEN], path[700];
    Record_BoardKey(mode, w, h, key, sizeof(key));
    long best = 0;
    GhostScore top;
    if (GhostLink_LoadScores(RECORD_SLUG, key, &top, 1) > 0) best = top.score;
    if (LocalPath(path, sizeof(path)) && LocalTable_Load(path, key, &top, 1) > 0 && top.score > best) best = top.score;
    if (mode == MODE_MARATHON && w == BOARD_W && h == BOARD_H) best = Record_ClassicBest(saveDatBest, best);
    return best;
}

long Record_LoadYourBest(GameMode mode, int w, int h, const char *username, long saveDatBest, int *rank) {
    if (rank) *rank = 0;
    if (mode == MODE_ZEN) return 0;
    char key[GHOSTLINK_MODE_LEN];
    Record_BoardKey(mode, w, h, key, sizeof(key));
    GhostScore rows[GHOSTLINK_TOP_N];
    int n = Record_LoadBoard(key, false, rows, GHOSTLINK_TOP_N);
    long best = 0;
    int place = 0;
    for (int i = 0; username && i < n; i++) {
        if (strcmp(rows[i].username, username) != 0) continue;
        best = rows[i].score;
        place = i + 1;
        break;
    }
    if (mode == MODE_MARATHON && w == BOARD_W && h == BOARD_H && saveDatBest > best) {
        best = saveDatBest;
        place = 1;
        for (int i = 0; i < n; i++) if (rows[i].score > best) place++;
    }
    if (rank) *rank = place;
    return best;
}

int Record_LoadBoard(const char *key, bool world, GhostScore *out, int maxOut) {
    if (!key || maxOut <= 0) return 0;
    if (world) return GhostLink_LoadGlobalScores(RECORD_SLUG, key, out, maxOut);
    GhostScore shared[GHOSTLINK_TOP_N], mine[GHOSTLINK_TOP_N];
    int ns = GhostLink_LoadScores(RECORD_SLUG, key, shared, GHOSTLINK_TOP_N);
    char path[700];
    int nm = LocalPath(path, sizeof(path)) ? LocalTable_Load(path, key, mine, GHOSTLINK_TOP_N) : 0;
    /* Both are best first: merge them, the shared table first on a tie. */
    int i = 0, j = 0, n = 0;
    while (n < maxOut && (i < ns || j < nm)) {
        if (j >= nm || (i < ns && shared[i].score >= mine[j].score)) out[n++] = shared[i++];
        else out[n++] = mine[j++];
    }
    return n;
}

int Record_Submit(const RunRecord *r, const char *username, const char *date, bool *shared) {
    if (shared) *shared = false;
    if (!r || !r->toBoard) return 0;
    if (!r->local && Record_SharedHasRoom()) {
        if (shared) *shared = true;
        return GhostLink_SubmitScore(RECORD_SLUG, r->key, username, r->value, date);
    }
    char path[700];
    if (!LocalPath(path, sizeof(path))) return 0;
    return LocalTable_Submit(path, r->key, username, r->value, date);
}

int Record_FindRow(const GhostScore *rows, int count, const char *username, long value, const char *date) {
    for (int i = 0; i < count; i++) {
        if (rows[i].score != value) continue;
        if (username && strcmp(rows[i].username, username) != 0) continue;
        if (date && strcmp(rows[i].date, date) != 0) continue;
        return i + 1;
    }
    return 0;
}

/* ------------------------------------------------------- boards listed */

bool Record_ParseKey(const char *key, GameMode *mode, bool *oldSprint, int *w, int *h) {
    static const struct { const char *name; GameMode mode; bool old; } kNames[] = {
        {"marathon", MODE_MARATHON, false}, {"sprint40", MODE_SPRINT, false}, {"sprint", MODE_SPRINT, true},
        {"ultra", MODE_ULTRA, false}, {"zen", MODE_ZEN, false}, {"power", MODE_POWER, false},
    };
    if (!key || !key[0]) return false;
    const char *dash = strchr(key, '-');
    size_t n = dash ? (size_t)(dash - key) : strlen(key);
    int found = -1;
    for (int i = 0; i < (int)(sizeof(kNames) / sizeof(kNames[0])); i++)
        if (strlen(kNames[i].name) == n && strncmp(key, kNames[i].name, n) == 0) found = i;
    if (found < 0) return false;
    int bw = BOARD_W, bh = BOARD_H;
    if (dash) {
        char tail;
        if (sscanf(dash + 1, "%dx%d%c", &bw, &bh, &tail) != 2) return false;
        if (bw < BOARD_MIN_W || bw > BOARD_MAX_W || bh < BOARD_MIN_H || bh > BOARD_MAX_H) return false;
        if (bw == BOARD_W && bh == BOARD_H) return false; /* the classic board never has a size suffix */
    }
    if (mode) *mode = kNames[found].mode;
    if (oldSprint) *oldSprint = kNames[found].old;
    if (w) *w = bw;
    if (h) *h = bh;
    return true;
}

static void AddSize(BoardSize *out, int *n, int max, int w, int h) {
    for (int i = 0; i < *n; i++) if (out[i].w == w && out[i].h == h) return;
    if (*n < max) out[(*n)++] = (BoardSize){w, h};
}

/* Every board key in one score file (row format mode|username|score|date). */
static void ScanFile(const char *path, GameMode mode, bool oldSprint, BoardSize *out, int *n, int max) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[512];
    int rows = 0;
    while (fgets(line, sizeof(line), f) && rows < 8192) {
        rows++;
        if (line[0] == '#') continue;
        char *bar = strchr(line, '|');
        if (!bar) continue;
        *bar = '\0';
        GameMode m;
        bool old;
        int w, h;
        if (Record_ParseKey(line, &m, &old, &w, &h) && m == mode && old == oldSprint) AddSize(out, n, max, w, h);
    }
    fclose(f);
}

static int SizeOrder(const BoardSize *s) {
    int p = Record_PresetOf(s->w, s->h);
    return p >= 0 ? p : BOARD_PRESET_COUNT;
}

int Record_BoardsWithRows(GameMode mode, bool oldSprint, BoardSize *out, int max) {
    int n = 0;
    char path[700];
    if (ScoresPath(false, path, sizeof(path))) ScanFile(path, mode, oldSprint, out, &n, max);
    if (LocalPath(path, sizeof(path))) ScanFile(path, mode, oldSprint, out, &n, max);
    if (ScoresPath(true, path, sizeof(path))) ScanFile(path, mode, oldSprint, out, &n, max);
    /* Presets in their order, then by width and height (a short list: an
     * insertion sort). */
    for (int i = 1; i < n; i++) {
        BoardSize v = out[i];
        int j = i - 1;
        while (j >= 0) {
            int a = SizeOrder(&out[j]), b = SizeOrder(&v);
            bool after = a > b || (a == b && (out[j].w > v.w || (out[j].w == v.w && out[j].h > v.h)));
            if (!after) break;
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = v;
    }
    return n;
}
