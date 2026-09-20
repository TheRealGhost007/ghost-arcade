#include "winscale.h"
#include "render.h"
#include "ui.h"
#include "prefs.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* --- Look: a haunted house by candlelight. Pink is this machine's light (the
 * ghost, the title, the exit); the house is deep purple stone, the priest a
 * black robe with a gold bell, and his sight a warm yellow beam so you can
 * always see exactly what he sees. Everything is drawn from rectangles. */
#define kBoardBg (Ui_Theme()->board)
#define kRule (Ui_Theme()->rule)
#define kText (Ui_Theme()->text)
#define kTextDim (Ui_Theme()->dim)
#define kFaint (Ui_Theme()->faint)
#define kLight (Ui_Theme()->light)
#define kGold (Ui_Theme()->gold)
#define kDanger (Ui_Theme()->danger)

static const Color kFloorA = {30, 24, 58, 255}, kFloorB = {26, 20, 50, 255};
static const Color kWall = {62, 50, 104, 255}, kWallHi = {86, 72, 140, 255}, kWallLo = {40, 32, 74, 255};
static const Color kWood = {150, 96, 54, 255}, kIron = {130, 138, 168, 255};
static const Color kBeam = {255, 226, 120, 255};
static const Color kEye = {21, 16, 43, 255};

#define DOT 4
#define ART 8

/* Each room only fills part of the 16x12 grid, so the drawing is shifted to put the
 * used part in the middle of the frame. */
static float sOffX = 0.0f, sOffY = 0.0f;
static int sBoxX0 = 0, sBoxY0 = 0, sBoxX1 = MAP_W - 1, sBoxY1 = MAP_H - 1;
static float TX(float tx) { return (float)FIELD_X + sOffX + tx * TILE; }
static float TY(float ty) { return (float)FIELD_Y + sOffY + ty * TILE; }

static void FitRoom(const Game *g) {
    int x0 = MAP_W, y0 = MAP_H, x1 = -1, y1 = -1;
    const LevelDef *d = &kLevels[(g->level - 1) % kLevelCount];
    for (int y = 0; y < MAP_H; y++) {
        const char *row = d->rows[y];
        for (int x = 0; row && row[x] && x < MAP_W; x++) if (row[x] != '#') { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    }
    if (x1 < 0) { x0 = 0; y0 = 0; x1 = MAP_W - 1; y1 = MAP_H - 1; }
    /* one wall tile of margin around what is used */
    x0 = x0 > 0 ? x0 - 1 : 0; y0 = y0 > 0 ? y0 - 1 : 0; x1 = x1 < MAP_W - 1 ? x1 + 1 : MAP_W - 1; y1 = y1 < MAP_H - 1 ? y1 + 1 : MAP_H - 1;
    sBoxX0 = x0; sBoxY0 = y0; sBoxX1 = x1; sBoxY1 = y1;
    sOffX = ((float)FIELD_W - (float)(x1 - x0 + 1) * TILE) / 2.0f - (float)x0 * TILE;
    sOffY = ((float)FIELD_H - (float)(y1 - y0 + 1) * TILE) / 2.0f - (float)y0 * TILE;
}

static void Art(const char *const *rows, int px, int py, Color (*pal)(char), bool mirror) {
    for (int r = 0; r < ART; r++) for (int c = 0; c < ART; c++) {
        char ch = rows[r][mirror ? ART - 1 - c : c];
        if (ch == '.') continue;
        DrawRectangle(px + 1 + c * DOT, py + 1 + r * DOT, DOT, DOT, pal(ch));
    }
}

/* ---------------------------------------------------------------- tiles */

static void DrawTile(const Game *g, int x, int y, float time) {
    int px = (int)TX((float)x), py = (int)TY((float)y);
    Tile t = (Tile)g->tile[y][x];
    if (t == T_WALL) {
        DrawRectangle(px, py, TILE, TILE, kWall);
        DrawRectangle(px, py, TILE, 3, kWallHi);
        DrawRectangle(px, py + TILE - 3, TILE, 3, kWallLo);
        DrawRectangle(px + 16, py + 3, 2, 14, kWallLo);
        DrawRectangle(px + 6, py + 17, 2, 14, kWallLo);
        DrawRectangle(px + 24, py + 17, 2, 14, kWallLo);
        return;
    }
    DrawRectangle(px, py, TILE, TILE, ((x + y) & 1) ? kFloorA : kFloorB);
    switch (t) {
        case T_GRATE:
            DrawRectangle(px + 2, py + 2, TILE - 4, TILE - 4, (Color){14, 10, 30, 255});
            for (int i = 0; i < 4; i++) DrawRectangle(px + 5 + i * 8, py + 2, 3, TILE - 4, kIron);
            break;
        case T_FLAP:
            DrawRectangle(px, py, TILE, TILE, kWall);
            DrawRectangle(px + 9, py + 12, 16, 16, (Color){10, 8, 22, 255});
            DrawRectangle(px + 9, py + 10, 16, 3, kWood);
            DrawRectangle(px + 14, py + 20, 6, 8, (Color){255, 170, 90, 60});
            break;
        case T_DOOR:
            DrawRectangle(px + 2, py, TILE - 4, TILE, kWood);
            DrawRectangle(px + 2, py, TILE - 4, 3, Ui_Lerp(kWood, WHITE, 0.3f));
            for (int i = 0; i < 3; i++) DrawRectangle(px + 4 + i * 9, py + 4, 2, TILE - 8, Ui_Lerp(kWood, BLACK, 0.3f));
            DrawCircle(px + TILE / 2 + 6, py + TILE / 2, 3.0f, kGold);
            DrawRectangle(px + TILE / 2 + 5, py + TILE / 2 + 2, 2, 5, kGold);
            break;
        case T_CRACK:
            DrawRectangle(px, py, TILE, TILE, kWall);
            DrawRectangle(px, py, TILE, 3, kWallHi);
            DrawLineEx((Vector2){(float)px + 8, (float)py + 2}, (Vector2){(float)px + 16, (float)py + 14}, 2.0f, kEye);
            DrawLineEx((Vector2){(float)px + 16, (float)py + 14}, (Vector2){(float)px + 11, (float)py + 22}, 2.0f, kEye);
            DrawLineEx((Vector2){(float)px + 16, (float)py + 14}, (Vector2){(float)px + 26, (float)py + 26}, 2.0f, kEye);
            break;
        case T_PLATE: {
            bool held = Game_PlateHeld(g, x, y);
            DrawRectangle(px + 4, py + 4, TILE - 8, TILE - 8, held ? (Color){120, 240, 150, 255} : (Color){70, 64, 110, 255});
            DrawRectangleLinesEx((Rectangle){(float)px + 4, (float)py + 4, TILE - 8, TILE - 8}, 2.0f, held ? WHITE : kWallHi);
            break;
        }
        case T_GATE: {
            bool open = Game_GateOpen(g);
            if (open) { DrawRectangle(px + 2, py + 4, 4, TILE - 8, kIron); DrawRectangle(px + TILE - 6, py + 4, 4, TILE - 8, kIron); }
            else { for (int i = 0; i < 4; i++) DrawRectangle(px + 3 + i * 8, py, 4, TILE, kIron); DrawRectangle(px, py + 6, TILE, 3, kIron); DrawRectangle(px, py + TILE - 10, TILE, 3, kIron); }
            break;
        }
        case T_EXIT: {
            float pulse = 0.5f + 0.5f * sinf(time * 3.0f);
            DrawEllipse(px + TILE / 2, py + TILE / 2, 13.0f, 15.0f, Fade(kLight, 0.35f + 0.3f * pulse));
            DrawEllipse(px + TILE / 2, py + TILE / 2, 9.0f, 11.0f, Ui_Lerp(kLight, WHITE, 0.6f));
            DrawEllipseLines(px + TILE / 2, py + TILE / 2, 15.0f, 17.0f, Fade(WHITE, 0.6f));
            DrawRectangle(px + 9, py + 6, 3, 5, Fade(WHITE, 0.7f));
            break;
        }
        default: break;
    }
}

/* -------------------------------------------------------------- creatures */

static Color GhostPal(char c) { switch (c) { case 'E': return kEye; default: return (Color){246, 240, 255, 255}; } }
static Color CatPal(char c) { switch (c) { case 'E': return (Color){120, 240, 150, 255}; case 'D': return (Color){200, 110, 50, 255}; default: return (Color){255, 160, 80, 255}; } }
static Color ArmorPal(char c) { switch (c) { case 'P': return kLight; case 'E': return kEye; case 'D': return (Color){110, 118, 150, 255}; default: return (Color){190, 198, 222, 255}; } }
static Color ServPal(char c) { switch (c) { case 'S': return (Color){240, 200, 170, 255}; case 'E': return kEye; case 'H': return (Color){80, 90, 130, 255}; default: return (Color){64, 110, 200, 255}; } }
static Color PriestPal(char c) { switch (c) { case 'W': return (Color){240, 236, 250, 255}; case 'S': return (Color){240, 200, 170, 255}; case 'G': return kGold; case 'E': return kEye; default: return (Color){28, 22, 44, 255}; } }
static Color CratePal(char c) { return c == 'D' ? (Color){104, 66, 36, 255} : (Color){168, 110, 60, 255}; }

static void DrawGhost(float x, float y, float time, bool possessing) {
    static const char *const kA[ART] = {"..XXXX..", ".XXXXXX.", "XXXXXXXX", "XEEXXEEX", "XXXXXXXX", "XXXXXXXX", "XXXXXXXX", "X.XX.XX."};
    static const char *const kB[ART] = {"..XXXX..", ".XXXXXX.", "XXXXXXXX", "XEEXXEEX", "XXXXXXXX", "XXXXXXXX", "XXXXXXXX", ".XX.XX.X"};
    float bob = 2.0f * sinf(time * 4.0f);
    int px = (int)roundf(x), py = (int)roundf(y + bob);
    if (possessing) DrawCircleGradient((Vector2){(float)(px + TILE / 2), (float)(py + TILE / 2)}, 26.0f, Fade(kLight, 0.35f), Fade(kLight, 0.0f));
    else DrawCircleGradient((Vector2){(float)(px + TILE / 2), (float)(py + TILE / 2)}, 30.0f, Fade(kLight, 0.18f), Fade(kLight, 0.0f));
    if (!possessing) Art(((int)(time * 4.0f) & 1) ? kA : kB, px, py, GhostPal, false);
}

static void DrawHost(const Host *h, float x, float y, bool possessed, float time) {
    static const char *const kCat[ART] = {"X......X", "XX....XX", "XXXXXXXX", "XEXXXXEX", "XXXXXXXX", ".XXXXXX.", ".XXDDXX.", ".X.XX.X."};
    static const char *const kArmor[ART] = {"...PP...", "..XXXX..", "..XEEX..", "..XXXX..", ".DXXXXD.", "XXXXXXXX", ".XX..XX.", ".DD..DD."};
    static const char *const kServ[ART] = {"..HHHH..", "..SSSS..", "..SESS..", "..SSSS..", ".XXXXXX.", "SXXXXXXS", ".XX..XX.", ".HH..HH."};
    int px = (int)roundf(x), py = (int)roundf(y);
    if (possessed) {
        float pulse = 0.5f + 0.5f * sinf(time * 6.0f);
        DrawRectangleLinesEx((Rectangle){(float)px, (float)py, TILE, TILE}, 2.0f, Fade(kLight, 0.6f + 0.4f * pulse));
        DrawCircleGradient((Vector2){(float)(px + TILE / 2), (float)(py + TILE / 2)}, 28.0f, Fade(kLight, 0.30f), Fade(kLight, 0.0f));
    }
    const char *const *art = h->type == HOST_CAT ? kCat : (h->type == HOST_ARMOR ? kArmor : kServ);
    Color (*pal)(char) = h->type == HOST_CAT ? CatPal : (h->type == HOST_ARMOR ? ArmorPal : ServPal);
    Art(art, px, py, pal, false);
    if (h->hasKey) { DrawRectangle(px + TILE - 12, py + 2, 8, 4, kGold); DrawRectangle(px + TILE - 6, py + 2, 4, 8, kGold); }
}

static void DrawPriest(const Priest *p, float x, float y, float time) {
    static const char *const kA[ART] = {"..WWWW..", "..SSSS..", "..SESS..", ".XXXXXX.", "XXXWWXXX", "XXXXXXXX", ".XXXXXX.", "XX....XX"};
    int px = (int)roundf(x), py = (int)roundf(y);
    Art(kA, px, py, PriestPal, p->dir == 3);
    /* the bell, swinging in his hand */
    float sw = sinf(time * 7.0f) * 3.0f;
    DrawRectangle(px + (p->dir == 3 ? 1 : TILE - 10) + (int)sw, py + 20, 8, 8, kGold);
    DrawRectangle(px + (p->dir == 3 ? 1 : TILE - 10) + (int)sw + 2, py + 28, 4, 3, Ui_Lerp(kGold, BLACK, 0.4f));
}

/* ---------------------------------------------------------------- effects */

static float sPrevGx, sPrevGy, sCurGx, sCurGy;
static float sPrevHx[MAX_HOSTS], sPrevHy[MAX_HOSTS], sCurHx[MAX_HOSTS], sCurHy[MAX_HOSTS];
static float sPrevPx[MAX_PRIESTS], sPrevPy[MAX_PRIESTS], sCurPx[MAX_PRIESTS], sCurPy[MAX_PRIESTS];
static float sPrevCx[MAX_CRATES], sPrevCy[MAX_CRATES], sCurCx[MAX_CRATES], sCurCy[MAX_CRATES];
static int sLastTick = -1, sLastLevel = 0;
static float sShakeAge = 9.0f, sBannerAge = 9.0f;

static void Snap(const Game *g) {
    sPrevGx = sCurGx = (float)g->gx; sPrevGy = sCurGy = (float)g->gy;
    for (int i = 0; i < g->hostCount; i++) { sPrevHx[i] = sCurHx[i] = (float)g->hosts[i].x; sPrevHy[i] = sCurHy[i] = (float)g->hosts[i].y; }
    for (int i = 0; i < g->priestCount; i++) { sPrevPx[i] = sCurPx[i] = (float)g->priests[i].x; sPrevPy[i] = sCurPy[i] = (float)g->priests[i].y; }
    for (int i = 0; i < g->crateCount; i++) { sPrevCx[i] = sCurCx[i] = (float)g->crates[i].x; sPrevCy[i] = sCurCy[i] = (float)g->crates[i].y; }
}

static void Advance(const Game *g) {
    sPrevGx = sCurGx; sPrevGy = sCurGy; sCurGx = (float)g->gx; sCurGy = (float)g->gy;
    for (int i = 0; i < g->hostCount; i++) { sPrevHx[i] = sCurHx[i]; sPrevHy[i] = sCurHy[i]; sCurHx[i] = (float)g->hosts[i].x; sCurHy[i] = (float)g->hosts[i].y; }
    for (int i = 0; i < g->priestCount; i++) { sPrevPx[i] = sCurPx[i]; sPrevPy[i] = sCurPy[i]; sCurPx[i] = (float)g->priests[i].x; sCurPy[i] = (float)g->priests[i].y; }
    for (int i = 0; i < g->crateCount; i++) { sPrevCx[i] = sCurCx[i]; sPrevCy[i] = sCurCy[i]; sCurCx[i] = (float)g->crates[i].x; sCurCy[i] = (float)g->crates[i].y; }
}

static void ReactToEvents(const Game *g) {
    float gx = TX((float)g->gx) + TILE / 2.0f, gy = TY((float)g->gy) + TILE / 2.0f;
    if (g->justPossess) Ui_Burst(gx, gy, kLight, 16, 160.0f);
    if (g->justRelease) Ui_Burst(gx, gy, WHITE, 10, 120.0f);
    if (g->justKey) Ui_Burst(gx, gy, kGold, 10, 130.0f);
    if (g->justDoor) Ui_Burst(gx, gy, kWood, 14, 170.0f);
    if (g->justSmash) { Ui_Burst(gx + TILE, gy, kWallHi, 20, 210.0f); sShakeAge = 0.0f; }
    if (g->justPush) Ui_Burst(gx, gy, kWood, 5, 90.0f);
    if (g->justCaught) { Ui_Burst(gx, gy, kLight, 26, 240.0f); sShakeAge = 0.0f; }
    if (g->justClear) Ui_Burst(gx, gy, kLight, 30, 260.0f);
    if (g->level != sLastLevel || g->justNewLevel) { sLastLevel = g->level; sBannerAge = 0.0f; }
}

/* ------------------------------------------------------------------- HUD */

static void DrawStat(int x, int y, const char *label, const char *value, Color vc) {
    Ui_Text(label, x, y, UI_T8, kTextDim);
    Ui_Text(value, x, y + 14, UI_T16, vc);
}

static void DrawHud(const Game *g) {
    Ui_Text("GHOSTMAZE", FIELD_X, 16, UI_T16, kLight);
    char buf[64];
    int colW = FIELD_W / 4;
    int statY = 46;
    snprintf(buf, sizeof(buf), "%ld", g->score);
    DrawStat(FIELD_X, statY, "Score", buf, kText);
    snprintf(buf, sizeof(buf), "%ld", g->highScore > g->score ? g->highScore : g->score);
    DrawStat(FIELD_X + colW, statY, "Best", buf, kText);
    snprintf(buf, sizeof(buf), "%d", g->level);
    DrawStat(FIELD_X + colW * 2, statY, "Room", buf, kText);
    Ui_Text("Lives", FIELD_X + colW * 3, statY, UI_T8, kTextDim);
    for (int i = 0; i < g->lives - 1 && i < 6; i++) {
        int x = FIELD_X + colW * 3 + i * 18, y = statY + 14;
        DrawRectangle(x + 2, y, 12, 10, (Color){246, 240, 255, 255});
        DrawRectangle(x, y + 4, 16, 10, (Color){246, 240, 255, 255});
        DrawRectangle(x + 4, y + 4, 3, 3, kEye);
        DrawRectangle(x + 9, y + 4, 3, 3, kEye);
    }
    Ui_TextCentered("WASD move   Space possess   P pause   R restart   Esc menu", UI_T8, WINDOW_WIDTH / 2, WINDOW_HEIGHT - 17, kFaint);
}

static const char *CaughtLine(CaughtBy c) {
    switch (c) {
        case CAUGHT_SEEN: return "He saw you";
        case CAUGHT_TOUCHED: return "He caught you";
        case CAUGHT_EXORCISED: return "Exorcised";
        default: return "";
    }
}

static void Card(const char *title, const char *sub, Color accent, int y) {
    int cx = FIELD_X + FIELD_W / 2;
    int w = Ui_Measure(title, UI_T16) + 56;
    if (sub) { int sw = Ui_Measure(sub, UI_T8) + 56; if (sw > w) w = sw; }
    DrawRectangle(cx - w / 2, y, w, sub ? 76 : 50, (Color){13, 10, 30, 235});
    DrawRectangle(cx - w / 2, y + (sub ? 72 : 46), w, 4, accent);
    Ui_TextCentered(title, UI_T16, cx, y + 14, accent);
    if (sub) Ui_TextCentered(sub, UI_T8, cx, y + 46, kText);
}

static void DrawGameOverOverlay(const Game *g, const FrameInfo *info) {
    DrawRectangle(FIELD_X, FIELD_Y, FIELD_W, FIELD_H, (Color){13, 10, 30, 224});
    int cx = FIELD_X + FIELD_W / 2, cy = FIELD_Y + FIELD_H / 2;
    Ui_TextCentered("GAME OVER", UI_T32, cx, cy - 116, kDanger);
    char buf[96];
    snprintf(buf, sizeof(buf), "You reached room %d", g->level);
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
    FitRoom(g);
    ReactToEvents(g);
    sShakeAge += dt;
    sBannerAge += dt;
    if (g->ticks < sLastTick || g->state == ST_INTRO || sLastTick < 0) { Snap(g); sLastTick = g->ticks; }
    else if (g->ticks != sLastTick) { Advance(g); sLastTick = g->ticks; }
    float t = g->state == ST_PLAY ? fminf(1.0f, g->tickAccumulator / TICK_SECONDS) : 1.0f;
    t = t * t * (3.0f - 2.0f * t);

    Win_BeginFrame();
    ClearBackground(Ui_Theme()->bg);
    DrawHud(g);
    DrawRectangle(FIELD_X - 4, FIELD_Y - 4, FIELD_W + 8, FIELD_H + 8, kRule);
    DrawRectangle(FIELD_X, FIELD_Y, FIELD_W, FIELD_H, kBoardBg);

    Camera2D cam = {0};
    cam.zoom = 1.0f;
    if (Prefs_Get()->screenShake && sShakeAge < 0.25f) { int amp = (int)(4.0f * (1.0f - sShakeAge / 0.25f)); cam.offset = (Vector2){(float)GetRandomValue(-amp, amp), (float)GetRandomValue(-amp, amp)}; }
    BeginMode2D(cam);
    BeginScissorMode(FIELD_X, FIELD_Y, FIELD_W, FIELD_H);

    for (int y = sBoxY0; y <= sBoxY1; y++) for (int x = sBoxX0; x <= sBoxX1; x++) DrawTile(g, x, y, time);
    for (int k = 0; k < g->keyCount; k++) {
        if (g->keys[k].taken) continue;
        float bob = 2.0f * sinf(time * 3.0f + (float)k);
        float kx = TX((float)g->keys[k].x), ky = TY((float)g->keys[k].y) + bob;
        DrawCircle((int)kx + 13, (int)ky + 14, 6.0f, kGold);
        DrawCircle((int)kx + 13, (int)ky + 14, 2.5f, (Color){30, 24, 58, 255});
        DrawRectangle((int)kx + 17, (int)ky + 12, 12, 4, kGold);
        DrawRectangle((int)kx + 24, (int)ky + 16, 3, 5, kGold);
    }

    /* Sight: every tile a priest can see right now, lit. */
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < g->priestCount; i++) {
        const Priest *p = &g->priests[i];
        static const int dx[4] = {0, 1, 0, -1}, dy[4] = {-1, 0, 1, 0};
        float flick = 0.85f + 0.15f * sinf(time * 9.0f + (float)i);
        for (int k = 1; k <= SIGHT_RANGE; k++) {
            int sx = p->x + dx[p->dir] * k, sy = p->y + dy[p->dir] * k;
            if (sx < 0 || sx >= MAP_W || sy < 0 || sy >= MAP_H) break;
            Tile tt = (Tile)g->tile[sy][sx];
            bool block = tt == T_WALL || tt == T_DOOR || tt == T_CRACK || (tt == T_GATE && !Game_GateOpen(g)) || Game_HostAt(g, sx, sy) >= 0 || Game_CrateAt(g, sx, sy) >= 0;
            float a = (0.50f - 0.07f * (float)k) * flick;
            DrawRectangle((int)TX((float)sx) + 2, (int)TY((float)sy) + 2, TILE - 4, TILE - 4, Fade(kBeam, block ? a * 0.6f : a));
            if (block) break;
        }
    }
    EndBlendMode();

    for (int i = 0; i < g->crateCount; i++) {
        if (!g->crates[i].present) continue;
        float x = TX(sPrevCx[i] + (sCurCx[i] - sPrevCx[i]) * t), y = TY(sPrevCy[i] + (sCurCy[i] - sPrevCy[i]) * t);
        DrawRectangle((int)x + 3, (int)y + 3, TILE - 6, TILE - 6, CratePal('L'));
        DrawRectangleLinesEx((Rectangle){x + 3, y + 3, TILE - 6, TILE - 6}, 3.0f, CratePal('D'));
        DrawLineEx((Vector2){x + 5, y + 5}, (Vector2){x + TILE - 5, y + TILE - 5}, 3.0f, CratePal('D'));
    }
    for (int i = 0; i < g->hostCount; i++) {
        if (!g->hosts[i].present) continue;
        float x = TX(sPrevHx[i] + (sCurHx[i] - sPrevHx[i]) * t), y = TY(sPrevHy[i] + (sCurHy[i] - sPrevHy[i]) * t);
        DrawHost(&g->hosts[i], x, y, g->possessed == i, time);
    }
    float gx = TX(sPrevGx + (sCurGx - sPrevGx) * t), gy = TY(sPrevGy + (sCurGy - sPrevGy) * t);
    if (g->state != ST_DYING || g->stateTimer > DYING_SECONDS * 0.5f) DrawGhost(gx, gy, time, g->possessed >= 0);
    for (int i = 0; i < g->priestCount; i++) {
        float x = TX(sPrevPx[i] + (sCurPx[i] - sPrevPx[i]) * t), y = TY(sPrevPy[i] + (sCurPy[i] - sPrevPy[i]) * t);
        DrawPriest(&g->priests[i], x, y, time);
    }
    Ui_UpdateParticles(dt);
    /* darkness at the edges, like a lantern's reach */
    DrawRectangleGradientV(FIELD_X, FIELD_Y, FIELD_W, 40, Fade(BLACK, 0.35f), Fade(BLACK, 0.0f));
    DrawRectangleGradientV(FIELD_X, FIELD_Y + FIELD_H - 40, FIELD_W, 40, Fade(BLACK, 0.0f), Fade(BLACK, 0.35f));
    if (info->scanlines) for (int y = 0; y < FIELD_H; y += 4) DrawRectangle(FIELD_X, FIELD_Y + y, FIELD_W, 1, (Color){0, 0, 0, 50});
    EndScissorMode();
    EndMode2D();

    /* Beneath the house: the room's name, its hint, and how you are doing against par. */
    const LevelDef *d = &kLevels[(g->level - 1) % kLevelCount];
    int by = FIELD_Y + FIELD_H + 18;
    Ui_Text(d->name, FIELD_X, by, UI_T16, kText);
    char buf[64];
    snprintf(buf, sizeof(buf), "Steps %d   Par %d", g->ticks, g->par);
    Ui_TextRight(buf, UI_T8, FIELD_X + FIELD_W, by + 4, g->ticks <= g->par ? kLight : kTextDim);
    Ui_Text(d->hint, FIELD_X, by + 28, UI_T8, kTextDim);
    if (g->possessed >= 0) {
        static const char *const kNames[3] = {"a cat: cat flaps", "armor: breaks walls, pushes crates", "a servant: keys and doors"};
        Ui_Text("You are", FIELD_X, by + 48, UI_T8, kFaint);
        Ui_Text(kNames[g->hosts[g->possessed].type], FIELD_X + 64, by + 48, UI_T8, kLight);
    } else {
        Ui_Text("A ghost dies in his sight. Possess something.", FIELD_X, by + 48, UI_T8, kFaint);
    }

    int cx = FIELD_X + FIELD_W / 2;
    if (g->phase == GS_PLAYING && g->state == ST_INTRO) {
        char head[24];
        snprintf(head, sizeof(head), "ROOM %d", g->level);
        Ui_TextCentered(head, UI_T16, cx, FIELD_Y + 120, kTextDim);
        Card(d->name, d->hint, kLight, FIELD_Y + 150);
    }
    if (g->phase == GS_PLAYING && g->state == ST_DYING && g->stateTimer < DYING_SECONDS - 0.25f) Card(CaughtLine(g->caughtBy), NULL, kText, FIELD_Y + 150);
    if (g->phase == GS_PLAYING && g->state == ST_CLEAR) {
        char sub[64];
        snprintf(sub, sizeof(sub), "+%ld%s", g->lastBonus, g->ticks <= g->par ? "   at or under par" : "");
        Card("ESCAPED", sub, kLight, FIELD_Y + 150);
    }
    if (g->phase == GS_PAUSED) Ui_PauseOverlay(FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    if (g->phase == GS_GAMEOVER) DrawGameOverOverlay(g, info);
    if (info->showFps) DrawFPS(10, WINDOW_HEIGHT - 24);
    Win_EndFrame();
}

void Render_MenuBackdrop(float dt) {
    static float sTime = 0.0f;
    sTime += dt;
    for (int i = 0; i < 7; i++) {
        float x = fmodf((float)(i * 131 + 40) + sTime * (10.0f + (float)(i % 3) * 6.0f), (float)WINDOW_WIDTH + 80.0f) - 40.0f;
        float y = 190.0f + (float)((i * 83) % 380) + 12.0f * sinf(sTime * 1.3f + (float)i);
        Color c = {246, 240, 255, 34};
        DrawRectangle((int)x, (int)y, 28, 24, c);
        DrawRectangle((int)x - 4, (int)y + 10, 36, 20, c);
        DrawRectangle((int)x + 6, (int)y + 8, 4, 5, (Color){21, 16, 43, 60});
        DrawRectangle((int)x + 18, (int)y + 8, 4, 5, (Color){21, 16, 43, 60});
    }
}
