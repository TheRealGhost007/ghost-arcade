#include "winscale.h"
#include "render.h"
#include "ui.h"
#include "prefs.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* --- Look: the inside of a burning tower. Red-pink is this machine's light
 * (the girders, the title, the goal); the shaft is deep indigo, the rings gold,
 * the rope pale, and the cursed fire rising from the bottom is the only warm
 * thing in the room, so you always know where it is. */
#define kBoardBg (Ui_Theme()->board)
#define kRule (Ui_Theme()->rule)
#define kText (Ui_Theme()->text)
#define kTextDim (Ui_Theme()->dim)
#define kFaint (Ui_Theme()->faint)
#define kLight (Ui_Theme()->light)
#define kGold (Ui_Theme()->gold)
#define kDanger (Ui_Theme()->danger)

static const Color kGirderDark = {148, 36, 72, 255};
static const Color kRing = {255, 214, 90, 255};
static const Color kRope = {236, 226, 200, 255};
static const Color kOrbCol = {150, 230, 255, 255};
static const Color kFire1 = {255, 90, 40, 255}, kFire2 = {255, 170, 60, 255}, kFire3 = {255, 240, 150, 255};
static const Color kEye = {21, 16, 43, 255};

static float sCamY = 0.0f;
static bool sCamInit = false;
static float sShakeAge = 9.0f, sIntroAge = 9.0f, sExtraAge = 9.0f;
static int sLastLevel = 0;

#define VX(x) ((float)VIEW_X + (x))
#define VY(y) ((float)VIEW_Y + (y) - sCamY)

/* ---------------------------------------------------------------- pieces */

static void DrawLedge(const Ledge *l, float time) {
    float x = VX(l->x), y = VY(l->y), w = l->w;
    if (y < (float)VIEW_Y - 30.0f || y > (float)VIEW_Y + VIEW_H + 10.0f) return;
    DrawRectangle((int)x, (int)y + 2, (int)w, 14, kLight);
    for (float t = 0.0f; t < w; t += 22.0f) {
        float xb = fminf(t + 11.0f, w), xc = fminf(t + 22.0f, w);
        DrawLineEx((Vector2){x + t, y + 15.0f}, (Vector2){x + xb, y + 4.0f}, 2.0f, kGirderDark);
        DrawLineEx((Vector2){x + xb, y + 4.0f}, (Vector2){x + xc, y + 15.0f}, 2.0f, kGirderDark);
    }
    DrawRectangle((int)x, (int)y, (int)w, 3, Ui_Lerp(kLight, WHITE, 0.55f));
    DrawRectangle((int)x, (int)y + 15, (int)w, 2, kGirderDark);
    if (l->goal) {
        float pulse = 0.5f + 0.5f * sinf(time * 3.0f);
        DrawRectangle((int)x, (int)y - 4, (int)w, 4, Fade(kGold, 0.5f + 0.4f * pulse));
        Ui_TextCentered("TOP", UI_T16, (int)(x + w / 2), (int)y - 30, kGold);
    } else if (l->checkpoint && l->y < 5000.0f) {
        /* a small flag: the fire will not put you back further than here */
        DrawRectangle((int)x + 8, (int)y - 26, 2, 26, kFaint);
        DrawRectangle((int)x + 10, (int)y - 26, 14, 9, l->reached ? kLight : kFaint);
    }
}

static void DrawRingAt(float wx, float wy, bool touched, bool targeted, float time) {
    float x = VX(wx), y = VY(wy);
    if (y < (float)VIEW_Y - 20.0f || y > (float)VIEW_Y + VIEW_H + 20.0f) return;
    float pulse = 0.5f + 0.5f * sinf(time * 4.0f + wx * 0.05f);
    Color c = touched ? Ui_Lerp(kRing, kFaint, 0.6f) : kRing;
    if (!touched) DrawCircleGradient((Vector2){x, y}, 26.0f, Fade(kRing, 0.16f + 0.10f * pulse), Fade(kRing, 0.0f));
    DrawCircleLines((int)x, (int)y, 10.0f, c);
    DrawCircleLines((int)x, (int)y, 9.0f, c);
    DrawRectangle((int)x - 2, (int)y - 16, 4, 6, c); /* the bracket it hangs from */
    if (targeted) {
        float r = 18.0f + 3.0f * pulse;
        DrawCircleLines((int)x, (int)y, r, Fade(WHITE, 0.9f));
        for (int k = 0; k < 4; k++) {
            float a = (float)k * 1.5708f + time * 1.5f;
            DrawLineEx((Vector2){x + cosf(a) * (r + 2.0f), y + sinf(a) * (r + 2.0f)}, (Vector2){x + cosf(a) * (r + 7.0f), y + sinf(a) * (r + 7.0f)}, 2.0f, WHITE);
        }
    }
}

static Color HeroPal(char c) {
    switch (c) {
        case 'H': return kGold;
        case 'h': return Ui_Lerp(kGold, BLACK, 0.35f);
        case 'S': return (Color){240, 200, 170, 255};
        case 'E': return kEye;
        case 'B': return (Color){88, 140, 235, 255};
        case 'b': return (Color){60, 100, 190, 255};
        case 'R': return kLight;
        case 'T': return (Color){70, 60, 90, 255};
        default: return WHITE;
    }
}

static void DrawHero(const Game *g, float time) {
    static const char *const kStand[13] = {
        "..hhhh..", ".HHHHHH.", "HHHHHHHH", "..SSSS..", "..SESS..", "..SSSS..", ".BBBBBB.", "SBBRRBBS", ".BBBBBB.", ".BBBBBB.", ".bb..bb.", ".bb..bb.", ".TT..TT.",
    };
    static const char *const kWalk[13] = {
        "..hhhh..", ".HHHHHH.", "HHHHHHHH", "..SSSS..", "..SESS..", "..SSSS..", ".BBBBBB.", "SBBRRBBS", ".BBBBBB.", ".BBBBBB.", "..bb.bb.", ".bb...bb", ".TT...TT",
    };
    static const char *const kHang[13] = {
        ".S.hhhh.", ".SHHHHHH", "HHHHHHHH", "..SSSS..", "..SESS..", "..SSSS..", ".BBBBBB.", ".BBRRBB.", ".BBBBBB.", ".BBBBBB.", ".bb..bb.", ".bb..bb.", ".TT..TT.",
    };
    static const char *const kFly[13] = {
        "S.hhhh.S", "SHHHHHHS", "HHHHHHHH", "..SSSS..", "..SESS..", "..SSSS..", ".BBBBBB.", ".BBRRBB.", ".BBBBBB.", ".BBBBBB.", "bb....bb", "bb....bb", "TT....TT",
    };
    const Player *p = &g->p;
    float cx = VX(p->x), cy = VY(p->y);
    if (g->state == LS_DYING && g->stateTimer < DYING_SECONDS - 0.4f) return;
    const char *const *art = kStand;
    if (p->mode == P_ROPE) art = kHang;
    else if (p->mode == P_AIR) art = kFly;
    else if (fabsf((float)g->moveX) > 0.5f && ((int)(time * 9.0f) & 1)) art = kWalk;
    int px = (int)roundf(cx) - 8, py = (int)roundf(cy) - 17;
    bool mirror = p->facing < 0;
    for (int r = 0; r < 13; r++) for (int c = 0; c < 8; c++) {
        char ch = art[r][mirror ? 7 - c : c];
        if (ch == '.') continue;
        DrawRectangle(px + c * 2, py + r * 2, 2, 2, HeroPal(ch));
    }
}

static void DrawFire(const Game *g, float time) {
    float top = VY(g->fireY);
    if (top > (float)VIEW_Y + VIEW_H) return;
    float bottom = (float)VIEW_Y + VIEW_H;
    if (top < (float)VIEW_Y) top = (float)VIEW_Y;
    DrawRectangleGradientV(VIEW_X, (int)top - 60, VIEW_W, 60, Fade(kFire2, 0.0f), Fade(kFire1, 0.22f)); /* the heat above it */
    DrawRectangle(VIEW_X, (int)top + 14, VIEW_W, (int)(bottom - top), kFire1);
    for (int i = 0; i < 28; i++) {
        float x = VX((float)i * 22.0f + 4.0f);
        float h = 20.0f + 18.0f * sinf(time * 5.0f + (float)i * 1.7f) + 10.0f * sinf(time * 9.0f + (float)i * 0.9f);
        DrawTriangle((Vector2){x - 12.0f, top + 22.0f}, (Vector2){x + 12.0f, top + 22.0f}, (Vector2){x + 2.0f * sinf(time * 4.0f + (float)i), top + 22.0f - h - 14.0f}, kFire2);
        DrawTriangle((Vector2){x - 7.0f, top + 22.0f}, (Vector2){x + 7.0f, top + 22.0f}, (Vector2){x, top + 22.0f - h * 0.6f - 6.0f}, kFire3);
    }
}

static void DrawOrb(const Game *g, const Orb *o) {
    float x = VX(o->x), y = VY(o->cy);
    if (y < (float)VIEW_Y - 30.0f || y > (float)VIEW_Y + VIEW_H + 30.0f) return;
    DrawCircleGradient((Vector2){x, y}, 30.0f, Fade(kOrbCol, 0.28f), Fade(kOrbCol, 0.0f));
    DrawCircle((int)x, (int)y, ORB_RADIUS, kOrbCol);
    DrawCircle((int)x - 4, (int)y - 3, 3.0f, kEye);
    DrawCircle((int)x + 4, (int)y - 3, 3.0f, kEye);
    DrawRectangle((int)x - 3, (int)y + 3, 6, 2, kEye);
    /* a short trail behind its sweep */
    float dir = cosf(o->freq * g->time + o->phase) >= 0.0f ? -1.0f : 1.0f;
    for (int k = 1; k <= 4; k++) DrawCircle((int)(x + dir * (float)k * 7.0f), (int)y, ORB_RADIUS - (float)k * 2.0f, Fade(kOrbCol, 0.25f - (float)k * 0.05f));
}

/* ---------------------------------------------------------------- effects */

#define POPUPS 5
static struct { float x, y, age; char text[12]; Color color; } sPopups[POPUPS];
static long sLastScore = 0;

static void Popup(float wx, float wy, long points, Color color) {
    for (int i = 0; i < POPUPS; i++) {
        if (sPopups[i].age < 0.9f) continue;
        sPopups[i].x = wx; sPopups[i].y = wy; sPopups[i].age = 0.0f; sPopups[i].color = color;
        snprintf(sPopups[i].text, sizeof(sPopups[i].text), "%ld", points);
        return;
    }
}

static void ReactToEvents(const Game *g) {
    float px = VX(g->p.x), py = VY(g->p.y);
    if (g->justAttach) Ui_Burst(VX(g->anchors[g->p.anchor].x), VY(g->anchors[g->p.anchor].y), kRing, 8, 120.0f);
    if (g->justRing) { Popup(g->anchors[g->p.anchor >= 0 ? g->p.anchor : 0].x, g->anchors[g->p.anchor >= 0 ? g->p.anchor : 0].y - 20.0f, RING_POINTS, kGold); Ui_Burst(px, py, kGold, 10, 140.0f); }
    if (g->justRelease) Ui_Burst(px, py, kRope, 5, 90.0f);
    if (g->justJump) Ui_Burst(px, py + 9.0f, WHITE, 5, 90.0f);
    if (g->justLand) Ui_Burst(px, py + 9.0f, kFaint, 4, 80.0f);
    if (g->justWall) sShakeAge = 0.0f;
    if (g->justCheckpoint) { Popup(g->p.x, g->p.y - 30.0f, CHECKPOINT_POINTS, kLight); Ui_Burst(px, py, kLight, 14, 160.0f); }
    if (g->justDeath) { Ui_Burst(px, py, g->death == DEATH_FIRE ? kFire2 : kOrbCol, 24, 240.0f); sShakeAge = 0.0f; }
    if (g->justClear) Ui_Burst(px, py, kGold, 30, 260.0f);
    if (g->justExtraLife) sExtraAge = 0.0f;
    if (g->justNewLevel || g->level != sLastLevel) { sLastLevel = g->level; sIntroAge = 0.0f; }
    sLastScore = g->score;
}

/* ------------------------------------------------------------------- HUD */

static void DrawStat(int x, int y, const char *label, const char *value, Color vc) {
    Ui_Text(label, x, y, UI_T8, kTextDim);
    Ui_Text(value, x, y + 14, UI_T16, vc);
}

static void DrawHud(const Game *g) {
    Ui_Text("GIRDERCLIMB", VIEW_X, 16, UI_T16, kLight);
    char buf[64];
    int colW = VIEW_W / 4;
    int statY = 46;
    snprintf(buf, sizeof(buf), "%ld", g->score);
    DrawStat(VIEW_X, statY, "Score", buf, kText);
    snprintf(buf, sizeof(buf), "%ld", g->highScore > g->score ? g->highScore : g->score);
    DrawStat(VIEW_X + colW, statY, "Best", buf, kText);
    snprintf(buf, sizeof(buf), "%dm", g->heightMeters);
    DrawStat(VIEW_X + colW * 2, statY, "Height", buf, kText);
    Ui_Text("Lives", VIEW_X + colW * 3, statY, UI_T8, kTextDim);
    for (int i = 0; i < g->lives - 1 && i < MAX_LIVES; i++) {
        int x = VIEW_X + colW * 3 + i * 18;
        bool flash = (i == g->lives - 2) && sExtraAge < 1.2f && fmodf(sExtraAge, 0.24f) < 0.12f;
        if (flash) DrawRectangle(x - 1, statY + 11, 16, 20, kGold);
        DrawRectangle(x + 1, statY + 13, 10, 4, kGold);
        DrawRectangle(x + 2, statY + 17, 8, 5, (Color){240, 200, 170, 255});
        DrawRectangle(x, statY + 22, 12, 7, (Color){88, 140, 235, 255});
    }
    snprintf(buf, sizeof(buf), "Tower %d", g->level);
    Ui_TextRight(buf, UI_T8, VIEW_X + VIEW_W, 22, kTextDim);
    Ui_TextCentered("WASD swing   Space grapple   X jump   P pause   R restart", UI_T8, WINDOW_WIDTH / 2, WINDOW_HEIGHT - 17, kFaint);
}

static const char *DeathLine(DeathCause c) {
    switch (c) {
        case DEATH_FIRE: return "The fire got you";
        case DEATH_ORB: return "A ghost got you";
        default: return "";
    }
}

static void Card(const char *title, const char *sub, Color accent, int y) {
    int cx = VIEW_X + VIEW_W / 2;
    int w = Ui_Measure(title, UI_T16) + 56;
    if (sub) { int sw = Ui_Measure(sub, UI_T8) + 56; if (sw > w) w = sw; }
    DrawRectangle(cx - w / 2, y, w, sub ? 76 : 50, (Color){13, 10, 30, 235});
    DrawRectangle(cx - w / 2, y + (sub ? 72 : 46), w, 4, accent);
    Ui_TextCentered(title, UI_T16, cx, y + 14, accent);
    if (sub) Ui_TextCentered(sub, UI_T8, cx, y + 46, kText);
}

static void DrawGameOverOverlay(const Game *g, const FrameInfo *info) {
    DrawRectangle(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, (Color){13, 10, 30, 224});
    int cx = VIEW_X + VIEW_W / 2, cy = VIEW_Y + VIEW_H / 2;
    Ui_TextCentered("GAME OVER", UI_T32, cx, cy - 116, kDanger);
    char buf[96];
    snprintf(buf, sizeof(buf), "Tower %d, %dm up", g->level, g->heightMeters);
    Ui_TextCentered(buf, UI_T8, cx, cy - 68, kTextDim);
    snprintf(buf, sizeof(buf), "%ld", g->score);
    Ui_TextCentered(buf, UI_T48, cx, cy - 38, kText);
    if (g->score >= g->highScore && g->score > 0) Ui_TextCentered("A new best", UI_T16, cx, cy + 34, kLight);
    else { snprintf(buf, sizeof(buf), "Best %ld", g->highScore); Ui_TextCentered(buf, UI_T16, cx, cy + 34, kTextDim); }
    if (info->lastRank > 0) { snprintf(buf, sizeof(buf), "%.16s is #%d on this machine", info->username, info->lastRank); Ui_TextCentered(buf, UI_T8, cx, cy + 68, kGold); }
    Ui_TextCentered("R plays again    Esc for the menu", UI_T8, cx, cy + 108, kTextDim);
}

/* ------------------------------------------------------------------ frame */

void Render_Frame(const Game *g, const FrameInfo *info) {
    float dt = GetFrameTime();
    float time = (float)GetTime();

    /* Camera: keep the climber about two thirds of the way up the view, and never show below the floor. */
    float want = g->p.y - (float)VIEW_H * 0.62f;
    float maxY = g->groundY + 60.0f - (float)VIEW_H;
    if (want > maxY) want = maxY;
    if (want < -20.0f) want = -20.0f;
    if (!sCamInit || g->state == LS_INTRO) { sCamY = want; sCamInit = true; }
    else sCamY += (want - sCamY) * fminf(1.0f, dt * 6.0f);

    ReactToEvents(g);
    sShakeAge += dt;
    sIntroAge += dt;
    sExtraAge += dt;

    Win_BeginFrame();
    ClearBackground(Ui_Theme()->bg);
    DrawHud(g);
    DrawRectangle(VIEW_X - 4, VIEW_Y - 4, VIEW_W + 8, VIEW_H + 8, kRule);
    DrawRectangle(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, kBoardBg);

    Camera2D cam = {0};
    cam.zoom = 1.0f;
    if (Prefs_Get()->screenShake && sShakeAge < 0.25f) { int amp = (int)(4.0f * (1.0f - sShakeAge / 0.25f)); cam.offset = (Vector2){(float)GetRandomValue(-amp, amp), (float)GetRandomValue(-amp, amp)}; }
    BeginMode2D(cam);
    BeginScissorMode(VIEW_X, VIEW_Y, VIEW_W, VIEW_H);

    /* The shaft: faint cross-braces that scroll with the tower. */
    for (float y = floorf(sCamY / 96.0f) * 96.0f; y < sCamY + VIEW_H + 96.0f; y += 96.0f) {
        float sy = VY(y);
        DrawLine(VIEW_X, (int)sy, VIEW_X + VIEW_W, (int)sy + 96, Fade(kRule, 0.35f));
        DrawLine(VIEW_X + VIEW_W, (int)sy, VIEW_X, (int)sy + 96, Fade(kRule, 0.35f));
    }
    DrawRectangle(VIEW_X, VIEW_Y, 6, VIEW_H, Fade(kLight, 0.25f));
    DrawRectangle(VIEW_X + VIEW_W - 6, VIEW_Y, 6, VIEW_H, Fade(kLight, 0.25f));

    int target = -1;
    if (g->p.mode != P_ROPE && g->state == LS_PLAY) target = Game_PickAnchor(g, g->p.x, g->p.y, g->moveX);
    for (int i = 0; i < g->ledgeCount; i++) DrawLedge(&g->ledges[i], time);
    for (int i = 0; i < g->anchorCount; i++) DrawRingAt(g->anchors[i].x, g->anchors[i].y, g->anchors[i].touched, i == target, time);
    if (target >= 0 && g->state == LS_PLAY) {
        float ax = VX(g->anchors[target].x), ay = VY(g->anchors[target].y), px = VX(g->p.x), py = VY(g->p.y);
        for (int k = 0; k < 8; k++) { float t0 = (float)k / 8.0f, t1 = t0 + 0.06f; DrawLineEx((Vector2){px + (ax - px) * t0, py + (ay - py) * t0}, (Vector2){px + (ax - px) * t1, py + (ay - py) * t1}, 1.5f, Fade(WHITE, 0.45f)); }
    }
    for (int i = 0; i < g->orbCount; i++) if (g->orbsEnabled) DrawOrb(g, &g->orbs[i]);

    if (g->p.mode == P_ROPE) {
        const Anchor *a = &g->anchors[g->p.anchor];
        DrawLineEx((Vector2){VX(a->x), VY(a->y)}, (Vector2){VX(g->p.x), VY(g->p.y) - 10.0f}, 2.0f, kRope);
    }
    DrawHero(g, time);
    DrawFire(g, time);
    Ui_UpdateParticles(dt);
    for (int i = 0; i < POPUPS; i++) {
        if (sPopups[i].age >= 0.9f) continue;
        sPopups[i].age += dt;
        Ui_TextCentered(sPopups[i].text, UI_T8, (int)VX(sPopups[i].x), (int)VY(sPopups[i].y) - (int)(sPopups[i].age / 0.9f * 26.0f), sPopups[i].color);
    }
    if (info->scanlines) for (int y = 0; y < VIEW_H; y += 4) DrawRectangle(VIEW_X, VIEW_Y + y, VIEW_W, 1, (Color){0, 0, 0, 50});
    EndScissorMode();
    EndMode2D();

    /* A thin meter down the right: where you are in the tower, and where the fire is. */
    {
        float total = g->groundY - g->towerTop;
        float me = 1.0f - (g->p.y - g->towerTop) / total, fire = 1.0f - (g->fireY - g->towerTop) / total;
        int mx = VIEW_X + VIEW_W + 10, mh = VIEW_H;
        DrawRectangle(mx, VIEW_Y, 4, mh, kRule);
        DrawRectangle(mx - 3, VIEW_Y + (int)((1.0f - fminf(1.0f, fmaxf(0.0f, me))) * (float)(mh - 6)), 10, 6, kLight);
        DrawRectangle(mx - 3, VIEW_Y + (int)((1.0f - fminf(1.0f, fmaxf(-0.1f, fire))) * (float)(mh - 6)), 10, 6, kFire2);
    }

    int cx = VIEW_X + VIEW_W / 2;
    if (g->phase == GS_PLAYING && g->state == LS_INTRO) {
        char sub[64];
        snprintf(sub, sizeof(sub), "%d m to the top   the fire is already climbing", (int)((g->groundY - g->towerTop) / 10.0f));
        char head[24];
        snprintf(head, sizeof(head), "TOWER %d", g->level);
        Ui_TextCentered("Fire the grapple at a ring above you. Hold Up to reel in.", UI_T8, cx, VIEW_Y + 180, kTextDim);
        Card(head, sub, kLight, VIEW_Y + 210);
    }
    if (g->phase == GS_PLAYING && g->state == LS_DYING && g->stateTimer < DYING_SECONDS - 0.3f) Card(DeathLine(g->death), NULL, kText, VIEW_Y + 200);
    if (g->phase == GS_PLAYING && g->state == LS_CLEAR) {
        char sub[64];
        snprintf(sub, sizeof(sub), "+%ld  bonus", g->lastBonus);
        Card("TOP OF THE TOWER", sub, kLight, VIEW_Y + 200);
    }
    if (g->phase == GS_PAUSED) Ui_PauseOverlay(VIEW_X, VIEW_Y, VIEW_W, VIEW_H);
    if (g->phase == GS_GAMEOVER) DrawGameOverOverlay(g, info);
    if (info->showFps) DrawFPS(10, WINDOW_HEIGHT - 24);
    Win_EndFrame();
}

void Render_MenuBackdrop(float dt) {
    static float sTime = 0.0f;
    sTime += dt;
    Color c = kLight;
    c.a = 40;
    for (int i = 0; i < 3; i++) {
        float y = 210.0f + (float)i * 140.0f;
        DrawLineEx((Vector2){30.0f, y}, (Vector2){WINDOW_WIDTH - 30.0f, y + ((i & 1) ? -22.0f : 22.0f)}, 10.0f, c);
    }
    /* a rope swinging from a ring, with a small climber on it */
    float ax = 470.0f, ay = 160.0f, len = 300.0f, ang = 0.7f * sinf(sTime * 1.1f);
    float px = ax + sinf(ang) * len, py = ay + cosf(ang) * len;
    Color rc = kRope;
    rc.a = 70;
    DrawLineEx((Vector2){ax, ay}, (Vector2){px, py}, 2.0f, rc);
    DrawCircleLines((int)ax, (int)ay, 10.0f, (Color){255, 214, 90, 90});
    DrawRectangle((int)px - 5, (int)py - 8, 10, 16, (Color){88, 140, 235, 90});
}
