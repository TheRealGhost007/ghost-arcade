#define _POSIX_C_SOURCE 200809L
#include "safefile.h"
#include "syncdata.h"
#include "online_config.h"
#include "updatecheck.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define APP_DIRNAME "ghost-launcher"

static char *TrimInPlace(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                        s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[--len] = '\0';
    }
    return s;
}

static void MkdirParents(const char *path) {
    char cursor[700] = {0};
    for (const char *p = path; ; p++) {
        if (*p == '/' || *p == '\0') {
            size_t len = (size_t)(p - path);
            if (len > 0 && len < sizeof(cursor)) {
                memcpy(cursor, path, len);
                cursor[len] = '\0';
                mkdir(cursor, 0755);
            }
            if (*p == '\0') break;
        }
    }
}

static bool XdgDir(const char *xdgVar, const char *homeFallback, char *out, size_t outSize) {
    const char *xdg = getenv(xdgVar);
    if (xdg && xdg[0] != '\0') {
        snprintf(out, outSize, "%s/%s", xdg, APP_DIRNAME);
        return true;
    }
    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') return false;
    snprintf(out, outSize, "%s/%s/%s", home, homeFallback, APP_DIRNAME);
    return true;
}

bool Sync_DataDir(char *out, size_t outSize) {
    return XdgDir("XDG_DATA_HOME", ".local/share", out, outSize);
}

void SyncConfig_Load(SyncConfig *cfg) {
    cfg->enabled = false;
    cfg->decided = false;
    cfg->checkUpdates = true;
    snprintf(cfg->updateRepo, sizeof(cfg->updateRepo), "%s", UPDATE_DEFAULT_REPO);
    snprintf(cfg->url, sizeof(cfg->url), "%s", ONLINE_DEFAULT_URL);
    snprintf(cfg->key, sizeof(cfg->key), "%s", ONLINE_DEFAULT_KEY);

    char dir[512], path[600];
    if (!XdgDir("XDG_CONFIG_HOME", ".config", dir, sizeof(dir))) return;
    snprintf(path, sizeof(path), "%s/online.conf", dir);

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[600];
    while (fgets(line, sizeof(line), f)) {
        char *trimmed = TrimInPlace(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#') continue;
        char *eq = strchr(trimmed, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = TrimInPlace(trimmed);
        char *value = TrimInPlace(eq + 1);
        if (strcmp(key, "enabled") == 0) { cfg->enabled = (atoi(value) != 0); cfg->decided = true; }
        else if (strcmp(key, "check_updates") == 0) cfg->checkUpdates = (atoi(value) != 0);
        else if (strcmp(key, "update_repo") == 0 && Update_IsRepo(value)) snprintf(cfg->updateRepo, sizeof(cfg->updateRepo), "%s", value);
        else if (strcmp(key, "url") == 0 && value[0]) snprintf(cfg->url, sizeof(cfg->url), "%s", value);
        else if (strcmp(key, "key") == 0 && value[0]) snprintf(cfg->key, sizeof(cfg->key), "%s", value);
    }
    fclose(f);

    /* Scores and the key only ever travel over TLS. A non-https url= is a
     * mistake (or tampering): refuse to sync rather than fall back silently. */
    if (strncmp(cfg->url, "https://", 8) != 0) cfg->enabled = false;

    /* A trailing slash would produce "//rest/v1" URLs. */
    size_t len = strlen(cfg->url);
    while (len > 0 && cfg->url[len - 1] == '/') cfg->url[--len] = '\0';
}

bool SyncConfig_SaveEnabled(bool enabled) {
    char dir[512], path[600];
    if (!XdgDir("XDG_CONFIG_HOME", ".config", dir, sizeof(dir))) return false;
    snprintf(path, sizeof(path), "%s/online.conf", dir);

    /* Keep whatever else is in the file (url=, key=, comments). */
    static char kept[8][600];
    int keptCount = 0;
    FILE *f = fopen(path, "r");
    if (f) {
        char line[600];
        while (fgets(line, sizeof(line), f) && keptCount < 8) {
            char copy[600];
            snprintf(copy, sizeof(copy), "%s", line);
            char *t = TrimInPlace(copy);
            char *eq = strchr(t, '=');
            if (eq && t[0] != '#') {
                *eq = '\0';
                if (strcmp(TrimInPlace(t), "enabled") == 0) continue;
            }
            snprintf(kept[keptCount++], sizeof(kept[0]), "%s", line);
        }
        fclose(f);
    }
    /* the directory may not exist yet: make it one level at a time */
    char parent[512];
    snprintf(parent, sizeof(parent), "%s", dir);
    char *slash = strrchr(parent, '/');
    if (slash) { *slash = '\0'; mkdir(parent, 0755); }
    mkdir(dir, 0755);

    f = SafeFile_Open(path);
    if (!f) return false;
    fprintf(f, "enabled=%d\n", enabled ? 1 : 0);
    for (int i = 0; i < keptCount; i++) fputs(kept[i], f);
    SafeFile_Close(f);
    return true;
}

static bool LooksLikeUuid(const char *s) {
    if (strlen(s) != 36) return false;
    for (int i = 0; i < 36; i++) {
        bool dash = (i == 8 || i == 13 || i == 18 || i == 23);
        char c = s[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (dash ? c != '-' : !hex) return false;
    }
    return true;
}

bool Sync_InstallId(char *out, size_t outSize) {
    char dir[512], path[600];
    if (outSize < SYNC_UUID_LEN || !Sync_DataDir(dir, sizeof(dir))) return false;
    snprintf(path, sizeof(path), "%s/install_id", dir);

    FILE *f = fopen(path, "r");
    if (f) {
        char line[64] = "";
        if (fgets(line, sizeof(line), f)) {
            char *id = TrimInPlace(line);
            if (LooksLikeUuid(id)) {
                snprintf(out, outSize, "%s", id);
                fclose(f);
                return true;
            }
        }
        fclose(f); /* unreadable or corrupt: fall through and mint a new one */
    }

    unsigned char b[16];
    FILE *rnd = fopen("/dev/urandom", "rb");
    if (!rnd) return false;
    size_t got = fread(b, 1, sizeof(b), rnd);
    fclose(rnd);
    if (got != sizeof(b)) return false;

    b[6] = (unsigned char)((b[6] & 0x0F) | 0x40); /* version 4 */
    b[8] = (unsigned char)((b[8] & 0x3F) | 0x80); /* RFC 4122 variant */
    snprintf(out, outSize,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
             b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);

    MkdirParents(dir);
    f = SafeFile_Open(path);
    if (!f) return false; /* without a stable id every sync would duplicate rows */
    fprintf(f, "%s\n", out);
    SafeFile_Close(f);
    return true;
}

bool Sync_IsSlug(const char *s, size_t maxLen) {
    size_t len = strlen(s);
    if (len == 0 || len > maxLen) return false;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    }
    return true;
}

bool Sync_IsDate(const char *s) {
    if (strlen(s) != 10) return false;
    for (int i = 0; i < 10; i++) {
        bool dash = (i == 4 || i == 7);
        if (dash ? s[i] != '-' : (s[i] < '0' || s[i] > '9')) return false;
    }
    int month = (s[5] - '0') * 10 + (s[6] - '0');
    int day = (s[8] - '0') * 10 + (s[9] - '0');
    return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

/* Printable ASCII only, no field separators, clipped to the server's limit.
 * Anything that ends up empty becomes PLAYER. */
static void CleanUsername(const char *in, char *out, size_t outSize) {
    size_t n = 0;
    while (*in == ' ') in++;
    for (; *in && n + 1 < outSize; in++) {
        unsigned char c = (unsigned char)*in;
        if (c < 32 || c == 127) continue;
        if (c >= 128) c = '?';
        if (c == '|' || c == '\\') c = '/'; /* '|' is our field separator; a backslash is escape-significant in JSON and in the server's CSV */
        out[n++] = (char)c;
    }
    while (n > 0 && out[n - 1] == ' ') n--;
    out[n] = '\0';
    if (n == 0) snprintf(out, outSize, "PLAYER");
}

bool Sync_ParseLocalLine(const char *game, const char *line, SyncRow *out) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", line);
    char *trimmed = TrimInPlace(buf);
    if (trimmed[0] == '\0' || trimmed[0] == '#') return false;

    char *fields[4];
    int n = 0;
    char *cursor = trimmed;
    while (n < 4) {
        fields[n++] = cursor;
        char *sep = strchr(cursor, '|');
        if (!sep) break;
        *sep = '\0';
        cursor = sep + 1;
    }
    if (n < 4) return false;

    if (!Sync_IsSlug(game, SYNC_SLUG_LEN - 1)) return false;
    if (!Sync_IsSlug(fields[0], SYNC_MODE_LEN - 1)) return false;
    if (!Sync_IsDate(fields[3])) return false;

    char *end = NULL;
    long score = strtol(fields[2], &end, 10);
    if (end == fields[2] || *end != '\0') return false;
    if (score < 1 || score > SYNC_MAX_SCORE) return false;

    snprintf(out->game, sizeof(out->game), "%s", game);
    snprintf(out->mode, sizeof(out->mode), "%s", fields[0]);
    CleanUsername(fields[1], out->username, sizeof(out->username));
    out->score = score;
    snprintf(out->date, sizeof(out->date), "%s", fields[3]);
    return true;
}

int Sync_LoadLocalFile(const char *path, const char *game, SyncRow *rows, int maxRows) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int count = 0;
    char line[512];
    while (count < maxRows && fgets(line, sizeof(line), f)) {
        if (Sync_ParseLocalLine(game, line, &rows[count])) count++;
    }
    fclose(f);
    return count;
}

size_t Sync_BuildUploadJson(const SyncRow *rows, int count, const char *installId,
                            char *out, size_t outSize) {
    int n = snprintf(out, outSize, "{\"p_install_id\":\"%s\",\"p_rows\":[", installId);
    if (n < 0 || (size_t)n >= outSize) return 0;
    size_t used = (size_t)n;

    for (int i = 0; i < count; i++) {
        /* game, mode and date are slug/date validated (no quotes possible);
         * only the username needs escaping. */
        char name[SYNC_NAME_LEN * 2 + 1];
        size_t q = 0;
        for (const char *p = rows[i].username; *p; p++) {
            unsigned char ch = (unsigned char)*p;
            if (ch < 32 || ch > 126) ch = '?';
            if (ch == '"' || ch == '\\') name[q++] = '\\';
            name[q++] = (char)ch;
        }
        name[q] = '\0';
        n = snprintf(out + used, outSize - used,
                     "%s{\"game\":\"%s\",\"mode\":\"%s\",\"username\":\"%s\",\"score\":%ld,\"played_on\":\"%s\"}",
                     i ? "," : "", rows[i].game, rows[i].mode, name, rows[i].score, rows[i].date);
        if (n < 0 || (size_t)n >= outSize - used) return 0;
        used += (size_t)n;
    }
    n = snprintf(out + used, outSize - used, "]}");
    if (n < 0 || (size_t)n >= outSize - used) return 0;
    return used + (size_t)n;
}

static const char *const kKnownGames[] = {
    "blockfall", "coilrush", "brickburst", "skyraid", "ghostmaze", "rockdrift",
    "lanehop", "crawlshot", "moondrop", "gemdive", "girderclimb",
};

int Sync_KnownGameCount(void) { return (int)(sizeof(kKnownGames) / sizeof(kKnownGames[0])); }
const char *Sync_KnownGame(int i) { return (i >= 0 && i < Sync_KnownGameCount()) ? kKnownGames[i] : NULL; }

bool Sync_IsKnownGame(const char *slug) {
    for (int i = 0; i < Sync_KnownGameCount(); i++) if (slug && strcmp(slug, kKnownGames[i]) == 0) return true;
    return false;
}

void Sync_MakePrintable(char *s) {
    if (!s) return;
    for (; *s; s++) {
        unsigned char ch = (unsigned char)*s;
        if (ch == '\n' || ch == '\t') { *s = ' '; continue; }
        if (ch < 32 || ch > 126) *s = '?';
    }
}

/* Reads one CSV record (RFC 4180: quoted fields, "" escapes) starting at
 * *cursor, advancing it past the record. Returns the field count, or -1 at
 * end of input. Over-long fields are truncated, extra fields ignored. */
static int CsvNextRecord(const char **cursor, char fields[][64], int maxFields) {
    const char *p = *cursor;
    if (*p == '\0') return -1;

    int field = 0;
    size_t len = 0;
    bool inQuotes = false;
    for (int i = 0; i < maxFields; i++) fields[i][0] = '\0';

    for (;; p++) {
        char c = *p;
        if (inQuotes) {
            if (c == '\0') break;
            if (c == '"') {
                if (p[1] == '"') { p++; } else { inQuotes = false; continue; }
            }
        } else {
            if (c == '"') { inQuotes = true; continue; }
            if (c == ',' || c == '\n' || c == '\0') {
                if (field < maxFields) fields[field][len] = '\0';
                field++;
                len = 0;
                if (c == ',') continue;
                if (c == '\n') p++;
                break;
            }
            if (c == '\r') continue;
        }
        if (field < maxFields && len + 1 < 64) fields[field][len++] = c;
    }
    *cursor = p;
    return field;
}

int Sync_ParseBoardCsv(const char *csv, SyncRow *rows, int maxRows) {
    const char *cursor = csv;
    char fields[5][64];
    int count = 0;
    bool headerSeen = false;

    for (;;) {
        int n = CsvNextRecord(&cursor, fields, 5);
        if (n < 0) break;
        if (!headerSeen) { headerSeen = true; continue; }
        if (n < 5 || count >= maxRows) continue;

        if (!Sync_IsSlug(fields[0], SYNC_SLUG_LEN - 1)) continue;
        if (!Sync_IsKnownGame(fields[0])) continue; /* the game name becomes a file name: only ours */
        if (!Sync_IsSlug(fields[1], SYNC_MODE_LEN - 1)) continue;
        if (!Sync_IsDate(fields[4])) continue;
        char *end = NULL;
        long score = strtol(fields[3], &end, 10);
        if (end == fields[3] || *end != '\0' || score < 1 || score > SYNC_MAX_SCORE) continue;

        /* Lengths were just validated; the explicit precision only tells the
         * compiler what the checks above already guarantee. */
        SyncRow *r = &rows[count++];
        snprintf(r->game, sizeof(r->game), "%.*s", (int)sizeof(r->game) - 1, fields[0]);
        snprintf(r->mode, sizeof(r->mode), "%.*s", (int)sizeof(r->mode) - 1, fields[1]);
        CleanUsername(fields[2], r->username, sizeof(r->username));
        r->score = score;
        snprintf(r->date, sizeof(r->date), "%.*s", (int)sizeof(r->date) - 1, fields[4]);
    }
    return count;
}

int Sync_WriteGlobalCache(const SyncRow *rows, int count) {
    char dir[512], globalDir[600];
    if (!Sync_DataDir(dir, sizeof(dir))) return 0;
    snprintf(globalDir, sizeof(globalDir), "%s/scores/global", dir);
    MkdirParents(globalDir);

    int files = 0;
    int i = 0;
    while (i < count) {
        const char *game = rows[i].game; /* already validated as a slug: safe in a path */
        char path[700], tmpPath[760];
        snprintf(path, sizeof(path), "%s/%s.txt", globalDir, game);
        snprintf(tmpPath, sizeof(tmpPath), "%s.tmp.%ld", path, (long)getpid());

        FILE *f = fopen(tmpPath, "w");
        int start = i;
        while (i < count && strcmp(rows[i].game, rows[start].game) == 0) {
            if (f) fprintf(f, "%s|%s|%ld|%s\n", rows[i].mode, rows[i].username, rows[i].score, rows[i].date);
            i++;
        }
        if (!f) continue;
        fclose(f);

        /* rename() is atomic, so a game reading the cache mid-sync sees
         * either the old board or the new one, never half a file. */
        if (rename(tmpPath, path) == 0) files++;
        else unlink(tmpPath);
    }
    return files;
}
