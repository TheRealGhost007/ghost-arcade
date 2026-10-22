#ifndef TEST_CHECK_H
#define TEST_CHECK_H

#include <stdio.h>

/* The one assertion the launcher's tests use: counts, and prints what failed. */
extern int gChecks;
extern int gFailures;

#define CHECK(cond, msg) do { \
    gChecks++; \
    if (!(cond)) { \
        gFailures++; \
        printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
    } \
} while (0)

/* tests/test_kit.c: the button kit, the back stack and the screens' layouts. */
void test_kit_all(void);
/* tests/test_settings.c: the text field, the key table and the Settings page. */
void test_settings_all(void);
/* tests/test_hall.c: the hall's row, hit testing, pad map, strip and status line; the name board's grid. */
void test_hall_all(void);
/* tests/test_row.c: your row (orders, favourites, hiding, the place/catalog mapping), the retired-games migration, hall music, the quiet hall. */
void test_row_all(void);
/* tests/test_page.c: a machine's page and its boards, admin's form, the Machines manager's carry-over. */
void test_page_all(void);
/* tests/test_extras.c: the daily desk, attract mode, Chipforge as a jukebox. */
void test_extras_all(void);
/* tests/test_update.c: Update now: the log, the steps, why it failed or cannot start, update.txt for this build, the background run. */
void test_update_all(void);

#endif
