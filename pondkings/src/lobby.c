#define _POSIX_C_SOURCE 200809L
#include "lobby.h"
#include "netmsg.h"
#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { JOB_NONE, JOB_RPC, JOB_STUN } JobKind;

static struct {
    pthread_mutex_t lock;
    pthread_cond_t wake;
    pthread_t thread;
    bool started, quit;
    JobKind job;         /* waiting to be picked up */
    char fn[48];
    char *body;
    bool running;        /* the worker is on a job */
    unsigned generation; /* bumped by Lobby_Forget: answers to older calls are dropped */
    unsigned queuedGen, runningGen;
    bool done;
    LobbyReply reply;
    struct { char fn[48]; char *body; } detached[4];
    int detachedCount;
} L = {.lock = PTHREAD_MUTEX_INITIALIZER, .wake = PTHREAD_COND_INITIALIZER};

static bool sCurlReady;

void NetCurl_Init(void) {
    if (!sCurlReady) sCurlReady = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
}

void NetCurl_Shutdown(void) {
    if (sCurlReady) curl_global_cleanup();
    sCurlReady = false;
}

typedef struct {
    char *data;
    size_t len;
} Buf;

static size_t Collect(char *ptr, size_t size, size_t n, void *ud) {
    Buf *b = (Buf *)ud;
    size_t add = size * n;
    if (b->len + add >= sizeof(((LobbyReply *)0)->body)) return 0; /* too big for any real answer: stop */
    memcpy(b->data + b->len, ptr, add);
    b->len += add;
    b->data[b->len] = '\0';
    return add;
}

static int Abort(void *ud, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
    (void)ud; (void)a; (void)b; (void)c; (void)d;
    pthread_mutex_lock(&L.lock);
    bool quit = L.quit;
    pthread_mutex_unlock(&L.lock);
    return quit ? 1 : 0;
}

static void DoRpc(const char *fn, const char *body, LobbyReply *out) {
    char url[400], key[256];
    NetMsg_ServerConfig(url, sizeof(url), key, sizeof(key));
    char full[512];
    snprintf(full, sizeof(full), "%s/rest/v1/rpc/%s", url, fn);
    CURL *h = curl_easy_init();
    if (!h) return;
    char keyHeader[300];
    snprintf(keyHeader, sizeof(keyHeader), "apikey: %s", key);
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, keyHeader);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    Buf b = {out->body, 0};
    out->body[0] = '\0';
    curl_easy_setopt(h, CURLOPT_URL, full);
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(h, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, Collect);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 6L);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, 12L);
    curl_easy_setopt(h, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, Abort);
    curl_easy_setopt(h, CURLOPT_USERAGENT, "pondkings");
    CURLcode rc = curl_easy_perform(h);
    long status = 0;
    if (rc == CURLE_OK) curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    out->status = status;
    out->ok = rc == CURLE_OK && status >= 200 && status < 300;
    /* PostgREST: 404 with PGRST202 when the function isn't there */
    out->missing = status == 404 && strstr(out->body, "PGRST202") != NULL;
    curl_slist_free_all(headers);
    curl_easy_cleanup(h);
}

static void DoStun(LobbyReply *out) {
    static const struct { const char *host; uint16_t port; } kServers[] = {
        {"stun.l.google.com", 19302},
        {"stun.cloudflare.com", 3478},
        {"stun1.l.google.com", 19302},
    };
    out->stunCount = 0;
    for (size_t k = 0; k < sizeof(kServers) / sizeof(kServers[0]) && out->stunCount < 3; k++) {
        NetAddr a;
        if (Net_Resolve(kServers[k].host, kServers[k].port, &a)) out->stun[out->stunCount++] = a;
    }
    out->ok = out->stunCount > 0;
}

static void *Worker(void *arg) {
    (void)arg;
    pthread_mutex_lock(&L.lock);
    for (;;) {
        while (!L.quit && L.job == JOB_NONE && L.detachedCount == 0) pthread_cond_wait(&L.wake, &L.lock);
        if (L.quit) break;
        if (L.job == JOB_NONE) { /* nothing the screens wait for: a detached call */
            char dfn[48];
            snprintf(dfn, sizeof(dfn), "%s", L.detached[0].fn);
            char *dbody = L.detached[0].body;
            memmove(L.detached, L.detached + 1, sizeof(L.detached[0]) * (size_t)(L.detachedCount - 1));
            L.detachedCount--;
            pthread_mutex_unlock(&L.lock);
            static LobbyReply ignored;
            memset(&ignored, 0, sizeof(ignored));
            DoRpc(dfn, dbody ? dbody : "{}", &ignored);
            free(dbody);
            pthread_mutex_lock(&L.lock);
            continue;
        }
        JobKind job = L.job;
        L.job = JOB_NONE;
        char fn[48];
        snprintf(fn, sizeof(fn), "%s", L.fn);
        char *body = L.body;
        L.body = NULL;
        L.runningGen = L.queuedGen;
        L.running = true;
        pthread_mutex_unlock(&L.lock);

        static LobbyReply reply;
        memset(&reply, 0, sizeof(reply));
        if (job == JOB_RPC) DoRpc(fn, body ? body : "{}", &reply);
        else DoStun(&reply);
        free(body);

        pthread_mutex_lock(&L.lock);
        L.running = false;
        if (L.runningGen == L.generation && !L.quit) {
            L.reply = reply;
            L.done = true;
        }
    }
    pthread_mutex_unlock(&L.lock);
    return NULL;
}

static bool Start(JobKind job, const char *fn, const char *body) {
    NetCurl_Init();
    pthread_mutex_lock(&L.lock);
    if (!L.started) {
        L.quit = false;
        if (pthread_create(&L.thread, NULL, Worker, NULL) != 0) {
            pthread_mutex_unlock(&L.lock);
            return false;
        }
        L.started = true;
    }
    if (job == JOB_NONE) { /* only making sure the worker runs */
        pthread_mutex_unlock(&L.lock);
        return true;
    }
    /* one call at a time; a forgotten one still finishing doesn't count */
    if (L.job != JOB_NONE || (L.running && L.runningGen == L.generation) || L.done) {
        pthread_mutex_unlock(&L.lock);
        return false;
    }
    L.job = job;
    snprintf(L.fn, sizeof(L.fn), "%s", fn ? fn : "");
    free(L.body);
    L.body = body ? strdup(body) : NULL;
    L.queuedGen = L.generation;
    pthread_cond_signal(&L.wake);
    pthread_mutex_unlock(&L.lock);
    return true;
}

bool Lobby_Call(const char *fn, const char *jsonBody) {
    /* function names are ours, but keep them to what a URL path takes */
    for (const char *c = fn; c && *c; c++)
        if (!((*c >= 'a' && *c <= 'z') || *c == '_')) return false;
    return Start(JOB_RPC, fn, jsonBody);
}

bool Lobby_LookupStun(void) { return Start(JOB_STUN, NULL, NULL); }

bool Lobby_CallDetached(const char *fn, const char *jsonBody) {
    for (const char *c = fn; c && *c; c++)
        if (!((*c >= 'a' && *c <= 'z') || *c == '_')) return false;
    if (!Start(JOB_NONE, NULL, NULL)) return false; /* makes sure the worker runs */
    pthread_mutex_lock(&L.lock);
    bool ok = L.detachedCount < 4;
    if (ok) {
        snprintf(L.detached[L.detachedCount].fn, sizeof(L.detached[0].fn), "%s", fn);
        L.detached[L.detachedCount].body = jsonBody ? strdup(jsonBody) : NULL;
        L.detachedCount++;
        pthread_cond_signal(&L.wake);
    }
    pthread_mutex_unlock(&L.lock);
    return ok;
}

LobbyState Lobby_Poll(LobbyReply *reply) {
    pthread_mutex_lock(&L.lock);
    LobbyState s = LOBBY_IDLE;
    if (L.done) {
        if (reply) *reply = L.reply;
        L.done = false;
        s = LOBBY_DONE;
    } else if ((L.job != JOB_NONE && L.queuedGen == L.generation) || (L.running && L.runningGen == L.generation)) {
        s = LOBBY_BUSY;
    }
    pthread_mutex_unlock(&L.lock);
    return s;
}

void Lobby_Forget(void) {
    pthread_mutex_lock(&L.lock);
    L.generation++;
    L.done = false;
    if (L.job != JOB_NONE) { /* not picked up yet: just drop it */
        L.job = JOB_NONE;
        free(L.body);
        L.body = NULL;
    }
    pthread_mutex_unlock(&L.lock);
}

void Lobby_Shutdown(void) {
    pthread_mutex_lock(&L.lock);
    bool started = L.started;
    L.quit = true;
    pthread_cond_signal(&L.wake);
    pthread_mutex_unlock(&L.lock);
    if (started) pthread_join(L.thread, NULL);
    pthread_mutex_lock(&L.lock);
    L.started = false;
    free(L.body);
    L.body = NULL;
    for (int i = 0; i < L.detachedCount; i++) free(L.detached[i].body);
    L.detachedCount = 0;
    L.job = JOB_NONE;
    L.done = false;
    pthread_mutex_unlock(&L.lock);
}
