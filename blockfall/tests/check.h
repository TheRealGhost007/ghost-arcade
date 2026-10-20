#ifndef BLOCKFALL_TEST_CHECK_H
#define BLOCKFALL_TEST_CHECK_H

#include <stdio.h>

/* The one assertion Blockfall's tests use: counts, and prints what failed. */
extern int gChecks;
extern int gFailures;

#define CHECK(cond, msg) do { \
    gChecks++; \
    if (!(cond)) { \
        gFailures++; \
        printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
    } \
} while (0)

/* tests/test_rules.c: lock delay, gravity, drop points, classic scoring,
 * kicks, the ghost, Zen hold, the power-up order, lock-out, the sweep hint. */
void test_rules_all(void);
/* tests/test_records.c: what a run records (save.dat, the boards, Sprint by
 * time), this machine's custom-size table, the settings file, auto-repeat. */
void test_records_all(void);
/* tests/test_layout.c: the play screen's layout and the text that must fit
 * the well, on every board size. */
void test_layout_all(void);
/* tests/test_hud.c: the play screen's callouts, popups, count-up, nudges,
 * level-up pips, Ultra's colours and passing your best (hudfx.c). */
void test_hud_all(void);
/* tests/test_feel.c: how play feels (feel.c: the clear's replay, the piece's
 * motion, landing, Hold and Next) and a frame of a run (playstep.c: READY-GO,
 * the same play as before). */
void test_feel_all(void);
/* tests/test_kit.c: the shared button kit and back stack (ghost-common's
 * uikit.c and uinav.c), ported from Ghost Launcher, and what the kit added. */
void test_kit_all(void);
/* tests/test_input.c: the shared input reader, key table and text field;
 * Blockfall's menu keys, what keys and the pad do in a run, Motion. */
void test_input_all(void);
/* tests/test_menus.c: what the menus decide, the skyline, the Back stack. */
void test_menus_all(void);
/* tests/test_around.c: around a run: the run's counters, the pause menu, the
 * results card, the block wipe, the countdown, the boards Scores lists and
 * the settings 2.5.0 added. */
void test_around_all(void);
/* tests/test_settings.c: Settings with tabs (config.ini's new lines, key
 * words, binding and swaps, Feel, the page) and How to play's demos. */
void test_settings_all(void);

#endif
