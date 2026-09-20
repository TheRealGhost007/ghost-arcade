/* ghost-sync: the one place in Ghost Arcade that talks to the network.
 *
 *   upload    every row of $XDG_DATA_HOME/ghost-launcher/scores/<game>.txt
 *             (idempotent: the server ignores rows it already has)
 *   download  the public top-10 boards into scores/global/<game>.txt, in the
 *             same mode|username|score|date format the local files use
 *
 * Games and the launcher never link libcurl; they spawn this fire-and-forget
 * after a run ends and read the text files it leaves behind. Exit status:
 * 0 = fine (including "disabled" and "nothing to do"), 1 = something failed. */
#define _POSIX_C_SOURCE 200809L
#include <curl/curl.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "syncdata.h"

#define RESPONSE_CAP (1024 * 1024)

static bool sQuiet = false;

static void Say(const char *fmt, ...) {
    if (sQuiet) return;
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

typedef struct {
    char *data;
    size_t len;
} Response;

static size_t OnBody(char *chunk, size_t size, size_t nmemb, void *userdata) {
    Response *r = userdata;
    size_t add = size * nmemb;
    if (r->len + add + 1 > RESPONSE_CAP) return 0; /* aborts the transfer */
    char *grown = realloc(r->data, r->len + add + 1);
    if (!grown) return 0;
    r->data = grown;
    memcpy(r->data + r->len, chunk, add);
    r->len += add;
    r->data[r->len] = '\0';
    return add;
}

/* Returns the HTTP status, or -1 if the request never completed. body may be
 * NULL for a GET. *resp is always left valid (possibly empty); caller frees
 * resp->data. */
static long Request(const SyncConfig *cfg, const char *pathAndQuery, const char *body,
                    const char *extraHeader1, const char *extraHeader2, Response *resp) {
    resp->data = NULL;
    resp->len = 0;

    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    char url[SYNC_URL_LEN + 512];
    snprintf(url, sizeof(url), "%s%s", cfg->url, pathAndQuery);
    char keyHeader[SYNC_KEY_LEN + 16];
    snprintf(keyHeader, sizeof(keyHeader), "apikey: %s", cfg->key);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, keyHeader);
    if (extraHeader1) headers = curl_slist_append(headers, extraHeader1);
    if (extraHeader2) headers = curl_slist_append(headers, extraHeader2);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, OnBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, resp);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 6L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https"); /* never downgrade, never follow to file:// etc. */
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);     /* the API never redirects; a redirect is someone else talking */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);     /* the defaults, stated so nothing can quietly change them */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE, (long)RESPONSE_CAP);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ghost-sync/2.0");
    if (body) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);

    long status = -1;
    CURLcode rc = curl_easy_perform(curl);
    if (rc == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    else Say("  network error: %s\n", curl_easy_strerror(rc));

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return status;
}

#define UPLOAD_PATH "/rest/v1/rpc/submit_scores"
#define UPLOAD_CHUNK 400      /* the server refuses more than 500 rows a call */
#define UPLOAD_MAX_ROWS 4096

/* Sends one chunk. Returns rows the server newly stored, -1 on failure, and
 * sets *limited if the server said we are being rate limited. */
static int PostRows(const SyncConfig *cfg, const SyncRow *rows, int count, const char *installId, bool *limited) {
    static char body[UPLOAD_CHUNK * 200 + 256];
    if (Sync_BuildUploadJson(rows, count, installId, body, sizeof(body)) == 0) return -1;

    Response resp;
    long status = Request(cfg, UPLOAD_PATH, body, "Content-Type: application/json", "Accept: application/json", &resp);
    int stored = -1;
    if (status >= 200 && status < 300 && resp.data) {
        /* {"inserted": N, "limited": bool, ...} -- we only need two facts from it. */
        const char *ins = strstr(resp.data, "\"inserted\"");
        if (ins && (ins = strchr(ins, ':')) != NULL) {
            long v = strtol(ins + 1, NULL, 10);
            stored = (v >= 0 && v <= count) ? (int)v : 0;
        }
        const char *lim = strstr(resp.data, "\"limited\"");
        if (lim && (lim = strchr(lim, ':')) != NULL) {
            while (*++lim == ' ') { }
            if (strncmp(lim, "true", 4) == 0) *limited = true;
        }
    } else if (status >= 400 && resp.data) {
        char shown[301];
        snprintf(shown, sizeof(shown), "%.300s", resp.data);
        Sync_MakePrintable(shown);
        Say("  server said (%ld): %s\n", status, shown);
    }
    free(resp.data);
    return stored;
}

/* Returns false if anything failed to upload. */
static bool UploadAll(const SyncConfig *cfg) {
    char dir[512], scoresDir[600], installId[SYNC_UUID_LEN];
    if (!Sync_DataDir(dir, sizeof(dir))) return false;
    snprintf(scoresDir, sizeof(scoresDir), "%s/scores", dir);

    DIR *d = opendir(scoresDir);
    if (!d) {
        Say("upload: no local scores yet\n");
        return true;
    }
    if (!Sync_InstallId(installId, sizeof(installId))) {
        Say("upload: could not read or create the install id\n");
        closedir(d);
        return false;
    }

    /* Every game's rows go up together: one request per sync, not one per game. */
    static SyncRow rows[UPLOAD_MAX_ROWS];
    int total = 0;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL && total < UPLOAD_MAX_ROWS) {
        const char *name = entry->d_name;
        size_t len = strlen(name);
        if (len < 5 || strcmp(name + len - 4, ".txt") != 0) continue;

        char game[SYNC_SLUG_LEN];
        if (len - 4 >= sizeof(game)) continue;
        memcpy(game, name, len - 4);
        game[len - 4] = '\0';
        if (!Sync_IsKnownGame(game)) continue;

        char path[900];
        snprintf(path, sizeof(path), "%s/%s", scoresDir, name);
        int room = UPLOAD_MAX_ROWS - total;
        total += Sync_LoadLocalFile(path, game, rows + total, room < SYNC_MAX_ROWS ? room : SYNC_MAX_ROWS);
    }
    closedir(d);
    if (total == 0) {
        Say("upload: nothing to send\n");
        return true;
    }

    int stored = 0;
    bool limited = false;
    for (int at = 0; at < total && !limited; at += UPLOAD_CHUNK) {
        int n = total - at < UPLOAD_CHUNK ? total - at : UPLOAD_CHUNK;
        int got = PostRows(cfg, rows + at, n, installId, &limited);
        if (got < 0) {
            Say("upload: failed\n");
            return false;
        }
        stored += got;
    }
    if (limited) Say("upload: the server is rate limiting this connection; the rest will go next time\n");
    Say("upload: %d row(s) offered, %d new\n", total, stored);
    return true;
}

static bool DownloadBoards(const SyncConfig *cfg) {
    /* Ask only for the games we know: game=in.(a,b,c). */
    char query[1024];
    int used = snprintf(query, sizeof(query), "/rest/v1/leaderboard?select=game,mode,username,score,played_on&game=in.(");
    for (int i = 0; i < Sync_KnownGameCount() && used > 0 && (size_t)used < sizeof(query) - 80; i++)
        used += snprintf(query + used, sizeof(query) - (size_t)used, "%s%s", i ? "," : "", Sync_KnownGame(i));
    snprintf(query + used, sizeof(query) - (size_t)used, ")&order=game.asc,mode.asc,rank.asc&limit=%d", SYNC_MAX_ROWS);

    Response resp;
    long status = Request(cfg, query, NULL, "Accept: text/csv", NULL, &resp);
    if (status < 200 || status >= 300) {
        if (status >= 400 && resp.data) {
            char shown[301];
            snprintf(shown, sizeof(shown), "%.300s", resp.data);
            Sync_MakePrintable(shown);
            Say("  server said (%ld): %s\n", status, shown);
        }
        Say("download: failed (HTTP %ld)\n", status);
        free(resp.data);
        return false;
    }

    static SyncRow rows[SYNC_MAX_ROWS];
    int count = Sync_ParseBoardCsv(resp.data ? resp.data : "", rows, SYNC_MAX_ROWS);
    free(resp.data);

    int files = Sync_WriteGlobalCache(rows, count);
    Say("download: %d row(s) across %d game board(s)\n", count, files);
    return true;
}

int main(int argc, char **argv) {
    bool doUpload = true, doDownload = true;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quiet") == 0) sQuiet = true;
        else if (strcmp(argv[i], "--no-upload") == 0) doUpload = false;
        else if (strcmp(argv[i], "--no-download") == 0) doDownload = false;
        else {
            printf("usage: ghost-sync [--quiet] [--no-upload] [--no-download]\n"
                   "Syncs Ghost Arcade scores with the online leaderboard.\n"
                   "Configure or disable in ~/.config/ghost-launcher/online.conf\n"
                   "(enabled=0|1, url=..., key=...). Off until you opt in..\n");
            return strcmp(argv[i], "--help") == 0 ? 0 : 1;
        }
    }

    SyncConfig cfg;
    SyncConfig_Load(&cfg);
    if (!cfg.enabled) {
        Say("online sharing is off (opt-in): answer the question in the launcher, or set enabled=1 in online.conf\n");
        return 0;
    }

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) return 1;
    bool ok = true;
    if (doUpload && !UploadAll(&cfg)) ok = false;
    if (doDownload && !DownloadBoards(&cfg)) ok = false;
    curl_global_cleanup();
    return ok ? 0 : 1;
}
