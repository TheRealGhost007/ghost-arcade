#include "boards.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void Boards_Clear(Boards *bs) { memset(bs, 0, sizeof(*bs)); }

bool Boards_IsDaily(const char *mode) {
    if (!mode || strncmp(mode, "daily-", 6) != 0 || strlen(mode) != 14) return false;
    for (int i = 6; i < 14; i++) if (mode[i] < '0' || mode[i] > '9') return false;
    return true;
}

static bool IsYou(const BoardYou *you, const char *name) {
    for (int i = 0; you && i < BOARD_YOU_MAX; i++)
        if (you->names[i] && you->names[i][0] && strcasecmp(you->names[i], name) == 0) return true;
    return false;
}

static Board *BoardFor(Boards *bs, const char *mode) {
    int at = Boards_Find(bs, mode);
    if (at >= 0) return &bs->b[at];
    if (bs->count >= BOARD_MAX) return NULL;
    Board *b = &bs->b[bs->count++];
    memset(b, 0, sizeof(*b));
    snprintf(b->mode, sizeof(b->mode), "%s", mode);
    return b;
}

/* Best first; an equal score goes after the ones already there, so the
 * world board keeps the server's order for ties. */
static void Insert(BoardRow *rows, int *count, const BoardRow *row) {
    int at = *count;
    while (at > 0 && rows[at - 1].score < row->score) at--;
    if (at >= BOARD_TOP) return;
    int last = *count < BOARD_TOP ? *count : BOARD_TOP - 1;
    for (int i = last; i > at; i--) rows[i] = rows[i - 1];
    rows[at] = *row;
    if (*count < BOARD_TOP) (*count)++;
}

/* "mode|name|score|date" into its fields, in place. */
static bool Split(char *line, char *fields[4]) {
    int n = 0;
    char *cursor = line;
    while (n < 4) {
        fields[n++] = cursor;
        char *sep = strchr(cursor, '|');
        if (!sep) break;
        *sep = '\0';
        cursor = sep + 1;
    }
    return n == 4;
}

static void AddLine(Boards *bs, char *line, bool world, const BoardYou *you, const char *dailyCutoff) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
    char *f[4];
    if (line[0] == '\0' || line[0] == '#' || !Split(line, f)) return;
    if (!f[0][0] || strlen(f[0]) >= SCORES_MODE_LEN) return;
    char *end = NULL;
    long score = strtol(f[2], &end, 10);
    if (end == f[2] || score <= 0) return;
    if (dailyCutoff && Boards_IsDaily(f[0]) && strcmp(f[0], dailyCutoff) < 0) return; /* the games drop these too */
    Board *b = BoardFor(bs, f[0]);
    if (!b) return;
    BoardRow row;
    memset(&row, 0, sizeof(row));
    snprintf(row.name, sizeof(row.name), "%.*s", BOARD_NAME_LEN - 1, f[1]);
    snprintf(row.date, sizeof(row.date), "%.10s", f[3]);
    row.score = score;
    row.you = IsYou(you, row.name);
    if (world) Insert(b->world, &b->worldCount, &row);
    else Insert(b->local, &b->localCount, &row);
}

void Boards_AddText(Boards *bs, const char *text, bool world, const BoardYou *you, const char *dailyCutoff) {
    const char *p = text ? text : "";
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        char line[512];
        snprintf(line, sizeof(line), "%.*s", (int)(n < sizeof(line) - 1 ? n : sizeof(line) - 1), p);
        AddLine(bs, line, world, you, dailyCutoff);
        p = nl ? nl + 1 : p + n;
    }
}

/* Regular boards A-Z, then the dailies, newest first. */
static int BoardOrder(const Board *a, const Board *b) {
    bool da = Boards_IsDaily(a->mode), db = Boards_IsDaily(b->mode);
    if (da != db) return da ? 1 : -1;
    int c = strcmp(a->mode, b->mode);
    return da ? -c : c;
}

void Boards_Finish(Boards *bs) {
    for (int i = 1; i < bs->count; i++) { /* a few dozen boards: insertion is plenty */
        Board moving = bs->b[i];
        int j = i;
        while (j > 0 && BoardOrder(&moving, &bs->b[j - 1]) < 0) {
            bs->b[j] = bs->b[j - 1];
            j--;
        }
        bs->b[j] = moving;
    }
    for (int i = 0; i < bs->count; i++) {
        Board *b = &bs->b[i];
        b->worldRank = 0;
        for (int r = 0; r < b->worldCount && !b->worldRank; r++) if (b->world[r].you) b->worldRank = r + 1;
    }
}

static void AddFile(Boards *bs, const char *slug, bool world, const BoardYou *you, const char *dailyCutoff) {
    char path[700];
    if (!Scores_FilePath(slug, world, path, sizeof(path))) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) AddLine(bs, line, world, you, dailyCutoff);
    fclose(f);
}

void Boards_Load(Boards *bs, const char *slug, const BoardYou *you, const char *dailyCutoff) {
    Boards_Clear(bs);
    if (!slug || !slug[0]) return;
    snprintf(bs->slug, sizeof(bs->slug), "%s", slug);
    AddFile(bs, slug, false, you, dailyCutoff);
    AddFile(bs, slug, true, you, dailyCutoff);
    Boards_Finish(bs);
    for (int i = 0; i < bs->count; i++) bs->b[i].timed = Scores_IsTimeBoard(slug, bs->b[i].mode);
}

int Boards_Find(const Boards *bs, const char *mode) {
    for (int i = 0; mode && i < bs->count; i++) if (strcmp(bs->b[i].mode, mode) == 0) return i;
    return -1;
}

void Boards_ModeWords(const char *mode, const char *today, char *out, size_t size) {
    static const char *const kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (!mode) mode = "";
    if (Boards_IsDaily(mode)) {
        int month = (mode[10] - '0') * 10 + (mode[11] - '0'), day = (mode[12] - '0') * 10 + (mode[13] - '0');
        if (today && strcmp(mode, today) == 0) snprintf(out, size, "Today's daily");
        else if (month >= 1 && month <= 12) snprintf(out, size, "Daily %d %s", day, kMonths[month - 1]);
        else snprintf(out, size, "Daily");
        return;
    }
    snprintf(out, size, "%s", mode);
    for (char *p = out; *p; p++) if (*p == '-' || *p == '_') *p = ' ';
    if (out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 'a' + 'A');
}

void Boards_ModeWordsFor(const char *slug, const char *mode, const char *today, char *out, size_t size) {
    if (slug && mode && strcmp(slug, "blockfall") == 0) {
        const char *name = NULL, *rest = NULL;
        if (strncmp(mode, "sprint40", 8) == 0 && (mode[8] == '\0' || mode[8] == '-')) { name = "Sprint 40"; rest = mode + 8; }
        else if (strncmp(mode, "sprint", 6) == 0 && (mode[6] == '\0' || mode[6] == '-')) { name = "Sprint (old)"; rest = mode + 6; }
        if (name) {
            snprintf(out, size, "%s%s%s", name, rest[0] ? " " : "", rest[0] ? rest + 1 : "");
            return;
        }
    }
    Boards_ModeWords(mode, today, out, size);
}
