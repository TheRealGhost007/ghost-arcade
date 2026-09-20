#define _POSIX_C_SOURCE 200809L
#include "daily.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static bool sActive = false;
static int sY, sM, sD;

uint64_t Daily_SeedForDate(int year, int month, int day) {
    uint64_t n = (uint64_t)year * 10000u + (uint64_t)month * 100u + (uint64_t)day;
    /* a couple of rounds of mixing so consecutive days are not consecutive seeds */
    n ^= n << 21; n *= 0x9E3779B97F4A7C15ull; n ^= n >> 29; n *= 0xBF58476D1CE4E5B9ull; n ^= n >> 32;
    return n ? n : 1;
}

void Daily_ModeForDate(int year, int month, int day, char *out, int size) {
    snprintf(out, (size_t)size, "daily-%04d%02d%02d", year, month, day);
}

void Daily_Init(int argc, char **argv) {
    sActive = false;
    for (int i = 1; i < argc; i++) if (argv[i] && strcmp(argv[i], "--daily") == 0) sActive = true;
    time_t now = time(NULL);
    struct tm t;
    if (localtime_r(&now, &t) == NULL) { sActive = false; return; }
    sY = t.tm_year + 1900; sM = t.tm_mon + 1; sD = t.tm_mday;
}

bool Daily_Active(void) { return sActive; }
uint64_t Daily_Seed(void) { return Daily_SeedForDate(sY, sM, sD); }

const char *Daily_Mode(void) {
    static char buf[24];
    Daily_ModeForDate(sY, sM, sD, buf, sizeof(buf));
    return buf;
}
