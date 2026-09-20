#include "winscale.h"
#include "render.h"
#include "ui.h"
#include "prefs.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* The board can be any size, so the layout is worked out per frame from the
 * game's board: the cell size shrinks to fit, and the side panel takes what
 * is left. The macros below keep the drawing code reading as it always did. */
#undef CELL_SIZE
#undef BOARD_W
#undef BOARD_H
static int sCell = 30, sBoardX = 40, sBoardY = 56, sBw = 10, sBh = 20, sPanelX = 380;
#define CELL_SIZE sCell
#define BOARD_W sBw
#define BOARD_H sBh
#define BOARD_PIXEL_W (sBw * sCell)
#define BOARD_PIXEL_H (sBh * sCell)
#define BOARD_X sBoardX
#define BOARD_Y sBoardY
#define PANEL_X sPanelX
#define PANEL_W (WINDOW_WIDTH - PANEL_X - 40)

static void Layout(const Game *g) {
    sBw = g->board.w;
    sBh = g->board.h;
    int maxW = WINDOW_WIDTH - 40 - 40 - 40 - 160; /* left margin, gap, right margin, a panel at least 160 wide */
    int maxH = WINDOW_HEIGHT - 56 - 58;
    int cell = 30;
    if (maxW / sBw < cell) cell = maxW / sBw;
    if (maxH / sBh < cell) cell = maxH / sBh;
    if (cell < 12) cell = 12;
    sCell = cell;
    sBoardX = 40;
    sBoardY = 56;
    sPanelX = sBoardX + sBw * sCell + 40;
}

/* --- Look: this window is the screen of the Blockfall cabinet in Ghost
 * Launcher's arcade hall, so it lives in the hall's indigo rather than a
 * neutral black. Violet is the machine's own light (the launcher pulls the
 * same hue out of the icon); the seven piece colours do the rest, so the
 * chrome stays quiet. */
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
static const Color kGrid = {26, 21, 54, 255};

static Color ColorFromU32(uint32_t c) {
    return (Color){
        (unsigned char)((c >> 24) & 0xFF),
        (unsigned char)((c >> 16) & 0xFF),
        (unsigned char)((c >> 8) & 0xFF),
        (unsigned char)(c & 0xFF),
    };
}

/* Filled block with a light/dark bevel so blocks read as solid tiles rather
 * than flat rectangles, entirely procedural (no image assets). */
static void DrawBlockAt(int x, int y, int size, Color base, float alpha) {
    Color c = base;
    c.a = (unsigned char)(255 * alpha);
    int bevel = size >= 24 ? 4 : 2;

    DrawRectangle(x + 1, y + 1, size - 2, size - 2, c);

    Color hi = c;
    hi.r = (unsigned char)((int)hi.r + (255 - hi.r) / 3);
    hi.g = (unsigned char)((int)hi.g + (255 - hi.g) / 3);
    hi.b = (unsigned char)((int)hi.b + (255 - hi.b) / 3);
    DrawRectangle(x + 1, y + 1, size - 2, bevel, hi);
    DrawRectangle(x + 1, y + 1, bevel, size - 2, hi);

    Color lo = c;
    lo.r = (unsigned char)(lo.r * 2 / 3);
    lo.g = (unsigned char)(lo.g * 2 / 3);
    lo.b = (unsigned char)(lo.b * 2 / 3);
    DrawRectangle(x + 1, y + size - 1 - bevel, size - 2, bevel, lo);
    DrawRectangle(x + size - 1 - bevel, y + 1, bevel, size - 2, lo);
}


/* Power-up blocks: a white core and a coloured glyph, pulsing, so you can see
 * at a glance which block of the piece carries what. */
static Color PowerColor(PowerUp p) {
    switch (p) {
        case PU_BOMB: return (Color){255, 96, 84, 255};
        case PU_LASER: return (Color){90, 230, 255, 255};
        case PU_FREEZE: return (Color){176, 222, 255, 255};
        case PU_SLOW: return (Color){255, 192, 84, 255};
        case PU_SWEEP: return (Color){120, 240, 150, 255};
        default: return WHITE;
    }
}

static const char *PowerGlyph(PowerUp p) {
    switch (p) {
        case PU_BOMB: return "B";
        case PU_LASER: return "L";
        case PU_FREEZE: return "F";
        case PU_SLOW: return "S";
        case PU_SWEEP: return "W";
        default: return "";
    }
}

static void DrawPowerMark(int x, int y, int size, PowerUp p) {
    if (p == PU_NONE) return;
    float pulse = 0.5f + 0.5f * sinf((float)GetTime() * 8.0f);
    Color c = PowerColor(p);
    DrawRectangle(x + 3, y + 3, size - 6, size - 6, Fade(WHITE, 0.55f + 0.3f * pulse));
    DrawRectangleLinesEx((Rectangle){(float)x + 1, (float)y + 1, (float)size - 2, (float)size - 2}, 2.0f, Fade(c, 0.65f + 0.35f * pulse));
    if (size >= 20) Ui_TextCentered(PowerGlyph(p), UI_T8, x + size / 2, y + size / 2 - 4, Ui_Lerp(c, BLACK, 0.45f));
    else DrawRectangle(x + size / 2 - 2, y + size / 2 - 2, 4, 4, Ui_Lerp(c, BLACK, 0.35f));
}

static int CellX(int col) { return BOARD_X + col * CELL_SIZE; }
static int CellY(int row) { return BOARD_Y + (row - BOARD_HIDDEN) * CELL_SIZE; }

static void DrawBlock(int col, int row, Color base, float alpha) {
    if (row < BOARD_HIDDEN) return; /* still above the visible field */
    DrawBlockAt(CellX(col), CellY(row), CELL_SIZE, base, alpha);
}

static void DrawGhostBlock(int col, int row, Color base) {
    if (row < BOARD_HIDDEN) return;
    Color outline = base;
    outline.a = 150;
    DrawRectangleLinesEx((Rectangle){(float)CellX(col) + 2, (float)CellY(row) + 2, CELL_SIZE - 4, CELL_SIZE - 4}, 2, outline);
}

/* ---------------------------------------------------------------- effects
 * Render-only reactions to the one-frame events the rules raise. Nothing in
 * here feeds back into the game. */
static struct {
    int rows[4];
    int count;
    float age;      /* line-clear flash */
    Cell lockCells[CELLS_PER_PIECE];
    int lockX, lockY;
    Color lockColor;
    float lockAge;  /* landed-piece flash */
    float shakeAge, shakeAmp;
    char banner[24];
    Color bannerColor;
    float bannerAge;
    int blastX, blastY;   /* a bomb going off */
    float blastAge;
    int laserX;           /* a laser firing */
    float laserAge;
    Color powerColor;
} sFx = {.age = 9.0f, .lockAge = 9.0f, .shakeAge = 9.0f, .bannerAge = 9.0f, .blastAge = 9.0f, .laserAge = 9.0f};

#define FLASH_TIME 0.30f
#define LOCK_FLASH_TIME 0.14f
#define SHAKE_TIME 0.16f
#define BANNER_TIME 1.1f

static void SetBanner(const char *text, Color color) {
    snprintf(sFx.banner, sizeof(sFx.banner), "%s", text);
    sFx.bannerColor = color;
    sFx.bannerAge = 0.0f;
}

static void ReactToEvents(const Game *g) {
    if (!g->justLocked) {
        if (g->lastWasLevelUp) {
            char buf[24];
            snprintf(buf, sizeof(buf), "LEVEL %d", g->level);
            SetBanner(buf, kLight);
        }
        return;
    }

    Color pieceColor = ColorFromU32(Tetromino_Color(g->lastLockType));

    if (g->justZenReset) SetBanner("FRESH START", kLight);
    if (g->lastPower != PU_NONE) {
        sFx.powerColor = PowerColor(g->lastPower);
        float cx = (float)(CellX(g->lastPowerX) + CELL_SIZE / 2), cy = (float)(CellY(g->lastPowerY) + CELL_SIZE / 2);
        char pb[24];
        snprintf(pb, sizeof(pb), "%s", Game_PowerName(g->lastPower));
        for (char *q = pb; *q; q++) if (*q >= 'a' && *q <= 'z') *q = (char)(*q - 32);
        SetBanner(pb, sFx.powerColor);
        if (g->lastPower == PU_BOMB) {
            sFx.blastX = g->lastPowerX; sFx.blastY = g->lastPowerY; sFx.blastAge = 0.0f;
            Ui_Burst(cx, cy, sFx.powerColor, 26, 260.0f);
            Ui_Burst(cx, cy, WHITE, 12, 180.0f);
            sFx.shakeAge = 0.0f; sFx.shakeAmp = 9.0f;
        } else if (g->lastPower == PU_LASER) {
            sFx.laserX = g->lastPowerX; sFx.laserAge = 0.0f;
            for (int r = 0; r < BOARD_H; r += 2) Ui_Burst(cx, (float)(BOARD_Y + r * CELL_SIZE), sFx.powerColor, 2, 120.0f);
        } else {
            Ui_Burst(cx, cy, sFx.powerColor, 16, 170.0f);
        }
    }

    if (g->lastClearCount > 0) {
        sFx.count = g->lastClearCount > 4 ? 4 : g->lastClearCount;
        for (int i = 0; i < sFx.count; i++) sFx.rows[i] = g->lastClearRows[i];
        sFx.age = 0.0f;
        for (int i = 0; i < sFx.count; i++) {
            int y = CellY(sFx.rows[i]) + CELL_SIZE / 2;
            for (int col = 0; col < BOARD_W; col++) {
                Ui_Burst((float)(CellX(col) + CELL_SIZE / 2), (float)y,
                           (col & 1) ? pieceColor : kText, sFx.count >= 4 ? 3 : 2, 210.0f);
            }
        }
        if (sFx.count >= 4) {
            SetBanner("QUAD", kGold);
            sFx.shakeAge = 0.0f;
            sFx.shakeAmp = 8.0f;
        } else if (g->lastCombo >= 2 && g->lastPower == PU_NONE) {
            char cb[24];
            snprintf(cb, sizeof(cb), "COMBO %d", g->lastCombo);
            SetBanner(cb, kLight);
        }
    } else {
        Tetromino_GetCells(g->lastLockType, g->lastLockRotation, sFx.lockCells);
        sFx.lockX = g->lastLockX;
        sFx.lockY = g->lastLockY;
        sFx.lockColor = pieceColor;
        sFx.lockAge = 0.0f;
    }

    if (g->justHardDropped && g->lastDropCells > 0) {
        /* A thud scaled by how far it fell, and dust off its underside. */
        float amp = 2.0f + (float)g->lastDropCells * 0.35f;
        if (amp > sFx.shakeAmp || sFx.shakeAge >= SHAKE_TIME) {
            sFx.shakeAmp = amp > 7.0f ? 7.0f : amp;
            sFx.shakeAge = 0.0f;
        }
        Cell cells[CELLS_PER_PIECE];
        Tetromino_GetCells(g->lastLockType, g->lastLockRotation, cells);
        for (int i = 0; i < CELLS_PER_PIECE; i++) {
            int row = g->lastLockY + cells[i].y;
            if (row < BOARD_HIDDEN) continue;
            Ui_Burst((float)(CellX(g->lastLockX + cells[i].x) + CELL_SIZE / 2), (float)(CellY(row) + CELL_SIZE),
                       Ui_Lerp(pieceColor, kText, 0.5f), 3, 120.0f);
        }
    }

    if (g->lastWasLevelUp && sFx.count < 4) {
        char buf[24];
        snprintf(buf, sizeof(buf), "LEVEL %d", g->level);
        SetBanner(buf, kLight);
    }
}

static void DrawBoard(const Game *g, float dt) {
    sFx.age += dt;
    sFx.lockAge += dt;
    sFx.shakeAge += dt;
    sFx.bannerAge += dt;
    sFx.blastAge += dt;
    sFx.laserAge += dt;

    Camera2D cam = {0};
    cam.zoom = 1.0f;
    if (Prefs_Get()->screenShake && sFx.shakeAge < SHAKE_TIME) {
        /* Vertical only: the thing that happened was a drop. */
        float k = 1.0f - sFx.shakeAge / SHAKE_TIME;
        cam.offset.y = roundf(sFx.shakeAmp * k * (fmodf(sFx.shakeAge * 60.0f, 2.0f) < 1.0f ? 1.0f : -0.6f));
    }
    BeginMode2D(cam);

    DrawRectangle(BOARD_X - 4, BOARD_Y - 4, BOARD_PIXEL_W + 8, BOARD_PIXEL_H + 8, kRule);
    DrawRectangle(BOARD_X, BOARD_Y, BOARD_PIXEL_W, BOARD_PIXEL_H, kBoardBg);
    for (int x = 1; x < BOARD_W; x++) {
        DrawRectangle(BOARD_X + x * CELL_SIZE, BOARD_Y, 1, BOARD_PIXEL_H, kGrid);
    }
    for (int y = 1; y < BOARD_H; y++) {
        DrawRectangle(BOARD_X, BOARD_Y + y * CELL_SIZE, BOARD_PIXEL_W, 1, kGrid);
    }

    for (int row = BOARD_HIDDEN; row < BOARD_TOTAL_H; row++) {
        for (int col = 0; col < BOARD_W; col++) {
            int8_t v = g->board.cells[row][col];
            if (v != CELL_EMPTY) DrawBlock(col, row, ColorFromU32(Tetromino_Color((PieceType)v)), 1.0f);
        }
    }

    if (g->phase == GS_PLAYING) {
        Color pieceColor = ColorFromU32(Tetromino_Color(g->current));
        Cell cells[CELLS_PER_PIECE];
        Tetromino_GetCells(g->current, g->rotation, cells);

        int ghostY = Game_GhostY(g);
        if (ghostY != g->py) {
            for (int i = 0; i < CELLS_PER_PIECE; i++) DrawGhostBlock(g->px + cells[i].x, ghostY + cells[i].y, pieceColor);
        }
        for (int i = 0; i < CELLS_PER_PIECE; i++) DrawBlock(g->px + cells[i].x, g->py + cells[i].y, pieceColor, 1.0f);
        if (g->curPower != PU_NONE) {
            int idx = g->curPowerCell % CELLS_PER_PIECE;
            int row = g->py + cells[idx].y;
            if (row >= BOARD_HIDDEN) DrawPowerMark(CellX(g->px + cells[idx].x), CellY(row), CELL_SIZE, g->curPower);
        }
    }

    /* Freeze and Slow tint the well while they last. */
    if (g->freezeTimer > 0.0f) {
        DrawRectangle(BOARD_X, BOARD_Y, BOARD_PIXEL_W, BOARD_PIXEL_H, Fade(PowerColor(PU_FREEZE), 0.10f));
        DrawRectangleLinesEx((Rectangle){(float)BOARD_X, (float)BOARD_Y, (float)BOARD_PIXEL_W, (float)BOARD_PIXEL_H}, 3.0f, Fade(PowerColor(PU_FREEZE), 0.7f));
    } else if (g->slowTimer > 0.0f) {
        DrawRectangleLinesEx((Rectangle){(float)BOARD_X, (float)BOARD_Y, (float)BOARD_PIXEL_W, (float)BOARD_PIXEL_H}, 3.0f, Fade(PowerColor(PU_SLOW), 0.6f));
    }

    if (sFx.blastAge < 0.35f) {
        float k = sFx.blastAge / 0.35f;
        int r = (int)((1.0f + k * 0.8f) * (float)CELL_SIZE * 1.5f);
        int cx = CellX(sFx.blastX) + CELL_SIZE / 2, cy = CellY(sFx.blastY) + CELL_SIZE / 2;
        DrawRectangle(cx - r, cy - r, r * 2, r * 2, Fade(WHITE, 0.55f * (1.0f - k)));
        DrawRectangleLinesEx((Rectangle){(float)(cx - r), (float)(cy - r), (float)(r * 2), (float)(r * 2)}, 3.0f, Fade(sFx.powerColor, 1.0f - k));
    }
    if (sFx.laserAge < 0.30f) {
        float k = sFx.laserAge / 0.30f;
        int w = (int)((float)CELL_SIZE * (1.0f - k * 0.7f));
        DrawRectangle(CellX(sFx.laserX) + (CELL_SIZE - w) / 2, BOARD_Y, w, BOARD_PIXEL_H, Fade(sFx.powerColor, 0.85f * (1.0f - k)));
    }

    /* The piece that just landed blinks white once. */
    if (sFx.lockAge < LOCK_FLASH_TIME) {
        unsigned char a = (unsigned char)(200.0f * (1.0f - sFx.lockAge / LOCK_FLASH_TIME));
        for (int i = 0; i < CELLS_PER_PIECE; i++) {
            int row = sFx.lockY + sFx.lockCells[i].y;
            if (row < BOARD_HIDDEN) continue;
            DrawRectangle(CellX(sFx.lockX + sFx.lockCells[i].x) + 1, CellY(row) + 1, CELL_SIZE - 2, CELL_SIZE - 2,
                          (Color){255, 255, 255, a});
        }
    }

    /* Cleared rows: a white bar where each row was, collapsing to a line. */
    if (sFx.age < FLASH_TIME) {
        float k = sFx.age / FLASH_TIME;
        int h = (int)((float)CELL_SIZE * (1.0f - k));
        if (h < 2) h = 2;
        unsigned char a = (unsigned char)(255.0f * (1.0f - k * 0.6f));
        for (int i = 0; i < sFx.count; i++) {
            if (sFx.rows[i] < BOARD_HIDDEN) continue;
            DrawRectangle(BOARD_X, CellY(sFx.rows[i]) + (CELL_SIZE - h) / 2, BOARD_PIXEL_W, h, (Color){255, 255, 255, a});
        }
    }

    Ui_UpdateParticles(dt);

    if (sFx.bannerAge < BANNER_TIME && g->phase == GS_PLAYING) {
        /* Rises a little and holds, then cuts: no fades on pixel type. */
        float k = sFx.bannerAge / BANNER_TIME;
        int rise = (int)(k < 0.25f ? k * 4.0f * 16.0f : 16.0f);
        int cx = BOARD_X + BOARD_PIXEL_W / 2;
        int y = BOARD_Y + 200 - rise;
        int w = Ui_Measure(sFx.banner, UI_T32);
        DrawRectangle(cx - w / 2 - 12, y - 8, w + 24, 48, (Color){13, 10, 30, 215});
        Ui_TextCentered(sFx.banner, UI_T32, cx, y, sFx.bannerColor);
    }

    EndMode2D();
}

#define PREVIEW_CELL 16

/* A small box showing one piece (the next one, or the one on hold). */
static void DrawPreview(bool have, PieceType piece, PowerUp power, int powerCell, const char *label, int x, int y, int w, int h) {
    DrawRectangle(x, y, w, h, kBoardBg);
    DrawRectangleLinesEx((Rectangle){(float)x, (float)y, (float)w, (float)h}, 2.0f, kRule);
    Ui_Text(label, x + 12, y + 10, UI_T8, kTextDim);
    if (!have) {
        Ui_TextCentered("empty", UI_T8, x + w / 2, y + h / 2 + 8, kFaint);
        return;
    }

    Cell cells[CELLS_PER_PIECE];
    Tetromino_GetCells(piece, 0, cells);
    Color color = ColorFromU32(Tetromino_Color(piece));

    int minX = 99, maxX = -99, minY = 99, maxY = -99;
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        if (cells[i].x < minX) minX = cells[i].x;
        if (cells[i].x > maxX) maxX = cells[i].x;
        if (cells[i].y < minY) minY = cells[i].y;
        if (cells[i].y > maxY) maxY = cells[i].y;
    }
    int shapeW = (maxX - minX + 1) * PREVIEW_CELL;
    int shapeH = (maxY - minY + 1) * PREVIEW_CELL;
    int labelH = 22;
    int originX = x + (w - shapeW) / 2;
    int originY = y + labelH + (h - labelH - shapeH) / 2;

    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        int cx = originX + (cells[i].x - minX) * PREVIEW_CELL, cy = originY + (cells[i].y - minY) * PREVIEW_CELL;
        DrawBlockAt(cx, cy, PREVIEW_CELL, color, 1.0f);
        if (power != PU_NONE && i == powerCell % CELLS_PER_PIECE) DrawPowerMark(cx, cy, PREVIEW_CELL, power);
    }
}

static void DrawStat(int x, int y, const char *label, const char *value) {
    Ui_Text(label, x, y, UI_T8, kTextDim);
    Ui_Text(value, x, y + 14, UI_T16, kText);
}

static void FormatClock(float seconds, char *out, int size) {
    if (seconds < 0.0f) seconds = 0.0f;
    int m = (int)(seconds / 60.0f);
    snprintf(out, (size_t)size, "%d:%02d", m, (int)seconds % 60);
}

static void DrawPanel(const Game *g) {
    DrawPreview(true, g->next, g->nextPower, g->nextPowerCell, "Next", PANEL_X, BOARD_Y - 4, PANEL_W, 80);
    DrawPreview((int)g->hold >= 0, g->hold >= 0 ? g->hold : PIECE_I, g->holdPower, g->holdPowerCell,
                g->holdUsed ? "Hold  used" : "Hold", PANEL_X, BOARD_Y + 84, PANEL_W, 80);

    char buf[64];
    int statY = BOARD_Y + 184;
    int statGap = 48;
    snprintf(buf, sizeof(buf), "%ld", g->score);
    DrawStat(PANEL_X, statY, "Score", buf);

    if (g->cfg.mode == MODE_ULTRA) {
        FormatClock(g->timeLeft, buf, sizeof(buf));
        DrawStat(PANEL_X, statY + statGap, "Time left", buf);
    } else if (g->cfg.mode == MODE_SPRINT) {
        FormatClock(g->elapsed, buf, sizeof(buf));
        DrawStat(PANEL_X, statY + statGap, "Time", buf);
    } else {
        snprintf(buf, sizeof(buf), "%d", g->level);
        DrawStat(PANEL_X, statY + statGap, "Level", buf);
    }
    if (g->cfg.mode == MODE_SPRINT) snprintf(buf, sizeof(buf), "%d/%d", g->linesCleared, SPRINT_LINES);
    else snprintf(buf, sizeof(buf), "%d", g->linesCleared);
    DrawStat(PANEL_X, statY + statGap * 2, "Lines", buf);
    if (g->cfg.mode == MODE_ZEN) {
        snprintf(buf, sizeof(buf), "%d", g->zenResets);
        DrawStat(PANEL_X, statY + statGap * 3, "Fresh starts", buf);
    } else {
        snprintf(buf, sizeof(buf), "%ld", g->highScore);
        DrawStat(PANEL_X, statY + statGap * 3, "Best", buf);
    }

    /* Progress to the next level, as pips: ten lines, ten pips. */
    if (g->cfg.mode != MODE_ULTRA && g->cfg.mode != MODE_SPRINT && g->cfg.mode != MODE_ZEN) {
        int into = g->linesCleared % LINES_PER_LEVEL;
        for (int i = 0; i < LINES_PER_LEVEL; i++) {
            int pw = PANEL_W >= 140 ? 12 : 10;
            DrawRectangle(PANEL_X + i * pw, statY + statGap + 36, pw - 4, 4, i < into ? kLight : kRule);
        }
    }

    /* What is running: combo, and any power-up that is still ticking. */
    int statusY = statY + statGap * 4 - 4;
    if (g->combo >= 1) {
        snprintf(buf, sizeof(buf), "Combo x%d", g->combo);
        Ui_Text(buf, PANEL_X, statusY, UI_T8, kLight);
        statusY += 14;
    }
    if (g->freezeTimer > 0.0f) {
        snprintf(buf, sizeof(buf), "Freeze %.0f", g->freezeTimer + 0.5f);
        Ui_Text(buf, PANEL_X, statusY, UI_T8, PowerColor(PU_FREEZE));
        statusY += 14;
    }
    if (g->slowTimer > 0.0f) {
        snprintf(buf, sizeof(buf), "Slow %.0f", g->slowTimer + 0.5f);
        Ui_Text(buf, PANEL_X, statusY, UI_T8, PowerColor(PU_SLOW));
        statusY += 14;
    }

    int ctrlY = BOARD_Y + BOARD_PIXEL_H - 8 * 16 - 4;
    if (ctrlY < statY + statGap * 4 + 34) ctrlY = statY + statGap * 4 + 34;
    if (ctrlY + 8 * 16 < WINDOW_HEIGHT - 30) {
        DrawRectangle(PANEL_X, ctrlY - 12, PANEL_W, 2, kRule);
        const char *lines[][2] = {
            {"Move", "Left Right"}, {"Soft drop", "Down"}, {"Rotate", "Up"}, {"Hard drop", "Space"},
            {"Hold", "C"}, {"Pause", "P"}, {"Restart", "R"}, {"Menu", "Esc"},
        };
        for (int i = 0; i < 8; i++) {
            Ui_Text(lines[i][0], PANEL_X, ctrlY + i * 16, UI_T8, kTextDim);
            Ui_TextRight(lines[i][1], UI_T8, PANEL_X + PANEL_W, ctrlY + i * 16, kFaint);
        }
    }
}

static void DrawGameOverOverlay(const Game *g) {
    DrawRectangle(BOARD_X, BOARD_Y, BOARD_PIXEL_W, BOARD_PIXEL_H, (Color){13, 10, 30, 222});
    int cx = BOARD_X + BOARD_PIXEL_W / 2;
    int cy = BOARD_Y + BOARD_PIXEL_H / 2;
    const char *l1 = "GAME", *l2 = "OVER";
    Color tc = kDanger;
    if (g->won && g->cfg.mode == MODE_SPRINT) { l1 = "SPRINT"; l2 = "CLEAR"; tc = kLight; }
    else if (g->won) { l1 = "TIME"; l2 = "UP"; tc = kGold; }
    int titleSize = BOARD_PIXEL_W >= 230 ? UI_T32 : UI_T16;
    Ui_TextCentered(l1, titleSize, cx, cy - 136, tc);
    Ui_TextCentered(l2, titleSize, cx, cy - 96, tc);

    char buf[64];
    snprintf(buf, sizeof(buf), "%ld", g->score);
    Ui_TextCentered(buf, BOARD_PIXEL_W >= 230 ? UI_T32 : UI_T16, cx, cy - 32, kText);
    if (g->cfg.mode == MODE_SPRINT && g->won) snprintf(buf, sizeof(buf), "%d lines in %.1fs", g->linesCleared, (double)g->elapsed);
    else snprintf(buf, sizeof(buf), "Level %d    %d lines", g->level, g->linesCleared);
    Ui_TextCentered(buf, UI_T8, cx, cy + 12, kTextDim);

    if (g->score >= g->highScore && g->score > 0 && g->cfg.mode != MODE_ZEN) {
        Ui_TextCentered("A new best", UI_T16, cx, cy + 44, kLight);
    } else if (g->cfg.mode != MODE_ZEN) {
        snprintf(buf, sizeof(buf), "Best %ld", g->highScore);
        Ui_TextCentered(buf, UI_T16, cx, cy + 44, kTextDim);
    }

    Ui_TextCentered("R plays again", UI_T8, cx, cy + 92, kTextDim);
    Ui_TextCentered("Esc for the menu", UI_T8, cx, cy + 112, kTextDim);
}

/* --- Menu background: a fixed pool of slowly falling tetrominoes. Cheap by
 * construction: no allocation, no per-frame texture work, just arithmetic
 * over a small fixed array. Only ever drawn on the menu screen. They fall
 * on a 16px grid and never rotate mid-air, so they stay pixel art. */
#define BG_PIECE_COUNT 12

typedef struct {
    float y;
    int x;
    float speed;
    int cell;
    PieceType type;
    int rotation;
} BgPiece;

static BgPiece sBgPieces[BG_PIECE_COUNT];
static bool sBgInitialized = false;

static void RespawnBgPiece(BgPiece *b, bool firstFill) {
    b->cell = GetRandomValue(0, 1) ? 16 : 24;
    b->x = (GetRandomValue(0, WINDOW_WIDTH) / 8) * 8;
    /* On first fill, scatter through the whole height so the first frame
     * isn't a synchronized drop; afterward, respawn just above the top. */
    b->y = firstFill ? (float)GetRandomValue(-WINDOW_HEIGHT, WINDOW_HEIGHT) : (float)(-4 * b->cell);
    b->speed = (float)GetRandomValue(26, 64);
    b->type = (PieceType)GetRandomValue(0, PIECE_COUNT - 1);
    b->rotation = GetRandomValue(0, ROTATIONS_PER_PIECE - 1);
}

void Render_MenuBackdrop(float dt) {
    if (!sBgInitialized) {
        for (int i = 0; i < BG_PIECE_COUNT; i++) RespawnBgPiece(&sBgPieces[i], true);
        sBgInitialized = true;
    }
    for (int i = 0; i < BG_PIECE_COUNT; i++) {
        BgPiece *b = &sBgPieces[i];
        b->y += b->speed * dt;
        if (b->y > WINDOW_HEIGHT) RespawnBgPiece(b, false);

        Cell cells[CELLS_PER_PIECE];
        Tetromino_GetCells(b->type, b->rotation, cells);
        Color c = ColorFromU32(Tetromino_Color(b->type));
        for (int k = 0; k < CELLS_PER_PIECE; k++) {
            DrawBlockAt(b->x + cells[k].x * b->cell, (int)b->y + cells[k].y * b->cell, b->cell, c, 0.16f);
        }
    }
}

void Render_Frame(const Game *g, const Settings *settings, bool showFps) {
    (void)settings;
    float dt = GetFrameTime();
    Layout(g);
    ReactToEvents(g);

    Win_BeginFrame();
    ClearBackground(kBg);

    Ui_Text("BLOCKFALL", BOARD_X, 16, UI_T16, kLight);
    {
        char label[48];
        if (g->cfg.width == 10 && g->cfg.height == 20) snprintf(label, sizeof(label), "%s", Game_ModeName(g->cfg.mode));
        else snprintf(label, sizeof(label), "%s  %dx%d", Game_ModeName(g->cfg.mode), g->cfg.width, g->cfg.height);
        Ui_Text(label, BOARD_X + Ui_Measure("BLOCKFALL", UI_T16) + 20, 20, UI_T8, kTextDim);
    }
    DrawBoard(g, dt);
    DrawPanel(g);

    if (g->phase == GS_PAUSED) Ui_PauseOverlay(BOARD_X, BOARD_Y, BOARD_PIXEL_W, BOARD_PIXEL_H);
    if (g->phase == GS_GAMEOVER) DrawGameOverOverlay(g);

    Ui_TextCentered("M mutes    - and = change the volume", UI_T8, WINDOW_WIDTH / 2, WINDOW_HEIGHT - 20, kFaint);
    if (showFps) DrawFPS(10, WINDOW_HEIGHT - 24);

    Win_EndFrame();
}
