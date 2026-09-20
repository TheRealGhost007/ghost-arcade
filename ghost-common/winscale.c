#include "winscale.h"
#include <math.h>
#include <stddef.h>
#include "raylib.h"
#include "prefs.h"

static RenderTexture2D sRT;
static bool sReady = false;
static int sW, sH;
static float sScale = 1.0f, sOffX = 0.0f, sOffY = 0.0f;
static bool sSmooth = false;
static void (*sOverlay)(void) = NULL;
static bool sApplied = false; /* the fullscreen state we last asked the window for */

void Win_Init(int logicalW, int logicalH) {
    sW = logicalW;
    sH = logicalH;
    sRT = LoadRenderTexture(sW, sH);
    sReady = sRT.id != 0;
    if (sReady) SetTextureFilter(sRT.texture, TEXTURE_FILTER_POINT);
    sApplied = Prefs_Get()->fullscreen;
    if (sApplied && !IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)) ToggleBorderlessWindowed();
}

void Win_Shutdown(void) {
    if (sReady) UnloadRenderTexture(sRT);
    sReady = false;
}

int Win_LogicalWidth(void) { return sW; }

bool Win_IsFullscreen(void) { return IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE); }

void Win_SetOverlay(void (*draw)(void)) { sOverlay = draw; }

void Win_Update(void) {
    ArcadePrefs *p = Prefs_Get();
    bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    if (IsKeyPressed(KEY_F11) || (alt && IsKeyPressed(KEY_ENTER))) {
        p->fullscreen = !p->fullscreen;
        Prefs_Save();
    }
    /* The settings screens flip the same preference, so one place applies it. */
    /* Ask once per change: if the compositor ignores the request we must not
     * keep toggling every frame. */
    if (p->fullscreen != sApplied) {
        sApplied = p->fullscreen;
        if (p->fullscreen != Win_IsFullscreen()) ToggleBorderlessWindowed();
    }

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    if (sw < 1 || sh < 1 || !sReady) return;
    float s = fminf((float)sw / (float)sW, (float)sh / (float)sH);
    float whole = floorf(s);
    bool smooth = true;
    if (whole >= 1.0f && whole / s >= 0.8f) { s = whole; smooth = false; } /* crisp pixels when little is wasted */
    sScale = s;
    sOffX = ((float)sw - (float)sW * s) * 0.5f;
    sOffY = ((float)sh - (float)sH * s) * 0.5f;
    if (smooth != sSmooth) {
        sSmooth = smooth;
        SetTextureFilter(sRT.texture, smooth ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
    }
    SetMouseOffset((int)-sOffX, (int)-sOffY);
    SetMouseScale(1.0f / s, 1.0f / s);
}

void Win_BeginFrame(void) {
    BeginDrawing();
    if (sReady) BeginTextureMode(sRT);
}

void Win_EndFrame(void) {
    if (sReady) {
        if (sOverlay) sOverlay();
        EndTextureMode();
        ClearBackground(BLACK);
        Rectangle src = {0.0f, 0.0f, (float)sW, -(float)sH};
        Rectangle dst = {roundf(sOffX), roundf(sOffY), (float)sW * sScale, (float)sH * sScale};
        DrawTexturePro(sRT.texture, src, dst, (Vector2){0, 0}, 0.0f, WHITE);
    }
    EndDrawing();
}
