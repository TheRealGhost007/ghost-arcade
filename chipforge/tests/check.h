#ifndef CHIPFORGE_CHECK_H
#define CHIPFORGE_CHECK_H

/* The one assertion the Chipforge suite uses. Every tests/test_<area>.c is
 * compiled by textual inclusion into test_main.c, so the counters are plain
 * statics shared by the whole translation unit, exactly as in the games'
 * suites. A failed check prints and carries on: one run reports everything. */
#include <stdio.h>

static int gChecks = 0;
static int gFailures = 0;

#define CHECK(cond, msg) do { \
    gChecks++; \
    if (!(cond)) { \
        gFailures++; \
        printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
    } \
} while (0)

#endif
