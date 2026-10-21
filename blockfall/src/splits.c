#include "splits.h"
#include "persist.h"
#include "runrecord.h"
#include "safefile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_LEN 256
#define MAX_LINES 256

int Splits_MarkLines(int i) { return (i + 1) * (SPRINT_LINES / SPLIT_MARKS); }

long Splits_Centis(float seconds) {
    if (seconds < 0.0f) seconds = 0.0f;
    return (long)(seconds * 100.0f + 0.5f);
}

static void Key(int w, int h, char *out, size_t n) { Record_BoardKey(MODE_SPRINT, w, h, out, n); }

/* "sprint40-14x20|1012|2140|3300|4210" */
static bool ParseLine(const char *line, const char *key, SprintPace *out) {
    size_t kl = strlen(key);
    if (strncmp(line, key, kl) != 0 || line[kl] != '|') return false;
    const char *p = line + kl;
    SprintPace pace;
    memset(&pace, 0, sizeof(pace));
    for (int i = 0; i < SPLIT_MARKS; i++) {
        if (*p != '|') return false;
        char *end = NULL;
        long v = strtol(p + 1, &end, 10);
        if (end == p + 1 || v <= 0) return false;
        pace.at[i] = v;
        p = end;
    }
    pace.have = true;
    *out = pace;
    return true;
}

static void TrimEnd(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) s[--n] = '\0';
}

bool Splits_Load(int w, int h, SprintPace *out) {
    memset(out, 0, sizeof(*out));
    char path[600], key[GHOSTLINK_MODE_LEN];
    if (!Persist_DataFile(SPLITS_FILE, path, sizeof(path))) return false;
    Key(w, h, key, sizeof(key));
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[LINE_LEN];
    bool found = false;
    while (!found && fgets(line, sizeof(line), f)) {
        TrimEnd(line);
        found = ParseLine(line, key, out);
    }
    fclose(f);
    return found;
}

bool Splits_Save(int w, int h, const SprintPace *p) {
    if (!p || !p->have) return false;
    char path[600], key[GHOSTLINK_MODE_LEN];
    if (!Persist_DataFile(SPLITS_FILE, path, sizeof(path))) return false;
    Key(w, h, key, sizeof(key));
    /* Every other line stays as it is; this board's is replaced or added. */
    static char kept[MAX_LINES][LINE_LEN];
    int n = 0;
    FILE *in = fopen(path, "r");
    if (in) {
        char line[LINE_LEN];
        while (n < MAX_LINES && fgets(line, sizeof(line), in)) {
            TrimEnd(line);
            SprintPace scratch;
            if (!line[0] || ParseLine(line, key, &scratch)) continue;
            snprintf(kept[n++], LINE_LEN, "%s", line);
        }
        fclose(in);
    }
    FILE *out = SafeFile_Open(path);
    if (!out) return false;
    bool header = false;
    for (int i = 0; i < n && !header; i++) header = kept[i][0] == '#';
    if (!header) fprintf(out, "# Blockfall Sprint best pace: board|at 10|at 20|at 30|finish (hundredths of a second)\n");
    for (int i = 0; i < n; i++) fprintf(out, "%s\n", kept[i]);
    fprintf(out, "%s", key);
    for (int i = 0; i < SPLIT_MARKS; i++) fprintf(out, "|%ld", p->at[i]);
    fprintf(out, "\n");
    return SafeFile_Close(out);
}

bool Splits_Better(const SprintPace *run, const SprintPace *best) {
    if (!run || !run->have) return false;
    if (!best || !best->have) return true;
    return run->at[SPLIT_MARKS - 1] < best->at[SPLIT_MARKS - 1];
}

void Splits_Reset(Splits *s, const SprintPace *best) {
    memset(s, 0, sizeof(*s));
    if (best) s->best = *best;
    s->age = 1000.0f;
}

bool Splits_React(Splits *s, const Game *g) {
    s->just = false;
    if (g->cfg.mode != MODE_SPRINT) return false;
    int lines = g->linesCleared;
    if (lines < s->lastLines) { /* a restart the caller did not announce */
        s->marks = 0;
        memset(&s->run, 0, sizeof(s->run));
    }
    s->lastLines = lines;
    long now = Splits_Centis(g->elapsed);
    while (s->marks < SPLIT_MARKS && lines >= Splits_MarkLines(s->marks)) {
        s->run.at[s->marks++] = now;
        s->just = true;
        s->age = 0.0f;
    }
    if (s->marks == SPLIT_MARKS) s->run.have = true;
    return s->just;
}

void Splits_Tick(Splits *s, float dt) {
    if (s->age < 1000.0f) s->age += dt;
}

void Splits_Consume(Splits *s) { s->just = false; }

int Splits_Last(const Splits *s) { return s->marks - 1; }

bool Splits_Diff(const Splits *s, int mark, long *centis) {
    if (mark < 0 || mark >= s->marks || !s->best.have) return false;
    if (centis) *centis = s->run.at[mark] - s->best.at[mark];
    return true;
}

/* Tenths, rounded half away from zero. */
static long Tenths(long centis) {
    long a = centis < 0 ? -centis : centis;
    long t = (a + 5) / 10;
    return centis < 0 ? -t : t;
}

void Splits_FormatGap(long centis, char *out, size_t n) {
    long t = Tenths(centis);
    if (t < 0) t = -t;
    snprintf(out, n, "%ld.%ld s", t / 10, t % 10);
}

void Splits_FormatDiff(long centis, char *out, size_t n) {
    long t = Tenths(centis);
    if (t == 0) { snprintf(out, n, "0.0 s"); return; }
    char gap[16];
    Splits_FormatGap(centis, gap, sizeof(gap));
    snprintf(out, n, "%c%s", t < 0 ? '-' : '+', gap);
}

bool Splits_Text(const Splits *s, char *out, size_t n, bool *gold) {
    int mark = Splits_Last(s);
    if (gold) *gold = false;
    if (mark < 0) { if (n) out[0] = '\0'; return false; }
    long diff;
    char v[24];
    if (Splits_Diff(s, mark, &diff)) {
        Splits_FormatDiff(diff, v, sizeof(v));
        if (gold) *gold = diff < 0;
    } else {
        Record_FormatTime(s->run.at[mark], v, sizeof(v));
    }
    snprintf(out, n, "%d: %s", Splits_MarkLines(mark), v);
    return true;
}
