#include "winscale.h"
#include "render.h"
#include "ui.h"
#include "prefs.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* --- Look: a vector monitor, like Rockdrift. No filled shapes: every line is
 * drawn twice -- a wide dim additive pass for the halo, then a thin bright
 * core. Amber is this machine's light (the moon itself); the lander is
 * near-white, the pads a hot white-yellow, and the readouts turn red when a
 * number is past what a landing can survive. */
#define kBoardBg (Ui_Theme()->board)
#define kRule (Ui_Theme()->rule)
#define kText (Ui_Theme()->text)
#define kTextDim (Ui_Theme()->dim)
#define kFaint (Ui_Theme()->faint)
#define kLight (Ui_Theme()->light)
#define kDanger (Ui_Theme()->danger)
#define kGold (Ui_Theme()->gold)

static const Color kShipColor = {242, 236, 255, 255};
static const Color kPadColor = {255, 244, 190, 255};
static const Color kFlameColor = {255, 150, 70, 255};
static const Color kSafe = {120, 230, 150, 255};

#define PI_F 3.14159265358979f

/* ---------------------------------------------------------------- lines */

typedef struct { Vector2 a, b; Color c; } Seg;
#define MAX_SEGS 4000
static Seg sSegs[MAX_SEGS];
static int sSegCount = 0;

static void Line(float x0, float y0, float x1, float y1, Color c) {
    if (sSegCount >= MAX_SEGS) return;
    sSegs[sSegCount++] = (Seg){{x0, y0}, {x1, y1}, c};
}

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

/* ---------------------------------------------------------------- camera */

/* World -> screen. The camera sits still over the whole moon until the ship
 * gets low, then eases in and follows it. */
static struct { float zoom, cx, cy; } sCam = {1.0f, FIELD_W / 2.0f, FIELD_H / 2.0f};

static float SX(float wx) { return (float)FIELD_X + FIELD_W / 2.0f + (wx - sCam.cx) * sCam.zoom; }
static float SY(float wy) { return (float)FIELD_Y + FIELD_H / 2.0f + (wy - sCam.cy) * sCam.zoom; }

static void FollowShip(const Game *g, float dt) {
    float target = 1.0f;
    if (g->phase != GS_GAMEOVER && g->state != SHIP_CRASHED) {
        float alt = Game_Altitude(g);
        if (alt < 130.0f) target = 1.0f + (1.0f - alt / 130.0f) * 1.6f; /* ramps up to 2.6x on the ground */
        if (target < 1.0f) target = 1.0f;
    }
    if (g->state == SHIP_LANDED) target = sCam.zoom;
    sCam.zoom += (target - sCam.zoom) * fminf(1.0f, dt * 4.0f);
    if (fabsf(sCam.zoom - 1.0f) < 0.005f) sCam.zoom = 1.0f;

    float halfH = FIELD_H / 2.0f / sCam.zoom;
    if (sCam.zoom <= 1.001f) {
        sCam.cx = FIELD_W / 2.0f;
        sCam.cy = FIELD_H / 2.0f;
    } else {
        sCam.cx = g->x;
        sCam.cy = g->y + halfH * 0.25f; /* keep the ground in view */
        if (sCam.cy < halfH) sCam.cy = halfH;
        if (sCam.cy > FIELD_H - halfH) sCam.cy = FIELD_H - halfH;
    }
}

/* ---------------------------------------------------------------- shapes */

static void Outline(float wx, float wy, float angle, const float pts[][2], int n, bool closed, Color c) {
    float sa = sinf(angle), ca = cosf(angle), z = sCam.zoom;
    float ox = SX(wx), oy = SY(wy);
    for (int i = 0; i < n - (closed ? 0 : 1); i++) {
        const float *p = pts[i], *q = pts[(i + 1) % n];
        Line(ox + (p[0] * ca - p[1] * sa) * z, oy + (p[0] * sa + p[1] * ca) * z,
             ox + (q[0] * ca - q[1] * sa) * z, oy + (q[0] * sa + q[1] * ca) * z, c);
    }
}

static const float kHull[6][2] = {{-5, -12}, {5, -12}, {9, -6}, {9, 2}, {-9, 2}, {-9, -6}};
static const float kWindow[4][2] = {{-3, -8}, {3, -8}, {3, -4}, {-3, -4}};
static const float kLegL[2][2] = {{-9, 2}, {-8, 10}};
static const float kLegR[2][2] = {{9, 2}, {8, 10}};
static const float kFootL[2][2] = {{-12, 10}, {-4, 10}};
static const float kFootR[2][2] = {{4, 10}, {12, 10}};
static const float kNozzle[3][2] = {{-3, 2}, {0, 5}, {3, 2}};

static void DrawShip(const Game *g, float time) {
    if (g->state == SHIP_CRASHED) return;
    Outline(g->x, g->y, g->angle, kHull, 6, true, kShipColor);
    Outline(g->x, g->y, g->angle, kWindow, 4, true, kShipColor);
    Outline(g->x, g->y, g->angle, kLegL, 2, false, kShipColor);
    Outline(g->x, g->y, g->angle, kLegR, 2, false, kShipColor);
    Outline(g->x, g->y, g->angle, kFootL, 2, false, kShipColor);
    Outline(g->x, g->y, g->angle, kFootR, 2, false, kShipColor);
    Outline(g->x, g->y, g->angle, kNozzle, 3, false, kShipColor);
    if (g->thrusting && g->state == SHIP_FLYING) {
        float len = 9.0f + 6.0f * (0.5f + 0.5f * sinf(time * 90.0f));
        float flame[3][2] = {{-3, 5}, {0, 5 + len}, {3, 5}};
        Outline(g->x, g->y, g->angle, flame, 3, false, kFlameColor);
        float inner[3][2] = {{-1.5f, 5}, {0, 5 + len * 0.55f}, {1.5f, 5}};
        Outline(g->x, g->y, g->angle, inner, 3, false, kPadColor);
    }
}

/* ---------------------------------------------------------- pods & wind */

static void DrawPods(const Game *g, float time) {
    for (int i = 0; i < g->podCount; i++) {
        const Pod *p = &g->pods[i];
        if (p->taken) continue;
        float pulse = 1.0f + 0.12f * sinf(time * 4.0f + (float)i * 2.0f);
        float rad = POD_RADIUS * 0.75f * pulse;
        for (int off = -1; off <= 1; off++) {
            float x = p->x + (float)off * FIELD_W;
            if (fabsf(x - sCam.cx) > FIELD_W / 2.0f / sCam.zoom + 30.0f) continue;
            for (int k = 0; k < 8; k++) {
                float a0 = (float)k * 0.7854f + time * 0.6f, a1 = (float)(k + 1) * 0.7854f + time * 0.6f;
                Line(SX(x + cosf(a0) * rad), SY(p->y + sinf(a0) * rad), SX(x + cosf(a1) * rad), SY(p->y + sinf(a1) * rad), kGold);
            }
            Line(SX(x - 4.0f), SY(p->y), SX(x + 4.0f), SY(p->y), kPadColor);
            Line(SX(x), SY(p->y - 4.0f), SX(x), SY(p->y + 4.0f), kPadColor);
        }
    }
}

static void DrawWindStreaks(const Game *g, float time) {
    float wind = Game_Wind(g);
    if (fabsf(wind) < 0.4f) return;
    for (int i = 0; i < 16; i++) {
        unsigned h = (unsigned)i * 2654435761u;
        float speed = wind * 22.0f;
        float x = fmodf((float)(h % (unsigned)FIELD_W) + speed * time, (float)FIELD_W);
        if (x < 0.0f) x += FIELD_W;
        float y = 30.0f + (float)((h >> 12) % 260u);
        float len = 10.0f + fabsf(wind) * 3.0f;
        Line(SX(sCam.cx - FIELD_W / 2.0f / sCam.zoom + x), SY(y), SX(sCam.cx - FIELD_W / 2.0f / sCam.zoom + x + (wind > 0 ? len : -len)), SY(y), Fade(kLight, 0.18f));
    }
}

/* -------------------------------------------------------------- terrain */

static float PointY(const Game *g, int idx) { return g->terrain[((idx % (TERR_N - 1)) + (TERR_N - 1)) % (TERR_N - 1)]; }

static struct { float x, y; int mult; } sLabels[MAX_PADS * 3];
static int sLabelCount = 0;

static void DrawTerrain(const Game *g) {
    float half = FIELD_W / 2.0f / sCam.zoom + TERR_STEP * 2.0f;
    int i0 = (int)floorf((sCam.cx - half) / TERR_STEP), i1 = (int)ceilf((sCam.cx + half) / TERR_STEP);
    sLabelCount = 0;
    for (int i = i0; i < i1; i++) {
        float x0 = (float)(i * TERR_STEP), x1 = (float)((i + 1) * TERR_STEP);
        float y0 = PointY(g, i), y1 = PointY(g, i + 1);
        int local = ((i % (TERR_N - 1)) + (TERR_N - 1)) % (TERR_N - 1);
        bool pad = false;
        for (int p = 0; p < g->padCount; p++) if (local >= g->pads[p].start && local < g->pads[p].start + g->pads[p].points - 1) pad = true;
        Line(SX(x0), SY(y0), SX(x1), SY(y1), pad ? kPadColor : kLight);
        /* a faint contour under the surface gives the ground some depth */
        Line(SX(x0), SY(y0) + 18.0f * sCam.zoom, SX(x1), SY(y1) + 18.0f * sCam.zoom, Fade(kLight, 0.13f));
    }
    for (int p = 0; p < g->padCount; p++) {
        const Pad *pd = &g->pads[p];
        float px0 = Game_PadX0(pd), px1 = Game_PadX1(pd), y = g->terrain[pd->start];
        for (int copy = -1; copy <= 1; copy++) {
            float off = (float)copy * FIELD_W;
            if (px1 + off < sCam.cx - half || px0 + off > sCam.cx + half) continue;
            Line(SX(px0 + off), SY(y), SX(px0 + off), SY(y) - 7.0f * sCam.zoom, kPadColor);
            Line(SX(px1 + off), SY(y), SX(px1 + off), SY(y) - 7.0f * sCam.zoom, kPadColor);
            if (sLabelCount < MAX_PADS * 3) {
                sLabels[sLabelCount].x = SX((px0 + px1) * 0.5f + off);
                sLabels[sLabelCount].y = SY(y) + 6.0f;
                sLabels[sLabelCount].mult = pd->mult;
                sLabelCount++;
            }
        }
    }
}

/* ---------------------------------------------------------------- effects */

typedef struct { float x, y, vx, vy, angle, spin, len, life, maxLife; Color color; } Shard;
#define MAX_SHARDS 120
static Shard sShards[MAX_SHARDS];
static float sShakeAge = 9.0f, sShakeAmp = 0.0f;
static float sBannerAge = 9.0f;
static int sLastLevel = 0;
static float sDeadFor = 0.0f;
static float sLowFuelAge = 9.0f;

static void SpawnShards(float x, float y, int count, float speed, float len, Color color) {
    for (int i = 0; i < MAX_SHARDS && count > 0; i++) {
        if (sShards[i].life > 0.0f) continue;
        float dir = (float)GetRandomValue(0, 628) / 100.0f;
        float v = speed * (0.35f + (float)GetRandomValue(0, 70) / 100.0f);
        sShards[i] = (Shard){x, y, sinf(dir) * v, -cosf(dir) * v - 20.0f, (float)GetRandomValue(0, 628) / 100.0f,
                             (float)GetRandomValue(-70, 70) / 10.0f, len * (0.6f + (float)GetRandomValue(0, 60) / 100.0f),
                             0.7f + (float)GetRandomValue(0, 60) / 100.0f, 1.0f, color};
        sShards[i].maxLife = sShards[i].life;
        count--;
    }
}

static void ReactToEvents(const Game *g) {
    if (g->justCrash) {
        SpawnShards(g->x, g->y, 10, 90.0f, 12.0f, kShipColor);
        SpawnShards(g->x, g->y, 6, 60.0f, 8.0f, kFlameColor);
        sShakeAge = 0.0f;
        sShakeAmp = 6.0f;
    }
    if (g->justLand) SpawnShards(g->x, g->y + FOOT_Y, 6, 40.0f, 5.0f, kPadColor);
    if (g->justLowFuel) sLowFuelAge = 0.0f;
    if (g->level != sLastLevel) { sLastLevel = g->level; sBannerAge = 0.0f; }
}

static void UpdateAndQueueShards(float dt) {
    for (int i = 0; i < MAX_SHARDS; i++) {
        Shard *s = &sShards[i];
        if (s->life <= 0.0f) continue;
        s->life -= dt;
        s->vy += GRAVITY * 0.6f * dt;
        s->x += s->vx * dt;
        s->y += s->vy * dt;
        s->angle += s->spin * dt;
        if (s->life <= 0.0f) continue;
        Color c = s->color;
        c.a = (unsigned char)(255.0f * (s->life / s->maxLife));
        float dx = sinf(s->angle) * s->len / 2.0f * sCam.zoom, dy = -cosf(s->angle) * s->len / 2.0f * sCam.zoom;
        Line(SX(s->x) - dx, SY(s->y) - dy, SX(s->x) + dx, SY(s->y) + dy, c);
    }
}

/* ------------------------------------------------------------------ HUD */

static void DrawStat(int x, int y, const char *label, const char *value) {
    Ui_Text(label, x, y, UI_T8, kTextDim);
    Ui_Text(value, x, y + 14, UI_T16, kText);
}

static void DrawHud(const Game *g) {
    Ui_Text("MOONDROP", FIELD_X, 16, UI_T16, kLight);
    char buf[64];
    if (Game_Wind(g) != 0.0f) {
        float w = Game_Wind(g);
        snprintf(buf, sizeof(buf), "WIND %s %.1f", w > 0 ? ">>>" : "<<<", fabsf(w));
        Ui_Text(buf, FIELD_X + 230, 20, UI_T8, fabsf(w) > 4.5f ? kDanger : kGold);
    }
    int colW = FIELD_W / 4;
    int statY = 46;
    snprintf(buf, sizeof(buf), "%ld", g->score);
    DrawStat(FIELD_X, statY, "Score", buf);
    snprintf(buf, sizeof(buf), "%ld", g->highScore > g->score ? g->highScore : g->score);
    DrawStat(FIELD_X + colW, statY, "Best", buf);
    snprintf(buf, sizeof(buf), "%d", g->level);
    DrawStat(FIELD_X + colW * 2, statY, "Level", buf);

    /* Fuel: a bar that shrinks, blinking red when low. */
    Ui_Text("Fuel", FIELD_X + colW * 3, statY, UI_T8, kTextDim);
    float frac = g->fuel / FUEL_MAX;
    if (frac > 1.0f) frac = 1.0f;
    bool low = g->fuel <= FUEL_LOW;
    bool blink = low && !Prefs_Get()->reducedFlashing && fmodf((float)GetTime(), 0.4f) < 0.2f;
    int bw = colW - 24;
    DrawRectangle(FIELD_X + colW * 3, statY + 16, bw, 14, kRule);
    if (!blink) DrawRectangle(FIELD_X + colW * 3, statY + 16, (int)((float)bw * frac), 14, low ? kDanger : kLight);
    snprintf(buf, sizeof(buf), "%d", (int)g->fuel);
    Ui_Text(buf, FIELD_X + colW * 3 + bw - Ui_Measure(buf, UI_T8), statY, UI_T8, low ? kDanger : kText);

    Ui_TextCentered("Left/Right or A/D rotate    Up, W or Space thrust    P pause    R restart    Esc menu", UI_T8,
                    WINDOW_WIDTH / 2, WINDOW_HEIGHT - 17, kFaint);
}

/* The three instruments, top-left of the field: green while a landing at
 * that number would be survivable, red when it would not. */
static void DrawInstruments(const Game *g) {
    float alt = Game_Altitude(g);
    if (alt < 0.0f) alt = 0.0f;
    int x = FIELD_X + 12, y = FIELD_Y + 10;
    char buf[48];
    snprintf(buf, sizeof(buf), "ALT %3d", (int)alt);
    Ui_Text(buf, x, y, UI_T8, kTextDim);
    snprintf(buf, sizeof(buf), "H %c%2d", g->vx < 0 ? '<' : '>', (int)fabsf(g->vx));
    Ui_Text(buf, x, y + 14, UI_T8, fabsf(g->vx) <= MAX_LAND_VX ? kSafe : kDanger);
    snprintf(buf, sizeof(buf), "V %c%2d", g->vy < 0 ? '^' : 'v', (int)fabsf(g->vy));
    Ui_Text(buf, x, y + 28, UI_T8, g->vy <= MAX_LAND_VY ? kSafe : kDanger);
    snprintf(buf, sizeof(buf), "TILT %2d", (int)(fabsf(g->angle) * 180.0f / PI_F + 0.5f));
    Ui_Text(buf, x, y + 42, UI_T8, fabsf(g->angle) < MAX_LAND_TILT ? kSafe : kDanger);
}

static const char *CrashLine(LandResult r) {
    switch (r) {
        case LAND_TOO_FAST_DOWN: return "Came in too fast";
        case LAND_TOO_FAST_SIDEWAYS: return "Too much sideways drift";
        case LAND_TILTED: return "Landed tilted over";
        case LAND_OFF_PAD: return "Missed the pad";
        case LAND_HIT_BODY: return "Hit the rock";
        default: return "";
    }
}

static void DrawGameOverOverlay(const Game *g, const FrameInfo *info) {
    DrawRectangle(FIELD_X, FIELD_Y, FIELD_W, FIELD_H, (Color){13, 10, 30, 224});
    int cx = FIELD_X + FIELD_W / 2, cy = FIELD_Y + FIELD_H / 2;
    Ui_TextCentered("GAME OVER", UI_T32, cx, cy - 116, kDanger);
    char buf[96];
    snprintf(buf, sizeof(buf), "Out of fuel on level %d", g->level);
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

static void DrawStars(void) {
    for (int i = 0; i < 70; i++) {
        unsigned h = (unsigned)(i + 1) * 2654435761u;
        h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
        int x = FIELD_X + (int)(h % FIELD_W), y = FIELD_Y + (int)((h >> 11) % (FIELD_H - 150));
        DrawRectangle(x, y, 2, 2, Fade(kFaint, 0.5f + 0.5f * (float)((h >> 5) & 3) / 3.0f));
    }
}

void Render_Frame(const Game *g, const FrameInfo *info) {
    float dt = GetFrameTime();
    float time = (float)GetTime();
    ReactToEvents(g);
    sShakeAge += dt;
    sBannerAge += dt;
    sLowFuelAge += dt;
    if (g->phase == GS_GAMEOVER) sDeadFor += dt; else sDeadFor = 0.0f;
    FollowShip(g, dt);

    Win_BeginFrame();
    ClearBackground(Ui_Theme()->bg);
    DrawHud(g);
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
    DrawStars();
    DrawTerrain(g);
    DrawWindStreaks(g, time);
    DrawPods(g, time);
    DrawShip(g, time);
    UpdateAndQueueShards(dt);
    FlushLines(info->glow);
    for (int i = 0; i < sLabelCount; i++) {
        char buf[8];
        snprintf(buf, sizeof(buf), "x%d", sLabels[i].mult);
        Ui_TextCentered(buf, UI_T8, (int)sLabels[i].x, (int)sLabels[i].y, sLabels[i].mult >= 5 ? kGold : kPadColor);
    }
    if (g->phase != GS_GAMEOVER) DrawInstruments(g);
    EndScissorMode();
    EndMode2D();

    int cx = FIELD_X + FIELD_W / 2;
    if (g->phase == GS_PLAYING && g->state == SHIP_LANDED) {
        char buf[96];
        DrawRectangle(cx - 200, FIELD_Y + 20, 400, 96, (Color){13, 10, 30, 225});
        DrawRectangle(cx - 200, FIELD_Y + 112, 400, 4, kLight);
        Ui_TextCentered("TOUCHDOWN", UI_T32, cx, FIELD_Y + 32, kLight);
        snprintf(buf, sizeof(buf), "+%ld  on the x%d pad", g->lastAward, g->pads[g->lastPad].mult);
        Ui_TextCentered(buf, UI_T16, cx, FIELD_Y + 80, kText);
    }
    if (g->phase == GS_PLAYING && g->state == SHIP_CRASHED) {
        char buf[64];
        Ui_TextCentered(CrashLine(g->lastResult), UI_T16, cx, FIELD_Y + 96, kText);
        snprintf(buf, sizeof(buf), "%.0f fuel lost", (double)CRASH_FUEL_PENALTY);
        Ui_TextCentered(buf, UI_T8, cx, FIELD_Y + 124, kDanger);
    }
    if (g->phase == GS_PLAYING && g->state == SHIP_FLYING && sBannerAge < 1.6f) {
        char buf[24];
        snprintf(buf, sizeof(buf), "LEVEL %d", g->level);
        int w = Ui_Measure(buf, UI_T32);
        int y = FIELD_Y + 96;
        DrawRectangle(cx - w / 2 - 20, y - 12, w + 40, 56, (Color){13, 10, 30, 225});
        DrawRectangle(cx - w / 2 - 20, y + 40, w + 40, 4, kLight);
        Ui_TextCentered(buf, UI_T32, cx, y, kLight);
    }
    if (g->phase == GS_PLAYING && sLowFuelAge < 1.4f && fmodf(sLowFuelAge, 0.3f) < 0.18f) Ui_TextCentered("LOW FUEL", UI_T16, cx, FIELD_Y + 60, kDanger);
    if (g->phase == GS_PAUSED) Ui_PauseOverlay(FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    if (g->phase == GS_GAMEOVER && sDeadFor > 1.0f) DrawGameOverOverlay(g, info);
    if (info->showFps) DrawFPS(10, WINDOW_HEIGHT - 24);
    Win_EndFrame();
}

/* A ridge line scrolling past and a lander easing down it, faint, behind the menu. */
void Render_MenuBackdrop(float dt) {
    static float sTime = 0.0f;
    sTime += dt;
    Color c = kLight;
    c.a = 46;
    float prevX = 0.0f, prevY = 0.0f;
    for (int i = 0; i <= 40; i++) {
        float x = (float)i * (WINDOW_WIDTH / 40.0f);
        float w = x + sTime * 12.0f;
        float y = 520.0f + 40.0f * sinf(w * 0.021f) + 22.0f * sinf(w * 0.057f + 1.3f) + 10.0f * sinf(w * 0.13f);
        if (i > 0) DrawLineEx((Vector2){prevX, prevY}, (Vector2){x, y}, 2.0f, c);
        prevX = x; prevY = y;
    }
    float t = fmodf(sTime, 14.0f) / 14.0f;
    float lx = 600.0f - t * 200.0f, ly = 130.0f + t * 330.0f, s = 1.6f;
    Color w = kShipColor;
    w.a = 60;
    DrawLineEx((Vector2){lx - 5 * s, ly - 12 * s}, (Vector2){lx + 5 * s, ly - 12 * s}, 2.0f, w);
    DrawLineEx((Vector2){lx + 5 * s, ly - 12 * s}, (Vector2){lx + 9 * s, ly - 6 * s}, 2.0f, w);
    DrawLineEx((Vector2){lx + 9 * s, ly - 6 * s}, (Vector2){lx + 9 * s, ly + 2 * s}, 2.0f, w);
    DrawLineEx((Vector2){lx + 9 * s, ly + 2 * s}, (Vector2){lx - 9 * s, ly + 2 * s}, 2.0f, w);
    DrawLineEx((Vector2){lx - 9 * s, ly + 2 * s}, (Vector2){lx - 9 * s, ly - 6 * s}, 2.0f, w);
    DrawLineEx((Vector2){lx - 9 * s, ly - 6 * s}, (Vector2){lx - 5 * s, ly - 12 * s}, 2.0f, w);
    DrawLineEx((Vector2){lx - 9 * s, ly + 2 * s}, (Vector2){lx - 8 * s, ly + 10 * s}, 2.0f, w);
    DrawLineEx((Vector2){lx + 9 * s, ly + 2 * s}, (Vector2){lx + 8 * s, ly + 10 * s}, 2.0f, w);
}
