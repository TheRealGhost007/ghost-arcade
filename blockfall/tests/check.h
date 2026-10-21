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
/* tests/test_danger.c: the danger warning (free rows, hysteresis, the
 * warning's 8 s, the next piece's outline), the music's intensity in every
 * mode, Ultra's last ticks, Sprint's gold counter, splits (marks, diffs,
 * words, the pace file, the finish line, the callout), the clock on GO. */
void test_danger_all(void);
/* tests/test_sound.c: which sounds a frame asks for (soundplan.c): one big
 * sound and one accent per lock, the pitches, the delayed level up, the
 * jingles, and that no effect changes the rules. */
void test_sound_all(void);
/* tests/test_powerfx.c: the power-ups' shows in time (powerfx.c) and the
 * Sweep's replay (feel.c). */
void test_powerfx_all(void);
/* tests/test_modern.c: modern rules on every board (3.2.0): the SRS kick
 * tables against the published ones, no climbing, lock resets on a kick,
 * T-spins and minis, perfect clears, back-to-back chains, the seed's piece
 * sequence unchanged, the Next queue and its setting, the optional 180 key,
 * the classic Marathon's fresh board and the old-rules board read only. */
void test_modern_all(void);
/* tests/test_stats.c: the lifetime stats (3.3.0): the file's round trip
 * and its tolerance, the fold of a scripted run (which runs count, the
 * counters, the T-spin clears), the three records, the total over the
 * modes, the lines-by-clear bar and the words. */
void test_stats_all(void);
/* tests/test_dig.c: Dig mode (3.4.0): the garbage it starts with (one gap
 * a row, never the row below's), a seed's pieces unchanged, the rise
 * timing curve, the rises (the stack and the piece lifted, the top-out), the
 * warning bar, Dug counting and its bonus, the level that stays, a pause
 * holding the rise, its board keys, the panel, the results card, the
 * callout, the rise's look and sound, the setup words and the demo. */
void test_dig_all(void);
/* tests/test_practice.c: the Practice room (3.5.0): its options bend the
 * rules, undo puts the exact game back up to 50 steps, the finesse search
 * and counter, nothing recorded, the eleventh action Undo (controls,
 * config.ini, play keys, the Settings page), the panel and the words. */
void test_practice_all(void);
/* tests/test_daily.c: the Daily challenge (3.6.0): the day and its key,
 * the twist's weekly rotation, the same date's pieces and power-ups, the
 * rules on a daily (the clock in every twist, the fast start, Dig, a
 * restart), what it records (the day's board, never a normal one), the
 * streak file and logic, and the panel, music, sounds, card and words. */
void test_daily_all(void);

#endif
