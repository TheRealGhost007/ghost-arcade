/* Which sounds a frame of play asks for (soundplan.c), with no window: the
 * one-big-sound rule on thousands of scripted locks (clears, quads, back to
 * back, every power-up, with and without clears), each lock's exact cues,
 * the pitches (size, back-to-back, the combo's semitones), the level up's
 * 0.15 s wait, the hold, the moves, GO, the coin, Ultra's warning, the
 * Freeze's thaw and the Slow's wind-up, and that driving every raylib-free
 * effect alongside a run changes nothing in the rules. The events come from
 * real locks through the rules' public calls. */
#include <string.h>
#include "check.h"
#include "../src/board.h"
#include "../src/game.h"
#include "../src/feel.h"
#include "../src/hudfx.h"
#include "../src/playstep.h"
#include "../src/powerfx.h"
#include "../src/soundplan.h"
#include "sfx.h"
#include "rng.h"

#define FRAME (1.0f / 60.0f)
#define NEAR(a, b) ((a) - (b) < 0.001f && (b) - (a) < 0.001f)

static const SoundEvents kQuiet = {0};

static void Row(Game *g, int y, const char *pattern) {
    int row = Board_TotalH(&g->board) - 1 - y;
    for (int x = 0; x < g->board.w; x++) {
        char c = x < (int)strlen(pattern) ? pattern[x] : '.';
        g->board.cells[row][x] = c == '#' ? (int8_t)PIECE_J : CELL_EMPTY;
    }
}

/* The piece, carrying nothing (Power mode hands out power-ups at random;
 * the tests that want one set it after). */
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

/* One frame: react, tick, take the cues, consume the flags. */
static int Frame(SoundPlan *p, Game *g, const SoundEvents *e, SoundCue *out, int max) {
    SoundPlan_React(p, g, e);
    SoundPlan_Tick(p, FRAME);
    int n = SoundPlan_Take(p, out, max);
    Game_ConsumeFrameFlags(g);
    return n;
}

static int Count(const SoundCue *c, int n, SoundWeight w) {
    int k = 0;
    for (int i = 0; i < n; i++) if (c[i].weight == w) k++;
    return k;
}

static bool Has(const SoundCue *c, int n, SoundHow how, int id) {
    for (int i = 0; i < n; i++) if (c[i].how == how && c[i].id == id) return true;
    return false;
}

static const SoundCue *Find(const SoundCue *c, int n, int id) {
    for (int i = 0; i < n; i++) if (c[i].how != SND_JINGLE && c[i].id == id) return &c[i];
    return NULL;
}

/* A vertical I dropped into the open column of `rows` rows (the rest of
 * the stack random), carrying `power` on one block. */
static void DropI(Game *g, Rng *rng, int rows, PowerUp power) {
    Board_Init(&g->board, g->cfg.width, g->cfg.height);
    int w = g->board.w, col = w - 1, stack = rows + (int)Rng_Range(rng, 3);
    for (int y = 0; y < stack; y++) {
        int row = Board_TotalH(&g->board) - 1 - y;
        for (int x = 0; x < w; x++) {
            bool filled = x != col && (y < rows || Rng_Range(rng, 3) != 0);
            g->board.cells[row][x] = filled ? (int8_t)Rng_Range(rng, PIECE_COUNT) : CELL_EMPTY;
        }
    }
    Place(g, PIECE_I, 1, col - 2, BOARD_HIDDEN);
    g->curPower = power;
    g->curPowerCell = (int)Rng_Range(rng, 4);
    Game_HardDrop(g);
}

/* ------------------------------------------------------ the one rule */

static void test_one_big_sound_per_lock(void) {
    Rng rng;
    Rng_Seed(&rng, 31);
    static const PowerUp kPowers[] = {PU_NONE, PU_NONE, PU_NONE, PU_BOMB, PU_LASER, PU_FREEZE, PU_SLOW, PU_SWEEP};
    int locks = 0, tooManyBig = 0, tooManyAccent = 0, silent = 0;
    int bigs[SFX_COUNT] = {0}, b2b = 0, powerAndClear = 0, powerAndClearOneBig = 0, coins = 0;
    SoundCue c[SOUND_CUES];
    for (int k = 0; k < 3000; k++) {
        Game g;
        SoundPlan p;
        PowerUp power = kPowers[k % 8];
        Game_InitConfig(&g, (GameConfig){power == PU_NONE ? MODE_MARATHON : MODE_POWER, 10, 20, k & 1}, (uint64_t)k, 0, 1, 0);
        SoundPlan_Reset(&p, &g);
        /* Some locks follow a quad (back to back) or a clear (a combo). */
        if (k % 5 == 0) { g.backToBack = true; g.stats.lastQuad = true; }
        if (k % 3 == 0) { g.combo = 2; g.stats.combo = 2; }
        int rows = (int)Rng_Range(&rng, 5); /* 0 to 4 */
        DropI(&g, &rng, rows, power);
        if (!g.justLocked) continue;
        int cleared = g.lastClearCount;
        int n = Frame(&p, &g, &kQuiet, c, SOUND_CUES);
        locks++;
        int big = Count(c, n, SND_BIG), accent = Count(c, n, SND_ACCENT);
        if (big > 1) tooManyBig++;
        if (accent > 1) tooManyAccent++;
        if (n == 0) silent++;
        for (int i = 0; i < n; i++) if (c[i].weight == SND_BIG && c[i].id >= 0 && c[i].id < SFX_COUNT) bigs[c[i].id]++;
        if (Has(c, n, SND_PLAY, SFX_B2B)) b2b++;
        if (Has(c, n, SND_JINGLE, JINGLE_COIN)) coins++;
        if (power != PU_NONE && cleared > 0) {
            powerAndClear++;
            if (big == 1 && Find(c, n, SoundPlan_PowerSfx(power)) && !Find(c, n, SFX_CLEAR) && !Find(c, n, SFX_QUAD)) powerAndClearOneBig++;
        }
    }
    printf("  sound: %d locks, %d with a power-up and a clear together\n", locks, powerAndClear);
    CHECK(locks > 2500 && tooManyBig == 0, "one rule: no lock ever plays two big sounds");
    CHECK(tooManyAccent == 0, "one rule: no lock ever plays two accents");
    CHECK(silent == 0, "one rule: every lock makes a sound (at least its tick or thud)");
    CHECK(bigs[SFX_CLEAR] > 0 && bigs[SFX_QUAD] > 0 && bigs[SFX_BOMB] > 0 && bigs[SFX_LASER] > 0 && bigs[SFX_FREEZE] > 0 &&
              bigs[SFX_SLOW] > 0 && bigs[SFX_SWEEP] > 0,
          "one rule: the trials heard every big sound (clear, quad, each power-up)");
    CHECK(b2b > 0 && coins > 0, "one rule: the trials heard the back-to-back sparkle and the power-up's coin");
    CHECK(powerAndClear > 100 && powerAndClearOneBig == powerAndClear,
          "one rule: a power-up that also clears rows plays the power-up's sound and not the clear's");
}

/* ------------------------------------------------------ each lock */

static void test_lock_cues(void) {
    Game g;
    SoundPlan p;
    SoundCue c[SOUND_CUES];
    int n;
    Game_InitConfig(&g, (GameConfig){MODE_POWER, 10, 20, true}, 5, 0, 1, 0);
    SoundPlan_Reset(&p, &g);

    /* A natural lock: the soft tick, nothing else. */
    Board_Init(&g.board, 10, 20);
    Place(&g, PIECE_O, 0, 4, Board_TotalH(&g.board) - 2);
    for (int i = 0; i < 40 && !g.justLocked; i++) Game_Update(&g, FRAME);
    CHECK(g.justLocked && !g.justHardDropped, "lock cues: (the O locked on its own)");
    n = Frame(&p, &g, &kQuiet, c, SOUND_CUES);
    CHECK(n == 1 && c[0].how == SND_PLAY && c[0].id == SFX_LOCK && c[0].weight == SND_ACCENT, "lock cues: a natural lock is one soft tick");

    /* A hard drop into nothing: the thud alone. */
    Place(&g, PIECE_O, 0, 0, BOARD_HIDDEN);
    Game_HardDrop(&g);
    n = Frame(&p, &g, &kQuiet, c, SOUND_CUES);
    CHECK(n == 1 && c[0].id == SFX_HARD_DROP && c[0].weight == SND_ACCENT, "lock cues: a hard drop that clears nothing is its thud");

    /* A single: the thud, then the clear at pitch 1. */
    Board_Init(&g.board, 10, 20);
    Row(&g, 0, "#########.");
    Place(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
    g.stats.combo = -1;
    Game_HardDrop(&g);
    n = Frame(&p, &g, &kQuiet, c, SOUND_CUES);
    CHECK(n == 2 && c[0].id == SFX_HARD_DROP && c[1].how == SND_PLAY && c[1].id == SFX_CLEAR && c[1].weight == SND_BIG,
          "lock cues: a hard-dropped single is the thud, then the clear as it always sounded");

    /* The next single in the chain rings a semitone up; a double, a triple
     * start higher still. */
    Board_Init(&g.board, 10, 20);
    Row(&g, 0, "#########.");
    Place(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
    Game_HardDrop(&g);
    n = Frame(&p, &g, &kQuiet, c, SOUND_CUES);
    CHECK(g.stats.combo == 1 && n == 2 && c[1].how == SND_PITCHED && c[1].id == SFX_CLEAR && NEAR(c[1].arg, SEMITONE),
          "lock cues: the second clear of a chain is a semitone up");
    CHECK(NEAR(SoundPlan_ClearPitch(2, false, 0), 1.12f) && NEAR(SoundPlan_ClearPitch(3, false, 0), 1.26f) &&
              NEAR(SoundPlan_ClearPitch(2, false, 3), 1.12f * SEMITONE * SEMITONE * SEMITONE),
          "lock cues: a double and a triple ring higher, and each combo step adds a semitone");
    CHECK(NEAR(SoundPlan_ClearPitch(1, false, 40), SoundPlan_ClearPitch(1, false, COMBO_PITCH_STEPS)), "lock cues: the rise stops an octave up");

    /* A quad: its own sound; back to back: the sparkle takes the thud's place. */
    Board_Init(&g.board, 10, 20);
    for (int y = 0; y < 4; y++) Row(&g, y, "#########.");
    Row(&g, 4, "#........."); /* not a perfect clear (3.2.0): that has a sound of its own */
    Place(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
    g.stats.combo = -1;
    g.backToBack = false;
    Game_HardDrop(&g);
    n = Frame(&p, &g, &kQuiet, c, SOUND_CUES);
    CHECK(n == 2 && c[0].id == SFX_HARD_DROP && c[1].how == SND_PLAY && c[1].id == SFX_QUAD, "lock cues: a quad is the thud and its own sound");
    Board_Init(&g.board, 10, 20);
    for (int y = 0; y < 4; y++) Row(&g, y, "#########.");
    Row(&g, 4, "#........."); /* not a perfect clear (3.2.0): that has a sound of its own */
    Place(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
    g.stats.combo = -1;
    g.linesCleared = 0; /* no level up here */
    Game_HardDrop(&g);
    bool wasB2B = g.lastWasB2B;
    n = Frame(&p, &g, &kQuiet, c, SOUND_CUES);
    CHECK(wasB2B && n == 2 && c[0].id == SFX_B2B && c[0].weight == SND_ACCENT && c[1].how == SND_PITCHED && c[1].id == SFX_QUAD &&
              NEAR(c[1].arg, 1.122f),
          "lock cues: a back-to-back quad is the sparkle and the quad two semitones brighter");

    /* A power-up: the soft coin and the power's own sound; no thud, and no
     * clear sound even when it clears. */
    Board_Init(&g.board, 10, 20);
    Row(&g, 0, "#########.");
    Row(&g, 1, "#####.###.");
    Place(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
    g.curPower = PU_BOMB;
    g.curPowerCell = 1;
    g.linesCleared = 0;
    Game_HardDrop(&g);
    bool bombCleared = g.lastPower == PU_BOMB && g.lastClearCount == 1;
    n = Frame(&p, &g, &kQuiet, c, SOUND_CUES);
    CHECK(bombCleared && n == 2 && c[0].how == SND_JINGLE && c[0].id == JINGLE_COIN &&
              c[0].weight == SND_ACCENT && c[1].id == SFX_BOMB && c[1].weight == SND_BIG,
          "lock cues: a Bomb that clears a row is the coin and the bomb: no thud, no clear sound");
    static const PowerUp kAll[] = {PU_LASER, PU_FREEZE, PU_SLOW, PU_SWEEP};
    static const int kSfx[] = {SFX_LASER, SFX_FREEZE, SFX_SLOW, SFX_SWEEP};
    bool each = true;
    for (int i = 0; i < 4; i++) {
        Board_Init(&g.board, 10, 20);
        Place(&g, PIECE_O, 0, 0, BOARD_HIDDEN);
        g.curPower = kAll[i];
        g.curPowerCell = 0;
        Game_HardDrop(&g);
        n = Frame(&p, &g, &kQuiet, c, SOUND_CUES);
        if (!(n == 2 && c[0].id == JINGLE_COIN && c[0].how == SND_JINGLE && c[1].id == kSfx[i] && c[1].weight == SND_BIG)) each = false;
        if (SoundPlan_PowerSfx(kAll[i]) != kSfx[i]) each = false;
    }
    CHECK(each, "lock cues: Laser, Freeze, Slow and Sweep each play the coin and their own sound");

    /* The level up waits 0.15 s after its clear. */
    Board_Init(&g.board, 10, 20);
    Row(&g, 0, "#########.");
    Place(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
    g.linesCleared = 9;
    g.level = 1;
    g.stats.combo = -1;
    Game_HardDrop(&g);
    CHECK(g.lastWasLevelUp, "lock cues: (the single made level 2)");
    n = Frame(&p, &g, &kQuiet, c, SOUND_CUES);
    CHECK(n == 2 && !Find(c, n, SFX_LEVEL_UP), "lock cues: the level-up sound is not on the clear's frame");
    int when = -1;
    for (int f = 1; f <= 20 && when < 0; f++) {
        n = Frame(&p, &g, &kQuiet, c, SOUND_CUES);
        if (Find(c, n, SFX_LEVEL_UP)) when = f;
    }
    CHECK(when == 8 || when == 9, "lock cues: ...it plays 0.15 s later");
    bool again = false;
    for (int f = 0; f < 20; f++) { n = Frame(&p, &g, &kQuiet, c, SOUND_CUES); if (n) again = true; }
    CHECK(!again, "lock cues: ...once");

    /* The frame the run ends: nothing from the planner (the end has its one
     * sound), and nothing waiting plays over it. */
    Board_Init(&g.board, 10, 20);
    Row(&g, 0, "#########.");
    Place(&g, PIECE_I, 1, 7, BOARD_HIDDEN);
    g.linesCleared = 19;
    g.level = 2;
    Game_HardDrop(&g);
    SoundEvents end = {0};
    end.ending = true;
    n = Frame(&p, &g, &end, c, SOUND_CUES);
    CHECK(n == 0 && p.laters == 0, "lock cues: the run's last frame asks for nothing, the level up it made included");
}

/* ------------------------------------------------------ the rest */

static void test_frame_cues(void) {
    Game g;
    SoundPlan p;
    SoundCue c[SOUND_CUES];
    int n;
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, true}, 9, 0, 1, 0);
    SoundPlan_Reset(&p, &g);

    SoundEvents e = {0};
    e.moved = true;
    n = Frame(&p, &g, &e, c, SOUND_CUES);
    CHECK(n == 1 && c[0].how == SND_VARIED && c[0].id == SFX_MOVE && NEAR(c[0].arg, MOVE_VARIANCE) && c[0].weight == SND_SMALL,
          "frame cues: a move varies a little");
    CHECK(Game_MoveLeft(&g) && Frame(&p, &g, &kQuiet, c, SOUND_CUES) == 1 && c[0].how == SND_VARIED && c[0].id == SFX_MOVE,
          "frame cues: ...from the rules' own flag too");
    memset(&e, 0, sizeof(e));
    e.rotated = true;
    n = Frame(&p, &g, &e, c, SOUND_CUES);
    CHECK(n == 1 && c[0].how == SND_PLAY && c[0].id == SFX_ROTATE, "frame cues: a turn");
    memset(&e, 0, sizeof(e));
    e.rotateBlocked = true;
    n = Frame(&p, &g, &e, c, SOUND_CUES);
    CHECK(n == 1 && c[0].how == SND_PITCHED && c[0].id == SFX_ROTATE && NEAR(c[0].arg, ROTATE_BLOCKED_PITCH), "frame cues: a turn that cannot be made is dull");
    memset(&e, 0, sizeof(e));
    e.held = true;
    n = Frame(&p, &g, &e, c, SOUND_CUES);
    CHECK(n == 1 && c[0].how == SND_PLAY && c[0].id == SFX_HOLD, "frame cues: Hold has its own swoosh (it borrowed the turn's sound)");
    CHECK(Game_Hold(&g) && Frame(&p, &g, &kQuiet, c, SOUND_CUES) == 1 && c[0].id == SFX_HOLD, "frame cues: ...from the rules' flag too");
    memset(&e, 0, sizeof(e));
    e.go = true;
    n = Frame(&p, &g, &e, c, SOUND_CUES);
    CHECK(n == 1 && c[0].how == SND_JINGLE && c[0].id == JINGLE_GO, "frame cues: GO");
    memset(&e, 0, sizeof(e));
    e.passedBest = true;
    e.moved = true;
    n = Frame(&p, &g, &e, c, SOUND_CUES);
    CHECK(n == 2 && c[0].how == SND_JINGLE && c[0].id == JINGLE_COIN && c[1].id == SFX_MOVE, "frame cues: passing your best is a coin, first");

    /* A Freeze running out: the thaw tick; a Slow: the wind-up. Only in
     * play, only once, and not when the timer is set straight back. */
    Game_InitConfig(&g, (GameConfig){MODE_POWER, 10, 20, false}, 9, 0, 1, 0);
    SoundPlan_Reset(&p, &g);
    Place(&g, PIECE_O, 0, 0, BOARD_HIDDEN);
    g.curPower = PU_FREEZE;
    Game_HardDrop(&g);
    Frame(&p, &g, &kQuiet, c, SOUND_CUES);
    int thaws = 0, frames = 0;
    while (g.freezeTimer > 0.0f && frames < 1000) {
        Game_Update(&g, FRAME);
        n = Frame(&p, &g, &kQuiet, c, SOUND_CUES);
        if (Has(c, n, SND_PLAY, SFX_THAW)) thaws++;
        frames++;
    }
    for (int i = 0; i < 10; i++) { Game_Update(&g, FRAME); n = Frame(&p, &g, &kQuiet, c, SOUND_CUES); if (Has(c, n, SND_PLAY, SFX_THAW)) thaws++; }
    CHECK(thaws == 1 && frames > 300, "frame cues: the Freeze's end is one thaw tick, six seconds on");
    Place(&g, PIECE_O, 0, 0, BOARD_HIDDEN);
    g.curPower = PU_SLOW;
    Game_HardDrop(&g);
    Frame(&p, &g, &kQuiet, c, SOUND_CUES);
    int windups = 0;
    frames = 0;
    Game_TogglePause(&g);
    for (int i = 0; i < 30; i++) { n = Frame(&p, &g, &kQuiet, c, SOUND_CUES); if (Has(c, n, SND_PLAY, SFX_WINDUP)) windups++; }
    Game_TogglePause(&g);
    while (g.slowTimer > 0.0f && frames < 2000) {
        Game_Update(&g, FRAME);
        n = Frame(&p, &g, &kQuiet, c, SOUND_CUES);
        if (Has(c, n, SND_PLAY, SFX_WINDUP)) windups++;
        frames++;
    }
    CHECK(windups == 1 && frames > 600, "frame cues: the Slow's end is one wind-up (a pause holds it)");

    /* Ultra's last ten seconds: WARNING once, as the countdown turns red. */
    Game_InitConfig(&g, (GameConfig){MODE_ULTRA, 10, 20, true}, 9, 0, 1, 0);
    SoundPlan_Reset(&p, &g);
    g.timeLeft = 11.5f;
    int warns = 0, at = -1;
    for (int i = 0; i < 180; i++) {
        Game_Update(&g, FRAME);
        n = Frame(&p, &g, &kQuiet, c, SOUND_CUES);
        if (Has(c, n, SND_JINGLE, JINGLE_WARNING)) { warns++; if (at < 0) at = i; }
    }
    CHECK(warns == 1 && at > 20 && at < 40 && Hud_UltraTint(11.5f - (float)(at + 1) * FRAME) == ULTRA_RED,
          "frame cues: Ultra warns once, the moment the countdown turns red at 0:10");
}

/* ------------------------------------------------------ no effect on the rules */

/* Two runs with the same seed and the same presses, one of them driving
 * every raylib-free effect each frame (feel, callouts, the sound plan, the
 * power-ups' timelines): the games stay identical, byte for byte. */
static void test_effects_change_nothing(void) {
    Rng rng;
    Rng_Seed(&rng, 77);
    bool same = true;
    int frames = 0, locks = 0, powers = 0, sweeps = 0;
    for (int run = 0; run < 6 && same; run++) {
        Game a, b;
        PlayDriver da, db;
        Feel feel;
        HudFx hud;
        SoundPlan plan;
        GameConfig cfg = {run % 2 ? MODE_POWER : MODE_MARATHON, 10, 20, true};
        Game_InitConfig(&a, cfg, (uint64_t)run, 0, 1, 0);
        Game_InitConfig(&b, cfg, (uint64_t)run, 0, 1, 0);
        memset(&da, 0, sizeof(da));
        memset(&db, 0, sizeof(db));
        PlayDriver_Start(&da, true, false);
        PlayDriver_Start(&db, true, false);
        Feel_Reset(&feel, &b, true);
        HudFx_Reset(&hud, &b);
        SoundPlan_Reset(&plan, &b);
        FeelFrame ff = {true, true, false};
        float age = 0.0f;
        for (int f = 0; f < 2400 && same; f++) {
            uint32_t r = Rng_Next(&rng);
            PlayIntent in = {(r & 7) == 0, (r & 7) == 1, (r & 15) == 2, (r & 31) == 3, (r & 63) == 4, (r & 127) == 5, (r & 31) == 6, false};
            /* Power mode: a power-up on most pieces, so the shows run. */
            if (cfg.mode == MODE_POWER && a.curPower == PU_NONE && (r & 3) == 0) {
                a.curPower = b.curPower = (PowerUp)(1 + (r >> 8) % (PU_COUNT - 1));
                a.curPowerCell = b.curPowerCell = (int)((r >> 12) & 3);
            }
            PlayStepResult sa, sb;
            Play_Step(&da, &a, &in, FRAME, 1.0f, &sa);
            Play_Step(&db, &b, &in, FRAME, 1.0f, &sb);
            /* Every effect, on b only. */
            Feel_React(&feel, &b, &ff);
            HudFx_React(&hud, &b, false);
            SoundEvents e = {sb.moved, sb.rotated, sb.rotateBlocked, sb.held, db.cue.goSound, false, b.phase == GS_GAMEOVER, false};
            SoundPlan_React(&plan, &b, &e);
            Feel_Tick(&feel, FRAME);
            HudFx_Tick(&hud, FRAME, b.phase == GS_PLAYING);
            SoundPlan_Tick(&plan, FRAME);
            SoundCue cues[SOUND_CUES];
            SoundPlan_Take(&plan, cues, SOUND_CUES);
            age += FRAME;
            (void)PowerFx_BombStage(age);
            (void)PowerFx_LaserCharge(age, true);
            (void)PowerFx_Frost(age, b.freezeTimer, true);
            (void)PowerFx_FreezeLit(b.freezeTimer, false);
            (void)Feel_SweepBar(&feel.clear, feel.clear.t);
            da.cue.goSound = db.cue.goSound = false;
            if (b.justLocked) locks++;
            if (b.lastPower != PU_NONE) powers++;
            if (b.lastPower == PU_SWEEP) sweeps++;
            Game_ConsumeFrameFlags(&a);
            Game_ConsumeFrameFlags(&b);
            frames++;
            if (memcmp(&a, &b, sizeof(Game)) != 0) same = false;
            if (a.phase == GS_GAMEOVER) {
                Game_Restart(&a, (uint64_t)(run * 1000 + f));
                Game_Restart(&b, (uint64_t)(run * 1000 + f));
                Feel_Reset(&feel, &b, true);
                HudFx_Reset(&hud, &b);
                SoundPlan_Reset(&plan, &b);
            }
        }
    }
    printf("  sound: %d frames, %d locks, %d power-ups (%d Sweeps) with every effect driven\n", frames, locks, powers, sweeps);
    CHECK(same && locks > 200 && powers > 20 && sweeps > 0, "effects: driving every effect alongside a run changes nothing in the rules");
}

void test_sound_all(void) {
    test_one_big_sound_per_lock();
    test_lock_cues();
    test_frame_cues();
    test_effects_change_nothing();
}
