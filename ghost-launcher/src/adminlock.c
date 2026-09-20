#include "adminlock.h"
#include <stdio.h>
#include <string.h>
#include "sha256.h"

/* Set by the Makefile from admin.local.mk (never committed). Empty = no admin mode. */
#ifndef ADMIN_LOCK_HASH
#define ADMIN_LOCK_HASH ""
#endif

#define ADMIN_LOCK_PREFIX "ghost-launcher-admin-v1:"

static bool ReadFirstLine(const char *path, char *out, size_t outSize) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    if (!fgets(out, (int)outSize, f)) {
        fclose(f);
        return false;
    }
    fclose(f);
    size_t len = strlen(out);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r' || out[len - 1] == ' ')) {
        out[--len] = '\0';
    }
    return true;
}

void AdminLock_Fingerprint(const char *machineId, char out[65]) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s%.200s", ADMIN_LOCK_PREFIX, machineId ? machineId : "");
    Sha256_Hex(buf, strlen(buf), out);
}

bool AdminLock_Configured(void) { return strlen(ADMIN_LOCK_HASH) == 64; }

bool AdminLock_IsUnlocked(void) {
    if (!AdminLock_Configured()) return false;
    char id[128];
    if (!ReadFirstLine("/etc/machine-id", id, sizeof(id)) &&
        !ReadFirstLine("/var/lib/dbus/machine-id", id, sizeof(id))) return false;
    if (id[0] == '\0') return false;
    char mine[65];
    AdminLock_Fingerprint(id, mine);
    return strcmp(mine, ADMIN_LOCK_HASH) == 0;
}
