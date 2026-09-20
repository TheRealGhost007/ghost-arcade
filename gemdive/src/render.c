#include "winscale.h"
#include "render.h"
#include "ui.h"
#include "prefs.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* --- Look: a mine cut into a dark rock face. Gold is this machine's light (the
 * title, the digger, the exit when it opens); the world is earth-brown dirt,
 * cool steel and grey boulders, so the gems -- each cell its own jewel colour,
 * each with a glint on its own beat -- are the brightest thing in it. */
#define kBoardBg (Ui_Theme()->board)
#define kRule (Ui_Theme()->rule)
#define kText (Ui_Theme()->text)
#define kTextDim (Ui_Theme()->dim)
#define kFaint (Ui_Theme()->faint)
#define kLight (Ui_Theme()->light)
#define kGold (Ui_Theme()->gold)
#define kDanger (Ui_Theme()->danger)

static const Color kDirt = {104, 68, 46, 255};
static const Color kDirtDark = {80, 50, 36, 255};
static const Color kDirtLight = {134, 90, 60, 255};
static const Color kSteel = {84, 92, 122, 255};
static const Color kBrick = {158, 74, 66, 255};
static const Color kMortar = {96, 44, 44, 255};
static const Color kBoulder = {150, 150, 172, 255};
static const Color kEye = {21, 16, 43, 255};

#define ART 8
#define DOT 3

static unsigned Hash(int x, int y) {
    unsigned h = ((unsigned)x * 73856093u) ^ ((unsigned)y * 19349663u);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}

/* --------------------------------------------------------------- camera */

static float sCamX = 0.0f, sCamY = 0.0f;
static bool sCamInit = false;

static void FollowDigger(const Game *g, float dt, float fx, float fy) {
    float tx = fx - VIEW_COLS / 2.0f + 0.5f, ty = fy - VIEW_ROWS / 2.0f + 0.5f;
    if (tx < 0.0f) tx = 0.0f;
    if (tx > (float)(COLS - VIEW_COLS)) tx = (float)(COLS - VIEW_COLS);
    if (ty < 0.0f) ty = 0.0f;
    if (ty > (float)(ROWS - VIEW_ROWS)) ty = (float)(ROWS - VIEW_ROWS);
    if (!sCamInit || g->state == ST_INTRO) { sCamX = tx; sCamY = ty; sCamInit = true; return; }
    float k = fminf(1.0f, dt * 7.0f);
    sCamX += (tx - sCamX) * k;
    sCamY += (ty - sCamY) * k;
}

/* ------------------------------------------------------------- tile art */

static void DrawDirt(int sx, int sy, int cx, int cy) {
    DrawRectangle(sx, sy, TILE, TILE, kDirt);
    unsigned h = Hash(cx, cy);
    for (int i = 0; i < 4; i++) {
        int px = (int)((h >> (i * 5)) % (TILE - 4)) + 1, py = (int)((h >> (i * 5 + 2)) % (TILE - 4)) + 1;
        DrawRectangle(sx + px, sy + py, 3, 2, (i & 1) ? kDirtDark : kDirtLight);
    }
}

static void DrawSteel(int sx, int sy) {
    Ui_BevelRect(sx + 1, sy + 1, TILE - 2, TILE - 2, kSteel);
    DrawRectangle(sx + 4, sy + 4, 3, 3, Ui_Lerp(kSteel, BLACK, 0.4f));
    DrawRectangle(sx + TILE - 7, sy + TILE - 7, 3, 3, Ui_Lerp(kSteel, BLACK, 0.4f));
}

static void DrawBrick(int sx, int sy) {
    DrawRectangle(sx, sy, TILE, TILE, kMortar);
    for (int r = 0; r < 3; r++) {
        int y = sy + r * 8 + 1, off = (r & 1) ? 6 : 0;
        DrawRectangle(sx + 1, y, off ? off - 1 : 10, 6, kBrick);
        DrawRectangle(sx + (off ? off + 1 : 12), y, off ? 10 : 11, 6, kBrick);
        if (off) DrawRectangle(sx + off + 12, y, TILE - off - 13, 6, kBrick);
    }
}

static const char *const kBoulderArt[ART] = {
    "..XXXX..", ".XXXXXX.", "XXXXXXXX", "XXXXXXXX", "XXXXXXXX", "XXXXXXXX", ".XXXXXX.", "..XXXX..",
};

static void DrawBoulder(int sx, int sy) {
    for (int r = 0; r < ART; r++) {
        for (int c = 0; c < ART; c++) {
            if (kBoulderArt[r][c] == '.') continue;
            Color col = kBoulder;
            if (r + c <= 3) col = Ui_Lerp(kBoulder, WHITE, 0.35f);
            else if (r + c >= 11) col = Ui_Lerp(kBoulder, BLACK, 0.35f);
            else if ((r == 4 && c == 5) || (r == 5 && c == 3)) col = Ui_Lerp(kBoulder, BLACK, 0.18f); /* a crack or two */
            DrawRectangle(sx + c * DOT, sy + r * DOT, DOT, DOT, col);
        }
    }
}

static Color GemColor(int cx, int cy) {
    static const Color kJewels[4] = {{96, 226, 255, 255}, {255, 110, 190, 255}, {120, 240, 150, 255}, {255, 214, 90, 255}};
    return kJewels[Hash(cx, cy) & 3];
}

static void DrawGem(int sx, int sy, Color c, float time, int cx, int cy) {
    static const char *const kArt[ART] = {
        "..XXXX..", ".XXXXXX.", "XXXXXXXX", "XXXXXXXX", ".XXXXXX.", "..XXXX..", "...XX...", "........",
    };
    for (int r = 0; r < ART; r++) {
        for (int col = 0; col < ART; col++) {
            if (kArt[r][col] == '.') continue;
            Color k = c;
            if (r <= 1) k = Ui_Lerp(c, WHITE, 0.5f);
            else if (col >= 5 || r >= 4) k = Ui_Lerp(c, BLACK, 0.25f);
            DrawRectangle(sx + col * DOT, sy + 1 + r * DOT, DOT, DOT, k);
        }
    }
    /* A glint that sweeps on its own beat. */
    float beat = fmodf(time * 0.9f + (float)(Hash(cx, cy) % 100) / 100.0f, 1.0f);
    if (beat < 0.16f) {
        DrawRectangle(sx + 4, sy + 4, 6, 2, WHITE);
        DrawRectangle(sx + 6, sy + 2, 2, 6, WHITE);
    }
}

static void DrawExit(int sx, int sy, bool open, float time) {
    DrawRectangle(sx, sy, TILE, TILE, kEye);
    if (!open) {
        DrawRectangleLines(sx + 2, sy + 2, TILE - 4, TILE - 4, kSteel);
        DrawRectangle(sx + 8, sy + 8, 8, 8, kSteel);
        return;
    }
    bool blink = Prefs_Get()->reducedFlashing || fmodf(time, 0.5f) < 0.3f;
    Color c = blink ? kLight : Ui_Lerp(kLight, BLACK, 0.4f);
    DrawRectangleLinesEx((Rectangle){(float)sx + 1, (float)sy + 1, TILE - 2, TILE - 2}, 3.0f, c);
    /* an arrow into the door */
    DrawRectangle(sx + 8, sy + 6, 8, 5, c);
    DrawRectangle(sx + 6, sy + 11, 12, 3, c);
    DrawRectangle(sx + 8, sy + 14, 8, 2, c);
    DrawRectangle(sx + 10, sy + 16, 4, 2, c);
}

static void DrawDigger(int sx, int sy, int facing, bool moving, int frame, bool alive) {
    static const char *const kArt[ART] = {
        "..HHHH..", ".HHLLHH.", "..SSSS..", "..SESS..", ".BBBBBB.", ".BBBBBB.", "..T..T..", ".TT..TT.",
    };
    static const char *const kArt2[ART] = {
        "..HHHH..", ".HHLLHH.", "..SSSS..", "..SESS..", ".BBBBBB.", ".BBBBBB.", "..TT.T..", "..T..TT.",
    };
    if (!alive) return;
    const char *const *art = (moving && (frame & 1)) ? kArt2 : kArt;
    for (int r = 0; r < ART; r++) {
        for (int c = 0; c < ART; c++) {
            char ch = art[r][facing < 0 ? ART - 1 - c : c];
            if (ch == '.') continue;
            Color col = kLight;
            switch (ch) {
                case 'H': col = kLight; break;
                case 'L': col = (Color){255, 250, 200, 255}; break;
                case 'S': col = (Color){240, 200, 170, 255}; break;
                case 'E': col = kEye; break;
                case 'B': col = (Color){96, 150, 230, 255}; break;
                case 'T': col = (Color){70, 60, 90, 255}; break;
            }
            DrawRectangle(sx + c * DOT, sy + r * DOT, DOT, DOT, col);
        }
    }
}

static void DrawCrawler(int sx, int sy, int kind, float time, int idx) {
    static const char *const kA[ART] = {
        "X..XX..X", ".XXXXXX.", "XXKXXKXX", ".XXXXXX.", "XXXXXXXX", ".XXXXXX.", "X..XX..X", "........",
    };
    static const char *const kB[ART] = {
        ".X.XX.X.", "XXXXXXXX", "XXKXXKXX", "XXXXXXXX", ".XXXXXX.", "X.XXXX.X", ".X.XX.X.", "........",
    };
    bool frame = fmodf(time * 6.0f + (float)idx, 2.0f) < 1.0f;
    const char *const *art = frame ? kA : kB;
    Color body = kind ? (Color){255, 110, 190, 255} : (Color){255, 140, 60, 255};
    for (int r = 0; r < ART; r++) {
        for (int c = 0; c < ART; c++) {
            char ch = art[r][c];
            if (ch == '.') continue;
            DrawRectangle(sx + c * DOT, sy + r * DOT, DOT, DOT, ch == 'K' ? kEye : ((r + c) & 1 ? body : Ui_Lerp(body, WHITE, 0.3f)));
        }
    }
}

/* ---------------------------------------------------------------- effects */

typedef struct { int x, y, kind; float age; } Flash;
#define MAX_FLASHES 32
static Flash sFlashes[MAX_FLASHES];
static float sShakeAge = 9.0f, sShakeAmp = 0.0f;
static float sIntroAge = 9.0f;
static long sLastTick = -1;
static float sPrevPx = 0, sPrevPy = 0, sCurPx = 0, sCurPy = 0;
static float sPrevCx[MAX_CRAWLERS], sPrevCy[MAX_CRAWLERS], sCurCx[MAX_CRAWLERS], sCurCy[MAX_CRAWLERS];
static float sExtraAge = 9.0f;
static int sLastLevel = 0;

#define POPUPS 6
static struct { float x, y, age; char text[12]; Color color; } sPopups[POPUPS];
static long sLastScore = 0;

static void Popup(float sx, float sy, long points, Color color) {
    for (int i = 0; i < POPUPS; i++) {
        if (sPopups[i].age < 0.9f) continue;
        sPopups[i].x = sx; sPopups[i].y = sy; sPopups[i].age = 0.0f; sPopups[i].color = color;
        snprintf(sPopups[i].text, sizeof(sPopups[i].text), "%ld", points);
        return;
    }
}

static void ReactToEvents(const Game *g) {
    for (int i = 0; i < g->blastCount; i++) {
        for (int f = 0; f < MAX_FLASHES; f++) {
            if (sFlashes[f].age < 0.45f) continue;
            sFlashes[f] = (Flash){g->blasts[i].x, g->blasts[i].y, g->blasts[i].kind, 0.0f};
            break;
        }
        sShakeAge = 0.0f;
        sShakeAmp = 5.0f;
    }
    if (g->justExtraLife) sExtraAge = 0.0f;
    if (g->level != sLastLevel || g->justNewLevel) { sLastLevel = g->level; sIntroAge = 0.0f; }
}

/* ------------------------------------------------------------------- HUD */

static void DrawStat(int x, int y, const char *label, const char *value, Color vc) {
    Ui_Text(label, x, y, UI_T8, kTextDim);
    Ui_Text(value, x, y + 14, UI_T16, vc);
}

static void DrawHud(const Game *g) {
    Ui_Text("GEMDIVE", VIEW_X, 16, UI_T16, kLight);
    char buf[64];
    int colW = VIEW_W / 4;
    int statY = 46;
    snprintf(buf, sizeof(buf), "%ld", g->score);
    DrawStat(VIEW_X, statY, "Score", buf, kText);
    snprintf(buf, sizeof(buf), "%ld", g->highScore > g->score ? g->highScore : g->score);
    DrawStat(VIEW_X + colW, statY, "Best", buf, kText);
    snprintf(buf, sizeof(buf), "%d/%d", g->gems, g->quota);
    DrawStat(VIEW_X + colW * 2, statY, "Gems", buf, g->exitOpen ? kLight : kText);
    int secs = (int)ceilf(g->timeLeft);
    snprintf(buf, sizeof(buf), "%d", secs);
    bool low = g->timeLeft <= 20.0f && g->state == ST_PLAY;
    bool blink = low && !Prefs_Get()->reducedFlashing && fmodf((float)GetTime(), 0.4f) < 0.2f;
    DrawStat(VIEW_X + colW * 3, statY, "Time", buf, blink ? kDanger : (low ? kDanger : kText));
    /* lives as little diggers, beside the title */
    for (int i = 0; i < g->lives - 1 && i < MAX_LIVES; i++) {
        bool flash = (i == g->lives - 2) && sExtraAge < 1.2f && fmodf(sExtraAge, 0.24f) < 0.12f;
        if (flash) DrawRectangle(VIEW_X + 200 + i * 22 - 1, 15, 20, 20, kGold);
        DrawRectangle(VIEW_X + 200 + i * 22 + 3, 17, 12, 4, kLight);
        DrawRectangle(VIEW_X + 200 + i * 22 + 4, 21, 10, 5, (Color){240, 200, 170, 255});
        DrawRectangle(VIEW_X + 200 + i * 22 + 2, 26, 14, 6, (Color){96, 150, 230, 255});
    }
    Ui_TextCentered("Arrows or WASD dig and move    P pause    R restart    Esc menu", UI_T8, WINDOW_WIDTH / 2, WINDOW_HEIGHT - 17, kFaint);
}

static const char *DeathLine(DeathCause c) {
    switch (c) {
        case DEATH_CRUSHED: return "Crushed";
        case DEATH_BLAST: return "Caught in the blast";
        case DEATH_TIME: return "Out of time";
        case DEATH_CRAWLER: return "A crawler got you";
        default: return "";
    }
}

static void DrawGameOverOverlay(const Game *g, const FrameInfo *info) {
    DrawRectangle(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, (Color){13, 10, 30, 224});
    int cx = VIEW_X + VIEW_W / 2, cy = VIEW_Y + VIEW_H / 2;
    Ui_TextCentered("GAME OVER", UI_T32, cx, cy - 116, kDanger);
    char buf[96];
    snprintf(buf, sizeof(buf), "You reached cave %d", g->level);
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

static void Banner(const char *title, const char *sub, Color accent) {
    int cx = VIEW_X + VIEW_W / 2;
    int w = Ui_Measure(title, UI_T32);
    int bw = w + 60;
    if (sub) { int sw = Ui_Measure(sub, UI_T8) + 60; if (sw > bw) bw = sw; }
    int y = VIEW_Y + 70;
    DrawRectangle(cx - bw / 2, y - 14, bw, sub ? 96 : 66, (Color){13, 10, 30, 232});
    DrawRectangle(cx - bw / 2, y - 14 + (sub ? 92 : 62), bw, 4, accent);
    Ui_TextCentered(title, UI_T32, cx, y, accent);
    if (sub) Ui_TextCentered(sub, UI_T8, cx, y + 52, kText);
}

/* ------------------------------------------------------------------ frame */

void Render_Frame(const Game *g, const FrameInfo *info) {
    float dt = GetFrameTime();
    float time = (float)GetTime();
    ReactToEvents(g);
    sShakeAge += dt;
    sIntroAge += dt;
    sExtraAge += dt;

    /* Positions one tick ago, so the digger and crawlers glide between cells. */
    float tickSec = Game_TickSeconds(g);
    if (g->tickCount < sLastTick || g->state == ST_INTRO || sLastTick < 0) {
        sCurPx = sPrevPx = (float)g->px; sCurPy = sPrevPy = (float)g->py;
        for (int i = 0; i < MAX_CRAWLERS; i++) { sCurCx[i] = sPrevCx[i] = (float)g->crawlers[i].x; sCurCy[i] = sPrevCy[i] = (float)g->crawlers[i].y; }
        sLastTick = g->tickCount;
    } else if (g->tickCount != sLastTick) {
        sPrevPx = sCurPx; sPrevPy = sCurPy; sCurPx = (float)g->px; sCurPy = (float)g->py;
        for (int i = 0; i < MAX_CRAWLERS; i++) { sPrevCx[i] = sCurCx[i]; sPrevCy[i] = sCurCy[i]; sCurCx[i] = (float)g->crawlers[i].x; sCurCy[i] = (float)g->crawlers[i].y; }
        sLastTick = g->tickCount;
    }
    float t = g->state == ST_PLAY ? fminf(1.0f, g->tickAccumulator / tickSec) : 1.0f;
    float dgx = sPrevPx + (sCurPx - sPrevPx) * t, dgy = sPrevPy + (sCurPy - sPrevPy) * t;
    FollowDigger(g, dt, dgx, dgy);

    Win_BeginFrame();
    ClearBackground(Ui_Theme()->bg);
    DrawHud(g);
    DrawRectangle(VIEW_X - 4, VIEW_Y - 4, VIEW_W + 8, VIEW_H + 8, kRule);
    DrawRectangle(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, kBoardBg);

    Camera2D cam = {0};
    cam.zoom = 1.0f;
    if (Prefs_Get()->screenShake && sShakeAge < 0.3f) {
        int amp = (int)(sShakeAmp * (1.0f - sShakeAge / 0.3f));
        cam.offset = (Vector2){(float)GetRandomValue(-amp, amp), (float)GetRandomValue(-amp, amp)};
    }
    BeginMode2D(cam);
    BeginScissorMode(VIEW_X, VIEW_Y, VIEW_W, VIEW_H);

    int ox = VIEW_X - (int)roundf(sCamX * TILE), oy = VIEW_Y - (int)roundf(sCamY * TILE);
    int x0 = (int)floorf(sCamX), y0 = (int)floorf(sCamY);
    for (int y = y0; y <= y0 + VIEW_ROWS && y < ROWS; y++) {
        for (int x = x0; x <= x0 + VIEW_COLS && x < COLS; x++) {
            int sx = ox + x * TILE, sy = oy + y * TILE;
            switch (g->cell[y][x]) {
                case C_DIRT: DrawDirt(sx, sy, x, y); break;
                case C_STEEL: DrawSteel(sx, sy); break;
                case C_BRICK: DrawBrick(sx, sy); break;
                case C_EXIT: DrawExit(sx, sy, g->exitOpen, time); break;
                case C_BOULDER:
                case C_GEM: {
                    int slide = g->falling[y][x] && g->state == ST_PLAY ? (int)((1.0f - t) * TILE) : 0;
                    if (slide) DrawRectangle(sx + 6, sy - slide, TILE - 12, slide, Fade(WHITE, 0.06f)); /* a faint streak behind it */
                    if (g->cell[y][x] == C_BOULDER) DrawBoulder(sx, sy - slide);
                    else DrawGem(sx, sy - slide, GemColor(x, y), time, x, y);
                    break;
                }
                default: break;
            }
        }
    }
    for (int i = 0; i < g->crawlerCount; i++) {
        if (!g->crawlers[i].alive) continue;
        float cx = sPrevCx[i] + (sCurCx[i] - sPrevCx[i]) * t, cy = sPrevCy[i] + (sCurCy[i] - sPrevCy[i]) * t;
        DrawCrawler(ox + (int)roundf(cx * TILE), oy + (int)roundf(cy * TILE), g->crawlers[i].kind, time, i);
    }
    bool moving = g->state == ST_PLAY && (sPrevPx != sCurPx || sPrevPy != sCurPy) && t < 1.0f;
    DrawDigger(ox + (int)roundf(dgx * TILE), oy + (int)roundf(dgy * TILE), g->facing, moving, (int)g->tickCount, g->playerAlive);

    for (int f = 0; f < MAX_FLASHES; f++) {
        if (sFlashes[f].age >= 0.45f) continue;
        sFlashes[f].age += dt;
        float k = sFlashes[f].age / 0.45f;
        Color c = sFlashes[f].kind ? (Color){255, 120, 200, 255} : (Color){255, 200, 90, 255};
        int bx = ox + (sFlashes[f].x - 1) * TILE, by = oy + (sFlashes[f].y - 1) * TILE;
        DrawRectangle(bx, by, TILE * 3, TILE * 3, Fade(WHITE, 0.55f * (1.0f - k)));
        int inset = (int)(k * TILE * 1.2f);
        if (inset * 2 < TILE * 3) DrawRectangle(bx + inset, by + inset, TILE * 3 - inset * 2, TILE * 3 - inset * 2, Fade(c, 0.5f * (1.0f - k)));
    }
    Ui_UpdateParticles(dt);
    for (int i = 0; i < POPUPS; i++) {
        if (sPopups[i].age >= 0.9f) continue;
        sPopups[i].age += dt;
        Ui_TextCentered(sPopups[i].text, UI_T8, (int)sPopups[i].x, (int)sPopups[i].y - (int)(sPopups[i].age / 0.9f * 24.0f), sPopups[i].color);
    }
    if (info->scanlines) for (int y = 0; y < VIEW_H; y += 4) DrawRectangle(VIEW_X, VIEW_Y + y, VIEW_W, 1, (Color){0, 0, 0, 50});
    EndScissorMode();
    EndMode2D();

    /* Gems collected pop a score and a burst where they were picked. */
    if (g->justGem) {
        float sx = (float)(ox + g->px * TILE + TILE / 2), sy = (float)(oy + g->py * TILE);
        Ui_Burst(sx, sy + 8.0f, kLight, 8, 130.0f);
        Popup(sx, sy, g->score - sLastScore, kLight);
    }
    if (g->justExitOpen) Ui_Burst((float)VIEW_X + VIEW_W / 2.0f, (float)VIEW_Y + 30.0f, kLight, 12, 200.0f);
    sLastScore = g->score;

    if (g->phase == GS_PLAYING && g->state == ST_INTRO) {
        char sub[64];
        snprintf(sub, sizeof(sub), "Collect %d gems, then find the exit", g->quota);
        char title[64];
        snprintf(title, sizeof(title), "%s", g->cave ? g->cave->name : "");
        char head[24];
        snprintf(head, sizeof(head), "CAVE %d", g->level);
        Ui_TextCentered(head, UI_T16, VIEW_X + VIEW_W / 2, VIEW_Y + 44, kTextDim);
        Banner(title, sub, kLight);
    }
    if (g->phase == GS_PLAYING && g->state == ST_CLEAR) {
        char sub[64];
        snprintf(sub, sizeof(sub), "+%ld  for the cave and the time left", g->lastClearBonus);
        Banner("CAVE CLEARED", sub, kLight);
    }
    if (g->phase == GS_PLAYING && g->state == ST_DYING && g->stateTimer < DYING_SECONDS - 0.3f) {
        Ui_TextCentered(DeathLine(g->death), UI_T16, VIEW_X + VIEW_W / 2, VIEW_Y + 40, kText);
    }
    if (g->phase == GS_PAUSED) Ui_PauseOverlay(VIEW_X, VIEW_Y, VIEW_W, VIEW_H);
    if (g->phase == GS_GAMEOVER) DrawGameOverOverlay(g, info);
    if (info->showFps) DrawFPS(10, WINDOW_HEIGHT - 24);
    Win_EndFrame();
    (void)dt;
}

/* Faint gems drifting down behind the menu. */
void Render_MenuBackdrop(float dt) {
    static float sTime = 0.0f;
    sTime += dt;
    static const Color kJewels[4] = {{96, 226, 255, 255}, {255, 110, 190, 255}, {120, 240, 150, 255}, {255, 214, 90, 255}};
    for (int i = 0; i < 14; i++) {
        float speed = 24.0f + (float)((i * 37) % 30);
        float y = fmodf((float)(i * 97 % 700) + sTime * speed, (float)WINDOW_HEIGHT + 60.0f) - 30.0f;
        int x = (i * 151 + 40) % (WINDOW_WIDTH - 40);
        Color c = kJewels[i & 3];
        c.a = 40;
        int s = 3 + (i % 3);
        DrawRectangle(x, (int)y, s * 6, s * 3, c);
        DrawRectangle(x + s, (int)y + s * 3, s * 4, s * 2, c);
        DrawRectangle(x + s * 2, (int)y + s * 5, s * 2, s, c);
    }
}
