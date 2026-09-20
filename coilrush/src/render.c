#include "winscale.h"
#include "render.h"
#include "ui.h"
#include "prefs.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define BOARD_PIXEL_W (GRID_W * CELL_SIZE)
#define BOARD_PIXEL_H (GRID_H * CELL_SIZE)
#define BOARD_X ((WINDOW_WIDTH - BOARD_PIXEL_W) / 2)
#define BOARD_Y 116

/* --- Look: this window is the screen of the Coilrush cabinet in Ghost
 * Launcher's arcade hall, so it lives in the hall's indigo rather than a
 * neutral black, and green is the machine's own light -- used for the snake
 * and the title, not sprayed over the chrome. */
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
static const Color kChecker = {19, 15, 42, 255};
static const Color kSnakeHead = {150, 244, 160, 255};
static const Color kSnakeTail = {32, 136, 120, 255};
static const Color kFood = {255, 79, 120, 255};
static const Color kBonus = {255, 210, 63, 255};
static const Color kWall = {88, 78, 150, 255};
static const Color kTongue = {255, 79, 120, 255};
static const Color kRelicColors[RELIC_COUNT] = {{150, 210, 255, 255}, {200, 160, 255, 255}, {255, 190, 90, 255}};

static const char *kModeTitle[MODE_COUNT] = {"Classic", "Wrap", "Maze"};

/* Filled tile with a light/dark bevel so cells read as solid blocks rather
 * than flat rectangles, entirely procedural (no image assets). `inset`
 * shrinks the tile inside its cell; negative grows it (the food bulge). */
static void DrawTileAt(float x, float y, int size, Color base, int inset) {
    int s = size - inset * 2;
    int px = (int)roundf(x) + inset, py = (int)roundf(y) + inset;
    int bevel = s >= 16 ? 3 : 2;

    DrawRectangle(px, py, s, s, base);

    Color hi = base;
    hi.r = (unsigned char)((int)hi.r + (255 - hi.r) / 3);
    hi.g = (unsigned char)((int)hi.g + (255 - hi.g) / 3);
    hi.b = (unsigned char)((int)hi.b + (255 - hi.b) / 3);
    DrawRectangle(px, py, s, bevel, hi);
    DrawRectangle(px, py, bevel, s, hi);

    Color lo = base;
    lo.r = (unsigned char)(lo.r * 2 / 3);
    lo.g = (unsigned char)(lo.g * 2 / 3);
    lo.b = (unsigned char)(lo.b * 2 / 3);
    DrawRectangle(px, py + s - bevel, s, bevel, lo);
    DrawRectangle(px + s - bevel, py, bevel, s, lo);
}

static void DrawTile(int col, int row, Color base, int inset) {
    DrawTileAt((float)(BOARD_X + col * CELL_SIZE), (float)(BOARD_Y + row * CELL_SIZE), CELL_SIZE, base, inset);
}

/* ----------------------------------------------------------------- board */

static const int kDirDX[4] = {0, 1, 0, -1};
static const int kDirDY[4] = {-1, 0, 1, 0};

static void DrawHead(float x, float y, Dir dir, Color color, int inset, bool tongueOut) {
    int ix = (int)roundf(x), iy = (int)roundf(y);

    if (tongueOut) {
        /* A stem out of the mouth, then a two-pixel fork. */
        int cx = ix + CELL_SIZE / 2, cy = iy + CELL_SIZE / 2;
        int dx = kDirDX[dir], dy = kDirDY[dir];
        for (int k = 3; k <= 4; k++) DrawRectangle(cx + dx * k * 4 - 2, cy + dy * k * 4 - 2, 4, 4, kTongue);
        DrawRectangle(cx + dx * 20 - 2 + dy * 4, cy + dy * 20 - 2 + dx * 4, 4, 4, kTongue);
        DrawRectangle(cx + dx * 20 - 2 - dy * 4, cy + dy * 20 - 2 - dx * 4, 4, 4, kTongue);
    }

    DrawTileAt(x, y, CELL_SIZE, color, inset);

    /* Two dark pixels on the leading half, facing the way it moves. */
    int e = 4, nearEdge = 5, side = 5, far = CELL_SIZE - side - e;
    Color eye = {13, 10, 30, 255};
    switch (dir) {
        case DIR_UP:
            DrawRectangle(ix + side, iy + nearEdge, e, e, eye);
            DrawRectangle(ix + far, iy + nearEdge, e, e, eye);
            break;
        case DIR_DOWN:
            DrawRectangle(ix + side, iy + CELL_SIZE - nearEdge - e, e, e, eye);
            DrawRectangle(ix + far, iy + CELL_SIZE - nearEdge - e, e, e, eye);
            break;
        case DIR_LEFT:
            DrawRectangle(ix + nearEdge, iy + side, e, e, eye);
            DrawRectangle(ix + nearEdge, iy + far, e, e, eye);
            break;
        case DIR_RIGHT:
            DrawRectangle(ix + CELL_SIZE - nearEdge - e, iy + side, e, e, eye);
            DrawRectangle(ix + CELL_SIZE - nearEdge - e, iy + far, e, e, eye);
            break;
    }
}

static void DrawBoardFrame(GameMode mode) {
    int x = BOARD_X - 4, y = BOARD_Y - 4;
    int w = BOARD_PIXEL_W + 8, h = BOARD_PIXEL_H + 8;

    if (mode != MODE_WRAP) {
        DrawRectangle(x, y, w, h, kRule);
    } else {
        /* Wrap mode: a dashed green frame says "these edges are open". */
        Color dash = {44, 120, 96, 255};
        int dashLen = CELL_SIZE / 2;
        for (int i = 0; i * dashLen < w; i += 2) {
            int dx = x + i * dashLen;
            int len = dashLen;
            if (dx + len > x + w) len = x + w - dx;
            DrawRectangle(dx, y, len, 4, dash);
            DrawRectangle(dx, y + h - 4, len, 4, dash);
        }
        for (int i = 0; i * dashLen < h; i += 2) {
            int dy = y + i * dashLen;
            int len = dashLen;
            if (dy + len > y + h) len = y + h - dy;
            DrawRectangle(x, dy, 4, len, dash);
            DrawRectangle(x + w - 4, dy, 4, len, dash);
        }
    }

    DrawRectangle(BOARD_X, BOARD_Y, BOARD_PIXEL_W, BOARD_PIXEL_H, kBoardBg);
    /* Checker tint instead of grid lines: keeps cells countable without the
     * visual noise of 50 lines behind a fast-moving snake. */
    for (int row = 0; row < GRID_H; row++) {
        for (int col = (row & 1); col < GRID_W; col += 2) {
            DrawRectangle(BOARD_X + col * CELL_SIZE, BOARD_Y + row * CELL_SIZE, CELL_SIZE, CELL_SIZE, kChecker);
        }
    }
}

/* Render-only state: how long the snake has been dead, how many segments
 * have already burst, and the last pickup a particle burst was fired for. */
static float sDeadFor = 0.0f;
static int sPopped = 0;
static int sSeenEatCount = 0;

static float PopRate(const Game *g) {
    float rate = (float)g->length / 0.9f; /* the whole body goes in under a second... */
    return rate < 24.0f ? 24.0f : rate;   /* ...but a short snake still pops briskly */
}

/* How far through the current step the snake should be drawn, 0..1. The
 * rules move a whole cell at once; drawing glides between the two. */
static float GlideT(const Game *g) {
    if (g->stepsOnBoard == 0 || g->phase == GS_GAMEOVER) return 1.0f;
    float t = g->stepInterval > 0.0f ? g->stepTimer / g->stepInterval : 1.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t;
}

static void DrawSnake(const Game *g, float t, float time) {
    bool dead = (g->phase == GS_GAMEOVER && !g->won);

    /* Tail first so the head always ends up on top. */
    for (int i = g->length - 1; i >= 0; i--) {
        if (dead && i < sPopped) continue;

        Cell to = Game_Segment(g, i);
        Cell from = to;
        if (i < g->length - 1) from = Game_Segment(g, i + 1);
        else if (g->tailMoved) from = g->prevTail;

        float k = g->length > 1 ? (float)i / (float)(g->length - 1) : 0.0f;
        Color color = Ui_Lerp(kSnakeHead, kSnakeTail, k);
        if (dead) color = Ui_Lerp(color, kDanger, 0.6f);
        if (g->phaseSteps > 0) {
            /* Phasing: see-through, shimmering, and it flickers as the spell runs out. */
            float shimmer = 0.5f + 0.12f * sinf(time * 9.0f + (float)i * 0.6f);
            if (g->phaseSteps <= 6 && !Prefs_Get()->reducedFlashing && sinf(time * 16.0f) < 0.0f) shimmer = 0.85f;
            color = Ui_Lerp(color, kRelicColors[RELIC_PHASE], 0.45f);
            color.a = (unsigned char)(255.0f * shimmer);
        }

        /* The swallowed-food bulge: a bump that peaks on the segment it is
         * passing through and tapers onto its neighbours. */
        int inset = 1;
        for (int b = 0; b < MAX_BULGES; b++) {
            if (g->bulgeAge[b] < 0) continue;
            float dist = fabsf((float)i - ((float)g->bulgeAge[b] - 1.0f + t));
            if (dist < 1.0f && inset > -2) inset = dist < 0.5f ? -2 : 0;
        }

        int dx = to.x - from.x, dy = to.y - from.y;
        bool wraps = (dx > 1 || dx < -1 || dy > 1 || dy < -1);
        if (wraps) { /* crossed an open edge: the real step was one cell the other way */
            dx = dx > 1 ? -1 : (dx < -1 ? 1 : dx);
            dy = dy > 1 ? -1 : (dy < -1 ? 1 : dy);
        }

        /* Entering position: slide into `to` from one cell back along the
         * step. For an ordinary step that is simply from -> to. */
        float x = BOARD_X + ((float)to.x - (float)dx * (1.0f - t)) * CELL_SIZE;
        float y = BOARD_Y + ((float)to.y - (float)dy * (1.0f - t)) * CELL_SIZE;
        bool tongue = (i == 0) && !dead && fmodf(time, 2.6f) < 0.22f;

        if (i == 0) DrawHead(x, y, g->dir, color, inset, tongue);
        else DrawTileAt(x, y, CELL_SIZE, color, inset);

        if (wraps && t < 1.0f) { /* and the half of it still leaving the far edge */
            float lx = BOARD_X + ((float)from.x + (float)dx * t) * CELL_SIZE;
            float ly = BOARD_Y + ((float)from.y + (float)dy * t) * CELL_SIZE;
            if (i == 0) DrawHead(lx, ly, g->dir, color, inset, false);
            else DrawTileAt(lx, ly, CELL_SIZE, color, inset);
        }
    }
}

static void DrawPickup(Cell c, bool bonus, float time, int stepsLeft, float scale) {
    int shrink = (int)((1.0f - scale) * 7);
    if (bonus) {
        bool urgent = stepsLeft <= BONUS_LIFETIME_STEPS / 4;
        float blinkRate = urgent ? 14.0f : 5.0f;
        if (urgent && !Prefs_Get()->reducedFlashing && sinf(time * blinkRate) <= -0.3f) return;
        Color col = Ui_Lerp(kBonus, WHITE, 0.25f + 0.25f * sinf(time * blinkRate));
        DrawTile(c.x, c.y, col, 3 + shrink);
        return;
    }
    int inset = 4 + (int)(1.5f + 1.5f * sinf(time * 6.0f)) + shrink;
    if (inset > 10) inset = 10;
    DrawTile(c.x, c.y, kFood, inset);
    if (scale > 0.6f) { /* stem */
        DrawRectangle(BOARD_X + c.x * CELL_SIZE + CELL_SIZE / 2 - 1, BOARD_Y + c.y * CELL_SIZE + 2, 3, 4, kLight);
    }
}

static void DrawRelic(const Game *g, float time) {
    if (!g->relicActive) return;
    bool urgent = g->relicStepsLeft <= 15;
    if (urgent && !Prefs_Get()->reducedFlashing && sinf(time * 14.0f) <= -0.3f) return;
    Color c = kRelicColors[g->relicType];
    float cx = BOARD_X + (g->relic.x + 0.5f) * CELL_SIZE, cy = BOARD_Y + (g->relic.y + 0.5f) * CELL_SIZE + 2.0f * sinf(time * 4.0f);
    DrawCircle((int)cx, (int)cy, 11.0f + 1.5f * sinf(time * 6.0f), Fade(c, 0.22f));
    DrawCircle((int)cx, (int)cy, 7.0f, Fade(c, 0.9f));
    static const char letters[RELIC_COUNT] = {'P', 'S', 'X'};
    char s[2] = {letters[g->relicType], 0};
    Ui_TextCentered(s, UI_T8, (int)cx + 1, (int)cy - 3, (Color){13, 10, 30, 255});
}

static void DrawBoard(const Game *g, float dt) {
    float time = (float)GetTime();
    float t = GlideT(g);
    bool dead = (g->phase == GS_GAMEOVER && !g->won);

    /* Death: a short, decaying shake of the playfield only. */
    Camera2D cam = {0};
    cam.zoom = 1.0f;
    if (dead && sDeadFor < 0.35f) {
        int amp = (int)(7.0f * (1.0f - sDeadFor / 0.35f));
        cam.offset = (Vector2){(float)GetRandomValue(-amp, amp), (float)GetRandomValue(-amp, amp)};
    }
    BeginMode2D(cam);

    DrawBoardFrame(g->mode);

    for (int row = 0; row < GRID_H; row++) {
        for (int col = 0; col < GRID_W; col++) {
            if (g->grid[row][col] == CELL_WALL) DrawTile(col, row, kWall, 0);
        }
    }

    if (g->grid[g->food.y][g->food.x] == CELL_FOOD) DrawPickup(g->food, false, time, 0, 1.0f);
    if (g->bonusActive) DrawPickup(g->bonus, true, time, g->bonusStepsLeft, 1.0f);
    DrawRelic(g, time);

    /* The rules swallow a pickup one step before the drawn head gets there,
     * so keep showing it, shrinking, until the head arrives. */
    bool pickupInFlight = (g->stepsSinceEat == 0 && g->stepsOnBoard > 0 && g->phase != GS_GAMEOVER);
    if (pickupInFlight && t < 0.7f) DrawPickup(g->lastEatCell, g->lastEatWasBonus, time, BONUS_LIFETIME_STEPS, 1.0f - t);

    if (g->eatCount < sSeenEatCount) sSeenEatCount = g->eatCount; /* a new run */
    if (g->eatCount > sSeenEatCount && (!pickupInFlight || t >= 0.7f)) {
        sSeenEatCount = g->eatCount;
        if (g->stepsOnBoard > 0) {
            Ui_Burst(BOARD_X + (g->lastEatCell.x + 0.5f) * CELL_SIZE, BOARD_Y + (g->lastEatCell.y + 0.5f) * CELL_SIZE,
                       g->lastEatWasBonus ? kBonus : kFood, g->lastEatWasBonus ? 22 : 10, g->lastEatWasBonus ? 260.0f : 170.0f);
        }
    }

    if (g->justRelic) {
        Cell h = Game_Segment(g, 0);
        Ui_Burst(BOARD_X + (h.x + 0.5f) * CELL_SIZE, BOARD_Y + (h.y + 0.5f) * CELL_SIZE, kRelicColors[g->lastRelic], 18, 210.0f);
    }

    /* Segments burst one after another from the head down. */
    if (dead) {
        int shouldHavePopped = (int)(sDeadFor * PopRate(g));
        if (shouldHavePopped > g->length) shouldHavePopped = g->length;
        while (sPopped < shouldHavePopped) {
            Cell c = Game_Segment(g, sPopped);
            float k = g->length > 1 ? (float)sPopped / (float)(g->length - 1) : 0.0f;
            Ui_Burst(BOARD_X + (c.x + 0.5f) * CELL_SIZE, BOARD_Y + (c.y + 0.5f) * CELL_SIZE,
                       Ui_Lerp(Ui_Lerp(kSnakeHead, kSnakeTail, k), kDanger, 0.5f), 4, 150.0f);
            sPopped++;
        }
    }

    BeginScissorMode(BOARD_X, BOARD_Y, BOARD_PIXEL_W, BOARD_PIXEL_H); /* wrapped halves poke past the edge */
    DrawSnake(g, t, time);
    EndScissorMode();

    Ui_UpdateParticles(dt);
    EndMode2D();
}

static void DrawStat(int x, int y, const char *label, const char *value) {
    Ui_Text(label, x, y, UI_T8, kTextDim);
    Ui_Text(value, x, y + 14, UI_T16, kText);
}

static void DrawHud(const Game *g) {
    Ui_Text("COILRUSH", BOARD_X, 16, UI_T16, kLight);

    {
        char sb[24];
        int sx = BOARD_X + 200;
        if (g->phaseSteps > 0) { snprintf(sb, sizeof(sb), "PHASE %d", g->phaseSteps); Ui_Text(sb, sx, 20, UI_T8, kRelicColors[RELIC_PHASE]); sx += Ui_Measure(sb, UI_T8) + 14; }
        if (g->slowSteps > 0) { snprintf(sb, sizeof(sb), "SLOW %d", g->slowSteps); Ui_Text(sb, sx, 20, UI_T8, kRelicColors[RELIC_SLOW]); sx += Ui_Measure(sb, UI_T8) + 14; }
        if (g->surgeSteps > 0) { snprintf(sb, sizeof(sb), "x2 %d", g->surgeSteps); Ui_Text(sb, sx, 20, UI_T8, kRelicColors[RELIC_SURGE]); }
    }

    char modeBuf[32];
    snprintf(modeBuf, sizeof(modeBuf), "%s mode", kModeTitle[g->mode]);
    Ui_TextRight(modeBuf, UI_T8, BOARD_X + BOARD_PIXEL_W, 20, kTextDim);

    char buf[64];
    int colW = BOARD_PIXEL_W / 4;
    int statY = 50;

    snprintf(buf, sizeof(buf), "%ld", g->score);
    DrawStat(BOARD_X, statY, "Score", buf);

    long high = g->highScore > g->score ? g->highScore : g->score;
    snprintf(buf, sizeof(buf), "%ld", high);
    DrawStat(BOARD_X + colW, statY, "Best", buf);

    snprintf(buf, sizeof(buf), "%d", g->length);
    DrawStat(BOARD_X + colW * 2, statY, "Length", buf);

    snprintf(buf, sizeof(buf), "%d", g->level);
    if (g->mode == MODE_MAZE) {
        DrawStat(BOARD_X + colW * 3, statY, "Stage", buf);
        /* Food left on this stage, as pips rather than another number. */
        int px = BOARD_X + colW * 3 + Ui_Measure(buf, UI_T16) + 12;
        for (int i = 0; i < FOOD_PER_STAGE; i++) {
            DrawRectangle(px + i * 10, statY + 20, 6, 6, i < g->stageFood ? kFood : kRule);
        }
    } else {
        DrawStat(BOARD_X + colW * 3, statY, "Level", buf);
    }

    /* Bonus countdown bar, only while a bonus is on the board. */
    if (g->bonusActive && g->phase != GS_GAMEOVER) {
        int barY = BOARD_Y - 16;
        DrawRectangle(BOARD_X, barY, BOARD_PIXEL_W, 4, kRule);
        DrawRectangle(BOARD_X, barY, BOARD_PIXEL_W * g->bonusStepsLeft / BONUS_LIFETIME_STEPS, 4, kBonus);
    }

    Ui_TextCentered("Arrows or WASD steer    P pause    R restart    Esc menu", UI_T8,
                  WINDOW_WIDTH / 2, WINDOW_HEIGHT - 17, kFaint);
}

static void DrawReadyOverlay(const Game *g) {
    int cx = BOARD_X + BOARD_PIXEL_W / 2;
    int y = BOARD_Y + BOARD_PIXEL_H / 2 - 120;
    char buf[32];
    if (g->mode == MODE_MAZE) snprintf(buf, sizeof(buf), "STAGE %d", g->level);
    else snprintf(buf, sizeof(buf), "READY");
    int w = Ui_Measure(buf, UI_T32);
    DrawRectangle(cx - w / 2 - 24, y - 16, w + 48, 64, (Color){13, 10, 30, 225});
    DrawRectangle(cx - w / 2 - 24, y + 44, w + 48, 4, kLight);
    Ui_TextCentered(buf, UI_T32, cx, y, kLight);
}

static const char *DeathLine(const Game *g) {
    switch (g->deathCause) {
        case DEATH_EDGE: return "You ran off the edge";
        case DEATH_WALL: return "You hit a wall";
        case DEATH_SELF: return "You bit your own tail";
        default: return "";
    }
}

static void DrawGameOverOverlay(const Game *g, const FrameInfo *info) {
    DrawRectangle(BOARD_X, BOARD_Y, BOARD_PIXEL_W, BOARD_PIXEL_H, (Color){13, 10, 30, 220});
    int cx = BOARD_X + BOARD_PIXEL_W / 2;
    int cy = BOARD_Y + BOARD_PIXEL_H / 2;

    if (g->won) {
        Ui_TextCentered("BOARD FULL", UI_T32, cx, cy - 120, kLight);
        Ui_TextCentered("There is nowhere left to go", UI_T8, cx, cy - 72, kTextDim);
    } else {
        Ui_TextCentered("GAME OVER", UI_T32, cx, cy - 120, kDanger);
        Ui_TextCentered(DeathLine(g), UI_T8, cx, cy - 72, kTextDim);
    }

    char buf[96];
    snprintf(buf, sizeof(buf), "%ld", g->score);
    Ui_TextCentered(buf, UI_T48, cx, cy - 40, kText);
    snprintf(buf, sizeof(buf), "%d long", g->length);
    Ui_TextCentered(buf, UI_T8, cx, cy + 20, kTextDim);

    if (g->score >= g->highScore && g->score > 0) {
        Ui_TextCentered("A new best", UI_T16, cx, cy + 48, kLight);
    } else {
        snprintf(buf, sizeof(buf), "Best %ld", g->highScore);
        Ui_TextCentered(buf, UI_T16, cx, cy + 48, kTextDim);
    }

    if (info->lastRank > 0) {
        snprintf(buf, sizeof(buf), "%.16s is #%d on the %s table", info->username, info->lastRank, kModeTitle[g->mode]);
        Ui_TextCentered(buf, UI_T8, cx, cy + 84, kBonus);
    }

    Ui_TextCentered("R plays again    Esc for the menu", UI_T8, cx, cy + 124, kTextDim);
}

/* --- Menu background: a fixed pool of snakes crawling across the screen
 * with a gentle wiggle. Cheap by construction: no allocation, no per-frame
 * texture work, just arithmetic over a small fixed array. Only ever drawn
 * on the menu screen, never during gameplay. */
#define BG_SNAKE_COUNT 8

typedef struct {
    float x, y;
    float speed; /* px/s, sign = direction */
    int segments;
    float size;
    float phase;
    Color color;
} BgSnake;

static BgSnake sBgSnakes[BG_SNAKE_COUNT];
static bool sBgInitialized = false;

static void RespawnBgSnake(BgSnake *s, bool firstFill) {
    static const Color kPalette[4] = {
        {96, 220, 130, 255}, {32, 136, 120, 255}, {150, 244, 160, 255}, {33, 212, 200, 255},
    };
    s->segments = GetRandomValue(5, 11);
    s->size = (float)(GetRandomValue(2, 3) * 8);
    s->y = (float)GetRandomValue(30, WINDOW_HEIGHT - 30);
    s->speed = (float)GetRandomValue(40, 95) * (GetRandomValue(0, 1) ? 1.0f : -1.0f);
    s->phase = (float)GetRandomValue(0, 628) / 100.0f;
    s->color = kPalette[GetRandomValue(0, 3)];

    if (firstFill) {
        /* Scatter across the whole width so the first frame isn't empty. */
        s->x = (float)GetRandomValue(0, WINDOW_WIDTH);
    } else {
        s->x = s->speed > 0 ? -s->size : (float)WINDOW_WIDTH + s->size;
    }
}

void Render_MenuBackdrop(float dt) {
    if (!sBgInitialized) {
        for (int i = 0; i < BG_SNAKE_COUNT; i++) RespawnBgSnake(&sBgSnakes[i], true);
        sBgInitialized = true;
    }

    float time = (float)GetTime();
    for (int i = 0; i < BG_SNAKE_COUNT; i++) {
        BgSnake *s = &sBgSnakes[i];
        s->x += s->speed * dt;

        float step = s->size + 2.0f;
        float span = (float)s->segments * step;
        bool gone = s->speed > 0 ? (s->x - span > WINDOW_WIDTH) : (s->x + span < 0);
        if (gone) RespawnBgSnake(s, false);

        float dirSign = s->speed > 0 ? 1.0f : -1.0f;
        for (int k = 0; k < s->segments; k++) {
            float sx = s->x - dirSign * (float)k * step;
            float sy = s->y + sinf(time * 2.2f + s->phase + (float)k * 0.55f) * 5.0f;
            Color c = s->color;
            c.a = (unsigned char)(k == 0 ? 70 : 42);
            DrawRectangle((int)(sx - s->size / 2), (int)(sy - s->size / 2), (int)s->size, (int)s->size, c);
        }
    }
}

void Render_Frame(const Game *g, const FrameInfo *info) {
    float dt = GetFrameTime();
    bool dead = (g->phase == GS_GAMEOVER && !g->won);
    if (dead) {
        sDeadFor += dt;
    } else {
        sDeadFor = 0.0f;
        sPopped = 0;
    }

    Win_BeginFrame();
    ClearBackground(kBg);

    DrawHud(g);
    DrawBoard(g, dt);

    if (g->phase == GS_PLAYING && g->startDelay > 0.0f) DrawReadyOverlay(g);
    if (g->phase == GS_PAUSED) Ui_PauseOverlay(BOARD_X, BOARD_Y, BOARD_PIXEL_W, BOARD_PIXEL_H);
    if (g->phase == GS_GAMEOVER) {
        /* Let the snake finish bursting before the card covers it. */
        float popTime = dead ? (float)g->length / PopRate(g) + 0.3f : 0.0f;
        if (sDeadFor >= popTime) DrawGameOverOverlay(g, info);
    }

    if (info->showFps) DrawFPS(10, WINDOW_HEIGHT - 24);

    Win_EndFrame();
}
