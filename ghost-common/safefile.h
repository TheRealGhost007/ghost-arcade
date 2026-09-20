#ifndef SAFEFILE_H
#define SAFEFILE_H

#include <stdbool.h>
#include <stdio.h>

/* Crash-safe file writing: SafeFile_Open writes to "<path>.tmp"; SafeFile_Close
 * flushes it to disk and renames it over <path>, so a crash, a full disk or a
 * power cut leaves either the old file or the new one, never half of one.
 * Use them where you would use fopen(path, "w") / fclose(f). Up to four files
 * may be open at once. SafeFile_Close returns false if anything failed (the
 * old file is then left as it was). No raylib. */
FILE *SafeFile_Open(const char *path);
bool SafeFile_Close(FILE *f);

#endif
