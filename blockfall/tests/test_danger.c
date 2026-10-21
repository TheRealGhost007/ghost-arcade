/* The danger warning, the music that follows the board and Sprint's splits
 * (danger.c, splits.c), with no window: the free rows above the stack, the
 * hysteresis (on at four rows, off only at seven), the warning once at the
 * start of danger and at most every 8 s, Zen never in danger, a pause
 * holding it, the next piece's red outline, the intensity formula for every
 * mode (the snare in as the stack climbs and out as you dig), Ultra's last
 * five ticks through the sound plan, danger's shared WARNING (3.6.1: one
 * in a frame with Ultra's, never in Zen), Sprint's gold counter, the split marks
 * (a quad that jumps a mark still takes it), the diffs and their words, the
 * pace file, the finish line, the split callout, that the clock starts on
 * GO and not on READY, and that none of it changes the rules. */
#include <stdio.h>
#include <string.h>
#include "check.h"
#include "../src/board.h"
#include "../src/game.h"
#include "../src/danger.h"
#include "../src/hudfx.h"
#include "../src/playstep.h"
#include "../src/results.h"
#include "../src/runrecord.h"
#include "../src/soundplan.h"
#include "../src/splits.h"
#include "../src/persist.h"
#include "rng.h"
#include "sfx.h"

#define FRAME (1.0f / 60.0f)
#define NEAR(a, b) ((a) - (b) < 0.001f && (b) - (a) < 0.001f)

/* Row y from the bottom (0 = the floor) filled, the last column left open
 * so nothing ever clears. */
static void Row(Game *g, int y, bool filled) {
    int row = Board_TotalH(&g->board) - 1 - y;
    for (int x = 0; x < g->board.w; x++) g->board.cells[row][x] = (filled && x != g->board.w - 1) ? (int8_t)PIECE_J : CELL_EMPTY;
}

/* A stack `rows` high (the rest empty). */
static void Stack(Game *g, int rows) {
    for (int y = 0; y < g->board.h; y++) Row(g, y, y < rows);
}

static void Place(Game *g, PieceType t, int rot, int px, int py) {
    g->current = t;
    g->rotation = rot;
    g->px = px;
    g->py = py;
    g->fallTimer = g->lockTimer = 0.0f;
    g->lockResets = 0;
    g->lowestRow = py;
    g->grounded = false;
    g->curPower = PU_NONE;
}

static int Frames(Danger *d, const Game *g, int n, int *warns) {
    int w = 0;
    for (int i = 0; i < n; i++) {
        Danger_Update(d, g, FRAME);
        if (d->warn) w++;
    }
    if (warns) *warns += w;
    return w;
}

/* ------------------------------------------------------------ danger */

static void test_free_rows_and_hysteresis(void) {
    Game g;
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, true}, 1, 0, 1, 0);
    CHECK(Danger_FreeRows(&g.board) == 20, "free rows: an empty 10x20 well has 20");
    Stack(&g, 1);
    CHECK(Danger_FreeRows(&g.board) == 19, "free rows: a block on the floor leaves 19");
    Stack(&g, 20);
    CHECK(Danger_FreeRows(&g.board) == 0, "free rows: a block on the top row leaves none");
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 14, 30, true}, 1, 0, 1, 0);
    Stack(&g, 11);
    CHECK(Danger_FreeRows(&g.board) == 19, "free rows: 11 rows on a 30-row well leave 19");

    /* The table: on at 4, off at 7, held between. */
    CHECK(Danger_Hysteresis(false, 4) && Danger_Hysteresis(false, 0) && !Danger_Hysteresis(false, 5) && !Danger_Hysteresis(false, 6) &&
              !Danger_Hysteresis(false, 7) && !Danger_Hysteresis(false, 20),
          "hysteresis: out of danger, four free rows or fewer bring it on; five do not");
    CHECK(Danger_Hysteresis(true, 4) && Danger_Hysteresis(true, 5) && Danger_Hysteresis(true, 6) && !Danger_Hysteresis(true, 7) && !Danger_Hysteresis(true, 12),
          "hysteresis: in danger, it holds through five and six free rows and lets go at seven");

    /* Through the frames of a run. */
    Danger d;
    Danger_Reset(&d);
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, true}, 1, 0, 1, 0);
    Place(&g, PIECE_O, 0, 8, BOARD_HIDDEN);
    Stack(&g, 15);
    Frames(&d, &g, 5, NULL);
    CHECK(!d.on && d.freeRows == 5 && d.ease == 0.0f, "danger: five rows clear: not yet");
    Stack(&g, 16);
    Frames(&d, &g, 1, NULL);
    CHECK(d.on && d.freeRows == 4, "danger: four rows clear: on");
    CHECK(d.ease > 0.0f && d.ease < 0.1f, "danger: the frame starts easing to red");
    Frames(&d, &g, 30, NULL);
    CHECK(d.ease == 1.0f, "danger: ...and is fully red after 0.4 s");
    Stack(&g, 14);
    Frames(&d, &g, 5, NULL);
    CHECK(d.on && d.freeRows == 6, "danger: digging to six rows clear keeps it on (no flicker)");
    Stack(&g, 13);
    Frames(&d, &g, 1, NULL);
    CHECK(!d.on && d.freeRows == 7, "danger: seven rows clear and it lets go");
    Frames(&d, &g, 12, NULL);
    CHECK(d.ease > 0.4f && d.ease < 0.6f, "danger: the red eases off over 0.4 s too");
    Frames(&d, &g, 30, NULL);
    CHECK(d.ease == 0.0f && d.t == 0.0f, "danger: ...to nothing");
    Stack(&g, 14);
    Frames(&d, &g, 5, NULL);
    CHECK(!d.on, "danger: back to six clear from outside stays off");
    Stack(&g, 16);
    Frames(&d, &g, 1, NULL);
    CHECK(d.on, "danger: four clear brings it back");

    /* The frame: steady with Reduced flashing, a gentle pulse without. */
    Frames(&d, &g, 30, NULL);
    CHECK(NEAR(Danger_Frame(&d, true), 1.0f) && NEAR(Danger_Stripes(&d), 1.0f), "danger: calm: the frame holds fully red, the stripes too");
    float lo = 2.0f, hi = -1.0f;
    for (int i = 0; i < 60; i++) {
        Danger_Update(&d, &g, FRAME);
        float k = Danger_Frame(&d, false);
        if (k < lo) lo = k;
        if (k > hi) hi = k;
    }
    CHECK(lo >= DANGER_PULSE_MIN - 0.02f && lo < DANGER_PULSE_MIN + 0.1f && hi > 0.95f && hi <= 1.0f,
          "danger: without it the frame pulses between about 0.55 and 1 over a second");

    /* A pause holds it; the run's end holds it; Zen never has it. */
    Game_TogglePause(&g);
    Stack(&g, 0);
    Frames(&d, &g, 30, NULL);
    CHECK(d.on && d.freeRows == 4, "danger: paused, it holds as it was whatever the board does");
    Game_TogglePause(&g);
    Frames(&d, &g, 1, NULL);
    CHECK(!d.on && d.freeRows == 20, "danger: ...and catches up when play goes on");
    Stack(&g, 16);
    Frames(&d, &g, 1, NULL);
    g.phase = GS_GAMEOVER;
    Stack(&g, 0);
    Frames(&d, &g, 30, NULL);
    CHECK(d.on, "danger: a finished run holds it");

    Game_InitConfig(&g, (GameConfig){MODE_ZEN, 10, 20, true}, 1, 0, 1, 0);
    Danger_Reset(&d);
    Stack(&g, 19);
    int warns = 0;
    Frames(&d, &g, 60, &warns);
    CHECK(!d.on && d.ease == 0.0f && warns == 0 && d.freeRows == 1, "danger: never in Zen, however high the stack");
}

static void test_warning_and_outline(void) {
    Game g;
    Danger d;
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, true}, 1, 0, 1, 0);
    Place(&g, PIECE_O, 0, 8, BOARD_HIDDEN);
    Danger_Reset(&d);
    int warns = 0;
    Stack(&g, 16);
    Frames(&d, &g, 1, &warns);
    CHECK(warns == 1 && d.warn, "warning: the frame danger starts plays it");
    Frames(&d, &g, 120, &warns);
    CHECK(warns == 1, "warning: not again while danger lasts");
    Stack(&g, 0);
    Frames(&d, &g, 60, &warns); /* out for a second */
    Stack(&g, 16);
    Frames(&d, &g, 60, &warns);
    CHECK(d.on && warns == 1, "warning: danger again within 8 s is silent");
    Stack(&g, 0);
    Frames(&d, &g, 8 * 60, &warns); /* out for eight seconds: 3 s in, 1 out, 1 in, 8 out = over 8 s since */
    Stack(&g, 16);
    Frames(&d, &g, 1, &warns);
    CHECK(d.on && warns == 2, "warning: after 8 s it may warn again");
    /* The gap counts from the last warning, not from leaving danger: a dip
     * out and in at 7.9 s is silent, at 8.1 s it is not. */
    Stack(&g, 0);
    Frames(&d, &g, 60, &warns);
    Stack(&g, 16);
    Frames(&d, &g, (int)(6.8f * 60.0f), &warns);
    Stack(&g, 0);
    Frames(&d, &g, 3, &warns);
    Stack(&g, 16);
    Frames(&d, &g, 1, &warns);
    CHECK(warns == 2, "warning: 7.9 s after the last one is too soon");
    Stack(&g, 0);
    Frames(&d, &g, 12, &warns);
    Stack(&g, 16);
    Frames(&d, &g, 1, &warns);
    CHECK(warns == 3, "warning: 8.1 s after it is not");
    Danger_Reset(&d);
    Game_TogglePause(&g);
    Frames(&d, &g, 5, &warns);
    CHECK(warns == 3 && !d.warn, "warning: never while paused");
    Game_TogglePause(&g);
    Frames(&d, &g, 1, &warns);
    CHECK(warns == 4, "warning: a fresh run in danger warns at once");

    /* The next piece's outline: only when it would not fit where it comes
     * in, and only in danger. */
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, true}, 1, 0, 1, 0);
    Place(&g, PIECE_I, 1, -2, BOARD_HIDDEN + 10);
    g.next = PIECE_T;
    Danger_Reset(&d);
    Stack(&g, 18); /* the top two rows clear: room for the T's two rows */
    Frames(&d, &g, 1, NULL);
    CHECK(d.on && d.freeRows == 2 && !d.nextBlocked && !Danger_NextBlocked(&g), "outline: the top two rows clear: the T would come in");
    Row(&g, 18, true); /* the second row filled but for the last column: the T's lower cells are taken */
    Frames(&d, &g, 1, NULL);
    CHECK(d.nextBlocked && Danger_NextBlocked(&g), "outline: a row filled under it: the next piece would not fit");
    /* Out of danger the flag is off even if a stray block sat there. */
    Stack(&g, 0);
    int row = BOARD_HIDDEN;
    g.board.cells[row][4] = (int8_t)PIECE_J;
    Frames(&d, &g, 1, NULL);
    CHECK(Danger_NextBlocked(&g), "outline: (one block in the spawn cells blocks the T)");
    CHECK(d.freeRows == 0 && d.on && d.nextBlocked, "outline: a block on the top row is danger, so the outline shows");
}

static void test_intensity(void) {
    Game g;
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, true}, 1, 0, 1, 0);
    g.level = 1;
    float empty = Danger_Intensity(&g, 20, false), half = Danger_Intensity(&g, 10, false), high = Danger_Intensity(&g, 4, false);
    CHECK(empty < 0.35f && empty >= 0.3f, "intensity: level 1 on an empty well: bass and arpeggio, no lead yet");
    CHECK(half > empty && high > half, "intensity: it climbs with the stack");
    CHECK(half < 0.5f && high > 0.5f, "intensity: at level 1 the snare (above 0.5) comes in as the stack nears the top...");
    CHECK(Danger_Intensity(&g, 10, false) < 0.5f, "intensity: ...and drops out again when you dig out");
    bool monotone = true;
    for (int lvl = 1; lvl <= 20 && monotone; lvl++) {
        g.level = lvl;
        float last = -1.0f;
        for (int freeRows = 20; freeRows >= 0; freeRows--) {
            float v = Danger_Intensity(&g, freeRows, false);
            if (v < last - 0.0001f || v < 0.0f || v > 1.0f) monotone = false;
            last = v;
        }
    }
    CHECK(monotone, "intensity: never falls as the stack climbs, at any level, and stays within 0 and 1");
    g.level = 1;
    float l1 = Danger_Intensity(&g, 20, false);
    g.level = 5;
    float l5 = Danger_Intensity(&g, 20, false);
    g.level = 10;
    float l10 = Danger_Intensity(&g, 20, false);
    g.level = 15;
    float l15 = Danger_Intensity(&g, 20, false);
    CHECK(l1 < l5 && l5 < l10 && NEAR(l10, l15), "intensity: it climbs with the level up to 10, then the level adds no more");
    CHECK(l10 > 0.5f && l10 < 0.75f, "intensity: at level 10 the snare stays in even on an empty well");
    CHECK(NEAR(Danger_Intensity(&g, 0, false), 1.0f), "intensity: level 15 with the stack at the top is full");
    g.level = 3;
    CHECK(NEAR(Danger_Intensity(&g, 12, true), Danger_Intensity(&g, 12, false) + DANGER_INTENSITY_BONUS), "intensity: danger adds 0.15");
    g.cfg.mode = MODE_POWER;
    g.level = 1;
    CHECK(NEAR(Danger_Intensity(&g, 10, false), half), "intensity: Power follows the same formula as Marathon");

    /* Sprint follows the lines, whatever the stack. */
    Game_InitConfig(&g, (GameConfig){MODE_SPRINT, 10, 20, true}, 1, 0, 1, 0);
    g.linesCleared = 0;
    float s0 = Danger_Intensity(&g, 20, false);
    g.linesCleared = 20;
    float s20 = Danger_Intensity(&g, 20, false), s20b = Danger_Intensity(&g, 2, false);
    g.linesCleared = 40;
    float s40 = Danger_Intensity(&g, 20, false);
    g.level = 9;
    float s40l = Danger_Intensity(&g, 20, false);
    CHECK(NEAR(s0, 0.35f) && s20 > s0 && s40 > s20 && NEAR(s40, 0.9f) && NEAR(s20, s20b) && NEAR(s40, s40l),
          "intensity: Sprint rises with the lines cleared (0.35 to 0.9), not with the stack or the level");

    /* Ultra follows the time, full in its last ten seconds. */
    Game_InitConfig(&g, (GameConfig){MODE_ULTRA, 10, 20, true}, 1, 0, 1, 0);
    g.timeLeft = 120.0f;
    float u120 = Danger_Intensity(&g, 20, false);
    g.timeLeft = 60.0f;
    float u60 = Danger_Intensity(&g, 20, false), u60s = Danger_Intensity(&g, 3, false);
    g.timeLeft = 11.0f;
    float u11 = Danger_Intensity(&g, 20, false);
    g.timeLeft = 10.9f;
    float u10 = Danger_Intensity(&g, 20, false);
    CHECK(NEAR(u120, 0.35f) && u60 > u120 && u11 > u60 && u11 < 1.0f && NEAR(u10, 1.0f) && NEAR(u60, u60s),
          "intensity: Ultra rises with the time gone and is full from 0:10, not with the stack");

    /* Zen stays calm: the lead, never the snare, whatever happens. */
    Game_InitConfig(&g, (GameConfig){MODE_ZEN, 10, 20, true}, 1, 0, 1, 0);
    g.level = 7;
    g.linesCleared = 200;
    CHECK(NEAR(Danger_Intensity(&g, 20, false), INTENSITY_ZEN) && NEAR(Danger_Intensity(&g, 0, false), INTENSITY_ZEN) &&
              INTENSITY_ZEN > 0.35f && INTENSITY_ZEN < 0.5f,
          "intensity: Zen is one calm level: the lead in, the snare never");
}

/* Ultra's last five seconds through the sound plan: five rising ticks, one
 * a second, after the warning at 0:10; none at 0 (the run ends). */
static void test_ultra_ticks_and_sprint_gold(void) {
    CHECK(Danger_UltraTick(6, 5) && Danger_UltraTick(2, 1) && !Danger_UltraTick(5, 5) && !Danger_UltraTick(1, 0) && !Danger_UltraTick(11, 10) &&
              !Danger_UltraTick(7, 6),
          "ultra ticks: a change to one of the last five seconds, 5 to 1");
    CHECK(Danger_SprintGold(5) && Danger_SprintGold(1) && !Danger_SprintGold(6) && !Danger_SprintGold(0) && !Danger_SprintGold(40),
          "sprint gold: the counter is gold from five lines left down to one");

    Game g;
    SoundPlan p;
    SoundCue c[SOUND_CUES];
    Game_InitConfig(&g, (GameConfig){MODE_ULTRA, 10, 20, true}, 9, 0, 1, 0);
    g.timeLeft = 6.2f;
    SoundPlan_Reset(&p, &g);
    SoundEvents quiet = {0};
    int ticks = 0, warns = 0, frames = 0;
    float lastPitch = 0.0f, tickAt[8];
    bool rising = true, ended = false;
    while (!ended && frames < 600) {
        Game_Update(&g, FRAME);
        ended = g.phase == GS_GAMEOVER;
        quiet.ending = ended;
        SoundPlan_React(&p, &g, &quiet);
        SoundPlan_Tick(&p, FRAME);
        int n = SoundPlan_Take(&p, c, SOUND_CUES);
        for (int i = 0; i < n; i++) {
            if (c[i].how == SND_JINGLE && c[i].id == JINGLE_WARNING) warns++;
            if (c[i].how == SND_PITCHED && c[i].id == SFX_MOVE) {
                if (ticks < 8) tickAt[ticks] = g.timeLeft;
                if (ticks > 0 && c[i].arg <= lastPitch) rising = false;
                lastPitch = c[i].arg;
                ticks++;
            }
        }
        Game_ConsumeFrameFlags(&g);
        frames++;
    }
    CHECK(ended && warns == 1, "ultra ticks: the run from 0:06 ends with its one warning (it was already under 0:10)");
    CHECK(ticks == 5 && rising, "ultra ticks: five ticks, each a little higher");
    CHECK(ticks == 5 && (int)tickAt[0] == 5 && (int)tickAt[4] == 1 && tickAt[0] > 4.95f && tickAt[4] > 0.95f,
          "ultra ticks: on the frames the countdown shows 5, 4, 3, 2 and 1");
    CHECK(NEAR(ULTRA_TICK_PITCH(5), 1.30f) && NEAR(ULTRA_TICK_PITCH(1), 1.50f), "ultra ticks: from 1.30 to 1.50 in pitch");
    /* Paused at 0:04 nothing ticks; a tick is not repeated on resuming. */
    Game_InitConfig(&g, (GameConfig){MODE_ULTRA, 10, 20, true}, 9, 0, 1, 0);
    g.timeLeft = 4.5f;
    SoundPlan_Reset(&p, &g);
    Game_TogglePause(&g);
    ticks = 0;
    quiet.ending = false;
    for (int i = 0; i < 60; i++) {
        SoundPlan_React(&p, &g, &quiet);
        SoundPlan_Tick(&p, FRAME);
        int n = SoundPlan_Take(&p, c, SOUND_CUES);
        for (int k = 0; k < n; k++) if (c[k].how == SND_PITCHED && c[k].id == SFX_MOVE) ticks++;
    }
    Game_TogglePause(&g);
    for (int i = 0; i < 40; i++) {
        Game_Update(&g, FRAME);
        SoundPlan_React(&p, &g, &quiet);
        SoundPlan_Tick(&p, FRAME);
        int n = SoundPlan_Take(&p, c, SOUND_CUES);
        for (int k = 0; k < n; k++) if (c[k].how == SND_PITCHED && c[k].id == SFX_MOVE) ticks++;
    }
    CHECK(ticks == 1, "ultra ticks: none while paused; 0:03 ticks once when play goes on");

    /* The danger warning through the plan: the shared WARNING (3.6.1), small,
     * only in play. */
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, true}, 9, 0, 1, 0);
    SoundPlan_Reset(&p, &g);
    SoundEvents e = {0};
    e.danger = true;
    SoundPlan_React(&p, &g, &e);
    int n = SoundPlan_Take(&p, c, SOUND_CUES);
    CHECK(n == 1 && c[0].how == SND_JINGLE && c[0].id == JINGLE_WARNING && c[0].weight == SND_SMALL,
          "danger sound: the warning is the shared WARNING jingle");
    e.moved = true;
    g.justLocked = true;
    g.justHardDropped = true;
    SoundPlan_React(&p, &g, &e);
    n = SoundPlan_Take(&p, c, SOUND_CUES);
    int bigs = 0, accents = 0;
    for (int i = 0; i < n; i++) { if (c[i].weight == SND_BIG) bigs++; if (c[i].weight == SND_ACCENT) accents++; }
    CHECK(n == 3 && bigs == 0 && accents == 1, "danger sound: it is small, so the one-big-sound rule is untouched");
    Game_ConsumeFrameFlags(&g);
}

/* How many WARNINGs a frame's cues hold, and whether any cue is one of
 * Blockfall's own sounds (an SFX id, not a jingle). */
static int Warnings(const SoundCue *c, int n, int *own) {
    int w = 0;
    for (int i = 0; i < n; i++) {
        if (c[i].how == SND_JINGLE && c[i].id == JINGLE_WARNING) w++;
        else if (c[i].how != SND_JINGLE && own) (*own)++;
    }
    return w;
}

/* One frame of danger and the plan, as scr_play.c runs them: the plan is
 * told danger began when danger.h says it may warn. */
static int DangerFrames(Danger *d, SoundPlan *p, const Game *g, int frames, int *own) {
    SoundCue c[SOUND_CUES];
    int w = 0;
    for (int i = 0; i < frames; i++) {
        Danger_Update(d, g, FRAME);
        SoundEvents e = {0};
        e.danger = d->warn;
        SoundPlan_React(p, g, &e);
        SoundPlan_Tick(p, FRAME);
        w += Warnings(c, SoundPlan_Take(p, c, SOUND_CUES), own);
    }
    return w;
}

/* 3.6.1: danger plays the shared WARNING (REWORK_PLAN.md, "Sound pass":
 * WARNING is for danger and Ultra's last 10 s), not a sound of its own; at
 * most every 8 s; never in Zen; and one WARNING if Ultra's comes on the
 * same frame. */
static void test_danger_shared_warning(void) {
    CHECK(SFX_COUNT == SFX_B2B + 1, "danger warning: Blockfall's own sounds end at the sparkle (no danger sound of its own)");

    Game g;
    Danger d;
    SoundPlan p;
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, true}, 4, 0, 1, 0);
    Danger_Reset(&d);
    SoundPlan_Reset(&p, &g);
    int own = 0;
    Stack(&g, 17);
    int w = DangerFrames(&d, &p, &g, 120, &own);
    CHECK(d.on && w == 1 && own == 0, "danger warning: the stack near the top plans one WARNING and nothing of its own");
    Stack(&g, 0);
    DangerFrames(&d, &p, &g, 60, &own);
    Stack(&g, 17);
    w = DangerFrames(&d, &p, &g, 60, &own);
    CHECK(d.on && w == 0, "danger warning: back within 8 s, red again but no WARNING");
    Stack(&g, 0);
    DangerFrames(&d, &p, &g, 9 * 60, &own);
    Stack(&g, 17);
    w = DangerFrames(&d, &p, &g, 60, &own);
    CHECK(d.on && w == 1 && own == 0, "danger warning: after 8 s out of danger, WARNING again");

    /* Zen: never, however high the stack and for however long; and the plan
     * itself turns down a danger event in Zen. */
    Game_InitConfig(&g, (GameConfig){MODE_ZEN, 10, 20, true}, 4, 0, 1, 0);
    Danger_Reset(&d);
    SoundPlan_Reset(&p, &g);
    own = 0;
    w = 0;
    for (int round = 0; round < 3; round++) {
        Stack(&g, 19);
        w += DangerFrames(&d, &p, &g, 600, &own);
        Stack(&g, 0);
        w += DangerFrames(&d, &p, &g, 600, &own);
    }
    CHECK(w == 0 && own == 0 && !d.on, "danger warning: a Zen run with the stack at the top never plans WARNING");
    SoundEvents forced = {0};
    forced.danger = true;
    SoundCue c[SOUND_CUES];
    SoundPlan_React(&p, &g, &forced);
    int n = SoundPlan_Take(&p, c, SOUND_CUES);
    CHECK(n == 0, "danger warning: a danger event in Zen plans nothing");

    /* Ultra crossing 0:10 on the frame danger begins: one WARNING, and not
     * another on the next frame. */
    Game_InitConfig(&g, (GameConfig){MODE_ULTRA, 10, 20, true}, 4, 0, 1, 0);
    g.timeLeft = 30.0f;
    SoundPlan_Reset(&p, &g);
    SoundEvents none = {0};
    SoundPlan_React(&p, &g, &none);
    n = SoundPlan_Take(&p, c, SOUND_CUES);
    CHECK(Warnings(c, n, NULL) == 0, "danger warning: Ultra at 0:30 has no WARNING yet");
    g.timeLeft = 9.9f;
    SoundPlan_React(&p, &g, &forced);
    n = SoundPlan_Take(&p, c, SOUND_CUES);
    CHECK(Warnings(c, n, NULL) == 1 && n == 1, "danger warning: Ultra's last ten seconds and danger on the same frame give one WARNING");
    g.timeLeft = 9.8f;
    SoundPlan_React(&p, &g, &none);
    n = SoundPlan_Take(&p, c, SOUND_CUES);
    CHECK(Warnings(c, n, NULL) == 0, "danger warning: ...and Ultra does not warn again on the next frame");
    /* Danger later in Ultra's last ten seconds still warns (a new event). */
    g.timeLeft = 8.0f;
    SoundPlan_React(&p, &g, &forced);
    n = SoundPlan_Take(&p, c, SOUND_CUES);
    CHECK(Warnings(c, n, NULL) == 1, "danger warning: danger later in Ultra's last ten seconds warns on its own");
    /* Two asks in one frame's cues (danger, then Ultra) before anything is
     * taken: still one. */
    Game_InitConfig(&g, (GameConfig){MODE_ULTRA, 10, 20, true}, 4, 0, 1, 0);
    g.timeLeft = 30.0f;
    SoundPlan_Reset(&p, &g);
    SoundPlan_React(&p, &g, &forced);
    g.timeLeft = 9.9f;
    SoundPlan_React(&p, &g, &none);
    n = SoundPlan_Take(&p, c, SOUND_CUES);
    CHECK(Warnings(c, n, NULL) == 1, "danger warning: danger and Ultra's warning waiting together are one WARNING");
}

/* ------------------------------------------------------------ splits */

static void test_splits_math(void) {
    CHECK(Splits_MarkLines(0) == 10 && Splits_MarkLines(1) == 20 && Splits_MarkLines(2) == 30 && Splits_MarkLines(3) == 40 && SPLIT_MARKS == 4,
          "splits: the marks are 10, 20, 30 and the finish at 40");
    CHECK(Splits_Centis(23.456f) == 2346 && Splits_Centis(0.0f) == 0 && Splits_Centis(-1.0f) == 0 && Splits_Centis(72.4f) == 7240,
          "splits: seconds to hundredths, rounded");

    char buf[24];
    Splits_FormatDiff(-140, buf, sizeof(buf));
    CHECK(strcmp(buf, "-1.4 s") == 0, "splits: ahead by 1.40 s reads -1.4 s");
    Splits_FormatDiff(80, buf, sizeof(buf));
    CHECK(strcmp(buf, "+0.8 s") == 0, "splits: behind by 0.80 s reads +0.8 s");
    Splits_FormatDiff(0, buf, sizeof(buf));
    CHECK(strcmp(buf, "0.0 s") == 0, "splits: level reads 0.0 s");
    Splits_FormatDiff(-4, buf, sizeof(buf));
    CHECK(strcmp(buf, "0.0 s") == 0, "splits: four hundredths ahead is still 0.0 s (no -0.0)");
    Splits_FormatDiff(-5, buf, sizeof(buf));
    CHECK(strcmp(buf, "-0.1 s") == 0, "splits: five hundredths round up to a tenth");
    Splits_FormatDiff(1234, buf, sizeof(buf));
    CHECK(strcmp(buf, "+12.3 s") == 0, "splits: a big gap");
    Splits_FormatGap(80, buf, sizeof(buf));
    CHECK(strcmp(buf, "0.8 s") == 0, "splits: a gap has no sign");
    Splits_FormatGap(-7240 + 7000, buf, sizeof(buf));
    CHECK(strcmp(buf, "2.4 s") == 0, "splits: ...either way round");

    SprintPace best = {true, {2550, 5000, 7500, 10000}};
    Splits s;
    Game g;
    Game_InitConfig(&g, (GameConfig){MODE_SPRINT, 10, 20, true}, 1, 0, 1, 0);
    Splits_Reset(&s, &best);
    CHECK(Splits_Last(&s) == -1 && !Splits_Text(&s, buf, sizeof(buf), NULL) && buf[0] == '\0', "splits: nothing before the first mark");
    g.linesCleared = 7;
    g.elapsed = 18.0f;
    CHECK(!Splits_React(&s, &g) && s.marks == 0, "splits: seven lines take no mark");
    g.linesCleared = 10;
    g.elapsed = 24.1f;
    bool gold = false;
    CHECK(Splits_React(&s, &g) && s.just && s.marks == 1 && s.run.at[0] == 2410, "splits: the lock that makes ten lines takes the 10 mark at the clock's time");
    long diff = 0;
    CHECK(Splits_Diff(&s, 0, &diff) && diff == -140, "splits: 24.10 against 25.50 is 1.4 s ahead");
    CHECK(Splits_Text(&s, buf, sizeof(buf), &gold) && strcmp(buf, "10: -1.4 s") == 0 && gold, "splits: the line reads 10: -1.4 s, in gold");
    Splits_Consume(&s);
    Splits_Tick(&s, FRAME);
    CHECK(!s.just && NEAR(s.age, FRAME), "splits: the frame's callout is taken once");
    g.linesCleared = 12;
    g.elapsed = 30.0f;
    CHECK(!Splits_React(&s, &g) && s.marks == 1, "splits: twelve lines: nothing new");
    g.linesCleared = 22; /* a quad from 18 jumps the 20 mark */
    g.elapsed = 50.8f;
    CHECK(Splits_React(&s, &g) && s.marks == 2 && s.run.at[1] == 5080, "splits: a clear that jumps a mark still takes it");
    CHECK(Splits_Diff(&s, 1, &diff) && diff == 80 && Splits_Text(&s, buf, sizeof(buf), &gold) && strcmp(buf, "20: +0.8 s") == 0 && !gold,
          "splits: behind reads +0.8 s, not gold");
    g.linesCleared = 41; /* a quad from 37 passes 30? no: from 29 a triple passes 30, then a long jump passes 40 */
    g.elapsed = 99.2f;
    CHECK(Splits_React(&s, &g) && s.marks == 4 && s.run.at[2] == 9920 && s.run.at[3] == 9920 && s.run.have,
          "splits: a lock past two marks takes both at the same time; the finish fills the run");
    CHECK(Splits_Better(&s.run, &best), "splits: 1:39.20 beats a 1:40.00 pace");
    SprintPace slow = {true, {2500, 5000, 7500, 10200}};
    CHECK(!Splits_Better(&slow, &best) && Splits_Better(&slow, NULL) && Splits_Better(&best, &slow), "splits: a slower finish is not better; any finish beats no pace");
    SprintPace unfinished = {false, {2500, 0, 0, 0}};
    CHECK(!Splits_Better(&unfinished, NULL), "splits: an unfinished run is never the pace");

    /* No pace to race: the split's own time shows. */
    Splits_Reset(&s, NULL);
    g.linesCleared = 10;
    g.elapsed = 24.1f;
    Splits_React(&s, &g);
    CHECK(!Splits_Diff(&s, 0, &diff) && Splits_Text(&s, buf, sizeof(buf), &gold) && strcmp(buf, "10: 0:24.10") == 0 && !gold,
          "splits: with no best pace the mark shows its time");

    /* Other modes take no marks. */
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, true}, 1, 0, 1, 0);
    Splits_Reset(&s, &best);
    g.linesCleared = 30;
    g.elapsed = 60.0f;
    CHECK(!Splits_React(&s, &g) && s.marks == 0, "splits: a Marathon has no splits");

    /* The callout: under the clear's word, gold when ahead; once. */
    HudFx h;
    Game_InitConfig(&g, (GameConfig){MODE_SPRINT, 10, 20, true}, 1, 0, 1, 0);
    HudFx_Reset(&h, &g);
    HudFx_NoteSplit(&h, "10: -1.4 s", true);
    g.justLocked = true;
    g.lastClearCount = 2;
    g.lastLockType = PIECE_I;
    HudFx_React(&h, &g, false);
    const Callout *cs[CALLOUT_MAX];
    int n = HudFx_Callouts(&h, cs);
    CHECK(n == 2 && strcmp(cs[0]->text, "DOUBLE") == 0 && strcmp(cs[1]->text, "10: -1.4 s") == 0 && cs[1]->tint == TINT_GOLD && cs[1]->kind == CALL_SPLIT,
          "splits: the split is a callout under the clear's word, gold when ahead");
    CHECK(HudFx_CalloutMaxSize(&h, cs[0]) == 32 && HudFx_CalloutMaxSize(&h, cs[1]) == 16, "splits: the clear's word is the headline");
    CHECK(HudFx_Nudge(&h, STAT_TIME, true) == NUDGE_PX && !h.splitPending, "splits: the clock nudges, and the note is spent");
    Game_ConsumeFrameFlags(&g);
    HudFx_Tick(&h, 1.0f, true);
    HudFx_React(&h, &g, false);
    CHECK(HudFx_Callouts(&h, cs) == 0, "splits: the next frame brings no second split callout");
    HudFx_NoteSplit(&h, "20: +0.8 s", false);
    HudFx_React(&h, &g, false);
    n = HudFx_Callouts(&h, cs);
    CHECK(n == 1 && strcmp(cs[0]->text, "20: +0.8 s") == 0 && cs[0]->tint == TINT_LIGHT, "splits: alone (a single took the mark) it is the line, not gold when behind");
}

static void test_splits_file(void) {
    SprintPace p, q;
    CHECK(!Splits_Load(10, 20, &p) && !p.have, "pace file: none to start with");
    SprintPace a = {true, {2410, 5080, 7700, 9920}}, b = {true, {3000, 6000, 9000, 12000}};
    CHECK(Splits_Save(10, 20, &a), "pace file: the classic board's pace saves");
    CHECK(Splits_Load(10, 20, &p) && p.have && p.at[0] == 2410 && p.at[3] == 9920, "pace file: ...and loads back");
    CHECK(!Splits_Load(14, 20, &q), "pace file: another board has none");
    CHECK(Splits_Save(14, 20, &b) && Splits_Load(14, 20, &q) && q.at[1] == 6000 && Splits_Load(10, 20, &p) && p.at[0] == 2410,
          "pace file: a second board's pace joins the file without touching the first");
    SprintPace a2 = {true, {2300, 4900, 7400, 9800}};
    CHECK(Splits_Save(10, 20, &a2) && Splits_Load(10, 20, &p) && p.at[3] == 9800 && Splits_Load(14, 20, &q) && q.at[3] == 12000,
          "pace file: a new pace replaces the board's line and keeps the other");
    char path[600];
    CHECK(Persist_DataFile(SPLITS_FILE, path, sizeof(path)), "pace file: (its path)");
    FILE *f = fopen(path, "r");
    int headers = 0, rows = 0;
    char line[256];
    bool classicKey = false;
    while (f && fgets(line, sizeof(line), f)) {
        if (line[0] == '#') headers++;
        else if (line[0]) rows++;
        if (strncmp(line, "sprint40|2300|4900|7400|9800", 28) == 0) classicKey = true;
    }
    if (f) fclose(f);
    CHECK(headers == 1 && rows == 2 && classicKey, "pace file: one header, one line a board, the classic board as sprint40|...");
    /* A line that is not a pace is left as it is and never read as one. */
    f = fopen(path, "a");
    if (f) { fprintf(f, "sprint40-8x26|12|x|3\nsomething else\n"); fclose(f); }
    CHECK(!Splits_Load(8, 26, &q), "pace file: a malformed line is no pace");
    SprintPace c = {true, {1, 2, 3, 4}};
    CHECK(Splits_Save(8, 26, &c) && Splits_Load(8, 26, &q) && q.at[3] == 4, "pace file: ...and saving over it works");
    f = fopen(path, "r");
    bool keptOther = false;
    while (f && fgets(line, sizeof(line), f)) if (strncmp(line, "something else", 14) == 0) keptOther = true;
    if (f) fclose(f);
    CHECK(keptOther, "pace file: an unknown line is kept");
    SprintPace none = {false, {0}};
    CHECK(!Splits_Save(10, 20, &none), "pace file: an unfinished pace is never written");
    remove(path);
}

/* The clock a split is taken from starts on GO: READY's 0.6 s is not on it. */
static void test_clock_starts_on_go(void) {
    Game g;
    PlayDriver d;
    Splits s;
    Game_InitConfig(&g, (GameConfig){MODE_SPRINT, 10, 20, true}, 3, 0, 1, 0);
    memset(&d, 0, sizeof(d));
    PlayDriver_Start(&d, true, false); /* READY, then GO */
    Splits_Reset(&s, NULL);
    /* Nine lines done, the tenth ready for a vertical I in the last column. */
    g.linesCleared = 9;
    Board_Init(&g.board, 10, 20);
    int floor = Board_TotalH(&g.board) - 1;
    for (int x = 0; x < 9; x++) g.board.cells[floor][x] = (int8_t)PIECE_J;
    Place(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
    g.next = PIECE_O;
    PlayIntent drop = {false, false, false, false, true, false, false, false}, none = {0};
    PlayStepResult out;
    int frames = 0;
    /* A hard drop pressed during READY never happens. */
    Play_Step(&d, &g, &drop, FRAME, 1.0f, &out);
    Splits_React(&s, &g);
    while (PlayDriver_Waiting(&d) && frames < 100) {
        Play_Step(&d, &g, &none, FRAME, 1.0f, &out);
        Splits_React(&s, &g);
        frames++;
    }
    CHECK(frames >= 35 && frames <= 37 && g.elapsed == 0.0f && s.marks == 0 && g.piecesLocked == 0,
          "clock on GO: through READY the clock stays at 0:00.00 and no mark is taken");
    for (int i = 0; i < 30; i++) { Play_Step(&d, &g, &none, FRAME, 1.0f, &out); Splits_React(&s, &g); }
    CHECK(g.elapsed > 0.49f && g.elapsed < 0.52f, "clock on GO: half a second after GO the clock says so");
    Play_Step(&d, &g, &drop, FRAME, 1.0f, &out);
    bool took = Splits_React(&s, &g);
    CHECK(took && g.linesCleared == 10 && s.marks == 1 && s.run.at[0] == Splits_Centis(g.elapsed) && s.run.at[0] >= 50 && s.run.at[0] <= 53,
          "clock on GO: the 10 mark is 0.5 s, the time since GO, not 1.1 s since READY");
    /* With READY-GO off the clock runs from the first frame. */
    Game_InitConfig(&g, (GameConfig){MODE_SPRINT, 10, 20, true}, 3, 0, 1, 0);
    memset(&d, 0, sizeof(d));
    PlayDriver_Start(&d, false, false);
    Play_Step(&d, &g, &none, FRAME, 1.0f, &out);
    CHECK(NEAR(g.elapsed, FRAME), "clock on GO: with Ready, go off it runs at once");
}

/* The finish line on the results card. */
static void test_finish_line(void) {
    Game g;
    Game_InitConfig(&g, (GameConfig){MODE_SPRINT, 10, 20, true}, 3, 0, 1, 0);
    g.phase = GS_GAMEOVER;
    g.won = true;
    g.linesCleared = 40;
    g.score = 9000;
    g.piecesLocked = 100;
    g.elapsed = 99.2f;
    RunFacts f;
    memset(&f, 0, sizeof(f));
    f.best = Record_SprintValue(100.0f);
    f.board = "Classic 10x20";
    ResultsCard c;
    Results_Build(&c, &g, &f);
    CHECK(c.newBest && strcmp(c.bestLine, "New best by 0.8 s (was 1:40.00)") == 0, "finish: a Sprint beating its best by 0.80 s says so");
    g.elapsed = 100.0f;
    Results_Build(&c, &g, &f);
    CHECK(!c.newBest && strcmp(c.bestLine, "Best 1:40.00, matched") == 0, "finish: the same time matches");
    f.best = 0;
    g.elapsed = 99.2f;
    Results_Build(&c, &g, &f);
    CHECK(c.newBest && strcmp(c.bestLine, "No best before this one") == 0, "finish: the first finish has nothing to beat by");
    /* Points boards keep their words. */
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, false}, 3, 0, 1, 0);
    g.phase = GS_GAMEOVER;
    g.score = 30000;
    g.piecesLocked = 100;
    f.best = 26714;
    Results_Build(&c, &g, &f);
    CHECK(c.newBest && strcmp(c.bestLine, "Old best 26 714") == 0, "finish: a points board still says Old best");
}

/* Two runs with the same presses, one driving danger and the splits every
 * frame: the games stay identical. */
static void test_rules_unchanged(void) {
    Rng rng;
    Rng_Seed(&rng, 99);
    bool same = true;
    int dangers = 0, marks = 0, frames = 0;
    for (int run = 0; run < 4 && same; run++) {
        Game a, b;
        PlayDriver da, db;
        Danger d;
        Splits s;
        SprintPace pace = {true, {1000, 2000, 3000, 4000}};
        GameConfig cfg = {run % 2 ? MODE_SPRINT : MODE_MARATHON, 10, 20, true};
        Game_InitConfig(&a, cfg, (uint64_t)run + 10, 0, 1, 0);
        Game_InitConfig(&b, cfg, (uint64_t)run + 10, 0, 1, 0);
        memset(&da, 0, sizeof(da));
        memset(&db, 0, sizeof(db));
        PlayDriver_Start(&da, true, false);
        PlayDriver_Start(&db, true, false);
        Danger_Reset(&d);
        Splits_Reset(&s, &pace);
        for (int f = 0; f < 3000 && same; f++) {
            uint32_t r = Rng_Next(&rng);
            PlayIntent in = {(r & 7) == 0, (r & 7) == 1, (r & 15) == 2, (r & 31) == 3, (r & 15) == 4, (r & 127) == 5, (r & 31) == 6, false};
            PlayStepResult sa, sb;
            Play_Step(&da, &a, &in, FRAME, 1.0f, &sa);
            Play_Step(&db, &b, &in, FRAME, 1.0f, &sb);
            Danger_Update(&d, &b, FRAME);
            if (Splits_React(&s, &b)) marks++;
            Splits_Tick(&s, FRAME);
            (void)Danger_Intensity(&b, d.freeRows, d.on);
            (void)Danger_Frame(&d, false);
            if (d.on) dangers++;
            da.cue.goSound = db.cue.goSound = false;
            Game_ConsumeFrameFlags(&a);
            Game_ConsumeFrameFlags(&b);
            frames++;
            if (memcmp(&a, &b, sizeof(Game)) != 0) same = false;
            if (a.phase == GS_GAMEOVER) {
                Game_Restart(&a, (uint64_t)(run * 1000 + f));
                Game_Restart(&b, (uint64_t)(run * 1000 + f));
                Danger_Reset(&d);
                Splits_Reset(&s, &pace);
            }
        }
    }
    printf("  danger: %d frames, %d in danger, %d split marks taken\n", frames, dangers, marks);
    CHECK(same && dangers > 0, "rules: driving danger and the splits alongside a run changes nothing in the rules");
}

void test_danger_all(void) {
    test_free_rows_and_hysteresis();
    test_warning_and_outline();
    test_intensity();
    test_ultra_ticks_and_sprint_gold();
    test_danger_shared_warning();
    test_splits_math();
    test_splits_file();
    test_clock_starts_on_go();
    test_finish_line();
    test_rules_unchanged();
}
