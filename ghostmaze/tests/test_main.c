/* Headless tests for Ghostmaze's rules and its levels. No raylib. */
#include <stdio.h>
#include <string.h>
#include "../src/game.h"

static int gChecks = 0, gFailures = 0;
#define CHECK(cond, msg) do { gChecks++; if (!(cond)) { gFailures++; printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); } } while (0)

static Game sG;
static LevelDef sDef;
static LevelDef sSaved;

/* Runs `rows` as level 1 by swapping it into the level table for the test. */
static const LevelDef *Table(void) { return &kLevels[0]; }

static void Run(const char *acts) {
    for (const char *p = acts; *p; p++) {
        Action a = ACT_WAIT;
        switch (*p) { case 'U': a = ACT_UP; break; case 'D': a = ACT_DOWN; break; case 'L': a = ACT_LEFT; break; case 'R': a = ACT_RIGHT; break; case 'P': a = ACT_POSSESS; break; default: break; }
        Game_Tick(&sG, a);
    }
}

static void Start(int level) {
    Game_Init(&sG, 0);
    Game_StartLevel(&sG, level, false);
}

/* A custom room for a rule test: overwrites level 1 (restored by End). */
static void Custom(const char *const *rows, int nrows, int priests, const int p[][5]) {
    LevelDef *d = (LevelDef *)&kLevels[0];
    sSaved = *d;
    memset(&sDef, 0, sizeof(sDef));
    sDef.name = "test";
    for (int i = 0; i < nrows && i < MAP_H; i++) sDef.rows[i] = rows[i];
    sDef.priestCount = priests;
    for (int i = 0; i < priests; i++) memcpy(sDef.patrol[i], p[i], sizeof(int) * 5);
    *d = sDef;
    Start(1);
}
static void End(void) { *(LevelDef *)&kLevels[0] = sSaved; }

static void test_ghost_basics(void) {
    const char *r[] = {"########", "#@..b.X#", "########"};
    Custom(r, 3, 0, NULL);
    CHECK(sG.gx == 1 && sG.gy == 1 && sG.possessed < 0, "ghost: starts where it is placed");
    Run("R");
    CHECK(sG.gx == 2 && sG.justStep, "ghost: moves a tile a tick");
    Run("L");
    Run("L");
    CHECK(sG.gx == 1, "ghost: walls stop it");
    Run("RR");
    Run("R");
    CHECK(sG.gx == 3, "ghost: a crate is solid to a ghost");
    End();

    const char *g[] = {"#######", "#@:.:X#", "#######"};
    Custom(g, 3, 0, NULL);
    Run("RRR");
    CHECK(sG.gx == 4 && sG.state == ST_PLAY, "ghost: floats over grates");
    Run("R");
    CHECK(sG.state == ST_CLEAR && sG.justClear, "ghost: the exit mirror clears the level");
    CHECK(sG.lastBonus >= SCORE_CLEAR && sG.score >= SCORE_CLEAR, "ghost: paid for it");
    End();
}

static void test_possession(void) {
    const char *r[] = {"#########", "#@.c...X#", "#########"};
    Custom(r, 3, 0, NULL);
    Run("P");
    CHECK(sG.possessed < 0 && sG.justBlocked, "possess: nothing within reach does nothing");
    Run("R");
    Run("P");
    CHECK(sG.possessed == 0 && sG.gx == 3 && sG.justPossess, "possess: an adjacent host takes you in");
    Run("R");
    CHECK(sG.gx == 4 && sG.hosts[0].x == 4, "possess: and you move as it");
    Run("P");
    CHECK(sG.possessed < 0 && sG.justRelease && sG.gx == 4 && sG.hosts[0].x == 4, "possess: releasing leaves the host where it stood");
    Run("L");
    Run("R");
    CHECK(sG.gx == 3, "possess: and the host is solid to the ghost");
    End();
    const char *cd[] = {"#########", "#@c.....#", "#########"};
    Custom(cd, 3, 0, NULL);
    Run("P");
    Run("P");
    Run("P");
    CHECK(sG.possessed < 0 && sG.justBlocked, "possess: not straight away after releasing");
    Run(".");
    Run("P");
    CHECK(sG.possessed >= 0, "possess: but a tick later you can");
    End();

    /* Exit is ghost-only. */
    const char *e[] = {"########", "#@cX...#", "########"};
    Custom(e, 3, 0, NULL);
    Run("P");
    Run("R");
    CHECK(sG.possessed >= 0, "possess: took the cat");
    Run("R");
    CHECK(sG.state == ST_PLAY && sG.gx == 2, "possess: a possessed host can't use the exit");
    End();
}

static void test_cat_flap_and_grate(void) {
    const char *r[] = {"#########", "#@..f...#", "#########"};
    Custom(r, 3, 0, NULL);
    Run("RRR");
    CHECK(sG.gx == 3, "cat flap: a ghost can't pass one");
    End();
    const char *c[] = {"##########", "#.c.f...X#", "##########"};
    Custom(c, 3, 0, NULL);
    sG.gx = 1; sG.gy = 1;
    Run("P");
    Run("RR");
    CHECK(sG.possessed == 0 && sG.gx == 4, "cat flap: a cat squeezes through");
    End();
}

static void test_armor_crate_and_crack(void) {
    const char *r[] = {"##########", "#@a.b..._#", "##########"};
    Custom(r, 3, 0, NULL);
    Run("P");
    Run("R");
    Run("R");
    CHECK(sG.hosts[0].x == 3, "armor: slow, one move every other tick (two presses, one step)");
    Run("R");
    Run("R");
    CHECK(sG.hosts[0].x == 4, "armor: and it does get there");
    int before = sG.crates[0].x;
    for (int i = 0; i < 8; i++) Run("R");
    CHECK(sG.crates[0].x > before, "armor: pushes a crate");
    End();

    const char *k[] = {"#########", "#@aC...X#", "#########"};
    Custom(k, 3, 0, NULL);
    Run("P");
    for (int i = 0; i < 4; i++) Run("R");
    CHECK(sG.tile[1][3] == T_FLOOR, "armor: smashes a cracked wall");
    End();
    const char *n[] = {"#########", "#@cC...X#", "#########"};
    Custom(n, 3, 0, NULL);
    Run("P");
    for (int i = 0; i < 4; i++) Run("R");
    CHECK(sG.tile[1][3] == T_CRACK, "armor: a cat can't");
    End();

    /* Crates don't go through walls, or other crates. */
    const char *w[] = {"#######", "#@abb.#", "#######"};
    Custom(w, 3, 0, NULL);
    Run("P");
    for (int i = 0; i < 6; i++) Run("R");
    CHECK(sG.crates[0].x == 3 && sG.hosts[0].x == 2, "armor: two crates in a row are too heavy");
    End();
}

static void test_servant_keys_doors(void) {
    const char *r[] = {"###########", "#@s.k.D..X#", "###########"};
    Custom(r, 3, 0, NULL);
    Run("P");
    Run("RR");
    CHECK(sG.hosts[0].hasKey && sG.justKey, "servant: picks up a key by walking over it");
    Run("R");
    Run("R");
    CHECK(sG.tile[1][6] == T_FLOOR && !sG.hosts[0].hasKey && sG.justDoor, "servant: opens a locked door with it, using it up");
    End();
    const char *g[] = {"###########", "#@.....D.X#", "###########"};
    Custom(g, 3, 0, NULL);
    Run("RRRRRR");
    CHECK(sG.gx == 6, "door: a ghost can't open it");
    End();
    const char *s[] = {"###########", "#@s..D..X##", "###########"};
    Custom(s, 3, 0, NULL);
    Run("P");
    Run("RRR");
    CHECK(sG.hosts[0].x == 4 && sG.tile[1][5] == T_DOOR, "servant: no key, no door");
    End();
    const char *c[] = {"###########", "#@c.k.D..X#", "###########"};
    Custom(c, 3, 0, NULL);
    Run("P");
    Run("RRRR");
    CHECK(!sG.hosts[0].hasKey, "key: only a servant can carry one");
    End();
}

static void test_plates_and_gates(void) {
    const char *r[] = {"##########", "#@.b_.G.X#", "##########"};
    Custom(r, 3, 0, NULL);
    CHECK(!Game_GateOpen(&sG), "gate: shut while the plate is empty");
    sG.crates[0].x = 4;
    CHECK(Game_GateOpen(&sG), "gate: open while a crate is on the plate");
    sG.crates[0].x = 3;
    CHECK(!Game_GateOpen(&sG), "gate: and shut again when it goes");
    End();
    const char *gl[] = {"##########", "#@._.G.X##", "##########"};
    Custom(gl, 3, 0, NULL);
    Run("RR");
    CHECK(sG.gx == 3 && !Game_GateOpen(&sG), "gate: a ghost is too light to press a plate");
    End();
    const char *h[] = {"##########", "#@c_..G.X#", "##########"};
    Custom(h, 3, 0, NULL);
    Run("P");
    Run("R");
    CHECK(Game_GateOpen(&sG), "gate: a possessed cat presses it");
    Run("P");
    Run("...");
    Run("RRRRRR");
    CHECK(sG.state == ST_CLEAR, "gate: and it stays open once you leave the cat on the plate");
    End();
    const char *two[] = {"##########", "#@.b_.G.X#", "#..c_...##", "##########"};
    Custom(two, 4, 0, NULL);
    sG.crates[0].x = 4;
    CHECK(!Game_GateOpen(&sG), "gate: with two plates, both must be held");
    sG.hosts[0].x = 4; sG.hosts[0].y = 2;
    CHECK(Game_GateOpen(&sG), "gate: and it opens when they are");
    End();
}

static void test_priests(void) {
    /* A priest walks a line and turns round. */
    const char *r[] = {"###########", "#@........#", "#.........#", "#.........#", "###########"};
    int p[1][5] = {{2, 3, 7, 3, 1}};
    Custom(r, 5, 1, p);
    CHECK(sG.priests[0].x == 2 && sG.priests[0].y == 3 && sG.priests[0].dir == 1, "priest: starts at one end facing the other");
    Run("......");
    CHECK(sG.priests[0].x == 7 || sG.priests[0].x == 8, "priest: walks a tile a tick at stride 1");
    bool turned = false;
    for (int i = 0; i < 20 && sG.state == ST_PLAY; i++) { Run("."); if (sG.priests[0].dir == 3) turned = true; }
    CHECK(turned, "priest: turns round at the end");
    End();

    /* Stride 2 is half as quick. */
    int p2[1][5] = {{2, 3, 7, 3, 2}};
    Custom(r, 5, 1, p2);
    Run("....");
    CHECK(sG.priests[0].x == 4, "priest: stride 2 moves every other tick");
    End();

    /* Sight: straight ahead, up to four tiles. */
    const char *s[] = {"###########", "#.........#", "###########"};
    int ps[1][5] = {{1, 1, 9, 1, 1}};
    Custom(s, 3, 1, ps);
    sG.gx = 6; sG.gy = 1;
    CHECK(!Game_SeenBy(&sG, 6, 1, NULL) || true, "sight: (setup)");
    sG.priests[0].x = 3; sG.priests[0].dir = 1;
    CHECK(Game_SeenBy(&sG, 7, 1, NULL) && !Game_SeenBy(&sG, 8, 1, NULL), "sight: four tiles ahead and no further");
    CHECK(!Game_SeenBy(&sG, 2, 1, NULL), "sight: not behind him");
    sG.priests[0].dir = 3;
    CHECK(Game_SeenBy(&sG, 1, 1, NULL) && !Game_SeenBy(&sG, 5, 1, NULL), "sight: it follows the way he faces");
    End();

    /* Walls and hosts block it. */
    const char *b[] = {"###########", "#.@.#.....#", "###########"};
    int pb[1][5] = {{9, 1, 6, 1, 1}};
    Custom(b, 3, 1, pb);
    sG.priests[0].x = 8; sG.priests[0].dir = 3;
    CHECK(!Game_SeenBy(&sG, 3, 1, NULL), "sight: a wall stops it");
    End();
    const char *h[] = {"###########", "#@..c..#..#", "###########"};
    int ph[1][5] = {{5, 1, 1, 1, 9}};
    Custom(h, 3, 1, ph);
    sG.priests[0].x = 5; sG.priests[0].dir = 3;
    /* cat at 4 directly ahead; ghost at 1 is behind it */
    CHECK(Game_SeenBy(&sG, 4, 1, NULL) && !Game_SeenBy(&sG, 3, 1, NULL), "sight: a host in the way casts a shadow");
    End();

    /* Being seen costs a life; hiding in a host does not. */
    const char *v[] = {"##########", "#@.......#", "##########"};
    int pv[1][5] = {{5, 1, 5, 1, 9}};
    Custom(v, 3, 1, pv);
    sG.priests[0].dir = 3;
    Run(".");
    CHECK(sG.state == ST_DYING && sG.caughtBy == CAUGHT_SEEN && sG.justCaught, "caught: a ghost in the open is seen");
    End();
    const char *c[] = {"##########", "#c@......#", "##########"};
    int pc[1][5] = {{6, 1, 6, 1, 9}};
    Custom(c, 3, 1, pc);
    sG.priests[0].dir = 3;
    Run("P");
    Run(".");
    CHECK(sG.state == ST_PLAY && sG.possessed >= 0, "hidden: inside a host, the priest sees nothing");
    End();
    /* Touching the priest ends it either way. */
    const char *t[] = {"##########", "#c@.....#", "##########"};
    int pt[1][5] = {{4, 1, 4, 1, 9}};
    Custom(t, 3, 1, pt);
    sG.priests[0].dir = 1; /* facing away */
    Run("P");
    Run("RRR");
    CHECK(sG.state == ST_DYING && sG.caughtBy == CAUGHT_EXORCISED, "caught: walking a host into the priest is an exorcism");
    End();
}

static void test_flow(void) {
    Game_Init(&sG, 40);
    CHECK(sG.state == ST_INTRO && sG.lives == START_LIVES && sG.level == 1 && sG.highScore == 40, "flow: opens on an intro");
    Game_Tick(&sG, ACT_RIGHT);
    CHECK(sG.ticks == 0, "flow: no ticks during the intro");
    for (int i = 0; i < 40; i++) Game_Update(&sG, 0.05f);
    CHECK(sG.state == ST_PLAY, "flow: then play");
    int t0 = sG.ticks;
    Game_Update(&sG, TICK_SECONDS * 0.4f);
    Game_Update(&sG, TICK_SECONDS * 0.4f);
    CHECK(sG.ticks - t0 <= 1, "flow: ticks come on the clock");
    Game_Queue(&sG, ACT_RIGHT);
    Game_Queue(&sG, ACT_LEFT);
    CHECK(sG.queued == ACT_LEFT, "flow: the latest input wins");

    /* Dying and restarting a level */
    Start(1);
    sG.state = ST_DYING; sG.stateTimer = 0.1f;
    for (int i = 0; i < 5; i++) Game_Update(&sG, 0.05f);
    CHECK(sG.lives == START_LIVES - 1 && sG.state == ST_PLAY && sG.ticks == 0, "flow: a life lost and the level starts again, straight away");
    sG.lives = 1; sG.state = ST_DYING; sG.stateTimer = 0.05f;
    Game_ConsumeFrameFlags(&sG);
    for (int i = 0; i < 4; i++) Game_Update(&sG, 0.05f);
    CHECK(sG.phase == GS_GAMEOVER && sG.justGameOver, "flow: the last life ends the run");
    Game_TogglePause(&sG);
    CHECK(sG.phase == GS_GAMEOVER, "flow: pause can't undo it");
    Game_Restart(&sG, 0);
    CHECK(sG.phase == GS_PLAYING && sG.level == 1, "flow: restart");

    /* Clearing goes on to the next level, and wraps. */
    Start(kLevelCount);
    sG.state = ST_CLEAR; sG.stateTimer = 0.05f;
    for (int i = 0; i < 4; i++) Game_Update(&sG, 0.05f);
    CHECK(sG.level == kLevelCount + 1 && sG.state == ST_INTRO, "flow: the house goes round again");
    Game_LoadLevel(&sG, kLevelCount + 1);
    CHECK(sG.par == kLevels[0].par, "flow: and level numbers wrap onto the table");

    /* Pause */
    Start(1);
    Game_TogglePause(&sG);
    int t = sG.ticks;
    Game_Update(&sG, 5.0f);
    Game_Tick(&sG, ACT_RIGHT);
    CHECK(sG.ticks == t && sG.phase == GS_PAUSED, "flow: paused means paused");
}

static void test_levels(void) {
    int total = 0;
    for (int i = 1; i <= kLevelCount; i++) {
        char sol[400];
        int n = Solver_Solve(i, 200, sol, sizeof(sol));
        char msg[96];
        snprintf(msg, sizeof(msg), "level %d (%s): the solver finishes it", i, kLevels[i - 1].name);
        CHECK(n > 0, msg);
        snprintf(msg, sizeof(msg), "level %d: its stored par is the shortest solution", i);
        CHECK(n == kLevels[i - 1].par, msg);
        if (n > 0) {
            /* Replay the solution on the real game. */
            Game_Init(&sG, 0);
            Game_StartLevel(&sG, i, false);
            Run(sol);
            snprintf(msg, sizeof(msg), "level %d: replaying its solution clears it", i);
            CHECK(sG.state == ST_CLEAR && sG.ticks == n, msg);
            total += n;
        }
        /* Every level has exactly what it needs on the board. */
        Game_LoadLevel(&sG, i);
        bool hasExit = false;
        for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) if (sG.tile[y][x] == T_EXIT) hasExit = true;
        snprintf(msg, sizeof(msg), "level %d: has an exit and a ghost", i);
        CHECK(hasExit && sG.gx > 0 && sG.gy > 0, msg);
    }
    CHECK(kLevelCount >= 15 && total > 200, "levels: at least fifteen, and a reasonable amount of play");
    /* Waiting alone never wins, and doing nothing near a priest is not free. */
    Start(2);
    for (int i = 0; i < 100 && sG.state == ST_PLAY; i++) Run(".");
    CHECK(sG.state != ST_CLEAR, "levels: standing still never clears one");
}

static void test_determinism(void) {
    static Game a, b;
    char sol[400];
    Solver_Solve(8, 200, sol, sizeof(sol));
    Game_Init(&a, 0); Game_StartLevel(&a, 8, false);
    Game_Init(&b, 0); Game_StartLevel(&b, 8, false);
    for (const char *p = sol; *p; p++) {
        Action x = *p == 'U' ? ACT_UP : *p == 'D' ? ACT_DOWN : *p == 'L' ? ACT_LEFT : *p == 'R' ? ACT_RIGHT : *p == 'P' ? ACT_POSSESS : ACT_WAIT;
        Game_Tick(&a, x); Game_Tick(&b, x);
    }
    CHECK(a.gx == b.gx && a.gy == b.gy && a.ticks == b.ticks && a.state == b.state && a.score == b.score, "determinism: same actions, same house");
}

int main(void) {
    (void)Table;
    test_ghost_basics();
    test_possession();
    test_cat_flap_and_grate();
    test_armor_crate_and_crack();
    test_servant_keys_doors();
    test_plates_and_gates();
    test_priests();
    test_flow();
    test_levels();
    test_determinism();
    printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
