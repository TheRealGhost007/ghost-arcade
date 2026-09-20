#ifndef RUNSTATS_H
#define RUNSTATS_H

#include <time.h>

/* A summary of one game's runs.log (see ghost-common/runlog.h): how many runs,
 * when the last was, and how many happened on each of the last RUNSTATS_DAYS
 * days (index 0 = today). No raylib. */
#define RUNSTATS_DAYS 14

typedef struct {
    int runs;
    int zeroRuns;             /* runs that scored nothing */
    char last[11];            /* YYYY-MM-DD of the most recent run, "" if none */
    int perDay[RUNSTATS_DAYS];
} RunSummary;

/* Reads $XDG_DATA_HOME/<slug>/runs.log (or ~/.local/share). `now` is passed in so the tests can pin the date. */
void RunStats_Load(const char *slug, time_t now, RunSummary *out);

/* Whole days between a YYYY-MM-DD string and `now`'s date, or -1 if it does not parse. */
int RunStats_DaysAgo(const char *date, time_t now);

#endif
