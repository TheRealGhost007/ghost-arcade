#ifndef NETMSG_H
#define NETMSG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* What online play says to the arcade's server, minus the transport
 * (lobby.c and relay.c carry it): where the server is, JSON in and out for
 * the room and crew calls, and the messages of the server's Realtime
 * channels that the relay rides on. No curl, no raylib, so all of it is
 * tested headlessly. */

/* The server: Ghost Launcher's online.conf (url=, key=) if it names one,
 * else the arcade's own project. The key is the publishable one. */
void NetMsg_ServerConfig(char *url, size_t urlSize, char *key, size_t keySize);
/* One line of online.conf, for the tests: fills url or key if it sets them. */
void NetMsg_ConfigLine(const char *line, char *url, size_t urlSize, char *key, size_t keySize);

/* JSON, the small part of it the server's answers use. */
/* s as a JSON string, quotes included; false if it does not fit. */
bool Json_Quote(const char *s, char *out, size_t size);
/* The value of a key in the object json points at (whitespace first is
 * fine), or NULL. Only that object's own keys, not ones inside its values. */
const char *Json_Find(const char *json, const char *key);
bool Json_GetString(const char *json, const char *key, char *out, size_t size);
bool Json_GetBool(const char *json, const char *key, bool *out);

/* Realtime channels (Phoenix protocol 1.0.0): join, send a packet, keep
 * alive; and what an incoming message is. Packets travel as hex. */
int Rt_Join(char *out, size_t size, const char *topic, int ref);
int Rt_Broadcast(char *out, size_t size, const char *topic, const uint8_t *data, int len, int ref);
int Rt_Heartbeat(char *out, size_t size, int ref);
typedef enum { RT_OTHER, RT_JOINED, RT_JOIN_FAILED, RT_PACKET, RT_CLOSED } RtKind;
/* For the channel topic; RT_PACKET fills data/len. joinRef is the ref the join was sent with. */
RtKind Rt_Parse(const char *msg, const char *topic, int joinRef, uint8_t *data, int cap, int *len);

#endif
