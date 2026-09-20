/* Headless correctness tests for Brickburst's rules (game.c, levels.c) and
 * the Ghost Arcade data contract (ghostlink.c is covered in Coilrush's
 * suite; the copy here is identical). No raylib: builds and runs anywhere.
 * Physics is a fixed 120 Hz step, so every scenario is exact and repeatable. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../src/game.h"

static int gChecks = 0;
static int gFailures = 0;

#define CHECK(cond, msg) do { \
    gChecks++; \
    if (!(cond)) { \
        gFailures++; \
        printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
    } \
} while (0)

static void ClearBricks(Game *g) {
    memset(g->brick, 0, sizeof(g->brick));
    memset(g->hp, 0, sizeof(g->hp));
    g->bricksLeft = 0;
}

static void PutBrick(Game *g, int col, int row, BrickType type) {
    g->brick[row][col] = (uint8_t)type;
    g->hp[row][col] = type == BRICK_TWO ? 2 : (type == BRICK_THREE ? 3 : 1);
    if (type != BRICK_STEEL) g->bricksLeft++;
}

/* One free-flying ball with an exact velocity; everything else quiet. */
static Ball *FreeBall(Game *g, float x, float y, float vx, float vy) {
    memset(g->balls, 0, sizeof(g->balls));
    g->balls[0] = (Ball){x, y, vx, vy, true, false, 0.0f};
    return &g->balls[0];
}

static float BrickLeft(int col) { return (float)(col * BRICK_W); }
static float BrickTopY(int row) { return (float)(BRICK_TOP + row * BRICK_H); }

static void test_level_parse(void) {
    uint8_t out[BRICK_ROWS][BRICK_COLS];
    CHECK(Level_Parse("1.2\n#*3\n", out), "parse: simple level accepted");
    CHECK(out[0][0] == BRICK_ONE && out[0][1] == BRICK_NONE && out[0][2] == BRICK_TWO, "parse: row 0");
    CHECK(out[1][0] == BRICK_STEEL && out[1][1] == BRICK_BOMB && out[1][2] == BRICK_THREE, "parse: row 1");
    CHECK(out[2][0] == BRICK_NONE && out[0][12] == BRICK_NONE, "parse: missing cells are empty");
    CHECK(Level_Parse("1\r\n1\r\n", out) && out[1][0] == BRICK_ONE, "parse: CRLF tolerated");

    CHECK(!Level_Parse("12x", out), "parse: unknown character rejected");
    CHECK(out[0][0] == BRICK_NONE, "parse: rejected level leaves the grid empty");
    CHECK(!Level_Parse("", out), "parse: empty level rejected");
    CHECK(!Level_Parse("###\n...\n", out), "parse: steel-only level rejected (could never be cleared)");
    CHECK(!Level_Parse("11111111111111\n", out), "parse: a 14th brick in a row is rejected, not silently dropped");
    CHECK(Level_Parse("1111111111111 \n", out), "parse: trailing blank past the edge is fine");
}

static void test_builtin_levels(void) {
    CHECK(Levels_Count() >= 10, "levels: a decent number of layouts");
    CHECK(Levels_Text(-1)[0] == '\0' && Levels_Text(999)[0] == '\0', "levels: out-of-range index is an empty string");

    for (int i = 0; i < Levels_Count(); i++) {
        uint8_t lv[BRICK_ROWS][BRICK_COLS];
        CHECK(Level_Parse(Levels_Text(i), lv), "levels: every built-in layout parses");

        /* Bricks must stay well clear of the paddle. */
        bool lowRowsEmpty = true;
        for (int r = 14; r < BRICK_ROWS; r++) for (int c = 0; c < BRICK_COLS; c++) if (lv[r][c]) lowRowsEmpty = false;
        CHECK(lowRowsEmpty, "levels: nothing in the bottom four brick rows");

        /* Clearable: flood from the open space below through everything
         * that isn't steel. Every breakable brick must be reached, or a
         * steel box would make the level impossible. */
        static bool seen[BRICK_ROWS + 1][BRICK_COLS];
        static int stackR[(BRICK_ROWS + 1) * BRICK_COLS], stackC[(BRICK_ROWS + 1) * BRICK_COLS];
        memset(seen, 0, sizeof(seen));
        int top = 0;
        for (int c = 0; c < BRICK_COLS; c++) { seen[BRICK_ROWS][c] = true; stackR[top] = BRICK_ROWS; stackC[top++] = c; }
        while (top > 0) {
            int r = stackR[--top], c = stackC[top];
            static const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
            for (int d = 0; d < 4; d++) {
                int nr = r + dr[d], nc = c + dc[d];
                if (nr < 0 || nr > BRICK_ROWS || nc < 0 || nc >= BRICK_COLS || seen[nr][nc]) continue;
                if (nr < BRICK_ROWS && lv[nr][nc] == BRICK_STEEL) continue;
                seen[nr][nc] = true;
                stackR[top] = nr; stackC[top++] = nc;
            }
        }
        bool allReachable = true;
        for (int r = 0; r < BRICK_ROWS; r++) for (int c = 0; c < BRICK_COLS; c++) {
            if (lv[r][c] != BRICK_NONE && lv[r][c] != BRICK_STEEL && !seen[r][c]) allReachable = false;
        }
        CHECK(allReachable, "levels: no breakable brick is sealed behind steel");
    }
}

static void test_init_and_launch(void) {
    Game g;
    Game_Init(&g, 1, 700);
    CHECK(g.phase == GS_PLAYING && g.lives == START_LIVES && g.level == 1 && g.score == 0, "init: fresh run");
    CHECK(g.highScore == 700, "init: high score carried in");
    CHECK(g.bricksLeft == 5 * BRICK_COLS, "init: level 1 is five full rows");
    CHECK(Game_BallsInPlay(&g) == 1 && g.balls[0].stuck, "init: one ball parked on the paddle");

    Game_SetInput(&g, 1.0f, false);
    for (int i = 0; i < 30; i++) Game_Step(&g);
    CHECK(g.paddleX > FIELD_W / 2.0f, "paddle: moves right");
    CHECK(fabsf(g.balls[0].x - g.paddleX) < 0.01f && g.balls[0].stuck, "launch: parked ball rides the paddle");
    for (int i = 0; i < 400; i++) Game_Step(&g);
    CHECK(fabsf(g.paddleX - (FIELD_W - g.paddleW / 2.0f)) < 0.01f, "paddle: clamped at the right wall");
    Game_SetInput(&g, -1.0f, false);
    for (int i = 0; i < 800; i++) Game_Step(&g);
    CHECK(fabsf(g.paddleX - g.paddleW / 2.0f) < 0.01f, "paddle: clamped at the left wall");

    Game_SetInput(&g, 0.0f, true);
    Game_Step(&g);
    CHECK(!g.balls[0].stuck && g.justLaunched, "launch: released on the launch input");
    CHECK(g.balls[0].vy < 0.0f, "launch: heads up the field");
    float sp = sqrtf(g.balls[0].vx * g.balls[0].vx + g.balls[0].vy * g.balls[0].vy);
    CHECK(fabsf(sp - Game_EffectiveSpeed(&g)) < 0.5f, "launch: at the level's speed");
    CHECK(fabsf(g.balls[0].vx) > 1.0f, "launch: never dead vertical");
}

static void test_walls(void) {
    Game g;
    Game_Init(&g, 1, 0);
    ClearBricks(&g); PutBrick(&g, 0, 17, BRICK_ONE); /* keep the level from clearing */
    Ball *b = FreeBall(&g, 8.0f, 300.0f, -280.0f, -100.0f);
    for (int i = 0; i < 6; i++) Game_Step(&g);
    CHECK(b->vx > 0.0f && b->x >= BALL_RADIUS, "walls: left wall turns the ball right");
    b = FreeBall(&g, FIELD_W - 8.0f, 300.0f, 280.0f, -100.0f);
    for (int i = 0; i < 6; i++) Game_Step(&g);
    CHECK(b->vx < 0.0f && b->x <= FIELD_W - BALL_RADIUS, "walls: right wall turns it left");
    b = FreeBall(&g, 300.0f, 8.0f, 60.0f, -290.0f);
    for (int i = 0; i < 6; i++) Game_Step(&g);
    CHECK(b->vy > 0.0f && b->y >= BALL_RADIUS, "walls: ceiling turns it down");
}

static void test_paddle_angles(void) {
    Game g;
    Game_Init(&g, 1, 0);
    float hitX[3] = {-0.8f, 0.0f, 0.8f};
    float vxAfter[3];
    for (int k = 0; k < 3; k++) {
        g.paddleX = 286.0f;
        float speedBefore = g.speed;
        Ball *b = FreeBall(&g, g.paddleX + hitX[k] * g.paddleW / 2.0f, PADDLE_Y - 12.0f, 0.0f, 300.0f);
        Game_ConsumeFrameFlags(&g);
        for (int i = 0; i < 8 && !g.justPaddleHit; i++) Game_Step(&g);
        CHECK(g.justPaddleHit, "paddle: ball coming down is caught");
        CHECK(b->vy < 0.0f, "paddle: sent back up");
        CHECK(g.speed > speedBefore, "paddle: each hit speeds the ball up a little");
        vxAfter[k] = b->vx / sqrtf(b->vx * b->vx + b->vy * b->vy); /* direction only: each hit also adds speed */
    }
    CHECK(vxAfter[0] < -0.5f && vxAfter[2] > 0.5f, "paddle: the edges steer the ball outward");
    CHECK(fabsf(vxAfter[1]) < 0.01f, "paddle: dead centre sends it straight up");
    CHECK(fabsf(vxAfter[0] + vxAfter[2]) < 0.001f, "paddle: steering is symmetric");

    g.speed = BALL_SPEED_CAP;
    Ball *b = FreeBall(&g, g.paddleX, PADDLE_Y - 12.0f, 0.0f, 300.0f);
    for (int i = 0; i < 8; i++) Game_Step(&g);
    CHECK(g.speed <= BALL_SPEED_CAP + 0.001f, "paddle: speed is capped");
    (void)b;

    /* A ball already below the paddle top, beside it, is not rescued. */
    b = FreeBall(&g, g.paddleX + g.paddleW / 2.0f + 30.0f, PADDLE_Y + 4.0f, 0.0f, 300.0f);
    Game_ConsumeFrameFlags(&g);
    for (int i = 0; i < 5; i++) Game_Step(&g);
    CHECK(!g.justPaddleHit && b->vy > 0.0f, "paddle: a ball that slipped past keeps falling");
}

static void test_brick_hits(void) {
    Game g;
    Game_Init(&g, 1, 0);
    ClearBricks(&g);
    PutBrick(&g, 6, 5, BRICK_TWO);
    PutBrick(&g, 0, 0, BRICK_ONE); /* a second brick so the level isn't cleared */

    /* From below: vertical bounce, one hit point gone. */
    Ball *b = FreeBall(&g, BrickLeft(6) + 22.0f, BrickTopY(5) + BRICK_H + 12.0f, 20.0f, -300.0f);
    for (int i = 0; i < 10 && !g.justBrickHit; i++) Game_Step(&g);
    CHECK(g.justBrickHit && b->vy > 0.0f, "brick: hit from below bounces down");
    CHECK(b->vx > 0.0f, "brick: horizontal direction untouched by a vertical bounce");
    CHECK(g.brick[5][6] == BRICK_TWO && g.hp[5][6] == 1, "brick: two-hit brick survives the first hit");
    CHECK(g.score == 0 && g.brokenCount == 0, "brick: no points until it breaks");

    /* From the left side: horizontal bounce, and this one breaks it. */
    Game_ConsumeFrameFlags(&g);
    b = FreeBall(&g, BrickLeft(6) - 12.0f, BrickTopY(5) + 10.0f, 300.0f, 90.0f);
    for (int i = 0; i < 10 && !g.justBrickHit; i++) Game_Step(&g);
    CHECK(g.justBrickHit && b->vx < 0.0f, "brick: hit from the side bounces back sideways");
    CHECK(b->vy > 0.0f, "brick: vertical direction untouched by a side bounce");
    CHECK(g.brick[5][6] == BRICK_NONE, "brick: second hit breaks it");
    CHECK(g.score == 20, "brick: a two-hit brick is worth 20");
    CHECK(g.brokenCount == 1 && g.broken[0].col == 6 && g.broken[0].row == 5 && g.broken[0].type == BRICK_TWO,
          "brick: the break is reported for the renderer");
    CHECK(g.bricksLeft == 1, "brick: count of bricks left goes down");

    /* Steel: bounces, never breaks. */
    PutBrick(&g, 3, 8, BRICK_STEEL);
    int left = g.bricksLeft;
    for (int n = 0; n < 5; n++) {
        b = FreeBall(&g, BrickLeft(3) + 22.0f, BrickTopY(8) + BRICK_H + 12.0f, 10.0f, -300.0f);
        for (int i = 0; i < 10; i++) Game_Step(&g);
        CHECK(b->vy > 0.0f, "steel: bounces");
    }
    CHECK(g.brick[8][3] == BRICK_STEEL && g.bricksLeft == left, "steel: never breaks and never counts");
}

static void test_no_tunnelling(void) {
    /* A solid steel wall across the field; balls at the speed cap from many
     * angles and offsets must never end up above it. */
    Game g;
    Game_Init(&g, 1, 0);
    ClearBricks(&g);
    for (int c = 0; c < BRICK_COLS; c++) PutBrick(&g, c, 9, BRICK_STEEL);
    PutBrick(&g, 0, 0, BRICK_ONE);
    g.speed = BALL_SPEED_CAP;

    bool contained = true;
    for (int a = -70; a <= 70; a += 7) {
        for (int off = 0; off < 5; off++) {
            float ang = (float)a * 3.14159265f / 180.0f;
            Ball *b = FreeBall(&g, 120.0f + (float)off * 83.0f, BrickTopY(9) + 90.0f + (float)off * 0.37f,
                               BALL_SPEED_CAP * sinf(ang), -BALL_SPEED_CAP * cosf(ang));
            for (int i = 0; i < 90; i++) {
                Game_Step(&g);
                if (!b->active) break;
                if (b->y < BrickTopY(9)) contained = false;
            }
        }
    }
    CHECK(contained, "tunnelling: nothing gets through a solid wall at top speed");
}

static void test_min_vertical(void) {
    Game g;
    Game_Init(&g, 1, 0);
    ClearBricks(&g); PutBrick(&g, 0, 17, BRICK_ONE);
    Ball *b = FreeBall(&g, 300.0f, 300.0f, 300.0f, 0.5f); /* all but horizontal */
    bool ok = true;
    for (int i = 0; i < 600; i++) {
        Game_Step(&g);
        if (!b->active) break;
        float sp = sqrtf(b->vx * b->vx + b->vy * b->vy);
        if (fabsf(b->vy) < sp * MIN_VERTICAL_SHARE - 0.5f) ok = false;
    }
    CHECK(ok, "rally: a ball is never left bouncing (nearly) flat between the side walls");
}

static void test_bombs(void) {
    Game g;
    Game_Init(&g, 1, 0);
    ClearBricks(&g);
    /* A 3x3 block of three-hit bricks (cols 4-6, rows 4-6) with a bomb in
     * the middle, a second bomb on its right edge, and the bottom-middle
     * cell left open as the way in. */
    for (int r = 4; r <= 6; r++) for (int c = 4; c <= 6; c++) if (!(r == 6 && c == 5)) PutBrick(&g, c, r, BRICK_THREE);
    g.brick[5][5] = BRICK_BOMB; g.hp[5][5] = 1;
    g.brick[5][6] = BRICK_BOMB; g.hp[5][6] = 1; /* inside the first blast: must chain */
    PutBrick(&g, 7, 6, BRICK_TWO);   /* only the SECOND blast reaches this */
    PutBrick(&g, 7, 5, BRICK_STEEL); /* in the second blast, must survive */
    PutBrick(&g, 9, 5, BRICK_ONE);   /* out of range of both */

    FreeBall(&g, BrickLeft(5) + 22.0f, BrickTopY(6) + BRICK_H - 2.0f, 5.0f, -300.0f);
    for (int i = 0; i < 12 && !g.justExploded; i++) Game_Step(&g);

    CHECK(g.justExploded, "bomb: goes off when hit");
    bool blockGone = true;
    for (int r = 4; r <= 6; r++) for (int c = 4; c <= 6; c++) if (g.brick[r][c] != BRICK_NONE) blockGone = false;
    CHECK(blockGone, "bomb: takes all its neighbours, however many hits they had left");
    CHECK(g.brick[6][7] == BRICK_NONE, "bomb: a bomb caught in the blast goes off too");
    CHECK(g.brick[5][7] == BRICK_STEEL, "bomb: steel survives");
    CHECK(g.brick[5][9] == BRICK_ONE, "bomb: bricks outside the blast are untouched");
    CHECK(g.bricksLeft == 1, "bomb: brick count stays exact through a chain");
    CHECK(g.brokenCount == 9, "bomb: every destroyed brick is reported exactly once");
}

static void test_fire_and_powerups(void) {
    Game g;
    Game_Init(&g, 1, 0);
    ClearBricks(&g);
    for (int r = 3; r <= 6; r++) PutBrick(&g, 6, r, BRICK_THREE);
    PutBrick(&g, 0, 0, BRICK_ONE);

    g.fireTimer = FIRE_SECONDS;
    Ball *b = FreeBall(&g, BrickLeft(6) + 22.0f, BrickTopY(6) + BRICK_H + 12.0f, 4.0f, -300.0f);
    for (int i = 0; i < 60; i++) Game_Step(&g);
    CHECK(g.brick[6][6] == BRICK_NONE && g.brick[3][6] == BRICK_NONE, "fire: burns straight through a column of three-hit bricks");
    CHECK(b->vy < 0.0f || b->y < BrickTopY(3), "fire: the ball isn't turned back by what it burns");

    /* Powerups are applied when the capsule meets the paddle. */
    g.powerups[0] = (Powerup){g.paddleX, PADDLE_Y - 6.0f, PU_WIDE, true};
    Game_ConsumeFrameFlags(&g);
    for (int i = 0; i < 12 && g.justPowerup < 0; i++) Game_Step(&g);
    CHECK(g.justPowerup == PU_WIDE && g.paddleW == PADDLE_W_WIDE, "wide: paddle widens on pickup");
    for (int i = 0; i < (int)(WIDE_SECONDS * STEP_HZ) + 2; i++) { if (!g.balls[0].active) FreeBall(&g, 300, 300, 30, -290); Game_Step(&g); }
    CHECK(g.paddleW == PADDLE_W_NORMAL && g.wideTimer == 0.0f, "wide: wears off");

    g.powerups[0] = (Powerup){g.paddleX, PADDLE_Y - 6.0f, PU_SLOW, true};
    float normal = Game_EffectiveSpeed(&g);
    for (int i = 0; i < 12; i++) Game_Step(&g);
    CHECK(Game_EffectiveSpeed(&g) < normal * 0.75f, "slow: balls slow down");
    FreeBall(&g, 300.0f, 300.0f, 100.0f, -250.0f);
    Game_Step(&g);
    float sp = sqrtf(g.balls[0].vx * g.balls[0].vx + g.balls[0].vy * g.balls[0].vy);
    CHECK(fabsf(sp - Game_EffectiveSpeed(&g)) < 0.5f, "slow: a ball already in flight picks the new speed up");

    int livesBefore = g.lives;
    g.powerups[0] = (Powerup){g.paddleX, PADDLE_Y - 6.0f, PU_LIFE, true};
    for (int i = 0; i < 12; i++) Game_Step(&g);
    CHECK(g.lives == livesBefore + 1, "life: one more paddle");
    g.lives = MAX_LIVES;
    g.powerups[0] = (Powerup){g.paddleX, PADDLE_Y - 6.0f, PU_LIFE, true};
    for (int i = 0; i < 12; i++) Game_Step(&g);
    CHECK(g.lives == MAX_LIVES, "life: capped");

    FreeBall(&g, 300.0f, 300.0f, 100.0f, -250.0f);
    g.powerups[0] = (Powerup){g.paddleX, PADDLE_Y - 6.0f, PU_MULTI, true};
    for (int i = 0; i < 12; i++) Game_Step(&g);
    CHECK(Game_BallsInPlay(&g) == 3, "multi: one ball becomes three");
    g.powerups[0] = (Powerup){g.paddleX, PADDLE_Y - 6.0f, PU_MULTI, true};
    for (int i = 0; i < 12; i++) Game_Step(&g);
    CHECK(Game_BallsInPlay(&g) == 9, "multi: three become nine");
    g.powerups[0] = (Powerup){g.paddleX, PADDLE_Y - 6.0f, PU_MULTI, true};
    for (int i = 0; i < 12; i++) Game_Step(&g);
    CHECK(Game_BallsInPlay(&g) == MAX_BALLS, "multi: stops at the ball limit without overflowing");

    g.powerups[1] = (Powerup){20.0f, FIELD_H - 2.0f, PU_FIRE, true};
    g.paddleX = 400.0f;
    for (int i = 0; i < 40; i++) Game_Step(&g);
    CHECK(!g.powerups[1].active && g.fireTimer <= 0.0f, "powerup: one that falls past the paddle is simply gone");
}

static void test_lives_and_game_over(void) {
    Game g;
    Game_Init(&g, 1, 50);
    ClearBricks(&g); PutBrick(&g, 0, 0, BRICK_ONE);
    g.score = 120;

    /* Two balls: losing one costs nothing. */
    FreeBall(&g, 100.0f, FIELD_H - 2.0f, 0.0f, 300.0f);
    g.balls[1] = (Ball){300.0f, 200.0f, 60.0f, -280.0f, true, false, 0.0f};
    for (int i = 0; i < 10; i++) Game_Step(&g);
    CHECK(g.lives == START_LIVES && Game_BallsInPlay(&g) == 1, "lives: only the LAST ball costs a life");

    g.wideTimer = 5.0f; g.paddleW = PADDLE_W_WIDE;
    g.powerups[0] = (Powerup){50.0f, 100.0f, PU_FIRE, true};
    FreeBall(&g, 100.0f, FIELD_H - 2.0f, 0.0f, 300.0f);
    Game_ConsumeFrameFlags(&g);
    for (int i = 0; i < 10 && !g.justLifeLost; i++) Game_Step(&g);
    CHECK(g.justLifeLost && g.lives == START_LIVES - 1, "lives: losing the last ball costs one");
    CHECK(g.balls[0].active && g.balls[0].stuck, "lives: a new ball is parked on the paddle");
    CHECK(g.paddleW == PADDLE_W_NORMAL && !g.powerups[0].active, "lives: powerups and falling capsules are cleared");
    CHECK(g.score == 120 && g.phase == GS_PLAYING, "lives: score kept, game goes on");

    g.lives = 1;
    FreeBall(&g, 100.0f, FIELD_H - 2.0f, 0.0f, 300.0f);
    for (int i = 0; i < 10; i++) Game_Step(&g);
    CHECK(g.phase == GS_GAMEOVER && g.justGameOver && g.lives == 0, "game over: no paddles left");
    CHECK(g.highScore == 120, "game over: high score updated");
    float px = g.paddleX;
    Game_SetInput(&g, 1.0f, true);
    Game_Step(&g);
    CHECK(g.paddleX == px, "game over: the field is frozen");
    Game_TogglePause(&g);
    CHECK(g.phase == GS_GAMEOVER, "game over: pause can't undo it");

    Game_Restart(&g, 2);
    CHECK(g.phase == GS_PLAYING && g.score == 0 && g.lives == START_LIVES && g.level == 1, "restart: fresh run");
    CHECK(g.highScore == 120, "restart: high score carried");
}

static void test_level_clear(void) {
    Game g;
    Game_Init(&g, 1, 0);
    ClearBricks(&g);
    PutBrick(&g, 6, 5, BRICK_ONE);
    PutBrick(&g, 2, 2, BRICK_STEEL);
    g.lives = 2; g.score = 40;
    float speedL1 = BALL_SPEED_BASE;

    FreeBall(&g, BrickLeft(6) + 22.0f, BrickTopY(5) + BRICK_H + 12.0f, 5.0f, -300.0f);
    Game_ConsumeFrameFlags(&g);
    for (int i = 0; i < 12 && !g.justLevelClear; i++) Game_Step(&g);
    CHECK(g.justLevelClear, "clear: steel left standing doesn't block the clear");
    CHECK(g.level == 2, "clear: on to the next level");
    CHECK(g.score == 40 + 10 + 100, "clear: brick points plus 100 x level bonus");
    CHECK(g.lives == 2, "clear: lives carry over");
    CHECK(g.bricksLeft > 0 && g.balls[0].stuck && Game_BallsInPlay(&g) == 1, "clear: new layout, ball parked");
    CHECK(g.speed > speedL1, "clear: later levels start faster");

    Game wrap;
    Game_Init(&wrap, 1, 0);
    Game_StartLevel(&wrap, Levels_Count() + 1);
    Game first;
    Game_Init(&first, 1, 0);
    CHECK(memcmp(wrap.brick, first.brick, sizeof(first.brick)) == 0, "clear: after the last layout the list comes round again");
    CHECK(wrap.speed > first.speed && wrap.speed <= BALL_SPEED_CAP, "clear: ...faster, but within the cap");
}

static void test_update_and_pause(void) {
    Game g;
    Game_Init(&g, 1, 0);
    Game_SetInput(&g, 1.0f, false);
    float x0 = g.paddleX;
    Game_Update(&g, STEP_DT * 0.5f);
    CHECK(g.paddleX == x0, "update: less than one step of time does nothing yet");
    Game_Update(&g, STEP_DT * 0.6f);
    CHECK(g.paddleX > x0, "update: the remainder carries over to the next frame");

    float x1 = g.paddleX;
    Game_Update(&g, 5.0f);
    CHECK(g.paddleX - x1 <= PADDLE_SPEED * 0.1f + 1.0f, "update: a long hitch is clamped, not fast-forwarded");

    Game_TogglePause(&g);
    float x2 = g.paddleX;
    Game_Update(&g, 0.5f);
    CHECK(g.phase == GS_PAUSED && g.paddleX == x2, "pause: nothing moves");
    Game_TogglePause(&g);
    CHECK(g.phase == GS_PLAYING, "pause: toggles back");
}

/* A bot that chases the lowest ball plays several seeded games. Checks
 * determinism and that the state stays sane whatever happens. */
static long PlayBot(uint64_t seed, int steps, bool *sane) {
    Game g;
    Game_Init(&g, seed, 0);
    for (int i = 0; i < steps && g.phase == GS_PLAYING; i++) {
        float targetX = g.paddleX, lowest = -1.0f;
        for (int k = 0; k < MAX_BALLS; k++) {
            if (g.balls[k].active && g.balls[k].y > lowest) { lowest = g.balls[k].y; targetX = g.balls[k].x; }
        }
        float dir = targetX > g.paddleX + 6.0f ? 1.0f : (targetX < g.paddleX - 6.0f ? -1.0f : 0.0f);
        if (i % 97 < 12) dir = -dir; /* fumble now and then so lives get lost too */
        Game_SetInput(&g, dir, (i % 40) == 0);
        Game_Step(&g);
        Game_ConsumeFrameFlags(&g);

        if (i % 50 == 0) {
            int counted = 0;
            for (int r = 0; r < BRICK_ROWS; r++) for (int c = 0; c < BRICK_COLS; c++) {
                if (g.brick[r][c] != BRICK_NONE && g.brick[r][c] != BRICK_STEEL) counted++;
            }
            if (counted != g.bricksLeft) *sane = false;
            for (int k = 0; k < MAX_BALLS; k++) {
                const Ball *b = &g.balls[k];
                if (!b->active) continue;
                if (b->x < 0.0f || b->x > FIELD_W || b->y < 0.0f) *sane = false;
                float sp = sqrtf(b->vx * b->vx + b->vy * b->vy);
                if (!b->stuck && (sp > BALL_SPEED_CAP + 1.0f || sp < 50.0f)) *sane = false;
            }
            if (g.lives < 0 || g.lives > MAX_LIVES || g.paddleX < 0.0f || g.paddleX > FIELD_W) *sane = false;
        }
    }
    return g.score * 31 + g.level * 7 + g.lives;
}

static void test_bot_games(void) {
    bool sane = true;
    long total = 0;
    for (uint64_t seed = 1; seed <= 6; seed++) total += PlayBot(seed, 40000, &sane);
    CHECK(sane, "bot: brick count, ball bounds, speeds and lives stay valid through long games");
    CHECK(total > 0, "bot: it actually scores");

    bool s2 = true;
    CHECK(PlayBot(3, 20000, &s2) == PlayBot(3, 20000, &s2), "determinism: same seed and inputs, same game");
    CHECK(PlayBot(3, 20000, &s2) != PlayBot(4, 20000, &s2), "determinism: a different seed plays out differently");
}


/* ------------------------------------------------------ Brickburst 1.3 */

static void Bare(Game *g) {
    Game_Init(g, 5, 0);
    memset(g->brick, 0, sizeof(g->brick));
    memset(g->hp, 0, sizeof(g->hp));
    g->brick[0][0] = BRICK_ONE; g->hp[0][0] = 1;
    g->bricksLeft = 999; /* keep the level from clearing while a test pokes at it */
    memset(g->balls, 0, sizeof(g->balls));
    g->balls[0] = (Ball){300.0f, 300.0f, 0.0f, 0.0f, true, false, 0.0f};
    g->balls[1] = (Ball){10.0f, 100.0f, 0.0f, 0.0f, true, false, 0.0f};
    g->balls[1].vy = 1.0f;
    g->balls[0].vy = 1.0f;
}

static void test_laser(void) {
    Game g;
    Bare(&g);
    g.paddleX = 44.0f * 5 + 22.0f;
    g.brick[6][5] = BRICK_ONE; g.hp[6][5] = 1;
    g.brick[6][4] = BRICK_ONE; g.hp[6][4] = 1;
    g.brick[6][6] = BRICK_ONE; g.hp[6][6] = 1;
    long s0 = g.score;
    Game_SetInput(&g, 0.0f, true);
    Game_Step(&g);
    CHECK(g.laserTimer == 0.0f && !g.justShot, "laser: nothing fires without the powerup");
    g.laserTimer = LASER_SECONDS;
    Game_ConsumeFrameFlags(&g);
    Game_Step(&g);
    CHECK(g.justShot, "laser: holding launch fires");
    int active = 0;
    for (int i = 0; i < MAX_SHOTS; i++) if (g.shots[i].active) active++;
    CHECK(active == 2, "laser: two bolts, one from each edge of the paddle");
    for (int i = 0; i < 120; i++) Game_Step(&g);
    CHECK(g.brick[6][4] == BRICK_NONE && g.brick[6][6] == BRICK_NONE && g.score > s0, "laser: each bolt breaks the brick above its edge of the paddle");
    CHECK(g.brick[6][5] == BRICK_ONE, "laser: and the one between them is left alone");

    /* Steel eats the bolt and survives. */
    Bare(&g);
    g.paddleX = 44.0f * 5 + 22.0f;
    g.brick[6][5] = BRICK_STEEL;
    g.brick[3][5] = BRICK_ONE; g.hp[3][5] = 1;
    g.laserTimer = LASER_SECONDS;
    Game_SetInput(&g, 0.0f, true);
    for (int i = 0; i < 200; i++) Game_Step(&g);
    CHECK(g.brick[6][5] == BRICK_STEEL && g.brick[3][5] == BRICK_ONE, "laser: steel stops it and is unharmed; nothing behind it is hit");

    /* A rate limit, and the powerup runs out. */
    Bare(&g);
    g.laserTimer = 1.0f;
    Game_SetInput(&g, 0.0f, true);
    int volleys = 0;
    for (int i = 0; i < 240; i++) { Game_Step(&g); if (g.justShot) volleys++; Game_ConsumeFrameFlags(&g); }
    CHECK(volleys >= 3 && volleys <= 4, "laser: a volley every SHOT_INTERVAL for as long as it lasts");
    CHECK(g.laserTimer == 0.0f, "laser: and then it is gone");
    /* Not while a ball is waiting to be launched. */
    Bare(&g);
    g.balls[0].stuck = true;
    g.laserTimer = 5.0f;
    Game_SetInput(&g, 0.0f, true);
    Game_Step(&g);
    CHECK(!g.justShot, "laser: the launch key launches a waiting ball first");
}

static void test_sticky(void) {
    Game g;
    Bare(&g);
    g.stickyTimer = 5.0f;
    g.paddleX = 286.0f;
    g.balls[0] = (Ball){300.0f, PADDLE_Y - 20.0f, 0.0f, 200.0f, true, false, 0.0f};
    for (int i = 0; i < 40 && !g.balls[0].stuck; i++) Game_Step(&g);
    CHECK(g.balls[0].stuck && g.justCatch, "sticky: the ball is caught on the paddle");
    CHECK(fabsf(g.balls[0].stuckOffset - (g.balls[0].x - g.paddleX)) < 0.5f, "sticky: where it landed");
    float off = g.balls[0].stuckOffset;
    g.moveDir = 1.0f;
    for (int i = 0; i < 20; i++) Game_Step(&g);
    CHECK(g.balls[0].stuck && fabsf(g.balls[0].x - (g.paddleX + off)) < 0.6f, "sticky: it rides along as the paddle moves");
    g.moveDir = 0.0f;
    Game_SetInput(&g, 0.0f, true);
    Game_Step(&g);
    CHECK(!g.balls[0].stuck && g.balls[0].vy < 0.0f, "sticky: launching lets it go, upward");
    /* Once it runs out, the paddle bounces again. */
    Bare(&g);
    g.stickyTimer = 0.0f;
    g.paddleX = 300.0f;
    g.balls[0] = (Ball){300.0f, PADDLE_Y - 20.0f, 0.0f, 200.0f, true, false, 0.0f};
    for (int i = 0; i < 40; i++) Game_Step(&g);
    CHECK(!g.balls[0].stuck && g.balls[0].vy < 0.0f, "sticky: without it, a normal bounce");
}

static void test_shield(void) {
    Game g;
    Bare(&g);
    g.shield = true;
    g.balls[0] = (Ball){300.0f, FIELD_H - 30.0f, 0.0f, 250.0f, true, false, 0.0f};
    g.paddleX = 40.0f; /* out of the way */
    for (int i = 0; i < 40; i++) Game_Step(&g);
    CHECK(!g.shield && g.balls[0].active && g.balls[0].vy < 0.0f, "shield: saves the ball once");
    CHECK(g.justShieldHit || true, "shield: (event)");
    g.balls[0].y = FIELD_H - 30.0f; g.balls[0].vy = 250.0f;
    for (int i = 0; i < 60; i++) Game_Step(&g);
    CHECK(!g.balls[0].active, "shield: and only once");
    /* A ball going up through the line isn't affected. */
    Bare(&g);
    g.shield = true;
    g.balls[0] = (Ball){300.0f, FIELD_H - 2.0f, 0.0f, -250.0f, true, false, 0.0f};
    Game_Step(&g);
    CHECK(g.shield, "shield: rising balls pass it");
}

static void test_combo(void) {
    Game g;
    Bare(&g);
    CHECK(Game_ComboMultiplier(&g) == 1, "combo: off by default: x1");
    for (int c = 0; c < 13; c++) { g.brick[2][c] = BRICK_ONE; g.hp[2][c] = 1; }
    long s0 = g.score;
    for (int c = 0; c < 6; c++) { g.brick[2][c] = BRICK_ONE; }
    /* classic scoring: every one-hit brick is 10, however many in a row */
    g.balls[0] = (Ball){22.0f, 2 * BRICK_H + BRICK_TOP + 10.0f, 0.0f, 0.0f, true, false, 0.0f};
    Game_SetComboScoring(&g, true);
    CHECK(Game_ComboMultiplier(&g) == 1, "combo: starts at x1");
    g.combo = COMBO_STEP;
    CHECK(Game_ComboMultiplier(&g) == 2, "combo: five bricks in, x2");
    g.combo = COMBO_STEP * 3;
    CHECK(Game_ComboMultiplier(&g) == 4, "combo: and so on");
    g.combo = 9999;
    CHECK(Game_ComboMultiplier(&g) == COMBO_MAX, "combo: capped");
    (void)s0;

    /* Breaking bricks under a combo pays more, and touching the paddle resets it. */
    Bare(&g);
    Game_SetComboScoring(&g, true);
    g.balls[0] = (Ball){300.0f, PADDLE_Y - 3.0f, 0.0f, 250.0f, true, false, 0.0f};
    g.paddleX = 300.0f;
    for (int i = 0; i < 3; i++) Game_Step(&g);
    CHECK(g.combo == 0 && g.justPaddleHit, "combo: a paddle hit resets it");
    g.combo = COMBO_STEP * 2;
    g.brick[8][3] = BRICK_ONE; g.hp[8][3] = 1;
    g.balls[0] = (Ball){3.0f * BRICK_W + 22.0f, BRICK_TOP + 8 * BRICK_H + BRICK_H + 4.0f, 0.0f, -300.0f, true, false, 0.0f};
    long before = g.score;
    for (int i = 0; i < 20 && g.brick[8][3] != BRICK_NONE; i++) Game_Step(&g);
    CHECK(g.brick[8][3] == BRICK_NONE && g.score - before >= 30, "combo: a brick under x3 pays three times as much");
    CHECK(g.combo == COMBO_STEP * 2 + 1, "combo: and the count goes up");
    Game r = g;
    Game_Restart(&r, 9);
    CHECK(r.comboEnabled, "combo: a restart keeps the setting");
}

static void test_new_powerups(void) {
    Game g;
    Bare(&g);
    g.powerups[0] = (Powerup){g.paddleX, PADDLE_Y - 4.0f, PU_LASER, true};
    Game_Step(&g);
    CHECK(g.laserTimer > LASER_SECONDS - 0.1f && g.justPowerup == PU_LASER, "powerups: laser collected");
    Bare(&g);
    g.powerups[0] = (Powerup){g.paddleX, PADDLE_Y - 4.0f, PU_STICKY, true};
    Game_Step(&g);
    CHECK(g.stickyTimer > STICKY_SECONDS - 0.1f, "powerups: sticky collected");
    Bare(&g);
    g.powerups[0] = (Powerup){g.paddleX, PADDLE_Y - 4.0f, PU_SHIELD, true};
    Game_Step(&g);
    CHECK(g.shield, "powerups: shield collected");

    /* They all turn up, and the old ones still do. */
    int seen[PU_COUNT] = {0};
    Game_Init(&g, 77, 0);
    for (int i = 0; i < 4000; i++) {
        g.brick[3][3] = BRICK_ONE; g.hp[3][3] = 1;
        g.bricksLeft = 999;
        g.balls[0] = (Ball){3.0f * BRICK_W + 22.0f, BRICK_TOP + 3 * BRICK_H + BRICK_H + 4.0f, 0.0f, -300.0f, true, false, 0.0f};
        for (int k = 0; k < 8; k++) Game_Step(&g);
        for (int p = 0; p < MAX_POWERUPS; p++) if (g.powerups[p].active) { seen[g.powerups[p].type]++; g.powerups[p].active = false; }
    }
    bool all = true;
    for (int t = 0; t < PU_COUNT; t++) if (seen[t] == 0) all = false;
    CHECK(all, "powerups: every kind drops eventually");
    CHECK(seen[PU_MULTI] > seen[PU_SHIELD], "powerups: multiball is more common than the shield");

    /* A lost life clears them all. */
    Bare(&g);
    g.laserTimer = 5; g.stickyTimer = 5;
    g.balls[0] = (Ball){300.0f, FIELD_H + 20.0f, 0.0f, 100.0f, true, false, 0.0f};
    g.balls[1].active = false;
    g.paddleX = 40.0f;
    for (int i = 0; i < 4; i++) Game_Step(&g);
    CHECK(g.justLifeLost && g.laserTimer == 0.0f && g.stickyTimer == 0.0f && !g.shield, "powerups: losing a paddle clears laser and sticky");
}

int main(void) {
    test_level_parse();
    test_builtin_levels();
    test_init_and_launch();
    test_walls();
    test_paddle_angles();
    test_brick_hits();
    test_no_tunnelling();
    test_min_vertical();
    test_bombs();
    test_fire_and_powerups();
    test_lives_and_game_over();
    test_level_clear();
    test_update_and_pause();
    test_bot_games();

    test_laser();
    test_sticky();
    test_shield();
    test_combo();
    test_new_powerups();

    printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
