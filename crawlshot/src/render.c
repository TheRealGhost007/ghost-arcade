#include "winscale.h"
#include "render.h"
#include "ui.h"
#include "prefs.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* --- Look: a night garden. Magenta is this machine's light (the shooter, the
 * title, the wave banner); the toadstools and the caterpillar take a new pair
 * of colours every wave so a long game never looks the same twice. */
#define kBoardBg (Ui_Theme()->board)
#define kRule (Ui_Theme()->rule)
#define kText (Ui_Theme()->text)
#define kTextDim (Ui_Theme()->dim)
#define kFaint (Ui_Theme()->faint)
#define kLight (Ui_Theme()->light)
#define kGold (Ui_Theme()->gold)
#define kDanger (Ui_Theme()->danger)

typedef struct { Color cap, worm, head; } Palette;
static const Palette kPalettes[] = {
    {{240, 96, 120, 255}, {120, 220, 110, 255}, {226, 255, 150, 255}},
    {{92, 170, 255, 255}, {255, 172, 72, 255}, {255, 232, 150, 255}},
    {{255, 196, 72, 255}, {120, 204, 255, 255}, {200, 240, 255, 255}},
    {{176, 112, 255, 255}, {255, 122, 150, 255}, {255, 200, 210, 255}},
    {{96, 224, 192, 255}, {255, 212, 92, 255}, {255, 244, 170, 255}},
    {{255, 124, 72, 255}, {170, 152, 255, 255}, {220, 210, 255, 255}},
};
#define PALETTE_COUNT ((int)(sizeof(kPalettes) / sizeof(kPalettes[0])))
static const Palette *PaletteFor(int wave) { return &kPalettes[((wave < 1 ? 1 : wave) - 1) % PALETTE_COUNT]; }

static const Color kStem = {236, 226, 200, 255};
static const Color kEye = {21, 16, 43, 255};
static const Color kSpider = {150, 160, 255, 255};
static const Color kFlea = {255, 226, 120, 255};
static const Color kScorpion = {255, 150, 90, 255};

/* Field art is 8x8 drawn at 2px a dot: 16px in an 18px tile. */
#define ART 8
#define DOT 2

static const char *const kMushArt[ART] = {
    "..XXXX..", ".XXXXXX.", "XXXXXXXX", "XXXXXXXX", ".XXXXXX.", "...SS...", "...SS...", "...SS...",
};

/* Damage: which dots of the cap and stem are gone at each hit point. */
static bool Bitten(int hp, int r, int c) {
    if (hp >= 4) return false;
    bool b = (r <= 1 && c >= 5) || (r == 2 && c == 7);
    if (hp <= 2) b = b || (r <= 2 && c <= 1) || (r == 3 && c >= 6);
    if (hp <= 1) b = b || r <= 2 || c <= 1 || c >= 6;
    return b;
}

static void DrawMushroom(int x, int y, int hp, bool poisoned, Color cap, float time) {
    int sx = FIELD_X + x * TILE + 1, sy = FIELD_Y + y * TILE + 1;
    Color body = cap;
    if (poisoned) body = (Prefs_Get()->reducedFlashing || fmodf(time, 0.5f) < 0.25f) ? (Color){250, 250, 255, 255} : (Color){160, 60, 210, 255};
    for (int r = 0; r < ART; r++) {
        for (int c = 0; c < ART; c++) {
            char ch = kMushArt[r][c];
            if (ch == '.' || Bitten(hp, r, c)) continue;
            Color col = ch == 'S' ? kStem : body;
            if (ch == 'X' && ((r == 2 && (c == 2 || c == 5)) || (r == 3 && c == 3))) col = Ui_Lerp(body, WHITE, 0.55f); /* spots */
            DrawRectangle(sx + c * DOT, sy + r * DOT, DOT, DOT, col);
        }
    }
}

static void DrawSegment(int x, int y, bool head, bool diving, int dir, Color body, int legFrame) {
    int sx = FIELD_X + x * TILE + 1, sy = FIELD_Y + y * TILE + 1;
    static const char *const kBody[ART - 1] = {
        "..XXXX..", ".XXXXXX.", "XXXXXXXX", "XXXXXXXX", "XXXXXXXX", ".XXXXXX.", "..XXXX..",
    };
    for (int r = 0; r < ART - 1; r++) {
        for (int c = 0; c < ART; c++) {
            if (kBody[r][c] == '.') continue;
            Color col = ((r + c) & 1) && !head ? Ui_Lerp(body, BLACK, 0.10f) : body;
            DrawRectangle(sx + c * DOT, sy + r * DOT, DOT, DOT, col);
        }
    }
    if (head) {
        int e0 = dir > 0 ? 4 : 1, e1 = dir > 0 ? 6 : 3, er = 2;
        if (diving) { e0 = 2; e1 = 5; er = 4; }
        DrawRectangle(sx + e0 * DOT, sy + er * DOT, DOT, DOT * 2, kEye);
        DrawRectangle(sx + e1 * DOT, sy + er * DOT, DOT, DOT * 2, kEye);
        DrawRectangle(sx + 2 * DOT, sy - 1, DOT, 2, body); /* antennae */
        DrawRectangle(sx + 5 * DOT, sy - 1, DOT, 2, body);
    }
    Color leg = Ui_Lerp(body, BLACK, 0.45f);
    for (int c = legFrame & 1; c < ART; c += 2) DrawRectangle(sx + c * DOT, sy + 14, DOT, DOT, leg);
}

static void DrawShooter(float px, float py, bool flash) {
    static const char *const kArt[ART] = {
        "...XX...", "..XXXX..", "..XWWX..", ".XXXXXX.", "XXXXXXXX", "XX.XX.XX", "X..XX..X", "........",
    };
    int sx = FIELD_X + (int)roundf(px * TILE) - 8, sy = FIELD_Y + (int)roundf(py * TILE) - 8;
    for (int r = 0; r < ART; r++) {
        for (int c = 0; c < ART; c++) {
            char ch = kArt[r][c];
            if (ch == '.') continue;
            Color col = ch == 'W' ? (Color){255, 240, 250, 255} : kLight;
            if (flash) col = WHITE;
            DrawRectangle(sx + c * DOT, sy + r * DOT, DOT, DOT, col);
        }
    }
}

/* Bigger creatures are drawn at 3px a dot, centred on a point. */
static void DrawArt(const char *const *art, int rows, int cols, int cx, int cy, int dot, Color body, bool mirror, float time) {
    int sx = cx - cols * dot / 2, sy = cy - rows * dot / 2;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            char ch = art[r][mirror ? cols - 1 - c : c];
            if (ch == '.') continue;
            Color col = ch == 'K' ? kEye : (ch == 'L' ? Ui_Lerp(body, BLACK, 0.3f) : body);
            int jitter = (ch == 'L' && ((int)(time * 12.0f) & 1)) ? 1 : 0;
            DrawRectangle(sx + c * dot, sy + r * dot + jitter, dot, dot, col);
        }
    }
}

static void DrawSpider(const Spider *s, float time) {
    static const char *const kArt[6] = {
        "L..XXXX..L", ".LXXXXXXL.", "L.XKXXKX.L", ".LXXXXXXL.", "L..XXXX..L", "L........L",
    };
    DrawArt(kArt, 6, 10, FIELD_X + (int)((s->x + 0.5f) * TILE), FIELD_Y + (int)(s->y * TILE), 3, kSpider, false, time);
}

static void DrawFlea(const Flea *f, float time) {
    static const char *const kArt[8] = {
        "...XX...", "..XXXX..", ".XKXXKX.", ".XXXXXX.", "..XXXX..", ".L.XX.L.", "L..XX..L", "L......L",
    };
    Color c = f->hits > 0 ? (Color){255, 120, 120, 255} : kFlea;
    DrawArt(kArt, 8, 8, FIELD_X + (int)(((float)f->col + 0.5f) * TILE), FIELD_Y + (int)(f->y * TILE), 2, c, false, time);
}

static void DrawScorpion(const Scorpion *s, float time) {
    static const char *const kArt[6] = {
        "......XXXX..", ".....X....X.", "XX.XXXXXX..X", "XXXXXXXXXXXX", ".L.L.L.L....", "L.L.L.L.L...",
    };
    DrawArt(kArt, 6, 12, FIELD_X + (int)((s->x + 0.5f) * TILE), FIELD_Y + (int)(((float)s->row + 0.5f) * TILE), 3, kScorpion, s->dir < 0, time);
}

/* ---------------------------------------------------------------- effects */
#define MAX_POPUPS 8
static struct { float x, y, age; char text[10]; Color color; } sPopups[MAX_POPUPS];
static unsigned char sPrevMush[ROWS][COLS];
static long sLastScore = 0;
static int sLastWave = 0;
static float sBannerAge = 9.0f;
static int sBannerWave = 1;
static float sExtraAge = 9.0f;
static float sSplitAge = 9.0f;
#define POPUP_TIME 0.9f

static void Popup(float sx, float sy, int points, Color color) {
    for (int p = 0; p < MAX_POPUPS; p++) {
        if (sPopups[p].age < POPUP_TIME) continue;
        sPopups[p].x = sx;
        sPopups[p].y = sy;
        sPopups[p].age = 0.0f;
        sPopups[p].color = color;
        snprintf(sPopups[p].text, sizeof(sPopups[p].text), "%d", points);
        return;
    }
}

static const Color kPowerColors[PU_COUNT] = {{255, 214, 90, 255}, {255, 120, 200, 255}, {120, 220, 255, 255}};

static void ReactToEvents(const Game *g) {
    const Palette *pal = PaletteFor(g->wave);
    float px = (float)FIELD_X + g->px * TILE, py = (float)FIELD_Y + g->py * TILE;

    if (g->justPlayerDeath) Ui_Burst(px, py, kLight, 24, 240.0f);
    if (g->justPowerUp) Ui_Burst(px, py, kPowerColors[g->lastPower], 18, 190.0f);
    for (int i = 0; i < g->killCount; i++) {
        const Kill *k = &g->kills[i];
        float sx = (float)FIELD_X + k->x * TILE, sy = (float)FIELD_Y + k->y * TILE;
        switch (k->kind) {
            case KILL_HEAD: Ui_Burst(sx, sy, pal->head, 14, 200.0f); break;
            case KILL_BODY: Ui_Burst(sx, sy, pal->worm, 8, 150.0f); break;
            case KILL_SPIDER: Ui_Burst(sx, sy, kSpider, 18, 220.0f); break;
            case KILL_FLEA: Ui_Burst(sx, sy, kFlea, 14, 200.0f); break;
            case KILL_SCORPION: Ui_Burst(sx, sy, kScorpion, 18, 220.0f); break;
            default: Ui_Burst(sx, sy, pal->cap, 4, 90.0f); break;
        }
        if (k->points > 1) Popup(sx, sy - 6.0f, k->points, k->kind == KILL_SPIDER || k->kind == KILL_SCORPION ? kGold : kText);
    }
    if (g->justSplit) sSplitAge = 0.0f;
    if (g->justExtraLife) sExtraAge = 0.0f;

    /* Healing after a death: sparkle wherever a toadstool just got its strength back. */
    if (g->justRestoreTick) {
        for (int y = 0; y < ROWS; y++) for (int x = 0; x < COLS; x++) {
            if (g->mush[y][x] > sPrevMush[y][x] && sPrevMush[y][x] != 0) {
                float sx = (float)FIELD_X + ((float)x + 0.5f) * TILE, sy = (float)FIELD_Y + ((float)y + 0.5f) * TILE;
                Ui_Burst(sx, sy, kGold, 6, 110.0f);
                Popup(sx, sy - 8.0f, 5, kGold);
            }
        }
    }
    if (g->wave != sLastWave) { sBannerAge = 0.0f; sBannerWave = g->wave; }
    if (g->phase == GS_GAMEOVER) sLastWave = 0; else sLastWave = g->wave;
    for (int y = 0; y < ROWS; y++) for (int x = 0; x < COLS; x++) sPrevMush[y][x] = g->mush[y][x];
    sLastScore = g->score;
}

/* ------------------------------------------------------------------- HUD */

static void DrawStat(int x, int y, const char *label, const char *value) {
    Ui_Text(label, x, y, UI_T8, kTextDim);
    Ui_Text(value, x, y + 14, UI_T16, kText);
}

static void DrawHud(const Game *g) {
    Ui_Text("CRAWLSHOT", FIELD_X, 16, UI_T16, kLight);
    char buf[64];
    {
        int px = FIELD_X + FIELD_W;
        if (g->freezeTimer > 0.0f) { snprintf(buf, sizeof(buf), "FREEZE %.0f", g->freezeTimer + 0.5f); px -= Ui_Measure(buf, UI_T8); Ui_Text(buf, px, 14, UI_T8, kPowerColors[PU_FREEZE]); px -= 16; }
        if (g->blastCharges > 0) { snprintf(buf, sizeof(buf), "BLAST x%d", g->blastCharges); px -= Ui_Measure(buf, UI_T8); Ui_Text(buf, px, 14, UI_T8, kPowerColors[PU_BLAST]); px -= 16; }
        if (g->pierceTimer > 0.0f) { snprintf(buf, sizeof(buf), "PIERCE %.0f", g->pierceTimer + 0.5f); px -= Ui_Measure(buf, UI_T8); Ui_Text(buf, px, 14, UI_T8, kPowerColors[PU_PIERCE]); }
    }
    int colW = FIELD_W / 4;
    int statY = 46;
    snprintf(buf, sizeof(buf), "%ld", g->score);
    DrawStat(FIELD_X, statY, "Score", buf);
    snprintf(buf, sizeof(buf), "%ld", g->highScore > g->score ? g->highScore : g->score);
    DrawStat(FIELD_X + colW, statY, "Best", buf);
    snprintf(buf, sizeof(buf), "%d", g->wave);
    DrawStat(FIELD_X + colW * 2, statY, "Wave", buf);

    Ui_Text("Shooters", FIELD_X + colW * 3, statY, UI_T8, kTextDim);
    int spare = g->lives - 1 < 0 ? 0 : g->lives - 1;
    for (int i = 0; i < spare && i < MAX_LIVES; i++) {
        int x = FIELD_X + colW * 3 + i * 20;
        bool flash = (i == spare - 1) && sExtraAge < 1.2f && fmodf(sExtraAge, 0.24f) < 0.12f;
        if (flash) DrawRectangle(x - 1, statY + 11, 18, 18, kGold);
        static const char *const kIcon[4] = {"..XX..", ".XXXX.", "XXXXXX", "X.XX.X"};
        for (int r = 0; r < 4; r++) for (int c = 0; c < 6; c++) if (kIcon[r][c] == 'X') DrawRectangle(x + c * 2 + 1, statY + 14 + r * 3, 2, 3, flash ? BLACK : kLight);
    }

    Ui_TextCentered("WASD move   Space fire   P pause   R restart   Esc menu", UI_T8, WINDOW_WIDTH / 2, WINDOW_HEIGHT - 17, kFaint);
}

/* ------------------------------------------------------------------ field */

static void DrawField(const Game *g, float time) {
    DrawRectangle(FIELD_X, FIELD_Y, FIELD_W, FIELD_H, kBoardBg);
    /* The garden, where the shooter lives, is a shade lighter and has an edge. */
    DrawRectangle(FIELD_X, FIELD_Y + PLAYER_TOP * TILE, FIELD_W, (ROWS - PLAYER_TOP) * TILE, Ui_Lerp(kBoardBg, kLight, 0.06f));
    DrawRectangle(FIELD_X, FIELD_Y + PLAYER_TOP * TILE, FIELD_W, 1, Fade(kLight, 0.35f));
    for (int y = 1; y < ROWS; y += 2) for (int x = 1; x < COLS; x += 2) DrawRectangle(FIELD_X + x * TILE, FIELD_Y + y * TILE, 1, 1, Fade(kRule, 0.8f));

    const Palette *pal = PaletteFor(g->wave);
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            if (g->mush[y][x]) DrawMushroom(x, y, g->mush[y][x], g->poison[y][x] != 0, pal->cap, time);
        }
    }

    /* While a life is being tidied away, damaged toadstools blink until they are healed. */
    if (g->restoring) {
        for (int y = 0; y < ROWS; y++) for (int x = 0; x < COLS; x++) {
            if (g->mush[y][x] && (g->mush[y][x] < MUSH_HP || g->poison[y][x]) && (Prefs_Get()->reducedFlashing || fmodf(time, 0.3f) < 0.15f)) {
                DrawRectangleLines(FIELD_X + x * TILE, FIELD_Y + y * TILE, TILE, TILE, Fade(kGold, 0.8f));
            }
        }
    }

    for (int i = 0; i < MAX_WORMS; i++) {
        const Worm *w = &g->worms[i];
        if (!w->active || w->enterDelay > 0.0f) continue;
        for (int k = w->len - 1; k >= 0; k--) {
            Color body = pal->worm;
            if (k == 0) body = pal->head;
            if (sSplitAge < 0.15f) body = Ui_Lerp(body, WHITE, 0.5f * (1.0f - sSplitAge / 0.15f));
            DrawSegment(w->seg[k].x, w->seg[k].y, k == 0, w->diving, w->dir, body, (int)(time * 10.0f) + k);
        }
    }

    if (g->spider.active) DrawSpider(&g->spider, time);
    if (g->flea.active) DrawFlea(&g->flea, time);
    if (g->scorpion.active) DrawScorpion(&g->scorpion, time);

    for (int i = 0; i < MAX_PICKUPS; i++) {
        const Pickup *p = &g->pickups[i];
        if (!p->active) continue;
        static const char letters[PU_COUNT] = {'P', 'B', 'F'};
        Color c = kPowerColors[p->type];
        int sx = FIELD_X + (int)(p->x * TILE), sy = FIELD_Y + (int)(p->y * TILE);
        float bob = sinf((float)GetTime() * 7.0f + p->x) * 1.5f;
        DrawCircle(sx, sy + (int)bob, 9.0f, (Color){13, 10, 30, 255});
        DrawCircleLines(sx, sy + (int)bob, 9.0f, c);
        char s[2] = {letters[p->type], 0};
        Ui_TextCentered(s, UI_T8, sx, sy - 4 + (int)bob, c);
    }
    if (g->bullet.active) {
        int bx = FIELD_X + (int)roundf(g->bullet.x * TILE), by = FIELD_Y + (int)roundf(g->bullet.y * TILE);
        Color bc = g->pierceTimer > 0.0f ? kPowerColors[PU_PIERCE] : (g->blastCharges > 0 ? kPowerColors[PU_BLAST] : WHITE);
        DrawRectangle(bx - 1, by - 5, 3, 10, bc);
        DrawRectangle(bx - 2, by + 5, 5, 4, Fade(kLight, 0.6f));
    }
    if (g->alive) DrawShooter(g->px, g->py, false);
}

static void DrawGameOverOverlay(const Game *g, const FrameInfo *info) {
    DrawRectangle(FIELD_X, FIELD_Y, FIELD_W, FIELD_H, (Color){13, 10, 30, 224});
    int cx = FIELD_X + FIELD_W / 2, cy = FIELD_Y + FIELD_H / 2;
    Ui_TextCentered("GAME OVER", UI_T32, cx, cy - 120, kDanger);
    char buf[96];
    snprintf(buf, sizeof(buf), "Reached wave %d", g->wave);
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

void Render_Frame(const Game *g, const FrameInfo *info) {
    float dt = GetFrameTime();
    float time = (float)GetTime();
    ReactToEvents(g);
    sBannerAge += dt;
    sExtraAge += dt;
    sSplitAge += dt;

    Win_BeginFrame();
    ClearBackground(Ui_Theme()->bg);
    DrawHud(g);
    DrawRectangle(FIELD_X - 4, FIELD_Y - 4, FIELD_W + 8, FIELD_H + 8, kRule);

    BeginScissorMode(FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    DrawField(g, time);
    Ui_UpdateParticles(dt);

    for (int p = 0; p < MAX_POPUPS; p++) {
        if (sPopups[p].age >= POPUP_TIME) continue;
        sPopups[p].age += dt;
        int rise = (int)(sPopups[p].age / POPUP_TIME * 26.0f);
        Ui_TextCentered(sPopups[p].text, UI_T8, (int)sPopups[p].x, (int)sPopups[p].y - rise, sPopups[p].color);
    }
    if (info->scanlines) for (int y = 0; y < FIELD_H; y += 4) DrawRectangle(FIELD_X, FIELD_Y + y, FIELD_W, 1, (Color){0, 0, 0, 56});
    EndScissorMode();

    int cx = FIELD_X + FIELD_W / 2;
    if (g->phase == GS_PLAYING && g->restoring) Ui_TextCentered("Tidying the garden", UI_T16, cx, FIELD_Y + FIELD_H / 2 - 8, kText);
    if (g->phase == GS_PLAYING && sBannerAge < 1.6f) {
        char buf[24];
        snprintf(buf, sizeof(buf), "WAVE %d", sBannerWave);
        int w = Ui_Measure(buf, UI_T32);
        int y = FIELD_Y + 8 * TILE;
        DrawRectangle(cx - w / 2 - 20, y - 12, w + 40, 56, (Color){13, 10, 30, 225});
        DrawRectangle(cx - w / 2 - 20, y + 40, w + 40, 4, kLight);
        Ui_TextCentered(buf, UI_T32, cx, y, kLight);
    }
    if (g->phase == GS_PAUSED) Ui_PauseOverlay(FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    if (g->phase == GS_GAMEOVER) DrawGameOverOverlay(g, info);
    if (info->showFps) DrawFPS(10, WINDOW_HEIGHT - 24);
    Win_EndFrame();
    (void)sLastScore;
}

float Render_ScreenToFieldX(float sx) { return (sx - (float)FIELD_X) / (float)TILE; }
float Render_ScreenToFieldY(float sy) { return (sy - (float)FIELD_Y) / (float)TILE; }

/* Faint caterpillars wander through a few toadstools behind the menu. */
void Render_MenuBackdrop(float dt) {
    static float sTime = 0.0f;
    sTime += dt;
    for (int row = 0; row < 5; row++) {
        int y = 190 + row * 46;
        const Palette *pal = PaletteFor(row + 1);
        float speed = 34.0f + (float)row * 12.0f;
        int dir = (row % 2) ? 1 : -1;
        Color body = pal->worm;
        body.a = 40;
        for (int seg = 0; seg < 9; seg++) {
            float x = fmodf(180.0f * (float)row + sTime * speed * (float)dir - (float)seg * 16.0f * (float)dir + 4000.0f, (float)(WINDOW_WIDTH + 200)) - 100.0f;
            DrawRectangle((int)x, y, 16, 16, body);
        }
        Color cap = pal->cap;
        cap.a = 30;
        for (int k = 0; k < 4; k++) DrawRectangle(60 + ((row * 131 + k * 173) % 500), y + 24, 16, 12, cap);
    }
}
