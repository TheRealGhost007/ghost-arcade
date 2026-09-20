/* Headless correctness tests for the core game logic (rng/game) and the
 * Ghost Arcade data contract (ghostlink). No raylib dependency, so this
 * builds and runs anywhere `make test` runs, including CI. Tests poke Game
 * fields directly to set up exact scenarios rather than relying on random
 * food placement, which keeps everything deterministic. */
#define _POSIX_C_SOURCE 200809L /* setenv, mkdtemp */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include "../src/game.h"
#include "rng.h"
#include "ghostlink.h"

static int gChecks = 0;
static int gFailures = 0;

#define CHECK(cond, msg) do { \
    gChecks++; \
    if (!(cond)) { \
        gFailures++; \
        printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
    } \
} while (0)

/* ---------- helpers ---------- */

static const int kDX[4] = {0, 1, 0, -1};
static const int kDY[4] = {-1, 0, 1, 0};

/* Moves the food somewhere specific so a scripted path can't eat it by
 * accident. The destination must be empty. */
static void MoveFoodTo(Game *g, int x, int y) {
    g->grid[g->food.y][g->food.x] = CELL_EMPTY;
    g->food = (Cell){(int8_t)x, (int8_t)y};
    g->grid[y][x] = CELL_FOOD;
}

static void MoveBonusTo(Game *g, int x, int y) {
    g->grid[g->bonus.y][g->bonus.x] = CELL_EMPTY;
    g->bonus = (Cell){(int8_t)x, (int8_t)y};
    g->grid[y][x] = CELL_BONUS;
}

/* Puts the food directly in front of the head and steps onto it. */
static void FeedOnce(Game *g) {
    Cell head = Game_Segment(g, 0);
    int nx = (head.x + kDX[g->dir] + GRID_W) % GRID_W;
    int ny = (head.y + kDY[g->dir] + GRID_H) % GRID_H;
    if (g->grid[ny][nx] != CELL_FOOD) MoveFoodTo(g, nx, ny);
    Game_Step(g);
}

/* grid and body must always describe the same snake, and while a game is
 * live there is exactly one food, where g->food says it is. */
static void CheckInvariants(const Game *g) {
    int snakeCells = 0, foodCells = 0, bonusCells = 0;
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            if (g->grid[y][x] == CELL_SNAKE) snakeCells++;
            if (g->grid[y][x] == CELL_FOOD) foodCells++;
            if (g->grid[y][x] == CELL_BONUS) bonusCells++;
        }
    }
    int distinct = 0; /* a phasing snake can lie on itself */
    for (int i = 0; i < g->length; i++) {
        Cell c = Game_Segment(g, i);
        bool dup = false;
        for (int j = 0; j < i; j++) { Cell d = Game_Segment(g, j); if (d.x == c.x && d.y == c.y) dup = true; }
        if (!dup) distinct++;
    }
    CHECK(snakeCells == distinct, "invariant: snake cell count equals distinct segment cells");
    int relicCells = 0;
    for (int y = 0; y < GRID_H; y++) for (int x = 0; x < GRID_W; x++) if (g->grid[y][x] == CELL_RELIC) relicCells++;
    CHECK(relicCells == (g->relicActive ? 1 : 0), "invariant: relic cell present iff relicActive");

    bool allOnSnakeCells = true;
    for (int i = 0; i < g->length; i++) {
        Cell c = Game_Segment(g, i);
        if (g->grid[c.y][c.x] != CELL_SNAKE) allOnSnakeCells = false;
    }
    CHECK(allOnSnakeCells, "invariant: every body segment sits on a CELL_SNAKE cell");

    if (g->phase == GS_PLAYING) {
        CHECK(foodCells == 1, "invariant: exactly one food on a live board");
        CHECK(g->grid[g->food.y][g->food.x] == CELL_FOOD, "invariant: g->food points at the food cell");
    }
    CHECK(bonusCells == (g->bonusActive ? 1 : 0), "invariant: bonus cell present iff bonusActive");
}

/* ---------- rng ---------- */

static void test_rng_deterministic(void) {
    Rng a, b;
    Rng_Seed(&a, 12345);
    Rng_Seed(&b, 12345);
    for (int i = 0; i < 100; i++) {
        CHECK(Rng_Next(&a) == Rng_Next(&b), "rng: same seed must reproduce same sequence");
    }
    Rng c;
    Rng_Seed(&c, 99);
    for (int i = 0; i < 200; i++) {
        CHECK(Rng_Range(&c, 7) < 7, "rng: Range stays below bound");
    }
}

/* ---------- core rules ---------- */

static void test_initial_state(void) {
    for (int m = 0; m < MODE_COUNT; m++) {
        Game g;
        Game_Init(&g, 42, (GameMode)m, 500);
        CHECK(g.phase == GS_PLAYING, "init: starts playing");
        CHECK(g.length == START_LENGTH, "init: start length");
        CHECK(g.dir == DIR_RIGHT, "init: heading right");
        CHECK(g.score == 0 && g.level == 1 && g.foodEaten == 0, "init: counters zeroed");
        CHECK(g.highScore == 500, "init: high score carried in");
        CHECK(g.mode == (GameMode)m, "init: mode stored");
        Cell head = Game_Segment(&g, 0);
        Cell tail = Game_Segment(&g, g.length - 1);
        CHECK(head.x == SPAWN_HEAD_X && head.y == SPAWN_Y, "init: head on the spawn cell");
        CHECK(tail.x == SPAWN_HEAD_X - (START_LENGTH - 1) && tail.y == SPAWN_Y, "init: tail trails to the left");
        CHECK(g.startDelay > 0.0f, "init: start delay armed");
        CheckInvariants(&g);
    }
}

static void test_basic_movement(void) {
    Game g;
    Game_Init(&g, 1, MODE_CLASSIC, 0);
    MoveFoodTo(&g, 0, 0);
    Game_Step(&g);
    Cell head = Game_Segment(&g, 0);
    CHECK(head.x == SPAWN_HEAD_X + 1 && head.y == SPAWN_Y, "move: head advances one cell");
    CHECK(g.length == START_LENGTH, "move: length unchanged without food");
    CHECK(g.grid[SPAWN_Y][SPAWN_HEAD_X - (START_LENGTH - 1)] == CELL_EMPTY, "move: old tail cell vacated");
    CheckInvariants(&g);
}

static void test_turn_queue(void) {
    Game g;
    Game_Init(&g, 1, MODE_CLASSIC, 0);
    MoveFoodTo(&g, 0, 0);

    CHECK(!Game_QueueTurn(&g, DIR_LEFT), "turn: direct reversal rejected");
    CHECK(!Game_QueueTurn(&g, DIR_RIGHT), "turn: repeat of current heading rejected");
    CHECK(g.turnCount == 0, "turn: rejected turns don't occupy the queue");

    CHECK(Game_QueueTurn(&g, DIR_UP), "turn: perpendicular turn accepted");
    CHECK(!Game_QueueTurn(&g, DIR_DOWN), "turn: reversal of the QUEUED heading rejected");
    CHECK(!Game_QueueTurn(&g, DIR_UP), "turn: repeat of the queued heading rejected");
    CHECK(Game_QueueTurn(&g, DIR_LEFT), "turn: second turn buffered relative to the first");
    CHECK(!Game_QueueTurn(&g, DIR_DOWN), "turn: queue is full at TURN_QUEUE_LEN");
    CHECK(g.turnCount == TURN_QUEUE_LEN, "turn: two turns buffered");

    /* The classic quick U-turn: up then left, on consecutive steps. */
    Game_Step(&g);
    Cell h1 = Game_Segment(&g, 0);
    CHECK(g.dir == DIR_UP && h1.x == SPAWN_HEAD_X && h1.y == SPAWN_Y - 1, "turn: first buffered turn applied");
    Game_Step(&g);
    Cell h2 = Game_Segment(&g, 0);
    CHECK(g.dir == DIR_LEFT && h2.x == SPAWN_HEAD_X - 1 && h2.y == SPAWN_Y - 1, "turn: second buffered turn applied next step");
    CHECK(g.turnCount == 0, "turn: queue drained");
    CHECK(g.phase == GS_PLAYING, "turn: buffered U-turn is safe");
    CheckInvariants(&g);
}

static void test_growth_and_scoring(void) {
    Game g;
    Game_Init(&g, 1, MODE_WRAP, 0);
    FeedOnce(&g);
    CHECK(g.justAte, "eat: justAte flag raised");
    CHECK(g.score == 10, "eat: food worth 10 x level at level 1");
    CHECK(g.foodEaten == 1, "eat: foodEaten counts");
    CHECK(g.length == START_LENGTH, "eat: growth lands on the following step");
    CHECK(g.growPending == 1, "eat: one segment pending");
    CheckInvariants(&g);

    MoveFoodTo(&g, 0, 0);
    Game_Step(&g);
    CHECK(g.length == START_LENGTH + 1, "eat: snake grew by one");
    CHECK(g.growPending == 0, "eat: pending growth consumed");
    CheckInvariants(&g);

    Game_ConsumeFrameFlags(&g);
    CHECK(!g.justAte && !g.justLeveledUp && !g.justDied && !g.justAteBonus && !g.justBonusSpawned,
          "flags: consume clears every one-shot flag");
}

static void test_edge_death_classic(void) {
    Game g;
    Game_Init(&g, 1, MODE_CLASSIC, 0);
    MoveFoodTo(&g, 0, 0);
    int stepsToEdge = (GRID_W - 1) - SPAWN_HEAD_X;
    for (int i = 0; i < stepsToEdge; i++) Game_Step(&g);
    CHECK(g.phase == GS_PLAYING, "edge: alive on the last column");
    CHECK(Game_Segment(&g, 0).x == GRID_W - 1, "edge: head on the last column");
    Game_Step(&g);
    CHECK(g.phase == GS_GAMEOVER, "edge: stepping off the board is fatal in classic");
    CHECK(g.deathCause == DEATH_EDGE, "edge: cause recorded");
    CHECK(g.justDied, "edge: justDied raised");
    CHECK(!g.won, "edge: a death is not a win");
    CheckInvariants(&g);

    Cell before = Game_Segment(&g, 0);
    Game_Step(&g);
    Cell after = Game_Segment(&g, 0);
    CHECK(before.x == after.x && before.y == after.y, "edge: no movement after game over");
}

static void test_wrap_mode(void) {
    Game g;
    Game_Init(&g, 1, MODE_WRAP, 0);
    MoveFoodTo(&g, 0, 0);
    int stepsToEdge = (GRID_W - 1) - SPAWN_HEAD_X;
    for (int i = 0; i < stepsToEdge + 1; i++) Game_Step(&g);
    CHECK(g.phase == GS_PLAYING, "wrap: crossing the right edge is safe");
    CHECK(Game_Segment(&g, 0).x == 0 && Game_Segment(&g, 0).y == SPAWN_Y, "wrap: reappears on the left");
    CheckInvariants(&g);

    Game_QueueTurn(&g, DIR_UP);
    for (int i = 0; i < SPAWN_Y + 1; i++) {
        if (g.grid[(Game_Segment(&g, 0).y - 1 + GRID_H) % GRID_H][0] == CELL_FOOD) MoveFoodTo(&g, 5, 5);
        Game_Step(&g);
    }
    CHECK(g.phase == GS_PLAYING, "wrap: crossing the top edge is safe");
    CHECK(Game_Segment(&g, 0).y == GRID_H - 1, "wrap: reappears on the bottom row");
    CheckInvariants(&g);
}

static void test_tail_chase_is_legal(void) {
    /* Length 4 turning in a 2x2 circle: the 4th move enters the cell the
     * tail is leaving on that same step. */
    Game g;
    Game_Init(&g, 1, MODE_CLASSIC, 0);
    MoveFoodTo(&g, 0, 0);
    Game_QueueTurn(&g, DIR_DOWN); Game_Step(&g);
    Game_QueueTurn(&g, DIR_LEFT); Game_Step(&g);
    Cell tail = Game_Segment(&g, g.length - 1);
    Game_QueueTurn(&g, DIR_UP); Game_Step(&g);
    Cell head = Game_Segment(&g, 0);
    CHECK(g.phase == GS_PLAYING, "tail-chase: moving into the vacating tail cell is legal");
    CHECK(head.x == tail.x && head.y == tail.y, "tail-chase: head now occupies the old tail cell");
    CheckInvariants(&g);

    /* Keep circling: must stay alive indefinitely. */
    const Dir circle[4] = {DIR_RIGHT, DIR_DOWN, DIR_LEFT, DIR_UP};
    for (int i = 0; i < 40; i++) {
        Game_QueueTurn(&g, circle[i % 4]);
        Game_Step(&g);
    }
    CHECK(g.phase == GS_PLAYING, "tail-chase: can circle forever at length 4");
    CheckInvariants(&g);
}

static void test_tail_chase_while_growing_is_fatal(void) {
    Game g;
    Game_Init(&g, 1, MODE_CLASSIC, 0);
    MoveFoodTo(&g, 0, 0);
    Game_QueueTurn(&g, DIR_DOWN); Game_Step(&g);
    Game_QueueTurn(&g, DIR_LEFT); Game_Step(&g);
    g.growPending = 1; /* the tail will stay put on the next step */
    Game_QueueTurn(&g, DIR_UP); Game_Step(&g);
    CHECK(g.phase == GS_GAMEOVER, "tail-chase: fatal when the tail isn't moving away");
    CHECK(g.deathCause == DEATH_SELF, "tail-chase: counted as biting yourself");
    CheckInvariants(&g);
}

static void test_self_collision(void) {
    Game g;
    Game_Init(&g, 1, MODE_CLASSIC, 0);
    MoveFoodTo(&g, 0, 0);
    g.growPending = 1;
    Game_Step(&g); /* length 5 */
    CHECK(g.length == START_LENGTH + 1, "self: grew to 5");
    Game_QueueTurn(&g, DIR_DOWN); Game_Step(&g);
    Game_QueueTurn(&g, DIR_LEFT); Game_Step(&g);
    Game_QueueTurn(&g, DIR_UP); Game_Step(&g);
    CHECK(g.phase == GS_GAMEOVER, "self: running into your body is fatal");
    CHECK(g.deathCause == DEATH_SELF, "self: cause recorded");
    CHECK(g.length == START_LENGTH + 1, "self: length unchanged by the fatal step");
    CheckInvariants(&g);
}

static void test_wall_collision_maze(void) {
    Game g;
    Game_Init(&g, 1, MODE_MAZE, 0);
    MoveFoodTo(&g, 0, 0);
    /* Drop a wall two cells ahead; layout 0 leaves the spawn row clear. */
    g.grid[SPAWN_Y][SPAWN_HEAD_X + 2] = CELL_WALL;
    Game_Step(&g);
    CHECK(g.phase == GS_PLAYING, "wall: still alive next to the wall");
    Game_Step(&g);
    CHECK(g.phase == GS_GAMEOVER, "wall: hitting a wall is fatal");
    CHECK(g.deathCause == DEATH_WALL, "wall: cause recorded");
}

static void test_leveling_and_speed(void) {
    Game g;
    Game_Init(&g, 7, MODE_WRAP, 0);
    float startInterval = g.stepInterval;
    for (int i = 0; i < FOOD_PER_LEVEL - 1; i++) FeedOnce(&g);
    CHECK(g.level == 1, "level: still level 1 before the threshold");
    Game_ConsumeFrameFlags(&g);
    FeedOnce(&g);
    CHECK(g.level == 2, "level: level 2 after FOOD_PER_LEVEL food");
    CHECK(g.justLeveledUp, "level: justLeveledUp raised");
    CHECK(g.stepInterval < startInterval, "level: snake speeds up");
    CHECK(g.score == 10L * FOOD_PER_LEVEL, "level: level-1 food all scored at 10");

    long before = g.score;
    if (g.bonusActive) MoveBonusTo(&g, 1, 0);
    FeedOnce(&g);
    CHECK(g.score - before == 20, "level: food worth 20 at level 2");

    float prev = Game_StepIntervalForLevel(1);
    bool monotonic = true;
    for (int lv = 2; lv <= 60; lv++) {
        float cur = Game_StepIntervalForLevel(lv);
        if (cur > prev) monotonic = false;
        prev = cur;
    }
    CHECK(monotonic, "speed: interval never increases with level");
    CHECK(Game_StepIntervalForLevel(1000) >= 0.05f, "speed: floored so the game stays playable");
    CHECK(Game_StepIntervalForLevel(1000) == Game_StepIntervalForLevel(500), "speed: floor is flat");
}

static void test_bonus_lifecycle(void) {
    Game g;
    Game_Init(&g, 3, MODE_WRAP, 0);
    for (int i = 0; i < BONUS_EVERY - 1; i++) FeedOnce(&g);
    CHECK(!g.bonusActive, "bonus: none before the Nth food");
    Game_ConsumeFrameFlags(&g);
    FeedOnce(&g);
    CHECK(g.bonusActive, "bonus: appears after every BONUS_EVERY food");
    CHECK(g.justBonusSpawned, "bonus: spawn flag raised");
    CHECK(g.bonusStepsLeft == BONUS_LIFETIME_STEPS, "bonus: full lifetime on spawn");
    CHECK(g.grid[g.bonus.y][g.bonus.x] == CELL_BONUS, "bonus: present on the grid");
    CheckInvariants(&g);

    /* Park both pickups off the snake's row and let the timer run out. */
    MoveFoodTo(&g, 0, 0);
    MoveBonusTo(&g, 1, 0);
    for (int i = 0; i < BONUS_LIFETIME_STEPS - 1; i++) Game_Step(&g);
    CHECK(g.bonusActive && g.bonusStepsLeft == 1, "bonus: still there one step before expiry");
    Game_Step(&g);
    CHECK(!g.bonusActive, "bonus: expires after its lifetime");
    CHECK(g.grid[0][1] == CELL_EMPTY, "bonus: expired bonus removed from the grid");
    CHECK(g.phase == GS_PLAYING, "bonus: expiry is harmless");
    CheckInvariants(&g);

    /* Eating one: worth level * (20 + 2 * stepsLeft), and no growth. */
    for (int i = 0; i < BONUS_EVERY; i++) {
        FeedOnce(&g);
        if (g.bonusActive && i < BONUS_EVERY - 1) MoveBonusTo(&g, 1, 0);
    }
    CHECK(g.bonusActive, "bonus: second bonus spawned");
    MoveFoodTo(&g, 0, 0);
    Game_Step(&g); /* flush pending growth so the length comparison is clean */
    Cell head = Game_Segment(&g, 0);
    MoveBonusTo(&g, (head.x + 1) % GRID_W, head.y);
    g.bonusStepsLeft = 30;
    long before = g.score;
    int lengthBefore = g.length;
    int level = g.level;
    Game_ConsumeFrameFlags(&g);
    Game_Step(&g);
    CHECK(g.justAteBonus, "bonus: eat flag raised");
    CHECK(!g.bonusActive, "bonus: consumed");
    CHECK(g.score - before == (long)level * (20 + 2 * 30), "bonus: score scales with time left");
    Game_Step(&g);
    CHECK(g.length == lengthBefore, "bonus: gives points without growth");
    CheckInvariants(&g);
}

static void test_food_placement(void) {
    for (uint64_t seed = 1; seed <= 150; seed++) {
        for (int m = 0; m < MODE_COUNT; m++) {
            Game g;
            Game_Init(&g, seed, (GameMode)m, 0);
            bool onWall = (m == MODE_MAZE) && Maze_IsWall(Game_LayoutForStage(1), g.food.x, g.food.y);
            bool onSnake = false;
            for (int i = 0; i < g.length; i++) {
                Cell c = Game_Segment(&g, i);
                if (c.x == g.food.x && c.y == g.food.y) onSnake = true;
            }
            CHECK(!onWall && !onSnake, "food: never spawns on a wall or the snake");
        }
    }
}

static void test_last_empty_cell_and_win(void) {
    /* Board is solid wall except the snake, the food straight ahead, and
     * one free cell: the next food has exactly one place to go. */
    Game g;
    Game_Init(&g, 5, MODE_CLASSIC, 0);
    MoveFoodTo(&g, SPAWN_HEAD_X + 1, SPAWN_Y);
    g.growPending = 1; /* keep the tail planted so stepping doesn't free a cell behind it */
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            if (g.grid[y][x] == CELL_EMPTY) g.grid[y][x] = CELL_WALL;
        }
    }
    g.grid[SPAWN_Y][SPAWN_HEAD_X + 2] = CELL_EMPTY;
    Game_Step(&g);
    CHECK(g.phase == GS_PLAYING, "full: still playing while a cell remains");
    CHECK(g.food.x == SPAWN_HEAD_X + 2 && g.food.y == SPAWN_Y, "full: food takes the only empty cell");

    /* Eat that one too; the tail doesn't move (growth pending), so now
     * there is truly nowhere left. */
    Game_Step(&g);
    CHECK(g.phase == GS_GAMEOVER, "full: game ends when the board is full");
    CHECK(g.won, "full: counted as a win");
    CHECK(g.deathCause == DEATH_NONE, "full: a win has no death cause");
    CHECK(g.justDied, "full: end-of-run flag raised for the caller");
}

static void test_presentation_hints(void) {
    Game g;
    Game_Init(&g, 1, MODE_WRAP, 0);
    MoveFoodTo(&g, 0, 0);
    CHECK(g.stepsOnBoard == 0, "hints: nothing to glide from before the first step");
    bool noBulges = true;
    for (int i = 0; i < MAX_BULGES; i++) if (g.bulgeAge[i] >= 0) noBulges = false;
    CHECK(noBulges && g.eatCount == 0, "hints: clean slate");

    Cell tailBefore = Game_Segment(&g, g.length - 1);
    Game_Step(&g);
    CHECK(g.stepsOnBoard == 1, "hints: step counted");
    CHECK(g.tailMoved && g.prevTail.x == tailBefore.x && g.prevTail.y == tailBefore.y, "hints: vacated tail cell remembered");

    Cell head = Game_Segment(&g, 0);
    FeedOnce(&g);
    CHECK(g.eatCount == 1 && g.stepsSinceEat == 0, "hints: pickup noted on the step it happens");
    CHECK(g.lastEatCell.x == head.x + 1 && g.lastEatCell.y == head.y && !g.lastEatWasBonus, "hints: pickup cell recorded");
    CHECK(g.bulgeAge[0] == 0, "hints: a bulge starts at the head");

    MoveFoodTo(&g, 0, 0);
    Game_Step(&g);
    CHECK(!g.tailMoved, "hints: tail stays put on the growth step");
    CHECK(g.stepsSinceEat == 1 && g.bulgeAge[0] == 1, "hints: bulge moves one segment per step");
    for (int i = 0; i < 10; i++) Game_Step(&g);
    CHECK(g.bulgeAge[0] == -1, "hints: bulge retires when it reaches the tail");

    /* More pickups in flight than slots must not overflow or wedge. */
    for (int i = 0; i < MAX_BULGES + 3; i++) {
        FeedOnce(&g);
        if (g.bonusActive) MoveBonusTo(&g, 1, 0);
    }
    bool sane = true;
    for (int i = 0; i < MAX_BULGES; i++) if (g.bulgeAge[i] < -1 || g.bulgeAge[i] >= g.length) sane = false;
    CHECK(sane, "hints: bulge slots stay in range under rapid eating");
    CHECK(g.eatCount == 1 + MAX_BULGES + 3, "hints: every pickup counted once");

    Game m;
    Game_Init(&m, 11, MODE_MAZE, 0);
    for (int i = 0; i < FOOD_PER_STAGE; i++) {
        FeedOnce(&m);
        if (m.bonusActive) MoveBonusTo(&m, 0, 1);
    }
    CHECK(m.level == 2 && m.stepsOnBoard == 0, "hints: a new maze stage starts with nothing to glide from");
    noBulges = true;
    for (int i = 0; i < MAX_BULGES; i++) if (m.bulgeAge[i] >= 0) noBulges = false;
    CHECK(noBulges, "hints: bulges don't carry onto the rebuilt board");
}

/* ---------- maze ---------- */

static void test_maze_layouts(void) {
    for (int layout = 0; layout < MAZE_LAYOUT_COUNT; layout++) {
        int walls = 0;
        for (int y = 0; y < GRID_H; y++) {
            for (int x = 0; x < GRID_W; x++) {
                if (Maze_IsWall(layout, x, y)) walls++;
            }
        }
        CHECK(walls > 0, "maze: every layout actually has walls");
        CHECK(walls < GRID_W * GRID_H / 4, "maze: layouts leave most of the board open");

        bool laneClear = true;
        for (int x = 0; x <= 14; x++) {
            if (Maze_IsWall(layout, x, SPAWN_Y)) laneClear = false;
        }
        CHECK(laneClear, "maze: spawn lane and run-up are clear");

        /* Flood fill from the spawn cell: every open cell must be reachable,
         * or food could spawn somewhere the snake can never get to. */
        static bool seen[GRID_H][GRID_W];
        static Cell stack[MAX_SNAKE];
        memset(seen, 0, sizeof(seen));
        int top = 0, reached = 0;
        stack[top++] = (Cell){SPAWN_HEAD_X, SPAWN_Y};
        seen[SPAWN_Y][SPAWN_HEAD_X] = true;
        while (top > 0) {
            Cell c = stack[--top];
            reached++;
            for (int d = 0; d < 4; d++) {
                int nx = c.x + kDX[d], ny = c.y + kDY[d];
                if (nx < 0 || nx >= GRID_W || ny < 0 || ny >= GRID_H) continue;
                if (seen[ny][nx] || Maze_IsWall(layout, nx, ny)) continue;
                seen[ny][nx] = true;
                stack[top++] = (Cell){(int8_t)nx, (int8_t)ny};
            }
        }
        CHECK(reached == GRID_W * GRID_H - walls, "maze: every open cell is reachable");
    }

    CHECK(!Maze_IsWall(0, -1, 0) && !Maze_IsWall(0, 0, GRID_H), "maze: out-of-range lookups are safe");
    CHECK(!Maze_IsWall(MAZE_LAYOUT_COUNT, 5, 5), "maze: unknown layout has no walls");
    CHECK(Game_LayoutForStage(1) == 0, "maze: stage 1 uses layout 0");
    CHECK(Game_LayoutForStage(MAZE_LAYOUT_COUNT) == MAZE_LAYOUT_COUNT - 1, "maze: last layout");
    CHECK(Game_LayoutForStage(MAZE_LAYOUT_COUNT + 1) == 0, "maze: layouts cycle");
}

static void test_maze_stage_advance(void) {
    Game g;
    Game_Init(&g, 11, MODE_MAZE, 0);
    for (int i = 0; i < FOOD_PER_STAGE - 1; i++) {
        FeedOnce(&g);
        if (g.bonusActive) MoveBonusTo(&g, 0, 1);
    }
    CHECK(g.level == 1 && g.stageFood == FOOD_PER_STAGE - 1, "stage: one food short of clearing");
    CHECK(g.stepInterval == Game_StepIntervalForLevel(1), "stage: speed is per stage, not per food");
    long scoreBefore = g.score;
    Game_ConsumeFrameFlags(&g);
    FeedOnce(&g);

    CHECK(g.level == 2, "stage: advanced to stage 2");
    CHECK(g.justLeveledUp, "stage: level-up flag raised");
    CHECK(g.score == scoreBefore + 10, "stage: score carries over");
    CHECK(g.foodEaten == FOOD_PER_STAGE, "stage: total food count carries over");
    CHECK(g.stageFood == 0, "stage: per-stage counter reset");
    CHECK(g.length == START_LENGTH && g.growPending == 0, "stage: snake reset to start length");
    CHECK(Game_Segment(&g, 0).x == SPAWN_HEAD_X && Game_Segment(&g, 0).y == SPAWN_Y, "stage: snake back on spawn");
    CHECK(g.dir == DIR_RIGHT && g.turnCount == 0, "stage: heading and turn queue reset");
    CHECK(!g.bonusActive, "stage: bonus cleared");
    CHECK(g.startDelay > 0.0f, "stage: start delay re-armed");
    CHECK(g.stepInterval == Game_StepIntervalForLevel(2), "stage: faster on the next stage");

    bool wallsMatch = true;
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            bool isWall = g.grid[y][x] == CELL_WALL;
            if (isWall != Maze_IsWall(Game_LayoutForStage(2), x, y)) wallsMatch = false;
        }
    }
    CHECK(wallsMatch, "stage: board rebuilt with the next layout");
    CheckInvariants(&g);
}

/* ---------- timing / phases ---------- */

static void test_update_timing(void) {
    Game g;
    Game_Init(&g, 1, MODE_WRAP, 0);
    MoveFoodTo(&g, 0, 0);
    Cell start = Game_Segment(&g, 0);

    Game_Update(&g, START_DELAY_SECONDS * 0.5f);
    CHECK(Game_Segment(&g, 0).x == start.x, "timing: no movement during the start delay");
    while (g.startDelay > 0.0f) Game_Update(&g, 0.1f); /* these calls only burn the delay */
    CHECK(Game_Segment(&g, 0).x == start.x, "timing: the call that ends the delay doesn't step");

    Game_Update(&g, g.stepInterval * 0.6f);
    CHECK(Game_Segment(&g, 0).x == start.x, "timing: no step before a full interval");
    Game_Update(&g, g.stepInterval * 0.6f);
    CHECK(Game_Segment(&g, 0).x == start.x + 1, "timing: one step once the interval elapses");

    Game_Update(&g, 10.0f); /* a huge hitch is clamped, not fast-forwarded */
    int moved = Game_Segment(&g, 0).x - (start.x + 1);
    CHECK(moved >= 1 && moved <= 2, "timing: long frame is capped to a couple of steps");

    Game_TogglePause(&g);
    CHECK(g.phase == GS_PAUSED, "pause: toggles on");
    Cell paused = Game_Segment(&g, 0);
    Game_Update(&g, 1.0f);
    CHECK(Game_Segment(&g, 0).x == paused.x, "pause: no movement while paused");
    CHECK(!Game_QueueTurn(&g, DIR_UP), "pause: turns ignored while paused");
    Game_TogglePause(&g);
    CHECK(g.phase == GS_PLAYING, "pause: toggles off");
}

static void test_restart_and_high_score(void) {
    Game g;
    Game_Init(&g, 1, MODE_MAZE, 25);
    FeedOnce(&g);
    FeedOnce(&g);
    FeedOnce(&g);
    CHECK(g.score == 30, "restart: scored 30");
    MoveFoodTo(&g, 0, 0);
    while (g.phase == GS_PLAYING) Game_Step(&g); /* run into the right edge */
    CHECK(g.highScore == 30, "highscore: updated on death when beaten");

    Game_TogglePause(&g);
    CHECK(g.phase == GS_GAMEOVER, "pause: can't un-game-over by pausing");

    Game_Restart(&g, 2);
    CHECK(g.phase == GS_PLAYING && g.score == 0 && g.length == START_LENGTH, "restart: fresh run");
    CHECK(g.mode == MODE_MAZE, "restart: keeps the mode");
    CHECK(g.highScore == 30, "restart: keeps the high score");
    CHECK(g.deathCause == DEATH_NONE && !g.won, "restart: end-of-run state cleared");
    CheckInvariants(&g);

    Game low;
    Game_Init(&low, 1, MODE_CLASSIC, 1000);
    MoveFoodTo(&low, 0, 0);
    while (low.phase == GS_PLAYING) Game_Step(&low);
    CHECK(low.highScore == 1000, "highscore: a worse run doesn't lower it");
}

/* A random-walk bot across many seeds: whatever happens, grid and body must
 * stay in sync and nothing may index out of bounds (run under `make debug`
 * style sanitizers for the latter). */
static void PlaceRelic(Game *g, int x, int y, RelicType type) {
    g->relic = (Cell){(int8_t)x, (int8_t)y};
    g->grid[y][x] = CELL_RELIC;
    g->relicActive = true;
    g->relicType = type;
    g->relicStepsLeft = RELIC_LIFETIME_STEPS;
}

static void test_relics(void) {
    Game g;
    /* One appears after every RELIC_EVERY-th food. */
    Game_Init(&g, 3, MODE_WRAP, 0);
    for (int i = 0; i < RELIC_EVERY; i++) { g.bonusActive = false; FeedOnce(&g); if (g.phase != GS_PLAYING) break; }
    CHECK(g.relicActive && g.justRelicSpawned && g.grid[g.relic.y][g.relic.x] == CELL_RELIC, "relic: appears after the seventh food");

    /* It times out. */
    Game_Init(&g, 3, MODE_WRAP, 0);
    PlaceRelic(&g, 20, 3, RELIC_SLOW);
    g.relicStepsLeft = 2;
    Game_Step(&g); Game_Step(&g);
    CHECK(!g.relicActive && g.grid[3][20] == CELL_EMPTY, "relic: vanishes if ignored");

    /* Slow stretches the step interval. */
    Game_Init(&g, 3, MODE_WRAP, 0);
    PlaceRelic(&g, g.body[g.headIdx].x + 1, g.body[g.headIdx].y, RELIC_SLOW);
    Game_Step(&g);
    CHECK(g.justRelic && g.slowSteps > 0 && Game_CurrentInterval(&g) > g.stepInterval * 1.5f, "slow: picked up, steps get longer");
    for (int i = 0; i < SLOW_STEPS + 2 && g.phase == GS_PLAYING; i++) Game_Step(&g);
    CHECK(g.slowSteps == 0 && Game_CurrentInterval(&g) == g.stepInterval, "slow: wears off");

    /* Surge doubles points. */
    Game_Init(&g, 3, MODE_WRAP, 0);
    g.surgeSteps = 10;
    long before = g.score;
    FeedOnce(&g);
    CHECK(g.score - before == 10L * g.level * SURGE_MULT || g.score - before == 20L, "surge: food scores double");

    /* Phase: pass through your own body; wall still kills; ending inside yourself is fatal. */
    Game_Init(&g, 3, MODE_WRAP, 0);
    MoveFoodTo(&g, 0, 0);
    g.growPending = 3;
    for (int i = 0; i < 3; i++) Game_Step(&g);
    g.phaseSteps = 10;
    Game_QueueTurn(&g, DIR_DOWN); Game_Step(&g);
    Game_QueueTurn(&g, DIR_LEFT); Game_Step(&g);
    Game_QueueTurn(&g, DIR_UP); Game_Step(&g);
    CHECK(g.phase == GS_PLAYING, "phase: the snake slides through its own body");
    CheckInvariants(&g);
    for (int i = 0; i < 6 && g.phase == GS_PLAYING; i++) { Game_Step(&g); CheckInvariants(&g); }
    CHECK(g.phase == GS_PLAYING, "phase: and keeps its grid straight while overlapping");

    Game_Init(&g, 3, MODE_CLASSIC, 0);
    MoveFoodTo(&g, 0, 0);
    g.growPending = 3;
    for (int i = 0; i < 3; i++) Game_Step(&g);
    g.phaseSteps = 3;
    Game_QueueTurn(&g, DIR_DOWN); Game_Step(&g);
    Game_QueueTurn(&g, DIR_LEFT); Game_Step(&g);
    Game_QueueTurn(&g, DIR_UP); Game_Step(&g); /* head now on its own body, phase has ended */
    CHECK(g.phase == GS_GAMEOVER && g.deathCause == DEATH_SELF, "phase: running out while inside yourself ends the run");

    Game_Init(&g, 3, MODE_CLASSIC, 0);
    g.phaseSteps = 100;
    for (int i = 0; i < 30 && g.phase == GS_PLAYING; i++) Game_Step(&g);
    CHECK(g.phase == GS_GAMEOVER && g.deathCause == DEATH_EDGE, "phase: edges are still fatal");
}

static void test_fuzz_invariants(void) {
    for (uint64_t seed = 1; seed <= 24; seed++) {
        Game g;
        Game_Init(&g, seed, (GameMode)(seed % MODE_COUNT), 0);
        Rng bot;
        Rng_Seed(&bot, seed * 7919);
        int steps = 0;
        while (g.phase == GS_PLAYING && steps < 1500) {
            if (steps % 200 == 50) g.phaseSteps = 12;
            if (Rng_Range(&bot, 3) == 0) Game_QueueTurn(&g, (Dir)Rng_Range(&bot, 4));
            Game_Step(&g);
            steps++;
            if (steps % 25 == 0) CheckInvariants(&g);
        }
        CheckInvariants(&g);
        CHECK(g.length >= START_LENGTH && g.length <= MAX_SNAKE, "fuzz: length stays in range");
    }
}

/* ---------- ghostlink ---------- */

static char gTmpRoot[512];

static void WriteFile(const char *relPath, const char *content) {
    char path[700];
    snprintf(path, sizeof(path), "%s/%s", gTmpRoot, relPath);
    FILE *f = fopen(path, "w");
    if (!f) { CHECK(false, "ghostlink: test fixture write failed"); return; }
    fputs(content, f);
    fclose(f);
}

static void test_ghostlink_username(void) {
    char name[GHOSTLINK_NAME_LEN];

    CHECK(!GhostLink_GetUsername("Coilrush", name, sizeof(name)), "profile: no launcher dir -> not found");
    CHECK(strcmp(name, GHOSTLINK_DEFAULT_NAME) == 0, "profile: falls back to PLAYER");

    char cmd[600];
    snprintf(cmd, sizeof(cmd), "%s/ghost-launcher", gTmpRoot);
    CHECK(mkdir(cmd, 0755) == 0, "profile: fixture dir created");

    WriteFile("ghost-launcher/profiles.txt",
              "# Ghost Launcher per-game profiles: game_name|username\n"
              "Blockfall|blockmaster\n"
              "Coilrush|TheRealGhost\n");
    CHECK(GhostLink_GetUsername("Coilrush", name, sizeof(name)), "profile: entry found");
    CHECK(strcmp(name, "TheRealGhost") == 0, "profile: right game's name returned");
    CHECK(GhostLink_GetUsername("Blockfall", name, sizeof(name)) && strcmp(name, "blockmaster") == 0,
          "profile: lookup is per game");
    CHECK(!GhostLink_GetUsername("Nope", name, sizeof(name)) && strcmp(name, GHOSTLINK_DEFAULT_NAME) == 0,
          "profile: unknown game -> PLAYER");

    WriteFile("ghost-launcher/profiles.txt", "Coilrush|\n");
    CHECK(!GhostLink_GetUsername("Coilrush", name, sizeof(name)) && strcmp(name, GHOSTLINK_DEFAULT_NAME) == 0,
          "profile: empty name -> PLAYER");

    WriteFile("ghost-launcher/profiles.txt", "Coilrush|a|b\n");
    CHECK(GhostLink_GetUsername("Coilrush", name, sizeof(name)) && strcmp(name, "a/b") == 0,
          "profile: separator inside a name is neutralised");

    WriteFile("ghost-launcher/profiles.txt", "Blockfall|blockmaster\n" GHOSTLINK_ARCADE_KEY "|Ghost\n");
    CHECK(GhostLink_GetUsername("Coilrush", name, sizeof(name)) && strcmp(name, "Ghost") == 0,
          "profile: no entry of its own -> the arcade-wide name");
    CHECK(GhostLink_GetUsername("Blockfall", name, sizeof(name)) && strcmp(name, "blockmaster") == 0,
          "profile: an override still beats the arcade name");
    WriteFile("ghost-launcher/profiles.txt", GHOSTLINK_ARCADE_KEY "|Ghost\nCoilrush|Snek\n");
    CHECK(GhostLink_GetUsername("Coilrush", name, sizeof(name)) && strcmp(name, "Snek") == 0,
          "profile: override wins even when it comes after the arcade row");
    WriteFile("ghost-launcher/profiles.txt", "Coilrush|\n" GHOSTLINK_ARCADE_KEY "|Ghost\n");
    CHECK(GhostLink_GetUsername("Coilrush", name, sizeof(name)) && strcmp(name, "Ghost") == 0,
          "profile: blank override falls back to the arcade name");

    char tiny[4];
    WriteFile("ghost-launcher/profiles.txt", "Coilrush|LongUserName\n");
    GhostLink_GetUsername("Coilrush", tiny, sizeof(tiny));
    CHECK(strlen(tiny) == 3, "profile: output truncated safely to the buffer");
}

static void test_ghostlink_scores(void) {
    GhostScore out[GHOSTLINK_TOP_N + 4];

    CHECK(GhostLink_LoadScores("coilrush", "classic", out, GHOSTLINK_TOP_N) == 0, "scores: empty before any run");
    CHECK(GhostLink_SubmitScore("coilrush", "classic", "ghost", 0, "2026-09-19") == 0, "scores: zero score ignored");
    CHECK(GhostLink_SubmitScore("coilrush", "classic", "ghost", -5, "2026-09-19") == 0, "scores: negative score ignored");

    CHECK(GhostLink_SubmitScore("coilrush", "classic", "ghost", 100, "2026-09-19") == 1, "scores: first run is rank 1");
    CHECK(GhostLink_SubmitScore("coilrush", "classic", "ghost", 300, "2026-09-19") == 1, "scores: better run takes rank 1");
    CHECK(GhostLink_SubmitScore("coilrush", "classic", "anna", 200, "2026-09-20") == 2, "scores: middle run slots in at 2");
    CHECK(GhostLink_SubmitScore("coilrush", "classic", "late", 200, "2026-09-21") == 3, "scores: a tie ranks below the earlier run");
    CHECK(GhostLink_SubmitScore("coilrush", "maze", "ghost", 50, "2026-09-19") == 1, "scores: modes rank independently");

    int n = GhostLink_LoadScores("coilrush", "classic", out, GHOSTLINK_TOP_N);
    CHECK(n == 4, "scores: four classic rows persisted");
    CHECK(out[0].score == 300 && out[1].score == 200 && out[2].score == 200 && out[3].score == 100,
          "scores: loaded best-first");
    CHECK(strcmp(out[1].username, "anna") == 0 && strcmp(out[2].username, "late") == 0, "scores: tie order is stable");
    CHECK(strcmp(out[1].date, "2026-09-20") == 0, "scores: date round-trips");
    CHECK(strcmp(out[0].mode, "classic") == 0, "scores: mode round-trips");
    CHECK(GhostLink_LoadScores("coilrush", "maze", out, GHOSTLINK_TOP_N) == 1, "scores: maze table separate");
    CHECK(GhostLink_LoadScores("coilrush", "wrap", out, GHOSTLINK_TOP_N) == 0, "scores: untouched mode is empty");
    CHECK(GhostLink_LoadScores("otherslug", "classic", out, GHOSTLINK_TOP_N) == 0, "scores: files are per game slug");
    CHECK(GhostLink_LoadScores("coilrush", "classic", out, 2) == 2, "scores: maxOut respected");

    /* Fill past the cap: only the top N survive, and a run too low to make
     * the table reports rank 0. */
    for (int i = 0; i < GHOSTLINK_TOP_N + 3; i++) {
        GhostLink_SubmitScore("coilrush", "wrap", "filler", 1000 + i * 10, "2026-09-19");
    }
    n = GhostLink_LoadScores("coilrush", "wrap", out, GHOSTLINK_TOP_N + 4);
    CHECK(n == GHOSTLINK_TOP_N, "scores: table trimmed to the top N");
    CHECK(out[0].score == 1000 + (GHOSTLINK_TOP_N + 2) * 10, "scores: best kept at the top");
    CHECK(out[GHOSTLINK_TOP_N - 1].score == 1030, "scores: lowest three dropped");
    CHECK(GhostLink_SubmitScore("coilrush", "wrap", "weak", 5, "2026-09-19") == 0, "scores: below the cut -> rank 0");
    CHECK(GhostLink_LoadScores("coilrush", "wrap", out, GHOSTLINK_TOP_N + 4) == GHOSTLINK_TOP_N, "scores: still N rows after a miss");
    CHECK(GhostLink_LoadScores("coilrush", "classic", out, GHOSTLINK_TOP_N) == 4, "scores: trimming one mode leaves others alone");

    /* Hostile / sloppy input. */
    CHECK(GhostLink_SubmitScore("coilrush", "maze", "pipe|name", 75, "2026-09-19") == 1, "scores: name with separator accepted");
    n = GhostLink_LoadScores("coilrush", "maze", out, GHOSTLINK_TOP_N);
    CHECK(n == 2 && strcmp(out[0].username, "pipe/name") == 0, "scores: separator neutralised so the row still parses");
    CHECK(GhostLink_SubmitScore("coilrush", "maze", "", 60, "2026-09-19") == 2, "scores: empty name accepted");
    n = GhostLink_LoadScores("coilrush", "maze", out, GHOSTLINK_TOP_N);
    CHECK(n == 3 && strcmp(out[1].username, GHOSTLINK_DEFAULT_NAME) == 0, "scores: empty name stored as PLAYER");

    WriteFile("ghost-launcher/scores/junk.txt",
              "# comment\n"
              "\n"
              "classic|ok|40|2026-01-01\n"
              "not a row at all\n"
              "classic|missing-date|99\n"
              "classic|zero|0|2026-01-01\n"
              "classic|best|70|2026-01-02\n");
    n = GhostLink_LoadScores("junk", "classic", out, GHOSTLINK_TOP_N);
    CHECK(n == 2, "scores: malformed and zero rows skipped");
    CHECK(out[0].score == 70 && out[1].score == 40, "scores: hand-edited file is sorted on load");
}

static void test_ghostlink_many_modes(void) {
    /* A file with far more rows than the old 128 cap, in many modes: recording
     * one more run must not lose the modes that sort last. */
    GhostScore out[GHOSTLINK_TOP_N];
    char mode[GHOSTLINK_MODE_LEN];
    for (int m = 0; m < 30; m++) {
        snprintf(mode, sizeof(mode), "size-%02d", m);
        for (int k = 0; k < 6; k++) GhostLink_SubmitScore("manymodes", mode, "ghost", 100 + k, "2026-09-20");
    }
    CHECK(GhostLink_SubmitScore("manymodes", "aaa-first", "ghost", 5, "2026-09-20") == 1, "many modes: a new board still records");
    CHECK(GhostLink_LoadScores("manymodes", "size-29", out, GHOSTLINK_TOP_N) == 6, "many modes: the last-sorting board survives (180+ rows)");
    CHECK(GhostLink_LoadScores("manymodes", "size-00", out, GHOSTLINK_TOP_N) == 6, "many modes: and so does the first");

    /* Daily boards: today's is kept, an ancient one is pruned, ordinary modes never are. */
    GhostLink_SubmitScore("dailies", "daily-20200101", "ghost", 50, "2020-01-01");
    GhostLink_SubmitScore("dailies", "arcade", "ghost", 70, "2020-01-01");
    time_t now = time(NULL);
    struct tm tmNow;
    localtime_r(&now, &tmNow);
    snprintf(mode, sizeof(mode), "daily-%04d%02d%02d", tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday);
    GhostLink_SubmitScore("dailies", mode, "ghost", 60, "2026-09-20");
    CHECK(GhostLink_LoadScores("dailies", mode, out, GHOSTLINK_TOP_N) == 1, "dailies: today's board is kept");
    CHECK(GhostLink_LoadScores("dailies", "daily-20200101", out, GHOSTLINK_TOP_N) == 0, "dailies: a years-old daily board is pruned");
    CHECK(GhostLink_LoadScores("dailies", "arcade", out, GHOSTLINK_TOP_N) == 1, "dailies: normal boards are never pruned, however old");
}

static void test_ghostlink_global_and_sync(void) {
    GhostScore out[GHOSTLINK_TOP_N];
    CHECK(GhostLink_LoadGlobalScores("coilrush", "classic", out, GHOSTLINK_TOP_N) == 0,
          "global: empty before any sync (local scores don't leak in)");

    char dir[700];
    snprintf(dir, sizeof(dir), "%s/ghost-launcher/scores/global", gTmpRoot);
    CHECK(mkdir(dir, 0755) == 0, "global: fixture dir created");
    WriteFile("ghost-launcher/scores/global/coilrush.txt",
              "classic|worldbest|9000|2026-09-01\n"
              "classic|second|4000|2026-09-02\n"
              "maze|mazer|700|2026-09-03\n");
    int n = GhostLink_LoadGlobalScores("coilrush", "classic", out, GHOSTLINK_TOP_N);
    CHECK(n == 2 && out[0].score == 9000 && strcmp(out[0].username, "worldbest") == 0,
          "global: reads the cache ghost-sync writes");
    CHECK(GhostLink_LoadGlobalScores("coilrush", "maze", out, GHOSTLINK_TOP_N) == 1, "global: per mode");
    n = GhostLink_LoadScores("coilrush", "classic", out, GHOSTLINK_TOP_N);
    CHECK(n == 4 && out[0].score == 300, "global: the local table is untouched by the cache");

    /* With no helper anywhere on PATH or under HOME, triggering a sync must
     * return promptly and harmlessly (the game may be installed alone). */
    setenv("PATH", gTmpRoot, 1);
    setenv("HOME", gTmpRoot, 1);
    GhostLink_TriggerSync();
    GhostLink_TriggerSync();
    CHECK(true, "sync: trigger with no helper installed is a silent no-op");
}

static void RemoveTree(void) {
    /* Fixed, known set of files -- no recursive delete needed. */
    const char *files[] = {
        "ghost-launcher/scores/global/coilrush.txt",
        "ghost-launcher/scores/coilrush.txt", "ghost-launcher/scores/junk.txt",
        "ghost-launcher/profiles.txt",
    };
    char path[700];
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        snprintf(path, sizeof(path), "%s/%s", gTmpRoot, files[i]);
        unlink(path);
    }
    snprintf(path, sizeof(path), "%s/ghost-launcher/scores/global", gTmpRoot); rmdir(path);
    snprintf(path, sizeof(path), "%s/ghost-launcher/scores", gTmpRoot); rmdir(path);
    snprintf(path, sizeof(path), "%s/ghost-launcher", gTmpRoot); rmdir(path);
    rmdir(gTmpRoot);
}

int main(void) {
    test_rng_deterministic();
    test_initial_state();
    test_basic_movement();
    test_turn_queue();
    test_growth_and_scoring();
    test_edge_death_classic();
    test_wrap_mode();
    test_tail_chase_is_legal();
    test_tail_chase_while_growing_is_fatal();
    test_self_collision();
    test_wall_collision_maze();
    test_leveling_and_speed();
    test_bonus_lifecycle();
    test_food_placement();
    test_last_empty_cell_and_win();
    test_presentation_hints();
    test_maze_layouts();
    test_maze_stage_advance();
    test_update_timing();
    test_restart_and_high_score();
    test_relics();
    test_fuzz_invariants();

    /* ghostlink tests run against a throwaway XDG_DATA_HOME so they can
     * never touch the real launcher data. */
    const char *tmpBase = getenv("TMPDIR");
    snprintf(gTmpRoot, sizeof(gTmpRoot), "%s/coilrush-test-XXXXXX", (tmpBase && tmpBase[0]) ? tmpBase : "/tmp");
    if (mkdtemp(gTmpRoot) == NULL) {
        printf("FAIL: could not create temp dir for ghostlink tests\n");
        gFailures++;
    } else {
        setenv("XDG_DATA_HOME", gTmpRoot, 1);
        test_ghostlink_username();
        test_ghostlink_scores();
        test_ghostlink_many_modes();
        test_ghostlink_global_and_sync(); /* last: it clobbers PATH and HOME */
        RemoveTree();
    }

    printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
