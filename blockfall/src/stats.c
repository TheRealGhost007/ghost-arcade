#include "stats.h"
#include "persist.h"
#include "runrecord.h"
#include "safefile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_LEN 256

static const char *const kModeKeys[MODE_COUNT] = {"marathon", "sprint", "ultra", "zen", "power", "dig"};
static const char *const kBarNames[STATS_BAR_PARTS] = {"Single", "Double", "Triple", "Quad", "T-spin"};

void Stats_Clear(LifeStats *s) { memset(s, 0, sizeof(*s)); }

const char *Stats_ModeKey(GameMode mode) { return ((int)mode >= 0 && mode < MODE_COUNT) ? kModeKeys[mode] : kModeKeys[0]; }

const char *Stats_BarName(int part) { return (part >= 0 && part < STATS_BAR_PARTS) ? kBarNames[part] : ""; }

/* ----------------------------------------------------------------- file */

static void TrimEnd(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = '\0';
}

static void CopyDate(char *out, const char *date) {
    snprintf(out, STATS_DATE_LEN, "%s", date ? date : "");
}

/* "7|2026-10-20": a record's value and day. */
static void ParseRecord(const char *v, StatRecord *r) {
    char *end = NULL;
    long value = strtol(v, &end, 10);
    if (end == v || value < 0) return;
    r->value = value;
    if (*end == '|') CopyDate(r->date, end + 1);
    else r->date[0] = '\0';
}

/* "10|20|5|12": up to four counts, into out[1..4]. */
static void ParseCounts(const char *v, long out[5]) {
    const char *p = v;
    for (int i = 1; i <= 4; i++) {
        char *end = NULL;
        long n = strtol(p, &end, 10);
        if (end == p) return;
        out[i] = n < 0 ? 0 : n;
        if (*end != '|') return;
        p = end + 1;
    }
}

static long Count(const char *v) {
    long n = atol(v);
    return n < 0 ? 0 : n;
}

static void ReadLine(LifeStats *s, int *mode, const char *key, const char *v) {
    if (strcmp(key, "since") == 0) { CopyDate(s->since, v); return; }
    if (*mode < 0) return;
    ModeStats *m = &s->mode[*mode];
    if (strcmp(key, "runs") == 0) m->runs = Count(v);
    else if (strcmp(key, "lines") == 0) m->lines = Count(v);
    else if (strcmp(key, "pieces") == 0) m->pieces = Count(v);
    else if (strcmp(key, "centis") == 0) m->centis = Count(v);
    else if (strcmp(key, "clears") == 0) ParseCounts(v, m->clears);
    else if (strcmp(key, "tspin_clears") == 0) ParseCounts(v, m->tspinClears);
    else if (strcmp(key, "tspins") == 0) m->tspins = Count(v);
    else if (strcmp(key, "perfect") == 0) m->perfects = Count(v);
    else if (strcmp(key, "powers") == 0) m->powers = Count(v);
    else if (strcmp(key, "holds") == 0) m->holds = Count(v);
    else if (strcmp(key, "best_pps") == 0) ParseRecord(v, &m->pps);
    else if (strcmp(key, "best_combo") == 0) ParseRecord(v, &m->combo);
    else if (strcmp(key, "most_quads") == 0) ParseRecord(v, &m->quads);
    /* anything else: a later build's line, kept out of the way */
}

bool Stats_Load(LifeStats *s) {
    Stats_Clear(s);
    char path[600];
    if (!Persist_DataFile(STATS_FILE, path, sizeof(path))) return false;
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[LINE_LEN];
    int mode = -1; /* the section; -1 before the first or under an unknown one */
    while (fgets(line, sizeof(line), f)) {
        TrimEnd(line);
        if (!line[0] || line[0] == '#') continue;
        if (line[0] == '[') {
            char *close = strchr(line, ']');
            if (!close) continue;
            *close = '\0';
            mode = -1;
            for (int i = 0; i < MODE_COUNT; i++) if (strcmp(line + 1, kModeKeys[i]) == 0) mode = i;
            continue;
        }
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        ReadLine(s, &mode, line, eq + 1);
    }
    fclose(f);
    return true;
}

static void WriteRecord(FILE *f, const char *key, const StatRecord *r) {
    if (r->value <= 0) return; /* no record yet: no line */
    fprintf(f, "%s=%ld|%s\n", key, r->value, r->date);
}

bool Stats_Save(const LifeStats *s) {
    char path[600];
    if (!Persist_DataFile(STATS_FILE, path, sizeof(path))) return false;
    FILE *f = SafeFile_Open(path);
    if (!f) return false;
    fprintf(f, "# Blockfall lifetime stats: every run that counted on this machine since the day below (3.3.0)\n");
    fprintf(f, "# clears and tspin_clears: locks that cleared 1|2|3|4 rows; records: value|day\n");
    fprintf(f, "since=%s\n", s->since);
    for (int i = 0; i < MODE_COUNT; i++) {
        const ModeStats *m = &s->mode[i];
        fprintf(f, "[%s]\n", kModeKeys[i]);
        fprintf(f, "runs=%ld\nlines=%ld\npieces=%ld\ncentis=%ld\n", m->runs, m->lines, m->pieces, m->centis);
        fprintf(f, "clears=%ld|%ld|%ld|%ld\n", m->clears[1], m->clears[2], m->clears[3], m->clears[4]);
        fprintf(f, "tspin_clears=%ld|%ld|%ld|%ld\n", m->tspinClears[1], m->tspinClears[2], m->tspinClears[3], m->tspinClears[4]);
        fprintf(f, "tspins=%ld\nperfect=%ld\npowers=%ld\nholds=%ld\n", m->tspins, m->perfects, m->powers, m->holds);
        WriteRecord(f, "best_pps", &m->pps);
        WriteRecord(f, "best_combo", &m->combo);
        WriteRecord(f, "most_quads", &m->quads);
    }
    return SafeFile_Close(f);
}

/* ---------------------------------------------------------------- a run */

long Stats_RunPps(const Game *g) {
    if (g->piecesLocked < STATS_PPS_MIN_PIECES || !(g->elapsed > 0.0f)) return 0;
    double pps = (double)g->piecesLocked / (double)g->elapsed;
    return (long)(pps * 1000.0 + 0.5);
}

static void Beat(StatRecord *r, long value, const char *date) {
    if (value <= r->value) return;
    r->value = value;
    CopyDate(r->date, date);
}

void Stats_Add(ModeStats *m, const Game *g, const char *date) {
    m->runs++;
    m->lines += g->linesCleared;
    m->pieces += g->piecesLocked;
    m->centis += g->elapsed > 0.0f ? (long)((double)g->elapsed * 100.0 + 0.5) : 0;
    for (int k = 1; k <= 4; k++) {
        m->clears[k] += g->stats.clears[k];
        m->tspinClears[k] += g->stats.tspinClears[k];
    }
    m->tspins += g->stats.tspins;
    m->perfects += g->stats.perfectClears;
    m->powers += g->stats.powers;
    m->holds += g->stats.holds;
    Beat(&m->pps, Stats_RunPps(g), date);
    Beat(&m->combo, g->stats.bestCombo, date);
    Beat(&m->quads, g->stats.clears[4], date);
}

bool Stats_Fold(LifeStats *s, const Game *g, bool tuned, const char *date) {
    RunRecord r = Record_Classify(g, tuned);
    if (!r.counts) return false;
    GameMode mode = g->cfg.mode;
    if ((int)mode < 0 || mode >= MODE_COUNT) return false;
    if (!s->since[0]) CopyDate(s->since, date);
    Stats_Add(&s->mode[mode], g, date);
    return true;
}

/* ------------------------------------------------------------ the view */

static void Best(StatRecord *out, int *who, const StatRecord *r, int mode) {
    if (r->value <= out->value) return;
    *out = *r;
    if (who) *who = mode;
}

void Stats_Total(const LifeStats *s, ModeStats *out, int who[3]) {
    memset(out, 0, sizeof(*out));
    if (who) who[0] = who[1] = who[2] = -1;
    for (int i = 0; i < MODE_COUNT; i++) {
        const ModeStats *m = &s->mode[i];
        out->runs += m->runs;
        out->lines += m->lines;
        out->pieces += m->pieces;
        out->centis += m->centis;
        for (int k = 1; k <= 4; k++) {
            out->clears[k] += m->clears[k];
            out->tspinClears[k] += m->tspinClears[k];
        }
        out->tspins += m->tspins;
        out->perfects += m->perfects;
        out->powers += m->powers;
        out->holds += m->holds;
        Best(&out->pps, who ? &who[0] : NULL, &m->pps, i);
        Best(&out->combo, who ? &who[1] : NULL, &m->combo, i);
        Best(&out->quads, who ? &who[2] : NULL, &m->quads, i);
    }
}

void Stats_Bar(const ModeStats *m, long parts[STATS_BAR_PARTS]) {
    long tspinLines = 0;
    for (int k = 1; k <= 3; k++) {
        long plain = m->clears[k] - m->tspinClears[k];
        parts[k - 1] = (plain > 0 ? plain : 0) * k; /* a hand-edited file can not make it negative */
        tspinLines += (m->tspinClears[k] > 0 ? m->tspinClears[k] : 0) * k;
    }
    parts[3] = (m->clears[4] > 0 ? m->clears[4] : 0) * 4;
    parts[4] = tspinLines;
}

long Stats_BarTotal(const long parts[STATS_BAR_PARTS]) {
    long total = 0;
    for (int i = 0; i < STATS_BAR_PARTS; i++) total += parts[i];
    return total;
}

void Stats_FormatTime(long centis, char *out, size_t n) {
    long s = centis > 0 ? centis / 100 : 0;
    if (s < 60) snprintf(out, n, "%lds", s);
    else if (s < 3600) snprintf(out, n, "%ldm %02lds", s / 60, s % 60);
    else snprintf(out, n, "%ldh %02ldm", s / 3600, (s / 60) % 60);
}

void Stats_FormatPps(long ppsMilli, char *out, size_t n) {
    long c = ppsMilli > 0 ? (ppsMilli + 5) / 10 : 0;
    snprintf(out, n, "%ld.%02ld", c / 100, c % 100);
}
