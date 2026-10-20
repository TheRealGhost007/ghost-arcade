/* Phase 6 (3.0.0): Settings with tabs and How to play with moving examples.
 *  - config.ini: an old file loads unchanged, the new lines have defaults,
 *    everything reads back, nonsense is clamped, an older build can read it.
 *  - Every key name config.ini can hold has words; every pad button too.
 *  - Binding: a clash swaps the two actions, Esc and Menu are never handed
 *    over, no action is ever left without a key or a button (a fuzz), and
 *    clearing, resetting and the keys turned away.
 *  - Feel: the defaults are today's feel to the bit; delay, speed (0 = to
 *    the wall), the soft drop's speeds, rotate left, READY keeping turns in
 *    order; the try-it strip.
 *  - The Settings page: every tab's rows fit the window and do not overlap,
 *    the arrows' walk, what each change does, the capture timer.
 *  - How to play: every demo clears the rows its page says it does (and
 *    every power-up does what it says), loops the same, never tops out.
 *  - Block patterns: seven different marks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "check.h"
#include "rng.h"
#include "keynames.h"
#include "uikit.h"
#include "uikeymap.h"
#include "../src/persist.h"
#include "../src/controls.h"
#include "../src/actions.h"
#include "../src/playstep.h"
#include "../src/setpage.h"
#include "../src/howtodemo.h"
#include "../src/look.h"
#include "../src/menukeys.h"

#define FRAME (1.0f / 60.0f)

static void ConfigPath(char *out, size_t size) {
    char dir[600];
    snprintf(dir, sizeof(dir), "%s/blockfall", getenv("XDG_CONFIG_HOME"));
    mkdir(dir, 0755);
    snprintf(out, size, "%s/config.ini", dir);
}

static void WriteText(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs(text, f);
    fclose(f);
}

static void ReadText(const char *path, char *out, size_t size) {
    out[0] = '\0';
    FILE *f = fopen(path, "r");
    if (!f) return;
    size_t n = fread(out, 1, size - 1, f);
    out[n] = '\0';
    fclose(f);
}

/* ------------------------------------------------------------ config.ini */

static const char *kOwner200 = "# blockfall settings\nleft=LEFT\nleft2=A\nright=RIGHT\nright2=D\nsoft_drop=DOWN\nsoft_drop2=S\nrotate=UP\n"
                               "rotate2=W\nhard_drop=SPACE\nhold=C\npause=P\nrestart=R\nquit=ESCAPE\nvolume=50\naudio_enabled=1\nsetup_mode=3\n"
                               "setup_preset=1\nsetup_width=10\nsetup_height=20\n";

static bool Invariants(const Settings *s, char *why, size_t size);

static void test_config(void) {
    char path[700], text[8192], why[96] = "";
    ConfigPath(path, sizeof(path));

    /* The owner's 2.0.0 file: every line as it was, the new ones defaulted. */
    WriteText(path, kOwner200);
    Settings s;
    CHECK(Settings_Load(&s), "config 3.0: the owner's 2.0.0 file is found");
    CHECK(strcmp(s.left, "LEFT") == 0 && strcmp(s.left2, "A") == 0 && strcmp(s.rotate2, "W") == 0 && strcmp(s.hardDrop, "SPACE") == 0 &&
              strcmp(s.hold, "C") == 0 && strcmp(s.pause, "P") == 0 && strcmp(s.restart, "R") == 0 && strcmp(s.quit, "ESCAPE") == 0 &&
              s.volumePercent == 50 && s.audioEnabled && s.setupMode == 3 && s.setupPreset == 1,
          "config 3.0: every key and value of the old file loads unchanged");
    CHECK(strcmp(s.rotateLeft, "Z") == 0 && Controls_IsNone(s.rotateLeft2) && Controls_IsNone(s.hardDrop2) && Controls_IsNone(s.hold2) &&
              Controls_IsNone(s.restart2),
          "config 3.0: Rotate left takes the free Z; the new second keys start empty");
    CHECK(s.pad[ACT_HARD][0] == PB_A && s.pad[ACT_ROTATE][0] == PB_UP && s.pad[ACT_ROTATE_LEFT][0] == PB_X && s.pad[ACT_HOLD][0] == PB_Y &&
              s.pad[ACT_HOLD][1] == PB_LB && s.pad[ACT_PAUSE][0] == PB_MENU && s.pad[ACT_PAUSE][1] == PB_B && s.pad[ACT_RESTART][0] == PB_VIEW &&
              s.pad[ACT_LEFT][0] == PB_LEFT && s.pad[ACT_SOFT][0] == PB_DOWN && s.pad[ACT_LEFT][1] == PAD_NONE,
          "config 3.0: the pad is as it always was, plus X for Rotate left and LB as Hold's second");
    CHECK(s.repeatDelayMs == 170 && s.repeatSpeedMs == 35 && s.softDropSpeed == SOFT_NORMAL && s.grid == GRID_ON && !s.blockPatterns &&
              s.autoPause && !s.showHowTo && s.ghostPiece == GHOST_OUTLINE && Controls_AllDifferent(&s),
          "config 3.0: Feel, Look and Game start as Blockfall always was");

    /* A 2.1.0 file with every key moved off its default (and both second
     * keys cleared), so a key that loaded cannot pass for one that was
     * defaulted: each must read back as written. Esc is Hard drop's, as a
     * 2.1.0 swap could leave it. (Pause's second is N, not Space: Space
     * picks menu items, and the load mends it, which is tested below.) */
    WriteText(path, "# blockfall settings\nleft=J\nleft2=NONE\nright=L\nright2=NONE\nsoft_drop=K\nsoft_drop2=NONE\nrotate=I\nrotate2=NONE\n"
                    "hard_drop=ESCAPE\nhold=H\npause=O\nrestart=T\nquit=N\nvolume=35\naudio_enabled=0\nsetup_mode=1\nsetup_preset=2\n"
                    "setup_width=12\nsetup_height=22\n");
    CHECK(Settings_Load(&s), "config 3.0: a 2.1.0 file with every key moved is found");
    CHECK(strcmp(s.left, "J") == 0 && strcmp(s.right, "L") == 0 && strcmp(s.softDrop, "K") == 0 && strcmp(s.rotate, "I") == 0 &&
              strcmp(s.hardDrop, "ESCAPE") == 0 && strcmp(s.hold, "H") == 0 && strcmp(s.pause, "O") == 0 && strcmp(s.restart, "T") == 0 &&
              strcmp(s.quit, "N") == 0,
          "config 3.0: every rebound first key reads back as written (J L K I Esc H O T N)");
    CHECK(strcmp(s.left2, "NONE") == 0 && strcmp(s.right2, "NONE") == 0 && strcmp(s.softDrop2, "NONE") == 0 && strcmp(s.rotate2, "NONE") == 0 &&
              Controls_IsNone(s.left2) && Controls_IsNone(s.right2) && Controls_IsNone(s.softDrop2) && Controls_IsNone(s.rotate2),
          "config 3.0: the four cleared second keys read back as none, not as A D S W");
    CHECK(s.volumePercent == 35 && !s.audioEnabled && s.setupMode == 1 && s.setupPreset == 2 && s.setupWidth == 12 && s.setupHeight == 22,
          "config 3.0: volume, sound and the play setup read back as written");
    CHECK(Controls_EscTaken(&s) && Controls_AllDifferent(&s), "config 3.0: Esc is Hard drop's, as the file says, and no two keys clash");
    CHECK(strcmp(s.rotateLeft, "Z") == 0 && Controls_IsNone(s.rotateLeft2) && Controls_IsNone(s.hardDrop2) && Controls_IsNone(s.hold2) &&
              Controls_IsNone(s.restart2) && s.repeatDelayMs == 170 && s.repeatSpeedMs == 35 && s.softDropSpeed == SOFT_NORMAL &&
              s.grid == GRID_ON && s.ghostPiece == GHOST_OUTLINE && !s.blockPatterns && !s.showHowTo && s.autoPause,
          "config 3.0: the 3.0.0 lines it lacks stay at their defaults (Z free, so Rotate left takes it)");
    CHECK(s.pad[ACT_HARD][0] == PB_A && s.pad[ACT_HOLD][0] == PB_Y && s.pad[ACT_PAUSE][0] == PB_MENU && s.pad[ACT_PAUSE][1] == PB_B,
          "config 3.0: and the pad is as Blockfall came");

    /* Z already yours: Rotate left starts with none, and Z keeps its job. */
    WriteText(path, "hold=Z\nleft=J\n");
    Settings_Load(&s);
    CHECK(strcmp(s.hold, "Z") == 0 && Controls_IsNone(s.rotateLeft) && Controls_AllDifferent(&s),
          "config 3.0: an old file that already uses Z leaves Rotate left without a key (nothing shares one)");
    WriteText(path, "rotate_left=NONE\n");
    Settings_Load(&s);
    CHECK(Controls_IsNone(s.rotateLeft), "config 3.0: a Rotate left set to none stays none");

    /* Everything new set, saved and read back exactly. */
    Settings_Default(&s);
    Controls_BindKey(&s, ACT_HARD, 1, "X");
    Controls_BindKey(&s, ACT_HOLD, 1, "LEFT_SHIFT");
    Controls_BindPad(&s, ACT_HOLD, 0, PB_RB);
    s.repeatDelayMs = 110;
    s.repeatSpeedMs = 0;
    s.softDropSpeed = SOFT_INSTANT;
    s.grid = GRID_FAINT;
    s.blockPatterns = true;
    s.autoPause = false;
    s.showHowTo = true;
    s.ghostPiece = GHOST_SOLID;
    Settings back;
    CHECK(Settings_Save(&s) && Settings_Load(&back), "config 3.0: saved and found");
    CHECK(memcmp(&s, &back, sizeof(s)) == 0, "config 3.0: and read back exactly");
    ReadText(path, text, sizeof(text));
    static const char *const kOld[] = {"left=", "left2=", "right=", "right2=", "soft_drop=", "soft_drop2=", "rotate=", "rotate2=",
                                       "hard_drop=", "hold=", "pause=", "restart=", "quit=", "volume=", "audio_enabled=", "setup_mode=",
                                       "setup_preset=", "setup_width=", "setup_height=", "reduced_motion=", "hide_controls=",
                                       "piece_snap=", "ready_go=", "quick_restart=", "resume_countdown=", "ghost_piece="};
    bool all = true;
    for (size_t i = 0; i < sizeof(kOld) / sizeof(kOld[0]); i++) {
        char want[64];
        snprintf(want, sizeof(want), "\n%s", kOld[i]);
        if (!strstr(text, want)) all = false;
    }
    CHECK(all, "config 3.0: the file keeps every line an older build reads, in the same form");
    CHECK(strstr(text, "\nhard_drop2=X\n") && strstr(text, "\nhold2=LEFT_SHIFT\n") && strstr(text, "\npad_hold=RB\n") && strstr(text, "\npad_hold2=LB\n") &&
              strstr(text, "\ndas_ms=110\n") && strstr(text, "\narr_ms=0\n") && strstr(text, "\nsoft_drop_speed=instant\n") &&
              strstr(text, "\ngrid=faint\n") && strstr(text, "\nblock_patterns=1\n") && strstr(text, "\nauto_pause=0\n") &&
              strstr(text, "\nshow_howto=1\n") && strstr(text, "\nghost_piece=2\n") && strstr(text, "\nrestart2=NONE\n"),
          "config 3.0: and gains the new ones");
    CHECK(Keys_CodeFromName(KEY_NONE_NAME) == 0, "config 3.0: an older build reads NONE as a key that is never pressed");

    /* A hand edit that gives Enter, Space or pad A to Pause or Restart (the
     * menus' press keys, which Settings never allows there): the slot is
     * none, or back to how Blockfall came when it was all the action had. */
    WriteText(path, "pause=ENTER\nquit=ESCAPE\nrestart=R\nrestart2=SPACE\npad_pause=A\npad_pause2=B\npad_restart=VIEW\npad_restart2=A\n");
    Settings_Load(&s);
    CHECK(Controls_IsNone(s.pause) && strcmp(s.quit, "ESCAPE") == 0 && strcmp(s.restart, "R") == 0 && Controls_IsNone(s.restart2),
          "config 3.0: Enter on Pause and Space on Restart's second key load as none (Esc and R stay)");
    CHECK(s.pad[ACT_PAUSE][0] == PAD_NONE && s.pad[ACT_PAUSE][1] == PB_B && s.pad[ACT_RESTART][0] == PB_VIEW && s.pad[ACT_RESTART][1] == PAD_NONE,
          "config 3.0: pad A on Pause's first and Restart's second button load as none (B and View stay)");
    WriteText(path, "pause=SPACE\nquit=NONE\nrestart=ENTER\nrestart2=NONE\npad_pause=A\npad_pause2=NONE\npad_restart=A\npad_restart2=NONE\n");
    Settings_Load(&s);
    CHECK(strcmp(s.pause, "P") == 0 && Controls_IsNone(s.quit) && strcmp(s.restart, "R") == 0 && Controls_IsNone(s.restart2),
          "config 3.0: when it was the only key the action had, the default comes back (P, R)");
    CHECK(s.pad[ACT_PAUSE][0] == PB_MENU && s.pad[ACT_PAUSE][1] == PAD_NONE && s.pad[ACT_RESTART][0] == PB_VIEW && s.pad[ACT_RESTART][1] == PAD_NONE,
          "config 3.0: and the only button too (Menu, View)");
    CHECK(Invariants(&s, why, sizeof(why)), "config 3.0: the mended file holds every rule");
    WriteText(path, "hold=R\nrestart=ENTER\nrestart2=NONE\npad_hold=VIEW\npad_restart=A\npad_restart2=NONE\n");
    Settings_Load(&s);
    CHECK(strcmp(s.hold, "R") == 0 && Controls_IsNone(s.restart) && s.pad[ACT_HOLD][0] == PB_VIEW && s.pad[ACT_RESTART][0] == PAD_NONE && Controls_AllDifferent(&s),
          "config 3.0: the default is not brought back onto another action that has it: none instead");
    /* Both slots mended, or the second alone when the first is already none:
     * the action is never left with nothing. Its first default comes back
     * into the first slot (R, View; P, Menu). */
    WriteText(path, "restart=ENTER\nrestart2=SPACE\npad_restart=A\npad_restart2=A\npause=SPACE\nquit=ENTER\npad_pause=A\npad_pause2=A\n");
    Settings_Load(&s);
    CHECK(strcmp(s.restart, "R") == 0 && Controls_IsNone(s.restart2) && strcmp(s.pause, "P") == 0 && Controls_IsNone(s.quit),
          "config 3.0: Enter and Space in both of Restart's (Pause's) slots: R (P) comes back, the second stays none");
    CHECK(s.pad[ACT_RESTART][0] == PB_VIEW && s.pad[ACT_RESTART][1] == PAD_NONE && s.pad[ACT_PAUSE][0] == PB_MENU && s.pad[ACT_PAUSE][1] == PAD_NONE,
          "config 3.0: A in both pad slots: View (Menu) comes back, the second stays none");
    CHECK(Invariants(&s, why, sizeof(why)), "config 3.0: and that file holds every rule");
    WriteText(path, "restart=NONE\nrestart2=ENTER\npad_restart=NONE\npad_restart2=A\npause=NONE\nquit=SPACE\npad_pause=NONE\npad_pause2=A\n");
    Settings_Load(&s);
    CHECK(strcmp(s.restart, "R") == 0 && Controls_IsNone(s.restart2) && strcmp(s.pause, "P") == 0 && Controls_IsNone(s.quit),
          "config 3.0: a first slot of none and Enter (Space) in the second: R (P) comes back into the first, not the second's default");
    CHECK(s.pad[ACT_RESTART][0] == PB_VIEW && s.pad[ACT_RESTART][1] == PAD_NONE && s.pad[ACT_PAUSE][0] == PB_MENU && s.pad[ACT_PAUSE][1] == PAD_NONE,
          "config 3.0: the same on the pad: View (Menu) comes back into the first slot");
    CHECK(Invariants(&s, why, sizeof(why)), "config 3.0: and that file holds every rule too");
    /* The first default held by another action: the second default is
     * tried (Esc, B), and when there is none (R, View have none) the
     * action is left with none, as the file was already past mending. */
    WriteText(path, "hold=R\nrestart=ENTER\nrestart2=SPACE\npad_hold=VIEW\npad_restart=NONE\npad_restart2=A\nhold2=P\npause=NONE\nquit=SPACE\npad_hold2=MENU\npad_pause=A\npad_pause2=A\n");
    Settings_Load(&s);
    CHECK(strcmp(s.hold, "R") == 0 && Controls_IsNone(s.restart) && Controls_IsNone(s.restart2) && s.pad[ACT_HOLD][0] == PB_VIEW &&
              s.pad[ACT_RESTART][0] == PAD_NONE && s.pad[ACT_RESTART][1] == PAD_NONE,
          "config 3.0: Hold has R and View: Restart is left with none in both slots rather than taking them back");
    CHECK(strcmp(s.hold2, "P") == 0 && Controls_IsNone(s.pause) && strcmp(s.quit, "ESCAPE") == 0 && s.pad[ACT_HOLD][1] == PB_MENU &&
              s.pad[ACT_PAUSE][0] == PAD_NONE && s.pad[ACT_PAUSE][1] == PB_B && Controls_AllDifferent(&s),
          "config 3.0: Hold has P and Menu: Pause falls back to its second defaults, Esc and B");
    WriteText(path, "restart=NONE\nrestart2=NONE\npad_restart=NONE\npad_restart2=NONE\n");
    Settings_Load(&s);
    CHECK(Controls_IsNone(s.restart) && Controls_IsNone(s.restart2) && s.pad[ACT_RESTART][0] == PAD_NONE && s.pad[ACT_RESTART][1] == PAD_NONE,
          "config 3.0: a file that simply gives Restart nothing is left alone (only a mended slot brings the default back)");
    WriteText(path, "hard_drop=ENTER\nhold2=SPACE\npad_hold=A\npad_hard_drop=UP\npad_rotate=NONE\n");
    Settings_Load(&s);
    CHECK(strcmp(s.hardDrop, "ENTER") == 0 && strcmp(s.hold2, "SPACE") == 0 && s.pad[ACT_HOLD][0] == PB_A,
          "config 3.0: every other action keeps Enter, Space or A from the file");
    Settings_Default(&s);
    Settings_Save(&s);
    Settings_Load(&back);
    CHECK(memcmp(&s, &back, sizeof(s)) == 0, "config 3.0: the defaults come back untouched by the mending");

    /* Nonsense from a hand edit: clamped or ignored. */
    WriteText(path, "das_ms=9999\narr_ms=-5\nsoft_drop_speed=zoom\ngrid=purple\npad_hold=TURBO\npad_hold3=A\npad_pause2=Y\nghost_piece=7\n");
    Settings_Load(&s);
    CHECK(s.repeatDelayMs == 300 && s.repeatSpeedMs == 0 && s.softDropSpeed == SOFT_NORMAL && s.grid == GRID_ON && s.pad[ACT_HOLD][0] == PAD_NONE &&
              s.pad[ACT_HOLD][1] == PB_LB && s.pad[ACT_PAUSE][1] == PB_Y && s.ghostPiece == 7,
          "config 3.0: numbers clamped, unknown words defaulted, unknown buttons none, unknown keys ignored, a future ghost look kept");
    remove(path);
}

/* ---------------------------------------------------------------- words */

static void test_words(void) {
    int names = 0;
    bool words = true, shortFit = true;
    for (int code = 0; code < 512; code++) {
        const char *name = Keys_NameFromCode(code);
        if (!name) continue;
        names++;
        char full[32], shortName[32];
        Controls_Name(name, full, sizeof(full));
        Controls_ShortName(name, shortName, sizeof(shortName));
        if (!Controls_HasWords(name)) words = false;
        bool own = strlen(name) == 1;
        if (!own && strcmp(full, name) == 0) words = false; /* LEFT_SHIFT must not show as it is stored */
        if (strlen(shortName) > 6) shortFit = false;
    }
    CHECK(names >= 56 && words, "words: every key name config.ini can hold has words of its own (Left Shift, not LEFT_SHIFT)");
    CHECK(shortFit, "words: every short name fits the card (six letters at most)");
    bool pads = true;
    for (int b = 0; b < PB_COUNT; b++) {
        if (!Controls_PadCap(b)[0] || !Controls_PadWords(b)[0]) pads = false;
        if (Persist_PadFromName(Persist_PadName(b)) != b) pads = false;
        if (Kit_CapWidth(Controls_PadCap(b), true) > SET_PAD_W - 8) pads = false;
    }
    CHECK(pads && Persist_PadFromName("NONE") == PAD_NONE && strcmp(Persist_PadName(PAD_NONE), "NONE") == 0,
          "words: every pad button has a cap, words and a config.ini name, and its cap fits");
}

/* -------------------------------------------------------------- binding */

static bool Invariants(const Settings *s, char *why, size_t size) {
    if (!Controls_AllDifferent(s)) { snprintf(why, size, "two slots share"); return false; }
    for (int a = 0; a < ACT_COUNT; a++) {
        bool key = false, pad = false;
        for (int slot = 0; slot < CONTROL_SLOTS; slot++) {
            const char *k = Controls_KeyConst(s, a, slot);
            if (!Controls_IsNone(k)) key = true;
            if (Controls_Pad(s, a, slot) != PAD_NONE) pad = true;
            if (a != ACT_PAUSE && strcmp(k, "ESCAPE") == 0) { snprintf(why, size, "%s has Esc", Controls_ActionLabel(a)); return false; }
            if (a != ACT_PAUSE && Controls_Pad(s, a, slot) == PB_MENU) { snprintf(why, size, "%s has Menu", Controls_ActionLabel(a)); return false; }
            if ((a == ACT_PAUSE || a == ACT_RESTART) && (strcmp(k, "ENTER") == 0 || strcmp(k, "SPACE") == 0)) {
                snprintf(why, size, "%s has %s", Controls_ActionLabel(a), k);
                return false;
            }
            if ((a == ACT_PAUSE || a == ACT_RESTART) && Controls_Pad(s, a, slot) == PB_A) {
                snprintf(why, size, "%s has pad A", Controls_ActionLabel(a));
                return false;
            }
        }
        if (!key || !pad) { snprintf(why, size, "%s lost all its %s", Controls_ActionLabel(a), key ? "buttons" : "keys"); return false; }
        if (a != ACT_PAUSE && Controls_IsNone(Controls_KeyConst(s, a, 0))) { snprintf(why, size, "%s's first key is empty", Controls_ActionLabel(a)); return false; }
    }
    return true;
}

static void test_binding(void) {
    Settings s;
    Settings_Default(&s);
    char why[96] = "";
    CHECK(Invariants(&s, why, sizeof(why)) && Controls_AreDefault(&s), "binds: the defaults hold every rule");

    /* A fuzz of what the screen can do: any key it lets through (never Esc,
     * never M), any button but Menu, clears, now and then a reset. Enter,
     * Space and pad A go to every action here, Pause and Restart too: the
     * model itself must turn them away (and never hand them over in a
     * swap). */
    static const char *const kPool[] = {"A", "B", "C", "D", "J", "K", "P", "Q", "R", "S", "W", "X", "Z", "SPACE", "UP", "DOWN", "LEFT",
                                        "RIGHT", "LEFT_SHIFT", "RIGHT_SHIFT", "TAB", "ENTER", "COMMA", "1", "2"};
    Rng rng;
    Rng_Seed(&rng, 606);
    int ops = 0, swaps = 0, refusals = 0, withheld = 0, broken = 0, padARefused = 0, padAWithheld = 0;
    char first[320] = "";
    for (int i = 0; i < 6000; i++) {
        int a = (int)Rng_Range(&rng, ACT_COUNT), slot = (int)Rng_Range(&rng, 2), what = (int)Rng_Range(&rng, 100);
        BindResult r = {BIND_SAME, -1, -1, false, ""};
        if (what < 45) r = Controls_BindKey(&s, a, slot, kPool[Rng_Range(&rng, sizeof(kPool) / sizeof(kPool[0]))]);
        else if (what < 90) {
            int b = (int)Rng_Range(&rng, PB_COUNT);
            if (b == PB_MENU) continue;
            if (Rng_Range(&rng, 6) == 0) b = PB_A; /* A often, on Pause and Restart too */
            r = Controls_BindPad(&s, a, slot, b);
            if (b == PB_A && (a == ACT_PAUSE || a == ACT_RESTART) && r.kind == BIND_REFUSED && strstr(r.note, "A picks menu items")) padARefused++;
            if (r.kind == BIND_SWAPPED && r.withheld && (r.otherAction == ACT_PAUSE || r.otherAction == ACT_RESTART) && strstr(r.note, "A picks menu items"))
                padAWithheld++;
        } else if (what < 95) Controls_ClearKey(&s, a, slot);
        else if (what < 99) Controls_ClearPad(&s, a, slot);
        else Controls_Reset(&s);
        ops++;
        if (r.kind == BIND_SWAPPED) swaps++;
        if (r.kind == BIND_REFUSED) refusals++;
        if (r.withheld) withheld++;
        if (!Invariants(&s, why, sizeof(why))) {
            if (!broken) snprintf(first, sizeof(first), "op %d: %s (%s)", i, why, r.note);
            broken++;
            Settings_Default(&s);
        }
    }
    if (broken) printf("  binds: %s\n", first);
    CHECK(broken == 0 && swaps > 500 && refusals > 20 && withheld > 5,
          "binds: 6000 random binds, clears and resets: no two actions ever share a key or button, none loses its last one, Esc and Menu stay Pause's, "
          "Enter, Space and pad A never reach Pause or Restart");
    CHECK(padARefused > 20 && padAWithheld > 0, "binds: the fuzz tried A on Pause and Restart outright (refused) and through swaps (withheld)");
    printf("  binds: %d operations, %d swaps, %d refused, %d with Esc or Menu kept, pad A refused %d times and withheld %d\n", ops, swaps, refusals,
           withheld, padARefused, padAWithheld);

    /* A swap between two actions, by the pad. */
    Settings_Default(&s);
    BindResult r = Controls_BindPad(&s, ACT_ROTATE, 0, PB_A);
    CHECK(r.kind == BIND_SWAPPED && s.pad[ACT_ROTATE][0] == PB_A && s.pad[ACT_HARD][0] == PB_UP, "binds: pad A for Rotate swaps with Hard drop's d-pad Up");
    CHECK(strcmp(r.note, "A moved from Hard drop to Rotate right. Hard drop took D-pad up.") == 0, "binds: and the note names the buttons");
    r = Controls_BindPad(&s, ACT_HOLD, 1, PB_Y);
    CHECK(r.kind == BIND_SWAPPED && s.pad[ACT_HOLD][0] == PB_LB && s.pad[ACT_HOLD][1] == PB_Y, "binds: an action's two buttons swap places");
    r = Controls_BindPad(&s, ACT_RESTART, 1, PB_RT);
    CHECK(r.kind == BIND_SET && strcmp(r.note, "Restart's second button is now RT.") == 0, "binds: a free button just binds");
    /* The only button an action has stays where it is. */
    r = Controls_BindPad(&s, ACT_SOFT, 1, PB_A);
    CHECK(r.kind == BIND_REFUSED && s.pad[ACT_ROTATE][0] == PB_A && strcmp(r.note, "A is the only button Rotate right has, so it stays there.") == 0,
          "binds: taking an action's only button is refused, with a note");
    /* Clearing: second slots only. */
    Settings_Default(&s);
    CHECK(!Controls_ClearKey(&s, ACT_HOLD, 0) && !Controls_ClearPad(&s, ACT_HOLD, 0), "binds: a first key or button cannot be cleared");
    CHECK(Controls_ClearPad(&s, ACT_HOLD, 1) && s.pad[ACT_HOLD][1] == PAD_NONE && !Controls_ClearPad(&s, ACT_HOLD, 1),
          "binds: a second one can, once");
    CHECK(Controls_ClearKey(&s, ACT_PAUSE, 1) && Controls_IsNone(s.quit), "binds: Pause's second key (Esc) can be cleared: Esc still pauses");
    Controls_Reset(&s);
    CHECK(Controls_AreDefault(&s), "binds: Reset puts every key and button back");
    /* What capture turns away. */
    CHECK(!Controls_KeyAllowed(ACT_HOLD, "M", why, sizeof(why)) && strstr(why, "mutes"), "binds: M is the mute key, so it is turned away");
    CHECK(!Controls_KeyAllowed(ACT_HOLD, NULL, why, sizeof(why)) && !Controls_KeyAllowed(ACT_HOLD, "F5", why, sizeof(why)) &&
              !Controls_KeyAllowed(ACT_PAUSE, "KP_ENTER", why, sizeof(why)),
          "binds: so are keys Blockfall cannot store (the keypad's Enter among them)");
    CHECK(Controls_KeyAllowed(ACT_HOLD, "LEFT_SHIFT", why, sizeof(why)) && Controls_KeyAllowed(ACT_HOLD, "Z", why, sizeof(why)), "binds: Shift and letters are fine");
    /* Enter and Space pick menu items: the pause menu reads Pause and
     * Restart before its buttons, so neither action may have them. */
    for (int act = 0; act < ACT_COUNT; act++) {
        bool menuAct = act == ACT_PAUSE || act == ACT_RESTART;
        why[0] = '\0';
        bool enter = Controls_KeyAllowed(act, "ENTER", why, sizeof(why));
        bool enterWhy = strstr(why, "Enter and Space pick menu items") != NULL;
        bool space = Controls_KeyAllowed(act, "SPACE", why, sizeof(why));
        if (menuAct) CHECK(!enter && enterWhy && !space, "binds: Enter and Space are turned away from Pause and Restart, with the note");
        else CHECK(enter && space, "binds: every other action may have Enter or Space (Space is the hard drop)");
    }
    CHECK(!Controls_KeyAllowed(ACT_RESTART, "SPACE", why, sizeof(why)) && strcmp(why, "Enter and Space pick menu items, so they cannot pause or restart.") == 0,
          "binds: the note's words");
    /* The model keeps the same rule, bound outright or handed over by a
     * swap, both ways round. R onto Hard drop's first key would give Space
     * to Restart: refused while R is Restart's only key, and once Restart
     * has a second key, Space is withheld (Restart keeps the other one). */
    Settings_Default(&s);
    r = Controls_BindKey(&s, ACT_RESTART, 1, "ENTER");
    CHECK(r.kind == BIND_REFUSED && Controls_IsNone(s.restart2) && strcmp(r.note, "Enter and Space pick menu items, so they cannot pause or restart.") == 0,
          "binds: Enter bound outright to Restart is refused by the model too, with the same note");
    r = Controls_BindKey(&s, ACT_PAUSE, 0, "SPACE");
    CHECK(r.kind == BIND_REFUSED && strcmp(s.pause, "P") == 0 && strcmp(s.hardDrop, "SPACE") == 0, "binds: and Space to Pause");
    r = Controls_BindKey(&s, ACT_HARD, 0, "R");
    CHECK(r.kind == BIND_REFUSED && strcmp(s.restart, "R") == 0 && strcmp(s.hardDrop, "SPACE") == 0 && Controls_AllDifferent(&s),
          "binds: R onto Hard drop is refused: Space would go to Restart, whose only key R is");
    CHECK(strcmp(r.note, "R is the only key Restart has, and Space picks menu items, so it stays there.") == 0, "binds: the refusal's note says both");
    r = Controls_BindKey(&s, ACT_RESTART, 1, "T");
    CHECK(r.kind == BIND_SET, "binds: Restart gets a second key, T");
    r = Controls_BindKey(&s, ACT_HARD, 0, "R");
    CHECK(r.kind == BIND_SWAPPED && r.withheld && strcmp(s.hardDrop, "R") == 0 && strcmp(s.restart, "T") == 0 && Controls_IsNone(s.restart2) &&
              Controls_AllDifferent(&s),
          "binds: now R moves to Hard drop, Restart keeps T and is never given Space");
    CHECK(strcmp(r.note, "R moved from Restart to Hard drop. Space picks menu items, so Restart did not take it.") == 0, "binds: and the note says so");
    /* The other way round: Enter on Hold's second key, then Pause's P onto
     * that slot. Pause would get Enter: withheld (Esc stays), or refused
     * when Esc was cleared and P is all Pause has. */
    Settings_Default(&s);
    r = Controls_BindKey(&s, ACT_HOLD, 1, "ENTER");
    CHECK(r.kind == BIND_SET && strcmp(s.hold2, "ENTER") == 0, "binds: Enter is fine as Hold's second key");
    r = Controls_BindKey(&s, ACT_HOLD, 1, "P");
    CHECK(r.kind == BIND_SWAPPED && r.withheld && strcmp(s.hold2, "P") == 0 && Controls_IsNone(s.pause) && strcmp(s.quit, "ESCAPE") == 0 &&
              Controls_AllDifferent(&s),
          "binds: P onto that slot moves to Hold; Pause is never given Enter and keeps Esc");
    CHECK(strcmp(r.note, "P moved from Pause to Hold. Enter picks menu items, so Pause did not take it.") == 0, "binds: the note says so");
    Settings_Default(&s);
    Controls_BindKey(&s, ACT_HOLD, 1, "ENTER");
    CHECK(Controls_ClearKey(&s, ACT_PAUSE, 1), "binds: Pause's Esc cleared");
    r = Controls_BindKey(&s, ACT_HOLD, 1, "P");
    CHECK(r.kind == BIND_REFUSED && strcmp(s.pause, "P") == 0 && strcmp(s.hold2, "ENTER") == 0 &&
              strcmp(r.note, "P is the only key Pause has, and Enter picks menu items, so it stays there.") == 0,
          "binds: with P all Pause has, the swap is refused and the note says why");
    /* Any other action still takes Enter or Space in a swap. */
    Settings_Default(&s);
    r = Controls_BindKey(&s, ACT_HOLD, 0, "SPACE");
    CHECK(r.kind == BIND_SWAPPED && !r.withheld && strcmp(s.hold, "SPACE") == 0 && strcmp(s.hardDrop, "C") == 0 &&
              strcmp(r.note, "Space moved from Hard drop to Hold. Hard drop took C.") == 0,
          "binds: Space to Hold swaps with Hard drop as ever");

    /* The pad's A is the menus' press button (uikeymap.c maps it to
     * KA_UI_PRESS with Enter and Space): Pause and Restart never hold it
     * either. On a cap it is turned away, with its own note. */
    for (int act = 0; act < ACT_COUNT; act++) {
        bool menuAct = act == ACT_PAUSE || act == ACT_RESTART;
        why[0] = '\0';
        bool a = Controls_PadAllowed(act, PB_A, why, sizeof(why));
        bool aWhy = strcmp(why, "A picks menu items, so it cannot pause or restart.") == 0;
        bool others = Controls_PadAllowed(act, PB_B, why, sizeof(why)) && Controls_PadAllowed(act, PB_VIEW, why, sizeof(why)) &&
                      Controls_PadAllowed(act, PB_UP, why, sizeof(why)) && why[0] == '\0';
        if (menuAct) CHECK(!a && aWhy && others, "binds: pad A is turned away from Pause and Restart, with the note; every other button is fine");
        else CHECK(a && others, "binds: every other action may have pad A (it is the hard drop)");
    }
    CHECK(!Controls_PadAllowed(ACT_HOLD, -1, why, sizeof(why)) && !Controls_PadAllowed(ACT_HOLD, PB_COUNT, why, sizeof(why)),
          "binds: a button that is not one is turned away");
    /* Bound outright: refused by the model, the slot untouched. */
    Settings_Default(&s);
    r = Controls_BindPad(&s, ACT_RESTART, 0, PB_A);
    CHECK(r.kind == BIND_REFUSED && r.otherAction < 0 && s.pad[ACT_RESTART][0] == PB_VIEW && s.pad[ACT_HARD][0] == PB_A &&
              strcmp(r.note, "A picks menu items, so it cannot pause or restart.") == 0,
          "binds: pad A bound outright to Restart is refused by the model, with the note, and Hard drop keeps it");
    r = Controls_BindPad(&s, ACT_RESTART, 1, PB_A);
    CHECK(r.kind == BIND_REFUSED && s.pad[ACT_RESTART][1] == PAD_NONE, "binds: and to Restart's second button");
    r = Controls_BindPad(&s, ACT_PAUSE, 0, PB_A);
    CHECK(r.kind == BIND_REFUSED && s.pad[ACT_PAUSE][0] == PB_MENU && s.pad[ACT_HARD][0] == PB_A && Controls_AreDefault(&s),
          "binds: and to Pause's first; nothing moved");
    /* A swap that would hand A to Restart: View (Restart's only button)
     * onto Hard drop's first, which holds A. Refused; once Restart has a
     * second button, View moves and A is withheld (Restart keeps RT). */
    r = Controls_BindPad(&s, ACT_HARD, 0, PB_VIEW);
    CHECK(r.kind == BIND_REFUSED && s.pad[ACT_HARD][0] == PB_A && s.pad[ACT_RESTART][0] == PB_VIEW && Controls_AllDifferent(&s),
          "binds: View onto Hard drop is refused: A would go to Restart, whose only button View is");
    CHECK(strcmp(r.note, "View is the only button Restart has, and A picks menu items, so it stays there.") == 0, "binds: the refusal's note says both");
    r = Controls_BindPad(&s, ACT_RESTART, 1, PB_RT);
    CHECK(r.kind == BIND_SET, "binds: Restart gets a second button, RT");
    r = Controls_BindPad(&s, ACT_HARD, 0, PB_VIEW);
    CHECK(r.kind == BIND_SWAPPED && r.withheld && s.pad[ACT_HARD][0] == PB_VIEW && s.pad[ACT_RESTART][0] == PB_RT && s.pad[ACT_RESTART][1] == PAD_NONE &&
              Controls_AllDifferent(&s) && Invariants(&s, why, sizeof(why)),
          "binds: now View moves to Hard drop, Restart keeps RT and is never given A");
    CHECK(strcmp(r.note, "View moved from Restart to Hard drop. A picks menu items, so Restart did not take it.") == 0, "binds: and the note says so");
    /* The other way round: A on Hold's second button (a swap with Hard
     * drop), then Pause's B onto that slot. Pause would get A: withheld
     * (Menu stays), or refused when B is all Pause has. */
    Settings_Default(&s);
    r = Controls_BindPad(&s, ACT_HOLD, 1, PB_A);
    CHECK(r.kind == BIND_SWAPPED && s.pad[ACT_HOLD][1] == PB_A && s.pad[ACT_HARD][0] == PB_LB, "binds: A is fine as Hold's second button (Hard drop took LB)");
    r = Controls_BindPad(&s, ACT_HOLD, 1, PB_B);
    CHECK(r.kind == BIND_SWAPPED && r.withheld && s.pad[ACT_HOLD][1] == PB_B && s.pad[ACT_PAUSE][0] == PB_MENU && s.pad[ACT_PAUSE][1] == PAD_NONE &&
              Controls_AllDifferent(&s) && Invariants(&s, why, sizeof(why)),
          "binds: B onto that slot moves to Hold; Pause is never given A and keeps Menu");
    CHECK(strcmp(r.note, "B moved from Pause to Hold. A picks menu items, so Pause did not take it.") == 0, "binds: the note says so");
    Settings_Default(&s);
    Controls_BindPad(&s, ACT_HOLD, 1, PB_A);
    r = Controls_BindPad(&s, ACT_HOLD, 1, PB_VIEW);
    CHECK(r.kind == BIND_REFUSED && s.pad[ACT_HOLD][1] == PB_A && s.pad[ACT_RESTART][0] == PB_VIEW &&
              strcmp(r.note, "View is the only button Restart has, and A picks menu items, so it stays there.") == 0,
          "binds: with View all Restart has, the swap is refused and the note says why");
    /* Any other action still takes A in a swap. */
    Settings_Default(&s);
    r = Controls_BindPad(&s, ACT_ROTATE_LEFT, 0, PB_A);
    CHECK(r.kind == BIND_SWAPPED && !r.withheld && s.pad[ACT_ROTATE_LEFT][0] == PB_A && s.pad[ACT_HARD][0] == PB_X &&
              strcmp(r.note, "A moved from Hard drop to Rotate left. Hard drop took X.") == 0,
          "binds: A to Rotate left swaps with Hard drop as ever");

    /* The card's pad words follow the buttons. */
    ControlWords w;
    Settings_Default(&s);
    Controls_Words(&s, &w);
    CHECK(strcmp(w.padMove, "Left Right") == 0 && strcmp(w.padHold, "Y LB") == 0 && strcmp(w.padPause, "Menu B") == 0 &&
              strcmp(w.padRotateLeft, "X") == 0 && strcmp(w.padRestart, "View") == 0,
          "binds: the card's pad row names your buttons");
    Controls_BindPad(&s, ACT_PAUSE, 0, PB_RS);
    Controls_Words(&s, &w);
    CHECK(strcmp(w.padPause, "Menu RS B") == 0, "binds: Menu still shows on Pause after its slot is rebound (it always pauses)");
}

/* ---------------------------------------------------------------- Feel */

/* An O held against a wall of a 24-wide board for `frames` frames: how many
 * cells it went, and the frame of the first auto-repeat step. */
static int StepsHeld(PlayHandling h, int frames, int *firstRepeat, int *repeats) {
    Game g;
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 24, 20, true}, 1, 0, 1, 0);
    g.current = PIECE_O;
    g.rotation = 0;
    g.px = 21; /* the O's blocks at 22 and 23: against the right wall */
    g.py = BOARD_HIDDEN;
    int start = g.px;
    PlayDriver d;
    memset(&d, 0, sizeof(d));
    PlayDriver_SetHandling(&d, h);
    PlayIntent in;
    memset(&in, 0, sizeof(in));
    in.left = true;
    *firstRepeat = -1;
    *repeats = 0;
    for (int f = 0; f < frames; f++) {
        PlayStepResult out;
        g.fallTimer = 0.0f; /* no gravity: only sideways */
        Play_Step(&d, &g, &in, FRAME, 0.0f, &out);
        if (out.moved && out.moveRepeat) {
            (*repeats)++;
            if (*firstRepeat < 0) *firstRepeat = f;
        }
        Game_ConsumeFrameFlags(&g);
    }
    return start - g.px;
}

/* The frame the first repeat comes on: the first whose time held reaches
 * the delay (time is counted from the frame after the press). */
static int RepeatFrame(float delay) {
    float t = 0.0f;
    for (int f = 1; f < 1000; f++) {
        t += FRAME;
        if (t >= delay) return f;
    }
    return -1;
}

static void test_feel_settings(void) {
    PlayHandling def = PlayHandling_Default(), from = PlayHandling_From(170, 35, SOFT_NORMAL);
    CHECK(def.delay == PLAY_DAS_SECONDS && def.speed == PLAY_ARR_SECONDS && def.soft == PLAY_SOFT_DROP_SECONDS && from.delay == def.delay &&
              from.speed == def.speed && from.soft == def.soft,
          "feel 3.0: Standard (170 ms, 35 ms, Normal) is today's feel to the bit");
    PlayHandling clamp = PlayHandling_From(1, 9999, 42);
    CHECK(clamp.delay == 0.05f && clamp.speed == 0.1f && clamp.soft == def.soft, "feel 3.0: values out of range are clamped");

    /* A driver set to the defaults plays frame for frame as a zeroed one. */
    Rng rng;
    Rng_Seed(&rng, 77);
    int diverged = 0;
    for (int run = 0; run < 6; run++) {
        Game a, b;
        GameConfig cfg = {(GameMode)(run % MODE_COUNT), 8 + run, 16 + run, run & 1};
        Game_InitConfig(&a, cfg, (uint64_t)(50 + run), 0, 1, 0);
        Game_InitConfig(&b, cfg, (uint64_t)(50 + run), 0, 1, 0);
        PlayDriver da, db;
        memset(&da, 0, sizeof(da));
        memset(&db, 0, sizeof(db));
        PlayDriver_SetHandling(&db, PlayHandling_From(170, 35, SOFT_NORMAL));
        PlayIntent in;
        memset(&in, 0, sizeof(in));
        for (int f = 0; f < 2000 && !diverged; f++) {
            if (Rng_Range(&rng, 10) == 0) in.left = !in.left;
            if (Rng_Range(&rng, 10) == 0) in.right = !in.right;
            if (Rng_Range(&rng, 16) == 0) in.softDrop = !in.softDrop;
            in.rotate = Rng_Range(&rng, 15) == 0;
            in.rotateLeft = Rng_Range(&rng, 17) == 0;
            in.hardDrop = Rng_Range(&rng, 60) == 0;
            PlayStepResult oa, ob;
            Play_Step(&da, &a, &in, FRAME, 1.0f, &oa);
            Play_Step(&db, &b, &in, FRAME, 1.0f, &ob);
            if (a.px != b.px || a.py != b.py || a.score != b.score || a.piecesLocked != b.piecesLocked || a.rotation != b.rotation) diverged++;
            Game_ConsumeFrameFlags(&a);
            Game_ConsumeFrameFlags(&b);
            if (a.phase == GS_GAMEOVER) { Game_Restart(&a, (uint64_t)f); Game_Restart(&b, (uint64_t)f); }
        }
    }
    CHECK(diverged == 0, "feel 3.0: the defaults set from config.ini play exactly as the built-in feel");

    int rep, repeats;
    int cells = StepsHeld(PlayHandling_From(170, 35, SOFT_NORMAL), 60, &rep, &repeats);
    CHECK(rep == RepeatFrame(0.17f) && repeats >= 21 && repeats <= 23, "feel 3.0: Standard repeats after 0.17 s, then every 0.035 s");
    cells = StepsHeld(PlayHandling_From(50, 35, SOFT_NORMAL), 30, &rep, &repeats);
    CHECK(rep == RepeatFrame(0.05f) && rep <= 4, "feel 3.0: a 50 ms delay repeats after three or four frames");
    cells = StepsHeld(PlayHandling_From(300, 100, SOFT_NORMAL), 60, &rep, &repeats);
    CHECK(rep == RepeatFrame(0.3f) && repeats >= 6 && repeats <= 7, "feel 3.0: 300 ms and 100 ms: slow to start, then a step every 6 frames");
    cells = StepsHeld(PlayHandling_From(170, 0, SOFT_NORMAL), RepeatFrame(0.17f) + 1, &rep, &repeats);
    CHECK(cells == 22, "feel 3.0: a repeat speed of 0 goes straight to the wall once the delay is over");
    cells = StepsHeld(PlayHandling_From(170, 0, SOFT_NORMAL), RepeatFrame(0.17f), &rep, &repeats);
    CHECK(cells == 1, "feel 3.0: ...and not before (the tap is still one cell)");

    /* The soft drop's speeds. */
    static const int kSpeeds[3] = {SOFT_SLOW, SOFT_NORMAL, SOFT_FAST};
    int rows[3];
    for (int i = 0; i < 3; i++) {
        Game g;
        Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 36, false}, 3, 0, 1, 0);
        PlayDriver d;
        memset(&d, 0, sizeof(d));
        PlayDriver_SetHandling(&d, PlayHandling_From(170, 35, kSpeeds[i]));
        PlayIntent in;
        memset(&in, 0, sizeof(in));
        in.softDrop = true;
        int y0 = g.py;
        for (int f = 0; f < 30; f++) {
            PlayStepResult out;
            g.fallTimer = 0.0f;
            Play_Step(&d, &g, &in, FRAME, 1.0f, &out);
            Game_ConsumeFrameFlags(&g);
        }
        rows[i] = g.py - y0;
    }
    /* 30 frames is 0.5 s: about 9, 17 and 30 rows (Fast is capped at a row a frame). */
    CHECK(rows[0] >= 9 && rows[0] <= 10 && rows[1] >= 17 && rows[1] <= 18 && rows[2] == 30,
          "feel 3.0: soft drop Slow, Normal and Fast: a row every 60, 30 and 15 ms");
    Game g;
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, false}, 4, 0, 1, 0);
    PlayDriver d;
    memset(&d, 0, sizeof(d));
    PlayDriver_SetHandling(&d, PlayHandling_From(170, 35, SOFT_INSTANT));
    PlayIntent in;
    memset(&in, 0, sizeof(in));
    in.softDrop = true;
    PlayStepResult out;
    int ghost = Game_GhostY(&g), y0 = g.py;
    long score = g.score;
    Play_Step(&d, &g, &in, FRAME, 1.0f, &out);
    CHECK(g.py == ghost && g.piecesLocked == 0 && g.phase == GS_PLAYING && g.score == score + (ghost - y0),
          "feel 3.0: Instant takes the piece to the floor in one frame, a point a row, and does not lock it");

    /* Rotate left, in play and kept through READY in order. */
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, false}, 5, 0, 1, 0);
    g.current = PIECE_T;
    memset(&d, 0, sizeof(d));
    memset(&in, 0, sizeof(in));
    in.rotateLeft = true;
    Play_Step(&d, &g, &in, FRAME, 1.0f, &out);
    CHECK(g.rotation == 3 && out.rotated, "feel 3.0: Rotate left turns counter-clockwise");
    Game_InitConfig(&g, (GameConfig){MODE_SPRINT, 10, 20, false}, 6, 0, 1, 0);
    g.current = PIECE_T;
    memset(&d, 0, sizeof(d));
    PlayDriver_Start(&d, true, false);
    memset(&in, 0, sizeof(in));
    int frames = 0;
    while (d.cue.stage == CUE_READY && frames < 60) {
        memset(&in, 0, sizeof(in));
        in.rotateLeft = frames == 3 || frames == 6;
        in.rotate = frames == 9;
        Play_Step(&d, &g, &in, FRAME, 1.0f, &out);
        Game_ConsumeFrameFlags(&g);
        frames++;
    }
    CHECK(d.cue.stage == CUE_GO && g.rotation == 3, "feel 3.0: turns pressed in READY are made at GO, left left right: one left in all");
}

/* ---------------------------------------------------------------- strip */

static void test_strip(void) {
    PlayHandling std = PlayHandling_From(170, 35, SOFT_NORMAL);
    FeelStrip s;
    FeelStrip_Reset(&s);
    int maxX = STRIP_CELLS - STRIP_PIECE_W;
    int f = 0;
    while (s.x < maxX && f < 200) { FeelStrip_Tick(&s, false, false, &std, FRAME); f++; }
    CHECK(s.x == maxX && f >= 21 && f <= 23, "strip: left alone it shows the values: Right held to the wall (a step, 0.17 s, then a step every 0.035 s)");
    int rest = 0;
    while (s.x == maxX && rest < 200) { FeelStrip_Tick(&s, false, false, &std, FRAME); rest++; }
    CHECK(rest >= 29 && rest <= 31 && s.x == maxX - 1, "strip: rests half a second at the wall, then Left");
    FeelStrip_Reset(&s);
    s.idle = 0.0f;
    FeelStrip_Tick(&s, false, true, &std, FRAME);
    CHECK(s.x == 1 && s.moved && !s.repeat, "strip: Right held there moves it at once");
    for (int i = 1; i < RepeatFrame(0.17f); i++) FeelStrip_Tick(&s, false, true, &std, FRAME);
    CHECK(s.x == 1, "strip: ...and waits the delay");
    FeelStrip_Tick(&s, false, true, &std, FRAME);
    CHECK(s.x == 2 && s.repeat, "strip: then repeats");
    FeelStrip_Tick(&s, true, true, &std, FRAME);
    CHECK(s.x == 2, "strip: Left and Right together cancel");
    PlayHandling wall = PlayHandling_From(170, 0, SOFT_NORMAL);
    FeelStrip_Reset(&s);
    s.idle = 0.0f;
    for (int i = 0; i < 12; i++) FeelStrip_Tick(&s, false, true, &wall, FRAME);
    CHECK(s.x == maxX, "strip: with speed 0 it jumps to the wall after the delay");
}

/* --------------------------------------------------------------- the page */

static bool Overlap(KitRect a, KitRect b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }

static void test_page(void) {
    Kit_SetScreen(620, 720);
    MenuKeys_Install(NULL);
    Settings s;
    Settings_Default(&s);
    ArcadePrefs pr = {70, true, false, false};
    static SetPage p;
    bool fits = true, apart = true, labels = true;
    int rows[SET_TABS] = {0};
    for (int tab = 0; tab < SET_TABS; tab++) {
        SetView v = {&s, &pr, tab, KIT_NONE, true};
        SetPage_Layout(&v, &p);
        rows[tab] = p.rowCount;
        for (int i = 0; i < p.list.count; i++) {
            KitRect r = Kit_Bounds(&p.list.items[i]);
            if (r.x < 0 || r.y < 0 || r.x + r.w > 620 || r.y + r.h > SET_NOTE_Y) fits = false;
            for (int j = 0; j < i; j++)
                if (Overlap(r, Kit_Bounds(&p.list.items[j]))) apart = false;
        }
        for (int i = 0; i < p.rowCount; i++) {
            const SetRow *row = &p.rows[i];
            if (!row->label[0] || row->count == 0) continue;
            const KitItem *first = Kit_Get(&p.list, row->controls[0]);
            int end = SET_LABEL_X + Kit_TextWidth(row->label, 16) + (row->shared ? 6 + 16 : 0);
            int start = (int)first->r.x + (first->kind == KIT_STEPPER ? 12 : 0); /* a stepper's "<" sits 12 px into its cell */
            if (end + 4 > start) labels = false;
        }
    }
    CHECK(fits && apart, "page: on every tab every control is inside the window, above the note line, and none overlaps another");
    CHECK(labels, "page: every row's label (and its *) ends before its controls start");
    CHECK(rows[TAB_CONTROLS] == 10 && rows[TAB_FEEL] == 5 && rows[TAB_LOOK] == 9 && rows[TAB_SOUND] == 4 && rows[TAB_GAME] == 5,
          "page: Controls has the nine actions and Reset, Feel five rows, Look nine, Sound four, Game five");

    /* Controls: the caps name the keys, "-" for none; the pad's caps. */
    SetView v = {&s, &pr, TAB_CONTROLS, KIT_NONE, true};
    SetPage_Layout(&v, &p);
    const KitItem *hold2 = Kit_Get(&p.list, SID_CAP_OF(ACT_HOLD, 1)), *holdPad2 = Kit_Get(&p.list, SID_CAP_OF(ACT_HOLD, 3));
    const KitItem *pause2 = Kit_Get(&p.list, SID_CAP_OF(ACT_PAUSE, 1)), *shiftCap;
    CHECK(hold2 && strcmp(hold2->label, "-") == 0 && holdPad2 && strcmp(holdPad2->label, "LB") == 0 && pause2 && strcmp(pause2->label, "Esc") == 0,
          "page: the caps show your keys and buttons, a dash for none");
    Controls_BindKey(&s, ACT_HOLD, 1, "RIGHT_SHIFT");
    SetPage_Layout(&v, &p);
    shiftCap = Kit_Get(&p.list, SID_CAP_OF(ACT_HOLD, 1));
    CHECK(shiftCap && strcmp(shiftCap->label, "Right Shift") == 0 && strcmp(shiftCap->caption, "Hold key 2") == 0, "page: in words, each cap named for the harness");
    const KitItem *reset = Kit_Get(&p.list, SID_RESET);
    CHECK(reset && !(reset->flags & KIT_DISABLED), "page: Reset controls is there once a key changed");
    Settings_Default(&s);
    SetPage_Layout(&v, &p);
    reset = Kit_Get(&p.list, SID_RESET);
    CHECK(reset && (reset->flags & KIT_DISABLED) && strstr(reset->reason, "as Blockfall came"), "page: and faint, saying why, when nothing did");

    /* The arrows' walk: rows, the caps along a row, the tabs above. */
    KitState k;
    Kit_Init(&k);
    Kit_Enter(&k, SetPage_FirstRowControl(&p));
    CHECK(k.focus == SID_CAP_OF(ACT_LEFT, 0), "page: Controls opens on Move left's key");
    Input in;
    memset(&in, 0, sizeof(in));
    in.focused = true;
    in.key[IK_RIGHT] = in.keyRepeat[IK_RIGHT] = in.keyDown[IK_RIGHT] = true;
    SetPage_Navigate(&k, &p, &in);
    SetPage_Navigate(&k, &p, &in);
    SetPage_Navigate(&k, &p, &in);
    SetPage_Navigate(&k, &p, &in);
    CHECK(k.focus == SID_CAP_OF(ACT_LEFT, 3), "page: Right walks along the caps and stops at the last");
    memset(&in, 0, sizeof(in));
    in.focused = true;
    in.key[IK_DOWN] = in.keyRepeat[IK_DOWN] = in.keyDown[IK_DOWN] = true;
    for (int i = 0; i < 12; i++) SetPage_Navigate(&k, &p, &in);
    CHECK(k.focus == SID_CAP_OF(ACT_RESTART, 3), "page: Down walks the rows; faint Reset controls is passed over");
    Controls_BindKey(&s, ACT_HOLD, 1, "X");
    SetPage_Layout(&v, &p);
    SetPage_Navigate(&k, &p, &in);
    CHECK(k.focus == SID_RESET, "page: once there is something to reset, Down lands on it");
    Settings_Default(&s);
    SetPage_Layout(&v, &p);
    memset(&in, 0, sizeof(in));
    in.focused = true;
    in.key[IK_UP] = in.keyRepeat[IK_UP] = in.keyDown[IK_UP] = true;
    for (int i = 0; i < 12; i++) SetPage_Navigate(&k, &p, &in);
    CHECK(k.focus == SID_TABS, "page: Up past the first row lands on the tabs, and stays");
    in.key[IK_UP] = in.keyRepeat[IK_UP] = in.keyDown[IK_UP] = false;
    in.key[IK_DOWN] = in.keyRepeat[IK_DOWN] = in.keyDown[IK_DOWN] = true;
    SetPage_Navigate(&k, &p, &in);
    CHECK(k.focus == SID_CAP_OF(ACT_LEFT, 0), "page: Down from the tabs: the first row");

    /* What each change does. */
    KitEvent ev = {KIT_EV_PRESS, SID_CAP_OF(ACT_HOLD, 2), 0};
    SetEffect e = SetPage_Apply(&s, &pr, &ev);
    CHECK(e.kind == SE_CAPTURE && e.captureAction == ACT_HOLD && e.captureColumn == 2, "page: a cap pressed waits for a key or button");
    ev = (KitEvent){KIT_EV_PRESS, SID_RESET, 0};
    CHECK(SetPage_Apply(&s, &pr, &ev).kind == SE_RESET_ASK, "page: Reset controls asks first");
    ev = (KitEvent){KIT_EV_CHANGE, SID_TABS, 3};
    e = SetPage_Apply(&s, &pr, &ev);
    CHECK(e.kind == SE_TAB && e.value == 3, "page: the tabs change");
    ev = (KitEvent){KIT_EV_PRESS, SID_PRESET + 2, 0};
    e = SetPage_Apply(&s, &pr, &ev);
    CHECK(e.kind == SE_CHANGED && e.config && s.repeatDelayMs == 110 && s.repeatSpeedMs == 15 && s.softDropSpeed == SOFT_FAST &&
              SetPage_PresetOf(&s) == 2,
          "page: the Fast preset sets all three, saved to config.ini");
    ev = (KitEvent){KIT_EV_CHANGE, SID_DELAY, 12};
    SetPage_Apply(&s, &pr, &ev);
    CHECK(s.repeatDelayMs == 120 && SetPage_PresetOf(&s) == -1, "page: a delay of its own is no preset (Custom)");
    ev = (KitEvent){KIT_EV_CHANGE, SID_SPEED, 0};
    SetPage_Apply(&s, &pr, &ev);
    ev = (KitEvent){KIT_EV_CHANGE, SID_SOFT, SOFT_INSTANT};
    SetPage_Apply(&s, &pr, &ev);
    CHECK(s.repeatSpeedMs == 0 && s.softDropSpeed == SOFT_INSTANT, "page: speed 0 and Instant");
    ev = (KitEvent){KIT_EV_PRESS, SID_PRESET + 1, 0};
    SetPage_Apply(&s, &pr, &ev);
    CHECK(SetPage_PresetOf(&s) == 1 && s.repeatDelayMs == 170 && s.repeatSpeedMs == 35 && s.softDropSpeed == SOFT_NORMAL, "page: Standard is today's");
    /* The ghost's stepper goes Outline, Solid, Off. */
    ev = (KitEvent){KIT_EV_CHANGE, SID_GHOST, 1};
    SetPage_Apply(&s, &pr, &ev);
    int solid = s.ghostPiece;
    ev = (KitEvent){KIT_EV_CHANGE, SID_GHOST, 2};
    SetPage_Apply(&s, &pr, &ev);
    CHECK(solid == GHOST_SOLID && s.ghostPiece == GHOST_OFF, "page: Ghost piece: Outline, Solid, Off");
    ev = (KitEvent){KIT_EV_CHANGE, SID_PATTERNS, 1};
    e = SetPage_Apply(&s, &pr, &ev);
    CHECK(s.blockPatterns && e.patterns && e.config, "page: Block patterns on (the drawing is told)");
    ev = (KitEvent){KIT_EV_CHANGE, SID_FLASH, 1};
    e = SetPage_Apply(&s, &pr, &ev);
    CHECK(pr.reducedFlashing && e.prefs && !e.config, "page: Reduced flashing is shared: prefs.ini");
    ev = (KitEvent){KIT_EV_CHANGE, SID_MUSIC, 4};
    e = SetPage_Apply(&s, &pr, &ev);
    CHECK(pr.musicVolume == 40 && e.prefs && e.musicVolume, "page: Music volume is shared too");
    ev = (KitEvent){KIT_EV_CHANGE, SID_EFFECTS, 3};
    e = SetPage_Apply(&s, &pr, &ev);
    CHECK(s.volumePercent == 30 && e.config && e.effectsVolume && e.value == 3, "page: the effects volume ticks at its block's pitch");
    ev = (KitEvent){KIT_EV_CHANGE, SID_AUTOPAUSE, 0};
    SetPage_Apply(&s, &pr, &ev);
    /* [Show How to play] Next launch is a button: a change event is not its
     * way, a press sets it (never clears it: main.c does, as that launch
     * opens How to play) and the Game tab says it is armed. */
    ev = (KitEvent){KIT_EV_CHANGE, SID_HOWTO, 1};
    e = SetPage_Apply(&s, &pr, &ev);
    CHECK(e.kind == SE_NONE && !s.showHowTo, "page: Show How to play is a button, not a switch: a change event does nothing");
    ev = (KitEvent){KIT_EV_PRESS, SID_HOWTO, 0};
    e = SetPage_Apply(&s, &pr, &ev);
    CHECK(e.kind == SE_HOWTO_NEXT && e.config && !e.prefs && s.showHowTo, "page: [Show How to play] Next launch sets it, saved to config.ini");
    e = SetPage_Apply(&s, &pr, &ev);
    CHECK(e.kind == SE_HOWTO_NEXT && s.showHowTo, "page: pressed again it stays set");
    v = (SetView){&s, &pr, TAB_GAME, KIT_NONE, true};
    SetPage_Layout(&v, &p);
    const KitItem *next = Kit_Get(&p.list, SID_HOWTO);
    CHECK(next && next->kind == KIT_BUTTON && strcmp(next->label, "Next launch") == 0 && strcmp(p.rows[4].label, "Show How to play") == 0,
          "page: the Game tab's Show How to play row is a Next launch button");
    CHECK(strstr(p.rows[4].help, "It is set to open next time") != NULL, "page: its help says it is armed");
    s.showHowTo = false;
    SetPage_Layout(&v, &p);
    CHECK(strstr(p.rows[4].help, "It is set") == NULL && strstr(p.rows[4].help, "next time Blockfall starts") != NULL, "page: and plain when it is not");
    CHECK(Kit_TextWidth(p.rows[4].label, 16) <= SET_CONTROL_X - SET_LABEL_X - 8, "page: the row label fits before the button");
    s.showHowTo = true;
    ev = (KitEvent){KIT_EV_CHANGE, SID_GRID, GRID_OFF};
    SetPage_Apply(&s, &pr, &ev);
    CHECK(!s.autoPause && s.showHowTo && s.grid == GRID_OFF, "page: Auto pause, How to play next launch, the grid");
    /* Sound off: Test sound says why it cannot play. */
    s.audioEnabled = false;
    v = (SetView){&s, &pr, TAB_SOUND, KIT_NONE, true};
    SetPage_Layout(&v, &p);
    const KitItem *test = Kit_Get(&p.list, SID_TEST);
    CHECK(test && (test->flags & KIT_DISABLED) && strcmp(test->reason, "Sound is off.") == 0, "page: Test sound is faint with the sound off");
    /* "R restarts" with your key. */
    Settings_Default(&s);
    Controls_BindKey(&s, ACT_RESTART, 0, "T"); /* (Enter cannot restart: it picks menu items) */
    v = (SetView){&s, &pr, TAB_GAME, KIT_NONE, true};
    SetPage_Layout(&v, &p);
    CHECK(strcmp(p.rows[1].label, "T restarts") == 0, "page: the restart row names your key");
    Controls_BindKey(&s, ACT_RESTART, 0, "RIGHT_SHIFT");
    SetPage_Layout(&v, &p);
    CHECK(strcmp(p.rows[1].label, "Restart key") == 0, "page: or says Restart key when the name will not fit");

    /* Waiting for a key: five seconds, a bar. */
    Capture c;
    Capture_Begin(&c, ACT_HOLD, 3);
    int frames = 0;
    while (!Capture_Tick(&c, FRAME) && frames < 1000) frames++;
    CHECK(c.pad && frames >= 298 && frames <= 300, "page: a capture gives up after five seconds");
    Capture_Begin(&c, ACT_HOLD, 1);
    Capture_Tick(&c, 2.5f);
    CHECK(!c.pad && Capture_Left(&c) > 0.49f && Capture_Left(&c) < 0.51f && Capture_IsModifier("LEFT_ALT") && !Capture_IsModifier("A"),
          "page: its bar drains; Shift, Ctrl and Alt are told apart");
    /* Which caps wait for a button: the third and fourth columns, not the first two. */
    Capture_Begin(&c, ACT_HARD, 2);
    CHECK(c.on && c.pad && c.column == 2 && c.action == ACT_HARD, "page: the first pad cap (column 2) waits for a button");
    Capture_Begin(&c, ACT_HARD, 0);
    CHECK(c.on && !c.pad && c.column == 0 && c.action == ACT_HARD, "page: the first key cap (column 0) waits for a key");
    Capture_Begin(&c, ACT_HARD, 1);
    CHECK(!c.pad && c.column == 1, "page: the second key cap (column 1) waits for a key too");

    /* A button pressed during a pad capture binds it (what scr_settings.c
     * feeds from the frame's Input). */
    Settings_Default(&s);
    BindResult r;
    memset(&in, 0, sizeof(in));
    memset(&r, 0, sizeof(r));
    Capture_Begin(&c, ACT_HARD, 2);
    in.pad[PB_RB] = true;
    CHECK(Capture_FeedPad(&c, &s, &in, false, &r) == CAPFEED_BOUND && r.kind == BIND_SET && s.pad[ACT_HARD][0] == PB_RB &&
              strcmp(r.note, "Hard drop's button is now RB.") == 0,
          "page: RB pressed on Hard drop's first pad cap binds it to slot 1, with the note");
    Settings_Default(&s);
    Capture_Begin(&c, ACT_HARD, 3);
    CHECK(Capture_FeedPad(&c, &s, &in, false, &r) == CAPFEED_BOUND && r.kind == BIND_SET && s.pad[ACT_HARD][1] == PB_RB && s.pad[ACT_HARD][0] == PB_A,
          "page: the second pad cap (column 3) binds slot 2 and leaves slot 1 alone");
    /* A swap: Y is Hold's; pressed for Rotate right's first, the two swap. */
    Settings_Default(&s);
    memset(&in, 0, sizeof(in));
    in.pad[PB_Y] = true;
    Capture_Begin(&c, ACT_ROTATE, 2);
    CHECK(Capture_FeedPad(&c, &s, &in, false, &r) == CAPFEED_BOUND && r.kind == BIND_SWAPPED && r.otherAction == ACT_HOLD && r.otherSlot == 0 &&
              s.pad[ACT_ROTATE][0] == PB_Y && s.pad[ACT_HOLD][0] == PB_UP && Controls_AllDifferent(&s) &&
              strcmp(r.note, "Y moved from Hold to Rotate right. Hold took D-pad up.") == 0,
          "page: a button another action has swaps, and the result says what moved");
    /* Menu never binds (it gives up, which the screen handles first). */
    Settings_Default(&s);
    memset(&in, 0, sizeof(in));
    in.pad[PB_MENU] = true;
    Capture_Begin(&c, ACT_HARD, 2);
    CHECK(Capture_FeedPad(&c, &s, &in, false, &r) == CAPFEED_NONE && s.pad[ACT_HARD][0] == PB_A && c.on,
          "page: Menu is not a button to bind: nothing happens and the capture goes on");
    /* A key during a pad capture: the screen says so. Nothing: nothing. */
    memset(&in, 0, sizeof(in));
    CHECK(Capture_FeedPad(&c, &s, &in, true, &r) == CAPFEED_KEY_WHILE_PAD && s.pad[ACT_HARD][0] == PB_A && c.on,
          "page: a key pressed while a pad cap waits is reported, and binds nothing");
    CHECK(Capture_FeedPad(&c, &s, &in, false, &r) == CAPFEED_NONE, "page: a frame with nothing pressed keeps waiting");
    /* A pressed on Restart's cap: the screen turns it away before this
     * (Controls_PadAllowed), and fed anyway the model refuses it, with the
     * note, and the slot stays. */
    Settings_Default(&s);
    memset(&in, 0, sizeof(in));
    in.pad[PB_A] = true;
    Capture_Begin(&c, ACT_RESTART, 2);
    CHECK(Capture_FeedPad(&c, &s, &in, false, &r) == CAPFEED_BOUND && r.kind == BIND_REFUSED && s.pad[ACT_RESTART][0] == PB_VIEW &&
              strcmp(r.note, "A picks menu items, so it cannot pause or restart.") == 0,
          "page: A fed to Restart's pad cap is refused by the model, with the note");
    /* The first button wins when two go down in one frame; a key capture ignores the pad here. */
    Settings_Default(&s);
    memset(&in, 0, sizeof(in));
    in.pad[PB_LT] = in.pad[PB_RT] = true;
    Capture_Begin(&c, ACT_RESTART, 3);
    CHECK(Capture_FeedPad(&c, &s, &in, false, &r) == CAPFEED_BOUND && s.pad[ACT_RESTART][1] == PB_LT, "page: two buttons in one frame: the first wins");
    Capture_Begin(&c, ACT_RESTART, 1);
    CHECK(Capture_FeedPad(&c, &s, &in, false, &r) == CAPFEED_NONE && s.pad[ACT_RESTART][1] == PB_LT, "page: a key cap is not fed by the pad");
}

/* ------------------------------------------------------------- the demos */

typedef struct {
    int clears[DEMO_MAX_CLEARS];
    long gained[DEMO_MAX_CLEARS];
    int count;
    bool topped;
    bool powerSeen;
    int filledBefore, filledAfter, removed;
    float freeze, slow;
    bool columnEmpty;
    int lowRowsFilledBefore;
    int clearsAtPower;   /* clears made by the time the power-up fired (its own lock's included) */
} Loop;

static Loop RunLoop(HowToDemo *d, int page, int variant) {
    Loop l;
    memset(&l, 0, sizeof(l));
    HowToDemo_Start(d, page, variant);
    int loops = d->loops;
    for (int f = 0; f < 60 * 30 && d->loops == loops; f++) {
        int before = d->clearCount;
        PowerUp firedBefore = d->fired;
        int low = 0;
        for (int y = 0; y < 3; y++)
            for (int x = 0; x < d->game.board.w; x++)
                if (d->game.board.cells[BOARD_HIDDEN + d->game.board.h - 1 - y][x] != CELL_EMPTY) low++;
        HowToDemo_Tick(d, FRAME);
        if (d->game.phase == GS_GAMEOVER) l.topped = true;
        if (d->loops != loops) break;
        if (d->clearCount > before) {
            l.count = d->clearCount;
            memcpy(l.clears, d->clears, sizeof(l.clears));
            memcpy(l.gained, d->gained, sizeof(l.gained));
        }
        if (d->fired != PU_NONE && firedBefore == PU_NONE && !l.powerSeen) {
            l.powerSeen = true;
            l.filledBefore = d->filledBefore;
            l.filledAfter = d->filledAfter;
            l.removed = d->filledBefore + 4 - d->filledAfter;
            l.freeze = d->game.freezeTimer;
            l.slow = d->game.slowTimer;
            l.columnEmpty = true;
            for (int y = BOARD_HIDDEN; y < Board_TotalH(&d->game.board); y++)
                if (d->game.board.cells[y][3] != CELL_EMPTY) l.columnEmpty = false;
            l.lowRowsFilledBefore = low;
            l.clearsAtPower = d->clearCount;
        }
    }
    return l;
}

static bool SameClaim(const Loop *l, DemoClaim c) {
    if (l->count != c.count) return false;
    for (int i = 0; i < c.count; i++)
        if (l->clears[i] != c.clears[i] || l->gained[i] != c.gained[i]) return false;
    return true;
}

static void test_demos(void) {
    static HowToDemo d;
    bool claims = true, again = true, safe = true;
    for (int page = 0; page < HOWTO_PAGES; page++) {
        int variants = page == HOWTO_MODES ? MODE_COUNT : (page == HOWTO_POWER ? PU_COUNT - 1 : 1);
        for (int v = 0; v < variants; v++) {
            int variant = page == HOWTO_MODES ? v : (page == HOWTO_POWER ? v + 1 : 0);
            Loop a = RunLoop(&d, page, variant);
            Loop b = RunLoop(&d, page, variant);
            if (page != HOWTO_POWER && !SameClaim(&a, HowToDemo_Claim(page, variant))) {
                claims = false;
                printf("  demo %s/%d: %d clears (%d, %ld)\n", HowTo_PageName(page), variant, a.count, a.clears[0], a.gained[0]);
            }
            if (a.count != b.count || memcmp(a.clears, b.clears, sizeof(a.clears)) != 0) again = false;
            if (a.topped) safe = false;
        }
    }
    CHECK(claims, "demos: Basics, Hold and next and every mode clear a single (100); Scoring a quad (800) then a back-to-back quad (1 250)");
    CHECK(again && safe, "demos: each plays the same every loop and never tops out");

    /* Power-ups: each does what its tile says. */
    Loop bomb = RunLoop(&d, HOWTO_POWER, PU_BOMB);
    CHECK(bomb.powerSeen && bomb.clearsAtPower == 0 && bomb.removed == 6, "demos: the Bomb blows up the blocks around it (6 here) and clears no row");
    Loop laser = RunLoop(&d, HOWTO_POWER, PU_LASER);
    CHECK(laser.powerSeen && laser.columnEmpty && laser.removed == 5, "demos: the Laser empties its column");
    Loop freeze = RunLoop(&d, HOWTO_POWER, PU_FREEZE);
    CHECK(freeze.powerSeen && freeze.freeze > 5.9f, "demos: Freeze stops the fall for 6 seconds");
    Loop slow = RunLoop(&d, HOWTO_POWER, PU_SLOW);
    CHECK(slow.powerSeen && slow.slow > 11.9f, "demos: Slow lasts 12 seconds");
    Loop sweep = RunLoop(&d, HOWTO_POWER, PU_SWEEP);
    CHECK(sweep.powerSeen && sweep.removed == sweep.lowRowsFilledBefore && sweep.removed > 0, "demos: Sweep takes away the bottom three rows");

    /* Freeze holds the next piece up; with Slow it still falls. */
    HowToDemo_Start(&d, HOWTO_POWER, PU_FREEZE);
    int py = -1, frames = 0;
    while (frames < 600 && d.fired == PU_NONE) { HowToDemo_Tick(&d, FRAME); frames++; }
    py = d.game.py;
    for (int i = 0; i < 120; i++) HowToDemo_Tick(&d, FRAME);
    CHECK(d.game.py == py, "demos: under Freeze the next piece hangs where it came in");
    HowToDemo_Start(&d, HOWTO_POWER, PU_SLOW);
    frames = 0;
    while (frames < 600 && d.fired == PU_NONE) { HowToDemo_Tick(&d, FRAME); frames++; }
    py = d.game.py;
    for (int i = 0; i < 150; i++) HowToDemo_Tick(&d, FRAME);
    CHECK(d.game.py == py + 1, "demos: under Slow it falls a row in 2.5 s");

    /* The keys it presses light their caps. */
    HowToDemo_Start(&d, HOWTO_BASICS, 0);
    bool sawTurnLeft = false, sawHard = false;
    for (int f = 0; f < 600 && d.loops == 0; f++) {
        HowToDemo_Tick(&d, FRAME);
        if (d.press == DOP_TURN_LEFT && d.pressAge == 0.0f) sawTurnLeft = true;
        if (d.press == DOP_HARD && d.pressAge == 0.0f) sawHard = true;
    }
    CHECK(sawTurnLeft && sawHard, "demos: Basics presses Rotate left and Hard drop (their caps light up)");

    /* Hold and next: Next shows the piece that really comes. From the
     * first Hold (S in, I out) until the I locks, Next shows the O; the O
     * is what spawns; the second Hold swaps the S back in for it. */
    HowToDemo_Start(&d, HOWTO_HOLD, 0);
    int fallFrames = 0, nextWrong = 0;
    bool afterI = false, oCame = false, sBack = false;
    for (int f = 0; f < 60 * 30 && d.loops == 0; f++) {
        HowToDemo_Tick(&d, FRAME);
        if (d.clearCount == 0 && d.game.hold == PIECE_S && d.game.current == PIECE_I) {
            fallFrames++;
            if (d.game.next != PIECE_O) nextWrong++;
        }
        if (d.clearCount > 0 && !afterI) {
            afterI = true;
            oCame = d.game.current == PIECE_O && d.game.hold == PIECE_S;
        }
        if (afterI && d.game.current == PIECE_S && d.game.hold == PIECE_O) sBack = true;
    }
    CHECK(fallFrames >= 60 && nextWrong == 0, "demos: Hold and next shows the O in Next for the whole of the I's fall");
    CHECK(oCame && sBack, "demos: the O Next showed is what comes after the I, and Hold swaps the S back in for it");
}

/* -------------------------------------------------------------- patterns */

static void test_patterns(void) {
    unsigned masks[PIECE_COUNT];
    bool marked = true;
    for (int t = 0; t < PIECE_COUNT; t++) {
        masks[t] = 0;
        int cells = 0;
        for (int y = 0; y < LOOK_PATTERN_N; y++)
            for (int x = 0; x < LOOK_PATTERN_N; x++)
                if (Look_PatternCell((PieceType)t, x, y)) {
                    masks[t] |= 1u << (y * LOOK_PATTERN_N + x);
                    cells++;
                }
        if (cells < 4) marked = false;
    }
    bool distinct = true;
    for (int a = 0; a < PIECE_COUNT; a++)
        for (int b = a + 1; b < PIECE_COUNT; b++)
            if (masks[a] == masks[b]) distinct = false;
    CHECK(marked && distinct, "patterns: seven different marks, one per piece, none faint");
    CHECK(!Look_PatternCell((PieceType)-1, 0, 0) && !Look_PatternCell(PIECE_I, 5, 2), "patterns: nothing outside them");
    Look_SetPatterns(true);
    CHECK(Look_Patterns(), "patterns: the switch");
    Look_SetPatterns(false);
}

void test_settings_all(void) {
    test_config();
    test_words();
    test_binding();
    test_feel_settings();
    test_strip();
    test_page();
    test_demos();
    test_patterns();
}
