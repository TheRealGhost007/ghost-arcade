#include "winscale.h"
#include "render.h"
#include "ui.h"
#include "prefs.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* --- Look: a top-down crossing at dusk, drawn from rectangles. Coral is
 * this machine's light (traffic-light red, warmed a little); the world
 * itself is water blue, asphalt and grass, so the hare -- cream on all of
 * them -- is the one thing that always stands out. */
#define kBoardBg (Ui_Theme()->board)
#define kRule (Ui_Theme()->rule)
#define kText (Ui_Theme()->text)
#define kTextDim (Ui_Theme()->dim)
#define kFaint (Ui_Theme()->faint)
#define kLight (Ui_Theme()->light)
#define kGold (Ui_Theme()->gold)
#define kDanger (Ui_Theme()->danger)

static const Color kWater = {24, 44, 104, 255};
static const Color kWaterLine = {52, 92, 176, 255};
static const Color kRoad = {40, 38, 52, 255};
static const Color kLaneMark = {170, 166, 196, 255};
static const Color kGrass = {30, 74, 50, 255};
static const Color kGrassTuft = {44, 104, 66, 255};
static const Color kHedge = {22, 58, 42, 255};
static const Color kBurrow = {12, 8, 20, 255};
static const Color kLog = {140, 88, 48, 255};
static const Color kTurtle = {70, 170, 110, 255};
static const Color kHare = {250, 240, 215, 255};
static const Color kEar = {255, 170, 190, 255};
static const Color kEye = {21, 16, 43, 255};

#define ART 8
static const char *const kHareArt[ART] = {
    "..X..X..", "..X..X..", "..XXXX..", ".XXXXXX.", ".XKXXKX.", ".XXXXXX.", "..XXXX..", ".XX..XX.",
};

static void DrawHareArt(int x, int y, int px, bool ears) {
    for (int r = 0; r < ART; r++) {
        for (int c = 0; c < ART; c++) {
            char ch = kHareArt[r][c];
            if (ch == '.') continue;
            Color col = ch == 'K' ? kEye : ((r < 2 && ears) ? kEar : kHare);
            DrawRectangle(x + c * px, y + r * px, px, px, col);
        }
    }
}

static int TileX(float tx) { return FIELD_X + (int)roundf(tx * TILE); }
static int TileY(float ty) { return FIELD_Y + (int)roundf(ty * TILE); }

/* ----------------------------------------------------------------- lanes */

typedef struct { float x; int len; char ch; int startIdx; } Run;

/* Every solid run of a lane, as it is scrolled right now, including the
 * copies of the repeating pattern that are on (or just off) the screen. */
static int LaneRuns(const Game *g, int row, Run *out, int maxOut) {
    const LaneDef *d = Game_Lane(row);
    int p = (int)strlen(d->pattern), n = 0;
    int i0 = 0;
    while (i0 < p && d->pattern[i0] != '.') i0++; /* begin at a gap so no run straddles the wrap */
    if (i0 == p) return 0;
    for (int k = 1; k <= p; ) {
        int idx = (i0 + k) % p;
        if (d->pattern[idx] == '.') { k++; continue; }
        int len = 0;
        while (len < p && d->pattern[(idx + len) % p] != '.') len++;
        for (int copy = -1; copy <= 1; copy++) {
            /* pattern cell j sits at field x = j + scroll (+ any whole period) */
            float x = (float)idx + g->laneOffset[row] + (float)(copy * p);
            if (x + (float)len < -1.0f || x > (float)LANE_COLS + 1.0f) continue;
            if (n < maxOut) out[n++] = (Run){x, len, d->pattern[idx], idx};
        }
        k += len;
    }
    return n;
}

static void DrawCar(int x, int y, int w, int h, Color body, bool facingRight, char kind) {
    int r = y + 5, hh = h - 10;
    Ui_BevelRect(x + 2, r, w - 4, hh, body);
    /* windscreen toward the front, rear window at the back */
    int glassW = w > 60 ? 22 : 12;
    Color glass = {150, 200, 255, 255};
    if (facingRight) DrawRectangle(x + w - 8 - glassW, r + 4, glassW, hh - 8, glass);
    else DrawRectangle(x + 8, r + 4, glassW, hh - 8, glass);
    if (kind == 't') { /* a truck: a load behind the cab */
        Color load = Ui_Lerp(body, WHITE, 0.25f);
        if (facingRight) DrawRectangle(x + 6, r + 3, w - glassW - 22, hh - 6, load);
        else DrawRectangle(x + 16 + glassW, r + 3, w - glassW - 22, hh - 6, load);
    }
    Color wheel = {20, 16, 30, 255};
    DrawRectangle(x + 8, y + 2, 10, 4, wheel);
    DrawRectangle(x + 8, y + h - 6, 10, 4, wheel);
    DrawRectangle(x + w - 18, y + 2, 10, 4, wheel);
    DrawRectangle(x + w - 18, y + h - 6, 10, 4, wheel);
    Color lamp = {255, 240, 170, 255};
    if (facingRight) DrawRectangle(x + w - 4, r + 3, 3, 4, lamp);
    else DrawRectangle(x + 1, r + 3, 3, 4, lamp);
}

static void DrawBike(int x, int y, int w, int h, bool facingRight) {
    Color body = {255, 210, 63, 255};
    int cy = y + h / 2;
    Ui_BevelRect(x + 8, cy - 5, w - 16, 10, body);
    DrawRectangle(x + 4, cy - 8, 8, 16, (Color){20, 16, 30, 255});
    DrawRectangle(x + w - 12, cy - 8, 8, 16, (Color){20, 16, 30, 255});
    DrawRectangle(facingRight ? x + w - 6 : x + 2, cy - 2, 4, 4, (Color){255, 240, 170, 255});
}

static void DrawVehicles(const Game *g, int row) {
    static const Color kBodies[5] = {
        {255, 104, 96, 255}, {110, 160, 255, 255}, {150, 244, 160, 255}, {255, 150, 60, 255}, {200, 130, 255, 255},
    };
    const LaneDef *d = Game_Lane(row);
    Run runs[24];
    int n = LaneRuns(g, row, runs, 24);
    for (int i = 0; i < n; i++) {
        int x = TileX(runs[i].x), y = TileY((float)row), w = runs[i].len * TILE;
        bool right = d->dir > 0;
        if (runs[i].ch == 'b') { DrawBike(x, y, w, TILE, right); continue; }
        if (runs[i].ch == 't') { DrawCar(x, y, w, TILE, (Color){215, 210, 235, 255}, right, 't'); continue; }
        /* a run of several cars: draw them one to a tile, each its own colour */
        for (int k = 0; k < runs[i].len; k++) {
            DrawCar(x + k * TILE, y, TILE, TILE, kBodies[(row + runs[i].startIdx + k) % 5], right, 'a');
        }
    }
}

static void DrawPlatforms(const Game *g, int row, float time) {
    const LaneDef *d = Game_Lane(row);
    Run runs[24];
    int n = LaneRuns(g, row, runs, 24);
    for (int i = 0; i < n; i++) {
        int x = TileX(runs[i].x), y = TileY((float)row), w = runs[i].len * TILE;
        if (d->kind == LANE_LOG) {
            Ui_BevelRect(x + 1, y + 6, w - 2, TILE - 12, kLog);
            for (int k = 1; k < runs[i].len; k++) DrawRectangle(x + k * TILE - 2, y + 10, 4, TILE - 20, (Color){100, 60, 30, 255});
            DrawRectangle(x + 6, y + 14, 3, 3, (Color){186, 124, 70, 255});
            DrawRectangle(x + w - 12, y + TILE - 20, 3, 3, (Color){186, 124, 70, 255});
        } else {
            bool sinking = Game_TurtleSinking(g, row, runs[i].startIdx);
            bool under = Game_TurtleSubmerged(g, row, runs[i].startIdx);
            for (int k = 0; k < runs[i].len; k++) {
                int tx = x + k * TILE, ty = y;
                if (under) { /* just a ring of ripple where they went */
                    DrawRectangle(tx + 10, ty + 20, 24, 3, kWaterLine);
                    if (fmodf(time * 3.0f + (float)k, 1.0f) < 0.5f) DrawRectangle(tx + 18 + k, ty + 10, 4, 4, (Color){150, 190, 255, 255});
                    continue;
                }
                int shrink = sinking ? 5 + (int)(2.0f * sinf(time * 14.0f)) : 0; /* the wobble is the warning */
                Color shell = sinking ? Ui_Lerp(kTurtle, kWater, 0.35f) : kTurtle;
                Ui_BevelRect(tx + 6 + shrink / 2, ty + 8 + shrink / 2, TILE - 12 - shrink, TILE - 16 - shrink, shell);
                DrawRectangle(tx + 14, ty + 16, 6, 6, Ui_Lerp(shell, BLACK, 0.3f));
                DrawRectangle(tx + 24, ty + 16, 6, 6, Ui_Lerp(shell, BLACK, 0.3f));
                /* a head on the leading side, and flippers */
                int hx = d->dir > 0 ? tx + TILE - 8 : tx + 2;
                DrawRectangle(hx, ty + 18, 6, 8, shell);
                DrawRectangle(tx + 10, ty + 4 + shrink / 2, 6, 4, shell);
                DrawRectangle(tx + 10, ty + TILE - 8 - shrink / 2, 6, 4, shell);
            }
        }
    }
}

static void DrawWorld(const Game *g, float time) {
    for (int r = 0; r < LANE_ROWS; r++) {
        const LaneDef *d = Game_Lane(r);
        int y = TileY((float)r);
        switch (d->kind) {
            case LANE_HOME: {
                DrawRectangle(FIELD_X, y, FIELD_W, TILE, kHedge);
                for (int x = 0; x < LANE_COLS; x++) {
                    if (((x * 7 + 3) % 5) < 2) DrawRectangle(FIELD_X + x * TILE + 8, y + 8 + (x % 3) * 8, 4, 4, Ui_Lerp(kHedge, kGrassTuft, 0.7f));
                }
                break;
            }
            case LANE_LOG: case LANE_TURTLE: {
                DrawRectangle(FIELD_X, y, FIELD_W, TILE, kWater);
                /* ripples drift the way the lane flows, slowly */
                for (int k = 0; k < 7; k++) {
                    float rx = fmodf((float)(k * 83 + r * 41) + time * 9.0f * (float)d->dir + 4000.0f, (float)FIELD_W);
                    DrawRectangle(FIELD_X + (int)rx, y + 8 + ((k * 13 + r * 7) % 26), 16, 2, kWaterLine);
                }
                break;
            }
            case LANE_SAFE: {
                DrawRectangle(FIELD_X, y, FIELD_W, TILE, kGrass);
                for (int k = 0; k < 16; k++) DrawRectangle(FIELD_X + ((k * 97 + r * 31) % (FIELD_W - 8)), y + 6 + ((k * 53) % (TILE - 12)), 4, 6, kGrassTuft);
                break;
            }
            case LANE_ROAD: {
                DrawRectangle(FIELD_X, y, FIELD_W, TILE, kRoad);
                if (r > ROAD_FIRST) for (int x = 0; x < FIELD_W; x += 44) DrawRectangle(FIELD_X + x + 6, y - 1, 26, 3, kLaneMark);
                break;
            }
        }
    }

    /* Burrows in the hedge. */
    for (int i = 0; i < BAY_COUNT; i++) {
        int bx = FIELD_X + Game_BayColumn(i) * TILE, by = FIELD_Y;
        DrawRectangle(bx + 4, by + 8, TILE - 8, TILE - 8, kBurrow);
        DrawRectangle(bx + 8, by + 4, TILE - 16, 6, kBurrow); /* the arch */
        DrawRectangle(bx + 4, by + TILE - 3, TILE - 8, 3, (Color){60, 40, 24, 255});
        if (g->bays[i]) DrawHareArt(bx + 6, by + 6, 4, true);
    }
}

static void DrawGoodie(const Game *g, float time) {
    if (!g->goodieOn) return;
    /* blink in the last two seconds */
    if (g->goodieTimer < 2.0f && !Prefs_Get()->reducedFlashing && fmodf(time, 0.24f) > 0.12f) return;
    int cx = FIELD_X + g->goodieCol * TILE + TILE / 2;
    int cy = FIELD_Y + MEDIAN_ROW * TILE + TILE / 2 + (int)(2.0f * sinf(time * 5.0f));
    Color c = g->goodieType == GOODIE_CARROT ? (Color){255, 150, 60, 255} : (g->goodieType == GOODIE_CLOCK ? (Color){130, 220, 255, 255} : (Color){120, 255, 160, 255});
    DrawCircle(cx, cy, 15.0f, (Color){13, 10, 30, 255});
    DrawCircleLines(cx, cy, 15.0f, c);
    if (g->goodieType == GOODIE_CARROT) {
        DrawTriangle((Vector2){(float)cx - 6, (float)cy - 7}, (Vector2){(float)cx + 6, (float)cy - 7}, (Vector2){(float)cx, (float)cy + 9}, c);
        DrawRectangle(cx - 1, cy - 12, 2, 5, (Color){110, 220, 110, 255});
    } else if (g->goodieType == GOODIE_CLOCK) {
        DrawCircleLines(cx, cy, 8.0f, c);
        DrawLine(cx, cy, cx, cy - 6, c);
        DrawLine(cx, cy, cx + 4, cy, c);
    } else {
        DrawRectangle(cx - 6, cy - 8, 12, 10, c);
        DrawTriangle((Vector2){(float)cx - 6, (float)cy + 2}, (Vector2){(float)cx, (float)cy + 9}, (Vector2){(float)cx + 6, (float)cy + 2}, c);
    }
}

static void DrawBayGuests(const Game *g, float time) {
    if (g->flyBay >= 0 && g->flyTimer > 0.0f && (g->flyTimer > 1.0f || Prefs_Get()->reducedFlashing || fmodf(time, 0.2f) < 0.1f)) {
        int bx = FIELD_X + Game_BayColumn(g->flyBay) * TILE + TILE / 2, by = FIELD_Y + TILE / 2 + (int)(2.0f * sinf(time * 12.0f));
        DrawRectangle(bx - 3, by - 2, 6, 5, (Color){20, 16, 30, 255});
        DrawRectangle(bx - 8, by - 5 + ((int)(time * 20.0f) & 1) * 3, 5, 3, (Color){200, 220, 255, 255});
        DrawRectangle(bx + 3, by - 5 + ((int)(time * 20.0f) & 1) * 3, 5, 3, (Color){200, 220, 255, 255});
    }
    if (g->crocBay >= 0 && g->crocTimer > 0.0f) {
        int bx = FIELD_X + Game_BayColumn(g->crocBay) * TILE, by = FIELD_Y;
        int open = (int)(2.0f + 2.0f * sinf(time * 5.0f));
        Color green = {90, 190, 90, 255};
        DrawRectangle(bx + 5, by + 10, TILE - 10, 12, green);
        DrawRectangle(bx + 5, by + 22 + open, TILE - 10, 8, Ui_Lerp(green, BLACK, 0.2f));
        DrawRectangle(bx + 8, by + 6, 6, 6, (Color){255, 240, 170, 255});
        DrawRectangle(bx + TILE - 14, by + 6, 6, 6, (Color){255, 240, 170, 255});
        for (int k = 0; k < 4; k++) DrawRectangle(bx + 9 + k * 8, by + 22, 3, 4, WHITE);
    }
}

/* ---------------------------------------------------------------- effects */
#define MAX_POPUPS 6
static struct { float x, y, age; char text[10]; Color color; } sPopups[MAX_POPUPS];
static long sLastScore = 0;
static float sBannerAge = 9.0f;
static int sBannerLevel = 1;
static float sExtraAge = 9.0f;
#define POPUP_TIME 0.9f

static void Popup(float tx, float ty, long points, Color color) {
    for (int p = 0; p < MAX_POPUPS; p++) {
        if (sPopups[p].age < POPUP_TIME) continue;
        sPopups[p].x = tx;
        sPopups[p].y = ty;
        sPopups[p].age = 0.0f;
        sPopups[p].color = color;
        snprintf(sPopups[p].text, sizeof(sPopups[p].text), "%ld", points);
        return;
    }
}

static void ReactToEvents(const Game *g) {
    float hx, hy;
    Game_HarePos(g, &hx, &hy);
    float px = (float)FIELD_X + (hx + 0.5f) * TILE, py = (float)FIELD_Y + (hy + 0.5f) * TILE;

    if (g->justGoodie) Ui_Burst(px, py, kGold, 16, 180.0f);
    if (g->justShieldSave) Ui_Burst(px, py, (Color){120, 255, 160, 255}, 26, 240.0f);
    if (g->justDeath) {
        switch (g->deathCause) {
            case DEATH_CAR: Ui_Burst(px, py, (Color){255, 104, 96, 255}, 18, 220.0f); break;
            case DEATH_WATER: case DEATH_OFFSCREEN: Ui_Burst(px, py, (Color){150, 190, 255, 255}, 20, 190.0f); break;
            default: Ui_Burst(px, py, kHare, 14, 170.0f); break;
        }
    }
    if (g->justHome) {
        float bx = (float)FIELD_X + ((float)Game_BayColumn(g->homeBay) + 0.5f) * TILE;
        Ui_Burst(bx, (float)FIELD_Y + TILE / 2.0f, kGold, 16, 180.0f);
        Popup(bx, (float)FIELD_Y + TILE / 2.0f, g->score - sLastScore, kGold);
    }
    if (g->justLevelClear) { sBannerAge = 0.0f; sBannerLevel = g->level; }
    if (g->justExtraLife) sExtraAge = 0.0f;
    sLastScore = g->score;
}

/* ------------------------------------------------------------------- HUD */

static void DrawStat(int x, int y, const char *label, const char *value) {
    Ui_Text(label, x, y, UI_T8, kTextDim);
    Ui_Text(value, x, y + 14, UI_T16, kText);
}

static void DrawHud(const Game *g, float time) {
    Ui_Text("LANEHOP", FIELD_X, 16, UI_T16, kLight);
    char buf[64];
    int colW = FIELD_W / 4;
    int statY = 46;
    snprintf(buf, sizeof(buf), "%ld", g->score);
    DrawStat(FIELD_X, statY, "Score", buf);
    snprintf(buf, sizeof(buf), "%ld", g->highScore > g->score ? g->highScore : g->score);
    DrawStat(FIELD_X + colW, statY, "Best", buf);
    snprintf(buf, sizeof(buf), "%d", g->level);
    DrawStat(FIELD_X + colW * 2, statY, "Level", buf);

    Ui_Text(g->shield ? "Hares  SHIELD" : "Hares", FIELD_X + colW * 3, statY, UI_T8, g->shield ? (Color){120, 255, 160, 255} : kTextDim);
    for (int i = 0; i < g->lives - 1 && i < MAX_LIVES; i++) {
        bool flash = (i == g->lives - 2) && sExtraAge < 1.2f && fmodf(sExtraAge, 0.24f) < 0.12f;
        if (flash) DrawRectangle(FIELD_X + colW * 3 + i * 20 - 1, statY + 11, 18, 18, kGold);
        DrawHareArt(FIELD_X + colW * 3 + i * 20, statY + 12, 2, true);
    }

    /* The clock: green, then yellow, then a blinking red for the last six. */
    float frac = g->lifeTimer / LIFE_SECONDS;
    if (frac < 0.0f) frac = 0.0f;
    Color bar = g->lifeTimer > 12.0f ? (Color){96, 220, 130, 255} : (g->lifeTimer > 6.0f ? kGold : kDanger);
    bool blink = g->lifeTimer <= 6.0f && !Prefs_Get()->reducedFlashing && fmodf(time, 0.4f) < 0.2f;
    DrawRectangle(FIELD_X, 96, FIELD_W, 6, kRule);
    if (!blink) DrawRectangle(FIELD_X, 96, (int)((float)FIELD_W * frac), 6, bar);

    Ui_TextCentered("Arrows or WASD hop    P pause    R restart    Esc menu", UI_T8, WINDOW_WIDTH / 2, WINDOW_HEIGHT - 17, kFaint);
}

/* ------------------------------------------------------------------ hare */

static void DrawHare(const Game *g) {
    float hx, hy;
    Game_HarePos(g, &hx, &hy);
    int px = TileX(hx) + 6, py = TileY(hy) + 6;

    if (!g->hare.alive) {
        float age = DEATH_SECONDS - g->deathTimer;
        int cx = TileX(hx) + TILE / 2, cy = TileY(hy) + TILE / 2;
        if (g->deathCause == DEATH_CAR) {
            /* flattened, with a few stars */
            DrawRectangle(cx - 18, cy - 4, 36, 8, kHare);
            DrawRectangle(cx - 8, cy - 4, 4, 4, kEye);
            DrawRectangle(cx + 4, cy - 4, 4, 4, kEye);
            for (int k = 0; k < 3; k++) {
                float a = age * 6.0f + (float)k * 2.1f;
                DrawRectangle(cx + (int)(sinf(a) * 20.0f) - 2, cy - 14 + (int)(cosf(a) * 4.0f), 4, 4, kGold);
            }
        } else if (g->deathCause == DEATH_WATER || g->deathCause == DEATH_OFFSCREEN) {
            /* rings spreading where it went in, and the ears going under */
            for (int k = 0; k < 3; k++) {
                float rr = (age * 40.0f + (float)k * 10.0f);
                if (rr < 40.0f) DrawRectangleLines(cx - (int)rr, cy - (int)rr / 2, (int)rr * 2, (int)rr, (Color){190, 215, 255, 255});
            }
            if (age < 0.5f) DrawHareArt(px, py + (int)(age * 40.0f), 4, true);
        } else {
            float k = age / DEATH_SECONDS;
            if (k < 0.8f) DrawRectangle(cx - 12, cy - 12 - (int)(k * 14.0f), 24 - (int)(k * 20.0f), 24 - (int)(k * 20.0f), Fade(kHare, 1.0f - k));
        }
        return;
    }
    if (g->homePause > 0.0f) return; /* it just got home: the burrow shows it */

    int lift = 0, stretch = 0;
    if (g->hare.hopping) {
        float t = g->hare.hopT / HOP_TIME;
        if (t > 1.0f) t = 1.0f;
        lift = (int)(sinf(t * 3.14159f) * 10.0f);
        stretch = (int)(sinf(t * 3.14159f) * 4.0f);
    }
    /* a shadow on the ground, then the hare a little above it */
    DrawRectangle(px + 4, py + 26, 24, 4, (Color){0, 0, 0, 60});
    DrawHareArt(px - stretch / 4, py - lift - stretch / 2, 4, true);
}

static void DrawGameOverOverlay(const Game *g, const FrameInfo *info) {
    DrawRectangle(FIELD_X, FIELD_Y, FIELD_W, FIELD_H, (Color){13, 10, 30, 224});
    int cx = FIELD_X + FIELD_W / 2, cy = FIELD_Y + FIELD_H / 2;
    Ui_TextCentered("GAME OVER", UI_T32, cx, cy - 120, kDanger);
    char buf[96];
    snprintf(buf, sizeof(buf), "Level %d, and the road won", g->level);
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

static const char *DeathLine(DeathCause c) {
    switch (c) {
        case DEATH_CAR: return "Run over";
        case DEATH_WATER: return "Fell in the river";
        case DEATH_OFFSCREEN: return "Carried away";
        case DEATH_TIME: return "Out of time";
        case DEATH_WALL: return "That's hedge";
        case DEATH_BAY_FULL: return "That burrow is taken";
        case DEATH_CROC: return "There was a crocodile";
        default: return "";
    }
}

void Render_Frame(const Game *g, const FrameInfo *info) {
    float dt = GetFrameTime();
    float time = (float)GetTime();
    ReactToEvents(g);
    sBannerAge += dt;
    sExtraAge += dt;

    Win_BeginFrame();
    ClearBackground(Ui_Theme()->bg);
    DrawHud(g, time);
    DrawRectangle(FIELD_X - 4, FIELD_Y - 4, FIELD_W + 8, FIELD_H + 8, kRule);

    BeginScissorMode(FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    DrawWorld(g, time);
    for (int r = RIVER_FIRST; r <= RIVER_LAST; r++) DrawPlatforms(g, r, time);
    for (int r = ROAD_FIRST; r <= ROAD_LAST; r++) DrawVehicles(g, r);
    DrawBayGuests(g, time);
    DrawGoodie(g, time);
    DrawHare(g);
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
    if (g->phase == GS_PLAYING && !g->hare.alive) Ui_TextCentered(DeathLine(g->deathCause), UI_T16, cx, FIELD_Y + 6 * TILE + 14, kText);
    if (g->phase == GS_PLAYING && sBannerAge < 1.6f) {
        char buf[24];
        snprintf(buf, sizeof(buf), "LEVEL %d", sBannerLevel);
        int w = Ui_Measure(buf, UI_T32);
        int y = FIELD_Y + 6 * TILE - 6;
        DrawRectangle(cx - w / 2 - 20, y - 12, w + 40, 56, (Color){13, 10, 30, 225});
        DrawRectangle(cx - w / 2 - 20, y + 40, w + 40, 4, kLight);
        Ui_TextCentered(buf, UI_T32, cx, y, kLight);
    }
    if (g->phase == GS_PAUSED) Ui_PauseOverlay(FIELD_X, FIELD_Y, FIELD_W, FIELD_H);
    if (g->phase == GS_GAMEOVER) DrawGameOverOverlay(g, info);
    if (info->showFps) DrawFPS(10, WINDOW_HEIGHT - 24);
    Win_EndFrame();
}

/* Faint traffic crossing behind the menu: a few rectangles and a clock. */
void Render_MenuBackdrop(float dt) {
    static float sTime = 0.0f;
    sTime += dt;
    static const Color kBodies[4] = {{255, 104, 96, 255}, {110, 160, 255, 255}, {150, 244, 160, 255}, {255, 150, 60, 255}};
    for (int lane = 0; lane < 5; lane++) {
        int y = 190 + lane * 46;
        float speed = 40.0f + (float)lane * 22.0f;
        int dir = (lane % 2) ? 1 : -1;
        for (int k = 0; k < 4; k++) {
            float x = fmodf((float)(k * 240 + lane * 97) + sTime * speed * (float)dir + 4000.0f, (float)(WINDOW_WIDTH + 200)) - 100.0f;
            Color c = kBodies[(lane + k) % 4];
            c.a = 34;
            DrawRectangle((int)x, y, lane == 2 ? 100 : 44, 26, c);
        }
    }
}
