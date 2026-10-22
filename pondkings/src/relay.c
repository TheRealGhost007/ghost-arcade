#define _POSIX_C_SOURCE 200809L
#include "relay.h"
#include "lobby.h"
#include "net.h"
#include "netmsg.h"
#include "wire.h"
#include <curl/curl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define QUEUE 96
#define HEARTBEAT_MS 25000
#define JOIN_TIMEOUT_MS 10000
#define JOIN_REF 1

typedef struct {
    int len;
    uint8_t data[WIRE_MAX];
} Slot;
typedef struct {
    Slot s[QUEUE];
    int head, count;
} Queue;

static struct {
    pthread_mutex_t lock;
    pthread_t thread;
    bool started, quit;
    RelayState state;
    char key[40];
    Queue out, in;
} R = {.lock = PTHREAD_MUTEX_INITIALIZER};

static bool Push(Queue *q, const uint8_t *d, int len) {
    if (q->count >= QUEUE || len <= 0 || len > WIRE_MAX) return false;
    Slot *s = &q->s[(q->head + q->count) % QUEUE];
    s->len = len;
    memcpy(s->data, d, (size_t)len);
    q->count++;
    return true;
}

static int Pop(Queue *q, uint8_t *buf, int cap) {
    if (q->count == 0) return 0;
    Slot *s = &q->s[q->head];
    int n = s->len <= cap ? s->len : 0;
    if (n) memcpy(buf, s->data, (size_t)n);
    q->head = (q->head + 1) % QUEUE;
    q->count--;
    return n;
}

static void SetState(RelayState s) {
    pthread_mutex_lock(&R.lock);
    R.state = s;
    pthread_mutex_unlock(&R.lock);
}

static bool Quitting(void) {
    pthread_mutex_lock(&R.lock);
    bool q = R.quit;
    pthread_mutex_unlock(&R.lock);
    return q;
}

static int AbortConnect(void *ud, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
    (void)ud; (void)a; (void)b; (void)c; (void)d;
    return Quitting() ? 1 : 0;
}

static void WaitFd(curl_socket_t fd, short events, int ms) {
    struct pollfd p = {(int)fd, events, 0};
    poll(&p, 1, ms);
}

static bool SendText(CURL *h, curl_socket_t fd, const char *text, size_t len) {
    size_t done = 0;
    for (int tries = 0; done < len && tries < 400; tries++) {
        size_t sent = 0;
        CURLcode rc = curl_ws_send(h, text + done, len - done, &sent, 0, CURLWS_TEXT);
        done += sent;
        if (rc == CURLE_OK) continue;
        if (rc != CURLE_AGAIN) return false;
        WaitFd(fd, POLLOUT, 5);
    }
    return done == len;
}

static void *Worker(void *arg) {
    (void)arg;
    char url[400], key[256], topic[80];
    NetMsg_ServerConfig(url, sizeof(url), key, sizeof(key));
    pthread_mutex_lock(&R.lock);
    snprintf(topic, sizeof(topic), "realtime:pk-%s", R.key);
    pthread_mutex_unlock(&R.lock);

    CURL *h = curl_easy_init();
    if (!h) { SetState(RELAY_FAILED); return NULL; }
    char *escKey = curl_easy_escape(h, key, 0);
    char wsUrl[800];
    snprintf(wsUrl, sizeof(wsUrl), "wss://%s/realtime/v1/websocket?apikey=%s&vsn=1.0.0", url + strlen("https://"), escKey ? escKey : "");
    curl_free(escKey);
    curl_easy_setopt(h, CURLOPT_URL, wsUrl);
    curl_easy_setopt(h, CURLOPT_CONNECT_ONLY, 2L);
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_PROTOCOLS_STR, "wss");
    curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, AbortConnect);
    curl_easy_setopt(h, CURLOPT_USERAGENT, "pondkings");
    curl_socket_t fd = CURL_SOCKET_BAD;
    if (curl_easy_perform(h) != CURLE_OK || curl_easy_getinfo(h, CURLINFO_ACTIVESOCKET, &fd) != CURLE_OK || fd == CURL_SOCKET_BAD) {
        SetState(RELAY_FAILED);
        curl_easy_cleanup(h);
        return NULL;
    }

    static char msg[65536];
    static char text[4096];
    size_t msgLen = 0;
    bool overflow = false;
    int ref = JOIN_REF;
    int n = Rt_Join(text, sizeof(text), topic, ref++);
    bool alive = n > 0 && SendText(h, fd, text, (size_t)n);
    uint32_t start = Net_Millis(), lastBeat = start;
    RelayState state = RELAY_CONNECTING;

    while (alive && !Quitting()) {
        WaitFd(fd, POLLIN, 4);
        for (;;) {
            char chunk[4096];
            size_t got = 0;
            const struct curl_ws_frame *meta = NULL;
            CURLcode rc = curl_ws_recv(h, chunk, sizeof(chunk), &got, &meta);
            if (rc == CURLE_AGAIN) break;
            if (rc != CURLE_OK || !meta) { alive = false; break; }
            if (meta->flags & CURLWS_CLOSE) { alive = false; break; }
            if (!(meta->flags & CURLWS_TEXT)) continue; /* pings are answered by libcurl; nothing else is ours */
            if (msgLen + got < sizeof(msg)) {
                memcpy(msg + msgLen, chunk, got);
                msgLen += got;
            } else {
                overflow = true;
            }
            if (meta->bytesleft > 0 || (meta->flags & CURLWS_CONT)) continue;
            msg[msgLen] = '\0';
            if (!overflow) {
                uint8_t pkt[WIRE_MAX];
                int plen = 0;
                switch (Rt_Parse(msg, topic, JOIN_REF, pkt, sizeof(pkt), &plen)) {
                    case RT_JOINED:
                        state = RELAY_READY;
                        SetState(RELAY_READY);
                        break;
                    case RT_JOIN_FAILED:
                    case RT_CLOSED: alive = false; break;
                    case RT_PACKET:
                        pthread_mutex_lock(&R.lock);
                        Push(&R.in, pkt, plen); /* a full queue drops it: the game resends */
                        pthread_mutex_unlock(&R.lock);
                        break;
                    default: break;
                }
            }
            msgLen = 0;
            overflow = false;
        }
        if (!alive) break;
        uint32_t now = Net_Millis();
        if (state == RELAY_CONNECTING && now - start > JOIN_TIMEOUT_MS) { alive = false; break; }
        if (state == RELAY_READY) {
            for (;;) {
                uint8_t pkt[WIRE_MAX];
                pthread_mutex_lock(&R.lock);
                int plen = Pop(&R.out, pkt, sizeof(pkt));
                pthread_mutex_unlock(&R.lock);
                if (plen <= 0) break;
                n = Rt_Broadcast(text, sizeof(text), topic, pkt, plen, ref++);
                if (n <= 0 || !SendText(h, fd, text, (size_t)n)) { alive = false; break; }
            }
        }
        if (alive && now - lastBeat >= HEARTBEAT_MS) {
            n = Rt_Heartbeat(text, sizeof(text), ref++);
            alive = n > 0 && SendText(h, fd, text, (size_t)n);
            lastBeat = now;
        }
    }
    if (!Quitting()) SetState(RELAY_FAILED);
    size_t sent = 0;
    curl_ws_send(h, "", 0, &sent, 0, CURLWS_CLOSE);
    curl_easy_cleanup(h);
    return NULL;
}

bool Relay_Start(const char *relayKey) {
    Relay_Stop();
    if (!relayKey || strlen(relayKey) != 32) return false;
    for (const char *c = relayKey; *c; c++)
        if (!((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f'))) return false;
    NetCurl_Init();
    pthread_mutex_lock(&R.lock);
    snprintf(R.key, sizeof(R.key), "%s", relayKey);
    R.quit = false;
    R.state = RELAY_CONNECTING;
    R.in.count = R.in.head = R.out.count = R.out.head = 0;
    bool ok = pthread_create(&R.thread, NULL, Worker, NULL) == 0;
    R.started = ok;
    if (!ok) R.state = RELAY_FAILED;
    pthread_mutex_unlock(&R.lock);
    return ok;
}

RelayState Relay_State(void) {
    pthread_mutex_lock(&R.lock);
    RelayState s = R.state;
    pthread_mutex_unlock(&R.lock);
    return s;
}

void Relay_Send(const uint8_t *data, int len) {
    pthread_mutex_lock(&R.lock);
    if (R.state == RELAY_READY) Push(&R.out, data, len);
    pthread_mutex_unlock(&R.lock);
}

int Relay_Recv(uint8_t *buf, int cap) {
    pthread_mutex_lock(&R.lock);
    int n = Pop(&R.in, buf, cap);
    pthread_mutex_unlock(&R.lock);
    return n;
}

void Relay_Stop(void) {
    pthread_mutex_lock(&R.lock);
    bool started = R.started;
    R.quit = true;
    pthread_mutex_unlock(&R.lock);
    if (started) pthread_join(R.thread, NULL);
    pthread_mutex_lock(&R.lock);
    R.started = false;
    R.state = RELAY_OFF;
    R.in.count = R.out.count = 0;
    pthread_mutex_unlock(&R.lock);
}
