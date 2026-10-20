/* Render-only reactions to the one-frame events the rules raise: bomb and
 * laser blasts, shake, the pixels a clear bursts into and a hard drop's
 * dust, the callouts and popups hudfx.c decides, READY and GO and the
 * countdown after a pause, and a Sprint clear's rows bursting upward. The
 * cleared rows, the landed piece and the falling piece are well.c's (from
 * feel.h). Nothing in here feeds back into the game. Every effect honours
 * Screen shake, Reduced flashing (no white, nothing pulses) and Motion
 * (comfort.h), and stays inside the well. */
#include "renderpriv.h"
#include "blockdraw.h"
#include "comfort.h"
#include "feel.h"
#include "prefs.h"
#include <math.h>
#include <string.h>

static struct {
    float shakeAge, shakeAmp;
    int blastX, blastY;   /* a bomb going off */
    float blastAge;
    int laserX;           /* a laser firing */
    float laserAge;
    Color powerColor;
} sFx = {.shakeAge = 9.0f, .blastAge = 9.0f, .laserAge = 9.0f};

#define SHAKE_TIME 0.16f
#define TRIPLE_SHAKE 3.0f
#define QUAD_SHAKE 8.0f

/* The well's own pixels: a clear's burst and a hard drop's dust. A pool of
 * its own (the shared one holds 256, and a quad on a 24-wide well bursts
 * into up to 288); a pixel that leaves the well is gone. */
#define BITS 640
#define BIT_GRAVITY 640.0f
typedef struct {
    float x, y, vx, vy, life, maxLife;
    int size;
    Color c;
} Bit;
static Bit sBits[BITS];
static int sBitCount = 0; /* in use, for the harness */
static int sEndGone = 0;  /* a Sprint clear's rows burst so far */

static float Rand(int lo, int hi) { return (float)GetRandomValue(lo, hi); }

static void Emit(float x, float y, float vx, float vy, float life, int size, Color c) {
    for (int i = 0; i < BITS; i++) {
        Bit *b = &sBits[i];
        if (b->life > 0.0f) continue;
        *b = (Bit){x, y, vx, vy, life, life, size, c};
        return;
    }
}

void Effects_Reset(void) {
    memset(&sFx, 0, sizeof(sFx));
    sFx.shakeAge = sFx.blastAge = sFx.laserAge = 9.0f;
    memset(sBits, 0, sizeof(sBits));
    sBitCount = 0;
    sEndGone = 0;
}

int Effects_Bits(void) { return sBitCount; }

/* Particles from the shared pool (power-ups): half as many with Reduced motion. */
static void Burst(float cx, float cy, Color color, int count, float speed) {
    Ui_Burst(cx, cy, color, Comfort_Particles(count), speed);
}

static void Shake(float amp) {
    if (amp > sFx.shakeAmp || sFx.shakeAge >= SHAKE_TIME) {
        sFx.shakeAmp = amp;
        sFx.shakeAge = 0.0f;
    }
}

/* A hard drop throws dust sideways off the piece's underside. */
static void Dust(const PlayLayout *l, const Feel *f) {
    const LandFeel *land = &f->land;
    if (!land->on) return; /* a Zen fresh start swept it away */
    Cell cells[CELLS_PER_PIECE];
    Tetromino_GetCells(land->type, land->rot, cells);
    Color c = Ui_Lerp(Render_PieceColor(land->type), kText, 0.5f);
    int landY = land->replayed ? land->y : land->liveY;
    int size = l->cell >= 20 ? 4 : 2;
    for (int i = 0; i < CELLS_PER_PIECE; i++) {
        bool under = true;
        for (int j = 0; j < CELLS_PER_PIECE; j++)
            if (cells[j].x == cells[i].x && cells[j].y == cells[i].y + 1) under = false;
        int row = landY + cells[i].y;
        if (!under || row < BOARD_HIDDEN) continue;
        float x = (float)(Rp_CellX(l, land->x + cells[i].x) + l->cell / 2), y = (float)(Rp_CellY(l, row) + l->cell - 2);
        int n = Comfort_Particles(2);
        for (int k = 0; k < n; k++) {
            float dir = (k & 1) ? 1.0f : -1.0f;
            Emit(x + dir * (float)(l->cell / 3), y, dir * Rand(70, 170), -Rand(20, 90), Rand(22, 36) / 100.0f, size, c);
        }
    }
}

void Effects_React(const Game *g, const PlayLayout *l, const Feel *f) {
    if (!g->justLocked) return;
    int cell = l->cell;
    bool calm = Comfort_Calm();

    if (g->lastPower != PU_NONE) {
        sFx.powerColor = Rp_PowerColor(g->lastPower);
        float cx = (float)(Rp_CellX(l, g->lastPowerX) + cell / 2), cy = (float)(Rp_CellY(l, g->lastPowerY) + cell / 2);
        if (g->lastPower == PU_BOMB) {
            sFx.blastX = g->lastPowerX; sFx.blastY = g->lastPowerY; sFx.blastAge = 0.0f;
            Burst(cx, cy, sFx.powerColor, 26, 260.0f);
            Burst(cx, cy, calm ? sFx.powerColor : WHITE, 12, 180.0f);
            sFx.shakeAge = 0.0f; sFx.shakeAmp = 9.0f;
        } else if (g->lastPower == PU_LASER) {
            sFx.laserX = g->lastPowerX; sFx.laserAge = 0.0f;
            for (int r = 0; r < g->board.h; r += 2) Burst(cx, (float)(l->boardY + r * cell), sFx.powerColor, 2, 120.0f);
        } else {
            Burst(cx, cy, sFx.powerColor, 16, 170.0f);
        }
    }

    /* Each clear size its own weight: a triple shakes a little, a quad 8 px. */
    if (g->lastClearCount >= 4) Shake(QUAD_SHAKE);
    else if (g->lastClearCount == 3) Shake(TRIPLE_SHAKE);

    if (g->justHardDropped && g->lastDropCells > 0) {
        /* A thud scaled by how far it fell, and dust off its underside. */
        float amp = 2.0f + (float)g->lastDropCells * 0.35f;
        Shake(amp > 7.0f ? 7.0f : amp);
        Dust(l, f);
    }
}

/* The cleared cells that zipped out this frame burst into 1 to 3 pixels of
 * their own colour, away from where the piece landed (a quad's all at once,
 * with gold among them). */
static void ClearBursts(const PlayLayout *l, const Feel *f) {
    const ClearReplay *r = &f->clear;
    if (!r->on || r->count <= 0 || r->t == r->prevT) return;
    int size = l->cell >= 20 ? 4 : 2;
    for (int i = 0; i < r->count && i < 4; i++) {
        int row = r->rows[i];
        if (row < BOARD_HIDDEN) continue;
        for (int col = 0; col < r->before.w; col++) {
            int8_t v = r->before.cells[row][col];
            if (v == CELL_EMPTY || !Feel_ClearCellGone(r, col, r->t) || Feel_ClearCellGone(r, col, r->prevT)) continue;
            Color c = Render_PieceColor((PieceType)v);
            int n = Comfort_Particles(r->quad ? 3 : (r->count >= 3 ? GetRandomValue(2, 3) : GetRandomValue(1, 2)));
            float x = (float)(Rp_CellX(l, col) + l->cell / 2), y = (float)(Rp_CellY(l, row) + l->cell / 2);
            float dir = col > r->lockCol ? 1.0f : (col < r->lockCol ? -1.0f : (GetRandomValue(0, 1) ? 1.0f : -1.0f));
            for (int k = 0; k < n; k++) {
                Color bc = (r->quad && (k & 1)) ? kGold : c;
                Emit(x + Rand(-l->cell / 3, l->cell / 3), y + Rand(-l->cell / 3, l->cell / 3), dir * Rand(60, 210), -Rand(30, 150),
                     Rand(30, 50) / 100.0f, size, bc);
            }
        }
    }
}

void Effects_EndShow(const Game *g, const PlayLayout *l, const Results *end) {
    if (!end || !end->on || end->card.kind != END_SPRINT_CLEAR) {
        sEndGone = 0;
        return;
    }
    int gone = Results_RowsTurned(end, g->board.h);
    int size = l->cell >= 20 ? 4 : 2;
    for (int i = sEndGone; i < gone; i++) {
        int row = BOARD_HIDDEN + g->board.h - 1 - i;
        for (int col = 0; col < g->board.w; col++) {
            int8_t v = g->board.cells[row][col];
            if (v == CELL_EMPTY) continue;
            Color c = Render_PieceColor((PieceType)v);
            float x = (float)(Rp_CellX(l, col) + l->cell / 2), y = (float)(Rp_CellY(l, row) + l->cell / 2);
            int n = Comfort_Particles(2);
            for (int k = 0; k < n; k++)
                Emit(x + Rand(-l->cell / 3, l->cell / 3), y, Rand(-70, 70), -Rand(240, 460), Rand(45, 75) / 100.0f, size, c);
        }
    }
    sEndGone = gone;
}

void Effects_Tick(float dt, const PlayLayout *l, const Feel *f) {
    sFx.shakeAge += dt;
    sFx.blastAge += dt;
    sFx.laserAge += dt;
    ClearBursts(l, f);
}

int Effects_ShakeY(void) {
    if (!Prefs_Get()->screenShake || sFx.shakeAge >= SHAKE_TIME) return 0;
    /* Vertical only: the thing that happened was a drop. */
    float k = 1.0f - sFx.shakeAge / SHAKE_TIME;
    return (int)roundf(sFx.shakeAmp * k * (fmodf(sFx.shakeAge * 60.0f, 2.0f) < 1.0f ? 1.0f : -0.6f));
}

/* The well's pixels move on and are drawn; one that leaves the well is gone. */
static void Bits(const PlayLayout *l, float dt) {
    float left = (float)l->boardX, right = (float)(l->boardX + l->boardW), bottom = (float)(l->boardY + l->boardH);
    sBitCount = 0;
    for (int i = 0; i < BITS; i++) {
        Bit *b = &sBits[i];
        if (b->life <= 0.0f) continue;
        b->life -= dt;
        b->x += b->vx * dt;
        b->y += b->vy * dt;
        b->vy += BIT_GRAVITY * dt;
        b->vx *= 1.0f - 2.0f * dt;
        if (b->life <= 0.0f || b->x < left || b->x >= right || b->y >= bottom) {
            b->life = 0.0f;
            continue;
        }
        sBitCount++;
        int s = b->life < b->maxLife * 0.4f && b->size > 2 ? b->size - 2 : b->size;
        /* Snapped to a 2 px grid so they stay in the same pixel world. */
        DrawRectangle(((int)b->x / 2) * 2 - s / 2, ((int)b->y / 2) * 2 - s / 2, s, s, b->c);
    }
}

void Effects_DrawInWell(const Game *g, const PlayLayout *l, float dt) {
    int bx = l->boardX, by = l->boardY, bw = l->boardW, bh = l->boardH, cell = l->cell;
    bool calm = Comfort_Calm();

    /* Freeze and Slow tint the well while they last. */
    if (g->freezeTimer > 0.0f) {
        DrawRectangle(bx, by, bw, bh, Fade(Rp_PowerColor(PU_FREEZE), 0.10f));
        DrawRectangleLinesEx((Rectangle){(float)bx, (float)by, (float)bw, (float)bh}, 3.0f, Fade(Rp_PowerColor(PU_FREEZE), 0.7f));
    } else if (g->slowTimer > 0.0f) {
        DrawRectangleLinesEx((Rectangle){(float)bx, (float)by, (float)bw, (float)bh}, 3.0f, Fade(Rp_PowerColor(PU_SLOW), 0.6f));
    }

    if (sFx.blastAge < 0.35f) {
        float k = sFx.blastAge / 0.35f;
        int r = (int)((1.0f + k * 0.8f) * (float)cell * 1.5f);
        int cx = Rp_CellX(l, sFx.blastX) + cell / 2, cy = Rp_CellY(l, sFx.blastY) + cell / 2;
        if (!calm) DrawRectangle(cx - r, cy - r, r * 2, r * 2, Fade(WHITE, 0.55f * (1.0f - k))); /* no white flash with Reduced flashing */
        DrawRectangleLinesEx((Rectangle){(float)(cx - r), (float)(cy - r), (float)(r * 2), (float)(r * 2)}, 3.0f, Fade(sFx.powerColor, 1.0f - k));
    }
    if (sFx.laserAge < 0.30f) {
        float k = sFx.laserAge / 0.30f;
        int w = (int)((float)cell * (1.0f - k * 0.7f));
        DrawRectangle(Rp_CellX(l, sFx.laserX) + (cell - w) / 2, by, w, bh, Fade(sFx.powerColor, (calm ? 0.30f : 0.85f) * (1.0f - k)));
    }

    Bits(l, dt);
    Ui_UpdateParticles(dt);
}

static Color TintOf(const Callout *c) {
    switch (c->tint) {
        case TINT_GOLD: return kGold;
        case TINT_POWER: return Rp_PowerColor(c->power);
        default: return kLight;
    }
}

void Effects_DrawCallouts(const PlayLayout *l, const HudFx *hud) {
    bool motion = Comfort_Motion();
    /* Popups first, from the cleared rows: the callouts sit over them. */
    for (int i = 0; i < hud->pops; i++) {
        const Popup *p = &hud->pop[i];
        int cx = l->boardX + (int)(p->col * (float)l->cell), cy = l->boardY + (int)(p->row * (float)l->cell);
        PopupPlace pp = Layout_Popup(l, p->text, cx, cy, HudFx_PopupRise(p, motion), Ui_Measure);
        DrawRectangle(pp.x - 4, pp.y - 4, pp.w + 8, pp.size + 8, kPlate);
        Ui_Text(p->text, pp.x, pp.y, pp.size, p->gold ? kGold : kLight);
    }

    const Callout *cs[CALLOUT_MAX];
    int n = HudFx_Callouts(hud, cs);
    if (n <= 0) return;
    CalloutText lines[CALLOUT_MAX];
    CalloutPlace at[CALLOUT_MAX];
    for (int i = 0; i < n; i++)
        lines[i] = (CalloutText){cs[i]->text, cs[i]->shortText, HudFx_CalloutRise(cs[i], motion), HudFx_CalloutMaxSize(hud, cs[i])};
    /* Rises a little and holds, then cuts: no fades on pixel type. */
    Layout_Callouts(l, lines, n, Ui_Measure, at);
    for (int i = 0; i < n; i++) {
        DrawRectangle(at[i].plateX, at[i].plateY, at[i].plateW, at[i].plateH, kPlate);
        Rp_DrawFitted(at[i].useShort ? cs[i]->shortText : cs[i]->text, at[i].fit, l->boardX + l->boardW / 2, at[i].textY, TintOf(cs[i]));
    }
}

/* READY (violet) drops 8 px in and holds while the run waits; GO (gold)
 * shows as play begins. After a pause (with the countdown on) 3, 2 and 1
 * in violet, each dropping in the same way. Placed like a callout, so it
 * fits every well; a callout that arrives during GO takes its place. */
#define CUE_DROP_SECONDS 0.12f
void Effects_DrawCue(const PlayLayout *l, const StartCue *cue, const HudFx *hud) {
    if (cue->stage == CUE_NONE || (cue->stage == CUE_GO && hud->calls > 0)) return;
    bool ready = cue->stage == CUE_READY, count = cue->stage == CUE_COUNT;
    float into = count ? fmodf(cue->t, CUE_COUNT_STEP) : cue->t; /* each number drops in afresh */
    int rise = 0;
    if ((ready || count) && Comfort_Motion() && into < CUE_DROP_SECONDS) rise = CALLOUT_RISE - (int)(into / CUE_DROP_SECONDS * (float)CALLOUT_RISE);
    static const char *const kCount[4] = {"", "1", "2", "3"};
    CalloutText line = {count ? kCount[PlayDriver_CountNumber(cue)] : (ready ? "READY" : "GO"), NULL, rise, 32};
    CalloutPlace at;
    Layout_Callouts(l, &line, 1, Ui_Measure, &at);
    DrawRectangle(at.plateX, at.plateY, at.plateW, at.plateH, kPlate);
    Rp_DrawFitted(line.text, at.fit, l->boardX + l->boardW / 2, at.textY, (ready || count) ? kLight : kGold);
}
