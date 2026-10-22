#ifndef RELAY_H
#define RELAY_H

#include <stdbool.h>
#include <stdint.h>

/* The relay: when two machines cannot reach each other directly (some
 * routers refuse), their packets travel through the arcade server's
 * Realtime broadcast channel for the room instead. The channel is named
 * after the room's random relay key, so only the two machines are on it.
 * It is slower and metered, so online play sends fewer, fuller packets
 * this way. A worker thread owns the websocket (libcurl). */

typedef enum { RELAY_OFF, RELAY_CONNECTING, RELAY_READY, RELAY_FAILED } RelayState;

/* relayKey: the room's 32 hex digits. */
bool Relay_Start(const char *relayKey);
RelayState Relay_State(void);
/* Queued for the worker; dropped when the queue is full or the relay isn't ready. */
void Relay_Send(const uint8_t *data, int len);
/* One packet from the other machine (its length), or 0 when none is waiting. */
int Relay_Recv(uint8_t *buf, int cap);
void Relay_Stop(void);

#endif
