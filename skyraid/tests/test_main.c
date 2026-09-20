/* Headless correctness tests for Skyraid's rules (game.c). No raylib: builds
 * and runs anywhere. Physics is a fixed 120 Hz step, so every scenario is
 * exact and repeatable. */
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

/* Keeps the enemy quiet so a scripted scenario can't be interrupted. */
static void Silence(Game *g) {
    g->enemyFireTimer = 1e9f;
    g->ufoTimer = 1e9f;
    memset(g->enemyShots, 0, sizeof(g->enemyShots));
}

static void KillAllBut(Game *g, int keepRow, int keepCol) {
    for (int r = 0; r < INV_ROWS; r++) for (int c = 0; c < INV_COLS; c++) g->alive[r][c] = (r == keepRow && c == keepCol);
    g->aliveCount = 1;
}

static void RunSteps(Game *g, int n) { for (int i = 0; i < n; i++) Game_Step(g); }

static void test_tables(void) {
    float prev = Game_MarchInterval(1, 1);
    bool monotonic = true;
    for (int alive = 2; alive <= INV_ROWS * INV_COLS; alive++) {
        float cur = Game_MarchInterval(alive, 1);
        if (cur < prev) monotonic = false;
        prev = cur;
    }
    CHECK(monotonic, "march: fewer invaders never march slower");
    CHECK(Game_MarchInterval(1, 1) < 0.06f && Game_MarchInterval(55, 1) > 0.5f, "march: the last one is frantic, the full formation stately");
    CHECK(Game_MarchInterval(55, 4) < Game_MarchInterval(55, 1), "march: later waves are quicker");
    CHECK(Game_MarchInterval(55, 99) >= Game_MarchInterval(55, 1) * 0.59f, "march: the wave speed-up is capped");
    CHECK(Game_MarchInterval(0, 1) == Game_MarchInterval(1, 1), "march: degenerate counts are safe");

    CHECK(Game_RowPoints(0) == 30 && Game_RowPoints(1) == 20 && Game_RowPoints(2) == 20 &&
          Game_RowPoints(3) == 10 && Game_RowPoints(4) == 10, "points: top row worth most");
    CHECK(Game_UfoPoints(8) == 300 && Game_UfoPoints(23) == 300, "ufo: the ninth shot of every fifteen is the jackpot");
    CHECK(Game_UfoPoints(0) == 100 && Game_UfoPoints(-5) == 100, "ufo: table start, negative guarded");

    int solid = 0;
    for (int r = 0; r < BUNKER_ROWS; r++) for (int c = 0; c < BUNKER_COLS; c++) if (Game_BunkerShape(r, c)) solid++;
    CHECK(solid > 200 && solid < BUNKER_ROWS * BUNKER_COLS, "bunker: shaped, not a plain block");
    CHECK(!Game_BunkerShape(0, 0) && Game_BunkerShape(5, 0), "bunker: chamfered shoulders");
    CHECK(!Game_BunkerShape(15, 11) && Game_BunkerShape(15, 2), "bunker: arch underneath");
    bool symmetric = true;
    for (int r = 0; r < BUNKER_ROWS; r++) for (int c = 0; c < BUNKER_COLS; c++) {
        if (Game_BunkerShape(r, c) != Game_BunkerShape(r, BUNKER_COLS - 1 - c)) symmetric = false;
    }
    CHECK(symmetric, "bunker: left-right symmetric");
    CHECK(!Game_BunkerShape(-1, 3) && !Game_BunkerShape(3, 99), "bunker: out-of-range lookups are safe");
    CHECK(Game_BunkerX(BUNKER_COUNT - 1) + BUNKER_COLS * BUNKER_CELL < FIELD_W, "bunker: all four fit on the field");
}

static void test_init(void) {
    Game g;
    Game_Init(&g, 1, 900);
    CHECK(g.phase == GS_PLAYING && g.lives == START_LIVES && g.wave == 1 && g.score == 0, "init: fresh run");
    CHECK(g.highScore == 900, "init: high score carried in");
    CHECK(g.aliveCount == INV_ROWS * INV_COLS, "init: full formation");
    float w = (float)((INV_COLS - 1) * INV_CELL_W + INV_W);
    CHECK(fabsf(g.formX - (FIELD_W - w) / 2.0f) < 0.01f && g.formY == FORMATION_START_Y, "init: formation centred at the start height");
    CHECK(g.bunker[0][5][5] == 1 && g.bunker[3][15][11] == 0, "init: bunkers built to shape");
    CHECK(fabsf(g.playerX - FIELD_W / 2.0f) < 0.01f && !g.playerShot.active, "init: player centred, not firing");
}

static void test_player(void) {
    Game g;
    Game_Init(&g, 1, 0);
    Silence(&g);
    Game_SetInput(&g, -1.0f, false);
    RunSteps(&g, 400);
    CHECK(fabsf(g.playerX - PLAYER_W / 2.0f) < 0.01f, "player: clamped at the left edge");
    Game_SetInput(&g, 1.0f, false);
    RunSteps(&g, 800);
    CHECK(fabsf(g.playerX - (FIELD_W - PLAYER_W / 2.0f)) < 0.01f, "player: clamped at the right edge");

    Game_SetInput(&g, 0.0f, true);
    Game_Step(&g);
    CHECK(g.playerShot.active && g.justFired && g.shotsFired == 1, "fire: a shot leaves the ship");
    float y1 = g.playerShot.y;
    Game_Step(&g);
    CHECK(g.shotsFired == 1 && g.playerShot.y < y1, "fire: holding fire doesn't add a second shot; the first flies up");
    /* far right column: nothing above, so it leaves the top */
    RunSteps(&g, 200);
    CHECK(g.shotsFired >= 2, "fire: once the shot is gone, holding fire shoots again");
}

static void test_shooting_invaders(void) {
    Game g;
    Game_Init(&g, 1, 0);
    Silence(&g);
    g.marchTimer = -1e9f; /* hold the formation still */

    /* Line up under column 5; the bottom row (4) is hit first. */
    float ix, iy;
    Game_InvaderRect(&g, 4, 5, &ix, &iy);
    g.playerX = ix + INV_W / 2.0f;
    Game_SetInput(&g, 0.0f, true);
    Game_ConsumeFrameFlags(&g);
    for (int i = 0; i < 200 && g.killCount == 0; i++) { Game_SetInput(&g, 0.0f, i == 0); Game_Step(&g); }
    CHECK(g.killCount == 1 && g.kills[0].row == 4 && g.kills[0].points == 10, "shoot: bottom invader in the column dies first, 10 points");
    CHECK(!g.alive[4][5] && g.alive[3][5] && g.aliveCount == INV_ROWS * INV_COLS - 1, "shoot: exactly one invader removed");
    CHECK(g.score == 10 && !g.playerShot.active, "shoot: scored, shot consumed");
    CHECK(fabsf(g.kills[0].x - (ix + INV_W / 2.0f)) < 0.01f, "shoot: kill position reported for the renderer");

    /* Clear the column from the bottom up: rows are worth 10,10,20,20,30. */
    for (int n = 0; n < 4; n++) {
        Game_ConsumeFrameFlags(&g);
        for (int i = 0; i < 300 && g.killCount == 0; i++) { Game_SetInput(&g, 0.0f, true); Game_Step(&g); }
    }
    CHECK(g.score == 10 + 10 + 20 + 20 + 30, "shoot: a whole column is worth 90");

    /* A shot up the GAP between two columns hits nothing. */
    g.playerX = ix + INV_W + (INV_CELL_W - INV_W) / 2.0f;
    int before = g.aliveCount;
    g.playerShot.active = false;
    Game_SetInput(&g, 0.0f, true);
    Game_Step(&g);
    Game_SetInput(&g, 0.0f, false);
    RunSteps(&g, 200);
    CHECK(g.aliveCount == before, "shoot: the gap between sprites is really a gap");
}

static void test_march(void) {
    Game g;
    Game_Init(&g, 1, 0);
    Silence(&g);
    float x0 = g.formX, y0 = g.formY;
    int steps = (int)(Game_MarchInterval(g.aliveCount, 1) * STEP_HZ) + 2;
    Game_ConsumeFrameFlags(&g);
    RunSteps(&g, steps);
    CHECK(g.justMarched && g.formX == x0 + MARCH_DX && g.formY == y0, "march: one step right");
    CHECK(g.marchFrame == 1 && g.marchNote == 1, "march: pose and bass note advance with the step");

    /* Walk to the right edge: it must drop and turn, never leave the field. */
    bool inside = true, dropped = false;
    for (int i = 0; i < 40 && !dropped; i++) {
        float yBefore = g.formY;
        RunSteps(&g, steps);
        float right = g.formX + (INV_COLS - 1) * INV_CELL_W + INV_W;
        if (right > FIELD_W - FORMATION_MARGIN + 0.01f || g.formX < FORMATION_MARGIN - 0.01f) inside = false;
        if (g.formY > yBefore) dropped = true;
    }
    CHECK(dropped && g.marchDir == -1, "march: drops a row and reverses at the edge");
    CHECK(inside, "march: never crosses the margin");
    float xAtDrop = g.formX;
    RunSteps(&g, steps);
    CHECK(g.formX == xAtDrop - MARCH_DX, "march: heads back the other way");

    /* With only the LEFT column alive the formation can travel further right. */
    Game h;
    Game_Init(&h, 1, 0);
    Silence(&h);
    for (int r = 0; r < INV_ROWS; r++) for (int c = 1; c < INV_COLS; c++) h.alive[r][c] = false;
    h.aliveCount = INV_ROWS;
    float maxX = h.formX;
    for (int i = 0; i < 4000; i++) { Game_Step(&h); if (h.formX > maxX) maxX = h.formX; if (h.marchDir < 0) break; }
    CHECK(maxX > FIELD_W - FORMATION_MARGIN - INV_W - MARCH_DX - 0.01f, "march: an emptied edge column lets the rest march further");
}

static void test_enemy_fire(void) {
    Game g;
    Game_Init(&g, 5, 0);
    g.ufoTimer = 1e9f;
    g.marchTimer = -1e9f;
    /* Knock holes in the formation, then check every shot starts under the
     * lowest survivor of some column. */
    g.alive[4][3] = g.alive[3][3] = false; g.alive[4][7] = false; g.aliveCount -= 3;
    bool ok = true;
    int seen = 0;
    g.playerX = -1000.0f; /* out of harm's way */
    for (int i = 0; i < 6000; i++) {
        bool had[MAX_ENEMY_SHOTS];
        for (int k = 0; k < MAX_ENEMY_SHOTS; k++) had[k] = g.enemyShots[k].active;
        Game_Step(&g);
        g.playerX = -1000.0f;
        for (int k = 0; k < MAX_ENEMY_SHOTS; k++) {
            if (had[k] || !g.enemyShots[k].active) continue;
            seen++;
            int col = (int)((g.enemyShots[k].x - g.formX) / INV_CELL_W);
            int lowest = INV_ROWS - 1;
            while (lowest >= 0 && !g.alive[lowest][col]) lowest--;
            float expectY = g.formY + lowest * INV_CELL_H + INV_H;
            if (lowest < 0 || fabsf(g.enemyShots[k].y - expectY) > 3.0f) ok = false;
        }
        int active = 0;
        for (int k = 0; k < MAX_ENEMY_SHOTS; k++) if (g.enemyShots[k].active) active++;
        if (active > MAX_ENEMY_SHOTS) ok = false;
    }
    CHECK(seen > 10, "enemy fire: they do shoot");
    CHECK(ok, "enemy fire: only the lowest invader of a column fires, never more than three shots");
}

static void test_bunkers(void) {
    Game g;
    Game_Init(&g, 1, 0);
    Silence(&g);
    g.marchTimer = -1e9f;

    int before = 0, after = 0;
    for (int r = 0; r < BUNKER_ROWS; r++) for (int c = 0; c < BUNKER_COLS; c++) before += g.bunker[0][r][c];

    /* Fire straight up into the first bunker's left leg. */
    g.playerX = Game_BunkerX(0) + 3 * BUNKER_CELL + 1.0f;
    Game_SetInput(&g, 0.0f, true);
    Game_Step(&g);
    Game_SetInput(&g, 0.0f, false);
    Game_ConsumeFrameFlags(&g);
    for (int i = 0; i < 60 && !g.justBunkerHit; i++) Game_Step(&g);
    for (int r = 0; r < BUNKER_ROWS; r++) for (int c = 0; c < BUNKER_COLS; c++) after += g.bunker[0][r][c];
    CHECK(g.justBunkerHit && !g.playerShot.active, "bunker: stops the player's own shot");
    CHECK(after < before && before - after >= 5 && before - after < 40, "bunker: a hit blasts a ragged hole, not the whole thing");
    CHECK(g.aliveCount == INV_ROWS * INV_COLS, "bunker: the shot never reached the formation");

    /* Through the arch there is nothing to hit. */
    Game h;
    Game_Init(&h, 1, 0);
    Silence(&h);
    h.marchTimer = -1e9f;
    h.playerX = Game_BunkerX(1) + 11 * BUNKER_CELL;
    Game_SetInput(&h, 0.0f, true);
    Game_Step(&h);
    Game_SetInput(&h, 0.0f, false);
    bool hit = false;
    for (int i = 0; i < 7; i++) { Game_Step(&h); if (h.justBunkerHit) hit = true; }
    CHECK(!hit && h.playerShot.y < BUNKER_Y + BUNKER_ROWS * BUNKER_CELL, "bunker: a shot flies up into the open arch untouched");
    for (int i = 0; i < 20 && !hit; i++) { Game_Step(&h); if (h.justBunkerHit) hit = true; }
    CHECK(hit, "bunker: ...and is stopped by the roof above it");

    /* Enemy shots erode from above. */
    Game e;
    Game_Init(&e, 1, 0);
    Silence(&e);
    e.marchTimer = -1e9f;
    e.enemyShots[0] = (Shot){Game_BunkerX(2) + 30.0f, BUNKER_Y - 40.0f, true};
    Game_ConsumeFrameFlags(&e);
    for (int i = 0; i < 80 && !e.justBunkerHit; i++) Game_Step(&e);
    CHECK(e.justBunkerHit && !e.enemyShots[0].active && e.lives == START_LIVES, "bunker: shelters the player from above");

    /* Invaders walking over a bunker flatten what they touch. */
    Game t;
    Game_Init(&t, 1, 0);
    Silence(&t);
    KillAllBut(&t, 0, 0);
    t.formX = Game_BunkerX(0) + 8.0f;
    t.formY = BUNKER_Y + 4.0f;
    RunSteps(&t, (int)(Game_MarchInterval(1, 1) * STEP_HZ) + 2);
    int solid = 0;
    for (int r = 2; r < 6; r++) for (int c = 6; c < 9; c++) solid += t.bunker[0][r][c];
    CHECK(solid == 0, "bunker: trampled where the formation walks through it");
}

static void test_player_death_and_lives(void) {
    Game g;
    Game_Init(&g, 1, 0);
    Silence(&g);
    g.marchTimer = -1e9f;
    g.playerX = 300.0f;
    g.enemyShots[0] = (Shot){300.0f, PLAYER_Y - 30.0f, true};
    g.enemyShots[1] = (Shot){100.0f, 200.0f, true};
    Game_ConsumeFrameFlags(&g);
    for (int i = 0; i < 60 && !g.justPlayerHit; i++) Game_Step(&g);
    CHECK(g.justPlayerHit && g.lives == START_LIVES - 1, "death: a hit costs a ship");
    CHECK(!g.enemyShots[1].active && g.respawnTimer > 0.0f, "death: the sky clears and the world holds its breath");
    float fx = g.formX;
    Game_SetInput(&g, 1.0f, true);
    g.marchTimer = 1e9f;
    RunSteps(&g, 30);
    CHECK(g.formX == fx && g.playerX == 300.0f && !g.playerShot.active, "death: nothing moves during the pause");
    RunSteps(&g, (int)(RESPAWN_SECONDS * STEP_HZ) + 2);
    CHECK(g.respawnTimer == 0.0f && g.phase == GS_PLAYING, "death: play resumes");

    g.lives = 1;
    g.marchTimer = -1e9f;
    Silence(&g);
    g.score = 440;
    g.enemyShots[0] = (Shot){g.playerX, PLAYER_Y - 10.0f, true};
    Game_ConsumeFrameFlags(&g);
    RunSteps(&g, 30);
    CHECK(g.phase == GS_GAMEOVER && g.justGameOver && !g.invaded && g.lives == 0, "game over: last ship lost");
    CHECK(g.highScore == 440, "game over: high score updated");
    Game_TogglePause(&g);
    CHECK(g.phase == GS_GAMEOVER, "game over: pause can't undo it");
    Game_Restart(&g, 9);
    CHECK(g.phase == GS_PLAYING && g.score == 0 && g.lives == START_LIVES && g.highScore == 440, "restart: fresh run, best kept");
}

static void test_invasion(void) {
    Game g;
    Game_Init(&g, 1, 0);
    Silence(&g);
    g.lives = 3;
    g.formY = PLAYER_Y - (INV_ROWS - 1) * INV_CELL_H - INV_H - 4.0f; /* bottom row almost on the ground */
    g.formX = FIELD_W - FORMATION_MARGIN - ((INV_COLS - 1) * INV_CELL_W + INV_W); /* at the edge: next march drops */
    g.marchDir = 1;
    RunSteps(&g, (int)(Game_MarchInterval(g.aliveCount, 1) * STEP_HZ) + 2);
    CHECK(g.phase == GS_GAMEOVER && g.invaded, "invasion: reaching the ground ends the game outright");
    CHECK(g.lives == 3, "invasion: ...however many ships were left");
}

static void test_waves_and_extra_life(void) {
    Game g;
    Game_Init(&g, 1, 0);
    Silence(&g);
    g.marchTimer = -1e9f;
    KillAllBut(&g, 2, 5);
    g.bunker[0][5][5] = 0;
    float ix, iy;
    Game_InvaderRect(&g, 2, 5, &ix, &iy);
    g.playerX = ix + INV_W / 2.0f;
    Game_ConsumeFrameFlags(&g);
    for (int i = 0; i < 300 && !g.justWaveClear; i++) { Game_SetInput(&g, 0.0f, true); Game_Step(&g); }
    CHECK(g.justWaveClear && g.wave == 2, "wave: clearing the formation brings the next one");
    CHECK(g.aliveCount == INV_ROWS * INV_COLS && g.formY == FORMATION_START_Y + WAVE_LOWER_STEP, "wave: full again, one notch lower");
    CHECK(g.bunker[0][5][5] == 1, "wave: bunkers rebuilt");
    CHECK(g.score == 20 && g.lives == START_LIVES, "wave: score and ships carry over");

    Game deep;
    Game_Init(&deep, 1, 0);
    Game_StartWave(&deep, 40);
    CHECK(deep.formY == FORMATION_START_Y + WAVE_LOWER_MAX * WAVE_LOWER_STEP, "wave: the lowering is capped");
    CHECK(deep.formY + (INV_ROWS - 1) * INV_CELL_H + INV_H < BUNKER_Y, "wave: even the deepest start is above the bunkers");

    /* Extra ship, once. */
    g.score = EXTRA_LIFE_SCORE - 10;
    Silence(&g);
    g.marchTimer = -1e9f;
    Game_InvaderRect(&g, 4, 5, &ix, &iy);
    g.playerX = ix + INV_W / 2.0f;
    g.playerShot.active = false;
    Game_ConsumeFrameFlags(&g);
    for (int i = 0; i < 300 && !g.justExtraLife; i++) { Game_SetInput(&g, 0.0f, true); Game_Step(&g); }
    CHECK(g.justExtraLife && g.lives == START_LIVES + 1, "extra ship: awarded on passing the threshold");
    int lives = g.lives;
    g.score += 5000;
    for (int i = 0; i < 600; i++) { Game_SetInput(&g, 0.0f, true); Game_Step(&g); }
    CHECK(g.lives <= lives, "extra ship: only once per run");
}

static void test_ufo(void) {
    Game g;
    Game_Init(&g, 2, 0);
    g.enemyFireTimer = 1e9f;
    g.marchTimer = -1e9f;
    g.ufoTimer = 0.01f;
    Game_ConsumeFrameFlags(&g);
    RunSteps(&g, 4);
    CHECK(g.ufoActive && g.justUfoAppeared, "ufo: appears when its timer runs out");
    CHECK((g.ufoDir > 0 && g.ufoX < 0.0f) || (g.ufoDir < 0 && g.ufoX >= FIELD_W - 2.0f), "ufo: enters from off-screen");

    /* Put it right above the player and shoot it. */
    g.ufoDir = 1;
    g.playerX = 30.0f; /* left of the formation and the bunkers: a clear line to the top */
    g.ufoX = g.playerX - UFO_W / 2.0f;
    g.shotsFired = 8;                          /* the next shot is the 9th... */
    Game_ConsumeFrameFlags(&g);
    bool killed = false;
    for (int i = 0; i < 200 && !killed; i++) {
        Game_SetInput(&g, 0.0f, i == 0);
        Game_Step(&g);
        if (g.killCount > 0 && g.kills[0].row == -1) killed = true;
        /* keep it over the gun so the test isn't about aiming */
        if (g.ufoActive) g.ufoX = g.playerX - UFO_W / 2.0f;
    }
    CHECK(killed && !g.ufoActive, "ufo: can be shot down");
    CHECK(g.kills[0].points == Game_UfoPoints(9) && g.score == Game_UfoPoints(9), "ufo: value comes from the shot count at the moment of the kill");

    Game h;
    Game_Init(&h, 2, 0);
    h.enemyFireTimer = 1e9f; h.marchTimer = -1e9f; h.ufoTimer = 0.01f;
    RunSteps(&h, 4);
    RunSteps(&h, (int)((FIELD_W + 2 * UFO_W) / UFO_SPEED * STEP_HZ) + 10);
    CHECK(!h.ufoActive && h.ufoTimer > 10.0f, "ufo: leaves the far side and schedules its next pass");

    Game few;
    Game_Init(&few, 2, 0);
    few.enemyFireTimer = 1e9f; few.marchTimer = -1e9f;
    KillAllBut(&few, 0, 0);
    few.ufoTimer = 0.01f;
    RunSteps(&few, 10);
    CHECK(!few.ufoActive, "ufo: stays away once the formation is nearly gone");
}

static void test_update_and_pause(void) {
    Game g;
    Game_Init(&g, 1, 0);
    Silence(&g);
    Game_SetInput(&g, 1.0f, false);
    float x0 = g.playerX;
    Game_Update(&g, STEP_DT * 0.5f);
    CHECK(g.playerX == x0, "update: less than one step of time does nothing yet");
    Game_Update(&g, STEP_DT * 0.6f);
    CHECK(g.playerX > x0, "update: the remainder carries over");
    float x1 = g.playerX;
    Game_Update(&g, 5.0f);
    CHECK(g.playerX - x1 <= PLAYER_SPEED * 0.1f + 1.0f, "update: a long hitch is clamped");
    Game_TogglePause(&g);
    float x2 = g.playerX;
    Game_Update(&g, 0.5f);
    CHECK(g.phase == GS_PAUSED && g.playerX == x2, "pause: nothing moves");
    Game_TogglePause(&g);
    CHECK(g.phase == GS_PLAYING, "pause: toggles back");
}

/* A bot that sits under the nearest column and dodges plays seeded games. */
static long PlayBot(uint64_t seed, int steps, bool *sane) {
    Game g;
    Game_Init(&g, seed, 0);
    for (int i = 0; i < steps && g.phase == GS_PLAYING; i++) {
        float target = g.playerX;
        float best = 1e9f;
        for (int c = 0; c < INV_COLS; c++) {
            for (int r = INV_ROWS - 1; r >= 0; r--) {
                if (!g.alive[r][c]) continue;
                float ix, iy;
                Game_InvaderRect(&g, r, c, &ix, &iy);
                float d = fabsf(ix + INV_W / 2.0f - g.playerX);
                if (d < best) { best = d; target = ix + INV_W / 2.0f + g.marchDir * 10.0f; }
                break;
            }
        }
        float dir = target > g.playerX + 3.0f ? 1.0f : (target < g.playerX - 3.0f ? -1.0f : 0.0f);
        for (int k = 0; k < MAX_ENEMY_SHOTS; k++) {
            const Shot *s = &g.enemyShots[k];
            if (s->active && s->y > PLAYER_Y - 120.0f && fabsf(s->x - g.playerX) < 22.0f) dir = s->x > g.playerX ? -1.0f : 1.0f;
        }
        Game_SetInput(&g, dir, true);
        Game_Step(&g);
        Game_ConsumeFrameFlags(&g);

        if (i % 60 == 0) {
            int counted = 0;
            for (int r = 0; r < INV_ROWS; r++) for (int c = 0; c < INV_COLS; c++) counted += g.alive[r][c] ? 1 : 0;
            if (counted != g.aliveCount) *sane = false;
            if (g.lives < 0 || g.lives > MAX_LIVES) *sane = false;
            if (g.playerX < 0.0f || g.playerX > FIELD_W) *sane = false;
            /* formX is column 0's position even when column 0 is dead, so
             * bound what is actually alive, not the origin. */
            for (int r = 0; r < INV_ROWS; r++) for (int c = 0; c < INV_COLS; c++) {
                if (!g.alive[r][c]) continue;
                float ix, iy;
                Game_InvaderRect(&g, r, c, &ix, &iy);
                if (ix < FORMATION_MARGIN - 0.01f || ix + INV_W > FIELD_W - FORMATION_MARGIN + 0.01f) *sane = false;
            }
        }
    }
    return g.score * 13 + g.wave * 5 + g.lives;
}

static void test_bot_games(void) {
    bool sane = true;
    long total = 0;
    for (uint64_t seed = 1; seed <= 6; seed++) total += PlayBot(seed, 60000, &sane);
    CHECK(sane, "bot: counts, lives and positions stay valid through long games");
    CHECK(total > 0, "bot: it scores");
    bool s2 = true;
    CHECK(PlayBot(3, 30000, &s2) == PlayBot(3, 30000, &s2), "determinism: same seed and inputs, same game");
    CHECK(PlayBot(3, 30000, &s2) != PlayBot(4, 30000, &s2), "determinism: a different seed plays out differently");
}

static void test_powerups(void) {
    Game g;
    Game_Init(&g, 1, 0);
    Silence(&g);
    g.marchTimer = -1e9f;
    g.enemyFireTimer = 1e9f;
    g.ufoTimer = 1e9f;

    /* Rapid: several shots can be in flight, paced by a cooldown. */
    g.rapidTimer = POWER_SECONDS;
    Game_SetInput(&g, 0.0f, true);
    RunSteps(&g, 60);
    CHECK(Game_ActiveShots(&g) >= 2, "rapid: more than one shot in flight");
    g.rapidTimer = 0.0f;
    Game_SetInput(&g, 0.0f, false);
    RunSteps(&g, 300);
    CHECK(Game_ActiveShots(&g) == 0, "rapid: shots clear");
    Game_SetInput(&g, 0.0f, true);
    RunSteps(&g, 20);
    CHECK(Game_ActiveShots(&g) == 1, "rapid: back to one volley at a time when it runs out");

    /* Twin: two shots, side by side. */
    Game_Init(&g, 1, 0);
    Silence(&g);
    g.marchTimer = -1e9f; g.enemyFireTimer = 1e9f; g.ufoTimer = 1e9f;
    g.twinTimer = POWER_SECONDS;
    Game_SetInput(&g, 0.0f, true);
    Game_Step(&g);
    CHECK(Game_ActiveShots(&g) == 2 && g.shotsFired == 1, "twin: one trigger pull, two shots");
    CHECK(g.playerShot.x != g.extraShots[0].x, "twin: shots are offset");

    /* Capsule pickup. */
    Game_Init(&g, 1, 0);
    Silence(&g);
    g.marchTimer = -1e9f; g.enemyFireTimer = 1e9f; g.ufoTimer = 1e9f;
    g.capsules[0] = (Capsule){g.playerX, PLAYER_Y - 20.0f, PU_SHIELD, true};
    Game_SetInput(&g, 0.0f, false);
    RunSteps(&g, 120);
    CHECK(g.shield && !g.capsules[0].active && g.justPowerUp == true, "capsule: catching it grants the power-up");
    /* a capsule that is missed falls away */
    Game_ConsumeFrameFlags(&g);
    g.capsules[1] = (Capsule){10.0f, PLAYER_Y - 20.0f, PU_RAPID, true};
    g.playerX = 400.0f;
    RunSteps(&g, 400);
    CHECK(!g.capsules[1].active && g.rapidTimer <= 0.0f, "capsule: missed capsules leave the field");

    /* Shield eats a hit without costing a ship. */
    Game_ConsumeFrameFlags(&g);
    g.enemyShots[0] = (Shot){g.playerX, PLAYER_Y, true};
    Game_Step(&g);
    CHECK(!g.shield && g.lives == START_LIVES && g.justShieldHit && !g.justPlayerHit, "shield: absorbs one hit");
    g.enemyShots[0] = (Shot){g.playerX, PLAYER_Y, true};
    Game_Step(&g);
    CHECK(g.lives == START_LIVES - 1, "shield: the next hit costs a ship");

    /* Nova wipes the lowest row for its points. */
    Game_Init(&g, 1, 0);
    Silence(&g);
    g.marchTimer = -1e9f; g.enemyFireTimer = 1e9f; g.ufoTimer = 1e9f;
    g.capsules[0] = (Capsule){g.playerX, PLAYER_Y - 5.0f, PU_NOVA, true};
    Game_Step(&g);
    CHECK(g.justNova && g.aliveCount == INV_ROWS * INV_COLS - INV_COLS && g.score == 10 * INV_COLS, "nova: clears the bottom row");

    /* Losing your last ship strips power-ups. */
    g.rapidTimer = 5.0f;
    g.lives = 1;
    g.enemyShots[0] = (Shot){g.playerX, PLAYER_Y, true};
    Game_Step(&g);
    CHECK(g.phase == GS_GAMEOVER && g.rapidTimer == 0.0f, "death: power-ups cleared");

    /* Drops actually happen over a long bot run. */
    bool sane = true;
    PlayBot(9, 60000, &sane);
    CHECK(sane, "powerups: bot run stays sane");
}

int main(void) {
    test_tables();
    test_init();
    test_player();
    test_shooting_invaders();
    test_march();
    test_enemy_fire();
    test_bunkers();
    test_player_death_and_lives();
    test_invasion();
    test_waves_and_extra_life();
    test_ufo();
    test_update_and_pause();
    test_powerups();
    test_bot_games();

    printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
