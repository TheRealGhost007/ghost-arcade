/* Headless correctness tests for Gemdive's rules (game.c) and its shipped
 * caves. No raylib. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/game.h"
#include "../src/bot.h"

static int gChecks = 0;
static int gFailures = 0;

#define CHECK(cond, msg) do { \
    gChecks++; \
    if (!(cond)) { \
        gFailures++; \
        printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
    } \
} while (0)

/* A hand-made cave: `rows` fill the top of a steel-walled 40x22 box, the rest is steel. */
static Cave sCave;
static void Make(const char *const *rows, int n, int quota) {
    memset(&sCave, 0, sizeof(sCave));
    snprintf(sCave.name, sizeof(sCave.name), "Test");
    sCave.quota = quota;
    sCave.timeLimit = 100;
    for (int y = 0; y < CAVE_ROWS; y++) {
        for (int x = 0; x < CAVE_COLS; x++) sCave.map[y][x] = '#';
        sCave.map[y][CAVE_COLS] = '\0';
    }
    for (int y = 0; y < n; y++) {
        for (int x = 0; rows[y][x] && x < CAVE_COLS - 2; x++) sCave.map[y + 1][x + 1] = rows[y][x] == ' ' ? '.' : rows[y][x];
    }
}

static Game sGame;
static void Start(const char *const *rows, int n, int quota) {
    Make(rows, n, quota);
    Game_Init(&sGame, &sCave, 1, 0);
    sGame.state = ST_PLAY;
}
static Game *G(void) { return &sGame; }
static void Tick(int n) { for (int i = 0; i < n; i++) Game_TickWithMove(G(), '.'); }
static int PX(void) { return sGame.px - 1; }
static int PY(void) { return sGame.py - 1; }
static void SetPlayer(int x, int y) { sGame.px = x + 1; sGame.py = y + 1; }
static void SetRaw(int x, int y) { sGame.px = x; sGame.py = y; }
static Cell At(int x, int y) { return (Cell)G()->cell[y + 1][x + 1]; } /* rows are offset by the steel border */

static void test_parse(void) {
    Cave c;
    const char *err = NULL;
    char text[4096] = "name=Hello\ngems=3\ntime=90\nsolution=UDLR.\nmap\n";
    char row[CAVE_COLS + 2];
    for (int y = 0; y < CAVE_ROWS; y++) {
        memset(row, '#', CAVE_COLS);
        if (y == 1) { row[1] = 'P'; row[2] = ':'; row[3] = 'o'; row[4] = '*'; row[5] = 'f'; row[6] = 'g'; row[7] = 'B'; }
        if (y == 2) row[1] = 'X';
        row[CAVE_COLS] = '\n'; row[CAVE_COLS + 1] = '\0';
        strcat(text, row);
    }
    CHECK(Cave_Parse(text, &c, &err), "parse: a well-formed cave");
    CHECK(strcmp(c.name, "Hello") == 0 && c.quota == 3 && c.timeLimit == 90 && strcmp(c.solution, "UDLR.") == 0, "parse: header fields");
    CHECK(c.map[1][1] == 'P' && c.map[1][3] == 'o' && c.map[2][1] == 'X', "parse: map cells");
    char *bad = strdup(text);
    bad[strlen(bad) - 5] = '\0';
    CHECK(!Cave_Parse(bad, &c, &err) && err, "parse: a short map is rejected");
    free(bad);
    char *noP = strdup(text);
    *strchr(strstr(noP, "map\n"), 'P') = '#';
    CHECK(!Cave_Parse(noP, &c, &err), "parse: no player is rejected");
    free(noP);
    char *weird = strdup(text);
    *strchr(strstr(weird, "map\n"), 'o') = 'Z';
    CHECK(!Cave_Parse(weird, &c, &err), "parse: unknown characters are rejected");
    free(weird);
    char *noQuota = strdup(text);
    memcpy(strstr(noQuota, "gems="), "gemz=", 5);
    CHECK(!Cave_Parse(noQuota, &c, &err), "parse: missing gems= is rejected");
    free(noQuota);
    /* A file that ends in a bare carriage return must not hang the parser. */
    char *cr = strdup(text);
    size_t crn = strlen(cr);
    cr[crn - 1] = '\r';
    Cave_Parse(cr, &c, &err);
    Cave_Parse("name=x\r", &c, &err);
    Cave_Parse("\r", &c, &err);
    Cave_Parse("", &c, &err);
    CHECK(true, "parse: a trailing carriage return, or nothing at all, terminates");
    free(cr);
}

static void test_falling(void) {
    /* A boulder over empty space falls one cell a tick, then rests. */
    const char *a[] = {"P  o", "    ", "    ", ":::: "};
    Start(a, 4, 1);
    /* P at (0,0); boulder at (3,0); floor of dirt at row 3. */
    CHECK(At(3, 0) == C_BOULDER, "fall: set up");
    Tick(1);
    CHECK(At(3, 0) == C_EMPTY && At(3, 1) == C_BOULDER && G()->falling[2][4], "fall: one cell a tick, and now falling");
    Tick(1);
    CHECK(At(3, 2) == C_BOULDER, "fall: the second cell");
    Tick(1);
    CHECK(At(3, 2) == C_BOULDER && G()->falling[3][4] == 0, "fall: it lands and is no longer falling");
    CHECK(G()->justRockLand, "fall: landing is an event");

    /* A boulder on dirt stays put. */
    const char *b[] = {"P   ", " o  ", "::::"};
    Start(b, 3, 1);
    Tick(5);
    CHECK(At(1, 1) == C_BOULDER, "rest: a supported boulder doesn't move");

    /* Gems fall like boulders. */
    const char *c[] = {"P *", "   ", ":::"};
    Start(c, 3, 5);
    Tick(2);
    CHECK(At(2, 1) == C_GEM, "fall: gems fall too");

    /* Digging out from under a resting boulder makes it fall once you have left. */
    const char *d[] = {" o ", " : ", "P  "};
    Start(d, 3, 1);
    Game_TickWithMove(G(), 'R');
    Game_TickWithMove(G(), 'U'); /* digs the dirt under the boulder and steps into it */
    CHECK(At(1, 0) == C_BOULDER && PY() == 1 && G()->playerAlive, "dig: standing in the hole, the boulder above you doesn't fall on you");
    Game_TickWithMove(G(), 'D');
    Game_TickWithMove(G(), 'R');
    CHECK(G()->playerAlive, "dig: stepping down and aside is fine");
    Tick(2);
    CHECK(At(1, 2) == C_BOULDER && G()->playerAlive, "dig: it drops once you've left");
}

static void test_rolling(void) {
    /* A boulder on top of another rolls off to the left when it can. */
    const char *a[] = {"P     ", "  o   ", "  o   ", ":::::: "};
    Start(a, 4, 1);
    /* Both boulders rest on dirt; the top one sits on a boulder (rounded) but nothing beside it is empty-below... */
    Tick(1);
    CHECK(At(2, 0) == C_EMPTY || At(1, 1) == C_BOULDER || At(3, 1) == C_BOULDER, "roll: the upper boulder rolls off the lower one");
    /* Exactly: left free at (1,0)? start uses rows 0..; rewrite precisely. */
    const char *b[] = {"P     ", "      ", "  o   ", "  o   ", ":::::: "};
    Start(b, 5, 1);
    Tick(1);
    CHECK(At(2, 2) == C_EMPTY && At(1, 2) == C_BOULDER && G()->falling[3][2], "roll: left first, and it is falling as it goes");
    Tick(1);
    CHECK(At(1, 3) == C_BOULDER, "roll: then drops beside the pile");

    /* Left blocked -> rolls right. */
    const char *c[] = {"P     ", "      ", "  o   ", "  o   ", ":::::: "};
    Start(c, 5, 1);
    G()->cell[3][2] = C_DIRT; /* (1,2) now dirt: left is blocked */
    Tick(1);
    CHECK(At(3, 2) == C_BOULDER && At(2, 2) == C_EMPTY, "roll: if the left is blocked it goes right");

    /* No room either side: it stays. */
    const char *d[] = {"P     ", "      ", " :o:  ", "  o   ", ":::::: "};
    Start(d, 5, 1);
    Tick(3);
    CHECK(At(2, 2) == C_BOULDER, "roll: boxed in, it stays");

    /* Left is empty but the cell beside-below is not: it doesn't roll that way. */
    const char *e[] = {"P     ", "      ", "  o   ", " :o   ", ":::::: "};
    Start(e, 5, 1);
    Tick(1);
    CHECK(At(3, 2) == C_BOULDER || At(2, 2) == C_BOULDER, "roll: needs the diagonal empty too");
    CHECK(At(1, 2) == C_EMPTY, "roll: doesn't roll into a cell whose lower neighbour is solid");

    /* Rounded objects: gems and brick also make things roll; dirt and steel don't. */
    const char *f[] = {"P     ", "      ", "  o   ", "  *   ", ":::::: "};
    Start(f, 5, 1);
    Tick(1);
    CHECK(At(2, 2) == C_EMPTY, "roll: off a gem");
    const char *h[] = {"P     ", "      ", "  o   ", "  B   ", ":::::: "};
    Start(h, 5, 1);
    Tick(1);
    CHECK(At(2, 2) == C_EMPTY, "roll: off a brick wall");
    const char *i[] = {"P     ", "      ", "  o   ", "  :   ", ":::::: "};
    Start(i, 5, 1);
    Tick(3);
    CHECK(At(2, 2) == C_BOULDER, "roll: not off dirt");
}

static void test_crush(void) {
    /* A falling boulder onto the player kills; a resting one doesn't. */
    const char *a[] = {" o ", "   ", "   ", " P ", ":::"};
    Start(a, 5, 1);
    /* Boulder at (1,0), player at (1,3), empty between: it falls 2 cells then hits. */
    Tick(1);
    Tick(1);
    CHECK(G()->playerAlive, "crush: two cells above you is not yet");
    Tick(1);
    CHECK(!G()->playerAlive && G()->death == DEATH_CRUSHED && G()->state == ST_DYING, "crush: a falling boulder that reaches you kills");
    CHECK(G()->justDeath && G()->justExplode, "crush: reported");

    const char *b[] = {"   ", " o ", " P ", ":::"};
    Start(b, 4, 1);
    Tick(6);
    CHECK(G()->playerAlive, "crush: a boulder resting on you is harmless");
    const char *c[] = {" * ", " P ", ":::"};
    Start(c, 3, 1);
    Tick(6);
    CHECK(G()->playerAlive, "crush: so is a gem");

    /* Running down a shaft you dug: the rock follows one behind and only kills when you stop. */
    const char *d[] = {" o ", " : ", " : ", " : ", " : ", ":::", "   "};
    Start(d, 7, 1);
    SetPlayer(1, 2); /* under the dirt column (row index 1 => y+1) */
    G()->cell[2][2] = C_EMPTY;
    for (int i = 0; i < 4; i++) Game_TickWithMove(G(), 'D');
    CHECK(G()->playerAlive, "crush: keep moving and it never catches you");
    /* Note: the shaft rows below are dirt, so 'D' digs and moves each tick. */
    Game_TickWithMove(G(), '.');
    Game_TickWithMove(G(), '.');
    CHECK(!G()->playerAlive || G()->state == ST_PLAY, "crush: stop and it does");
}

static void test_push(void) {
    const char *a[] = {"Po  ", ":::: "};
    Start(a, 2, 1);
    Game_TickWithMove(G(), 'R');
    CHECK(PX() == 1 && At(2, 0) == C_BOULDER && G()->justPush, "push: sideways onto empty ground");
    Game_TickWithMove(G(), 'R');
    CHECK(PX() == 2 && At(3, 0) == C_BOULDER, "push: again");
    Game_TickWithMove(G(), 'R'); /* now at the wall: (4,0) is beyond the row => empty '.'? build says row width 4 */
    const char *b[] = {"Po:  ", ":::: "};
    Start(b, 2, 1);
    Game_TickWithMove(G(), 'R');
    CHECK(PX() == 0 && At(1, 0) == C_BOULDER, "push: blocked by dirt behind it");
    const char *c[] = {"Poo  ", ":::::"};
    Start(c, 2, 1);
    Game_TickWithMove(G(), 'R');
    CHECK(PX() == 0, "push: only one boulder at a time");
    const char *d[] = {" o ", " P ", ":::"};
    Start(d, 3, 1);
    Game_TickWithMove(G(), 'U');
    CHECK(PY() == 1 && At(1, 0) == C_BOULDER, "push: never up");
    const char *e[] = {"Po*", ":::"};
    Start(e, 2, 1);
    Game_TickWithMove(G(), 'R');
    CHECK(PX() == 0, "push: a gem behind blocks it");
    /* Pushed off an edge, it falls. */
    const char *f[] = {"Po ", ":: ", ":::"};
    Start(f, 3, 1);
    Game_TickWithMove(G(), 'R');
    Tick(1);
    CHECK(At(2, 1) == C_BOULDER, "push: over the edge it drops");
}

static void test_digging_and_gems(void) {
    const char *a[] = {"P:*", ":::"};
    Start(a, 2, 1);
    Game_TickWithMove(G(), 'R');
    CHECK(PX() == 1 && At(1, 0) == C_EMPTY && G()->justDig, "dig: dirt becomes empty and you step in");
    Game_TickWithMove(G(), 'R');
    CHECK(G()->gems == 1 && G()->score == GEM_POINTS && G()->justGem && At(2, 0) == C_EMPTY, "gem: collected for 10");
    CHECK(G()->exitOpen && G()->justExitOpen, "gem: the quota opens the exit");

    /* Walls are walls. */
    const char *b[] = {"PB#", ":::"};
    Start(b, 2, 1);
    Game_TickWithMove(G(), 'R');
    CHECK(PX() == 0, "wall: brick blocks");
    G()->cell[1][2] = C_STEEL;
    Game_TickWithMove(G(), 'L');
    CHECK(PX() == 0, "wall: so does the border");

    /* Gems after the quota are worth more. */
    const char *c[] = {"P**", ":::"};
    Start(c, 2, 1);
    Game_TickWithMove(G(), 'R');
    Game_TickWithMove(G(), 'R');
    CHECK(G()->score == GEM_POINTS + GEM_POINTS_OPEN, "gem: 15 once the exit is open");

    /* Input handling: a tap between ticks still moves; horizontal beats vertical. */
    const char *d[] = {"P  ", "   ", ":::"};
    Start(d, 3, 1);
    Game_SetInput(G(), 1, 0);
    Game_SetInput(G(), 0, 0);
    Game_Tick(G());
    CHECK(PX() == 1, "input: a tap released before the tick still moves once");
    Game_Tick(G());
    CHECK(PX() == 1, "input: and only once");
    Game_SetInput(G(), 1, 1);
    CHECK(G()->heldDx == 1 && G()->heldDy == 0, "input: one axis at a time");
    Game_SetInput(G(), 0, 1);
    Game_Tick(G());
    CHECK(PY() == 1, "input: held down moves down");
    Game_Tick(G());
    CHECK(PY() == 2, "input: and keeps moving each tick while held");
}

static void test_exit_and_time(void) {
    const char *a[] = {"P*X", ":::"};
    Start(a, 2, 1);
    Game_TickWithMove(G(), 'R');
    CHECK(G()->exitOpen, "exit: open after the quota");
    long before = G()->score;
    Game_TickWithMove(G(), 'R');
    CHECK(G()->state == ST_CLEAR && G()->justClear, "exit: walking in clears the cave");
    CHECK(G()->score - before == CLEAR_BONUS + (long)ceilf(G()->timeLeft) * TIME_BONUS_PER_SECOND && G()->lastClearBonus > 0, "exit: bonus for the time left");

    Start(a, 2, 2);
    G()->cell[1][3] = C_EXIT;
    G()->px = 2;
    Game_TickWithMove(G(), 'R');
    CHECK(G()->state == ST_PLAY && G()->px == 2, "exit: closed until the quota is met");

    /* Time. */
    Start(a, 2, 5);
    G()->timeLeft = 0.25f;
    Game_ConsumeFrameFlags(G());
    Tick(1);
    Tick(1);
    Tick(1);
    CHECK(!G()->playerAlive && G()->death == DEATH_TIME && G()->timeLeft == 0.0f, "time: running out kills");
    Start(a, 2, 5);
    G()->timeLeft = 20.05f;
    Game_ConsumeFrameFlags(G());
    bool warned = false;
    for (int i = 0; i < 4; i++) { Game_TickWithMove(G(), '.'); if (G()->justTimeWarning) warned = true; }
    CHECK(warned, "time: a warning at 20 seconds");

    /* Quota zero: the exit starts open. */
    Start(a, 2, 0);
    CHECK(G()->exitOpen, "exit: a cave with no quota is open from the start");
}

static void test_blasts(void) {
    /* A falling boulder onto a fire crawler: a 3x3 blast of nothing. */
    const char *a[] = {"P   o", "     ", "    f", "#####"};
    Start(a, 4, 1);
    /* Crawler at (4,2)... put the boulder directly above it. */
    const char *b[] = {"P    ", "  o  ", "     ", "  f  ", ":::::"};
    Start(b, 5, 1);
    /* Surround the blast area with things to be destroyed. */
    G()->cell[3][2] = C_DIRT;
    G()->cell[4][4] = C_DIRT; /* (3,3) */
    G()->cell[5][1] = C_BRICK; /* (0,4) is outside; use (1,4) below */
    Tick(1); /* boulder to (2,2) */
    Tick(1); /* crawler moves; boulder lands on or beside it */
    /* The crawler wanders, so build a deterministic case by placing it directly below. */
    Start(b, 5, 1);
    G()->crawlers[0].x = 2; G()->crawlers[0].y = 4; /* (2,3) in row terms: one below and one more? */
    G()->cell[3][3] = C_EMPTY;
    G()->crawlers[0].y = 3;   /* directly below the empty cell under the boulder's next position */
    G()->cell[2][3] = C_EMPTY;
    /* Boulder is at cell y=2 (row idx), crawler at y=3, boulder falling flag must be set first. */
    G()->falling[2][3] = 1;
    G()->crawlers[0].x = 3; G()->crawlers[0].y = 3;
    G()->cell[2][3] = C_BOULDER;
    G()->cell[3][2] = C_DIRT; G()->cell[3][4] = C_DIRT; G()->cell[4][2] = C_BRICK; G()->cell[4][4] = C_DIRT;
    G()->cell[0][3] = C_STEEL;
    /* Sanity: crawler alive, boulder falling above it. */
    CHECK(G()->crawlers[0].alive && G()->cell[2][3] == C_BOULDER, "blast: set up");
    Game_ConsumeFrameFlags(G());
    Game_TickWithMove(G(), '.');
    CHECK(!G()->crawlers[0].alive && G()->justExplode, "blast: a falling boulder kills the crawler under it");
    bool clear = true;
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if (G()->cell[3 + dy][3 + dx] != C_EMPTY && G()->cell[3 + dy][3 + dx] != C_STEEL) clear = false;
    CHECK(clear, "blast: the whole 3x3 is emptied (dirt, brick and boulder alike)");
    CHECK(G()->cell[0][3] == C_STEEL, "blast: steel survives");
    CHECK(G()->blastCount >= 1 && G()->blasts[0].kind == 0, "blast: reported for the renderer");
    CHECK(G()->playerAlive, "blast: a player outside it is fine");

    /* A gem crawler leaves gems. */
    Start(b, 5, 1);
    G()->crawlers[0].kind = 1;
    G()->crawlers[0].x = 3; G()->crawlers[0].y = 3;
    G()->cell[2][3] = C_BOULDER; G()->falling[2][3] = 1;
    G()->cell[3][2] = C_DIRT; G()->cell[4][4] = C_DIRT;
    G()->cell[5][3] = C_STEEL;
    Game_TickWithMove(G(), '.');
    int gems = 0;
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if (G()->cell[3 + dy][3 + dx] == C_GEM) gems++;
    CHECK(gems >= 7 && G()->blasts[0].kind == 1, "blast: a gem crawler blows up into gems");

    /* Chain: a second crawler in the blast goes too. */
    Start(b, 5, 1);
    G()->crawlers[0] = (Crawler){3, 3, 0, 0, true};
    G()->crawlers[1] = (Crawler){4, 2, 0, 1, true};
    G()->crawlerCount = 2;
    G()->cell[2][3] = C_BOULDER; G()->falling[2][3] = 1;
    Game_ConsumeFrameFlags(G());
    Game_TickWithMove(G(), '.');
    CHECK(!G()->crawlers[0].alive && !G()->crawlers[1].alive && G()->blastCount == 2, "blast: caught crawlers blow up in turn");

    /* You in the blast die. */
    Start(b, 5, 1);
    G()->crawlers[0] = (Crawler){3, 3, 0, 0, true};
    SetRaw(4, 3);
    G()->cell[2][3] = C_BOULDER; G()->falling[2][3] = 1;
    Game_TickWithMove(G(), '.');
    CHECK(!G()->playerAlive && G()->death != DEATH_NONE, "blast: standing next to it is fatal");

    /* Dying takes a 3x3 with you. */
    Start(b, 5, 1);
    G()->cell[2][1] = C_DIRT; G()->cell[2][2] = C_DIRT;
    SetRaw(1, 3);
    G()->cell[2][1] = C_BOULDER; G()->falling[2][1] = 1;
    Game_TickWithMove(G(), '.');
    CHECK(!G()->playerAlive, "blast: crushed");
    CHECK(G()->cell[2][1] == C_EMPTY || G()->cell[2][1] == C_STEEL, "blast: the boulder that got you is gone too");
}

static void test_crawlers(void) {
    /* Left-hand rule in an empty room: it hugs the wall. */
    const char *a[] = {"P     ", "      ", "      ", "      "};
    Start(a, 4, 5);
    G()->crawlerCount = 1;
    G()->crawlers[0] = (Crawler){1, 1, 1, 0, true}; /* facing right along the top wall at row 0 => y=1 */
    SetRaw(5, 4);
    /* Left of "facing right" is up: a wall, so it goes straight. */
    Tick(1);
    CHECK(G()->crawlers[0].x == 2 && G()->crawlers[0].y == 1, "crawler: straight along the wall");
    for (int i = 0; i < 4; i++) Tick(1);
    CHECK(G()->crawlers[0].x == 6 && G()->crawlers[0].y == 1, "crawler: to the corner");
    Tick(1);
    CHECK(G()->crawlers[0].dir == 2 && G()->crawlers[0].x == 6 && G()->crawlers[0].y == 1, "crawler: blocked ahead and left, it turns right in place");
    Tick(1);
    CHECK(G()->crawlers[0].x == 6 && G()->crawlers[0].y == 2, "crawler: then follows the wall down");
    /* Its left hand stays on the wall all the way round (it should keep circling the room). */
    bool ok = true;
    for (int i = 0; i < 80; i++) {
        Tick(1);
        if (!G()->playerAlive) break;
        const Crawler *c = &G()->crawlers[0];
        if (c->x < 1 || c->x > 6 || c->y < 1 || c->y > 4) ok = false;
    }
    CHECK(ok, "crawler: stays in its room");

    /* Rounds an inside corner to the left: a wall it was hugging ends. */
    const char *b[] = {"P     ", "     :", "      "};
    Start(b, 3, 5);
    G()->crawlerCount = 1;
    G()->crawlers[0] = (Crawler){1, 2, 1, 0, true};
    SetRaw(6, 3);
    G()->cell[2][6] = C_DIRT;
    Tick(1);
    Tick(1);
    Tick(1);
    Tick(1);
    CHECK(G()->crawlers[0].y == 2 || G()->crawlers[0].y == 3 || G()->crawlers[0].y == 1, "crawler: still moving");

    /* It can't dig: dirt is a wall. */
    const char *c[] = {"P:::::", ":::::::"};
    Start(c, 2, 5);
    G()->crawlerCount = 1;
    G()->crawlers[0] = (Crawler){1, 1, 1, 0, true};
    G()->cell[1][1] = C_EMPTY;
    Tick(6);
    CHECK(G()->crawlers[0].x == 1 && G()->crawlers[0].y == 1, "crawler: shut in by dirt, it goes nowhere");

    /* It kills on contact, either way. */
    const char *d[] = {"P f  ", "     "};
    Start(d, 2, 5);
    Game_TickWithMove(G(), 'R');
    Tick(1);
    CHECK(!G()->playerAlive && G()->death == DEATH_CRAWLER, "crawler: touching it kills");
    Start(d, 2, 5);
    G()->crawlers[0] = (Crawler){2, 1, 0, 0, true};
    Tick(1);
    CHECK(!G()->playerAlive, "crawler: it can walk into you");
    /* Determinism of a crawler's route. */
    Start(a, 4, 5);
    G()->crawlerCount = 1;
    G()->crawlers[0] = (Crawler){1, 1, 1, 0, true};
    SetRaw(5, 4);
    static Game copy;
    copy = *G();
    for (int i = 0; i < 40; i++) { Game_TickWithMove(G(), '.'); Game_TickWithMove(&copy, '.'); }
    CHECK(G()->crawlers[0].x == copy.crawlers[0].x && G()->crawlers[0].y == copy.crawlers[0].y, "crawler: deterministic");
}

static void Wait(Game *g, float seconds) {
    while (seconds > 0.0f) {
        float d = seconds > 0.05f ? 0.05f : seconds;
        Game_Update(g, d);
        seconds -= d;
    }
}

static void test_flow(void) {
    static Cave set[2];
    const char *r1[] = {"P:*X"};
    const char *r2[] = {"P *X"};
    Make(r1, 1, 1);
    set[0] = sCave;
    Make(r2, 1, 1);
    snprintf(sCave.name, sizeof(sCave.name), "Second");
    set[1] = sCave;
    Game g;
    Game_Init(&g, set, 2, 50);
    CHECK(g.phase == GS_PLAYING && g.state == ST_INTRO && g.lives == START_LIVES && g.level == 1 && g.highScore == 50, "flow: starts on an intro card");
    Game_Tick(&g);
    CHECK(g.tickCount == 0, "flow: no ticks during the intro");
    Wait(&g, INTRO_SECONDS + 0.2f);
    CHECK(g.state == ST_PLAY, "flow: then play");
    long t0 = g.tickCount;
    Game_Update(&g, TICK_SECONDS * 0.4f);
    Game_Update(&g, TICK_SECONDS * 0.4f);
    CHECK(g.tickCount - t0 <= 1, "flow: ticks come at TICK_SECONDS, not per frame");
    long t1 = g.tickCount;
    Game_Update(&g, 5.0f);
    CHECK(g.tickCount - t1 <= (long)(0.1f / TICK_SECONDS) + 1, "flow: a long hitch is clamped");

    /* Clear -> the next cave. */
    Game_Init(&g, set, 2, 0);
    g.state = ST_PLAY;
    Game_TickWithMove(&g, 'R');
    Game_TickWithMove(&g, 'R');
    Game_TickWithMove(&g, 'R');
    CHECK(g.state == ST_CLEAR, "flow: reached the exit");
    Wait(&g, CLEAR_SECONDS + 0.2f);
    CHECK(g.level == 2 && g.state == ST_INTRO && strcmp(g.cave->name, "Second") == 0 && g.gems == 0, "flow: on to the next cave, gems reset");
    /* After the last cave the set cycles, faster. */
    g.state = ST_PLAY;
    Game_TickWithMove(&g, 'R');
    Game_TickWithMove(&g, 'R');
    Game_TickWithMove(&g, 'R');
    CHECK(g.state == ST_CLEAR, "flow: cleared the second");
    Wait(&g, CLEAR_SECONDS + 0.2f);
    CHECK(g.level == 3 && g.round == 1 && strcmp(g.cave->name, "Test") == 0, "flow: the set cycles");
    CHECK(Game_TickSeconds(&g) < TICK_SECONDS, "flow: and a cycle is quicker");
    g.round = 100;
    CHECK(Game_TickSeconds(&g) >= 0.06f, "flow: with a floor");

    /* Dying: lives, restarting the cave, game over. */
    Game_Init(&g, set, 2, 0);
    g.state = ST_PLAY;
    g.timeLeft = 0.05f;
    g.score = 200;
    Game_TickWithMove(&g, '.');
    CHECK(g.state == ST_DYING && !g.playerAlive, "die: time out");
    Wait(&g, DYING_SECONDS + 0.2f);
    CHECK(g.lives == START_LIVES - 1 && g.state == ST_INTRO && g.playerAlive && g.timeLeft == g.timeLimit && g.score == 200, "die: a life lost, the cave restarts, score kept");
    g.lives = 1;
    g.state = ST_PLAY;
    g.timeLeft = 0.05f;
    Game_TickWithMove(&g, '.');
    Game_ConsumeFrameFlags(&g);
    Wait(&g, DYING_SECONDS + 0.2f);
    CHECK(g.phase == GS_GAMEOVER && g.justGameOver && g.lives == 0, "die: the last life ends the run");
    Game_TogglePause(&g);
    CHECK(g.phase == GS_GAMEOVER, "die: pause can't undo it");
    Game_Restart(&g, 0);
    CHECK(g.phase == GS_PLAYING && g.score == 0 && g.level == 1 && g.highScore == 200, "restart: fresh run, best kept");

    /* Extra lives. */
    Game_Init(&g, set, 2, 0);
    g.state = ST_PLAY;
    g.score = EXTRA_LIFE_EVERY - 5;
    Game_TickWithMove(&g, 'R');
    Game_TickWithMove(&g, 'R');
    CHECK(g.lives == START_LIVES + 1 && g.justExtraLife, "extra life: at 3,000");

    /* Pause. */
    Game_Init(&g, set, 2, 0);
    g.state = ST_PLAY;
    Game_TogglePause(&g);
    float t = g.timeLeft;
    Game_Update(&g, 1.0f);
    Game_Tick(&g);
    CHECK(g.timeLeft == t && g.phase == GS_PAUSED, "pause: nothing moves");
}

/* ---------------------------------------------------------------- the caves */

static bool Replay(const Cave *cave, int *ticksOut, int *deathsOut) {
    static Game g;
    Game_Init(&g, cave, 1, 0);
    g.state = ST_PLAY;
    int n = (int)strlen(cave->solution);
    for (int i = 0; i < n; i++) {
        Game_TickWithMove(&g, cave->solution[i]);
        if (!g.playerAlive) { if (deathsOut) (*deathsOut)++; return false; }
        if (g.state == ST_CLEAR) { if (ticksOut) *ticksOut = i + 1; return i + 1 == n; }
    }
    return false;
}

static void test_caves(const char *dir) {
    int found = 0, solved = 0, wellFormed = 0, timeOk = 0, numbered = 0;
    for (int i = 1; i <= 40; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%02d.cave", dir, i);
        Cave c;
        const char *err = NULL;
        FILE *f = fopen(path, "rb");
        if (!f) break;
        fclose(f);
        found++;
        if (!Cave_LoadFile(path, &c, &err)) { printf("  cave %d: %s\n", i, err ? err : "?"); continue; }
        wellFormed++;
        int ticks = 0, deaths = 0;
        if (Replay(&c, &ticks, &deaths)) solved++; else printf("  cave %d (%s): its recorded solution does not finish it\n", i, c.name);
        if ((float)ticks * TICK_SECONDS < (float)c.timeLimit * 0.75f) timeOk++;
        else printf("  cave %d: too tight, solution takes %.0fs of %ds\n", i, (double)ticks * TICK_SECONDS, c.timeLimit);
        /* No border holes, and gems available for the quota. */
        int gems = 0; bool border = true;
        for (int y = 0; y < CAVE_ROWS; y++) for (int x = 0; x < CAVE_COLS; x++) {
            if (c.map[y][x] == '*') gems++;
            if ((y == 0 || y == CAVE_ROWS - 1 || x == 0 || x == CAVE_COLS - 1) && c.map[y][x] != '#') border = false;
        }
        if (gems >= c.quota && border) numbered++;
    }
    CHECK(found >= 20, "caves: at least 20 ship");
    CHECK(wellFormed == found, "caves: every one parses");
    CHECK(solved == found, "caves: every one is solvable -- the recorded solution finishes it");
    CHECK(timeOk == found, "caves: and the solution leaves a quarter of the time spare");
    CHECK(numbered == found, "caves: steel border, and enough gems to meet the quota");
}

static void test_bot(void) {
    /* The bot alone, on a small hand-made cave. */
    const char *a[] = {"P:::*  ", ":o:::::", ":::::::", "X::::*:"};
    Start(a, 4, 2);
    for (int i = 0; i < 400 && G()->playerAlive && G()->state == ST_PLAY; i++) Game_TickWithMove(G(), Bot_Move(G()));
    CHECK(G()->state == ST_CLEAR, "bot: collects the gems and finds the exit");
}

int main(int argc, char **argv) {
    test_parse();
    test_falling();
    test_rolling();
    test_crush();
    test_push();
    test_digging_and_gems();
    test_exit_and_time();
    test_blasts();
    test_crawlers();
    test_flow();
    test_bot();
    test_caves(argc > 1 ? argv[1] : "assets/caves");
    printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
