#ifndef ADMINLOCK_H
#define ADMINLOCK_H

#include <stdbool.h>

/* Admin mode (adding, editing and removing catalog entries from inside the
 * launcher) is unlocked only on the machine the owner chose.
 *
 * Nothing that identifies that machine is in the source. `make admin-lock`
 * writes a one-way fingerprint of THIS computer's /etc/machine-id,
 *     sha256("ghost-launcher-admin-v1:" + machine-id)
 * into admin.local.mk, which git ignores; the build bakes that fingerprint in.
 * A build without that file (everyone else's) has no admin mode at all, and a
 * copy of the owner's binary does not unlock anywhere else.
 *
 * Be clear about what this is: a gate on a convenience screen, not a security
 * boundary. The catalog is a plain text file its owner can always edit, and
 * someone building from source can lock admin mode to their own machine. It
 * gives no access to anyone else's computer, scores or the online database. */
bool AdminLock_IsUnlocked(void);

/* The fingerprint for a machine id (exposed for the tests and `make admin-lock`). */
void AdminLock_Fingerprint(const char *machineId, char out[65]);
/* True if this build has a fingerprint baked in. */
bool AdminLock_Configured(void);

#endif
