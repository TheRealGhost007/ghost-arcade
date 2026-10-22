#define _POSIX_C_SOURCE 200809L
#include "netmsg.h"
#include "online_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ the server */

static void Trim(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = '\0';
    size_t lead = strspn(s, " \t");
    if (lead) memmove(s, s + lead, strlen(s + lead) + 1);
}

static bool Printable(const char *s) {
    for (; *s; s++) if ((unsigned char)*s <= 32 || (unsigned char)*s > 126) return false;
    return true;
}

void NetMsg_ConfigLine(const char *line, char *url, size_t urlSize, char *key, size_t keySize) {
    char buf[600];
    snprintf(buf, sizeof(buf), "%s", line ? line : "");
    Trim(buf);
    if (buf[0] == '#' || !buf[0]) return;
    char *eq = strchr(buf, '=');
    if (!eq) return;
    *eq = '\0';
    char *k = buf, *v = eq + 1;
    Trim(k);
    Trim(v);
    if (!v[0] || !Printable(v)) return;
    /* only https servers, so the key and the addresses never travel in the clear */
    if (!strcmp(k, "url") && !strncmp(v, "https://", 8) && strlen(v) < urlSize) {
        snprintf(url, urlSize, "%s", v);
        size_t n = strlen(url);
        while (n > 8 && url[n - 1] == '/') url[--n] = '\0';
    } else if (!strcmp(k, "key") && strlen(v) < keySize) {
        snprintf(key, keySize, "%s", v);
    }
}

void NetMsg_ServerConfig(char *url, size_t urlSize, char *key, size_t keySize) {
    snprintf(url, urlSize, "%s", ONLINE_DEFAULT_URL);
    snprintf(key, keySize, "%s", ONLINE_DEFAULT_KEY);
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char path[600];
    if (xdg && xdg[0]) snprintf(path, sizeof(path), "%s/ghost-launcher/online.conf", xdg);
    else if (home && home[0]) snprintf(path, sizeof(path), "%s/.config/ghost-launcher/online.conf", home);
    else return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[600];
    while (fgets(line, sizeof(line), f)) NetMsg_ConfigLine(line, url, urlSize, key, keySize);
    fclose(f);
}

/* ------------------------------------------------------------ JSON */

bool Json_Quote(const char *s, char *out, size_t size) {
    size_t n = 0;
#define PUT(c) do { if (n + 1 >= size) return false; out[n++] = (char)(c); } while (0)
    PUT('"');
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
        unsigned char c = *p;
        if (c == '"' || c == '\\') { PUT('\\'); PUT(c); }
        else if (c == '\n') { PUT('\\'); PUT('n'); }
        else if (c == '\r') { PUT('\\'); PUT('r'); }
        else if (c == '\t') { PUT('\\'); PUT('t'); }
        else if (c < 32) {
            static const char kHex[] = "0123456789abcdef";
            PUT('\\'); PUT('u'); PUT('0'); PUT('0'); PUT(kHex[c >> 4]); PUT(kHex[c & 15]);
        } else PUT(c);
    }
    PUT('"');
#undef PUT
    out[n] = '\0';
    return true;
}

static const char *Ws(const char *p) {
    while (p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

/* Past the string p points at (on its opening quote); NULL if it never ends. */
static const char *SkipString(const char *p) {
    if (*p != '"') return NULL;
    for (p++; *p; p++) {
        if (*p == '\\') { if (!*++p) return NULL; }
        else if (*p == '"') return p + 1;
    }
    return NULL;
}

/* Past one value of any kind; NULL if it is broken. */
static const char *SkipValue(const char *p) {
    p = Ws(p);
    if (!p || !*p) return NULL;
    if (*p == '"') return SkipString(p);
    if (*p == '{' || *p == '[') {
        int depth = 0;
        while (*p) {
            if (*p == '"') { p = SkipString(p); if (!p) return NULL; continue; }
            if (*p == '{' || *p == '[') depth++;
            else if (*p == '}' || *p == ']') { if (--depth == 0) return p + 1; }
            p++;
        }
        return NULL;
    }
    /* a number, true, false or null */
    const char *start = p;
    while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') p++;
    return p > start ? p : NULL;
}

/* The string at p (on its quote), decoded into out. */
static bool ReadString(const char *p, char *out, size_t size) {
    if (!p || *p != '"' || size == 0) return false;
    size_t n = 0;
    for (p++; *p && *p != '"'; p++) {
        char c = *p;
        if (c == '\\') {
            p++;
            switch (*p) {
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'u': {
                    unsigned v = 0;
                    for (int k = 1; k <= 4; k++) {
                        char h = p[k];
                        int d = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : h >= 'A' && h <= 'F' ? h - 'A' + 10 : -1;
                        if (d < 0) return false;
                        v = v * 16 + (unsigned)d;
                    }
                    p += 4;
                    c = v < 128 ? (char)v : '?';
                    break;
                }
                default: return false;
            }
        }
        if (n + 1 >= size) return false;
        out[n++] = c;
    }
    if (*p != '"') return false;
    out[n] = '\0';
    return true;
}

const char *Json_Find(const char *json, const char *key) {
    const char *p = Ws(json);
    if (!p || *p != '{') return NULL;
    p = Ws(p + 1);
    if (*p == '}') return NULL;
    for (;;) {
        char name[64];
        if (*p != '"') return NULL;
        bool named = ReadString(p, name, sizeof(name));
        p = SkipString(p);
        if (!p) return NULL;
        p = Ws(p);
        if (*p != ':') return NULL;
        p = Ws(p + 1);
        if (named && !strcmp(name, key)) return p;
        p = SkipValue(p);
        if (!p) return NULL;
        p = Ws(p);
        if (*p == ',') { p = Ws(p + 1); continue; }
        return NULL; /* '}' or something broken: the key isn't there */
    }
}

bool Json_GetString(const char *json, const char *key, char *out, size_t size) {
    const char *v = Json_Find(json, key);
    return v && *v == '"' && ReadString(v, out, size);
}

bool Json_GetBool(const char *json, const char *key, bool *out) {
    const char *v = Json_Find(json, key);
    if (!v) return false;
    if (!strncmp(v, "true", 4)) { *out = true; return true; }
    if (!strncmp(v, "false", 5)) { *out = false; return true; }
    return false;
}

/* ------------------------------------------------------------ Realtime */

int Rt_Join(char *out, size_t size, const char *topic, int ref) {
    int n = snprintf(out, size,
                     "{\"topic\":\"%s\",\"event\":\"phx_join\",\"payload\":{\"config\":{\"broadcast\":{\"ack\":false,\"self\":false},"
                     "\"presence\":{\"key\":\"\"},\"private\":false}},\"ref\":\"%d\",\"join_ref\":\"%d\"}",
                     topic, ref, ref);
    return n > 0 && (size_t)n < size ? n : 0;
}

int Rt_Broadcast(char *out, size_t size, const char *topic, const uint8_t *data, int len, int ref) {
    int n = snprintf(out, size, "{\"topic\":\"%s\",\"event\":\"broadcast\",\"payload\":{\"type\":\"broadcast\",\"event\":\"m\",\"payload\":{\"d\":\"", topic);
    if (n <= 0 || (size_t)n >= size) return 0;
    static const char kHex[] = "0123456789abcdef";
    for (int i = 0; i < len; i++) {
        if ((size_t)n + 3 >= size) return 0;
        out[n++] = kHex[data[i] >> 4];
        out[n++] = kHex[data[i] & 15];
    }
    int tail = snprintf(out + n, size - (size_t)n, "\"}},\"ref\":\"%d\"}", ref);
    if (tail <= 0 || (size_t)(n + tail) >= size) return 0;
    return n + tail;
}

int Rt_Heartbeat(char *out, size_t size, int ref) {
    int n = snprintf(out, size, "{\"topic\":\"phoenix\",\"event\":\"heartbeat\",\"payload\":{},\"ref\":\"%d\"}", ref);
    return n > 0 && (size_t)n < size ? n : 0;
}

static int HexVal(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

RtKind Rt_Parse(const char *msg, const char *topic, int joinRef, uint8_t *data, int cap, int *len) {
    char event[32], top[96];
    if (!Json_GetString(msg, "event", event, sizeof(event)) || !Json_GetString(msg, "topic", top, sizeof(top))) return RT_OTHER;
    if (strcmp(top, topic) != 0) return RT_OTHER;
    const char *payload = Json_Find(msg, "payload");
    if (!strcmp(event, "phx_reply")) {
        char ref[16], status[16], want[16];
        snprintf(want, sizeof(want), "%d", joinRef);
        if (!Json_GetString(msg, "ref", ref, sizeof(ref)) || strcmp(ref, want) != 0) return RT_OTHER; /* a heartbeat's or a send's reply */
        if (!payload || !Json_GetString(payload, "status", status, sizeof(status))) return RT_JOIN_FAILED;
        return !strcmp(status, "ok") ? RT_JOINED : RT_JOIN_FAILED;
    }
    if (!strcmp(event, "phx_error") || !strcmp(event, "phx_close")) return RT_CLOSED;
    if (strcmp(event, "broadcast") != 0 || !payload) return RT_OTHER;
    char inner[8];
    if (!Json_GetString(payload, "event", inner, sizeof(inner)) || strcmp(inner, "m") != 0) return RT_OTHER;
    const char *body = Json_Find(payload, "payload");
    const char *d = body ? Json_Find(body, "d") : NULL;
    if (!d || *d != '"') return RT_OTHER;
    d++;
    int n = 0;
    while (d[0] && d[0] != '"') {
        int hi = HexVal(d[0]), lo = d[1] ? HexVal(d[1]) : -1;
        if (hi < 0 || lo < 0 || n >= cap) return RT_OTHER;
        data[n++] = (uint8_t)(hi * 16 + lo);
        d += 2;
    }
    if (d[0] != '"' || n == 0) return RT_OTHER;
    *len = n;
    return RT_PACKET;
}
