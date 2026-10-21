/* The Practice room (3.5.0, REWORK_PLAN.md "Practice room"), with no window:
 * its options bend the rules (the start level, gravity off, the lock off,
 * infinite hold, starting garbage, hints), a restart keeps them; undo puts
 * the exact game back (the board, the bag and queue, Hold, the score, the
 * level, the clock, the rng) up to 50 steps, through the rules and through a
 * frame of play; the finesse search against known press counts and the
 * counter against what a frame pressed; nothing recorded (the run never
 * counts, no stats, no best, the card's words, no "are you sure?"); the
 * eleventh action, Undo, in the controls, config.ini (old files load
 * unchanged) and the play keys; the Settings page's twelfth row; the
 * panel's Undos left; the UNDO and hint callouts and the undo's sound; the
 * options' words. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "check.h"
#include "uiinput.h"
#include "uikit.h"
#include "uikeymap.h"
#include "../src/actions.h"
#include "../src/board.h"
#include "../src/controls.h"
#include "../src/finesse.h"
#include "../src/game.h"
#include "../src/hudfx.h"
#include "../src/layout.h"
#include "../src/menukeys.h"
#include "../src/menumodel.h"
#include "../src/pausemenu.h"
#include "../src/persist.h"
#include "../src/playstep.h"
#include "../src/practice.h"
#include "../src/results.h"
#include "../src/runrecord.h"
#include "../src/setpage.h"
#include "../src/soundplan.h"
#include "../src/stats.h"

#define FRAME (1.0f / 60.0f)
#define NEAR(a, b) ((a) - (b) < 0.001f && (b) - (a) < 0.001f)

static PracticeRules Plain(void) {
    PracticeRules r;
    memset(&r, 0, sizeof(r));
    r.on = true;
    r.startLevel = 1;
    return r;
}

static Game Room(PracticeRules r, uint64_t seed) {
    Game g;
    Game_InitPractice(&g, (GameConfig){MODE_MARATHON, 10, 20, true}, r, seed);
    return g;
}

/* The same on the biggest board, where sixty pieces fit without a top-out. */
static Game BigRoom(PracticeRules r, uint64_t seed) {
    Game g;
    Game_InitPractice(&g, (GameConfig){MODE_MARATHON, 24, 36, true}, r, seed);
    return g;
}

static Game Marathon(uint64_t seed) {
    Game g;
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, true}, seed, 0, 1, 0);
    return g;
}

/* One frame with this intent (the rest of the intent quiet). */
static PlayStepResult Frame(PlayDriver *d, Game *g, PlayIntent in) {
    PlayStepResult out;
    Play_Step(d, g, &in, FRAME, 1.0f, &out);
    return out;
}
static PlayIntent Quiet(void) { PlayIntent in; memset(&in, 0, sizeof(in)); return in; }
static void Tap(PlayDriver *d, Game *g, bool left, bool right) {
    PlayIntent in = Quiet();
    in.left = left;
    in.right = right;
    Frame(d, g, in);
    Frame(d, g, Quiet()); /* let go */
}

/* --------------------------------------------------------------- options */

static void test_options(void) {
    /* The start level: the level, its speed, and the lines counting up from it. */
    PracticeRules r = Plain();
    r.startLevel = 7;
    Game g = Room(r, 11);
    CHECK(g.practice.on && g.level == 7 && NEAR(g.fallInterval, Game_FallInterval(7)) && g.cfg.mode == MODE_MARATHON && g.highScore == 0,
          "options: a practice run begins at its start level, at that level's speed, as a Marathon with no best");
    g.linesCleared = 9;
    memset(g.board.cells, CELL_EMPTY, sizeof(g.board.cells));
    int total = Board_TotalH(&g.board);
    for (int x = 0; x < 10; x++) g.board.cells[total - 1][x] = x == 4 ? (int8_t)CELL_EMPTY : (int8_t)PIECE_J;
    g.current = PIECE_I; g.rotation = 1; g.px = 2; g.py = BOARD_HIDDEN; g.lowestRow = g.py;
    Game_HardDrop(&g);
    CHECK(g.lastClearCount == 1 && g.linesCleared == 10 && g.level == 8 && g.lastWasLevelUp, "options: ten lines from level 7 make level 8");
    r.startLevel = 99;
    Game hi = Room(r, 11);
    r.startLevel = 0;
    Game lo = Room(r, 11);
    CHECK(hi.level == PRACTICE_LEVEL_MAX && hi.practice.startLevel == PRACTICE_LEVEL_MAX && lo.level == 1, "options: the level is clamped to 1..20");

    /* Gravity off: the piece hangs; a soft drop still moves it. */
    r = Plain();
    r.noGravity = true;
    g = Room(r, 12);
    int y0 = g.py;
    for (int i = 0; i < 600; i++) Game_Update(&g, FRAME);
    CHECK(g.py == y0 && g.phase == GS_PLAYING && g.piecesLocked == 0, "gravity off: ten seconds later the piece has not moved");
    CHECK(Game_SoftDrop(&g) && g.py == y0 + 1, "gravity off: a soft drop still steps it down");
    Game_HardDrop(&g);
    CHECK(g.piecesLocked == 1, "gravity off: a hard drop still locks it");
    Game m = Marathon(12);
    for (int i = 0; i < 70; i++) Game_Update(&m, FRAME);
    CHECK(m.py > BOARD_HIDDEN, "gravity off: ...while a plain run's piece falls");

    /* The lock off: a landed piece never locks by itself. */
    r = Plain();
    r.noLock = true;
    g = Room(r, 13);
    g.py = Game_GhostY(&g);
    for (int i = 0; i < 600; i++) Game_Update(&g, FRAME);
    CHECK(g.grounded && g.piecesLocked == 0 && g.phase == GS_PLAYING, "lock off: ten seconds on the ground and it has not locked");
    CHECK(Game_MoveLeft(&g) && g.piecesLocked == 0, "lock off: it still slides");
    g.lockResets = MAX_LOCK_RESETS; /* the resets spent: the timer would run in the air too */
    g.py = BOARD_HIDDEN;
    for (int i = 0; i < 60; i++) Game_Update(&g, FRAME);
    CHECK(g.lockTimer == 0.0f, "lock off: the lock timer never runs, resets spent or not");
    Game_HardDrop(&g);
    CHECK(g.piecesLocked == 1, "lock off: a hard drop locks it");
    Game n = Marathon(13);
    n.py = Game_GhostY(&n);
    for (int i = 0; i < 40; i++) Game_Update(&n, FRAME);
    CHECK(n.piecesLocked == 1, "lock off: ...while a plain run's piece locks after half a second");

    /* Infinite hold. */
    r = Plain();
    r.infiniteHold = true;
    g = Room(r, 14);
    PieceType first = g.current, second = g.next;
    CHECK(Game_Hold(&g) && !g.holdUsed && g.hold == first && g.current == second, "infinite hold: a hold leaves Hold ready again");
    CHECK(Game_Hold(&g) && g.current == first && g.hold == second && !g.holdUsed, "infinite hold: and the next swaps straight back");
    CHECK(g.stats.holds == 2, "infinite hold: both counted");
    Game o = Marathon(14);
    CHECK(Game_Hold(&o) && o.holdUsed && !Game_Hold(&o), "infinite hold: ...while a plain run holds once per piece");

    /* Starting garbage: Dig's rows, one gap each, never the row below's. */
    r = Plain();
    r.garbageRows = 4;
    g = Room(r, 15);
    CHECK(Game_GarbageRows(&g.board) == 4 && Board_CountFilled(&g.board) == 4 * 9, "garbage: four grey rows, one gap each");
    bool differ = true;
    total = Board_TotalH(&g.board);
    for (int i = 1; i < 4; i++)
        if (Game_GarbageGap(&g.board, total - 1 - i) == Game_GarbageGap(&g.board, total - i)) differ = false;
    CHECK(differ, "garbage: no two neighbouring rows open the same way");
    Game same = Room(r, 15), other = Room(r, 16);
    CHECK(memcmp(same.board.cells, g.board.cells, sizeof(g.board.cells)) == 0 && memcmp(other.board.cells, g.board.cells, sizeof(g.board.cells)) != 0,
          "garbage: the same seed, the same rows; another seed, other gaps");
    CHECK(same.current == Marathon(15).current && same.next == Marathon(15).next, "garbage: the pieces are still the seed's (the gaps have their own rng)");
    r.garbageRows = 99;
    Game cap;
    Game_InitPractice(&cap, (GameConfig){MODE_MARATHON, 6, 12, true}, r, 15);
    CHECK(Game_GarbageRows(&cap.board) == 8, "garbage: capped at 12, and a short well keeps four rows clear");
    r.garbageRows = 0;
    Game none = Room(r, 15), plainRun = Marathon(15);
    CHECK(Game_GarbageRows(&none.board) == 0 && Game_GarbageRows(&plainRun.board) == 0, "garbage: none by default, none in a plain run");
    /* Clearing a garbage row in the room is a plain clear: no Dug bonus. */
    r.garbageRows = 1;
    g = Room(r, 17);
    memset(g.board.cells, CELL_EMPTY, sizeof(g.board.cells));
    Game_GarbageRow(&g.board, total - 1, 4);
    g.current = PIECE_I; g.rotation = 1; g.px = 2; g.py = BOARD_HIDDEN; g.lowestRow = g.py; g.score = 0;
    Game_HardDrop(&g);
    CHECK(g.lastClearCount == 1 && g.dug == 0 && g.score == 100 + 2L * g.lastDropCells, "garbage: a row cleared pays the single alone, nothing for digging");

    /* Hints on or off: the lookup is made only with them on. */
    r = Plain();
    r.hints = false;
    g = Room(r, 18);
    Game_HardDrop(&g);
    CHECK(g.lastFinesseExtra == -1 && g.lastFinesseOptimal == -1, "hints off: no hint");
    r.hints = true;
    g = Room(r, 18);
    Game_HardDrop(&g);
    CHECK(g.lastFinesseExtra == 0 && g.lastFinesseOptimal == 0 && g.lastFinessePresses == 0, "hints on: a piece dropped where it came in was clean");
    m = Marathon(18);
    Game_HardDrop(&m);
    CHECK(m.lastFinesseExtra == -1, "hints: never outside the room");

    /* A restart keeps the rules; a plain run has none. */
    r = Plain();
    r.startLevel = 5;
    r.garbageRows = 3;
    r.noLock = true;
    g = Room(r, 19);
    Game_HardDrop(&g);
    Game_Restart(&g, 77);
    CHECK(g.practice.on && g.practice.noLock && g.level == 5 && Game_GarbageRows(&g.board) == 3 && g.piecesLocked == 0 && g.score == 0,
          "restart: the room's rules and rows come back, the run starts over");
    CHECK(!Marathon(19).practice.on && !Game_IsPractice(&m) && Game_IsPractice(&g), "restart: a plain run is not a practice run");
}

/* ------------------------------------------------------------------ undo */

static Game Clean(const Game *g) {
    Game c = *g;
    Game_ConsumeFrameFlags(&c);
    return c;
}

static void test_undo(void) {
    static UndoStack u;
    PracticeRules r = Plain();
    r.garbageRows = 2;
    Game g = BigRoom(r, 2026);
    Undo_Reset(&u, &g);
    CHECK(Undo_Left(&u) == 0 && !Undo_Pop(&u, &g), "undo: nothing to take back as a run starts");

    /* Sixty pieces, each moved a little, with Hold now and then, and the
     * state at the start of each kept here to compare. */
    static Game starts[64];
    int n = 0;
    starts[n++] = Clean(&g);
    for (int i = 0; i < 60; i++) {
        if (i % 7 == 3) Game_Hold(&g);
        for (int k = 0; k < i % 11; k++) { if (i % 2) Game_MoveRight(&g); else Game_MoveLeft(&g); } /* spread over the well */
        if (i % 3 == 1) Game_Rotate(&g, 1);
        for (int k = 0; k < 30; k++) Game_Update(&g, FRAME); /* half a second of the clock */
        Game_HardDrop(&g);
        if (g.phase != GS_PLAYING) break;
        Undo_Push(&u, &g);
        Game_ConsumeFrameFlags(&g);
        starts[n++] = g;
    }
    CHECK(n == 61 && g.piecesLocked == 60, "undo: sixty pieces locked");
    CHECK(Undo_Left(&u) == PRACTICE_UNDO_MAX, "undo: fifty can be taken back, no more");

    /* Each undo is the exact start of the piece before: the board, the
     * queue and bag (its rng), Hold, the score, the level, the clock. */
    bool exact = true, flagged = true, counted = true;
    for (int i = 0; i < PRACTICE_UNDO_MAX; i++) {
        if (!Undo_Pop(&u, &g)) { exact = false; break; }
        if (!g.justUndid) flagged = false;
        g.justUndid = false;
        const Game *want = &starts[60 - 1 - i];
        if (memcmp(&g, want, sizeof(g)) != 0) exact = false;
        if (Undo_Left(&u) != PRACTICE_UNDO_MAX - 1 - i || u.undone != i + 1) counted = false;
    }
    CHECK(exact, "undo: fifty undos in a row each give back the whole game as it was when that piece came in");
    CHECK(flagged && counted, "undo: each says so for the frame, and the count comes down");
    CHECK(!Undo_Pop(&u, &g) && Undo_Left(&u) == 0 && g.piecesLocked == 10, "undo: the fifty-first is refused; the run stands at its tenth piece");
    Game back = g;
    CHECK(memcmp(back.queue, starts[10].queue, sizeof(back.queue)) == 0 && memcmp(&back.bag, &starts[10].bag, sizeof(back.bag)) == 0 &&
              back.hold == starts[10].hold && back.score == starts[10].score && back.level == starts[10].level && NEAR(back.elapsed, starts[10].elapsed),
          "undo: in particular the queue, the bag's position and rng, Hold, the score, the level and the clock");

    /* From there the same play gives the same pieces: the undo did not move the rng. */
    for (int i = 10; i < 60; i++) {
        if (i % 7 == 3) Game_Hold(&g);
        for (int k = 0; k < i % 11; k++) { if (i % 2) Game_MoveRight(&g); else Game_MoveLeft(&g); } /* spread over the well */
        if (i % 3 == 1) Game_Rotate(&g, 1);
        for (int k = 0; k < 30; k++) Game_Update(&g, FRAME);
        Game_HardDrop(&g);
        Game_ConsumeFrameFlags(&g);
    }
    CHECK(memcmp(&g, &starts[60], sizeof(g)) == 0, "undo: played on from the tenth piece, the sixtieth state is the same to the byte");

    /* Through a frame of play: the lock a frame makes is pushed, the undo
     * un-locks it, and the piece comes in again untouched. */
    PlayDriver d;
    memset(&d, 0, sizeof(d));
    PlayDriver_Start(&d, false, false);
    g = Room(Plain(), 7);
    Undo_Reset(&u, &g);
    PieceType cur = g.current;
    Tap(&d, &g, true, false);
    Tap(&d, &g, true, false);
    PlayIntent drop = Quiet();
    drop.hardDrop = true;
    Frame(&d, &g, drop);
    CHECK(g.justLocked && g.piecesLocked == 1, "frame: a hard drop locked the piece");
    Undo_Push(&u, &g);
    Game_ConsumeFrameFlags(&g);
    Frame(&d, &g, Quiet());
    CHECK(Undo_Left(&u) == 1 && Undo_Pop(&u, &g) && g.piecesLocked == 0 && g.current == cur && g.px == 3 && g.presses == 0 && Board_CountFilled(&g.board) == 0,
          "frame: the undo brought the piece back where it came in, its presses forgotten, the well empty");
    SoundPlan p;
    SoundPlan_Reset(&p, &g);
    SoundEvents none;
    memset(&none, 0, sizeof(none));
    SoundPlan_React(&p, &g, &none);
    SoundCue cues[SOUND_CUES];
    int nc = SoundPlan_Take(&p, cues, SOUND_CUES);
    CHECK(nc == 1 && cues[0].how == SND_PITCHED && cues[0].id == SFX_HOLD && NEAR(cues[0].arg, UNDO_PITCH) && cues[0].weight == SND_SMALL,
          "frame: the undo is a low swoosh, a small sound");
    HudFx h;
    HudFx_Reset(&h, &g);
    HudFx_React(&h, &g, false);
    const Callout *cs[CALLOUT_MAX];
    CHECK(HudFx_Callouts(&h, cs) == 1 && strcmp(cs[0]->text, "UNDO") == 0 && cs[0]->tint == TINT_LIGHT && HudFx_Nudge(&h, STAT_UNDO, true) == NUDGE_PX,
          "frame: UNDO over the well, and the Undos left stat nudges");
    Game_ConsumeFrameFlags(&g);
    CHECK(!g.justUndid, "frame: the flag is the frame's");
    /* A push onto a full stack drops the oldest start. */
    g = BigRoom(Plain(), 8);
    Undo_Reset(&u, &g);
    for (int i = 0; i < UNDO_SLOTS + 5; i++) {
        for (int k = 0; k < i % 11; k++) { if (i % 2) Game_MoveRight(&g); else Game_MoveLeft(&g); }
        Game_HardDrop(&g);
        Undo_Push(&u, &g);
        Game_ConsumeFrameFlags(&g);
        if (g.phase != GS_PLAYING) break;
    }
    CHECK(Undo_Left(&u) == PRACTICE_UNDO_MAX && u.count == UNDO_SLOTS, "ring: full, it keeps the newest fifty-one starts");
}

/* --------------------------------------------------------------- finesse */

/* The rows an O dropped at px lands on, on an empty board. */
static int Floor(const Board *b, PieceType t, int rot, int px) {
    int y = BOARD_HIDDEN;
    while (Board_TestFit(b, t, rot, px, y + 1)) y++;
    return y;
}

static void test_finesse(void) {
    Board b;
    Board_Init(&b, 10, 20);
    /* The O: the spawn, a tap, two taps, the walls, and one in from a wall. */
    CHECK(Finesse_Optimal(&b, PIECE_O, 0, 3, Floor(&b, PIECE_O, 0, 3)) == 0, "finesse: an O dropped where it came in is 0");
    CHECK(Finesse_Optimal(&b, PIECE_O, 0, 2, Floor(&b, PIECE_O, 0, 2)) == 1, "finesse: one column over is a tap: 1");
    CHECK(Finesse_Optimal(&b, PIECE_O, 0, 1, Floor(&b, PIECE_O, 0, 1)) == 2, "finesse: two columns over, two taps: 2");
    CHECK(Finesse_Optimal(&b, PIECE_O, 0, -1, Floor(&b, PIECE_O, 0, -1)) == 1, "finesse: against the left wall is one held slide: 1");
    CHECK(Finesse_Optimal(&b, PIECE_O, 0, 7, Floor(&b, PIECE_O, 0, 7)) == 1, "finesse: against the right wall too: 1");
    CHECK(Finesse_Optimal(&b, PIECE_O, 0, 0, Floor(&b, PIECE_O, 0, 0)) == 2, "finesse: one in from the left wall is a slide and a tap back: 2 (not three taps)");
    CHECK(Finesse_Optimal(&b, PIECE_O, 0, 6, Floor(&b, PIECE_O, 0, 6)) == 2, "finesse: one in from the right wall: 2");
    /* The I: flat at the spawn is 0; upright in a wall column is a turn and a slide. */
    CHECK(Finesse_Optimal(&b, PIECE_I, 0, 3, Floor(&b, PIECE_I, 0, 3)) == 0, "finesse: an I lying where it came in: 0");
    CHECK(Finesse_Optimal(&b, PIECE_I, 1, -2, Floor(&b, PIECE_I, 1, -2)) == 2, "finesse: an I upright in the left column: a turn and a slide, 2");
    CHECK(Finesse_Optimal(&b, PIECE_I, 1, 7, Floor(&b, PIECE_I, 1, 7)) == 2, "finesse: upright in the right column: 2");
    CHECK(Finesse_Optimal(&b, PIECE_I, 3, -1, Floor(&b, PIECE_I, 3, -1)) == 2, "finesse: the same cells by the other turn: still 2");
    /* Turns: a T pointing right at the spawn is one; an S lying flat is the
     * same shape either way up; pointing down is two turns. */
    CHECK(Finesse_Optimal(&b, PIECE_T, 1, 3, Floor(&b, PIECE_T, 1, 3)) == 1, "finesse: a T turned once where it came in: 1");
    CHECK(Finesse_Optimal(&b, PIECE_S, 2, 3, Floor(&b, PIECE_S, 2, 3)) == 0, "finesse: an S lying flat is the same placement either way up: 0");
    CHECK(Finesse_Optimal(&b, PIECE_T, 2, 3, Floor(&b, PIECE_T, 2, 3)) == 2, "finesse: a T pointing down is two turns (no 180 key assumed): 2");
    CHECK(Finesse_Optimal(&b, PIECE_Z, 1, 3, Floor(&b, PIECE_Z, 1, 3)) == 1 && Finesse_Optimal(&b, PIECE_L, 3, 3, Floor(&b, PIECE_L, 3, 3)) == 1,
          "finesse: a Z or an L turned once: 1");
    /* A tuck: an O under a shelf cannot be reached by dropping from the top. */
    Board s;
    Board_Init(&s, 10, 20);
    int total = Board_TotalH(&s);
    for (int x = 0; x < 4; x++) s.cells[total - 3][x] = PIECE_J; /* a shelf two rows up, over the left */
    CHECK(Finesse_Optimal(&s, PIECE_O, 0, -1, total - 2) == -1, "finesse: an O tucked under a shelf has no hint");
    CHECK(Finesse_Optimal(&s, PIECE_O, 0, -1, total - 5) == 1, "finesse: ...while the O on the shelf is one slide");
    /* The spawn blocked: no hint rather than a search. */
    Board full;
    Board_Init(&full, 10, 20);
    for (int x = 0; x < 10; x++) full.cells[BOARD_HIDDEN][x] = PIECE_J;
    CHECK(Finesse_Optimal(&full, PIECE_O, 0, 3, BOARD_HIDDEN - 2) == -1, "finesse: a blocked spawn gives no hint");
    /* Words. */
    char text[24], shortText[12];
    Finesse_Words(2, text, sizeof(text), shortText, sizeof(shortText));
    CHECK(strcmp(text, "2 extra presses") == 0 && strcmp(shortText, "+2") == 0, "finesse: \"2 extra presses\", \"+2\"");
    Finesse_Words(1, text, sizeof(text), shortText, sizeof(shortText));
    CHECK(strcmp(text, "1 extra press") == 0, "finesse: one is singular");
    Finesse_Words(0, text, sizeof(text), shortText, sizeof(shortText));
    CHECK(text[0] == '\0' && shortText[0] == '\0', "finesse: nothing for a clean lock");

    /* The counter, through a frame of play: taps, held slides, turns. */
    PlayDriver d;
    memset(&d, 0, sizeof(d));
    PlayDriver_Start(&d, false, false);
    PracticeRules r = Plain();
    r.noGravity = true;
    r.hints = true;
    Game g = Room(r, 21);
    g.current = PIECE_O; g.next = PIECE_O; g.queue[0] = PIECE_O;
    Tap(&d, &g, true, false);
    Tap(&d, &g, true, false);
    CHECK(g.presses == 2 && g.px == 1, "counter: two taps are two presses");
    PlayIntent drop = Quiet();
    drop.hardDrop = true;
    Frame(&d, &g, drop);
    CHECK(g.lastFinessePresses == 2 && g.lastFinesseOptimal == 2 && g.lastFinesseExtra == 0 && g.presses == 0,
          "counter: two taps for a two-tap placement: clean, and the next piece starts at 0");
    Game_ConsumeFrameFlags(&g);
    /* Three taps left to one in from the wall: a slide and a tap back would do. */
    g.current = PIECE_O;
    Tap(&d, &g, true, false);
    Tap(&d, &g, true, false);
    Tap(&d, &g, true, false);
    Frame(&d, &g, drop);
    CHECK(g.lastFinessePresses == 3 && g.lastFinesseOptimal == 2 && g.lastFinesseExtra == 1, "counter: three taps where two presses would do: 1 extra press");
    HudFx h;
    HudFx_Reset(&h, &g);
    HudFx_React(&h, &g, false);
    const Callout *cs[CALLOUT_MAX];
    int nc = HudFx_Callouts(&h, cs);
    CHECK(nc == 1 && strcmp(cs[0]->text, "1 extra press") == 0 && cs[0]->tint == TINT_DIM && strcmp(cs[0]->shortText, "+1") == 0,
          "counter: the hint is a dim callout, \"1 extra press\"");
    Game_ConsumeFrameFlags(&g);
    /* A held slide is one press however far it goes; a turn is one; a hold,
     * a soft drop and the drop itself are none. */
    g.current = PIECE_O;
    PlayIntent hold = Quiet();
    hold.left = true;
    for (int i = 0; i < 30; i++) Frame(&d, &g, hold); /* half a second: past the delay, repeating */
    Frame(&d, &g, Quiet());
    CHECK(g.px == -1 && g.presses == 1, "counter: Left held to the wall is one press");
    PlayIntent turn = Quiet();
    turn.rotate = true;
    Frame(&d, &g, turn);
    Frame(&d, &g, Quiet());
    CHECK(g.presses == 2, "counter: a turn is a press (even on an O, which has none to make)");
    PlayIntent half = Quiet();
    half.rotateHalf = true;
    Frame(&d, &g, half);
    CHECK(g.presses == 4, "counter: a half turn counts two");
    PlayIntent soft = Quiet();
    soft.softDrop = true;
    Frame(&d, &g, soft);
    PlayIntent hd = Quiet();
    hd.hold = true;
    Frame(&d, &g, hd);
    CHECK(g.presses == 0 && g.hold == PIECE_O, "counter: a soft drop is not a press, and Hold starts the count over for the piece that comes in");
    /* A tap into the wall is still a press. */
    g.current = PIECE_O; g.px = -1; g.presses = 0;
    Tap(&d, &g, true, false);
    CHECK(g.px == -1 && g.presses == 1, "counter: a tap that could not move the piece was still pressed");
    /* The counter runs in every run (it costs nothing); only the room reads it. */
    Game m = Marathon(21);
    PlayDriver e;
    memset(&e, 0, sizeof(e));
    PlayDriver_Start(&e, false, false);
    Tap(&e, &m, false, true);
    CHECK(m.presses == 1, "counter: counted in a plain run too");
    Frame(&e, &m, drop);
    CHECK(m.lastFinesseExtra == -1, "counter: ...which never gets a hint");
    /* READY: a turn pressed while it holds the run is made at GO, and counted then. */
    PlayDriver rd;
    memset(&rd, 0, sizeof(rd));
    PlayDriver_Start(&rd, true, false);
    Game ready = Room(Plain(), 22);
    ready.current = PIECE_T;
    Frame(&rd, &ready, turn);
    CHECK(ready.presses == 0 && rd.cue.turns == 1, "counter: a turn during READY waits");
    for (int i = 0; i < 40; i++) Frame(&rd, &ready, Quiet());
    CHECK(ready.rotation == 1 && ready.presses == 1, "counter: ...and is made and counted at GO");
}

/* ------------------------------------------------------- nothing recorded */

static void test_nothing_recorded(void) {
    Game g = Room(Plain(), 31);
    g.score = 12345;
    g.piecesLocked = 40;
    g.linesCleared = 22;
    g.level = 3;
    g.elapsed = 90.0f;
    RunRecord r = Record_Classify(&g, false);
    CHECK(!r.counts && !r.toBoard && !r.toSave && r.value == 0, "recorded: a practice run with points never counts: no log, no trophy, no board, no save");
    Game m = Marathon(31);
    m.score = 12345;
    m.piecesLocked = 40;
    CHECK(Record_Classify(&m, false).counts, "recorded: ...where the same Marathon would");
    LifeStats life;
    Stats_Clear(&life);
    CHECK(!Stats_Fold(&life, &g, false, "2026-10-20") && life.mode[MODE_MARATHON].runs == 0 && life.since[0] == '\0',
          "recorded: the lifetime stats take nothing from it");
    CHECK(!Hud_PassesBest(g.score, 0, UNIT_POINTS, MODE_MARATHON, true, false), "recorded: with no best there is no NEW BEST to pass");
    CHECK(!PauseMenu_ShouldAsk(&g) && PauseMenu_ShouldAsk(&m), "recorded: Restart and Quit never ask in the room");
    char title[64], body[96], act[24];
    PauseMenu_AskWords(PAUSE_ASK_QUIT, &g, false, "Classic 10x20", title, sizeof(title), body, sizeof(body), act, sizeof(act));
    CHECK(strcmp(body, "Practice: nothing is recorded.") == 0, "recorded: the box's words, were it ever asked");
    /* The results card. */
    g.phase = GS_GAMEOVER;
    g.stats.holds = 4;
    RunFacts f = {false, 0, 0, 0, 0, NULL, "Classic 10x20"};
    ResultsCard c;
    Results_Build(&c, &g, &f);
    CHECK(strcmp(c.title, "GAME OVER") == 0 && strcmp(c.sub, "Practice on Classic 10x20") == 0 && !c.newBest, "card: a practice run ends as a practice run");
    CHECK(strcmp(c.bestLine, "Practice: nothing is recorded") == 0 && c.rankLine[0] == '\0' && c.note[0] == '\0', "card: no best, no rank");
    CHECK(c.bigValue == 12345 && strcmp(c.statLabel[9], "Holds") == 0 && strcmp(c.statValue[9], "4") == 0, "card: the score it reached, and its holds last");
}

/* ------------------------------------------------- the eleventh action */

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

static const char *kOld = "# blockfall settings\nleft=LEFT\nleft2=A\nright=RIGHT\nright2=D\nsoft_drop=DOWN\nsoft_drop2=S\nrotate=UP\n"
                          "rotate2=W\nhard_drop=SPACE\nhold=C\npause=P\nrestart=R\nquit=ESCAPE\nvolume=50\naudio_enabled=1\nsetup_mode=3\n"
                          "setup_preset=1\nsetup_width=10\nsetup_height=20\n";

static bool sDown[512], sPressed[512];
static bool KDown(int code) { return code > 0 && code < 512 && sDown[code]; }
static bool KPressed(int code) { return code > 0 && code < 512 && sPressed[code]; }
static const KeyReader kKb = {KDown, KPressed};
static int Code(const char *name) {
    static const struct { const char *n; int c; } k[] = {
        {"LEFT", 263}, {"RIGHT", 262}, {"UP", 265}, {"DOWN", 264}, {"SPACE", 32}, {"ESCAPE", 256}, {"A", 65}, {"D", 68},
        {"S", 83}, {"W", 87}, {"C", 67}, {"P", 80}, {"R", 82}, {"U", 85}, {"Z", 90}, {"K", 75},
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++) if (strcmp(name, k[i].n) == 0) return k[i].c;
    return 0;
}

static void test_controls(void) {
    Settings s;
    Settings_Default(&s);
    CHECK(ACT_UNDO == CONTROL_ACTIONS - 1 && ACT_UNDO == ACT_ROTATE_180 + 1 && CONTROL_ACTIONS == 11 && strcmp(Controls_ActionLabel(ACT_UNDO), "Undo") == 0,
          "undo: the eleventh action, after Rotate 180");
    CHECK(strcmp(s.undo, "U") == 0 && Controls_IsNone(s.undo2) && Controls_Pad(&s, ACT_UNDO, 0) == PB_RB && Controls_Pad(&s, ACT_UNDO, 1) == PAD_NONE,
          "undo: U and RB as it comes");
    CHECK(Controls_Key(&s, ACT_UNDO, 0) == s.undo && Controls_Key(&s, ACT_UNDO, 1) == s.undo2 && Controls_AreDefault(&s) && Controls_AllDifferent(&s),
          "undo: its slots, and the defaults are still all different");
    CHECK(Controls_Optional(ACT_UNDO) && Controls_Optional(ACT_ROTATE_180) && !Controls_Optional(ACT_HOLD) && !Controls_Optional(ACT_PAUSE),
          "undo: optional, like Rotate 180, and no other action is");
    ControlWords w;
    Controls_Words(&s, &w);
    CHECK(strcmp(w.undo, "U") == 0 && strcmp(w.undoName, "U") == 0 && strcmp(w.padUndo, "RB") == 0, "undo: its words for the card and the room");
    /* Its only key can be cleared, or taken by a swap (it is left with none). */
    CHECK(Controls_ClearKey(&s, ACT_UNDO, 0) && Controls_IsNone(s.undo), "undo: its only key clears");
    CHECK(Controls_ClearPad(&s, ACT_UNDO, 0) && Controls_Pad(&s, ACT_UNDO, 0) == PAD_NONE, "undo: and its only button");
    Controls_Words(&s, &w);
    CHECK(strcmp(w.undo, "-") == 0 && strcmp(w.undoName, "none") == 0 && w.padUndo[0] == '\0', "undo: with none, a dash and none");
    Settings_Default(&s);
    BindResult r = Controls_BindKey(&s, ACT_HOLD, 1, "U");
    CHECK(r.kind == BIND_SWAPPED && strcmp(s.hold2, "U") == 0 && Controls_IsNone(s.undo) && strstr(r.note, "U moved from Undo to Hold"),
          "undo: another action may take U, and Undo is left with none");
    r = Controls_BindPad(&s, ACT_HOLD, 1, PB_RB);
    CHECK(r.kind == BIND_SWAPPED && s.pad[ACT_HOLD][1] == PB_RB && Controls_Pad(&s, ACT_UNDO, 0) == PB_LB, "undo: RB too, and Undo takes the button it had (LB)");
    r = Controls_BindPad(&s, ACT_HARD, 1, PB_LB);
    CHECK(r.kind == BIND_SWAPPED && s.pad[ACT_HARD][1] == PB_LB && Controls_Pad(&s, ACT_UNDO, 0) == PAD_NONE, "undo: taken into an empty slot, Undo is left with none");
    Settings_Default(&s);
    r = Controls_BindKey(&s, ACT_UNDO, 0, "C");
    CHECK(r.kind == BIND_SWAPPED && strcmp(s.undo, "C") == 0 && strcmp(s.hold, "U") == 0, "undo: a swap the other way hands U to Hold");
    Controls_Reset(&s);
    CHECK(Controls_AreDefault(&s), "undo: Reset controls brings U and RB back");
    /* The play keys read it. */
    Settings_Default(&s);
    PlayKeys k;
    Actions_Keys(&s, Code, &k);
    CHECK(k.key[ACT_UNDO][0] == 85 && k.key[ACT_UNDO][1] == 0 && k.pad[ACT_UNDO][0] == PB_RB, "keys: U and RB as play keys");
    Input pad;
    memset(&pad, 0, sizeof(pad));
    pad.focused = true;
    PlayInput pi;
    memset(sDown, 0, sizeof(sDown));
    memset(sPressed, 0, sizeof(sPressed));
    sDown[85] = sPressed[85] = true;
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(pi.undo && !pi.hold && !pi.rotate, "keys: U pressed is Undo alone");
    Actions_Read(&k, &kKb, &pad, true, &pi);
    CHECK(!pi.undo, "keys: not on the run's first frame");
    memset(sDown, 0, sizeof(sDown));
    memset(sPressed, 0, sizeof(sPressed));
    pad.pad[PB_RB] = pad.padDown[PB_RB] = true;
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(pi.undo, "keys: RB on the pad is Undo");
    pad.pad[PB_RB] = false;
    Actions_Read(&k, &kKb, &pad, false, &pi);
    CHECK(!pi.undo, "keys: held, not pressed again");

    /* config.ini: an old file loads unchanged and Undo takes U and RB; one
     * whose keys already use them leaves Undo without. */
    char path[700];
    ConfigPath(path, sizeof(path));
    WriteText(path, kOld);
    CHECK(Settings_Load(&s) && strcmp(s.undo, "U") == 0 && s.pad[ACT_UNDO][0] == PB_RB && strcmp(s.hold, "C") == 0 && s.volumePercent == 50,
          "config: the owner's 2.0.0 file loads unchanged, and Undo is U and RB");
    CHECK(s.practiceLevel == 1 && s.practiceGravity && s.practiceLock && !s.practiceInfiniteHold && s.practiceGarbage == 0 && s.practiceHints,
          "config: the room's options at their defaults");
    char text[2048];
    snprintf(text, sizeof(text), "%shold2=U\npad_hold2=RB\n", kOld);
    WriteText(path, text);
    CHECK(Settings_Load(&s) && Controls_IsNone(s.undo) && s.pad[ACT_UNDO][0] == PAD_NONE && strcmp(s.hold2, "U") == 0 && s.pad[ACT_HOLD][1] == PB_RB &&
              Controls_AllDifferent(&s),
          "config: a file that already gave U and RB to Hold keeps them there; Undo starts with none");
    snprintf(text, sizeof(text), "%sundo=K\nundo2=NONE\npad_undo=LT\npad_undo2=NONE\npractice_level=12\npractice_gravity=0\npractice_lock=0\n"
                                 "practice_hold=1\npractice_garbage=5\npractice_hints=0\n", kOld);
    WriteText(path, text);
    CHECK(Settings_Load(&s) && strcmp(s.undo, "K") == 0 && s.pad[ACT_UNDO][0] == PB_LT && s.practiceLevel == 12 && !s.practiceGravity && !s.practiceLock &&
              s.practiceInfiniteHold && s.practiceGarbage == 5 && !s.practiceHints,
          "config: every new line reads back");
    CHECK(Settings_Save(&s) && Settings_Load(&s) && strcmp(s.undo, "K") == 0 && s.pad[ACT_UNDO][0] == PB_LT && s.practiceLevel == 12 && s.practiceGarbage == 5 &&
              !s.practiceHints && s.practiceInfiniteHold,
          "config: and survives a save and a load");
    snprintf(text, sizeof(text), "%sundo=NONE\npad_undo=NONE\npractice_level=99\npractice_garbage=-3\n", kOld);
    WriteText(path, text);
    CHECK(Settings_Load(&s) && Controls_IsNone(s.undo) && s.pad[ACT_UNDO][0] == PAD_NONE && s.practiceLevel == 20 && s.practiceGarbage == 0,
          "config: an explicit none stays none, and nonsense is clamped");
    s.practiceLevel = 0;
    s.practiceGarbage = 50;
    Setup_Sanitize(&s);
    CHECK(s.practiceLevel == 1 && s.practiceGarbage == PRACTICE_GARBAGE_MAX, "config: Setup_Sanitize keeps the options inside their ranges");

    /* The Settings page: twelve rows on Controls, Undo the last action. */
    Kit_SetScreen(620, 720);
    MenuKeys_Install(NULL);
    Settings_Default(&s);
    ArcadePrefs pr = {70, true, false, false};
    static SetPage p;
    SetView v = {&s, &pr, TAB_CONTROLS, KIT_NONE, true};
    SetPage_Layout(&v, &p);
    CHECK(p.rowCount == 12 && strcmp(p.rows[ACT_UNDO].label, "Undo") == 0, "page: Controls has the eleven actions and Reset");
    const KitItem *cap = Kit_Get(&p.list, SID_CAP_OF(ACT_UNDO, 0)), *padCap = Kit_Get(&p.list, SID_CAP_OF(ACT_UNDO, 2));
    CHECK(cap && strcmp(cap->label, "U") == 0 && padCap && strcmp(padCap->label, "RB") == 0 && strcmp(cap->caption, "Undo key") == 0,
          "page: its caps say U and RB");
    KitRect last = SetPage_CapRect(ACT_UNDO, 3);
    const KitItem *reset = Kit_Get(&p.list, SID_RESET);
    CHECK(last.y + last.h <= SET_RESET_Y && reset && reset->r.y + reset->r.h <= SET_NOTE_Y && SET_FOOT_Y + 2 * 14 <= SET_HINTS_CY - KIT_CAP_H / 2,
          "page: the row fits above Reset controls, which fits above the note line, and two lines of footnote clear the hints");
    CHECK(strstr(p.rows[ACT_UNDO].help, "Practice") != NULL, "page: its help says it is the room's");
}

/* ---------------------------------------------------- the panel and words */

static void test_panel_and_words(void) {
    Game g = Room(Plain(), 41), m = Marathon(41);
    HudSpec s = Layout_SpecFor(&g, true), plain = Layout_SpecFor(&m, true);
    CHECK(s.stat[0] == STAT_SCORE && s.stat[1] == STAT_LEVEL && s.stat[2] == STAT_LINES && s.stat[3] == STAT_UNDO && s.pipsStat == 1,
          "panel: Score, Level with its pips, Lines, and Undos left in Best's place");
    CHECK(plain.stat[3] == STAT_BEST && strcmp(Layout_StatLabel(STAT_UNDO), "Undos left") == 0, "panel: a plain run keeps Best; the stat has its words");
    bool fits = true;
    for (int w = BOARD_MIN_W; w <= BOARD_MAX_W; w += 2)
        for (int h = BOARD_MIN_H; h <= BOARD_MAX_H; h += 3) {
            PlayLayout l = Layout_Play(w, h, &s);
            if (LRect_Empty(l.stat[3]) || !LRect_Inside(l.stat[3], l.panel)) fits = false;
        }
    CHECK(fits, "panel: the stat has its place on every board");
    /* The options' words. */
    Settings st;
    Settings_Default(&st);
    CHECK(PRO_COUNT == 6 && strcmp(Practice_OptionLabel(PRO_LEVEL), "Start level") == 0 && strcmp(Practice_OptionLabel(PRO_GARBAGE), "Starting garbage") == 0 &&
              strcmp(Practice_OptionLabel(PRO_HINTS), "Finesse hints") == 0,
          "words: six options, named");
    char words[32];
    Practice_OptionWords(&st, PRO_LEVEL, words, sizeof(words));
    CHECK(strcmp(words, "1") == 0 && Practice_OptionKind(PRO_LEVEL) == PRO_KIND_NUMBER, "words: the level is a number");
    Practice_OptionWords(&st, PRO_GRAVITY, words, sizeof(words));
    CHECK(strcmp(words, "On") == 0 && Practice_OptionKind(PRO_GRAVITY) == PRO_KIND_TOGGLE && Practice_OptionValue(&st, PRO_GRAVITY) == 1, "words: gravity On");
    Practice_OptionWords(&st, PRO_HOLD, words, sizeof(words));
    CHECK(strcmp(words, "Once") == 0 && Practice_OptionKind(PRO_HOLD) == PRO_KIND_CHOICE && strcmp(Practice_ChoiceWord(PRO_HOLD, 1), "Infinite") == 0 &&
              Practice_ChoiceWord(PRO_HOLD, 2) == NULL,
          "words: Hold Once (once per piece), or Infinite");
    Practice_OptionWords(&st, PRO_GARBAGE, words, sizeof(words));
    CHECK(strcmp(words, "none") == 0, "words: no garbage is none");
    CHECK(Practice_SetOption(&st, PRO_GARBAGE, 1) && !Practice_SetOption(&st, PRO_GARBAGE, 1), "words: a change is a change once");
    Practice_OptionWords(&st, PRO_GARBAGE, words, sizeof(words));
    CHECK(strcmp(words, "1 row") == 0, "words: one row");
    Practice_SetOption(&st, PRO_GARBAGE, 40);
    Practice_OptionWords(&st, PRO_GARBAGE, words, sizeof(words));
    CHECK(strcmp(words, "12 rows") == 0 && st.practiceGarbage == 12, "words: clamped to twelve rows");
    int lo, hi;
    Practice_OptionRange(PRO_LEVEL, &lo, &hi);
    CHECK(lo == 1 && hi == 20, "words: the level's range");
    Practice_SetOption(&st, PRO_LEVEL, 15);
    Practice_SetOption(&st, PRO_GRAVITY, 0);
    Practice_SetOption(&st, PRO_LOCK, 0);
    Practice_SetOption(&st, PRO_HOLD, 1);
    Practice_SetOption(&st, PRO_HINTS, 0);
    PracticeRules r = Practice_Rules(&st);
    CHECK(r.on && r.startLevel == 15 && r.noGravity && r.noLock && r.infiniteHold && r.garbageRows == 12 && !r.hints, "words: the rules follow the options");
    st.setupMode = MODE_SPRINT;
    st.setupPreset = 1;
    GameConfig cfg = Practice_Config(&st);
    CHECK(cfg.mode == MODE_MARATHON && cfg.width == 14 && cfg.height == 20 && cfg.modernScoring, "words: the room is a Marathon on the board set up, whatever the mode");
    char sum[64];
    Practice_Summary(&st, sum, sizeof(sum));
    CHECK(strcmp(sum, "Practice on Wide 14x20") == 0, "words: the summary");
    ControlWords w;
    Settings_Default(&st);
    Controls_Words(&st, &w);
    char undo[128];
    Practice_UndoWords(&w, undo, sizeof(undo));
    CHECK(strcmp(undo, "U or RB takes the last piece back, up to 50 times.") == 0, "words: the undo line names your key and button");
    Controls_ClearPad(&st, ACT_UNDO, 0);
    Controls_Words(&st, &w);
    Practice_UndoWords(&w, undo, sizeof(undo));
    CHECK(strcmp(undo, "U takes the last piece back, up to 50 times.") == 0, "words: the key alone");
    Controls_ClearKey(&st, ACT_UNDO, 0);
    Controls_Words(&st, &w);
    Practice_UndoWords(&w, undo, sizeof(undo));
    CHECK(strstr(undo, "no key or button") != NULL, "words: with none it says where to set one");
    for (int o = 0; o < PRO_COUNT; o++)
        if (!Practice_OptionHelp((PracticeOption)o)[0]) fits = false;
    CHECK(fits, "words: every option has its help");
}

void test_practice_all(void) {
    test_options();
    test_undo();
    test_finesse();
    test_nothing_recorded();
    test_controls();
    test_panel_and_words();
}
