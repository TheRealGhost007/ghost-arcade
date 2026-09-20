#include "winscale.h"
#include "render.h"
#include "ui.h"
#include "prefs.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* --- Look: this window is the screen of the Brickburst cabinet in Ghost
 * Launcher's arcade hall: the hall's indigo, with orange as the machine's
 * own light. The wall of bricks is the colourful thing on screen, so the
 * chrome around it stays quiet. */
/* The shared palette lives in ghost-common's ui.c; these just read it. */
#define kBg (Ui_Theme()->bg)
#define kBoardBg (Ui_Theme()->board)
#define kPanel (Ui_Theme()->panel)
#define kRule (Ui_Theme()->rule)
#define kText (Ui_Theme()->text)
#define kTextDim (Ui_Theme()->dim)
#define kFaint (Ui_Theme()->faint)
#define kLight (Ui_Theme()->light) /* this game's own colour */
#define kGold (Ui_Theme()->gold)
#define kDanger (Ui_Theme()->danger)
static const Color kSteel = {120, 112, 170, 255};
static const Color kBall = {255, 246, 224, 255};

/* One colour per brick row, cycling. */
static const Color kRowColors[6] = {
    {255, 79, 120, 255}, {255, 150, 60, 255}, {255, 210, 63, 255},
    {96, 220, 130, 255}, {33, 212, 200, 255}, {160, 136, 255, 255},
};

static const Color kPowerupColors[PU_COUNT] = {
    {33, 212, 200, 255}, {96, 220, 130, 255}, {110, 160, 255, 255}, {255, 150, 60, 255}, {255, 79, 120, 255},
    {255, 236, 90, 255}, {190, 130, 255, 255}, {150, 230, 255, 255},
};
static const char *kPowerupLetters[PU_COUNT] = {"x3", "W", "S", "F", "+1", "L", "G", "U"};

static int BrickPxX(int col) { return FIELD_X + col * BRICK_W; }
static int BrickPxY(int row) { return FIELD_Y + BRICK_TOP + row * BRICK_H; }

static Color BrickColor(int row, uint8_t type, uint8_t hp) {
    if (type == BRICK_STEEL) return kSteel;
    if (type == BRICK_BOMB) return (Color){120, 28, 40, 255};
    /* Tough bricks start dark and brighten to their row colour as they
     * take hits, so how much is left is readable at a glance. */
    Color base = kRowColors[row % 6];
    if (hp >= 3) return Ui_Lerp(base, BLACK, 0.50f);
    if (hp == 2) return Ui_Lerp(base, BLACK, 0.26f);
    return base;
}

static void DrawBrick(int col, int row, uint8_t type, uint8_t hp, float time) {
    int x = BrickPxX(col) + 1, y = BrickPxY(row) + 1, w = BRICK_W - 2, h = BRICK_H - 2;
    Ui_BevelRect(x, y, w, h, BrickColor(row, type, hp));

    if (type == BRICK_STEEL) { /* rivets */
        Color rivet = Ui_Lerp(kSteel, BLACK, 0.45f);
        DrawRectangle(x + 5, y + 7, 4, 4, rivet);
        DrawRectangle(x + w - 9, y + 7, 4, 4, rivet);
    } else if (type == BRICK_BOMB) { /* a fuse-lit core */
        float pulse = 0.5f + 0.5f * sinf(time * 7.0f + (float)(col * 3 + row));
        DrawRectangle(x + w / 2 - 5, y + h / 2 - 4, 10, 8, Ui_Lerp(kDanger, kGold, pulse));
    } else if (hp >= 2) { /* one notch per extra hit it can still take */
        Color notch = Ui_Lerp(BrickColor(row, type, hp), BLACK, 0.4f);
        for (int i = 0; i < hp - 1; i++) DrawRectangle(x + w / 2 - 6 + i * 8, y + h / 2 - 2, 4, 4, notch);
    }
}

/* ---------------------------------------------------------------- effects
 * Render-only reactions to the one-frame events the rules raise. */
#define TRAIL_LEN 7
static struct {
    float trailX[MAX_BALLS][TRAIL_LEN], trailY[MAX_BALLS][TRAIL_LEN];
    int trailCount[MAX_BALLS];
    float shakeAge, shakeAmp;
    float paddleHitAge;
    float lifeLostAge;
} sFx = {.shakeAge = 9, .paddleHitAge = 9, .lifeLostAge = 9};

#define SHAKE_TIME 0.28f

static void ReactToEvents(const Game *g) {
    for (int i = 0; i < g->brokenCount; i++) {
        const BrokenBrick *b = &g->broken[i];
        Color c = b->type == BRICK_BOMB ? kGold : kRowColors[b->row % 6];
        Ui_Burst((float)(BrickPxX(b->col) + BRICK_W / 2), (float)(BrickPxY(b->row) + BRICK_H / 2),
                   c, b->type == BRICK_BOMB ? 14 : 7, b->type == BRICK_BOMB ? 300.0f : 190.0f);
    }
    if (g->justExploded) {
        sFx.shakeAge = 0.0f;
        sFx.shakeAmp = 9.0f;
    }
    if (g->justPaddleHit) sFx.paddleHitAge = 0.0f;
    if (g->justLifeLost) {
        sFx.lifeLostAge = 0.0f;
        if (sFx.shakeAge >= SHAKE_TIME) { sFx.shakeAge = 0.0f; sFx.shakeAmp = 5.0f; }
        for (int i = 0; i < MAX_BALLS; i++) sFx.trailCount[i] = 0;
    }
    if (g->justLevelClear) {
        for (int i = 0; i < MAX_BALLS; i++) sFx.trailCount[i] = 0;
    }
    if (g->justPowerup >= 0) {
        Ui_Burst((float)FIELD_X + g->paddleX, (float)FIELD_Y + PADDLE_Y, kPowerupColors[g->justPowerup], 16, 220.0f);
    }
}

static void DrawBall(float fx, float fy, bool fire, float time) {
    int x = FIELD_X + (int)roundf(fx), y = FIELD_Y + (int)roundf(fy);
    Color c = fire ? Ui_Lerp(kLight, kGold, 0.5f + 0.5f * sinf(time * 30.0f)) : kBall;
    /* A 10px pixel-circle: a plus-shaped stack of three rectangles. */
    DrawRectangle(x - 3, y - 5, 6, 10, c);
    DrawRectangle(x - 5, y - 3, 10, 6, c);
    DrawRectangle(x - 4, y - 4, 8, 8, c);
    if (!fire) DrawRectangle(x - 3, y - 3, 2, 2, WHITE);
}

static void DrawField(const Game *g, float dt) {
    float time = (float)GetTime();
    sFx.shakeAge += dt;
    sFx.paddleHitAge += dt;
    sFx.lifeLostAge += dt;

    Camera2D cam = {0};
    cam.zoom = 1.0f;
    if (Prefs_Get()->screenShake && sFx.shakeAge < SHAKE_TIME) {
        int amp = (int)(sFx.shakeAmp * (1.0f - sFx.shakeAge / SHAKE_TIME));
        cam.offset = (Vector2){(float)GetRandomValue(-amp, amp), (float)GetRandomValue(-amp, amp)};
    }
    BeginMode2D(cam);

    /* Frame: open at the bottom, because that is where the ball is lost. */
    Color frame = sFx.lifeLostAge < 0.35f ? Ui_Lerp(kDanger, kRule, sFx.lifeLostAge / 0.35f) : kRule;
    DrawRectangle(FIELD_X - 4, FIELD_Y - 4, FIELD_W + 8, 4, frame);
    DrawRectangle(FIELD_X - 4, FIELD_Y - 4, 4, FIELD_H + 4, frame);
    DrawRectangle(FIELD_X + FIELD_W, FIELD_Y - 4, 4, FIELD_H + 4, frame);
    DrawRectangle(FIELD_X, FIELD_Y, FIELD_W, FIELD_H, kBoardBg);

    for (int r = 0; r < BRICK_ROWS; r++) {
        for (int c = 0; c < BRICK_COLS; c++) {
            if (g->brick[r][c] != BRICK_NONE) DrawBrick(c, r, g->brick[r][c], g->hp[r][c], time);
        }
    }

    /* Capsules. */
    for (int i = 0; i < MAX_POWERUPS; i++) {
        const Powerup *p = &g->powerups[i];
        if (!p->active) continue;
        int x = FIELD_X + (int)roundf(p->x - POWERUP_W / 2), y = FIELD_Y + (int)roundf(p->y - POWERUP_H / 2);
        Color c = kPowerupColors[p->type];
        DrawRectangle(x + 2, y, (int)POWERUP_W - 4, (int)POWERUP_H, c);
        DrawRectangle(x, y + 2, (int)POWERUP_W, (int)POWERUP_H - 4, c);
        DrawRectangle(x + 2, y + 2, (int)POWERUP_W - 4, 2, Ui_Lerp(c, WHITE, 0.5f));
        Ui_TextCentered(kPowerupLetters[p->type], UI_T8, x + (int)POWERUP_W / 2, y + 3, kBoardBg);
    }

    /* Laser bolts. */
    for (int i = 0; i < MAX_SHOTS; i++) {
        if (!g->shots[i].active) continue;
        int sx = FIELD_X + (int)g->shots[i].x, sy = FIELD_Y + (int)g->shots[i].y;
        DrawRectangle(sx - 1, sy - 8, 3, 12, kPowerupColors[PU_LASER]);
        DrawRectangle(sx - 3, sy - 2, 7, 4, Fade(kPowerupColors[PU_LASER], 0.35f));
    }
    /* The shield: a bright floor while it is up. */
    if (g->shield) {
        float pulse = 0.6f + 0.4f * sinf(time * 8.0f);
        DrawRectangle(FIELD_X, FIELD_Y + (int)SHIELD_Y, FIELD_W, 3, Fade(kPowerupColors[PU_SHIELD], 0.5f + 0.4f * pulse));
        DrawRectangle(FIELD_X, FIELD_Y + (int)SHIELD_Y - 4, FIELD_W, 4, Fade(kPowerupColors[PU_SHIELD], 0.12f));
    }

    /* Paddle: dips a few pixels for a moment when the ball lands on it. */
    {
        int dip = sFx.paddleHitAge < 0.09f ? 3 : 0;
        int w = (int)g->paddleW;
        int x = FIELD_X + (int)roundf(g->paddleX) - w / 2, y = FIELD_Y + (int)PADDLE_Y + dip;
        Ui_BevelRect(x, y, w, (int)PADDLE_H, (Color){228, 222, 244, 255});
        if (g->laserTimer > 0.0f) {
            DrawRectangle(x + 2, y - 5, 4, 6, kPowerupColors[PU_LASER]);
            DrawRectangle(x + w - 6, y - 5, 4, 6, kPowerupColors[PU_LASER]);
        }
        if (g->stickyTimer > 0.0f) DrawRectangle(x, y - 2, w, 3, kPowerupColors[PU_STICKY]);
        Color cap = g->fireTimer > 0.0f ? kGold : kLight;
        DrawRectangle(x, y, 8, (int)PADDLE_H, cap);
        DrawRectangle(x + w - 8, y, 8, (int)PADDLE_H, cap);
    }

    /* Balls, each dragging a short fading trail. */
    bool fire = g->fireTimer > 0.0f;
    for (int i = 0; i < MAX_BALLS; i++) {
        const Ball *b = &g->balls[i];
        if (!b->active || b->stuck) {
            sFx.trailCount[i] = 0;
            if (b->active) DrawBall(b->x, b->y, fire, time);
            continue;
        }
        if (g->phase == GS_PLAYING) {
            for (int k = TRAIL_LEN - 1; k > 0; k--) {
                sFx.trailX[i][k] = sFx.trailX[i][k - 1];
                sFx.trailY[i][k] = sFx.trailY[i][k - 1];
            }
            sFx.trailX[i][0] = b->x;
            sFx.trailY[i][0] = b->y;
            if (sFx.trailCount[i] < TRAIL_LEN) sFx.trailCount[i]++;
        }
        for (int k = sFx.trailCount[i] - 1; k >= 1; k--) {
            int s = 8 - k;
            Color c = fire ? kLight : kBall;
            c.a = (unsigned char)(110 - k * 14);
            DrawRectangle(FIELD_X + (int)sFx.trailX[i][k] - s / 2, FIELD_Y + (int)sFx.trailY[i][k] - s / 2, s, s, c);
        }
        DrawBall(b->x, b->y, fire, time);
    }

    Ui_UpdateParticles(dt);
    EndMode2D();
}

static void DrawStat(int x, int y, const char *label, const char *value) {
    Ui_Text(label, x, y, UI_T8, kTextDim);
    Ui_Text(value, x, y + 14, UI_T16, kText);
}

static void DrawTimerBar(int x, int y, const char *label, float left, float full, Color color) {
    Ui_Text(label, x, y, UI_T8, color);
    int w = 96;
    DrawRectangle(x, y + 12, w, 4, kRule);
    DrawRectangle(x, y + 12, (int)((float)w * left / full), 4, color);
}

static void DrawHud(const Game *g) {
    Ui_Text("BRICKBURST", FIELD_X, 16, UI_T16, kLight);

    char buf[64];
    int colW = FIELD_W / 4;
    int statY = 46;

    snprintf(buf, sizeof(buf), "%ld", g->score);
    DrawStat(FIELD_X, statY, "Score", buf);
    snprintf(buf, sizeof(buf), "%ld", g->highScore > g->score ? g->highScore : g->score);
    DrawStat(FIELD_X + colW, statY, "Best", buf);
    snprintf(buf, sizeof(buf), "%d", g->level);
    DrawStat(FIELD_X + colW * 2, statY, "Level", buf);

    /* Paddles in reserve, drawn as paddles. */
    Ui_Text("Paddles", FIELD_X + colW * 3, statY, UI_T8, kTextDim);
    for (int i = 0; i < g->lives; i++) {
        int x = FIELD_X + colW * 3 + i * 24;
        DrawRectangle(x, statY + 18, 20, 6, (Color){228, 222, 244, 255});
        DrawRectangle(x, statY + 18, 3, 6, kLight);
        DrawRectangle(x + 17, statY + 18, 3, 6, kLight);
    }

    /* What is running out, and how fast. */
    int tx = FIELD_X, ty = 84;
    if (g->wideTimer > 0.0f) { DrawTimerBar(tx, ty, "Wide", g->wideTimer, WIDE_SECONDS, kPowerupColors[PU_WIDE]); tx += 120; }
    if (g->slowTimer > 0.0f) { DrawTimerBar(tx, ty, "Slow", g->slowTimer, SLOW_SECONDS, kPowerupColors[PU_SLOW]); tx += 120; }
    if (g->fireTimer > 0.0f) { DrawTimerBar(tx, ty, "Fireball", g->fireTimer, FIRE_SECONDS, kPowerupColors[PU_FIRE]); tx += 120; }
    if (g->laserTimer > 0.0f) { DrawTimerBar(tx, ty, "Laser", g->laserTimer, LASER_SECONDS, kPowerupColors[PU_LASER]); tx += 120; }
    if (g->stickyTimer > 0.0f) { DrawTimerBar(tx, ty, "Sticky", g->stickyTimer, STICKY_SECONDS, kPowerupColors[PU_STICKY]); tx += 120; }
    if (Game_ComboMultiplier(g) > 1) {
        char cb[16];
        snprintf(cb, sizeof(cb), "x%d", Game_ComboMultiplier(g));
        Ui_TextRight(cb, UI_T16, FIELD_X + FIELD_W, ty - 4, kGold);
    }

    Ui_TextCentered("Arrows, A/D or mouse    Space launches    P pause    Esc menu", UI_T8,
                  WINDOW_WIDTH / 2, WINDOW_HEIGHT - 17, kFaint);
}

static void DrawLaunchPrompt(const Game *g) {
    int cx = FIELD_X + FIELD_W / 2;
    { /* while the ball is parked there is time to read: always say which level this is */
        char buf[32];
        snprintf(buf, sizeof(buf), "LEVEL %d", g->level);
        int y = FIELD_Y + 430;
        int w = Ui_Measure(buf, UI_T32);
        DrawRectangle(cx - w / 2 - 20, y - 12, w + 40, 56, (Color){13, 10, 30, 225});
        DrawRectangle(cx - w / 2 - 20, y + 40, w + 40, 4, kLight);
        Ui_TextCentered(buf, UI_T32, cx, y, kLight);
    }
    if (fmod(GetTime(), 1.2) < 0.8) Ui_TextCentered("Space or click launches", UI_T8, cx, FIELD_Y + 502, kTextDim);
}

static void DrawGameOverOverlay(const Game *g, const FrameInfo *info) {
    DrawRectangle(FIELD_X, FIELD_Y, FIELD_W, FIELD_H, (Color){13, 10, 30, 222});
    int cx = FIELD_X + FIELD_W / 2;
    int cy = FIELD_Y + FIELD_H / 2;

    Ui_TextCentered("GAME OVER", UI_T32, cx, cy - 120, kDanger);
    char buf[96];
    snprintf(buf, sizeof(buf), "You reached level %d", g->level);
    Ui_TextCentered(buf, UI_T8, cx, cy - 72, kTextDim);

    snprintf(buf, sizeof(buf), "%ld", g->score);
    Ui_TextCentered(buf, UI_T48, cx, cy - 40, kText);

    if (g->score >= g->highScore && g->score > 0) {
        Ui_TextCentered("A new best", UI_T16, cx, cy + 36, kLight);
    } else {
        snprintf(buf, sizeof(buf), "Best %ld", g->highScore);
        Ui_TextCentered(buf, UI_T16, cx, cy + 36, kTextDim);
    }
    if (info->lastRank > 0) {
        snprintf(buf, sizeof(buf), "%.16s is #%d on this machine", info->username, info->lastRank);
        Ui_TextCentered(buf, UI_T8, cx, cy + 72, kGold);
    }
    Ui_TextCentered("R plays again    Esc for the menu", UI_T8, cx, cy + 112, kTextDim);
}

/* --- Menu background: a faint wall and one ball rattling around under it.
 * A few floats, no allocation; only drawn on the menu. */
static struct { float x, y, vx, vy; bool init; } sMenuBall;

void Render_MenuBackdrop(float dt) {
    if (!sMenuBall.init) {
        sMenuBall.x = 200.0f; sMenuBall.y = 420.0f; sMenuBall.vx = 170.0f; sMenuBall.vy = -210.0f;
        sMenuBall.init = true;
    }
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 14; c++) {
            Color col = kRowColors[r % 6];
            col.a = 28;
            DrawRectangle(2 + c * 44 + 1, 196 + r * 20 + 1, 42, 18, col);
        }
    }
    sMenuBall.x += sMenuBall.vx * dt;
    sMenuBall.y += sMenuBall.vy * dt;
    if (sMenuBall.x < 8 || sMenuBall.x > WINDOW_WIDTH - 8) sMenuBall.vx = -sMenuBall.vx;
    if (sMenuBall.y < 284 || sMenuBall.y > WINDOW_HEIGHT - 8) sMenuBall.vy = -sMenuBall.vy;
    if (sMenuBall.x < 8) sMenuBall.x = 8;
    if (sMenuBall.x > WINDOW_WIDTH - 8) sMenuBall.x = WINDOW_WIDTH - 8;
    if (sMenuBall.y < 284) sMenuBall.y = 284;
    if (sMenuBall.y > WINDOW_HEIGHT - 8) sMenuBall.y = WINDOW_HEIGHT - 8;
    Color b = kBall;
    b.a = 70;
    DrawRectangle((int)sMenuBall.x - 5, (int)sMenuBall.y - 5, 10, 10, b);
}

void Render_Frame(const Game *g, const FrameInfo *info) {
    float dt = GetFrameTime();
    ReactToEvents(g);

    Win_BeginFrame();
    ClearBackground(kBg);

    DrawHud(g);
    DrawField(g, dt);

    bool waiting = false;
    for (int i = 0; i < MAX_BALLS; i++) if (g->balls[i].active && g->balls[i].stuck) waiting = true;
    if (g->phase == GS_PLAYING && waiting) DrawLaunchPrompt(g);
    if (g->phase == GS_PAUSED) Ui_PauseOverlay(FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    if (g->phase == GS_GAMEOVER) DrawGameOverOverlay(g, info);

    if (info->showFps) DrawFPS(10, WINDOW_HEIGHT - 24);

    Win_EndFrame();
}
