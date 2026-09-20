#define _POSIX_C_SOURCE 200809L
#include "safefile.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define SAFEFILE_SLOTS 4
#define SAFEFILE_PATH 900

static struct { FILE *f; char path[SAFEFILE_PATH]; } sOpen[SAFEFILE_SLOTS];

FILE *SafeFile_Open(const char *path) {
    if (!path || strlen(path) + 5 >= SAFEFILE_PATH) return NULL;
    int slot = -1;
    for (int i = 0; i < SAFEFILE_SLOTS; i++) if (!sOpen[i].f) { slot = i; break; }
    if (slot < 0) return NULL;
    char tmp[SAFEFILE_PATH + 8];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return NULL;
    sOpen[slot].f = f;
    snprintf(sOpen[slot].path, sizeof(sOpen[slot].path), "%s", path);
    return f;
}

bool SafeFile_Close(FILE *f) {
    if (!f) return false;
    int slot = -1;
    for (int i = 0; i < SAFEFILE_SLOTS; i++) if (sOpen[i].f == f) { slot = i; break; }
    if (slot < 0) return fclose(f) == 0; /* not one of ours: behave like fclose */
    char path[SAFEFILE_PATH], tmp[SAFEFILE_PATH + 8];
    snprintf(path, sizeof(path), "%s", sOpen[slot].path);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    sOpen[slot].f = NULL;

    bool ok = fflush(f) == 0 && !ferror(f);
    if (ok) fsync(fileno(f));
    if (fclose(f) != 0) ok = false;
    if (ok && rename(tmp, path) != 0) ok = false;
    if (!ok) remove(tmp);
    return ok;
}
