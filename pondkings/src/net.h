#ifndef NET_H
#define NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The plumbing under online play: one non-blocking IPv4 UDP socket per
 * game, the machine's own LAN address, and STUN (asking a public server
 * which address and port the home router shows the world for that socket),
 * so two machines can find each other through their routers. No raylib. */

typedef struct {
    uint32_t ip;    /* host byte order; 0 = none */
    uint16_t port;
} NetAddr;

/* A non-blocking UDP socket on any free port, or -1. */
int Net_Open(void);
void Net_Close(int fd);
uint16_t Net_LocalPort(int fd);
bool Net_Send(int fd, NetAddr to, const void *data, int len);
/* One waiting datagram's length (0: nothing waiting). */
int Net_Recv(int fd, NetAddr *from, void *buf, int cap);

/* "1.2.3.4:5678" both ways. */
void Net_AddrString(NetAddr a, char *out, size_t size);
bool Net_ParseAddr(const char *text, NetAddr *out);
bool Net_AddrEqual(NetAddr a, NetAddr b);
bool Net_AddrUsable(NetAddr a);

/* The address other machines on the same network reach this one at.
 * Nothing is sent to find it out. */
bool Net_LanIp(uint32_t *ip);
/* A name lookup: blocking, so only from a worker thread. */
bool Net_Resolve(const char *host, uint16_t port, NetAddr *out);
/* n random bytes from the system. */
bool Net_RandomBytes(void *out, int bytes);
/* n random bytes from the system, as 2n lowercase hex digits. */
bool Net_RandomHex(char *out, size_t size, int bytes);
/* Milliseconds on a steady clock. */
uint32_t Net_Millis(void);

/* STUN (RFC 5389) Binding requests and answers. */
#define STUN_TXID 12
int Stun_Request(uint8_t *buf, int cap, const uint8_t txid[STUN_TXID]);
/* A STUN packet, as opposed to one of the game's. */
bool Stun_IsMessage(const uint8_t *buf, int len);
/* The address the server saw, from its answer to the request with txid. */
bool Stun_ParseResponse(const uint8_t *buf, int len, const uint8_t txid[STUN_TXID], NetAddr *mapped);

#endif
