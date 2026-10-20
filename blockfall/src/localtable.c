#include "localtable.h"
#include "safefile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX_LEN 1024

typedef struct {
    GhostScore s;
    int order; /* place in the file; the new run comes last, so it ranks under an equal score */
} Row;

typedef struct {
    Row *rows;
    int count, cap;
    char **raw; /* lines that are not rows (kept verbatim) */
    int rawCount, rawCap;
} Table;

static void Sanitize(char *out, size_t size, const char *in) {
    snprintf(out, size, "%s", in ? in : "");
    for (char *p = out; *p; p++)
        if ((unsigned char)*p < 32 || *p == '|' || (unsigned char)*p == 127) *p = '?';
}

/* "mode|username|score|date" into a score; false if it is not one. */
static bool ParseRow(char *line, GhostScore *out) {
    char *fields[4];
    int n = 0;
    char *cursor = line;
    while (n < 4) {
        fields[n++] = cursor;
        char *sep = strchr(cursor, '|');
        if (!sep) break;
        *sep = '\0';
        cursor = sep + 1;
    }
    if (n < 4 || !fields[0][0] || strlen(fields[0]) >= GHOSTLINK_MODE_LEN) return false;
    char *end = NULL;
    long score = strtol(fields[2], &end, 10);
    if (end == fields[2] || *end != '\0' || score <= 0) return false;
    memset(out, 0, sizeof(*out));
    snprintf(out->mode, sizeof(out->mode), "%s", fields[0]);
    snprintf(out->username, sizeof(out->username), "%.*s", (int)sizeof(out->username) - 1, fields[1]);
    out->score = score;
    snprintf(out->date, sizeof(out->date), "%.*s", (int)sizeof(out->date) - 1, fields[3]);
    return true;
}

static void TrimEnd(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = '\0';
}

static void FreeTable(Table *t) {
    free(t->rows);
    for (int i = 0; i < t->rawCount; i++) free(t->raw[i]);
    free(t->raw);
    memset(t, 0, sizeof(*t));
}

static bool AddRow(Table *t, const GhostScore *s) {
    if (t->count == t->cap) {
        int cap = t->cap ? t->cap * 2 : 64;
        Row *grown = realloc(t->rows, (size_t)cap * sizeof(Row));
        if (!grown) return false;
        t->rows = grown;
        t->cap = cap;
    }
    t->rows[t->count].s = *s;
    t->rows[t->count].order = t->count;
    t->count++;
    return true;
}

static bool AddRaw(Table *t, const char *line) {
    if (t->rawCount == t->rawCap) {
        int cap = t->rawCap ? t->rawCap * 2 : 8;
        char **grown = realloc(t->raw, (size_t)cap * sizeof(char *));
        if (!grown) return false;
        t->raw = grown;
        t->rawCap = cap;
    }
    size_t n = strlen(line) + 1;
    char *copy = malloc(n);
    if (!copy) return false;
    memcpy(copy, line, n);
    t->raw[t->rawCount++] = copy;
    return true;
}

/* Reads the whole file. False when it cannot be read in full: a read error,
 * a line too long for the buffer, too many rows, or no memory. A missing
 * file is an empty table. */
static bool ReadTable(const char *path, Table *t) {
    memset(t, 0, sizeof(*t));
    FILE *f = fopen(path, "r");
    if (!f) return true;
    char line[LINE_MAX_LEN];
    bool ok = true;
    while (ok && fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len == sizeof(line) - 1 && line[len - 1] != '\n' && !feof(f)) { ok = false; break; }
        TrimEnd(line);
        if (line[0] == '\0' || line[0] == '#') continue; /* our own header is written again */
        char work[LINE_MAX_LEN];
        snprintf(work, sizeof(work), "%s", line);
        GhostScore s;
        if (ParseRow(work, &s)) ok = AddRow(t, &s);
        else ok = AddRaw(t, line);
        if (t->count + t->rawCount > LOCALTABLE_MAX_ROWS) ok = false;
    }
    if (ferror(f)) ok = false;
    fclose(f);
    if (!ok) FreeTable(t);
    return ok;
}

/* Mode A-Z, then the best first; equal scores keep their order. */
static int RowCmp(const void *a, const void *b) {
    const Row *x = a, *y = b;
    int c = strcmp(x->s.mode, y->s.mode);
    if (c) return c;
    if (x->s.score != y->s.score) return x->s.score > y->s.score ? -1 : 1;
    return x->order - y->order;
}

int LocalTable_CountRows(const char *path) {
    FILE *f = path ? fopen(path, "r") : NULL;
    if (!f) return 0;
    char line[LINE_MAX_LEN];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        TrimEnd(line);
        if (line[0] != '\0' && line[0] != '#') n++;
    }
    fclose(f);
    return n;
}

int LocalTable_Load(const char *path, const char *mode, GhostScore *out, int maxOut) {
    if (!path || !mode || maxOut <= 0) return 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[LINE_MAX_LEN];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        TrimEnd(line);
        GhostScore s;
        if (line[0] == '#' || !ParseRow(line, &s) || strcmp(s.mode, mode) != 0) continue;
        /* Keep the best maxOut, best first; an equal score goes after. */
        int at = n;
        while (at > 0 && out[at - 1].score < s.score) at--;
        if (at >= maxOut) continue;
        int last = n < maxOut ? n : maxOut - 1;
        for (int i = last; i > at; i--) out[i] = out[i - 1];
        out[at] = s;
        if (n < maxOut) n++;
    }
    fclose(f);
    return n;
}

int LocalTable_Submit(const char *path, const char *mode, const char *username, long score, const char *date) {
    if (!path || !mode || !mode[0] || score <= 0) return 0;
    Table t;
    if (!ReadTable(path, &t)) return 0; /* never rewrite a file from part of it */

    GhostScore added;
    memset(&added, 0, sizeof(added));
    Sanitize(added.mode, sizeof(added.mode), mode);
    Sanitize(added.username, sizeof(added.username), (username && username[0]) ? username : GHOSTLINK_DEFAULT_NAME);
    added.score = score;
    Sanitize(added.date, sizeof(added.date), date ? date : "");
    if (!AddRow(&t, &added)) { FreeTable(&t); return 0; }
    int addedOrder = t.count - 1;

    qsort(t.rows, (size_t)t.count, sizeof(Row), RowCmp);

    int rank = 0, inMode = 0;
    for (int i = 0; i < t.count; i++) {
        if (strcmp(t.rows[i].s.mode, added.mode) != 0) continue;
        inMode++;
        if (t.rows[i].order == addedOrder) rank = inMode;
    }
    if (rank > GHOSTLINK_TOP_N) rank = 0;

    FILE *f = SafeFile_Open(path);
    if (!f) { FreeTable(&t); return 0; }
    fprintf(f, "# Blockfall scores kept on this machine only (custom board sizes): mode|username|score|date\n");
    const char *current = "";
    int kept = 0;
    for (int i = 0; i < t.count; i++) {
        if (strcmp(t.rows[i].s.mode, current) != 0) { current = t.rows[i].s.mode; kept = 0; }
        if (kept >= GHOSTLINK_TOP_N) continue;
        fprintf(f, "%s|%s|%ld|%s\n", t.rows[i].s.mode, t.rows[i].s.username, t.rows[i].s.score, t.rows[i].s.date);
        kept++;
    }
    for (int i = 0; i < t.rawCount; i++) fprintf(f, "%s\n", t.raw[i]);
    bool saved = SafeFile_Close(f);
    FreeTable(&t);
    return saved ? rank : 0;
}
