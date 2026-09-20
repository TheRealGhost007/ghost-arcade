#include "winscale.h"
#include "render.h"
#include "ui.h"
#include "prefs.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* --- Look: a vector monitor. No filled shapes at all: every rock, ship and
 * saucer is a handful of lines, drawn twice -- a wide dim pass in additive
 * blending for the halo, then a thin bright core -- so where lines cross
 * they glow brighter, as the phosphor did. Lime is this machine's light. */
#define kBoardBg (Ui_Theme()->board)
#define kRule (Ui_Theme()->rule)
#define kText (Ui_Theme()->text)
#define kTextDim (Ui_Theme()->dim)
#define kFaint (Ui_Theme()->faint)
#define kLight (Ui_Theme()->light)
#define kDanger (Ui_Theme()->danger)
#define kGold (Ui_Theme()->gold)

static const Color kShipColor = {242, 236, 255, 255};
static const Color kSaucerLarge = {255, 190, 60, 255};
static const Color kSaucerSmall = {255, 110, 180, 255};

#define PI_F 3.14159265358979f

/* ---------------------------------------------------------------- lines */

typedef struct { Vector2 a, b; Color c; } Seg;
#define MAX_SEGS 6000
static Seg sSegs[MAX_SEGS];
static int sSegCount = 0;

static void Line(float x0, float y0, float x1, float y1, Color c) {
    if (sSegCount >= MAX_SEGS) return;
    sSegs[sSegCount++] = (Seg){{x0, y0}, {x1, y1}, c};
}

/* Draws everything queued so far: halo first (additive), then the core. */
static void FlushLines(bool glow) {
    if (glow) {
        BeginBlendMode(BLEND_ADDITIVE);
        for (int i = 0; i < sSegCount; i++) DrawLineEx(sSegs[i].a, sSegs[i].b, 8.0f, Fade(sSegs[i].c, 0.10f));
        for (int i = 0; i < sSegCount; i++) DrawLineEx(sSegs[i].a, sSegs[i].b, 4.0f, Fade(sSegs[i].c, 0.22f));
        EndBlendMode();
    }
    for (int i = 0; i < sSegCount; i++) DrawLineEx(sSegs[i].a, sSegs[i].b, 1.6f, sSegs[i].c);
    sSegCount = 0;
}

/* A closed (or open) outline from local points, rotated by `angle` (radians,
 * clockwise from up) and scaled, placed at (x, y) in field coordinates. */
static void Outline(float x, float y, float angle, float scale, const float pts[][2], int n, bool closed, Color c) {
    float sa = sinf(angle), ca = cosf(angle);
    for (int i = 0; i < n - (closed ? 0 : 1); i++) {
        const float *p = pts[i], *q = pts[(i + 1) % n];
        float ax = (p[0] * ca - p[1] * sa) * scale, ay = (p[0] * sa + p[1] * ca) * scale;
        float bx = (q[0] * ca - q[1] * sa) * scale, by = (q[0] * sa + q[1] * ca) * scale;
        Line((float)FIELD_X + x + ax, (float)FIELD_Y + y + ay, (float)FIELD_X + x + bx, (float)FIELD_Y + y + by, c);
    }
}

/* The field wraps, so anything near an edge is drawn again, shifted by one
 * field width/height, where its far side pokes through. Runs the body once
 * per copy that is actually needed (the unshifted one always is). */
#define FOR_WRAP_OFFSETS(x, y, radius, ox, oy) \
    for (int _i = 0; _i < 9; _i++) \
        for (float ox = (float)(_i % 3 - 1) * FIELD_W, oy = (float)(_i / 3 - 1) * FIELD_H, _once = 1.0f; _once > 0.0f; _once = 0.0f) \
            if (!(((_i % 3 == 0) && (x) < FIELD_W - (radius)) || ((_i % 3 == 2) && (x) > (radius)) || \
                  ((_i / 3 == 0) && (y) < FIELD_H - (radius)) || ((_i / 3 == 2) && (y) > (radius))))

/* A small fighter, nose up: swept wings, a canopy, and two engines at the back.
 * (The old ship was a four-point arrowhead and read as a mouse cursor.) */
#define SHIP_HULL_N 16
static const float kShip[SHIP_HULL_N][2] = {
    {0, -17}, {3, -9}, {4, -3}, {13, 6}, {14, 12}, {9, 10}, {6, 12}, {4, 9}, {0, 10}, {-4, 9}, {-6, 12}, {-9, 10}, {-14, 12}, {-13, 6}, {-4, -3}, {-3, -9},
};
static const float kCanopy[4][2] = {{0, -11}, {2, -6}, {0, -2}, {-2, -6}};
static const float kWingLineR[2][2] = {{5, -1}, {9, 8}};
static const float kWingLineL[2][2] = {{-5, -1}, {-9, 8}};
static const float kEngineR[4][2] = {{3, 10}, {3, 14}, {6, 14}, {6, 12}};
static const float kEngineL[4][2] = {{-3, 10}, {-3, 14}, {-6, 14}, {-6, 12}};
static const float kFlameR[3][2] = {{3, 14}, {4.5f, 23}, {6, 14}};
static const float kFlameL[3][2] = {{-3, 14}, {-4.5f, 23}, {-6, 14}};
static const float kSaucerBody[6][2] = {{-14, 2}, {-6, -4}, {6, -4}, {14, 2}, {6, 8}, {-6, 8}};
static const float kSaucerDome[4][2] = {{-6, -4}, {-3, -9}, {3, -9}, {6, -4}};
static const float kSaucerBelt[2][2] = {{-14, 2}, {14, 2}};

static void DrawRock(const Rock *r) {
    float radii[ROCK_VERTS];
    Game_RockShape(r->shape, radii);
    float nominal = Game_RockRadius(r->size);
    FOR_WRAP_OFFSETS(r->x, r->y, nominal * 1.2f, ox, oy) {
        for (int i = 0; i < ROCK_VERTS; i++) {
            int j = (i + 1) % ROCK_VERTS;
            float a0 = r->angle + (float)i * 2.0f * PI_F / ROCK_VERTS, a1 = r->angle + (float)j * 2.0f * PI_F / ROCK_VERTS;
            Line((float)FIELD_X + r->x + ox + sinf(a0) * nominal * radii[i], (float)FIELD_Y + r->y + oy - cosf(a0) * nominal * radii[i],
                 (float)FIELD_X + r->x + ox + sinf(a1) * nominal * radii[j], (float)FIELD_Y + r->y + oy - cosf(a1) * nominal * radii[j], kLight);
        }
    }
}

static void DrawShip(const Ship *s, float time) {
    if (s->invuln > 0.0f && !Prefs_Get()->reducedFlashing && fmodf(time, 0.24f) < 0.12f) return; /* blink while protected */
    FOR_WRAP_OFFSETS(s->x, s->y, 16.0f, ox, oy) {
        Outline(s->x + ox, s->y + oy, s->angle, 1.3f, kShip, SHIP_HULL_N, true, kShipColor);
        Outline(s->x + ox, s->y + oy, s->angle, 1.3f, kCanopy, 4, true, kShipColor);
        Outline(s->x + ox, s->y + oy, s->angle, 1.3f, kWingLineR, 2, false, kShipColor);
        Outline(s->x + ox, s->y + oy, s->angle, 1.3f, kWingLineL, 2, false, kShipColor);
        Outline(s->x + ox, s->y + oy, s->angle, 1.3f, kEngineR, 4, true, kShipColor);
        Outline(s->x + ox, s->y + oy, s->angle, 1.3f, kEngineL, 4, true, kShipColor);
        if (s->thrusting && fmodf(time, 0.08f) < 0.05f) {
            Outline(s->x + ox, s->y + oy, s->angle, 1.3f, kFlameR, 3, false, kSaucerLarge);
            Outline(s->x + ox, s->y + oy, s->angle, 1.3f, kFlameL, 3, false, kSaucerLarge);
        }
    }
}

static void DrawSaucer(const Saucer *u) {
    float scale = u->small ? 0.62f : 1.0f;
    Color c = u->small ? kSaucerSmall : kSaucerLarge;
    FOR_WRAP_OFFSETS(u->x, u->y, 18.0f, ox, oy) {
        Outline(u->x + ox, u->y + oy, 0.0f, scale, kSaucerBody, 6, true, c);
        Outline(u->x + ox, u->y + oy, 0.0f, scale, kSaucerDome, 4, false, c);
        Outline(u->x + ox, u->y + oy, 0.0f, scale, kSaucerBelt, 2, false, c);
    }
}

/* -------------------------------------------------------------- effects */

typedef struct { float x, y, vx, vy, angle, spin, len, life, maxLife; Color color; } Shard;
#define MAX_SHARDS 160
static Shard sShards[MAX_SHARDS];
static float sShakeAge = 9.0f, sShakeAmp = 0.0f;
static float sDeadFor = 0.0f;
static float sBannerAge = 0.0f;
static int sLastWave = 0;

static void SpawnShards(float x, float y, int count, float speed, float len, Color color) {
    for (int i = 0; i < MAX_SHARDS && count > 0; i++) {
        if (sShards[i].life > 0.0f) continue;
        float dir = (float)GetRandomValue(0, 628) / 100.0f;
        float v = speed * (0.35f + (float)GetRandomValue(0, 70) / 100.0f);
        sShards[i] = (Shard){x, y, sinf(dir) * v, -cosf(dir) * v, (float)GetRandomValue(0, 628) / 100.0f,
                             (float)GetRandomValue(-70, 70) / 10.0f, len * (0.6f + (float)GetRandomValue(0, 60) / 100.0f),
                             0.55f + (float)GetRandomValue(0, 45) / 100.0f, 1.0f, color};
        sShards[i].maxLife = sShards[i].life;
        count--;
    }
}

static void ReactToEvents(const Game *g) {
    for (int i = 0; i < g->killCount; i++) {
        const Kill *k = &g->kills[i];
        switch (k->kind) {
            case KILL_ROCK_LARGE: SpawnShards(k->x, k->y, 9, 90.0f, 12.0f, kLight); break;
            case KILL_ROCK_MEDIUM: SpawnShards(k->x, k->y, 7, 110.0f, 8.0f, kLight); break;
            case KILL_ROCK_SMALL: SpawnShards(k->x, k->y, 5, 130.0f, 5.0f, kLight); break;
            case KILL_SAUCER_LARGE: SpawnShards(k->x, k->y, 12, 140.0f, 10.0f, kSaucerLarge); break;
            case KILL_SAUCER_SMALL: SpawnShards(k->x, k->y, 12, 160.0f, 8.0f, kSaucerSmall); break;
            case KILL_SHIP:
                SpawnShards(k->x, k->y, 8, 120.0f, 14.0f, kShipColor);
                SpawnShards(k->x, k->y, 5, 60.0f, 8.0f, kSaucerLarge);
                sShakeAge = 0.0f;
                sShakeAmp = 7.0f;
                break;
        }
    }
}

static void UpdateAndQueueShards(float dt) {
    for (int i = 0; i < MAX_SHARDS; i++) {
        Shard *s = &sShards[i];
        if (s->life <= 0.0f) continue;
        s->life -= dt;
        s->x += s->vx * dt;
        s->y += s->vy * dt;
        s->angle += s->spin * dt;
        if (s->life <= 0.0f) continue;
        Color c = s->color;
        c.a = (unsigned char)(255.0f * (s->life / s->maxLife));
        float dx = sinf(s->angle) * s->len / 2.0f, dy = -cosf(s->angle) * s->len / 2.0f;
        Line((float)FIELD_X + s->x - dx, (float)FIELD_Y + s->y - dy, (float)FIELD_X + s->x + dx, (float)FIELD_Y + s->y + dy, c);
    }
}

/* ------------------------------------------------------------------ HUD */

static void DrawStat(int x, int y, const char *label, const char *value) {
    Ui_Text(label, x, y, UI_T8, kTextDim);
    Ui_Text(value, x, y + 14, UI_T16, kText);
}

static void DrawHud(const Game *g, bool glow) {
    Ui_Text("ROCKDRIFT", FIELD_X, 16, UI_T16, kLight);

    char buf[64];
    int colW = FIELD_W / 4;
    int statY = 46;
    snprintf(buf, sizeof(buf), "%ld", g->score);
    DrawStat(FIELD_X, statY, "Score", buf);
    snprintf(buf, sizeof(buf), "%ld", g->highScore > g->score ? g->highScore : g->score);
    DrawStat(FIELD_X + colW, statY, "Best", buf);
    snprintf(buf, sizeof(buf), "%d", g->wave);
    DrawStat(FIELD_X + colW * 2, statY, "Wave", buf);

    /* Ships in reserve, drawn as ships. */
    Ui_Text("Ships", FIELD_X + colW * 3, statY, UI_T8, kTextDim);
    for (int i = 0; i < g->lives - 1 && i < MAX_LIVES; i++) {
        float x = (float)(FIELD_X + colW * 3 + 8 + i * 22), y = (float)(statY + 30);
        for (int k = 0; k < SHIP_HULL_N; k++) {
            const float *p = kShip[k], *q = kShip[(k + 1) % SHIP_HULL_N];
            Line(x + p[0] * 0.8f, y + p[1] * 0.8f, x + q[0] * 0.8f, y + q[1] * 0.8f, kShipColor);
        }
    }
    FlushLines(glow);

    Ui_TextCentered("Arrows or WASD fly    Space fires    Shift hyperspace    P pause    Esc menu", UI_T8,
                    WINDOW_WIDTH / 2, WINDOW_HEIGHT - 17, kFaint);
}

static void DrawGameOverOverlay(const Game *g, const FrameInfo *info) {
    DrawRectangle(FIELD_X, FIELD_Y, FIELD_W, FIELD_H, (Color){13, 10, 30, 224});
    int cx = FIELD_X + FIELD_W / 2, cy = FIELD_Y + FIELD_H / 2;

    Ui_TextCentered("GAME OVER", UI_T32, cx, cy - 116, kDanger);
    char buf[96];
    snprintf(buf, sizeof(buf), "You made it to wave %d", g->wave);
    Ui_TextCentered(buf, UI_T8, cx, cy - 68, kTextDim);
    snprintf(buf, sizeof(buf), "%ld", g->score);
    Ui_TextCentered(buf, UI_T48, cx, cy - 38, kText);
    if (g->score >= g->highScore && g->score > 0) {
        Ui_TextCentered("A new best", UI_T16, cx, cy + 34, kLight);
    } else {
        snprintf(buf, sizeof(buf), "Best %ld", g->highScore);
        Ui_TextCentered(buf, UI_T16, cx, cy + 34, kTextDim);
    }
    if (info->lastRank > 0) {
        snprintf(buf, sizeof(buf), "%.16s is #%d on this machine", info->username, info->lastRank);
        Ui_TextCentered(buf, UI_T8, cx, cy + 68, kGold);
    }
    Ui_TextCentered("R plays again    Esc for the menu", UI_T8, cx, cy + 108, kTextDim);
}

void Render_Frame(const Game *g, const FrameInfo *info) {
    float dt = GetFrameTime();
    float time = (float)GetTime();
    ReactToEvents(g);
    sShakeAge += dt;
    sBannerAge += dt;
    if (g->wave != sLastWave) { sLastWave = g->wave; sBannerAge = 0.0f; }
    if (g->phase == GS_GAMEOVER) sDeadFor += dt; else sDeadFor = 0.0f;

    Win_BeginFrame();
    ClearBackground(Ui_Theme()->bg);
    DrawHud(g, info->glow);

    DrawRectangle(FIELD_X - 4, FIELD_Y - 4, FIELD_W + 8, FIELD_H + 8, kRule);
    DrawRectangle(FIELD_X, FIELD_Y, FIELD_W, FIELD_H, kBoardBg);

    Camera2D cam = {0};
    cam.zoom = 1.0f;
    if (Prefs_Get()->screenShake && sShakeAge < 0.35f) {
        int amp = (int)(sShakeAmp * (1.0f - sShakeAge / 0.35f));
        cam.offset = (Vector2){(float)GetRandomValue(-amp, amp), (float)GetRandomValue(-amp, amp)};
    }
    BeginMode2D(cam);
    BeginScissorMode(FIELD_X, FIELD_Y, FIELD_W, FIELD_H);

    for (int i = 0; i < MAX_ROCKS; i++) if (g->rocks[i].active) DrawRock(&g->rocks[i]);
    if (g->saucer.active) DrawSaucer(&g->saucer);
    if (g->ship.alive) DrawShip(&g->ship, time);
    UpdateAndQueueShards(dt);
    FlushLines(info->glow);

    /* Bullets are points, not lines: little squares, the enemy's amber. */
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (g->bullets[i].active) DrawRectangle(FIELD_X + (int)g->bullets[i].x - 1, FIELD_Y + (int)g->bullets[i].y - 1, 3, 3, kShipColor);
    }
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (g->enemyBullets[i].active) DrawRectangle(FIELD_X + (int)g->enemyBullets[i].x - 2, FIELD_Y + (int)g->enemyBullets[i].y - 2, 4, 4, kSaucerLarge);
    }

    EndScissorMode();
    EndMode2D();

    int cx = FIELD_X + FIELD_W / 2;
    if (g->phase == GS_PLAYING && (g->waveTimer > 0.0f || sBannerAge < 1.6f)) {
        char buf[24];
        snprintf(buf, sizeof(buf), "WAVE %d", g->waveTimer > 0.0f ? g->wave + 1 : g->wave);
        int w = Ui_Measure(buf, UI_T32);
        int y = FIELD_Y + 96;
        DrawRectangle(cx - w / 2 - 20, y - 12, w + 40, 56, (Color){13, 10, 30, 225});
        DrawRectangle(cx - w / 2 - 20, y + 40, w + 40, 4, kLight);
        Ui_TextCentered(buf, UI_T32, cx, y, kLight);
    }
    if (g->phase == GS_PLAYING && !g->ship.alive && g->ship.respawnTimer <= 0.0f) {
        Ui_TextCentered("Waiting for a clear spot", UI_T8, cx, FIELD_Y + FIELD_H - 24, kTextDim);
    }
    if (g->phase == GS_PAUSED) Ui_PauseOverlay(FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    if (g->phase == GS_GAMEOVER && sDeadFor > 1.0f) DrawGameOverOverlay(g, info);

    if (info->showFps) DrawFPS(10, WINDOW_HEIGHT - 24);
    Win_EndFrame();
}

/* Rock outlines drifting behind the menu: a handful of floats, no storage
 * beyond a fixed array, and only ever drawn on the menu. */
void Render_MenuBackdrop(float dt) {
    typedef struct { float x, y, vx, vy, angle, spin, r; unsigned shape; } Drift;
    static Drift d[7];
    static bool init = false;
    if (!init) {
        for (int i = 0; i < 7; i++) {
            d[i] = (Drift){(float)GetRandomValue(0, WINDOW_WIDTH), (float)GetRandomValue(0, WINDOW_HEIGHT),
                           (float)GetRandomValue(-30, 30), (float)GetRandomValue(-24, 24), 0.0f, (float)GetRandomValue(-8, 8) / 10.0f,
                           (float)(i % 3 == 0 ? 60 : (i % 3 == 1 ? 34 : 18)), (unsigned)(i * 91 + 7)};
        }
        init = true;
    }
    for (int i = 0; i < 7; i++) {
        Drift *k = &d[i];
        k->x = fmodf(k->x + k->vx * dt + WINDOW_WIDTH, WINDOW_WIDTH);
        k->y = fmodf(k->y + k->vy * dt + WINDOW_HEIGHT, WINDOW_HEIGHT);
        k->angle += k->spin * dt;
        float radii[ROCK_VERTS];
        Game_RockShape(k->shape, radii);
        Color c = kLight;
        c.a = 46;
        for (int v = 0; v < ROCK_VERTS; v++) {
            int w = (v + 1) % ROCK_VERTS;
            float a0 = k->angle + (float)v * 2.0f * PI_F / ROCK_VERTS, a1 = k->angle + (float)w * 2.0f * PI_F / ROCK_VERTS;
            DrawLineEx((Vector2){k->x + sinf(a0) * k->r * radii[v], k->y - cosf(a0) * k->r * radii[v]},
                       (Vector2){k->x + sinf(a1) * k->r * radii[w], k->y - cosf(a1) * k->r * radii[w]}, 2.0f, c);
        }
    }
}
