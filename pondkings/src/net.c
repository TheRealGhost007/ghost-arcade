#define _POSIX_C_SOURCE 200809L /* getaddrinfo, clock_gettime */
#include "net.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static struct sockaddr_in ToSock(NetAddr a) {
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(a.ip);
    sa.sin_port = htons(a.port);
    return sa;
}

int Net_Open(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) {
        close(fd);
        return -1;
    }
    struct sockaddr_in any;
    memset(&any, 0, sizeof(any));
    any.sin_family = AF_INET;
    any.sin_addr.s_addr = htonl(INADDR_ANY);
    any.sin_port = 0;
    if (bind(fd, (struct sockaddr *)&any, sizeof(any)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

void Net_Close(int fd) {
    if (fd >= 0) close(fd);
}

uint16_t Net_LocalPort(int fd) {
    struct sockaddr_in sa;
    socklen_t len = sizeof(sa);
    if (fd < 0 || getsockname(fd, (struct sockaddr *)&sa, &len) != 0) return 0;
    return ntohs(sa.sin_port);
}

bool Net_Send(int fd, NetAddr to, const void *data, int len) {
    if (fd < 0 || !Net_AddrUsable(to) || len <= 0) return false;
    struct sockaddr_in sa = ToSock(to);
    return sendto(fd, data, (size_t)len, 0, (struct sockaddr *)&sa, sizeof(sa)) == len;
}

int Net_Recv(int fd, NetAddr *from, void *buf, int cap) {
    if (fd < 0) return 0;
    for (;;) {
        struct sockaddr_in sa;
        socklen_t sl = sizeof(sa);
        ssize_t n = recvfrom(fd, buf, (size_t)cap, 0, (struct sockaddr *)&sa, &sl);
        if (n < 0) {
            if (errno == EINTR) continue;
            return 0; /* EAGAIN: nothing waiting; anything else (an ICMP "unreachable"): skip it */
        }
        if (sa.sin_family != AF_INET) continue;
        if (from) {
            from->ip = ntohl(sa.sin_addr.s_addr);
            from->port = ntohs(sa.sin_port);
        }
        return (int)n;
    }
}

void Net_AddrString(NetAddr a, char *out, size_t size) {
    snprintf(out, size, "%u.%u.%u.%u:%u", (unsigned)(a.ip >> 24) & 255, (unsigned)(a.ip >> 16) & 255, (unsigned)(a.ip >> 8) & 255,
             (unsigned)a.ip & 255, (unsigned)a.port);
}

bool Net_ParseAddr(const char *text, NetAddr *out) {
    if (!text || !out) return false;
    unsigned part[5];
    int at = 0;
    const char *c = text;
    for (int k = 0; k < 5; k++) {
        if (*c < '0' || *c > '9') return false;
        unsigned v = 0;
        int digits = 0;
        while (*c >= '0' && *c <= '9') {
            v = v * 10 + (unsigned)(*c - '0');
            if (++digits > 5) return false;
            c++;
        }
        part[at++] = v;
        if (k < 3) { if (*c != '.') return false; c++; }
        else if (k == 3) { if (*c != ':') return false; c++; }
    }
    if (*c != '\0') return false;
    for (int k = 0; k < 4; k++) if (part[k] > 255) return false;
    if (part[4] < 1 || part[4] > 65535) return false;
    out->ip = (part[0] << 24) | (part[1] << 16) | (part[2] << 8) | part[3];
    out->port = (uint16_t)part[4];
    return true;
}

bool Net_AddrEqual(NetAddr a, NetAddr b) { return a.ip == b.ip && a.port == b.port; }

bool Net_AddrUsable(NetAddr a) {
    /* not 0.0.0.0, not a multicast or broadcast address, not port 0 */
    return a.ip != 0 && a.port != 0 && (a.ip >> 28) != 0xE && a.ip != 0xFFFFFFFFu;
}

bool Net_LanIp(uint32_t *ip) {
    /* "connecting" a UDP socket only picks the route: no packet leaves */
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;
    NetAddr probe = {0x08080808u, 53};
    struct sockaddr_in sa = ToSock(probe);
    bool ok = false;
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) == 0) {
        struct sockaddr_in me;
        socklen_t len = sizeof(me);
        if (getsockname(fd, (struct sockaddr *)&me, &len) == 0) {
            uint32_t v = ntohl(me.sin_addr.s_addr);
            if (v != 0 && (v >> 24) != 127) { *ip = v; ok = true; }
        }
    }
    close(fd);
    return ok;
}

bool Net_Resolve(const char *host, uint16_t port, NetAddr *out) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return false;
    bool ok = false;
    for (struct addrinfo *r = res; r && !ok; r = r->ai_next) {
        if (r->ai_family != AF_INET || r->ai_addrlen < sizeof(struct sockaddr_in)) continue;
        const struct sockaddr_in *sa = (const struct sockaddr_in *)r->ai_addr;
        out->ip = ntohl(sa->sin_addr.s_addr);
        out->port = port;
        ok = Net_AddrUsable(*out);
    }
    freeaddrinfo(res);
    return ok;
}

bool Net_RandomBytes(void *out, int bytes) {
    if (!out || bytes <= 0) return false;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    int got = 0;
    while (got < bytes) {
        ssize_t n = read(fd, (uint8_t *)out + got, (size_t)(bytes - got));
        if (n <= 0) { if (n < 0 && errno == EINTR) continue; break; }
        got += (int)n;
    }
    close(fd);
    return got == bytes;
}

bool Net_RandomHex(char *out, size_t size, int bytes) {
    if (!out || bytes <= 0 || size < (size_t)bytes * 2 + 1 || bytes > 64) return false;
    uint8_t raw[64];
    if (!Net_RandomBytes(raw, bytes)) return false;
    static const char kHex[] = "0123456789abcdef";
    for (int i = 0; i < bytes; i++) {
        out[i * 2] = kHex[raw[i] >> 4];
        out[i * 2 + 1] = kHex[raw[i] & 15];
    }
    out[bytes * 2] = '\0';
    return true;
}

uint32_t Net_Millis(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

/* ------------------------------------------------------------ STUN */

#define STUN_COOKIE 0x2112A442u
#define STUN_BINDING_REQUEST 0x0001
#define STUN_BINDING_SUCCESS 0x0101
#define STUN_ATTR_MAPPED 0x0001
#define STUN_ATTR_XOR_MAPPED 0x0020

static uint32_t Be16(const uint8_t *p) { return ((uint32_t)p[0] << 8) | p[1]; }
static uint32_t Be32(const uint8_t *p) { return (Be16(p) << 16) | Be16(p + 2); }

int Stun_Request(uint8_t *buf, int cap, const uint8_t txid[STUN_TXID]) {
    if (cap < 20) return 0;
    buf[0] = 0;
    buf[1] = STUN_BINDING_REQUEST;
    buf[2] = 0; /* no attributes */
    buf[3] = 0;
    buf[4] = (uint8_t)(STUN_COOKIE >> 24);
    buf[5] = (uint8_t)(STUN_COOKIE >> 16);
    buf[6] = (uint8_t)(STUN_COOKIE >> 8);
    buf[7] = (uint8_t)STUN_COOKIE;
    memcpy(buf + 8, txid, STUN_TXID);
    return 20;
}

bool Stun_IsMessage(const uint8_t *buf, int len) {
    return buf && len >= 20 && (buf[0] & 0xC0) == 0 && Be32(buf + 4) == STUN_COOKIE;
}

bool Stun_ParseResponse(const uint8_t *buf, int len, const uint8_t txid[STUN_TXID], NetAddr *mapped) {
    if (!Stun_IsMessage(buf, len) || Be16(buf) != STUN_BINDING_SUCCESS) return false;
    uint32_t body = Be16(buf + 2);
    if ((body & 3) != 0 || 20 + body != (uint32_t)len) return false;
    if (memcmp(buf + 8, txid, STUN_TXID) != 0) return false;
    bool haveXor = false, havePlain = false;
    NetAddr x = {0, 0}, plain = {0, 0};
    uint32_t at = 20;
    while (at + 4 <= (uint32_t)len) {
        uint32_t type = Be16(buf + at), alen = Be16(buf + at + 2);
        uint32_t next = at + 4 + ((alen + 3) & ~3u);
        if (at + 4 + alen > (uint32_t)len || next > (uint32_t)len + 0) return false;
        const uint8_t *v = buf + at + 4;
        if ((type == STUN_ATTR_XOR_MAPPED || type == STUN_ATTR_MAPPED) && alen >= 8 && v[1] == 0x01) {
            uint32_t port = Be16(v + 2), ip = Be32(v + 4);
            if (type == STUN_ATTR_XOR_MAPPED) {
                x.port = (uint16_t)(port ^ (STUN_COOKIE >> 16));
                x.ip = ip ^ STUN_COOKIE;
                haveXor = true;
            } else {
                plain.port = (uint16_t)port;
                plain.ip = ip;
                havePlain = true;
            }
        }
        at = next;
    }
    if (haveXor && Net_AddrUsable(x)) { *mapped = x; return true; }
    if (havePlain && Net_AddrUsable(plain)) { *mapped = plain; return true; }
    return false;
}
