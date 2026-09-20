#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>

/* SHA-256 of a byte string, as 64 lowercase hex characters plus a NUL. */
void Sha256_Hex(const void *data, size_t len, char out[65]);

#endif
