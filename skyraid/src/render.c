#include "winscale.h"
#include "render.h"
#include "ui.h"
#include "prefs.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* --- Sprites: 1-bit art as strings, two poses each, drawn at 2x. The
 * formation is the house brand's own cast -- bats on top, skulls in the
 * middle, ghosts at the bottom -- rather than anyone else's aliens. */
#define SPR_W 12
#define SPR_H 8

static const char *const kBat[2][SPR_H] = {
    {"X..........X", "XX........XX", "XXX.X..X.XXX", "XXXXXXXXXXXX", ".XXXX..XXXX.", "..XXXXXXXX..", "...XX..XX...", "............"},
    {"............", "....X..X....", "..XXXXXXXX..", "XXXX.XX.XXXX", "XXXXXXXXXXXX", "XX.XXXXXX.XX", "X...XXXX...X", "............"},
};
static const char *const kSkull[2][SPR_H] = {
    {"...XXXXXX...", "..XXXXXXXX..", ".XX..XX..XX.", ".XX..XX..XX.", ".XXXXXXXXXX.", "..XXX..XXX..", "..X.X..X.X..", "..X.X..X.X.."},
    {"...XXXXXX...", "..XXXXXXXX..", ".XX..XX..XX.", ".XX..XX..XX.", ".XXXXXXXXXX.", "..XXX..XXX..", "...X.XX.X...", "..X.X..X.X.."},
};
static const char *const kGhost[2][SPR_H] = {
    {"...XXXXXX...", "..XXXXXXXX..", ".XX.XXXX.XX.", ".XX.XXXX.XX.", ".XXXXXXXXXX.", ".XXXXXXXXXX.", ".XXXXXXXXXX.", ".X.XX..XX.X."},
    {"...XXXXXX...", "..XXXXXXXX..", ".XXX.XX.XXX.", ".XXX.XX.XXX.", ".XXXXXXXXXX.", ".XXXXXXXXXX.", ".XXXXXXXXXX.", ".XX..XX..XX."},
};
static const char *const kShip[8] = {
    "......X......", ".....XXX.....", ".....XXX.....", ".XXXXXXXXXXX.",
    "XXXXXXXXXXXXX", "XXXXXXXXXXXXX", "XXXXXXXXXXXXX", "XXXXXXXXXXXXX",
};
static const char *const kUfo[7] = {
    ".....XXXXXX.....", "...XXXXXXXXXX...", "..XXXXXXXXXXXX..", ".XX.XX.XX.XX.XX.",
    "XXXXXXXXXXXXXXXX", "..XXX..XX..XXX..", "...X........X...",
};

static const Color kRowColors[INV_ROWS] = {
    {255, 79, 163, 255},                        /* bats */
    {64, 200, 255, 255}, {64, 200, 255, 255},   /* skulls */
    {150, 244, 160, 255}, {150, 244, 160, 255}, /* ghosts */
};
static const Color kUfoColor = {255, 150, 60, 255};
static const Color kBunkerColor = {96, 220, 130, 255};

static void DrawSprite(const char *const *rows, int rowCount, int x, int y, int px, Color color) {
    for (int r = 0; r < rowCount; r++) {
        const char *line = rows[r];
        for (int c = 0; line[c]; c++) {
            if (line[c] != 'X') continue;
            /* Merge horizontal runs into one rectangle: far fewer draw calls. */
            int run = 1;
            while (line[c + run] == 'X') run++;
            DrawRectangle(x + c * px, y + r * px, run * px, px, color);
            c += run - 1;
        }
    }
}

/* ---------------------------------------------------------------- effects */
#define MAX_POPUPS 6
static struct {
    float x, y, age;
    char text[8];
    Color color;
} sPopups[MAX_POPUPS];

static float sShakeAge = 9.0f, sShakeAmp = 0.0f;
static float sBannerAge = 0.0f; /* starts at 0: the first wave announces itself too */
static int sBannerWave = 1;
static float sExtraLifeAge = 9.0f;

#define SHAKE_TIME 0.4f
#define POPUP_TIME 0.7f
#define BANNER_TIME 1.6f

static const Color kPowerColors[PU_COUNT] = {{255, 214, 90, 255}, {120, 220, 255, 255}, {120, 255, 160, 255}, {255, 120, 200, 255}};

static void ReactToEvents(const Game *g) {
    const UiTheme *t = Ui_Theme();
    for (int i = 0; i < g->killCount; i++) {
        const Kill *k = &g->kills[i];
        Color c = k->row < 0 ? kUfoColor : kRowColors[k->row];
        Ui_Burst(FIELD_X + k->x, FIELD_Y + k->y, c, k->row < 0 ? 26 : 9, k->row < 0 ? 280.0f : 170.0f);
        for (int p = 0; p < MAX_POPUPS; p++) {
            if (sPopups[p].age < POPUP_TIME) continue;
            sPopups[p].x = k->x;
            sPopups[p].y = k->y;
            sPopups[p].age = 0.0f;
            sPopups[p].color = k->row < 0 ? t->gold : c;
            snprintf(sPopups[p].text, sizeof(sPopups[p].text), "%d", k->points);
            break;
        }
    }
    if (g->justPlayerHit) {
        sShakeAge = 0.0f;
        sShakeAmp = 8.0f;
        Ui_Burst(FIELD_X + g->playerX, FIELD_Y + PLAYER_Y + PLAYER_H / 2.0f, t->text, 30, 260.0f);
        Ui_Burst(FIELD_X + g->playerX, FIELD_Y + PLAYER_Y + PLAYER_H / 2.0f, t->light, 14, 180.0f);
    }
    if (g->justWaveClear) {
        sBannerAge = 0.0f;
        sBannerWave = g->wave;
    }
    if (g->justExtraLife) sExtraLifeAge = 0.0f;
    if (g->justPowerUp || g->justShieldHit) {
        Color c = g->justShieldHit ? t->light : kPowerColors[g->lastPower];
        Ui_Burst(FIELD_X + g->playerX, FIELD_Y + PLAYER_Y, c, g->justNova ? 40 : 16, 200.0f);
    }
    if (g->justNova) { sShakeAge = 0.0f; sShakeAmp = 5.0f; }
}

static void DrawStars(void) {
    /* A fixed scatter from a tiny LCG: identical every frame, no storage. */
    unsigned seed = 0xC0FFEEu;
    for (int i = 0; i < 70; i++) {
        seed = seed * 1664525u + 1013904223u;
        int x = FIELD_X + (int)((seed >> 8) % (unsigned)FIELD_W);
        seed = seed * 1664525u + 1013904223u;
        int y = FIELD_Y + (int)((seed >> 8) % (unsigned)(FIELD_H - 60));
        unsigned char v = (unsigned char)(70 + (seed >> 20) % 90);
        DrawRectangle(x - x % 2, y - y % 2, 2, 2, (Color){v, v, (unsigned char)(v + 30), 255});
    }
}

static void DrawField(const Game *g, const FrameInfo *info, float dt) {
    const UiTheme *t = Ui_Theme();
    sShakeAge += dt;
    sBannerAge += dt;
    sExtraLifeAge += dt;

    Camera2D cam = {0};
    cam.zoom = 1.0f;
    if (Prefs_Get()->screenShake && sShakeAge < SHAKE_TIME) {
        int amp = (int)(sShakeAmp * (1.0f - sShakeAge / SHAKE_TIME));
        cam.offset = (Vector2){(float)GetRandomValue(-amp, amp), (float)GetRandomValue(-amp, amp)};
    }
    BeginMode2D(cam);

    DrawRectangle(FIELD_X - 4, FIELD_Y - 4, FIELD_W + 8, FIELD_H + 8, t->rule);
    DrawRectangle(FIELD_X, FIELD_Y, FIELD_W, FIELD_H, t->board);
    DrawStars();
    /* The ground the formation must never reach. */
    DrawRectangle(FIELD_X, FIELD_Y + PLAYER_Y + PLAYER_H + 8, FIELD_W, 2, Ui_Lerp(t->rule, kBunkerColor, 0.5f));

    BeginScissorMode(FIELD_X, FIELD_Y, FIELD_W, FIELD_H);

    for (int b = 0; b < BUNKER_COUNT; b++) {
        int bx = FIELD_X + (int)Game_BunkerX(b);
        for (int r = 0; r < BUNKER_ROWS; r++) {
            for (int c = 0; c < BUNKER_COLS; c++) {
                if (!g->bunker[b][r][c]) continue;
                int run = 1;
                while (c + run < BUNKER_COLS && g->bunker[b][r][c + run]) run++;
                DrawRectangle(bx + c * BUNKER_CELL, FIELD_Y + BUNKER_Y + r * BUNKER_CELL, run * BUNKER_CELL, BUNKER_CELL, kBunkerColor);
                c += run - 1;
            }
        }
    }

    for (int r = 0; r < INV_ROWS; r++) {
        const char *const *sprite = r == 0 ? kBat[g->marchFrame] : (r <= 2 ? kSkull[g->marchFrame] : kGhost[g->marchFrame]);
        for (int c = 0; c < INV_COLS; c++) {
            if (!g->alive[r][c]) continue;
            float ix, iy;
            Game_InvaderRect(g, r, c, &ix, &iy);
            DrawSprite(sprite, SPR_H, FIELD_X + (int)ix, FIELD_Y + (int)iy, 2, kRowColors[r]);
        }
    }

    if (g->ufoActive) DrawSprite(kUfo, 7, FIELD_X + (int)g->ufoX, FIELD_Y + UFO_Y, 2, kUfoColor);

    for (int i = 0; i < MAX_EXTRA_SHOTS + 1; i++) {
        const Shot *ps = i == 0 ? &g->playerShot : &g->extraShots[i - 1];
        if (!ps->active) continue;
        DrawRectangle(FIELD_X + (int)ps->x - SHOT_W / 2, FIELD_Y + (int)ps->y, SHOT_W, SHOT_H, g->rapidTimer > 0.0f ? t->gold : t->text);
    }
    for (int i = 0; i < MAX_CAPSULES; i++) {
        const Capsule *k = &g->capsules[i];
        if (!k->active) continue;
        static const char letters[PU_COUNT] = {'R', 'T', 'S', 'N'};
        Color c = kPowerColors[k->type];
        int x = FIELD_X + (int)k->x - CAPSULE_W / 2, y = FIELD_Y + (int)k->y - CAPSULE_H / 2;
        DrawRectangle(x, y, CAPSULE_W, CAPSULE_H, (Color){13, 10, 30, 255});
        DrawRectangleLines(x, y, CAPSULE_W, CAPSULE_H, c);
        char s[2] = {letters[k->type], 0};
        Ui_TextCentered(s, UI_T8, x + CAPSULE_W / 2, y + 2, c);
    }
    for (int i = 0; i < MAX_ENEMY_SHOTS; i++) {
        const Shot *s = &g->enemyShots[i];
        if (!s->active) continue;
        /* A zig-zag bolt, flipping as it falls. */
        int x = FIELD_X + (int)s->x, y = FIELD_Y + (int)s->y;
        int flip = ((int)(s->y / 8.0f)) & 1;
        for (int k = 0; k < 3; k++) DrawRectangle(x - 2 + (((k + flip) & 1) ? 2 : -2), y + k * 4, 4, 4, kUfoColor);
    }

    /* The ship: gone while the world holds its breath after a hit. */
    bool shipVisible = g->respawnTimer <= 0.0f && !(g->phase == GS_GAMEOVER && !g->invaded);
    if (shipVisible) {
        DrawSprite(kShip, 8, FIELD_X + (int)roundf(g->playerX) - PLAYER_W / 2, FIELD_Y + PLAYER_Y, 2, t->text);
        DrawRectangle(FIELD_X + (int)roundf(g->playerX) - 1, FIELD_Y + PLAYER_Y, 2, 2, t->light);
        if (g->shield) {
            Color sc = kPowerColors[PU_SHIELD];
            sc.a = 150 + (int)(60.0f * sinf((float)GetTime() * 8.0f));
            DrawCircleLines(FIELD_X + (int)roundf(g->playerX), FIELD_Y + PLAYER_Y + PLAYER_H / 2, 22.0f, sc);
        }
    }

    Ui_UpdateParticles(dt);

    for (int p = 0; p < MAX_POPUPS; p++) {
        if (sPopups[p].age >= POPUP_TIME) continue;
        sPopups[p].age += dt;
        int rise = (int)(sPopups[p].age / POPUP_TIME * 18.0f);
        Ui_TextCentered(sPopups[p].text, UI_T8, FIELD_X + (int)sPopups[p].x, FIELD_Y + (int)sPopups[p].y - 4 - rise, sPopups[p].color);
    }

    if (info->scanlines) {
        for (int y = 0; y < FIELD_H; y += 4) DrawRectangle(FIELD_X, FIELD_Y + y, FIELD_W, 1, (Color){0, 0, 0, 56});
    }
    EndScissorMode();
    EndMode2D();
}

static void DrawStat(int x, int y, const char *label, const char *value) {
    const UiTheme *t = Ui_Theme();
    Ui_Text(label, x, y, UI_T8, t->dim);
    Ui_Text(value, x, y + 14, UI_T16, t->text);
}

static void DrawHud(const Game *g) {
    const UiTheme *t = Ui_Theme();
    Ui_Text("SKYRAID", FIELD_X, 16, UI_T16, t->light);

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
    Ui_Text("Ships", FIELD_X + colW * 3, statY, UI_T8, t->dim);
    for (int i = 0; i < g->lives; i++) {
        Color c = (i == g->lives - 1 && sExtraLifeAge < 1.2f && fmodf(sExtraLifeAge, 0.24f) < 0.12f) ? t->gold : t->text;
        DrawSprite(kShip, 8, FIELD_X + colW * 3 + i * 20, statY + 14, 1, c);
    }

    /* Active power-ups, as draining bars under the field. */
    int by = FIELD_Y + FIELD_H + 4;
    int bx = FIELD_X;
    if (g->rapidTimer > 0.0f) {
        DrawRectangle(bx, by, (int)(90.0f * g->rapidTimer / POWER_SECONDS), 4, kPowerColors[PU_RAPID]);
        Ui_Text("RAPID", bx, by + 6, UI_T8, kPowerColors[PU_RAPID]);
        bx += 110;
    }
    if (g->twinTimer > 0.0f) {
        DrawRectangle(bx, by, (int)(90.0f * g->twinTimer / POWER_SECONDS), 4, kPowerColors[PU_TWIN]);
        Ui_Text("TWIN", bx, by + 6, UI_T8, kPowerColors[PU_TWIN]);
        bx += 110;
    }
    if (g->shield) Ui_Text("SHIELD", bx, by + 2, UI_T8, kPowerColors[PU_SHIELD]);

    Ui_TextCentered("Arrows or A/D move    Space fires    P pause    Esc menu", UI_T8,
                    WINDOW_WIDTH / 2, WINDOW_HEIGHT - 17, t->faint);
}

static void DrawGameOverOverlay(const Game *g, const FrameInfo *info) {
    const UiTheme *t = Ui_Theme();
    DrawRectangle(FIELD_X, FIELD_Y, FIELD_W, FIELD_H, (Color){13, 10, 30, 222});
    int cx = FIELD_X + FIELD_W / 2;
    int cy = FIELD_Y + FIELD_H / 2;

    Ui_TextCentered("GAME OVER", UI_T32, cx, cy - 120, t->danger);
    char buf[96];
    if (g->invaded) snprintf(buf, sizeof(buf), "They reached the ground on wave %d", g->wave);
    else snprintf(buf, sizeof(buf), "Your last ship fell on wave %d", g->wave);
    Ui_TextCentered(buf, UI_T8, cx, cy - 72, t->dim);

    snprintf(buf, sizeof(buf), "%ld", g->score);
    Ui_TextCentered(buf, UI_T48, cx, cy - 40, t->text);

    if (g->score >= g->highScore && g->score > 0) {
        Ui_TextCentered("A new best", UI_T16, cx, cy + 36, t->light);
    } else {
        snprintf(buf, sizeof(buf), "Best %ld", g->highScore);
        Ui_TextCentered(buf, UI_T16, cx, cy + 36, t->dim);
    }
    if (info->lastRank > 0) {
        snprintf(buf, sizeof(buf), "%.16s is #%d on this machine", info->username, info->lastRank);
        Ui_TextCentered(buf, UI_T8, cx, cy + 72, t->gold);
    }
    Ui_TextCentered("R plays again    Esc for the menu", UI_T8, cx, cy + 112, t->dim);
}

void Render_Frame(const Game *g, const FrameInfo *info) {
    const UiTheme *t = Ui_Theme();
    float dt = GetFrameTime();
    ReactToEvents(g);

    Win_BeginFrame();
    ClearBackground(t->bg);

    DrawHud(g);
    DrawField(g, info, dt);

    if (g->phase == GS_PLAYING && sBannerAge < BANNER_TIME) {
        char buf[24];
        snprintf(buf, sizeof(buf), "WAVE %d", sBannerWave);
        int cx = FIELD_X + FIELD_W / 2, y = FIELD_Y + 300;
        int w = Ui_Measure(buf, UI_T32);
        DrawRectangle(cx - w / 2 - 20, y - 12, w + 40, 56, (Color){13, 10, 30, 225});
        DrawRectangle(cx - w / 2 - 20, y + 40, w + 40, 4, t->light);
        Ui_TextCentered(buf, UI_T32, cx, y, t->light);
    }
    if (g->phase == GS_PAUSED) Ui_PauseOverlay(FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    if (g->phase == GS_GAMEOVER) DrawGameOverOverlay(g, info);

    if (info->showFps) DrawFPS(10, WINDOW_HEIGHT - 24);
    Win_EndFrame();
}

/* A ghostly formation drifting behind the menu. */
void Render_MenuBackdrop(float dt) {
    static float sTime = 0.0f;
    sTime += dt;
    int frame = ((int)(sTime * 1.6f)) & 1;
    int drift = (int)(sinf(sTime * 0.5f) * 40.0f);
    for (int r = 0; r < 3; r++) {
        const char *const *sprite = r == 0 ? kBat[frame] : (r == 1 ? kSkull[frame] : kGhost[frame]);
        Color c = kRowColors[r == 0 ? 0 : (r == 1 ? 1 : 3)];
        c.a = 36;
        for (int col = 0; col < 9; col++) {
            DrawSprite(sprite, SPR_H, 70 + col * 56 + drift, 196 + r * 28, 2, c);
        }
    }
}
