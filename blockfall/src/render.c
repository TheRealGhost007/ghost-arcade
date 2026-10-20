/* The play screen's frame. Where things go is layout.c's (raylib-free,
 * tested on every board), what is lively is hudfx.c's and how play moves is
 * feel.c's (the same), and the drawing is split by place: the well
 * (well.c), what happens in it (effects.c) and the panel or strip
 * (panel.c). This file draws the header and puts those together, then
 * whatever the play screen lays over them (the pause menu, the results
 * card: scr_play.c), the volume chip, the wipe and F3's counter. It also
 * owns the bevelled block every screen draws. */
#include "winscale.h"
#include "renderpriv.h"
#include "blockdraw.h"
#include "comfort.h"
#include "look.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static PlayLayout sLayout;
static HudFx sHud;
static Feel sFeel;
static unsigned sRun = 0;
static bool sHaveRun = false;

/* ---------------------------------------------------------------- blocks */

Color Rp_ColorFromU32(uint32_t c) {
    return (Color){(unsigned char)((c >> 24) & 0xFF), (unsigned char)((c >> 16) & 0xFF), (unsigned char)((c >> 8) & 0xFF), (unsigned char)(c & 0xFF)};
}

Color Render_PieceColor(PieceType t) { return Rp_ColorFromU32(Tetromino_Color(t)); }

/* Filled block with a light/dark bevel so blocks read as solid tiles rather
 * than flat rectangles, entirely procedural (no image assets). */
void Render_BlockAt(int x, int y, int size, Color base, float alpha) {
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

void Render_Pattern(int x, int y, int size, PieceType t, Color base, float alpha) {
    if (!Look_Patterns() || size < 12 || (int)t < 0 || t >= PIECE_COUNT) return;
    int bevel = size >= 24 ? 4 : 2;
    int px = (size - 2 * bevel - 2) / LOOK_PATTERN_N;
    if (px < 1) px = 1;
    int span = px * LOOK_PATTERN_N, ox = x + (size - span) / 2, oy = y + (size - span) / 2;
    int luma = (299 * base.r + 587 * base.g + 114 * base.b) / 1000;
    Color mark = luma > 140 ? Ui_Lerp(base, BLACK, 0.62f) : Ui_Lerp(base, WHITE, 0.62f);
    mark.a = (unsigned char)(255.0f * alpha);
    for (int r = 0; r < LOOK_PATTERN_N; r++)
        for (int c = 0; c < LOOK_PATTERN_N; c++)
            if (Look_PatternCell(t, c, r)) DrawRectangle(ox + c * px, oy + r * px, px, px, mark);
}

void Render_PieceBlock(int x, int y, int size, PieceType t, Color base, float alpha) {
    Render_BlockAt(x, y, size, base, alpha);
    Render_Pattern(x, y, size, t, base, alpha);
}

Color Rp_PowerColor(PowerUp p) {
    switch (p) {
        case PU_BOMB: return (Color){255, 96, 84, 255};
        case PU_LASER: return (Color){90, 230, 255, 255};
        case PU_FREEZE: return (Color){176, 222, 255, 255};
        case PU_SLOW: return (Color){255, 192, 84, 255};
        case PU_SWEEP: return (Color){120, 240, 150, 255};
        default: return WHITE;
    }
}

const char *Rp_PowerGlyph(PowerUp p) {
    switch (p) {
        case PU_BOMB: return "B";
        case PU_LASER: return "L";
        case PU_FREEZE: return "F";
        case PU_SLOW: return "S";
        case PU_SWEEP: return "W";
        default: return "";
    }
}

/* Power-up blocks: a light core and a coloured edge, pulsing, so you can
 * see at a glance which block of the piece carries what; its letter on any
 * block of 14 px and up (every well, and the 16 px previews). */
void Rp_PowerMark(int x, int y, int size, PowerUp p, bool grey) {
    if (p == PU_NONE) return;
    float pulse = (Comfort_Calm() || grey) ? 0.5f : 0.5f + 0.5f * sinf((float)GetTime() * 8.0f); /* steady with Reduced flashing */
    Color c = Rp_PowerColor(p), core = WHITE;
    if (grey) {
        c = Ui_Lerp(c, kRule, 0.6f);
        core = kTextDim;
    }
    DrawRectangle(x + 3, y + 3, size - 6, size - 6, Fade(core, 0.55f + 0.3f * pulse));
    DrawRectangleLinesEx((Rectangle){(float)x + 1, (float)y + 1, (float)size - 2, (float)size - 2}, 2.0f, Fade(c, 0.65f + 0.35f * pulse));
    if (size >= 14) Ui_TextCentered(Rp_PowerGlyph(p), UI_T8, x + size / 2, y + size / 2 - 4, Ui_Lerp(c, BLACK, 0.45f));
    else DrawRectangle(x + size / 2 - 2, y + size / 2 - 2, 4, 4, Ui_Lerp(c, BLACK, 0.35f));
}

Color Render_PowerColor(PowerUp p) { return Rp_PowerColor(p); }
void Render_PowerMark(int x, int y, int size, PowerUp p) { Rp_PowerMark(x, y, size, p, false); }

int Rp_CellX(const PlayLayout *l, int col) { return l->boardX + col * l->cell; }
int Rp_CellY(const PlayLayout *l, int row) { return l->boardY + (row - BOARD_HIDDEN) * l->cell; }

/* Text the layout fitted: the size it picked, on one line or two. */
void Rp_DrawFitted(const char *text, TextFit f, int cx, int y, Color c) {
    if (!f.split) { Ui_TextCentered(text, f.size, cx, y, c); return; }
    char first[64];
    int n = f.split - 1 < (int)sizeof(first) - 1 ? f.split - 1 : (int)sizeof(first) - 1;
    memcpy(first, text, (size_t)n);
    first[n] = '\0';
    Ui_TextCentered(first, f.size, cx, y, c);
    Ui_TextCentered(text + f.split, f.size, cx, y + f.size + 8, c);
}

/* M and - / = (named on the controls card) show what they did for a moment
 * in the footer band under everything. */
static void DrawVolumeChip(const RenderView *v) {
    LRect r = sLayout.chip;
    DrawRectangle(r.x, r.y, r.w, r.h, kPanel);
    DrawRectangle(r.x, r.y, 4, r.h, kLight);
    int ty = r.y + (r.h - 8) / 2;
    if (!v->soundOn) {
        Ui_TextCentered("Sound off", UI_T8, r.x + r.w / 2, ty, kText);
        return;
    }
    Ui_Text("Effects", r.x + 16, ty, UI_T8, kText);
    int blocks = (v->volume + 5) / 10, bx = r.x + r.w - 16 - 10 * 12;
    for (int i = 0; i < 10; i++) DrawRectangle(bx + i * 12, r.y + (r.h - 12) / 2, 8, 12, i < blocks ? kLight : kRule);
}

/* ------------------------------------------------------------ the frame */

void Render_Frame(const Game *g, const RenderView *v) {
    float dt = GetFrameTime();
    HudSpec spec = Layout_Spec(g->cfg.mode, g->cfg.modernScoring, v->showControls);
    sLayout = Layout_Play(g->board.w, g->board.h, &spec);
    FeelFrame feel = {Comfort_Motion(), Comfort_PieceGlide(), v->moveRepeat};
    if (!sHaveRun || v->run != sRun) {
        /* A new run or a restart: nothing left over from the last one. */
        HudFx_Reset(&sHud, g);
        Feel_Reset(&sFeel, g, feel.glide);
        Effects_Reset();
        sRun = v->run;
        sHaveRun = true;
    }
    Feel_React(&sFeel, g, &feel);
    HudFx_React(&sHud, g, v->justPassedBest);
    Effects_React(g, &sLayout, &sFeel);
    Effects_EndShow(g, &sLayout, v->end);
    HudFx_Tick(&sHud, dt, g->phase == GS_PLAYING);
    Feel_Tick(&sFeel, dt);
    Effects_Tick(dt, &sLayout, &sFeel);

    Win_BeginFrame();
    ClearBackground(kBg);

    Ui_Text("BLOCKFALL", LAYOUT_MARGIN, LAYOUT_HEADER_Y, UI_T16, kLight);
    {
        char label[48];
        if (g->cfg.width == 10 && g->cfg.height == 20) snprintf(label, sizeof(label), "%s", Game_ModeName(g->cfg.mode));
        else snprintf(label, sizeof(label), "%s  %dx%d", Game_ModeName(g->cfg.mode), g->cfg.width, g->cfg.height);
        Ui_Text(label, LAYOUT_MARGIN + Ui_Measure("BLOCKFALL", UI_T16) + 20, LAYOUT_HEADER_Y + 4, UI_T8, kTextDim);
    }

    /* The well and everything in it shake together; effects stay inside it
     * (the clip follows the shake). */
    Camera2D cam = {0};
    cam.zoom = 1.0f;
    cam.offset.y = (float)Effects_ShakeY();
    BeginMode2D(cam);
    Well_DrawFrame(g, &sLayout, &sHud, &sFeel, v->ghost, v->grid);
    BeginScissorMode(sLayout.boardX + (int)cam.offset.x, sLayout.boardY + (int)cam.offset.y, sLayout.boardW, sLayout.boardH);
    Well_DrawInside(g, &sLayout, &sFeel, v->ghost);
    Well_DrawEnd(g, &sLayout, v->end);
    Effects_DrawInWell(g, &sLayout, dt);
    if (g->phase == GS_PLAYING) {
        Effects_DrawCallouts(&sLayout, &sHud);
        Effects_DrawCue(&sLayout, &v->cue, &sHud);
    }
    EndScissorMode();
    EndMode2D();

    Panel_Draw(g, v, &sLayout, &sHud, &sFeel);
    if (g->phase == GS_PLAYING) Panel_DrawFlight(g, &sLayout, &sFeel);

    if (v->overlay) v->overlay(v->overlayCtx); /* the pause menu or the results card */

    if (v->chipTime > 0.0f) DrawVolumeChip(v);
    if (v->wipe && Wipe_Active(v->wipe)) Wipe_Draw(v->wipe);
    if (v->showFps) DrawFPS(10, LAYOUT_WINDOW_H - 24);

    Win_EndFrame();
}

void Render_Inspect(RenderInspect *out) {
    memset(out, 0, sizeof(*out));
    out->layout = sLayout;
    out->shownScore = sHud.shown;
    const Callout *cs[CALLOUT_MAX];
    out->callouts = HudFx_Callouts(&sHud, cs);
    for (int i = 0; i < out->callouts; i++) snprintf(out->callout[i], sizeof(out->callout[i]), "%s", cs[i]->text);
    out->popups = sHud.pops;
    if (sHud.pops > 0) snprintf(out->popup, sizeof(out->popup), "%s", sHud.pop[sHud.pops - 1].text);
    out->clearStage = Feel_ClearStage(&sFeel);
    out->clearRows = sFeel.clear.on ? sFeel.clear.count : 0;
    out->clearTotal = sFeel.clear.on ? Feel_ClearTotal(sFeel.clear.times) : 0.0f;
    float dx, dy;
    Feel_PieceOffset(&sFeel, &dx, &dy);
    out->pieceDX = (int)floorf(dx * (float)sLayout.cell + 0.5f);
    out->pieceDY = (int)floorf(dy * (float)sLayout.cell + 0.5f);
    out->pop = Feel_PopInset(&sFeel);
    out->fill = Feel_SpawnFill(&sFeel);
    out->ghost = Feel_ShowGhost(&sFeel);
    out->flight = Feel_HoldFlight(&sFeel, NULL);
    out->slide = Feel_NextSlide(&sFeel, NULL);
    out->holdReady = Feel_HoldReady(&sFeel) > 0.0f;
    out->squash = Feel_Squash(&sFeel);
    out->settle = Feel_Settle(&sFeel);
    out->smear = Feel_Smear(&sFeel) > 0.0f;
    out->impact = Feel_Impact(&sFeel) > 0.0f;
    out->bevel = Feel_Bevel(&sFeel) > 0.0f;
    out->streak = Feel_Streak(&sFeel) > 0.0f;
    out->quadFrame = Feel_QuadFrame(&sFeel) > 0.0f;
    out->bits = Effects_Bits();
    int lift = 0;
    /* The board's own rows only: the row map past them is never filled. */
    for (int r = 0; r < Board_TotalH(&sFeel.clear.before); r++) {
        int v = Feel_RowLift(&sFeel, r, sLayout.cell);
        if (v > lift || (lift == 0 && v < 0)) lift = v;
    }
    out->lift = lift;
}
