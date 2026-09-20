#define _POSIX_C_SOURCE 200809L
#include "updatecheck.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "safefile.h"
#include "syncdata.h"

bool Update_IsSha(const char *s) {
    if (!s || strlen(s) != 40) return false;
    for (int i = 0; i < 40; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

bool Update_IsRepo(const char *s) {
    if (!s) return false;
    size_t len = strlen(s);
    if (len < 3 || len >= UPDATE_REPO_LEN) return false;
    int slashes = 0;
    size_t partLen = 0;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (c == '/') {
            if (partLen == 0) return false;
            slashes++;
            partLen = 0;
            continue;
        }
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) return false;
        partLen++;
    }
    if (slashes != 1 || partLen == 0) return false;
    return strstr(s, "..") == NULL;
}

static const char *SkipSpace(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/* p points at an opening quote. Copies the JSON string (escapes decoded to
 * plain printable ASCII, everything else to '?') and returns the position
 * after the closing quote, or NULL if the string never ends. */
static const char *ReadJsonString(const char *p, char *out, size_t outSize) {
    if (*p != '"') return NULL;
    p++;
    size_t n = 0;
    while (*p && *p != '"') {
        unsigned char c = (unsigned char)*p;
        if (c == '\\') {
            p++;
            if (*p == '\0') return NULL;
            switch (*p) {
                case 'n': c = '\n'; break;
                case 't': c = ' '; break;
                case 'r': c = ' '; break;
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                case 'u': {
                    /* four hex digits; only plain ASCII is kept */
                    unsigned v = 0;
                    int k = 0;
                    for (; k < 4 && p[1 + k]; k++) {
                        char h = p[1 + k];
                        unsigned d = (h >= '0' && h <= '9') ? (unsigned)(h - '0')
                                   : (h >= 'a' && h <= 'f') ? (unsigned)(h - 'a' + 10)
                                   : (h >= 'A' && h <= 'F') ? (unsigned)(h - 'A' + 10) : 99u;
                        if (d == 99u) break;
                        v = v * 16u + d;
                    }
                    if (k < 4) return NULL;
                    p += 4;
                    c = (v >= 32 && v < 127) ? (unsigned char)v : (v == '\n' ? '\n' : '?');
                    break;
                }
                default: c = '?'; break;
            }
        }
        if (out && n + 1 < outSize) out[n++] = (char)c;
        p++;
    }
    if (*p != '"') return NULL;
    if (out && outSize) out[n] = '\0';
    return p + 1;
}

/* If a `"key"` token starts at p, returns the position after its colon. */
static const char *MatchKey(const char *p, const char *key) {
    size_t len = strlen(key);
    if (*p != '"' || strncmp(p + 1, key, len) != 0 || p[1 + len] != '"') return NULL;
    p = SkipSpace(p + len + 2);
    if (*p != ':') return NULL;
    return SkipSpace(p + 1);
}

static void FirstLinePrintable(char *s) {
    size_t n = 0;
    for (size_t i = 0; s[i]; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\n') break;
        s[n++] = (c < 32 || c > 126) ? '?' : (char)c;
    }
    while (n > 0 && s[n - 1] == ' ') n--;
    s[n] = '\0';
}

bool Update_ParseCommits(const char *json, const char *currentSha, UpdateState *out) {
    out->status = UPDATE_UNKNOWN;
    out->behind = 0;
    out->latest[0] = '\0';
    out->title[0] = '\0';
    if (!json) return false;

    bool haveCurrent = Update_IsSha(currentSha);
    int index = 0;          /* top-level commits seen so far */
    int foundAt = -1;
    bool wantTitle = false;

    /* A top-level commit object starts {"sha":"<40 hex>","node_id":...}. The
     * tree and parent hashes are also "sha" keys, but are followed by "url",
     * so the key after the value tells them apart. Strings are skipped whole,
     * so nothing inside a commit message can be mistaken for structure. */
    const char *p = json;
    while (*p) {
        if (*p != '"') { p++; continue; }
        const char *value = MatchKey(p, "sha");
        if (value && *value == '"') {
            char sha[64];
            const char *after = ReadJsonString(value, sha, sizeof(sha));
            if (!after) break;
            const char *next = SkipSpace(after);
            if (*next == ',') next = SkipSpace(next + 1);
            if (Update_IsSha(sha) && MatchKey(next, "node_id")) {
                if (index == 0) {
                    snprintf(out->latest, sizeof(out->latest), "%s", sha);
                    wantTitle = true;
                } else {
                    wantTitle = false;
                }
                if (haveCurrent && foundAt < 0 && strcmp(sha, currentSha) == 0) foundAt = index;
                index++;
            }
            p = after;
            continue;
        }
        value = MatchKey(p, "message");
        if (value && *value == '"') {
            char msg[512];
            const char *after = ReadJsonString(value, msg, sizeof(msg));
            if (!after) break;
            if (wantTitle && out->title[0] == '\0') {
                FirstLinePrintable(msg);
                snprintf(out->title, sizeof(out->title), "%.*s", (int)sizeof(out->title) - 1, msg);
                wantTitle = false;
            }
            p = after;
            continue;
        }
        const char *after = ReadJsonString(p, NULL, 0); /* some other key or value: skip it whole */
        if (!after) break;
        p = after;
    }

    if (index == 0) return false;
    if (foundAt == 0) out->status = UPDATE_CURRENT;
    else if (foundAt > 0) { out->status = UPDATE_BEHIND; out->behind = foundAt; }
    return true;
}

static bool StatePath(char *out, size_t size) {
    char dir[512];
    if (!Sync_DataDir(dir, sizeof(dir))) return false;
    snprintf(out, size, "%s/update.txt", dir);
    return true;
}

bool Update_Save(const UpdateState *s) {
    char path[700], dir[512];
    if (!StatePath(path, sizeof(path)) || !Sync_DataDir(dir, sizeof(dir))) return false;
    mkdir(dir, 0755); /* the parent already exists once anything has been played; best effort */
    FILE *f = SafeFile_Open(path);
    if (!f) return false;
    fprintf(f, "# Ghost Arcade update check (written by ghost-sync, read by the launcher)\n");
    fprintf(f, "status=%s\n", s->status == UPDATE_BEHIND ? "behind" : (s->status == UPDATE_CURRENT ? "current" : "unknown"));
    fprintf(f, "behind=%d\ncurrent=%s\nlatest=%s\ntitle=%s\nchecked=%lld\n", s->behind, s->current, s->latest, s->title, s->checkedAt);
    return SafeFile_Close(f);
}

void Update_Load(UpdateState *s) {
    memset(s, 0, sizeof(*s));
    s->status = UPDATE_UNKNOWN;
    char path[700];
    if (!StatePath(path, sizeof(path))) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256], status[16] = "";
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#') continue;
        *eq = '\0';
        const char *key = line, *val = eq + 1;
        if (strcmp(key, "status") == 0) snprintf(status, sizeof(status), "%.15s", val);
        else if (strcmp(key, "behind") == 0) s->behind = atoi(val);
        else if (strcmp(key, "current") == 0 && Update_IsSha(val)) snprintf(s->current, sizeof(s->current), "%s", val);
        else if (strcmp(key, "latest") == 0 && Update_IsSha(val)) snprintf(s->latest, sizeof(s->latest), "%s", val);
        else if (strcmp(key, "title") == 0) { snprintf(s->title, sizeof(s->title), "%.*s", (int)sizeof(s->title) - 1, val); FirstLinePrintable(s->title); }
        else if (strcmp(key, "checked") == 0) s->checkedAt = atoll(val);
    }
    fclose(f);
    /* Believe the file only where it is consistent. */
    if (s->behind < 0 || s->behind > 100000) s->behind = 0;
    if (strcmp(status, "behind") == 0 && s->behind > 0 && s->latest[0]) s->status = UPDATE_BEHIND;
    else if (strcmp(status, "current") == 0 && s->latest[0]) { s->status = UPDATE_CURRENT; s->behind = 0; }
    else { s->status = UPDATE_UNKNOWN; s->behind = 0; }
    if (s->checkedAt < 0) s->checkedAt = 0;
}
