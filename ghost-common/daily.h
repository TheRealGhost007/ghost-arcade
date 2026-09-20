#ifndef DAILY_H
#define DAILY_H

#include <stdbool.h>
#include <stdint.h>

/* The daily challenge: launched with --daily (Ghost Launcher's D key), a game
 * plays from a seed derived from today's date, so everyone gets the same moon,
 * tower or wave layout that day, and its scores go to a board of their own,
 * "daily-YYYYMMDD". No raylib. */
void Daily_Init(int argc, char **argv);
bool Daily_Active(void);
/* Same value all day, different every day. */
uint64_t Daily_Seed(void);
/* "daily-YYYYMMDD": a valid score-table mode. */
const char *Daily_Mode(void);
/* Pure helpers, for tests. */
uint64_t Daily_SeedForDate(int year, int month, int day);
void Daily_ModeForDate(int year, int month, int day, char *out, int size);

#endif
