#include "render.h"
#include <time.h>
#include "winscale.h"
#include "prefs.h"
#include "achievements.h"
#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "palette.h"
#include "pixtext.h"
#include "kitdraw.h"
#include "settings.h"
#include "jukebox.h"
#include "rlgl.h"

/* --- Look: a dim arcade hall. Indigo wall, patterned carpet, and a row of
 * cabinets whose only real light is their own marquee. Each game's light
 * colour is pulled from its icon, so the row gets more colourful as the
 * catalog grows without anyone picking colours by hand. */

/* Press Start 2P is drawn on an 8x8 grid, so it only renders with even
 * pixels at multiples of 8. Every piece of text in the launcher uses one of
 * these three sizes -- nothing in between. */
#define TYPE_S 8
#define TYPE_M 16
#define TYPE_L 24

static void PText(const char *text, int x, int y, int fontSize, Color color) { Pix_Text(text, x, y, fontSize, color); }
static int PMeasure(const char *text, int fontSize) { return Pix_Measure(text, fontSize); }
static void PTextCentered(const char *text, int fontSize, int centerX, int y, Color color) { Pix_TextCentered(text, fontSize, centerX, y, color); }
static void PTextRight(const char *text, int fontSize, int rightX, int y, Color color) { Pix_TextRight(text, fontSize, rightX, y, color); }

static Color Mix(Color a, Color b, float t) { return Pal_Mix(a, b, t); }

/* --- Icon cache: loaded lazily by path, kept for the process lifetime.
 * Alongside the texture we keep the cabinet light colour derived from it. */
typedef struct {
    char path[300];
    Texture2D tex;
    bool loaded;
    Color light;
} IconCacheEntry;

static IconCacheEntry sIconCache[MANIFEST_MAX_GAMES + 1];
static int sIconCacheCount = 0;

/* The icon's dominant vivid hue: a weighted 12-bin hue histogram (weight =
 * saturation x value, so greys and dark outlines don't vote), then the
 * winning bin's average hue at a fixed, marquee-bright saturation/value. */
static Color LightFromImage(Image img) {
    float binWeight[12] = {0}, binHue[12] = {0};
    Color *px = LoadImageColors(img);
    if (!px) return kTeal;

    int step = (img.width * img.height > 128 * 128) ? 2 : 1; /* big icons: every other pixel is plenty */
    for (int y = 0; y < img.height; y += step) {
        for (int x = 0; x < img.width; x += step) {
            Color c = px[y * img.width + x];
            if (c.a < 128) continue;
            Vector3 hsv = ColorToHSV(c);
            if (hsv.y < 0.25f || hsv.z < 0.25f) continue;
            int bin = (int)(hsv.x / 30.0f) % 12;
            float w = hsv.y * hsv.z;
            binWeight[bin] += w;
            binHue[bin] += hsv.x * w;
        }
    }
    UnloadImageColors(px);

    int best = -1;
    for (int i = 0; i < 12; i++) {
        if (binWeight[i] > 0.0f && (best < 0 || binWeight[i] > binWeight[best])) best = i;
    }
    if (best < 0) return kTeal; /* a greyscale icon */
    return ColorFromHSV(binHue[best] / binWeight[best], 0.72f, 1.0f);
}

/* No icon at all: a stable hue from the game's name. */
static Color LightFromName(const char *name) {
    unsigned h = 2166136261u;
    for (const char *p = name; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
    return ColorFromHSV((float)(h % 360), 0.72f, 1.0f);
}

static IconCacheEntry *GetIcon(const char *path) {
    if (!path || path[0] == '\0') return NULL;
    for (int i = 0; i < sIconCacheCount; i++) {
        if (strcmp(sIconCache[i].path, path) == 0) return sIconCache[i].loaded ? &sIconCache[i] : NULL;
    }
    if (sIconCacheCount >= (int)(sizeof(sIconCache) / sizeof(sIconCache[0]))) return NULL;

    IconCacheEntry *e = &sIconCache[sIconCacheCount++];
    snprintf(e->path, sizeof(e->path), "%s", path);
    char expanded[512];
    Manifest_ExpandPath(path, expanded, sizeof(expanded));
    if (FileExists(expanded)) {
        Image img = LoadImage(expanded);
        if (img.data) {
            e->light = LightFromImage(img);
            e->tex = LoadTextureFromImage(img);
            UnloadImage(img);
            e->loaded = (e->tex.id != 0);
            if (e->loaded) SetTextureFilter(e->tex, TEXTURE_FILTER_POINT); /* keep pixel art crisp when scaled */
        }
    }
    return e->loaded ? e : NULL;
}

void Render_ForgetMissingIcons(void) {
    int kept = 0;
    for (int i = 0; i < sIconCacheCount; i++) {
        if (sIconCache[i].loaded) sIconCache[kept++] = sIconCache[i];
    }
    sIconCacheCount = kept;
}

/* A game that isn't installed has no icon at its installed path yet; its
 * source folder usually does (assets/icons/<binary name>.png). */
static IconCacheEntry *GetGameIcon(const GameEntry *game) {
    IconCacheEntry *icon = GetIcon(game->icon);
    if (icon || game->install[0] == '\0') return icon;

    char slug[SCORES_SLUG_LEN], fallback[300];
    if (!Scores_SlugFromExec(game->exec, slug, sizeof(slug))) return NULL;
    snprintf(fallback, sizeof(fallback), "%.200s/assets/icons/%s.png", game->install, slug);
    return GetIcon(fallback);
}

static Texture2D sGhostIcon;
static bool sGhostIconLoaded = false;

void Render_Init(const char *assetsDir) {
    Pix_Load(assetsDir);

    char iconPath[512];
    snprintf(iconPath, sizeof(iconPath), "%s/icons/ghost-launcher.png", assetsDir);
    if (FileExists(iconPath)) {
        sGhostIcon = LoadTexture(iconPath);
        sGhostIconLoaded = (sGhostIcon.id != 0);
        if (sGhostIconLoaded) SetTextureFilter(sGhostIcon, TEXTURE_FILTER_POINT);
    }
}

void Render_Shutdown(void) {
    for (int i = 0; i < sIconCacheCount; i++) {
        if (sIconCache[i].loaded) UnloadTexture(sIconCache[i].tex);
    }
    if (sGhostIconLoaded) UnloadTexture(sGhostIcon);
    Pix_Unload();
}

static void FormatHMS(double seconds, char *out, int outSize) { Stats_FormatTime(seconds, out, (size_t)outSize); }

/* ------------------------------------------------------------------ hall */

#define FLOOR_Y HALL_FLOOR_Y         /* where the cabinets stand */
#define CAB_W_UNITS HALL_CAB_W_UNITS /* wide enough for a 10-letter name at 16px on the marquee */
#define CAB_H_UNITS HALL_CAB_H_UNITS

/* The carpet: every arcade had one. Sparse confetti in three colours from a
 * fixed seed, so it is identical every frame with no stored state. */
static void DrawCarpet(void) {
    DrawRectangle(0, FLOOR_Y, WINDOW_WIDTH, WINDOW_HEIGHT - FLOOR_Y, kFloor);

    const Color colors[3] = {kPink, kTeal, kYellow};
    unsigned seed = 0x9E3779B9u;
    for (int i = 0; i < 130; i++) {
        seed = seed * 1664525u + 1013904223u;
        int x = (int)((seed >> 8) % (unsigned)WINDOW_WIDTH);
        seed = seed * 1664525u + 1013904223u;
        int y = FLOOR_Y + 8 + (int)((seed >> 8) % (unsigned)(WINDOW_HEIGHT - FLOOR_Y - 52)); /* stops short of the key hints */
        seed = seed * 1664525u + 1013904223u;
        int shape = (int)((seed >> 8) % 4u);
        Color c = colors[(seed >> 16) % 3u];
        c.a = 95;
        x -= x % 4; /* snap to a 4px grid so it reads as woven, not sprinkled */
        y -= y % 4;
        switch (shape) {
            case 0: DrawRectangle(x, y, 12, 4, c); break;                                   /* dash */
            case 1: DrawRectangle(x, y, 4, 12, c); break;                                   /* bar */
            case 2: DrawRectangle(x, y, 8, 4, c); DrawRectangle(x + 4, y + 4, 8, 4, c); break; /* zig */
            default: DrawRectangle(x, y, 4, 4, c); DrawRectangle(x + 8, y, 4, 4, c); break;  /* dots */
        }
    }
    DrawRectangle(0, FLOOR_Y - 4, WINDOW_WIDTH, 4, kBaseboard);
}

/* ------------------------------------------------------- the hall itself
 * Everything behind the cabinets, in layers, back to front: the wall, a row
 * of dim machines far off across the hall, spotlights from the ceiling with
 * their pools on the carpet, a neon tube, the carpet, a strip light along
 * the floor, and a few dust motes drifting through it all. Nothing here is
 * an image: it is rectangles, triangles and one seeded scatter. The ambient
 * light eases toward the colour of whichever machine is selected, so the
 * whole room leans toward the game you are about to play. */

static Color sTint = {33, 212, 200, 255};

static void EaseTint(Color target, float dt) {
    float k = 1.0f - expf(-5.0f * dt);
    sTint = Mix(sTint, target, k);
}

static Color LightFor(const GameEntry *game) {
    IconCacheEntry *icon = GetIcon(game->icon);
    if (!icon && game->install[0] != '\0') {
        char slug[SCORES_SLUG_LEN], fallback[300];
        if (Scores_SlugFromExec(game->exec, slug, sizeof(slug))) {
            snprintf(fallback, sizeof(fallback), "%.200s/assets/icons/%s.png", game->install, slug);
            icon = GetIcon(fallback);
        }
    }
    return icon ? icon->light : LightFromName(game->name);
}

static unsigned Lcg(unsigned *seed) {
    *seed = *seed * 1664525u + 1013904223u;
    return *seed >> 8;
}

/* Machines far down the hall: silhouettes with one lit screen each, so the
 * room looks full even when the catalog is short. */
static void DrawFarCabinets(float time) {
    const Color palette[5] = {kPink, kTeal, kYellow, {150, 244, 160, 255}, {160, 136, 255, 255}};
    unsigned seed = 0xA5A5F00Du;
    int baseY = FLOOR_Y - 34;
    for (int i = 0; i < 18; i++) {
        int w = 34, h = 60 + (int)(Lcg(&seed) % 4u) * 6;
        int x = 8 + i * 54 + (int)(Lcg(&seed) % 6u);
        Color glow = palette[Lcg(&seed) % 5u];
        float phase = (float)(Lcg(&seed) % 628u) / 100.0f;
        float pulse = 0.55f + 0.45f * sinf(time * 0.9f + phase);

        DrawRectangle(x, baseY - h, w, h, (Color){25, 20, 54, 255});
        DrawRectangle(x + 2, baseY - h - 8, w - 4, 8, Mix((Color){25, 20, 54, 255}, glow, 0.20f + 0.14f * pulse)); /* marquee */
        DrawRectangle(x + 5, baseY - h + 7, w - 10, 20, Mix((Color){10, 8, 22, 255}, glow, 0.16f * pulse + 0.05f)); /* screen */
        DrawRectangle(x, baseY - h + 32, w, 3, (Color){36, 30, 76, 255});                                            /* control panel */
        DrawRectangle(x + 4, baseY - 2, w - 8, 2, (Color){17, 13, 40, 255});
    }
}

static void DrawSpotlights(float time, Color tint) {
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < 5; i++) {
        float x = 96.0f + (float)i * 192.0f;
        float sway = sinf(time * 0.35f + (float)i * 1.7f) * 10.0f;
        float breathe = 0.85f + 0.15f * sinf(time * 0.6f + (float)i * 2.3f);
        Color c = Mix(tint, kWarmWhite, 0.35f);
        /* Three nested cones: the steps read as pixel-art shading, not a blur. */
        for (int layer = 0; layer < 3; layer++) {
            float half = 150.0f - (float)layer * 46.0f;
            unsigned char a = (unsigned char)((7 + layer * 5) * breathe);
            DrawTriangle((Vector2){x, -8.0f}, (Vector2){x - half + sway, (float)FLOOR_Y}, (Vector2){x + half + sway, (float)FLOOR_Y}, Fade(c, a / 255.0f));
        }
        DrawEllipse((int)(x + sway), FLOOR_Y + 14, 118.0f, 14.0f, Fade(c, 0.07f * breathe));
        DrawEllipse((int)(x + sway), FLOOR_Y + 14, 62.0f, 8.0f, Fade(c, 0.09f * breathe));
        /* the lamp itself */
        DrawRectangle((int)x - 1, 0, 2, 12, Fade(kBaseboard, 0.9f));
        DrawTriangle((Vector2){x - 15.0f, 26.0f}, (Vector2){x + 15.0f, 26.0f}, (Vector2){x, 10.0f}, Fade(kCabinetEdge, 0.9f));
        DrawRectangle((int)x - 5, 26, 10, 3, Fade(kWarmWhite, 0.9f * breathe));
    }
    EndBlendMode();
}

static void DrawNeonTube(float time, Color tint) {
    /* Now and then the tube stutters, the way a tired one does. */
    float flicker = (!Prefs_Get()->reducedFlashing && fmodf(time, 9.3f) < 0.07f) ? 0.3f : 1.0f;
    Color core = Mix(tint, kWarmWhite, 0.55f);
    int y = 54;
    BeginBlendMode(BLEND_ADDITIVE);
    DrawRectangle(0, y - 8, WINDOW_WIDTH, 17, Fade(tint, 0.05f * flicker));
    DrawRectangle(0, y - 4, WINDOW_WIDTH, 9, Fade(tint, 0.12f * flicker));
    EndBlendMode();
    DrawRectangle(0, y - 1, WINDOW_WIDTH, 3, Fade(core, 0.9f * flicker));
    /* mounting brackets */
    for (int x = 60; x < WINDOW_WIDTH; x += 180) DrawRectangle(x, y + 3, 4, 8, (Color){40, 34, 84, 255});
}

/* A real exit now: under the mouse it lights up fully and glows, and it
 * presses in like a button. At rest it looks as it always has. */
static void DrawExitSign(float time, KitLook look) {
    int x = 28, y = 68;
    bool hot = look == KIT_LOOK_HOVER || look == KIT_LOOK_PRESSED;
    float on = (hot || fmodf(time, 4.0f) < 3.6f) ? 1.0f : 0.55f;
    int sink = look == KIT_LOOK_PRESSED ? 2 : 0;
    Color green = {90, 230, 130, 255};
    DrawRectangle(x - 1, y - 1, 36, 14, look == KIT_LOOK_PRESSED ? (Color){8, 26, 16, 255} : (Color){14, 40, 26, 255});
    BeginBlendMode(BLEND_ADDITIVE);
    DrawRectangle(x - 6, y - 6, 46, 24, Fade(green, (hot ? 0.16f : 0.06f) * on));
    EndBlendMode();
    PText("EXIT", x + 2, y + 2 + sink, TYPE_S, Fade(hot ? Mix(green, kWarmWhite, 0.4f) : green, on));
}

static void DrawMotes(float time) {
    unsigned seed = 0x5EED1234u;
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < 44; i++) {
        float bx = (float)(Lcg(&seed) % (unsigned)WINDOW_WIDTH);
        float by = (float)(Lcg(&seed) % (unsigned)(FLOOR_Y - 60)) + 60.0f;
        float speed = 4.0f + (float)(Lcg(&seed) % 8u);
        float phase = (float)(Lcg(&seed) % 628u) / 100.0f;
        float x = fmodf(bx + time * speed * 0.6f + sinf(time * 0.5f + phase) * 8.0f, (float)WINDOW_WIDTH);
        float y = fmodf(by - time * speed + (float)FLOOR_Y * 4.0f, (float)(FLOOR_Y - 60)) + 60.0f;
        float twinkle = 0.35f + 0.65f * (0.5f + 0.5f * sinf(time * 1.7f + phase * 3.0f));
        DrawRectangle((int)x & ~1, (int)y & ~1, 2, 2, Fade(kWarmWhite, 0.30f * twinkle));
    }
    EndBlendMode();
}


/* A faint diamond lattice over the wall, and a wainscot band at its foot. */
static void DrawWallpaper(Color tint) {
    Color line = Mix(kBaseboard, tint, 0.25f);
    for (int y = 64; y < FLOOR_Y - 40; y += 32) {
        for (int x = 0; x < WINDOW_WIDTH; x += 32) {
            float a = 0.055f;
            DrawLine(x, y + 16, x + 16, y, Fade(line, a));
            DrawLine(x + 16, y, x + 32, y + 16, Fade(line, a));
        }
    }
    int wy = FLOOR_Y - 44;
    DrawRectangle(0, wy, WINDOW_WIDTH, 40, Fade(kCabinetDark, 0.85f));
    DrawRectangle(0, wy, WINDOW_WIDTH, 3, kBaseboard);
    for (int x = 0; x < WINDOW_WIDTH; x += 64) DrawRectangle(x + 8, wy + 8, 48, 24, Fade(kBaseboard, 0.35f));
}

/* GHOST ARCADE in neon: two colours, a halo, and one letter with a bad connection. */
static void DrawNeonSign(float time, Color tint) {
    /* stacked on the empty left wall, clear of the selected machine's marquee */
    const char *a = "GHOST", *b = "ARCADE";
    int size = 32, x = 44, y = 96;
    float flick = (!Prefs_Get()->reducedFlashing && (fmodf(time, 7.1f) < 0.09f || (fmodf(time, 11.3f) > 5.0f && fmodf(time, 11.3f) < 5.06f))) ? 0.25f : 1.0f;
    BeginBlendMode(BLEND_ADDITIVE);
    for (int r = 3; r >= 1; r--) {
        Color ca = Fade(kPink, 0.05f * (float)(4 - r) * flick), cb = Fade(kTeal, 0.05f * (float)(4 - r) * flick);
        for (int dx = -r * 2; dx <= r * 2; dx += r * 2) for (int dy = -r * 2; dy <= r * 2; dy += r * 2) {
            PText(a, x + dx, y + dy, size, ca);
            PText(b, x + dx, y + 40 + dy, size, cb);
        }
    }
    EndBlendMode();
    PText(a, x, y, size, Fade(Mix(kPink, kWarmWhite, 0.35f), flick));
    PText(b, x, y + 40, size, Fade(Mix(kTeal, kWarmWhite, 0.35f), flick));
    float blink = fmodf(time, 1.6f) < 1.0f ? 1.0f : 0.15f;
    PText("INSERT COIN", x + 2, y + 86, TYPE_S, Fade(kYellow, 0.85f * blink));
    DrawRectangle(x - 10, y - 8, 2, 6, kBaseboard);
    DrawRectangle(x + PMeasure(b, size) + 8, y - 8, 2, 6, kBaseboard);
    (void)tint;
}

static void DrawFloorSheen(Color tint) {
    /* perspective seams on the carpet and a wet-look highlight */
    static const int kSeams[] = {16, 38, 68, 108, 160};
    for (int i = 0; i < 5; i++) {
        int y = FLOOR_Y + kSeams[i];
        if (y < WINDOW_HEIGHT - 4) DrawRectangle(0, y, WINDOW_WIDTH, 1, Fade(kBaseboard, 0.10f + 0.02f * (float)i));
    }
    for (int x = -400; x < WINDOW_WIDTH + 400; x += 96) DrawLine(WINDOW_WIDTH / 2 + (x - WINDOW_WIDTH / 2) / 4, FLOOR_Y, x, WINDOW_HEIGHT, Fade(kBaseboard, 0.07f));
    BeginBlendMode(BLEND_ADDITIVE);
    DrawRectangleGradientV(0, FLOOR_Y, WINDOW_WIDTH, 90, Fade(Mix(tint, kWarmWhite, 0.4f), 0.05f), Fade(tint, 0.0f));
    EndBlendMode();
}

static void DrawVignette(void) {
    Color dark = {6, 4, 14, 255};
    DrawRectangleGradientH(0, 0, 170, WINDOW_HEIGHT, Fade(dark, 0.55f), Fade(dark, 0.0f));
    DrawRectangleGradientH(WINDOW_WIDTH - 170, 0, 170, WINDOW_HEIGHT, Fade(dark, 0.0f), Fade(dark, 0.55f));
    DrawRectangleGradientV(0, WINDOW_HEIGHT - 70, WINDOW_WIDTH, 70, Fade(dark, 0.0f), Fade(dark, 0.45f));
}

/* full = the hall with its floor (the cabinet row); otherwise a quieter
 * version for the secondary screens, which have text to read. */
static void DrawHall(bool full, KitLook exitLook) {
    float time = (float)KitDraw_Clock();
    Color tint = sTint;

    if (full) {
        DrawRectangleGradientV(0, 0, WINDOW_WIDTH, FLOOR_Y, Mix(kWallTop, tint, 0.07f), Mix(kWallBottom, tint, 0.03f));
        /* panel seams on the wall: barely there, but they stop it being a flat wash */
        for (int x = 0; x < WINDOW_WIDTH; x += 96) DrawRectangle(x, 60, 1, FLOOR_Y - 60, Fade(kBaseboard, 0.10f));
        DrawWallpaper(tint);
        DrawFarCabinets(time);
        DrawSpotlights(time, tint);
        DrawNeonTube(time, tint);
        DrawNeonSign(time, tint);
        DrawExitSign(time, exitLook);
        DrawCarpet();
        DrawFloorSheen(tint);
        /* a strip light along the foot of the wall, and its wash on the carpet */
        BeginBlendMode(BLEND_ADDITIVE);
        DrawRectangleGradientV(0, FLOOR_Y, WINDOW_WIDTH, 34, Fade(tint, 0.13f), Fade(tint, 0.0f));
        EndBlendMode();
        DrawRectangle(0, FLOOR_Y + 1, WINDOW_WIDTH, 2, Fade(Mix(tint, kWarmWhite, 0.5f), 0.85f));
        DrawMotes(time);
    } else {
        DrawRectangleGradientV(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, Mix(kWallTop, tint, 0.05f), kFloor);
        BeginBlendMode(BLEND_ADDITIVE);
        for (int i = 0; i < 3; i++) {
            float x = 160.0f + (float)i * 320.0f;
            DrawTriangle((Vector2){x, -8.0f}, (Vector2){x - 200.0f, (float)WINDOW_HEIGHT}, (Vector2){x + 200.0f, (float)WINDOW_HEIGHT}, Fade(Mix(tint, kWarmWhite, 0.3f), 0.025f));
        }
        EndBlendMode();
        DrawMotes(time);
    }
}


/* ------------------------------------------------------ attract-mode screens
 * Each cabinet's screen plays a tiny looping scene of its game instead of a
 * still icon: rectangles and lines in a 100x88 virtual box, driven by time.
 * They are not the games (no rules, no input), just enough to make the row
 * of machines look alive. Unknown games fall back to their icon. */

typedef struct { float sx, sy, sw, sh, bright; } DemoBox;

static void DR(const DemoBox *b, float x, float y, float w, float h, Color c) {
    DrawRectangle((int)(b->sx + x * b->sw / 100.0f), (int)(b->sy + y * b->sh / 88.0f),
                  (int)ceilf(w * b->sw / 100.0f), (int)ceilf(h * b->sh / 88.0f), Fade(c, b->bright));
}
static void DL(const DemoBox *b, float x0, float y0, float x1, float y1, Color c) {
    DrawLineEx((Vector2){b->sx + x0 * b->sw / 100.0f, b->sy + y0 * b->sh / 88.0f},
               (Vector2){b->sx + x1 * b->sw / 100.0f, b->sy + y1 * b->sh / 88.0f}, 1.6f, Fade(c, b->bright));
}
static float Tri(float x) { x = fmodf(x, 2.0f); if (x < 0) x += 2.0f; return x < 1.0f ? x : 2.0f - x; }

static bool NameIs(const char *name, const char *want) {
    for (; *want; name++, want++) { char c = *name; if (c >= 'A' && c <= 'Z') c = (char)(c + 32); if (c != *want) return false; }
    return true;
}

static bool DrawDemo(const char *name, const DemoBox *b, float t) {
    const Color kViolet = {160, 136, 255, 255}, kCyan = {90, 220, 255, 255}, kGreenC = {120, 240, 150, 255};
    if (NameIs(name, "blockfall")) {
        Color cols[5] = {kViolet, kCyan, kYellow, kPink, kGreenC};
        DR(b, 8, 4, 84, 80, (Color){8, 6, 20, 255});
        for (int y = 0; y < 2; y++) for (int x = 0; x < 10; x++) if ((x * 7 + y * 3) % 10 != 4) DR(b, 10 + x * 8, 66 + y * 8, 7, 7, cols[(x + y) % 5]);
        float f = t * 2.0f; int piece = (int)(f / 8.0f) % 4; float row = fmodf(f, 8.0f);
        static const int shp[4][4][2] = {{{0,0},{1,0},{2,0},{1,1}}, {{0,0},{0,1},{1,1},{2,1}}, {{0,0},{1,0},{0,1},{1,1}}, {{0,0},{1,0},{1,1},{2,1}}};
        for (int k = 0; k < 4; k++) DR(b, 10 + (3 + shp[piece][k][0] + piece) * 8, 6 + ((int)row + shp[piece][k][1]) * 8, 7, 7, cols[piece]);
        return true;
    }
    if (NameIs(name, "coilrush")) {
        DR(b, 8, 6, 84, 78, (Color){8, 14, 16, 255});
        float L = 256.0f, head = fmodf(t * 34.0f, L);
        for (int i = 0; i < 12; i++) {
            float d = fmodf(head - (float)i * 5.0f + L, L), x, y;
            if (d < 70) { x = 15 + d; y = 15; } else if (d < 128) { x = 85; y = 15 + d - 70; } else if (d < 198) { x = 85 - (d - 128); y = 73; } else { x = 15; y = 73 - (d - 198); }
            DR(b, x - 2.5f, y - 2.5f, 5, 5, i == 0 ? (Color){200, 255, 210, 255} : kGreenC);
        }
        DR(b, 46, 40, 5, 5, fmodf(t, 0.6f) < 0.4f ? kPink : (Color){140, 40, 90, 255});
        return true;
    }
    if (NameIs(name, "brickburst")) {
        Color cols[5] = {kPink, {255, 150, 60, 255}, kYellow, kGreenC, kCyan};
        DR(b, 0, 0, 100, 88, (Color){10, 8, 24, 255});
        for (int r = 0; r < 5; r++) for (int c = 0; c < 8; c++) if (!((r + c) % 7 == 3 && fmodf(t, 6.0f) > 3.0f)) DR(b, 6 + c * 11.5f, 8 + r * 7, 10.5f, 6, cols[r]);
        float bx = 8 + 84 * Tri(t * 0.45f), by = 78 - 40 * Tri(t * 0.9f + 0.3f);
        DR(b, bx - 1.5f, by - 1.5f, 3.5f, 3.5f, WHITE);
        DR(b, 8 + 84 * Tri(t * 0.45f + 0.03f) - 9, 82, 18, 3.5f, kViolet);
        return true;
    }
    if (NameIs(name, "skyraid")) {
        DR(b, 0, 0, 100, 88, (Color){6, 10, 24, 255});
        for (int i = 0; i < 14; i++) DR(b, (float)((i * 37) % 100), fmodf((float)((i * 53) % 88) + t * (10 + i % 4 * 6), 88.0f), 1.5f, 1.5f, (Color){160, 190, 255, 255});
        for (int e = 0; e < 4; e++) { float ex = 14 + e * 22 + 6 * sinf(t * 1.4f + (float)e), ey = 8 + fmodf(t * 12.0f + (float)e * 9.0f, 40.0f); DR(b, ex - 5, ey, 10, 5, kPink); DR(b, ex - 2, ey + 5, 4, 3, kPink); }
        float px = 50 + 34 * sinf(t * 0.8f);
        DR(b, px - 1.5f, 70, 3, 8, kCyan); DR(b, px - 6, 76, 12, 4, kCyan);
        for (int k = 0; k < 2; k++) DR(b, px - 1, 68 - fmodf(t * 70.0f + (float)k * 30.0f, 60.0f), 2, 5, kYellow);
        return true;
    }
    if (NameIs(name, "ghostmaze")) {
        DR(b, 0, 0, 100, 88, (Color){8, 6, 16, 255});
        float hx = 20 + 60 * Tri(t * 0.25f);
        BeginBlendMode(BLEND_ADDITIVE);
        DrawTriangle((Vector2){b->sx + hx * b->sw / 100.0f, b->sy + 30 * b->sh / 88.0f}, (Vector2){b->sx + (hx - 30) * b->sw / 100.0f, b->sy + 84 * b->sh / 88.0f}, (Vector2){b->sx + (hx + 30) * b->sw / 100.0f, b->sy + 84 * b->sh / 88.0f}, Fade(kYellow, 0.22f * b->bright));
        EndBlendMode();
        DR(b, hx - 3, 24, 6, 8, (Color){255, 110, 190, 255}); DR(b, hx - 4, 28, 8, 5, (Color){255, 110, 190, 255});
        float gx = 18 + 64 * Tri(t * 0.35f + 0.5f), gy = 50 + 10 * sinf(t * 2.0f);
        DR(b, gx - 5, gy - 5, 10, 10, WHITE); DR(b, gx - 5, gy + 5, 3, 3, WHITE); DR(b, gx + 2, gy + 5, 3, 3, WHITE);
        DR(b, gx - 3, gy - 2, 2, 2, (Color){21, 16, 43, 255}); DR(b, gx + 1, gy - 2, 2, 2, (Color){21, 16, 43, 255});
        return true;
    }
    if (NameIs(name, "rockdrift")) {
        DR(b, 0, 0, 100, 88, (Color){5, 8, 16, 255});
        Color lime = {180, 255, 100, 255};
        for (int i = 0; i < 3; i++) {
            float cx = fmodf(20 + (float)i * 34 + t * (6 + (float)i * 3), 120) - 10, cy = 20 + (float)i * 22 + 6 * sinf(t + (float)i);
            float r = 9 + (float)i * 3, a = t * (0.6f + (float)i * 0.3f);
            for (int v = 0; v < 8; v++) { float a0 = a + (float)v * 0.785f, a1 = a + (float)(v + 1) * 0.785f, r0 = r * (0.85f + 0.15f * ((v * 5) % 3) / 2.0f), r1 = r * (0.85f + 0.15f * (((v + 1) * 5) % 3) / 2.0f);
                DL(b, cx + sinf(a0) * r0, cy - cosf(a0) * r0, cx + sinf(a1) * r1, cy - cosf(a1) * r1, lime); }
        }
        float a = t * 1.4f;
        for (int k = 0; k < 3; k++) { float ang[3] = {0, 2.5f, -2.5f}, rad[3] = {9, 6, 6}; float aa0 = a + ang[k], aa1 = a + ang[(k + 1) % 3]; float r0 = rad[k], r1 = rad[(k + 1) % 3]; DL(b, 50 + sinf(aa0) * r0, 44 - cosf(aa0) * r0, 50 + sinf(aa1) * r1, 44 - cosf(aa1) * r1, WHITE); }
        return true;
    }
    if (NameIs(name, "lanehop")) {
        DR(b, 0, 0, 100, 88, (Color){14, 34, 70, 255});
        DR(b, 0, 30, 100, 40, (Color){38, 36, 50, 255});
        Color cars[3] = {{255, 104, 96, 255}, kCyan, kYellow};
        for (int l = 0; l < 3; l++) for (int k = 0; k < 2; k++) { float x = fmodf((float)k * 60 + t * (18 + (float)l * 9) * (l % 2 ? -1 : 1) + 200, 130) - 15; DR(b, x, 34 + (float)l * 11, 14, 8, cars[l]); }
        for (int i = 0; i < 3; i++) DR(b, fmodf(t * 8 + (float)i * 40, 120) - 10, 10 + (float)i * 6, 22, 5, (Color){140, 88, 48, 255});
        int hop = (int)(t * 2.0f) % 6;
        DR(b, 46, 76 - (float)hop * 12, 8, 9, (Color){250, 240, 215, 255});
        return true;
    }
    if (NameIs(name, "crawlshot")) {
        DR(b, 0, 0, 100, 88, (Color){10, 8, 24, 255});
        for (int i = 0; i < 16; i++) DR(b, (float)(6 + (i * 29) % 84), (float)(14 + (i * 17) % 50), 5, 5, (Color){240, 96, 120, 255});
        float head = fmodf(t * 26.0f, 300.0f);
        for (int i = 0; i < 10; i++) { float d = head - (float)i * 5.5f; if (d < 0) continue; int row = (int)(d / 84.0f); float in = fmodf(d, 84.0f); float x = (row % 2) ? 90 - in : 6 + in; DR(b, x - 2.5f, 8 + (float)row * 8, 5.5f, 5.5f, i == 0 ? (Color){226, 255, 150, 255} : (Color){120, 220, 110, 255}); }
        float px = 50 + 30 * sinf(t * 0.9f);
        DR(b, px - 4, 78, 8, 6, (Color){232, 104, 255, 255}); DR(b, px - 1, 70 - fmodf(t * 60.0f, 60.0f) * 0.0f, 2, 6, WHITE);
        return true;
    }
    if (NameIs(name, "moondrop")) {
        DR(b, 0, 0, 100, 88, (Color){6, 8, 18, 255});
        Color amber = {255, 184, 64, 255};
        float pts[9][2] = {{0, 70}, {12, 62}, {24, 72}, {34, 76}, {48, 76}, {58, 66}, {72, 74}, {86, 60}, {100, 68}};
        for (int i = 0; i < 8; i++) DL(b, pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], i == 3 ? WHITE : amber);
        float k = fmodf(t * 0.16f, 1.0f), y = 8 + 58 * k * k * (3 - 2 * k), x = 41 + 10 * sinf(k * 3.0f) * (1 - k);
        DL(b, x - 5, y, x + 5, y, WHITE); DL(b, x - 5, y, x - 3, y - 8, WHITE); DL(b, x + 5, y, x + 3, y - 8, WHITE); DL(b, x - 3, y - 8, x + 3, y - 8, WHITE);
        if (k < 0.92f) DL(b, x, y, x + (float)((int)(t * 30) % 3 - 1), y + 8, (Color){255, 150, 70, 255});
        return true;
    }
    if (NameIs(name, "gemdelve") || NameIs(name, "gemdive")) {
        DR(b, 0, 0, 100, 88, (Color){104, 68, 46, 255});
        for (int i = 0; i < 22; i++) DR(b, (float)((i * 41) % 96), (float)((i * 23) % 84), 4, 2.5f, (i % 2) ? (Color){80, 50, 36, 255} : (Color){134, 90, 60, 255});
        DR(b, 0, 40, 100, 12, (Color){12, 10, 24, 255});
        Color gc[4] = {kCyan, kPink, kGreenC, kYellow};
        for (int g = 0; g < 4; g++) { float gx = 14 + (float)g * 24; if (fmodf(t * 0.5f, 4.0f) < (float)g * 1.0f + 0.7f || fmodf(t + (float)g, 1.4f) < 1.0f) { DR(b, gx, 42, 8, 8, gc[g]); if (fmodf(t * 2.0f + (float)g, 1.0f) < 0.15f) DR(b, gx + 3, 40, 2, 12, WHITE); } }
        float dx = 8 + 84 * Tri(t * 0.22f);
        DR(b, dx - 3, 40, 7, 10, (Color){255, 208, 80, 255}); DR(b, dx - 2, 46, 5, 5, (Color){96, 150, 230, 255});
        DR(b, 60, fmodf(t * 30.0f, 40.0f), 10, 10, (Color){150, 150, 172, 255});
        return true;
    }
    if (NameIs(name, "girderclimb")) {
        DR(b, 0, 0, 100, 88, (Color){10, 8, 24, 255});
        float ax = 50 + 26 * sinf(t * 0.5f), ay = 14;
        float ang = 0.8f * sinf(t * 1.6f), len = 44;
        float px = ax + sinf(ang) * len, py = ay + cosf(ang) * len;
        for (int r = 0; r < 12; r++) DR(b, ax + (px - ax) * (float)r / 12.0f - 0.5f, ay + (py - ay) * (float)r / 12.0f, 1.2f, 1.2f, (Color){236, 226, 200, 255});
        DR(b, ax - 3, ay - 3, 6, 6, (Color){255, 214, 90, 255});
        DR(b, px - 3, py, 6, 9, (Color){255, 208, 80, 255});
        DR(b, px - 3, py + 4, 6, 6, (Color){88, 140, 235, 255});
        DL(b, 4, 46, 30, 49, kPink); DL(b, 70, 40, 96, 37, kPink);
        for (int i = 0; i < 20; i++) { float x = (float)i * 5.0f; float h = 8.0f + 6.0f * sinf(t * 5.0f + (float)i); DR(b, x, 88 - h - 2.0f * sinf(t * 3.0f), 5, h + 6, (Color){255, 120, 50, 255}); }
        return true;
    }
    return false;
}

typedef struct {
    float x0, y0, u;
} CabFrame;

/* Rectangle in cabinet units. Edges are rounded independently so adjoining
 * parts never leave a seam while the cabinet is mid-way between sizes. */
static void CabRect(const CabFrame *f, float ux, float uy, float uw, float uh, Color c) {
    int x1 = (int)roundf(f->x0 + ux * f->u), x2 = (int)roundf(f->x0 + (ux + uw) * f->u);
    int y1 = (int)roundf(f->y0 + uy * f->u), y2 = (int)roundf(f->y0 + (uy + uh) * f->u);
    DrawRectangle(x1, y1, x2 - x1, y2 - y1, c);
}

/* One cabinet, front-on. focus: 0 = powered down at the edge of the row,
 * 1 = the selected machine, full size with everything lit. marqueePower
 * scales just the marquee light (used for the switch-on flicker). */
/* A coin seen face-on, faked-spinning by squashing its width. Drawn from
 * rectangles so it stays pixel art at any squash. */
static void DrawCoin(float cx, float cy, float radius, float spin, float visibleFrac) {
    float w = radius * (0.25f + 0.75f * fabsf(cosf(spin)));
    int top = (int)roundf(cy - radius);
    int rows = (int)roundf(radius * 2.0f * visibleFrac); /* the rest is already inside the slot */
    for (int i = 0; i < rows; i++) {
        float dy = ((float)i + 0.5f) - radius;
        float half = sqrtf(radius * radius - dy * dy) * (w / radius);
        int hx = (int)roundf(half / 2.0f) * 2; /* 2px steps: chunky edge */
        if (hx < 2) hx = 2;
        Color c = (i < rows / 3) ? (Color){255, 236, 150, 255} : kYellow;
        if (i > (int)(radius * 1.4f)) c = (Color){214, 160, 30, 255};
        DrawRectangle((int)roundf(cx) - hx, top + i, hx * 2, 1, c);
    }
    if (w > radius * 0.7f && visibleFrac > 0.8f) { /* the stamp, only when it faces us */
        DrawRectangle((int)roundf(cx) - 1, (int)roundf(cy - radius * 0.45f), 2, (int)(radius * 0.9f), (Color){214, 160, 30, 255});
    }
}

/* Under the pointer a side machine glows softly in its own light, in three
 * steps so it stays pixel art, and its marquee wakes a little. */
static void DrawHoverGlow(const CabFrame *f, Color light, bool pressed) {
    Color c = Mix(light, kWarmWhite, 0.35f);
    float x = f->x0 - f->u, w = (CAB_W_UNITS + 2) * f->u, h = CAB_H_UNITS * f->u;
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 3; i >= 1; i--) {
        float pad = 4.0f * (float)i;
        DrawRectangle((int)(x - pad), (int)(f->y0 - pad), (int)(w + 2 * pad), (int)(h + pad), Fade(c, pressed ? 0.07f : 0.05f));
    }
    EndBlendMode();
    DrawRectangleLinesEx((Rectangle){x - 2, f->y0 - 2, w + 4, h + 2}, 2.0f, Fade(c, pressed ? 0.75f : 0.5f));
}

/* The lit machine under the pointer: a click drops the coin, so its slots
 * light up and wait for it (steady with reduced flashing). */
static Color WaitingSlot(void) {
    float t = Prefs_Get()->reducedFlashing ? 0.6f : 0.45f + 0.35f * sinf((float)KitDraw_Clock() * 6.0f);
    return Mix(kCoinRed, kWarmWhite, t);
}

/* A favourite's gold star, stuck over the marquee's top left corner like a
 * sticker: 16 px, with a dark rim so it reads on any marquee light. */
static void DrawMarqueeStar(const CabFrame *f) {
    int x = (int)roundf(f->x0 - f->u) - 6, y = (int)roundf(f->y0) - 6;
    static const int kRim[4][2] = {{-2, 0}, {2, 0}, {0, -2}, {0, 2}};
    for (int i = 0; i < 4; i++) KitDraw_Star(x + kRim[i][0], y + kRim[i][1], 2, true, kInk);
    KitDraw_Star(x, y, 2, true, kYellow);
}

/* The coin door with its two slots, in the same place on every machine (the
 * jukebox's too), so the coin always lands in the left one. Dark when the
 * machine is off; waiting under the pointer; flashing once it has the
 * credit. */
#define DOOR_X (CAB_W_UNITS / 2.0f - 8.0f)

static void CoinDoor(const CabFrame *f, bool installed, float focus, float coinT, bool waiting) {
    CabRect(f, DOOR_X, 60, 16, 17, kCabinetDark);
    CabRect(f, DOOR_X, 60, 16, 1, kCabinetEdge);
    Color slot = Mix((Color){86, 22, 28, 255}, kCoinRed, installed ? focus : 0.0f);
    /* Credit accepted: the slots flash white, then strobe until the game is up. */
    if (coinT >= COIN_DING_AT) slot = (fmodf(coinT * 18.0f, 2.0f) < 1.0f) ? kWarmWhite : kYellow;
    else if (waiting && installed && coinT < 0.0f) slot = WaitingSlot();
    CabRect(f, DOOR_X + 3, 63, 3, 5, slot);
    CabRect(f, DOOR_X + 10, 63, 3, 5, slot);
    CabRect(f, DOOR_X + 5, 72, 6, 2, kScreenOff);
}

/* The coin's flight into the left slot, and the screen (in cabinet units)
 * warming to white as the machine takes the credit. */
static void CoinDrop(const CabFrame *f, float coinT, float scrX, float scrY, float scrW, float scrH) {
    if (coinT < 0.0f) return;
    float u = f->u;
    if (coinT >= COIN_DING_AT) {
        float k = (coinT - COIN_DING_AT) / (1.0f - COIN_DING_AT);
        int sx = (int)roundf(f->x0 + scrX * u), sw = (int)roundf(scrW * u);
        int sy = (int)roundf(f->y0 + scrY * u), sh = (int)roundf(scrH * u);
        DrawRectangle(sx, sy, sw, sh, Fade(kWarmWhite, k * k * 0.85f));
    }
    /* Flight: tossed up from the player's side of the floor, a lazy arc,
     * then straight down into the left slot. */
    float slotX = f->x0 + (DOOR_X + 4.5f) * u;
    float slotY = f->y0 + 63.0f * u;
    float flight = 0.46f;
    if (coinT < flight) {
        float k = coinT / flight;
        float ease = 1.0f - (1.0f - k) * (1.0f - k);
        float startX = slotX + 150.0f, startY = (float)FLOOR_Y + 40.0f;
        float x = startX + (slotX - startX) * ease;
        float y = startY + (slotY - 26.0f - startY) * ease - sinf(k * 3.14159f) * 120.0f;
        DrawCoin(x, y, 10.0f, coinT * 38.0f, 1.0f);
    } else if (coinT < COIN_DING_AT) {
        float k = (coinT - flight) / (COIN_DING_AT - flight);
        /* Face-on, shrinking away from us into the lit slot. */
        float slotMidY = slotY + 2.5f * u;
        DrawCoin(slotX, (slotY - 26.0f) + (slotMidY - (slotY - 26.0f)) * k, 10.0f - 7.0f * k, 0.0f, 1.0f);
    }
}

static void DrawCabinet(const GameEntry *game, float centerX, float focus, float marqueePower, bool installed, float coinT,
                        KitLook look, bool favourite) {
    float u = 3.0f + focus; /* 3px units in the row, 4px when selected */
    CabFrame f = {centerX - (CAB_W_UNITS * u) / 2.0f, (float)FLOOR_Y - 4.0f - CAB_H_UNITS * u, u};
    bool hot = look == KIT_LOOK_HOVER || look == KIT_LOOK_PRESSED;
    bool front = focus > 0.99f;

    IconCacheEntry *icon = GetGameIcon(game);
    Color light = icon ? icon->light : LightFromName(game->name);
    /* A machine that isn't installed is unplugged: even when selected its
     * marquee only gets a little ambient light and the coin slots stay dark. */
    float lit = focus * marqueePower * (installed ? 1.0f : 0.18f);
    if (hot && !front) {
        DrawHoverGlow(&f, light, look == KIT_LOOK_PRESSED);
        float awake = installed ? 0.45f : 0.12f;
        if (lit < awake) lit = awake;
    }

    /* Light it throws: a pool on the carpet and a halo on the wall. */
    if (lit > 0.02f) {
        DrawEllipse((int)centerX, FLOOR_Y + 12, 150.0f * focus, 16.0f * focus, Fade(light, 0.16f * lit));
        DrawRectangleGradientV((int)(centerX - 62.0f * focus), FLOOR_Y + 2, (int)(124.0f * focus), 86, Fade(light, 0.12f * lit), Fade(light, 0.0f)); /* its reflection */
        DrawCircleGradient((Vector2){centerX, f.y0 + 6 * u}, 170.0f, Fade(light, 0.22f * lit), Fade(light, 0.0f));
    }

    const float W = CAB_W_UNITS;
    CabRect(&f, -2, 84, W + 4, 1.5f, (Color){0, 0, 0, 110}); /* contact shadow */

    /* Marquee box + backlit face. */
    CabRect(&f, -1, 0, W + 2, 12, kCabinetDark);
    Color face = Mix(Mix(kCabinetDark, light, 0.16f), light, lit);
    CabRect(&f, 1, 2, W - 2, 8, face);
    if (lit > 0.5f) CabRect(&f, 1, 2, W - 2, 1, Mix(light, kWarmWhite, 0.55f)); /* tube hot-spot along the top */

    /* Name: as big as fits the face, clipped if it still doesn't. */
    {
        char name[24];
        snprintf(name, sizeof(name), "%.20s", game->name);
        for (char *p = name; *p; p++) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
        int faceW = (int)((W - 3) * u);
        int size = (focus > 0.85f && PMeasure(name, TYPE_M) <= faceW) ? TYPE_M : TYPE_S;
        size_t len = strlen(name);
        while (len > 1 && PMeasure(name, size) > faceW) name[--len] = '\0';
        int ty = (int)roundf(f.y0 + 6 * u) - size / 2;
        /* Dark ink on a bright face, the light's own colour on a dark one --
         * never mid-tone on mid-tone, which is what a half-lit marquee is. */
        Color nameColor = lit > 0.55f ? kInk : Mix(light, kWarmWhite, 0.25f + 0.5f * lit);
        PTextCentered(name, size, (int)roundf(centerX), ty, nameColor);
    }
    if (favourite) DrawMarqueeStar(&f);

    /* Body, speaker brow, bezel. */
    CabRect(&f, 0, 12, W, 72, kCabinet);
    CabRect(&f, 0, 12, W, 4, kCabinetDark);
    for (int i = 0; i < 5; i++) CabRect(&f, W / 2 - 7 + i * 3, 13.5f, 2, 1, kCabinetEdge);
    CabRect(&f, 2, 16, W - 4, 30, (Color){18, 14, 38, 255});

    /* Screen: the game's icon, dim when the machine is off. */
    CabRect(&f, 5, 18, W - 10, 26, Mix(kScreenOff, kScreenOn, focus));
    bool drewDemo = false;
    if (installed) {
        float sx0 = roundf(f.x0 + 5 * u), sy0 = roundf(f.y0 + 18 * u), sw0 = roundf((W - 10) * u), sh0 = roundf(26 * u);
        DemoBox box = {sx0, sy0, sw0, sh0, 0.30f + 0.70f * focus};
        BeginScissorMode((int)sx0, (int)sy0, (int)sw0, (int)sh0);
        drewDemo = DrawDemo(game->name, &box, (float)KitDraw_Clock());
        EndScissorMode();
    }
    if (icon && !drewDemo) {
        float side = 22 * u;
        Rectangle dst = {roundf(centerX - side / 2), roundf(f.y0 + 20 * u), roundf(side), roundf(side)};
        unsigned char v = installed ? (unsigned char)(70 + 185 * focus) : 45;
        DrawTexturePro(icon->tex, (Rectangle){0, 0, (float)icon->tex.width, (float)icon->tex.height},
                       dst, (Vector2){0, 0}, 0.0f, (Color){v, v, v, 255});
    } else if (!drewDemo) {
        PTextCentered("?", TYPE_L, (int)centerX, (int)(f.y0 + 28 * u), Mix(kCabinetEdge, light, focus));
    }
    if (!installed) {
        /* The arcade operator's paper sign, taped over the dead screen. */
        Color paper = Mix((Color){150, 142, 128, 255}, kWarmWhite, 0.35f + 0.65f * focus);
        CabRect(&f, W / 2 - 15, 24, 30, 14, paper);
        CabRect(&f, W / 2 - 16, 23, 5, 2.5f, (Color){255, 210, 63, 150});
        CabRect(&f, W / 2 + 11, 23, 5, 2.5f, (Color){255, 210, 63, 150});
        PTextCentered("NOT", TYPE_S, (int)roundf(centerX), (int)roundf(f.y0 + 26.5f * u), kInk);
        PTextCentered("INSTALLED", TYPE_S, (int)roundf(centerX), (int)roundf(f.y0 + 32 * u), kInk);
    }
    if (installed && focus > 0.5f) { /* scanlines only on the machine that is on */
        int sx = (int)roundf(f.x0 + 5 * u), sw = (int)roundf((W - 10) * u);
        int top = (int)roundf(f.y0 + 18 * u), bottom = (int)roundf(f.y0 + 44 * u);
        for (int y = top; y < bottom; y += 4) DrawRectangle(sx, y, sw, 1, (Color){0, 0, 0, 70});
    }

    /* Control panel: lip, joystick, three buttons. */
    CabRect(&f, -1, 46, W + 2, 4, kCabinetEdge);
    CabRect(&f, 0, 50, W, 6, kCabinetDark);
    CabRect(&f, 12, 42, 1.5f, 4.5f, (Color){190, 186, 214, 255});
    CabRect(&f, 11, 40, 3.5f, 3, Mix((Color){110, 30, 34, 255}, kCoinRed, focus));
    for (int i = 0; i < 3; i++) {
        Color b = (i == 1) ? kWarmWhite : light;
        CabRect(&f, W - 22 + i * 5, 46.5f, 3, 2, Mix(Mix(kCabinetDark, b, 0.35f), b, focus));
    }

    /* Front: side-art stripes, coin door with two slots, kick plate. */
    Color stripe = Mix(Mix(kCabinet, light, 0.30f), light, focus);
    CabRect(&f, 0, 56, 2, 24, stripe);
    CabRect(&f, W - 2, 56, 2, 24, stripe);
    CabRect(&f, 3, 56, 1, 24, Mix(kCabinet, stripe, 0.45f));
    CabRect(&f, W - 4, 56, 1, 24, Mix(kCabinet, stripe, 0.45f));
    CoinDoor(&f, installed, focus, coinT, hot && front);
    CabRect(&f, 0, 80, W, 4, kCabinetDark);
    CoinDrop(&f, coinT, 5, 18, W - 10, 26);
}

/* "Playing as TRG", a chip like the Menu chip beside it: screens.c says
 * what fits where; a click opens Settings > Player. An empty name is a red
 * "Set your name". */
static void DrawPlayingAs(const char *arcadeName, bool admin, bool updateBehind, KitLook look) {
    PlayingAs pa;
    Screens_PlayingAs(&pa, arcadeName, admin, updateBehind);
    KitItem block;
    memset(&block, 0, sizeof(block));
    block.kind = KIT_BUTTON;
    block.style = KIT_STYLE_SMALL;
    block.textSize = 8;
    block.r = pa.r;
    KitDraw_Item(&block, look == KIT_LOOK_FOCUSED ? KIT_LOOK_NORMAL : look, KIT_NONE, KIT_NONE);
    Color label = look == KIT_LOOK_HOVER ? kText : kTextDim, name = pa.empty ? kCoinRed : kYellow;
    int sink = 0;
    if (look == KIT_LOOK_PRESSED) {
        label = name = kInk;
        sink = 2;
    }
    if (pa.label[0]) PText(pa.label, pa.labelX, pa.y + sink, TYPE_S, label);
    PText(pa.name, pa.nameX, pa.y + sink, TYPE_S, name);
}

/* The title and, for admin, its badge; the chips come from the view. */
static void DrawHeader(const char *arcadeName, bool admin, bool updateBehind, KitLook nameLook) {
    if (sGhostIconLoaded) {
        DrawTexturePro(sGhostIcon, (Rectangle){0, 0, (float)sGhostIcon.width, (float)sGhostIcon.height},
                       (Rectangle){24, 12, 32, 32}, (Vector2){0, 0}, 0.0f, WHITE);
    }
    PText("GHOST LAUNCHER", 68, 20, TYPE_M, kWarmWhite);
    if (admin) {
        int x = 68 + PMeasure("GHOST LAUNCHER", TYPE_M) + 20;
        DrawRectangle(x, 18, PMeasure("ADMIN", TYPE_S) + 16, 20, (Color){80, 28, 70, 255});
        PText("ADMIN", x + 8, 24, TYPE_S, kPink);
    }
    DrawPlayingAs(arcadeName, admin, updateBehind, nameLook);
}

/* Greedy word-wrap against the pixel font's measured width. Returns the
 * number of lines produced (capped at maxLines; the rest is dropped). */
#define WRAP_BUF 200
static int WrapText(const char *text, int fontSize, int maxWidth, char lines[][WRAP_BUF], int maxLines) {
    int count = 0;
    char current[WRAP_BUF] = "";
    const char *p = text;
    while (*p && count < maxLines) {
        const char *start = p;
        while (*p && *p != ' ') p++;
        char word[WRAP_BUF];
        snprintf(word, sizeof(word), "%.*s", (int)(p - start) < WRAP_BUF - 1 ? (int)(p - start) : WRAP_BUF - 1, start);
        if (*p == ' ') p++;

        char trial[2 * WRAP_BUF + 2];
        if (current[0]) snprintf(trial, sizeof(trial), "%s %s", current, word);
        else snprintf(trial, sizeof(trial), "%s", word);

        if (!current[0] || PMeasure(trial, fontSize) <= maxWidth) {
            snprintf(current, sizeof(current), "%.*s", WRAP_BUF - 1, trial);
        } else {
            snprintf(lines[count++], WRAP_BUF, "%s", current);
            snprintf(current, sizeof(current), "%s", word);
        }
    }
    if (current[0] && count < maxLines) snprintf(lines[count++], WRAP_BUF, "%s", current);
    return count;
}

/* ------------------------------------------------------------- jukebox
 * Chipforge's machine: an arched top with a neon tube round it, bubble
 * tubes up its sides, a speaker grille, and a screen that scrolls the notes
 * of the hall's song (or, with the music off, shows its title card). It
 * stands in the same footprint as a cabinet, coin door and all, so the
 * mouse and the coin find it where they find any machine. */

/* The tubes' colour drifts slowly through the house's three lights; held
 * still with reduced flashing. */
static Color TubeColour(float time, float phase) {
    if (Prefs_Get()->reducedFlashing) return kPink;
    const Color stops[3] = {kPink, kYellow, kTeal};
    float t = fmodf(time * 0.12f + phase, 3.0f);
    if (t < 0.0f) t += 3.0f;
    int i = (int)t;
    return Mix(stops[i % 3], stops[(i + 1) % 3], t - (float)i);
}

/* Half an ellipse, a row of cabinet units at a time, so its edge steps like
 * pixel art at any size. */
static void HalfEllipse(const CabFrame *f, float cx, float baseY, float a, float b, Color c) {
    for (float r = 0.0f; r < b; r += 1.0f) {
        float dy = b - r - 0.5f;
        float hw = a * sqrtf(fmaxf(0.0f, 1.0f - (dy / b) * (dy / b)));
        CabRect(f, cx - hw, baseY - b + r, hw * 2.0f, 1.0f, c);
    }
}

/* A note on the screen: its lane's pitch range spread over the lane. */
static int NoteY(int note, int lo, int hi, int laneTop, int laneH, int blockH) {
    if (hi <= lo) return laneTop + (laneH - blockH) / 2;
    return laneTop + (int)((float)(hi - note) / (float)(hi - lo) * (float)(laneH - blockH));
}

/* The song's title on a jukebox title card, up to three lines. */
static void DrawTitleCard(int sx, int sy, int sw, int sh, float bright, const char *title) {
    char lines[3][WRAP_BUF];
    int n = WrapText(title && title[0] ? title : "Built-in theme", TYPE_S, sw - 20, lines, 3);
    for (int i = 0; i < n; i++) {
        int len = (int)strlen(lines[i]);
        while (len > 1 && PMeasure(lines[i], TYPE_S) > sw - 20) lines[i][--len] = '\0'; /* one long word */
    }
    int cardH = 12 + n * 12 + 8, cardW = sw - 12;
    int cx = sx + 6, cy = sy + (sh - cardH - 14) / 2;
    Color paper = Mix((Color){150, 142, 128, 255}, kWarmWhite, 0.35f + 0.65f * bright);
    DrawRectangle(cx, cy, cardW, cardH, paper);
    DrawRectangle(cx, cy, cardW, 4, Mix(paper, kPink, bright));
    DrawRectangle(cx, cy + cardH - 4, cardW, 4, Mix(paper, kTeal, bright));
    for (int i = 0; i < n; i++) PTextCentered(lines[i], TYPE_S, sx + sw / 2, cy + 8 + i * 12, kInk);
    PTextCentered("MUSIC OFF", TYPE_S, sx + sw / 2, cy + cardH + 6, Fade(kTextDim, 0.4f + 0.6f * bright));
}

/* The notes of the hall's song scrolling past a playhead a third of the way
 * in: the lead on top, the arpeggio, the bass at the bottom, the drums as a
 * row of dots under them. Notes already played are dimmer. */
static void DrawJukeNotes(int sx, int sy, int sw, int sh, float bright, const JukeView *j) {
    JukeWindow w;
    Jukebox_Window(j->song, j->seconds, &w);
    float colW = (float)sw / (float)(JUKE_STEPS - JUKE_LEAD);
    float frac = (float)(w.playhead - floor(w.playhead));
    int headX = sx + (int)roundf((float)JUKE_LEAD * colW);
    const int order[SONG_TONE_TRACKS] = {TRACK_LEAD, TRACK_ARP, TRACK_BASS};
    const Color colour[SONG_TONE_TRACKS] = {kYellow, kPink, kTeal};
    int drumH = 6, laneH = (sh - drumH - 6) / 3, blockH = laneH / 5 < 2 ? 2 : laneH / 5;
    for (int lane = 0; lane < SONG_TONE_TRACKS; lane++) {
        int t = order[lane], top = sy + 3 + lane * laneH;
        for (int c = 0; c < JUKE_STEPS; c++) {
            JukeNote n = w.tone[t][c];
            if (n.note == NOTE_NONE) continue;
            int x0 = sx + (int)roundf(((float)c - frac) * colW), x1 = sx + (int)roundf(((float)c + 1.0f - frac) * colW);
            if (n.start) x0 += 1; /* a struck note shows where it begins */
            int y = NoteY(n.note, w.lo[t], w.hi[t], top, laneH, blockH);
            bool now = c == JUKE_LEAD, past = c < JUKE_LEAD;
            Color col = now ? Mix(colour[lane], kWarmWhite, 0.5f) : colour[lane];
            DrawRectangle(x0, y, x1 - x0, blockH, Fade(col, bright * (past ? 0.45f : 1.0f)));
        }
    }
    for (int c = 0; c < JUKE_STEPS; c++) {
        uint8_t d = w.drums[c];
        if (!d) continue;
        int x = sx + (int)roundf(((float)c + 0.5f - frac) * colW), y = sy + sh - drumH;
        float a = bright * (c < JUKE_LEAD ? 0.45f : 0.9f);
        if (d & DRUM_KICK) DrawRectangle(x - 2, y, 4, 4, Fade(kWarmWhite, a));
        else if (d & DRUM_SNARE) DrawRectangle(x - 1, y, 3, 3, Fade(kTextDim, a));
        else DrawRectangle(x, y + 1, 1, 2, Fade(kTextDim, a));
    }
    DrawRectangle(headX, sy + 2, 1, sh - 4, Fade(kWarmWhite, 0.35f * bright));
}

static void DrawJukeScreen(int sx, int sy, int sw, int sh, float bright, const JukeView *j) {
    if (!j || !j->song) return;
    BeginScissorMode(sx, sy, sw, sh);
    if (j->on) DrawJukeNotes(sx, sy, sw, sh, bright, j);
    else DrawTitleCard(sx, sy, sw, sh, bright, j->title);
    EndScissorMode();
}

/* The arched top: its frame, the neon tube round it, and the lit plate
 * with the machine's name. */
static void DrawJukeArch(const CabFrame *f, const char *name, float focus, float lit, Color light, float time) {
    const float W = CAB_W_UNITS;
    Color tube = TubeColour(time, 0.0f);
    float glow = 0.25f + 0.75f * focus;
    if (focus > 0.3f) { /* the neon tube lights the wall round the arch */
        BeginBlendMode(BLEND_ADDITIVE);
        DrawCircleGradient((Vector2){f->x0 + W / 2 * f->u, f->y0 + 16 * f->u}, (W / 2 + 6) * f->u, Fade(tube, 0.10f * focus), Fade(tube, 0.0f));
        EndBlendMode();
    }
    HalfEllipse(f, W / 2, 16, W / 2 + 1, 16, kCabinetDark);
    HalfEllipse(f, W / 2, 16, W / 2 - 1, 14.5f, Mix(kCabinetDark, tube, 0.35f + 0.65f * glow));
    HalfEllipse(f, W / 2, 16, W / 2 - 3, 12.5f, kCabinetDark);
    Color face = Mix(Mix(kCabinetDark, light, 0.16f), light, lit);
    HalfEllipse(f, W / 2, 16, W / 2 - 5, 10.5f, face);
    char upper[24];
    snprintf(upper, sizeof(upper), "%.20s", name);
    for (char *p = upper; *p; p++) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
    int faceW = (int)((W - 14) * f->u);
    int size = (focus > 0.85f && PMeasure(upper, TYPE_M) <= faceW) ? TYPE_M : TYPE_S;
    size_t len = strlen(upper);
    while (len > 1 && PMeasure(upper, size) > faceW) upper[--len] = '\0';
    Color ink = lit > 0.55f ? kInk : Mix(light, kWarmWhite, 0.25f + 0.5f * lit);
    PTextCentered(upper, size, (int)roundf(f->x0 + W / 2 * f->u), (int)roundf(f->y0 + 11.0f * f->u) - size / 2, ink);
}

/* Bubble tubes up both sides, bubbles rising (still with reduced flashing). */
static void DrawJukeTubes(const CabFrame *f, float focus, float time) {
    const float W = CAB_W_UNITS;
    bool still = Prefs_Get()->reducedFlashing;
    for (int side = 0; side < 2; side++) {
        float x = side == 0 ? 1.0f : W - 4.0f;
        Color tube = TubeColour(time, side == 0 ? 0.6f : 1.6f);
        CabRect(f, x, 17, 3, 61, Mix(kCabinetDark, tube, 0.3f + 0.6f * focus));
        CabRect(f, x + 1, 17, 1, 61, Mix(kCabinetDark, Mix(tube, kWarmWhite, 0.4f), 0.3f + 0.6f * focus));
        for (int b = 0; b < 4; b++) {
            float rise = still ? (float)b * 15.0f : fmodf(time * (5.0f + (float)b) + (float)(b * 17 + side * 9), 58.0f);
            CabRect(f, x + 1, 76.0f - rise, 1, 1, Fade(kScreenOff, 0.8f));
        }
    }
    if (focus > 0.3f) {
        BeginBlendMode(BLEND_ADDITIVE);
        for (int side = 0; side < 2; side++) {
            float x = f->x0 + (side == 0 ? 0.0f : W - 5.0f) * f->u;
            DrawRectangle((int)x, (int)(f->y0 + 17 * f->u), (int)(5 * f->u), (int)(61 * f->u), Fade(TubeColour(time, side == 0 ? 0.6f : 1.6f), 0.08f * focus));
        }
        EndBlendMode();
    }
}

static void DrawJukebox(const GameEntry *game, float centerX, float focus, float marqueePower, bool installed, float coinT, KitLook look,
                        bool favourite, const JukeView *juke) {
    float u = 3.0f + focus;
    CabFrame f = {centerX - (CAB_W_UNITS * u) / 2.0f, (float)FLOOR_Y - 4.0f - CAB_H_UNITS * u, u};
    const float W = CAB_W_UNITS;
    bool hot = look == KIT_LOOK_HOVER || look == KIT_LOOK_PRESSED, front = focus > 0.99f;
    float time = (float)KitDraw_Clock();
    IconCacheEntry *icon = GetGameIcon(game);
    Color light = icon ? icon->light : LightFromName(game->name);
    float lit = focus * marqueePower * (installed ? 1.0f : 0.18f);
    if (hot && !front) {
        DrawHoverGlow(&f, light, look == KIT_LOOK_PRESSED);
        float awake = installed ? 0.45f : 0.12f;
        if (lit < awake) lit = awake;
    }
    if (lit > 0.02f) {
        DrawEllipse((int)centerX, FLOOR_Y + 12, 150.0f * focus, 16.0f * focus, Fade(light, 0.16f * lit));
        DrawRectangleGradientV((int)(centerX - 62.0f * focus), FLOOR_Y + 2, (int)(124.0f * focus), 86, Fade(light, 0.12f * lit), Fade(light, 0.0f));
        DrawCircleGradient((Vector2){centerX, f.y0 + 8 * u}, 170.0f, Fade(light, 0.22f * lit), Fade(light, 0.0f));
    }
    CabRect(&f, -2, 84, W + 4, 1.5f, (Color){0, 0, 0, 110}); /* contact shadow */

    /* Body under the arch, a chrome rail, the screen in its bezel. */
    CabRect(&f, 0, 16, W, 68, kCabinet);
    DrawJukeArch(&f, game->name, focus, lit, light, time);
    if (favourite) {
        int sx = (int)roundf(f.x0 + 4 * u) - 6, sy = (int)roundf(f.y0 + 9 * u) - 6;
        static const int kRim[4][2] = {{-2, 0}, {2, 0}, {0, -2}, {0, 2}};
        for (int i = 0; i < 4; i++) KitDraw_Star(sx + kRim[i][0], sy + kRim[i][1], 2, true, kInk);
        KitDraw_Star(sx, sy, 2, true, kYellow);
    }
    DrawJukeTubes(&f, focus, time);
    CabRect(&f, 5, 16, W - 10, 1, kCabinetEdge);
    CabRect(&f, 5, 17, W - 10, 27, (Color){18, 14, 38, 255});
    CabRect(&f, 6, 18, W - 12, 25, Mix(kScreenOff, kScreenOn, focus));
    int sx = (int)roundf(f.x0 + 6 * u), sy = (int)roundf(f.y0 + 18 * u), sw = (int)roundf((W - 12) * u), sh = (int)roundf(25 * u);
    if (installed) {
        DrawJukeScreen(sx, sy, sw, sh, 0.30f + 0.70f * focus, juke);
        if (focus > 0.5f) for (int y = sy; y < sy + sh; y += 4) DrawRectangle(sx, y, sw, 1, (Color){0, 0, 0, 60});
    } else {
        Color paper = Mix((Color){150, 142, 128, 255}, kWarmWhite, 0.35f + 0.65f * focus);
        CabRect(&f, W / 2 - 15, 24, 30, 14, paper);
        PTextCentered("NOT", TYPE_S, (int)roundf(centerX), (int)roundf(f.y0 + 26.5f * u), kInk);
        PTextCentered("INSTALLED", TYPE_S, (int)roundf(centerX), (int)roundf(f.y0 + 32 * u), kInk);
    }

    /* Chrome rail, then the speaker grille the coin door sits in. */
    CabRect(&f, 4, 44, W - 8, 2, Mix(kCabinetEdge, kWarmWhite, 0.25f * focus));
    CabRect(&f, 5, 47, W - 10, 31, kCabinetDark);
    Color slat = Mix(kCabinet, light, 0.12f + 0.25f * lit);
    for (float x = 7.0f; x < W - 7.0f; x += 3.0f) CabRect(&f, x, 49, 1.5f, 27, slat);
    CoinDoor(&f, installed, focus, coinT, hot && front);
    CabRect(&f, 0, 80, W, 4, kCabinetDark);
    CoinDrop(&f, coinT, 6, 18, W - 12, 25);
}

#define CARD_X HALL_CARD_X
#define CARD_Y HALL_CARD_Y /* 8 px up from where it was: room for the control strip */
#define CARD_W HALL_CARD_W
#define CARD_H HALL_CARD_H

static void DrawCardStat(int x, int y, const char *label, const char *value, const char *detail, bool empty) {
    PText(label, x, y, TYPE_S, kTextDim);
    if (empty) {
        PText(value, x, y + 16, TYPE_S, kTextDim);
        return;
    }
    PText(value, x, y + 14, TYPE_M, kText);
    if (detail && detail[0]) PText(detail, x + PMeasure(value, TYPE_M) + 12, y + 22, TYPE_S, kTextDim);
}

static Color ToneOf(HallTone t) {
    switch (t) {
        case HT_PLAY:
        case HT_BAD: return kCoinRed;
        case HT_INSTALL:
        case HT_BUSY:
        case HT_NOTE: return kYellow;
        default: return kTextDim;
    }
}

/* hall.c says what the line is and has made it fit its column. */
static void DrawStatusLine(const HallStatus *st, int x) {
    if (!st || !st->text[0]) return;
    if (st->blink && fmod(KitDraw_Clock(), 1.2) >= 0.8) return; /* attract-mode blink */
    char text[HALL_STATUS_LEN + 4];
    if (st->dots) snprintf(text, sizeof(text), "%s%.*s", st->text, (int)(KitDraw_Clock() * 3.0) % 4, "...");
    else snprintf(text, sizeof(text), "%s", st->text);
    PText(text, x, CARD_Y + (st->size == TYPE_M ? 90 : 94), st->size == TYPE_M ? TYPE_M : TYPE_S, ToneOf(st->tone));
}

/* The name in 24 px, cut with "..." before it would reach the Favourite
 * button (or the scores column). */
static void DrawCardName(const char *full, int x, int right) {
    char name[40];
    snprintf(name, sizeof(name), "%.22s", full);
    int n = (int)strlen(name);
    while (n > 1 && PMeasure(name, TYPE_L) > right - x) snprintf(name, sizeof(name), "%.*s...", --n, full);
    PText(name, x, CARD_Y + 16, TYPE_L, kWarmWhite);
}

/* Text for the scores column: 16 px if it fits, else 8 px cut to fit. */
static void DrawCardWords(int x, int y, const char *label, const char *words) {
    int room = CARD_X + CARD_W - 16 - x;
    PText(label, x, y, TYPE_S, kTextDim);
    if (PMeasure(words, TYPE_M) <= room) {
        PText(words, x, y + 14, TYPE_M, kText);
        return;
    }
    char cut[64];
    snprintf(cut, sizeof(cut), "%.60s", words);
    int n = (int)strlen(cut);
    while (n > 1 && PMeasure(cut, TYPE_S) > room) snprintf(cut, sizeof(cut), "%.*s...", --n, words);
    PText(cut, x, y + 18, TYPE_S, kText);
}

/* Chipforge makes music: its card counts your songs and says what the hall
 * plays, where a game's shows its scores. */
static void DrawToolStats(const GameCard *card, int col) {
    char value[48];
    snprintf(value, sizeof(value), "%d", card->songs);
    DrawCardStat(col, CARD_Y + 12, "Songs in your library", value, card->songs == 1 ? "song" : "songs", false);
    DrawCardWords(col, CARD_Y + 48, "Hall song", card->hallSong[0] ? card->hallSong : "Built-in theme");
}

static void DrawGameStats(const GameCard *card, int col) {
    char value[48], detail[96];
    if (card->localBest.found) {
        Scores_FormatScore(card->localBest.timed, card->localBest.score, value, sizeof(value));
        snprintf(detail, sizeof(detail), "%.16s", card->localBest.mode);
        DrawCardStat(col, CARD_Y + 12, "Your best", value, detail, false);
    } else {
        DrawCardStat(col, CARD_Y + 12, "Your best", "No runs recorded yet", NULL, true);
    }

    if (card->worldBest.found) {
        Scores_FormatScore(card->worldBest.timed, card->worldBest.score, value, sizeof(value));
        snprintf(detail, sizeof(detail), "%.12s", card->worldBest.username);
        DrawCardStat(col, CARD_Y + 48, "World best", value, detail, false);
    } else {
        DrawCardStat(col, CARD_Y + 48, "World best", "Nothing online yet", NULL, true);
    }
}

static KitLook LookOf(const KitView *kv, int id);

/* The instruction card: what this machine is, and how you're doing on it. */
static void DrawCard(const GameEntry *game, const GameCard *card, const HallStatus *status, const KitView *kv) {
    KitLook look = LookOf(kv, WID_CARD); /* a click on it opens the machine's page */
    Color edge = look == KIT_LOOK_PRESSED ? kPink : (look == KIT_LOOK_HOVER ? Mix(kBaseboard, kText, 0.6f) : kBaseboard);
    DrawRectangle(CARD_X, CARD_Y, CARD_W, CARD_H, look == KIT_LOOK_HOVER ? Mix(kCard, kPanel, 0.35f) : kCard);
    DrawRectangleLinesEx((Rectangle){CARD_X, CARD_Y, CARD_W, CARD_H}, 2.0f, edge);

    int left = HALL_CARD_LEFT;
    const KitItem *fav = kv ? Kit_Get(kv->list, WID_CARD_FAVOURITE) : NULL;
    DrawCardName(game->name, left, fav ? (int)fav->r.x - 16 : HALL_FAV_RIGHT);

    char lines[2][WRAP_BUF];
    int n = WrapText(game->desc, TYPE_S, 500, lines, 2);
    for (int i = 0; i < n; i++) PText(lines[i], left, CARD_Y + 52 + i * 14, TYPE_S, kTextDim);
    DrawStatusLine(status, left);

    int col = HALL_CARD_COL;
    DrawRectangle(col - 24, CARD_Y + 14, 2, CARD_H - 28, kBaseboard);
    if (card->tool) DrawToolStats(card, col);
    else DrawGameStats(card, col);

    char value[48], detail[96];
    FormatHMS(card->playSeconds, value, sizeof(value));
    detail[0] = '\0';
    if (card->playerName[0] != '\0' && card->nameIsOverride) snprintf(detail, sizeof(detail), "as %.12s", card->playerName);
    DrawCardStat(col, CARD_Y + 84, "Time played", value, detail, false);
}

/* A hint line's words: the keys, or after pad use the pad's buttons. */
static const char *ForHints(const KitView *kv, const char *keys, const char *pad) {
    return kv && kv->hints == DEV_PAD ? pad : keys;
}

/* The look of one of the view's items, or the rest look when it has none. */
static KitLook LookOf(const KitView *kv, int id) {
    const KitItem *it = kv ? Kit_Get(kv->list, id) : NULL;
    return it ? KitDraw_LookOf(kv, it) : KIT_LOOK_NORMAL;
}

/* "4 / 12" between the strip's arrows. */
static void DrawCounter(const KitView *kv, int place, int count) {
    const KitItem *prev = Kit_Get(kv->list, WID_STRIP_PREV), *next = Kit_Get(kv->list, WID_STRIP_NEXT);
    if (!prev || !next || count <= 0) return;
    char pos[32];
    snprintf(pos, sizeof(pos), "%d / %d", place + 1, count);
    int cx = (int)((prev->r.x + prev->r.w + next->r.x) / 2.0f);
    PTextCentered(pos, TYPE_S, cx, (int)(prev->r.y + (prev->r.h - TYPE_S) / 2.0f), kText);
}

/* Over the row: where you are once there is more row than fits, and the
 * order whenever it is not your own. */
static void DrawRowPosition(const HallScene *s, int count) {
    bool ordered = s->order != ORDER_YOURS;
    if (count == 0 || (count <= 5 && !ordered)) return;
    char pos[64];
    if (ordered) snprintf(pos, sizeof(pos), "%s   %d / %d", Row_OrderName(s->order), s->place + 1, count);
    else snprintf(pos, sizeof(pos), "%d / %d", s->place + 1, count);
    PTextCentered(pos, TYPE_S, WINDOW_WIDTH / 2, 72, kTextDim);
}

static void DrawEmptyRow(const HallScene *s) {
    if (s->allHidden) {
        PTextCentered("Every machine is hidden", TYPE_M, WINDOW_WIDTH / 2, 230, kWarmWhite);
        PTextCentered("Settings > Machines puts them back on the row", TYPE_S, WINDOW_WIDTH / 2, 266, kTextDim);
        return;
    }
    PTextCentered("The arcade is empty", TYPE_M, WINDOW_WIDTH / 2, 230, kWarmWhite);
    PTextCentered(s->admin ? "Press A to wheel in the first machine" : "Add games to ~/.config/ghost-launcher/games.txt",
                  TYPE_S, WINDOW_WIDTH / 2, 266, kTextDim);
}

/* The chalkboard on the wall: DAILY, how many of the row's daily games are
 * played today, and a chalk tally, a box each, filled once played. A real
 * button: it lights under the pointer and presses in. */
static void DrawChalkboard(const Desk *d, KitLook look) {
    if (!d || d->count == 0) return;
    int x = CHALK_X, y = CHALK_Y, w = CHALK_W, h = CHALK_H;
    bool hot = look == KIT_LOOK_HOVER || look == KIT_LOOK_PRESSED;
    int sink = look == KIT_LOOK_PRESSED ? 2 : 0;
    if (hot) {
        BeginBlendMode(BLEND_ADDITIVE);
        DrawRectangle(x - 8, y - 8, w + 16, h + 16, Fade(kWarmWhite, look == KIT_LOOK_PRESSED ? 0.07f : 0.05f));
        EndBlendMode();
    }
    DrawRectangle(x + w / 2 - 2, y - 8, 4, 8, kBaseboard); /* the hook it hangs from */
    Color frame = hot ? Pal_Mix(kCabinetEdge, kText, 0.35f) : kCabinetEdge;
    DrawRectangle(x, y, w, h, frame);
    DrawRectangle(x, y, w, 2, Pal_Mix(frame, kWarmWhite, 0.25f));
    Color slate = look == KIT_LOOK_PRESSED ? Pal_Mix(kScreenOff, kTeal, 0.04f) : Pal_Mix(kScreenOff, kTeal, 0.08f);
    DrawRectangle(x + 5, y + 5, w - 10, h - 13, slate);
    DrawRectangle(x + 14, y + 22, 40, 2, Fade(kWarmWhite, 0.06f)); /* old chalk, wiped */
    DrawRectangle(x + 96, y + 54, 48, 2, Fade(kWarmWhite, 0.05f));
    Color chalk = Fade(kWarmWhite, hot ? 0.95f : 0.82f);
    PTextCentered("DAILY", TYPE_M, x + w / 2, y + 12 + sink, chalk);
    char line[32];
    Desk_BoardLine(d, line, sizeof(line));
    PTextCentered(line, TYPE_S, x + w / 2, y + 36 + sink, chalk);
    int boxes = d->count < 7 ? d->count : 7, pitch = 16, bx = x + w / 2 - (boxes * pitch - 4) / 2;
    for (int i = 0; i < boxes; i++) {
        Rectangle r = {(float)(bx + i * pitch), (float)(y + 58 + sink), 12.0f, 12.0f};
        if (d->rows[i].played) DrawRectangleRec(r, chalk);
        else DrawRectangleLinesEx(r, 1.0f, Fade(kWarmWhite, 0.55f));
        if (d->rows[i].featured) KitDraw_Star((int)r.x + 2, (int)r.y - 10, 1, true, kYellow); /* today's featured game */
    }
    DrawRectangle(x + 6, y + h - 8, w - 12, 4, Pal_Mix(frame, kInk, 0.3f)); /* the chalk tray */
    DrawRectangle(x + w - 40, y + h - 11, 14, 3, chalk);
}

/* Attract mode: the card has slid away and this stands where it was,
 * blinking like a cabinet's own prompt (steady with reduced flashing). */
static void DrawAttractPrompt(float alpha) {
    if (alpha <= 0.0f) return;
    bool steady = Prefs_Get()->reducedFlashing;
    if (!steady && fmod(KitDraw_Clock(), 1.2) >= 0.8) return;
    const char *text = "PRESS ANY BUTTON";
    int y = HALL_CARD_Y + 44;
    PTextCentered(text, TYPE_L, WINDOW_WIDTH / 2 + 3, y + 3, Fade(kInk, alpha));
    PTextCentered(text, TYPE_L, WINDOW_WIDTH / 2, y, Fade(kYellow, alpha));
}

void Render_List(const HallScene *s, const KitView *kv) {
    const Manifest *m = s->m;
    const Lineup *row = s->lineup;
    int n = row->count;
    float scroll = s->row->scroll;
    ClearBackground(kWallBottom);
    if (n > 0) EaseTint(LightFor(&m->games[s->lit]), s->dt);
    DrawHall(true, LookOf(kv, WID_EXIT_SIGN));
    if (Kit_Get(kv->list, WID_CHALKBOARD)) DrawChalkboard(s->desk, LookOf(kv, WID_CHALKBOARD));

    float power = Hall_MarqueePower(s->row);
    /* Far cabinets first so the selected one overlaps its neighbours' glow.
     * Positions are places in the row; each place shows its catalog machine. */
    for (int pass = 0; pass < 2; pass++) {
        for (int p = 0; p < n; p++) {
            float offset = (float)p - scroll;
            if (fabsf(offset) > HALL_ROW_REACH) continue;
            bool isFront = (p == s->place);
            if ((pass == 1) != isFront) continue;
            int i = row->index[p];
            bool installed = s->installed ? s->installed[i] : true, favourite = s->prefs && i < s->prefs->count && s->prefs->m[i].favourite;
            float x = Hall_CabinetCenterX(scroll, p), focus = Hall_Focus(offset), lit = isFront ? power : 1.0f, coin = isFront ? s->coinT : -1.0f;
            KitLook look = LookOf(kv, WID_CABINET + p);
            if (Manifest_IsTool(&m->games[i])) DrawJukebox(&m->games[i], x, focus, lit, installed, coin, look, favourite, s->juke);
            else DrawCabinet(&m->games[i], x, focus, lit, installed, coin, look, favourite);
        }
    }

    DrawVignette();
    DrawHeader(s->arcadeName, s->admin, s->updateBehind, LookOf(kv, WID_PLAYING_AS));

    if (n == 0) DrawEmptyRow(s);
    else if (s->card && s->cardSlide < (float)(WINDOW_HEIGHT - CARD_Y)) {
        /* Attract mode slides it down and away; its buttons (screens.c) go with it. */
        rlPushMatrix();
        rlTranslatef(0.0f, s->cardSlide, 0.0f);
        DrawCard(&m->games[s->selected], s->card, s->status, kv);
        rlPopMatrix();
    }
    DrawRowPosition(s, n);

    KitDraw_List(kv);
    DrawCounter(kv, s->place, n);
    if (n > 0 && s->attract) DrawAttractPrompt(s->cardOut);
}

/* ------------------------------------------------------- secondary screens */

static void BeginScreen(const char *title) {
    ClearBackground(kWallBottom);
    DrawHall(false, KIT_LOOK_NORMAL);
    PText(title, 80, 56, TYPE_L, kWarmWhite);
    DrawRectangle(80, 92, PMeasure(title, TYPE_L), 4, kPink);
}

/* Solid while it moves and for a moment after, then a slow blink; always
 * solid with reduced flashing. */
static bool CursorShows(const TextField *f) {
    double rest = GetTime() - f->steadySince;
    return Prefs_Get()->reducedFlashing || rest < 0.6 || fmod(rest - 0.6, 1.0) >= 0.4;
}

/* What kind of path each field is, for the note beside its prompt. */
static bool FieldIsPath(int field, PathKind *kind) {
    switch (field) {
        case 1: *kind = PATH_PROGRAM; return true;
        case 2: *kind = PATH_FILE; return true;
        case 4: *kind = PATH_FOLDER; return true;
        default: return false;
    }
}

/* One box: the field with the keys is lit, with its cursor; the others
 * show what they hold (a long one its end, where it was typed). */
static void DrawFormField(const GameForm *f, int i, KitLook look) {
    KitRect b = GameForm_BoxRect(i);
    bool active = f->active == i && f->field.editing, bad = f->errorField == i;
    int y = FORM_Y + i * FORM_PITCH;
    PText(GameForm_Prompt(i), FORM_X, y, TYPE_S, active ? kYellow : (bad ? kCoinRed : kTextDim));
    PathKind kind;
    PathStatus st = FieldIsPath(i, &kind) ? Manifest_PathStatus(GameForm_Text(f, i), kind) : PATH_EMPTY;
    if (st != PATH_EMPTY) PTextRight(Manifest_PathWords(st), TYPE_S, FORM_X + FORM_W, y, st == PATH_FOUND ? kTeal : kCoinRed);
    Color fill = active ? kPanel : (look == KIT_LOOK_HOVER ? Mix(kCabinetDark, kCabinet, 0.5f) : kCabinetDark);
    DrawRectangle((int)b.x, (int)b.y, (int)b.w, (int)b.h, fill);
    Color edge = active ? kYellow : (bad ? kCoinRed : (look == KIT_LOOK_HOVER ? kText : kCabinetEdge));
    DrawRectangleLinesEx((Rectangle){b.x, b.y, b.w, b.h}, active || bad ? 2.0f : 1.0f, edge);
    int tx = (int)b.x + FORM_PAD, ty = (int)(b.y + (b.h - 16.0f) / 2.0f), chars = GameForm_Chars();
    const char *text = GameForm_Text(f, i);
    char shown[TEXTFIELD_CAP];
    if (active) {
        snprintf(shown, sizeof(shown), "%.*s", chars, text + f->field.view);
        PText(shown, tx, ty, TYPE_M, kWarmWhite);
        if (CursorShows(&f->field)) DrawRectangle(tx + (f->field.cursor - f->field.view) * FORM_CHAR_W - 2, ty - 2, 2, 20, kYellow);
        return;
    }
    int view, lead;
    if (!GameForm_RestCut(i, text, &view, &lead)) snprintf(shown, sizeof(shown), "%s", text);
    else if (lead) snprintf(shown, sizeof(shown), "...%.*s", chars - 3, text + view);
    else snprintf(shown, sizeof(shown), "%.*s...", chars - 3, text);
    PText(shown, tx, ty, TYPE_M, kText);
}

static void DrawFormNotes(const FormScene *s, const KitView *kv) {
    if (s->stays && s->stays[0]) PText(s->stays, FORM_X, FORM_STAYS_Y, TYPE_S, kYellow);
    if (s->scores && s->scores[0]) PText(s->scores, FORM_X, FORM_STAYS_Y + 16, TYPE_S, kTextDim);
    const KitItem *carry = Kit_Get(kv->list, WID_FORM_CARRY);
    if (carry) {
        PText("Carry them over", FORM_X, (int)carry->r.y + 8, TYPE_M, kText);
        const char *line = carry->value ? s->moves : "Off: they stay where they are, under the old name.";
        PText(line, FORM_X, FORM_MOVES_Y, TYPE_S, kTextDim);
    }
    if (s->form->error[0]) PText(s->form->error, FORM_X, FORM_ERROR_Y, TYPE_S, kCoinRed);
}

static const char *FormHint(const GameForm *f, const KitView *kv) {
    if (f->active == FORM_ON_BUTTONS)
        return ForHints(kv, "Left/Right choose   Enter press   Up back to the fields   Esc leaves", "D-pad choose   A press   B leaves");
    return ForHints(kv, "Type   Enter next field   Tab or Up/Down move   Ctrl+V pastes   Esc leaves",
                    "Type with the keyboard   D-pad move   A next field   B leaves");
}

void Render_GameForm(const FormScene *s, const KitView *kv) {
    BeginScreen(s->title);
    const GameForm *f = s->form;
    for (int i = 0; i < MANIFEST_FIELD_COUNT; i++) {
        const KitItem *box = Kit_Get(kv->list, WID_FORM_FIELD + i);
        DrawFormField(f, i, box ? KitDraw_LookOf(kv, box) : KIT_LOOK_NORMAL);
    }
    DrawFormNotes(s, kv);
    for (int i = 0; i < kv->list->count; i++) {
        const KitItem *it = &kv->list->items[i];
        if (it->kind == KIT_AREA || (it->flags & KIT_SELFDRAWN)) continue;
        KitLook look = GameForm_Look(f, KitDraw_LookOf(kv, it));
        KitDraw_Item(it, look, Kit_HoverCell(kv->state, it, kv->device), Kit_PressedCell(kv->state, it));
    }
    PText(FormHint(f, kv), FORM_X, WINDOW_HEIGHT - 24, TYPE_S, kTextDim);
}

/* ------------------------------------------------------- machine page */

static void DrawScrollBar(int x, int y, int h, int top, int visible, int count);

/* The name in 24 px with the house's pink bar, cut before whatever sits to
 * its right (admin's buttons, or the arrows to the next machine). */
static void DrawPageTitle(const char *full, int right) {
    char name[64];
    snprintf(name, sizeof(name), "%.40s", full);
    int n = (int)strlen(name);
    while (n > 1 && PMeasure(name, TYPE_L) > right - PAGE_X) snprintf(name, sizeof(name), "%.*s...", --n, full);
    PText(name, PAGE_X, PAGE_TITLE_Y, TYPE_L, kWarmWhite);
    DrawRectangle(PAGE_X, 92, PMeasure(name, TYPE_L), 4, kPink);
}

/* Text cut with "..." to a width. */
static void Fit(char *out, size_t size, const char *text, int size8or16, int maxW) {
    snprintf(out, size, "%s", text);
    int n = (int)strlen(out);
    while (n > 1 && PMeasure(out, size8or16) > maxW) snprintf(out, size, "%.*s...", --n, text);
}

static void DrawPageStrip(const PageScene *s) {
    PageStripCell cells[PAGE_STRIP_MAX];
    int n = GamePage_Strip(s->data, (time_t)s->now, cells);
    for (int i = 0; i < n; i++) {
        const PageStripCell *c = &cells[i];
        int right = i + 1 < n ? cells[i + 1].x - 12 : PAGE_X + 800;
        PText(c->label, c->x, PAGE_STRIP_Y, TYPE_S, kTextDim);
        char value[64];
        if (c->dim) {
            PText(c->value, c->x, PAGE_STRIP_Y + 18, TYPE_S, kTextDim);
            continue;
        }
        if (PMeasure(c->value, TYPE_M) <= right - c->x) {
            PText(c->value, c->x, PAGE_STRIP_Y + 14, TYPE_M, kText);
            int dx = c->x + PMeasure(c->value, TYPE_M) + 10;
            char detail[40];
            Fit(detail, sizeof(detail), c->detail, TYPE_S, right - dx); /* a long board name beside a big score */
            if (c->detail[0] && PMeasure("W...", TYPE_S) <= right - dx) PText(detail, dx, PAGE_STRIP_Y + 22, TYPE_S, kTextDim);
        } else {
            Fit(value, sizeof(value), c->value, TYPE_S, right - c->x); /* a long song title */
            PText(value, c->x, PAGE_STRIP_Y + 18, TYPE_S, kText);
        }
    }
}

/* The cabinet's screen, big: its attract demo with scanlines, or the
 * operator's paper sign when it is not installed. */
static void DrawPageScreen(const PageScene *s) {
    int x = PAGE_X, y = PAGE_DEMO_Y, w = PAGE_LEFT_W, h = PAGE_DEMO_H;
    IconCacheEntry *icon = GetGameIcon(s->game);
    Color light = icon ? icon->light : LightFromName(s->game->name);
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 3; i >= 1; i--) DrawRectangle(x - 4 * i, y - 4 * i, w + 8 * i, h + 8 * i, Fade(light, s->installed ? 0.035f : 0.012f));
    EndBlendMode();
    DrawRectangle(x, y, w, h, kCabinetDark);
    DrawRectangleLinesEx((Rectangle){(float)x, (float)y, (float)w, (float)h}, 2.0f, Mix(kCabinetEdge, light, s->installed ? 0.5f : 0.15f));
    int sx = x + 10, sy = y + 10, sw = w - 20, sh = h - 20;
    DrawRectangle(sx, sy, sw, sh, s->installed ? kScreenOn : kScreenOff);
    bool drew = false;
    if (s->installed && s->juke) { /* Chipforge: the jukebox's screen, big */
        DrawJukeScreen(sx, sy, sw, sh, 1.0f, s->juke);
        drew = true;
    } else if (s->installed) {
        DemoBox box = {(float)sx, (float)sy, (float)sw, (float)sh, 1.0f};
        BeginScissorMode(sx, sy, sw, sh);
        drew = DrawDemo(s->game->name, &box, (float)KitDraw_Clock());
        EndScissorMode();
    }
    if (!drew && icon) {
        float side = 128.0f;
        unsigned char v = s->installed ? 255 : 60;
        DrawTexturePro(icon->tex, (Rectangle){0, 0, (float)icon->tex.width, (float)icon->tex.height},
                       (Rectangle){(float)(sx + (sw - 128) / 2), (float)(sy + (sh - 128) / 2), side, side}, (Vector2){0, 0}, 0.0f,
                       (Color){v, v, v, 255});
    } else if (!drew) {
        PTextCentered("?", TYPE_L, sx + sw / 2, sy + sh / 2 - 12, Mix(kCabinetEdge, light, 0.5f));
    }
    if (!s->installed) {
        Color paper = Mix((Color){150, 142, 128, 255}, kWarmWhite, 0.6f);
        int pw = 224, ph = 64, px = sx + (sw - pw) / 2, py = sy + (sh - ph) / 2;
        DrawRectangle(px, py, pw, ph, paper);
        DrawRectangle(px - 6, py - 4, 40, 10, (Color){255, 210, 63, 150});
        DrawRectangle(px + pw - 34, py - 4, 40, 10, (Color){255, 210, 63, 150});
        PTextCentered(s->installing ? "INSTALLING" : "NOT", TYPE_M, px + pw / 2, py + (s->installing ? 24 : 12), kInk);
        if (!s->installing) PTextCentered("INSTALLED", TYPE_M, px + pw / 2, py + 36, kInk);
        return;
    }
    for (int ly = sy; ly < sy + sh; ly += 4) DrawRectangle(sx, ly, sw, 1, (Color){0, 0, 0, 70});
}

static void DrawPageAbout(const PageScene *s) {
    int x = PAGE_RIGHT_X, y = PAGE_BODY_Y, w = PAGE_RIGHT_W;
    char lines[6][WRAP_BUF];
    const HowTo *h = s->data->howto;
    if (!h) {
        int n = WrapText(s->game->desc[0] ? s->game->desc : "A machine of your own.", TYPE_S, w, lines, 4);
        for (int i = 0; i < n; i++, y += 14) PText(lines[i], x, y, TYPE_S, kText);
        PText("The games' own how-to cards do not know this machine.", x, y + 12, TYPE_S, kTextDim);
        return;
    }
    int n = WrapText(h->goal, TYPE_S, w, lines, 6);
    for (int i = 0; i < n; i++, y += 14) PText(lines[i], x, y, TYPE_S, kText);
    y += 12;
    PText("Controls", x, y, TYPE_S, kYellow);
    y += 18;
    const int whatX = x + 188, whatW = x + w - whatX;
    for (int i = 0; i < HOWTO_MAX_CONTROLS && h->controls[i][0]; i++) {
        PText(h->controls[i][0], x, y, TYPE_S, kTeal);
        char what[2][WRAP_BUF];
        int m = WrapText(h->controls[i][1], TYPE_S, whatW, what, 2);
        for (int k = 0; k < m; k++) PText(what[k], whatX, y + k * 12, TYPE_S, kText);
        y += 16 + (m > 1 ? 12 : 0);
    }
    if (!h->tip || !h->tip[0]) return;
    y += 10;
    PText("Tip", x, y, TYPE_S, kYellow);
    y += 18;
    n = WrapText(h->tip, TYPE_S, w, lines, 4);
    for (int i = 0; i < n && y < PAGE_HELP_Y - 12; i++, y += 14) PText(lines[i], x, y, TYPE_S, kTextDim);
}

static void DrawBoardColumn(const BoardRow *rows, int n, bool timed, int x, const char *title, const char *empty) {
    PText(title, x, PAGE_ROWS_Y - 18, TYPE_S, kYellow);
    if (n == 0) {
        PText(empty, x, PAGE_ROWS_Y, TYPE_S, kTextDim);
        return;
    }
    for (int r = 0; r < n; r++) {
        int y = PAGE_ROWS_Y + r * PAGE_ROW_PITCH;
        bool you = rows[r].you;
        Color c = you ? kYellow : kText;
        if (you) DrawRectangle(x - 4, y - 4, PAGE_COL_W - 8, 16, kPanel); /* yours, though not a selection: no bar */
        char rank[16], name[24], score[24];
        snprintf(rank, sizeof(rank), "%d", r + 1);
        PTextRight(rank, TYPE_S, x + 16, y, you ? kYellow : kTextDim);
        Fit(name, sizeof(name), rows[r].name, TYPE_S, 104);
        PText(name, x + 28, y, TYPE_S, c);
        Scores_FormatScore(timed, rows[r].score, score, sizeof(score)); /* a Sprint 40 time, not 9 992 760 */
        PTextRight(score, TYPE_S, x + PAGE_COL_W - 12, y, c);
    }
}

/* Under the boards: where you stand in the world, when the boards came
 * down, and how today's daily went. */
static void DrawBoardNotes(const PageScene *s, const Board *bd) {
    int x = PAGE_RIGHT_X, y = PAGE_ROWS_Y + BOARD_TOP * PAGE_ROW_PITCH + 8;
    char line[128];
    if (bd && bd->worldRank == 1) PText("You hold #1 in the world on this board.", x, y, TYPE_S, kYellow);
    else if (bd && bd->worldRank > 1) {
        snprintf(line, sizeof(line), "You are #%d in the world on this board.", bd->worldRank);
        PText(line, x, y, TYPE_S, kText);
    } else if (bd && bd->worldCount > 0) PText("You are not on this world board yet.", x, y, TYPE_S, kTextDim);
    y += 18;
    char ago[48];
    Settings_Ago(s->syncedAt, s->now, ago, sizeof(ago));
    if (!s->sharing) snprintf(line, sizeof(line), "Turn on Share my scores for world boards.");
    else if (s->syncedAt > 0) snprintf(line, sizeof(line), "World boards synced %s.", ago);
    else snprintf(line, sizeof(line), "The world boards have not come down yet.");
    PText(line, x, y, TYPE_S, kTextDim);
    if (!s->dailyGame) return;
    y += 18;
    char today[24];
    GamePage_Today((time_t)s->now, today, sizeof(today));
    int at = Boards_Find(&s->data->boards, today);
    const Board *d = at >= 0 ? &s->data->boards.b[at] : NULL;
    if (!d || d->localCount == 0) snprintf(line, sizeof(line), "Today's daily: not played yet.");
    else snprintf(line, sizeof(line), "Today's daily: your best %ld.", d->local[0].score);
    if (d && d->worldCount > 0) {
        size_t used = strlen(line);
        snprintf(line + used, sizeof(line) - used, " The world's: %ld.", d->world[0].score);
    }
    PText(line, x, y, TYPE_S, kText);
}

static void DrawPageScores(const PageScene *s) {
    const Boards *b = &s->data->boards;
    int at = GamePage_Board(s->page, b);
    if (at < 0) {
        PText("No scores yet. Play a run and its board shows up here.", PAGE_RIGHT_X, PAGE_BODY_Y + 8, TYPE_S, kTextDim);
        DrawBoardNotes(s, NULL);
        return;
    }
    const Board *bd = &b->b[at];
    DrawBoardColumn(bd->local, bd->localCount, bd->timed, PAGE_RIGHT_X, "This computer", "No runs on this board yet.");
    DrawBoardColumn(bd->world, bd->worldCount, bd->timed, PAGE_RIGHT_X + PAGE_COL_W + 16, "World", s->sharing ? "Nothing online yet." : "Not shared.");
    DrawBoardNotes(s, bd);
}

static void DrawPageTrophies(const PageScene *s) {
    const char *slug = s->data->slug;
    int n = Ach_CountForGame(slug), x = PAGE_RIGHT_X;
    if (n == 0) PText("No trophies for this game yet.", x, PAGE_BODY_Y + 8, TYPE_S, kTextDim);
    for (int i = 0; i < n; i++) {
        const AchDef *d = Ach_ForGame(slug, i);
        if (!d) break;
        int y = PAGE_BODY_Y + i * 54;
        char date[12] = "";
        bool have = Ach_IsUnlocked(slug, d->id, date);
        DrawRectangle(x, y, 32, 32, have ? kYellow : kCabinetEdge);
        DrawRectangle(x + 4, y + 4, 24, 24, have ? Mix(kYellow, kInk, 0.15f) : kCabinetDark);
        if (have) { DrawRectangle(x + 10, y + 14, 4, 4, kInk); DrawRectangle(x + 14, y + 18, 4, 4, kInk); DrawRectangle(x + 18, y + 10, 4, 4, kInk); }
        else { DrawRectangle(x + 11, y + 13, 10, 9, kCabinetEdge); DrawRectangle(x + 13, y + 8, 6, 5, kCabinetEdge); }
        const char *title = have ? d->title : "???";
        int tx = x + 44, room = PAGE_RIGHT_X + PAGE_RIGHT_W - tx - (have ? 96 : 0);
        if (PMeasure(title, TYPE_M) <= room) PText(title, tx, y, TYPE_M, have ? kText : kTextDim);
        else PText(title, tx, y + 4, TYPE_S, have ? kText : kTextDim);
        char desc[64];
        Fit(desc, sizeof(desc), d->desc, TYPE_S, PAGE_RIGHT_X + PAGE_RIGHT_W - tx);
        PText(desc, tx, y + 22, TYPE_S, kTextDim);
        if (have) PTextRight(date, TYPE_S, PAGE_RIGHT_X + PAGE_RIGHT_W, y + 4, kTeal);
    }
}

/* Chipforge's Songs, the hall's song: the rows are the view's buttons; under
 * them, how many songs there are and where more come from. */
static void DrawPageHallSongs(const PageScene *s) {
    const GamePageData *d = s->data;
    int count = GamePage_SongCount(d), top = s->page->songs.top;
    int shown = count - top < PAGE_SONGS_VISIBLE ? count - top : PAGE_SONGS_VISIBLE;
    DrawScrollBar(PAGE_RIGHT_X + PAGE_RIGHT_W + 4, PAGE_SONGS_Y, PAGE_SONGS_VISIBLE * PAGE_SONG_PITCH - 4, top, PAGE_SONGS_VISIBLE, count);
    char line[96];
    if (d->libraryCount == 0) snprintf(line, sizeof(line), "No songs of yours yet: Chipforge makes them.");
    else snprintf(line, sizeof(line), "%d song%s in your library. A press gives the hall one.", d->libraryCount, d->libraryCount == 1 ? "" : "s");
    PText(line, PAGE_RIGHT_X, PAGE_SONGS_Y + shown * PAGE_SONG_PITCH + 6, TYPE_S, kTextDim);
}

/* Chipforge's Songs, who plays what: every machine and the song it plays. */
static void DrawPageSongs(const PageScene *s) {
    const GamePageData *d = s->data;
    if (GamePage_SongsShown(s->page, s->view)) {
        DrawPageHallSongs(s);
        return;
    }
    int x = PAGE_RIGHT_X, y = PAGE_SONGS_Y;
    for (int i = 0; i < d->useCount && y < PAGE_HELP_Y - 40; i++, y += 18) {
        char song[48];
        PText(d->uses[i].machine, x, y, TYPE_S, kText);
        Fit(song, sizeof(song), d->uses[i].song, TYPE_S, PAGE_RIGHT_W - 184);
        PText(song, x + 184, y, TYPE_S, d->uses[i].custom ? kTeal : kTextDim);
    }
    PText("Ctrl+G in Chipforge gives a game (or the hall) a song.", x, y + 8, TYPE_S, kTextDim);
}

static void DrawPageCounter(const KitView *kv, int place, int count) {
    const KitItem *prev = Kit_Get(kv->list, WID_PAGE_PREV), *next = Kit_Get(kv->list, WID_PAGE_NEXT);
    if (!prev || !next || count <= 0) return;
    char pos[32];
    snprintf(pos, sizeof(pos), "%d / %d", place + 1, count);
    PTextCentered(pos, TYPE_S, (int)((prev->r.x + prev->r.w + next->r.x) / 2.0f), (int)(prev->r.y + (prev->r.h - TYPE_S) / 2.0f), kText);
}

void Render_GamePage(const PageScene *s, const KitView *kv) {
    ClearBackground(kWallBottom);
    DrawHall(false, KIT_LOOK_NORMAL);
    const KitItem *edit = Kit_Get(kv->list, WID_PAGE_EDIT);
    DrawPageTitle(s->game->name, edit ? (int)edit->r.x - 16 : PAGE_X + 800);
    char desc[128];
    Fit(desc, sizeof(desc), s->game->desc, TYPE_S, 800);
    PText(desc, PAGE_X, PAGE_DESC_Y, TYPE_S, kTextDim);
    DrawPageStrip(s);
    DrawPageScreen(s);
    DrawRectangle(PAGE_RIGHT_X, PAGE_TABS_Y + PAGE_TABS_H, PAGE_RIGHT_W, 2, kBaseboard);
    switch (GamePage_ShownTab(s->page, s->view->tool)) {
        case GT_SCORES: DrawPageScores(s); break;
        case GT_TROPHIES: DrawPageTrophies(s); break;
        case GT_SONGS: DrawPageSongs(s); break;
        default: DrawPageAbout(s); break;
    }
    if (s->help && s->help[0]) PText(s->help, PAGE_X, PAGE_HELP_Y, TYPE_S, kTextDim);
    const HallStatus *st = s->status;
    if (st && st->text[0]) {
        char text[HALL_STATUS_LEN + 4];
        if (st->dots) snprintf(text, sizeof(text), "%s%.*s", st->text, (int)(KitDraw_Clock() * 3.0) % 4, "...");
        else snprintf(text, sizeof(text), "%s", st->text);
        PText(text, PAGE_X, PAGE_STATUS_Y, TYPE_S, ToneOf(st->tone));
    }
    if (s->hint) PText(s->hint, PAGE_X, WINDOW_HEIGHT - 24, TYPE_S, kTextDim);
    KitDraw_List(kv);
    DrawPageCounter(kv, s->view->place, s->view->count);
}

/* A slim bar at the right of a list that is longer than the screen. */
static void DrawScrollBar(int x, int y, int h, int top, int visible, int count) {
    if (count <= visible) return;
    DrawRectangle(x, y, 4, h, kCabinetDark);
    int thumb = h * visible / count;
    if (thumb < 12) thumb = 12;
    int maxTop = count - visible;
    int ty = y + (h - thumb) * top / maxTop;
    DrawRectangle(x, ty, 4, thumb, kCabinetEdge);
}

void Render_Stats(const PlayStats *stats, const Manifest *m, const RunSummary *sums, int top, const KitView *kv) {
    BeginScreen("Stats");

    char totalBuf[32], line[120];
    FormatHMS(stats->totalLauncherSeconds, totalBuf, sizeof(totalBuf));
    int totalRuns = 0, trophies = 0, trophyTotal = 0;
    double gameSeconds = 0.0;
    for (int i = 0; i < m->count; i++) {
        char slug[64];
        gameSeconds += Stats_GetGameTime(stats, m->games[i].name);
        if (Manifest_IsTool(&m->games[i])) continue; /* Chipforge's time counts; it has no runs or trophies */
        totalRuns += sums[i].runs;
        if (Scores_SlugFromExec(m->games[i].exec, slug, sizeof(slug))) {
            trophies += Ach_UnlockedForGame(slug);
            trophyTotal += Ach_CountForGame(slug);
        }
    }
    char gameBuf[32];
    FormatHMS(gameSeconds, gameBuf, sizeof(gameBuf));
    snprintf(line, sizeof(line), "Arcade open %s   Playing %s   %d runs   %d of %d trophies", totalBuf, gameBuf, totalRuns, trophies, trophyTotal);
    PText(line, 80, 108, TYPE_S, kTextDim);

    /* Table header. */
    const int cName = 80, cTime = 400, cRuns = 540, cTro = 640;
    int y = 138;
    PText("Game", cName, y, TYPE_S, kTextDim);
    PTextRight("Time", TYPE_S, cTime + 90, y, kTextDim);
    PTextRight("Runs", TYPE_S, cRuns + 60, y, kTextDim);
    PText("Trophies", cTro, y, TYPE_S, kTextDim);
    PTextRight("Last run", TYPE_S, WINDOW_WIDTH - 80, y, kTextDim);
    DrawRectangle(80, y + 16, WINDOW_WIDTH - 160, 2, kCabinetEdge);
    y = STATS_ROW_Y;

    /* Most-played first. */
    int order[MANIFEST_MAX_GAMES];
    for (int i = 0; i < m->count; i++) order[i] = i;
    for (int i = 0; i < m->count; i++) {
        int best = i;
        for (int j2 = i + 1; j2 < m->count; j2++)
            if (Stats_GetGameTime(stats, m->games[order[j2]].name) > Stats_GetGameTime(stats, m->games[order[best]].name)) best = j2;
        int tmp = order[i]; order[i] = order[best]; order[best] = tmp;
    }
    double longest = m->count > 0 ? Stats_GetGameTime(stats, m->games[order[0]].name) : 0.0;
    if (m->count == 0) PText("No games in the catalog.", 80, y, TYPE_S, kTextDim);
    for (int k = top; k < m->count && k < top + STATS_VISIBLE; k++) {
        int i = order[k];
        const GameEntry *g = &m->games[i];
        double secs = Stats_GetGameTime(stats, g->name);
        char timeBuf[32], nm[40], runs[16], tro[16], slug[64] = "";
        FormatHMS(secs, timeBuf, sizeof(timeBuf));
        snprintf(nm, sizeof(nm), "%.20s", g->name);
        snprintf(runs, sizeof(runs), "%d", sums[i].runs);
        Scores_SlugFromExec(g->exec, slug, sizeof(slug));
        int n = Ach_CountForGame(slug), done = Ach_UnlockedForGame(slug);
        snprintf(tro, sizeof(tro), "%d/%d", done, n);
        PText(nm, cName, y, TYPE_S, k == 0 && secs > 0 ? kYellow : kText);
        /* a slim bar behind the time, on the same scale for every row */
        int bw = longest > 0.0 ? (int)(120.0 * secs / longest) : 0;
        DrawRectangle(cName + 200, y + 3, 110, 4, kCabinetDark);
        DrawRectangle(cName + 200, y + 3, bw * 110 / 120, 4, k == 0 ? kPink : kTeal);
        PTextRight(timeBuf, TYPE_S, cTime + 90, y, kText);
        if (Manifest_IsTool(g)) { /* a tool: no runs, no trophies, nothing to count */
            PTextRight("-", TYPE_S, cRuns + 60, y, kTextDim);
            PText("-", cTro, y, TYPE_S, kTextDim);
            PTextRight("a tool", TYPE_S, WINDOW_WIDTH - 80, y, kTextDim);
            y += STATS_ROW_PITCH;
            continue;
        }
        PTextRight(runs, TYPE_S, cRuns + 60, y, kText);
        PText(tro, cTro, y, TYPE_S, (n > 0 && done == n) ? kYellow : kText);
        PTextRight(sums[i].last[0] ? sums[i].last : "never", TYPE_S, WINDOW_WIDTH - 80, y, sums[i].last[0] ? kTeal : kTextDim);
        y += STATS_ROW_PITCH;
    }
    DrawScrollBar(WINDOW_WIDTH - 64, STATS_ROW_Y, STATS_VISIBLE * STATS_ROW_PITCH - 18, top, STATS_VISIBLE, m->count);

    /* The last two weeks, all games together. */
    int perDay[RUNSTATS_DAYS] = {0}, peak = 1;
    for (int i = 0; i < m->count; i++)
        if (!Manifest_IsTool(&m->games[i])) for (int d = 0; d < RUNSTATS_DAYS; d++) perDay[d] += sums[i].perDay[d];
    for (int d = 0; d < RUNSTATS_DAYS; d++) if (perDay[d] > peak) peak = perDay[d];
    PText("Runs, last two weeks", 80, 500, TYPE_S, kTextDim);
    int baseY = 590, bx = 80, colW = (WINDOW_WIDTH - 160) / RUNSTATS_DAYS;
    for (int d = RUNSTATS_DAYS - 1; d >= 0; d--) {
        int h = perDay[d] * 56 / peak;
        if (perDay[d] > 0 && h < 3) h = 3;
        DrawRectangle(bx + 2, baseY - 56, colW - 6, 56, kCabinetDark);
        DrawRectangle(bx + 2, baseY - h, colW - 6, h, d == 0 ? kPink : kTeal);
        bx += colW;
    }
    PText("14 days ago", 80, baseY + 6, TYPE_S, kTextDim);
    PTextRight("today", TYPE_S, WINDOW_WIDTH - 80, baseY + 6, kTextDim);

    const char *hint = m->count > STATS_VISIBLE ? ForHints(kv, "Up/Down scroll    Esc goes back", "D-pad scroll    B goes back")
                                                : ForHints(kv, "Esc goes back", "B goes back");
    PText(hint, 80, WINDOW_HEIGHT - 26, TYPE_S, kTextDim);
    KitDraw_List(kv);
}

/* The trophy room: every game's achievements, one game at a time. */
void Render_Achievements(const Manifest *m, int selected, int top, const KitView *kv) {
    BeginScreen("Trophies");
    int totalDone = 0, total = 0;
    for (int i = 0; i < m->count; i++) {
        char slug[64];
        if (Manifest_IsTool(&m->games[i]) || !Scores_SlugFromExec(m->games[i].exec, slug, sizeof(slug))) continue;
        total += Ach_CountForGame(slug);
        totalDone += Ach_UnlockedForGame(slug);
    }
    char line[96];
    snprintf(line, sizeof(line), "%d of %d unlocked across the arcade", totalDone, total);
    PText(line, 80, 116, TYPE_S, kTextDim);
    if (m->count == 0) {
        PText("No games in the catalog.", 80, 164, TYPE_S, kTextDim);
        PText(ForHints(kv, "Esc goes back", "B goes back"), 80, WINDOW_HEIGHT - 48, TYPE_S, kTextDim);
        KitDraw_List(kv);
        return;
    }
    if (selected < 0) selected = 0;
    if (selected >= m->count) selected = m->count - 1;

    const GameEntry *g = &m->games[selected];
    char slug[64] = "";
    Scores_SlugFromExec(g->exec, slug, sizeof(slug));
    int n = Ach_CountForGame(slug), done = Ach_UnlockedForGame(slug);

    /* "< Game >" is the stepper button (screens.c); its count and progress bar */
    snprintf(line, sizeof(line), "%d / %d", done, n);
    PTextRight(line, TYPE_M, WINDOW_WIDTH - 80, 156, kText);
    int barW = WINDOW_WIDTH - 160;
    DrawRectangle(80, 186, barW, 6, kCabinetDark);
    if (n > 0) DrawRectangle(80, 186, (int)((float)barW * (float)done / (float)n), 6, kPink);

    int y = TROPHY_ROW_Y;
    if (n == 0 && Manifest_IsTool(g)) PText("A tool, not a game: it has no trophies.", 80, y, TYPE_S, kTextDim);
    else if (n == 0) PText("No achievements for this game yet.", 80, y, TYPE_S, kTextDim);
    for (int i = top; i < n && i < top + TROPHY_VISIBLE; i++) {
        const AchDef *d = Ach_ForGame(slug, i);
        if (!d) break;
        char date[12] = "";
        bool have = Ach_IsUnlocked(slug, d->id, date);
        Color box = have ? kYellow : kCabinetEdge;
        DrawRectangle(80, y, 36, 36, box);
        DrawRectangle(84, y + 4, 28, 28, have ? Mix(kYellow, kInk, 0.15f) : kCabinetDark);
        if (have) { DrawRectangle(92, y + 16, 6, 6, kInk); DrawRectangle(98, y + 20, 6, 6, kInk); DrawRectangle(104, y + 10, 6, 6, kInk); }
        else { DrawRectangle(92, y + 14, 12, 10, kCabinetEdge); DrawRectangle(94, y + 8, 8, 6, kCabinetEdge); }
        PText(have ? d->title : "???", 132, y + 2, TYPE_M, have ? kText : kTextDim);
        PText(d->desc, 132, y + 24, TYPE_S, kTextDim);
        if (have) PTextRight(date, TYPE_S, WINDOW_WIDTH - 80, y + 6, kTeal);
        y += TROPHY_ROW_PITCH;
    }
    DrawScrollBar(WINDOW_WIDTH - 64, TROPHY_ROW_Y, TROPHY_VISIBLE * TROPHY_ROW_PITCH - 20, top, TROPHY_VISIBLE, n);
    const char *hint = n > TROPHY_VISIBLE
                           ? ForHints(kv, "Left/Right change game    Up/Down scroll    Esc goes back",
                                      "D-pad left/right change game    Up/down scroll    B goes back")
                           : ForHints(kv, "Left/Right change game    Esc goes back", "D-pad change game    B goes back");
    PText(hint, 80, WINDOW_HEIGHT - 48, TYPE_S, kTextDim);
    KitDraw_List(kv);
}

static void UpdateHeadline(const UpdateState *u, int y) {
    if (u->status == UPDATE_BEHIND) PText("A newer version is on GitHub.", 80, y, TYPE_M, kYellow);
    else if (u->status == UPDATE_CURRENT) PText("You have the newest version.", 80, y, TYPE_M, kTeal);
    else if (u->checkedAt > 0) PText("GitHub does not know this build.", 80, y, TYPE_M, kWarmWhite); /* it answered */
    else PText("No answer yet.", 80, y, TYPE_M, kWarmWhite);
}

static void CheckedAgo(long long checkedAt, char *out, size_t size) { Settings_Ago(checkedAt, (long long)time(NULL), out, size); }

/* Beside Check now: why it cannot, or that the last press got no answer.
 * While it checks, the button itself says so. */
static void CheckNowNote(const UpdateView *v, const KitView *kv) {
    const KitItem *b = Kit_Get(kv->list, WID_CHECK_NOW);
    if (!b) return;
    int x = (int)(b->r.x + b->r.w) + 24, y = (int)(b->r.y + b->r.h / 2) - 4;
    if (v->checksOff) PText("Update checks are off in Settings > Online.", x, y, TYPE_S, kTextDim);
    else if (v->noAnswer && !v->checking) PText("No answer from GitHub. Try again later.", x, y, TYPE_S, kCoinRed);
}

void Render_Update(const UpdateState *u, const char *sourceDir, const UpdateView *v, const KitView *kv) {
    BeginScreen("Updates");
    char line[200];
    int y = 120;
    UpdateHeadline(u, y);
    y += 44;

    PText("This build", 80, y, TYPE_S, kTextDim);
    snprintf(line, sizeof(line), "%.7s", u->current[0] ? u->current : "unknown");
    PText(line, 300, y, TYPE_S, kText);
    y += 26;
    PText("Newest on GitHub", 80, y, TYPE_S, kTextDim);
    snprintf(line, sizeof(line), "%.7s", u->latest[0] ? u->latest : "unknown");
    PText(line, 300, y, TYPE_S, u->status == UPDATE_BEHIND ? kYellow : kText);
    if (u->status == UPDATE_BEHIND) {
        char more[48];
        snprintf(more, sizeof(more), u->behind == 1 ? "1 change newer" : "%d changes newer", u->behind);
        PText(more, 300 + PMeasure(line, TYPE_S) + 24, y, TYPE_S, kTextDim);
    }
    y += 26;
    if (u->title[0]) {
        PText("Latest change", 80, y, TYPE_S, kTextDim);
        snprintf(line, sizeof(line), "%.52s", u->title);
        PText(line, 300, y, TYPE_S, kText);
        y += 26;
    }
    PText("Last checked", 80, y, TYPE_S, kTextDim);
    CheckedAgo(u->checkedAt, line, sizeof(line));
    PText(line, 300, y, TYPE_S, kText);
    y += 44;

    if (u->status == UPDATE_BEHIND) {
        PText("To update, run this in a terminal:", 80, y, TYPE_S, kTextDim);
        y += 26;
        snprintf(line, sizeof(line), "cd %.80s", (sourceDir && sourceDir[0]) ? sourceDir : "~/Work/ghost-arcade");
        DrawRectangle(80, y - 8, WINDOW_WIDTH - 160, 58, kCabinetDark);
        PText(line, 96, y, TYPE_S, kTeal);
        PText("./install.sh --update", 96, y + 24, TYPE_S, kTeal);
        y += 72;
        PText("Nothing is downloaded or installed until you do that.", 80, y, TYPE_S, kTextDim);
    } else if (u->status == UPDATE_UNKNOWN && u->checkedAt > 0) {
        PText("It is not among the newest changes on GitHub: it was made from", 80, y, TYPE_S, kTextDim); y += 22;
        PText("your own work in progress, or it is very old.", 80, y, TYPE_S, kTextDim);
    } else if (u->status == UPDATE_UNKNOWN) {
        PText("Either GitHub has not been reached yet, or this build was made", 80, y, TYPE_S, kTextDim); y += 22;
        PText("from changes that are not on GitHub (your own work in progress).", 80, y, TYPE_S, kTextDim);
    }

    CheckNowNote(v, kv);
    PText("The check is one anonymous request to GitHub, at most every six hours.", 80, WINDOW_HEIGHT - 96, TYPE_S, kTextDim);
    PText("Settings > Online turns it off.", 80, WINDOW_HEIGHT - 74, TYPE_S, kTextDim);
    /* R does nothing while checks are off or one is under way: say so by leaving it out. */
    const char *hint = (v->checksOff || v->checking) ? ForHints(kv, "Esc goes back", "B goes back")
                                                     : ForHints(kv, "R checks again now    Esc goes back", "View checks again now    B goes back");
    PText(hint, 80, WINDOW_HEIGHT - 48, TYPE_S, kTextDim);
    KitDraw_List(kv);
}

void Render_OnlineAsk(bool firstTime, bool sharingNow, const KitView *kv) {
    BeginScreen("Share scores online?");
    int y = 120;
    PText("Yes: your scores go to the Ghost Arcade leaderboard, so", 80, y, TYPE_S, kText); y += 22;
    PText("other people can see them and you can see theirs.", 80, y, TYPE_S, kText); y += 40;
    PText("What is sent", 80, y, TYPE_S, kYellow); y += 24;
    PText("Your arcade name, the game, the mode, the score and the", 80, y, TYPE_S, kTextDim); y += 20;
    PText("date, plus a random ID made on this computer so repeats", 80, y, TYPE_S, kTextDim); y += 20;
    PText("are not counted twice. No account, nothing else.", 80, y, TYPE_S, kTextDim); y += 40;
    PText("No: nothing leaves this machine. Local scores still work.", 80, y, TYPE_S, kText);
    /* The old screen lit the saved answer; a pointer user, who sees no focus,
     * is told in words instead. */
    if (!firstTime) {
        const KitItem *no = Kit_Get(kv->list, WID_ONLINE_NO);
        int ny = no ? (int)(no->r.y + no->r.h) + 20 : y + 128;
        PText(sharingNow ? "Right now your scores are shared." : "Right now your scores stay on this computer.", 80, ny, TYPE_S, kTextDim);
    }
    const char *hint = firstTime ? ForHints(kv, "Left/Right choose    Enter confirms    Esc means no", "D-pad choose    A confirms    B means no")
                                 : ForHints(kv, "Left/Right choose    Enter saves    Esc leaves it as it was",
                                            "D-pad choose    A saves    B leaves it as it was");
    PText(hint, 80, WINDOW_HEIGHT - 48, TYPE_S, kTextDim);
    KitDraw_List(kv);
}

/* ---------------------------------------------------------- daily desk */

/* A cell of the strip, its value cut before the next cell (a machine the
 * admin gave a long name). */
static void DeskCell(int x, int right, const char *label, const char *value, Color valueColour, const char *detail, Color detailColour) {
    char fit[64];
    Fit(fit, sizeof(fit), value, TYPE_M, right - x);
    PText(label, x, DESK_STRIP_Y, TYPE_S, kTextDim);
    PText(fit, x, DESK_STRIP_Y + 14, TYPE_M, valueColour);
    if (detail && detail[0]) PText(detail, x, DESK_STRIP_Y + 36, TYPE_S, detailColour);
}

static void DayWords(int n, char *out, size_t size) { snprintf(out, size, "%d day%s", n, n == 1 ? "" : "s"); }

/* The streak, the best one, today's featured game and the last fourteen
 * days as dots (filled when a daily was played; today ringed). */
static void DrawDeskStrip(const Desk *d) {
    char value[32];
    DayWords(d->streak.current, value, sizeof(value));
    const char *how = d->streak.today ? "Played today" : (d->streak.current > 0 ? "Keep it going today" : "One today starts it");
    DeskCell(DESK_X, DESK_CELL_BEST - 16, "Streak", value, d->streak.current > 0 ? kYellow : kTextDim, how, d->streak.today ? kTeal : kTextDim);
    DayWords(d->streak.best, value, sizeof(value));
    DeskCell(DESK_CELL_BEST, DESK_CELL_FEATURED - 16, "Best streak", value, d->streak.best > 0 ? kText : kTextDim, NULL, kTextDim);
    const char *slug = Desk_GameSlug(d->featured);
    int at = -1;
    for (int r = 0; r < d->count; r++) if (strcmp(d->rows[r].slug, slug) == 0) at = r;
    char name[24];
    snprintf(name, sizeof(name), "%c%.20s", slug[0] ? (char)(slug[0] - 32) : ' ', slug[0] ? slug + 1 : "");
    DeskCell(DESK_CELL_FEATURED, DESK_DOT_X - 16, "Featured today", at >= 0 ? d->rows[at].name : name, at >= 0 ? kWarmWhite : kTextDim, at >= 0 ? "Starred below" : "Not on your row",
             kTextDim);
    PText("Last 14 days", DESK_DOT_X, DESK_STRIP_Y, TYPE_S, kTextDim);
    static const char kInitial[7] = {'M', 'T', 'W', 'T', 'F', 'S', 'S'};
    for (int k = 0; k < DESK_DOTS; k++) {
        int x = DESK_DOT_X + k * DESK_DOT_PITCH, y = DESK_STRIP_Y + 16;
        bool today = k == DESK_DOTS - 1;
        if (today) DrawRectangleLinesEx((Rectangle){(float)x - 2, (float)y - 2, DESK_DOT_SIZE + 4, DESK_DOT_SIZE + 4}, 2.0f, kPink);
        if (d->dots[k]) DrawRectangle(x, y, DESK_DOT_SIZE, DESK_DOT_SIZE, kYellow);
        else {
            DrawRectangle(x, y, DESK_DOT_SIZE, DESK_DOT_SIZE, kCabinetDark);
            DrawRectangleLinesEx((Rectangle){(float)x, (float)y, DESK_DOT_SIZE, DESK_DOT_SIZE}, 1.0f, kCabinetEdge);
        }
        char letter[2] = {kInitial[Desk_Weekday(d->today - (DESK_DOTS - 1) + k)], '\0'};
        PTextCentered(letter, TYPE_S, x + DESK_DOT_SIZE / 2 + 1, y + DESK_DOT_SIZE + 6, today ? kPink : kTextDim);
    }
}

/* A number in its column: 16 px, or 8 px when it would reach the next. */
static void DeskNumber(const char *text, int x, int y, int room, Color c) {
    if (PMeasure(text, TYPE_M) <= room) PText(text, x, y, TYPE_M, c);
    else PText(text, x, y + 4, TYPE_S, c);
}

/* A chalk tick: two strokes. */
static void DrawTick(int x, int y, Color c) {
    for (int i = 0; i < 4; i++) DrawRectangle(x + i * 2, y + 4 + i * 2, 2, 2, c);
    for (int i = 0; i < 6; i++) DrawRectangle(x + 8 + i * 2, y + 10 - i * 2, 2, 2, c);
}

/* One game's row: lit as the house lights a row (panel and a pink bar),
 * its columns in their places. */
static void DrawDeskRow(const DeskRow *r, KitRect rect, KitLook look) {
    int x = (int)rect.x, y = (int)rect.y, w = (int)rect.w, h = (int)rect.h;
    Color name = kText, value = kText, dim = kTextDim;
    int sink = 0;
    switch (look) {
        case KIT_LOOK_HOVER: DrawRectangle(x, y, w, h, Pal_Mix(kPanel, kCabinet, 0.5f)); name = kWarmWhite; break;
        case KIT_LOOK_FOCUSED: DrawRectangle(x, y, w, h, kPanel); DrawRectangle(x, y, 4, h, kPink); name = kPink; break;
        case KIT_LOOK_PRESSED: DrawRectangle(x, y, w, h, kPink); name = value = dim = kInk; sink = 2; break;
        case KIT_LOOK_DISABLED: name = value = dim = kFaint; break;
        default: break;
    }
    if (r->featured) KitDraw_Star(x + 8, y + 6 + sink, 2, true, look == KIT_LOOK_PRESSED ? kInk : kYellow);
    char text[48];
    Fit(text, sizeof(text), r->name, TYPE_M, DESK_COL_TODAY - (x + 32) - 12);
    PText(text, x + 32, y + 6 + sink, TYPE_M, name);
    if (!r->installed) PText("Not installed", x + 32, y + 26 + sink, TYPE_S, dim);
    else if (r->featured) PText("Featured today", x + 32, y + 26 + sink, TYPE_S, look == KIT_LOOK_PRESSED ? kInk : kYellow);
    else if (r->runsToday > 1) {
        snprintf(text, sizeof(text), "%d runs today", r->runsToday);
        PText(text, x + 32, y + 26 + sink, TYPE_S, dim);
    }
    if (r->played) {
        DrawTick(DESK_COL_TODAY, y + 8 + sink, look == KIT_LOOK_PRESSED ? kInk : kTeal);
        PText("Played", DESK_COL_TODAY + 24, y + 12 + sink, TYPE_S, look == KIT_LOOK_PRESSED ? kInk : kTeal);
    } else {
        PText("Not yet", DESK_COL_TODAY, y + 12 + sink, TYPE_S, dim);
    }
    if (r->hasBest) {
        snprintf(text, sizeof(text), "%ld", r->best);
        DeskNumber(text, DESK_COL_BEST, y + 6 + sink, DESK_COL_WORLD - DESK_COL_BEST - 12, value);
    } else PText("-", DESK_COL_BEST, y + 6 + sink, TYPE_M, dim);
    if (r->hasWorld) {
        snprintf(text, sizeof(text), "%ld", r->world);
        DeskNumber(text, DESK_COL_WORLD, y + 6 + sink, DESK_COL_RANK - DESK_COL_WORLD - 12, value);
        snprintf(text, sizeof(text), "%.16s", r->worldName);
        PText(text, DESK_COL_WORLD, y + 26 + sink, TYPE_S, dim);
    } else PText("-", DESK_COL_WORLD, y + 6 + sink, TYPE_M, dim);
    if (r->rank > 0) {
        snprintf(text, sizeof(text), "#%d", r->rank);
        PText(text, DESK_COL_RANK, y + 6 + sink, TYPE_M, r->rank == 1 && look != KIT_LOOK_PRESSED ? kYellow : value);
    } else PText("-", DESK_COL_RANK, y + 6 + sink, TYPE_M, dim);
}

static void DrawDeskNote(const DeskScene *s) {
    char ago[48], line[96];
    Settings_Ago(s->syncedAt, s->now, ago, sizeof(ago));
    if (!s->sharing) snprintf(line, sizeof(line), "Turn on Share my scores for the world's best and your rank.");
    else if (s->syncedAt > 0) snprintf(line, sizeof(line), "World boards synced %s.", ago);
    else snprintf(line, sizeof(line), "The world boards have not come down yet.");
    PText(line, DESK_X, DESK_NOTE_Y, TYPE_S, kTextDim);
}

void Render_Desk(const DeskScene *s, const KitView *kv) {
    const Desk *d = s->desk;
    BeginScreen("Daily challenge");
    char date[48];
    Desk_DateWords(d->today, date, sizeof(date));
    PTextRight(date, TYPE_M, DESK_RIGHT, 62, kText);
    PText("Every daily game plays the same seed for everyone today, on a board of its own.", DESK_X, DESK_INTRO_Y, TYPE_S, kTextDim);
    DrawDeskStrip(d);
    if (d->count == 0) {
        PTextCentered("No daily games on your row", TYPE_M, WINDOW_WIDTH / 2, 300, kWarmWhite);
        PTextCentered("Settings > Machines shows hidden machines", TYPE_S, WINDOW_WIDTH / 2, 332, kTextDim);
    } else {
        static const struct { int x; const char *label; } kHead[] = {
            {DESK_X + 32, "Game"}, {DESK_COL_TODAY, "Today"}, {DESK_COL_BEST, "Your best"}, {DESK_COL_WORLD, "World best"}, {DESK_COL_RANK, "Your rank"}};
        for (size_t i = 0; i < sizeof(kHead) / sizeof(kHead[0]); i++) PText(kHead[i].label, kHead[i].x, DESK_HEAD_Y, TYPE_S, kTextDim);
        for (int i = 0; i < kv->list->count; i++) {
            const KitItem *it = &kv->list->items[i];
            int r = Desk_RowOf(it->id);
            if (r >= 0 && r < d->count) DrawDeskRow(&d->rows[r], it->r, KitDraw_LookOf(kv, it));
        }
        DrawScrollBar(DESK_RIGHT + 4, DESK_ROW_Y, DESK_VISIBLE * DESK_ROW_PITCH - 4, s->top, DESK_VISIBLE, d->count);
        DrawDeskNote(s);
    }
    if (s->help && s->help[0]) PText(s->help, DESK_X, DESK_HELP_Y, TYPE_S, kTextDim);
    if (s->hint) PText(s->hint, DESK_X, WINDOW_HEIGHT - 24, TYPE_S, kTextDim);
    KitDraw_List(kv);
}

/* --------------------------------------------------------------- settings */

static const Color kVeil = {6, 4, 14, 255}; /* what dims a page under a menu, a box or the name board */

static Color ToneColor(NoteTone t) {
    switch (t) {
        case NOTE_TEXT: return kText;
        case NOTE_GOOD: return kTeal;
        case NOTE_HOT: return kYellow;
        case NOTE_BAD: return kCoinRed;
        default: return kTextDim;
    }
}

static bool Typed(const SettingsRow *r, const Settings *s) {
    return r->field && s->field.editing && s->fieldFor == r->nameIndex;
}

/* The row the keys are on (or the one being typed in): panel and bar. The
 * mouse lights controls, not rows. */
static const SettingsRow *LitRow(const SettingsPage *p, const Settings *s, const KitView *kv) {
    for (int i = 0; i < p->rowCount; i++) if (Typed(&p->rows[i], s)) return &p->rows[i];
    if (!kv->state || kv->device == DEV_MOUSE) return NULL;
    return Settings_RowOf(p, kv->state->focus);
}

static void DrawRowText(const SettingsRow *r, bool lit) {
    int cy = (int)(r->r.y + r->r.h / 2.0f);
    if (r->kind == SROW_HEADING) {
        PText(r->label, SET_LABEL_X, (int)r->r.y + 4, TYPE_S, kYellow);
        return;
    }
    if (lit) {
        DrawRectangle((int)r->r.x, (int)r->r.y, (int)r->r.w, (int)r->r.h, kPanel);
        DrawRectangle((int)r->r.x, (int)r->r.y, 4, (int)r->r.h, kPink);
    }
    int labelY = r->sub[0] ? (int)r->r.y + 1 : cy - 8; /* a machine's status goes under its name */
    if (r->star) KitDraw_Star(SET_LABEL_X, labelY, 2, true, kYellow); /* 16 px: never mistaken for the shared rows' teal * */
    if (r->label[0]) {
        Color c = r->kind == SROW_INFO ? kTextDim : (lit ? kPink : (r->dim ? kTextDim : kText));
        int x = SET_LABEL_X + r->labelPad;
        PText(r->label, x, labelY, TYPE_M, c);
        if (r->shared) PText("*", x + PMeasure(r->label, TYPE_M) + 6, labelY, TYPE_M, kTeal);
    }
    if (r->sub[0]) PText(r->sub, SET_LABEL_X + r->labelPad, (int)r->r.y + 22, TYPE_S, ToneColor(r->subTone));
    bool two = r->note[1][0] != '\0';
    for (int l = 0; l < 2; l++) {
        if (!r->note[l][0]) continue;
        int y = two ? cy - 11 + l * 14 : cy - 4;
        PText(r->note[l], r->noteX, y, TYPE_S, ToneColor(r->tone[l]));
    }
}

/* The cursor sits in the gap before character `cursor`, between glyphs. */
static void DrawCursor(int textX, int y, const TextField *f) {
    if (!CursorShows(f)) return;
    DrawRectangle(textX + (f->cursor - f->view) * SET_CHAR_W - 2, y - 2, 2, 20, kYellow);
}

static void DrawTyping(KitRect b, const TextField *f) {
    DrawRectangle((int)b.x, (int)b.y, (int)b.w, (int)b.h, kPanel);
    DrawRectangleLinesEx((Rectangle){b.x, b.y, b.w, b.h}, 2.0f, kYellow);
    int textX = (int)b.x + SET_FIELD_PAD, y = (int)(b.y + (b.h - 16.0f) / 2.0f);
    char shown[TEXTFIELD_CAP];
    snprintf(shown, sizeof(shown), "%.*s", Settings_FieldChars(true), f->text + f->view);
    PText(shown, textX, y, TYPE_M, kWarmWhite);
    DrawCursor(textX, y, f);
    char count[24];
    int len = TextField_Length(f);
    snprintf(count, sizeof(count), "%d / %d", len, f->limit);
    Color c = len > f->limit ? kCoinRed : (len == f->limit ? kYellow : kTextDim);
    PTextRight(count, TYPE_S, (int)(b.x + b.w) - SET_FIELD_PAD, (int)(b.y + b.h / 2.0f) - 4, c);
}

/* A name at rest: the name, or a faint hint where there is none. */
static void DrawNameAtRest(KitRect b, const SettingsRow *r, KitLook look) {
    bool hover = look == KIT_LOOK_HOVER, pressed = look == KIT_LOOK_PRESSED;
    DrawRectangle((int)b.x, (int)b.y, (int)b.w, (int)b.h, hover ? Mix(kCabinetDark, kCabinet, 0.5f) : kCabinetDark);
    DrawRectangleLinesEx((Rectangle){b.x, b.y, b.w, b.h}, 1.0f, hover || pressed ? kText : kCabinetEdge);
    int textX = (int)b.x + SET_FIELD_PAD, cy = (int)(b.y + b.h / 2.0f);
    if (r->placeholder) {
        PText(r->value, textX, cy - 4, TYPE_S, r->nameIndex == SET_ARCADE_NAME ? kCoinRed : kTextDim);
        return;
    }
    int room = Settings_FieldChars(false), len = (int)strlen(r->value);
    char shown[TEXTFIELD_CAP];
    if (len > room) snprintf(shown, sizeof(shown), "%.*s...", room - 3, r->value); /* the whole name is one Edit away */
    else snprintf(shown, sizeof(shown), "%s", r->value);
    PText(shown, textX, cy - 8, TYPE_M, r->nameIndex == SET_ARCADE_NAME ? kYellow : kText);
}

static void DrawNameField(const SettingsRow *r, const Settings *s, const KitView *kv) {
    const KitItem *box = Kit_Get(kv->list, r->field);
    if (!box) return;
    if (Typed(r, s)) DrawTyping(box->r, &s->field);
    else DrawNameAtRest(box->r, r, KitDraw_LookOf(kv, box));
}

/* A row with one control shows its focus on the row; only a row of several
 * marks which of them has it. */
static void DrawSettingsControls(const SettingsPage *p, const Settings *s, const SettingsRow *lit, const KitView *kv) {
    for (int i = 0; i < kv->list->count; i++) {
        const KitItem *it = &kv->list->items[i];
        if (it->kind == KIT_AREA || (it->flags & KIT_SELFDRAWN)) continue;
        KitLook look = KitDraw_LookOf(kv, it);
        const SettingsRow *row = Settings_RowOf(p, it->id);
        /* A heading's button (Add a machine) has no lit row behind it to show it. */
        if (look == KIT_LOOK_FOCUSED && row && row == lit && row->controlCount == 1 && row->kind == SROW_CONTROL) look = KIT_LOOK_NORMAL;
        look = Settings_TypingLook(s, look, kv->device);
        KitDraw_Item(it, look, Kit_HoverCell(kv->state, it, kv->device), Kit_PressedCell(kv->state, it));
    }
}

static void DrawKeyCard(const SettingsPage *p) {
    int y = SET_CARD_Y;
    PText("What", SET_CARD_WHAT_X, y, TYPE_S, kYellow);
    PText("Keyboard", SET_CARD_KEYS_X, y, TYPE_S, kYellow);
    PText("Xbox pad", SET_CARD_PAD_X, y, TYPE_S, kYellow);
    PText("Mouse", SET_CARD_MOUSE_X, y, TYPE_S, kYellow);
    DrawRectangle(SET_CARD_WHAT_X, y + 14, SET_CARD_END_X - SET_CARD_WHAT_X, 2, kCabinetEdge);
    y += 24;
    for (int i = 0; i < p->cardLines; i++, y += SET_CARD_PITCH) {
        const KeymapLine *l = &p->lines[i];
        PText(l->what, SET_CARD_WHAT_X, y, TYPE_S, kText);
        PText(l->keys[0] ? l->keys : "-", SET_CARD_KEYS_X, y, TYPE_S, l->keys[0] ? kTeal : kFaint);
        PText(l->pad[0] ? l->pad : "-", SET_CARD_PAD_X, y, TYPE_S, l->pad[0] ? kTeal : kFaint);
        if (l->mouse[0]) PText(l->mouse, SET_CARD_MOUSE_X, y, TYPE_S, kTeal);
    }
    /* Under the table, not in the help line: the keys and the pad keep the
     * stepper chosen, so the help line always holds its sentence. */
    if (p->cardNote && p->cardNote[0]) PText(p->cardNote, SET_CARD_WHAT_X, y + 8, TYPE_S, kTextDim);
}

/* The sentence for the chosen row, or why a control cannot be used. */
static void DrawHelp(const SettingsPage *p, const Settings *s, const SettingsRow *lit, const KitView *kv) {
    if (s->field.editing && s->board) return; /* the board and the hint line say it all */
    const char *text = NULL;
    Color c = kTextDim;
    if (kv->state && kv->device == DEV_MOUSE) {
        const char *why = Kit_Reason(kv->state, kv->list, kv->device);
        const SettingsRow *hovered = Settings_RowOf(p, kv->state->hover);
        if (why[0]) { text = why; c = kTextDim; }
        else if (hovered) text = hovered->help;
    } else if (kv->state) {
        const KitItem *f = Kit_Get(kv->list, kv->state->focus);
        if (f && (f->flags & KIT_DISABLED)) text = f->reason;
        else if (lit) text = lit->help;
    }
    if (text && text[0]) PText(text, SET_LABEL_X, SET_HELP_Y, TYPE_S, c);
}

static void DrawFootnote(const char *text) {
    if (!text || !text[0]) return;
    if (text[0] == '*') {
        PText("*", SET_LABEL_X, SET_FOOT_Y, TYPE_S, kTeal);
        PText(text + 1, SET_LABEL_X + PMeasure("*", TYPE_S), SET_FOOT_Y, TYPE_S, kTextDim);
    } else {
        PText(text, SET_LABEL_X, SET_FOOT_Y, TYPE_S, kTextDim);
    }
}

/* The hint line, named from the keymap; after pad use, the pad's buttons.
 * Left/Right are named only where they do something. */
static void DrawSettingsHint(const SettingsPage *p, const Settings *s, const KitView *kv) {
    InputDevice device = kv->hints;
    char a[24], b[24], c[24], d[24], e[24], line[256];
    if (s->field.editing && s->board && device == DEV_PAD) {
        char f[24];
        Keymap_Hint(KA_BOARD_TYPE, DEV_PAD, a, sizeof(a));
        Keymap_Hint(KA_BOARD_DELETE, DEV_PAD, b, sizeof(b));
        Keymap_Hint(KA_BOARD_SPACE, DEV_PAD, c, sizeof(c));
        Keymap_Hint(KA_BOARD_DONE, DEV_PAD, d, sizeof(d));
        Keymap_Hint(KA_TEXT_CANCEL, DEV_PAD, f, sizeof(f));
        snprintf(line, sizeof(line), "D-pad choose    %s types    %s deletes    %s space    %s saves    %s puts it back", a, b, c, d, f);
    } else if (s->field.editing) {
        Keymap_Hint(KA_TEXT_SAVE, DEV_KEYBOARD, a, sizeof(a));
        Keymap_Hint(KA_TEXT_CANCEL, DEV_KEYBOARD, b, sizeof(b));
        Keymap_Hint(KA_TEXT_PASTE, DEV_KEYBOARD, c, sizeof(c));
        snprintf(line, sizeof(line), "Type a name    %s saves    %s puts it back    %s pastes", a, b, c);
    } else if (device == DEV_PAD) {
        Keymap_Hint(KA_UI_PRESS, DEV_PAD, a, sizeof(a));
        Keymap_Hint(KA_UI_PREV_TAB, DEV_PAD, b, sizeof(b));
        Keymap_Hint(KA_UI_NEXT_TAB, DEV_PAD, c, sizeof(c));
        Keymap_Hint(KA_BACK, DEV_PAD, d, sizeof(d));
        snprintf(line, sizeof(line), "D-pad choose    %s press    %s/%s tabs    %s back", a, b, c, d);
    } else {
        char f[24], g[24], side[64] = "";
        Keymap_Hint(KA_UI_UP, DEV_KEYBOARD, a, sizeof(a));
        Keymap_Hint(KA_UI_DOWN, DEV_KEYBOARD, b, sizeof(b));
        Keymap_Hint(KA_UI_LEFT, DEV_KEYBOARD, f, sizeof(f));
        Keymap_Hint(KA_UI_RIGHT, DEV_KEYBOARD, g, sizeof(g));
        Keymap_Hint(KA_UI_PRESS, DEV_KEYBOARD, c, sizeof(c));
        Keymap_Hint(KA_UI_NEXT_TAB, DEV_KEYBOARD, d, sizeof(d));
        Keymap_Hint(KA_BACK, DEV_KEYBOARD, e, sizeof(e));
        const char *what = kv->state ? Settings_SideKeys(p, kv->state->focus) : NULL;
        if (what) snprintf(side, sizeof(side), "%s/%s %s    ", f, g, what);
        snprintf(line, sizeof(line), "%s/%s choose    %s%s press    %s next tab    %s back", a, b, side, c, d, e);
    }
    PText(line, SET_LABEL_X, WINDOW_HEIGHT - 24, TYPE_S, kTextDim);
}

/* The pad's name board over the page: every cell a block in the kit's
 * looks; the pad's cursor is the house focus (panel and a 4 px bar), shown
 * while the pad is in use, since the keyboard types without it. */
static void DrawNameBoard(const SettingsPage *p, const Settings *s, const SettingsRow *typed, const KitView *kv) {
    KitList layer;
    Settings_EditLayer(p, s, &layer);
    KitRect b = Settings_BoardPanel(p, s);
    /* The rows round it dim, as the hall does under the menu, so the buttons
     * it half covers read as behind it; the name being typed stays lit. */
    int top = SET_TABS_Y + SET_TABS_H + 2, rowY = typed ? (int)typed->r.y : NB_BOTTOM, rowEnd = typed ? (int)(typed->r.y + typed->r.h) : NB_BOTTOM;
    DrawRectangle(0, top, WINDOW_WIDTH, rowY - top, Fade(kVeil, 0.55f));
    DrawRectangle(0, rowEnd, WINDOW_WIDTH, NB_BOTTOM - rowEnd, Fade(kVeil, 0.55f));
    DrawRectangle((int)b.x, (int)b.y, (int)b.w, (int)b.h, (Color){12, 9, 28, 250});
    DrawRectangleLinesEx((Rectangle){b.x, b.y, b.w, b.h}, 2.0f, kBaseboard);
    DrawRectangle((int)b.x, (int)b.y, (int)b.w, 4, kPink);
    for (int i = 0; i < NameBoard_Count(); i++) {
        const KitItem *cell = Kit_Get(&layer, WID_SET_BOARD + i);
        if (!cell) continue;
        KitItem it = *cell;
        it.style = KIT_STYLE_BLOCK;
        it.textSize = Kit_TextWidth(it.label, 16) <= it.r.w - 8 ? 16 : 8;
        KitLook look = kv->state ? Kit_Look(kv->state, &it, kv->device) : KIT_LOOK_NORMAL;
        if (look == KIT_LOOK_NORMAL && kv->device == DEV_PAD && i == s->boardPos.cell) look = KIT_LOOK_FOCUSED;
        KitDraw_Item(&it, look, KIT_NONE, KIT_NONE);
    }
}

void Render_Settings(const SettingsPage *p, const Settings *s, const KitView *kv) {
    BeginScreen("Settings");
    DrawRectangle(40, SET_TABS_Y + SET_TABS_H, WINDOW_WIDTH - 80, 2, kBaseboard);
    if (p->intro) PText(p->intro, SET_LABEL_X, SET_INTRO_Y, TYPE_S, kTextDim);
    const SettingsRow *lit = LitRow(p, s, kv);
    for (int i = 0; i < p->rowCount; i++) DrawRowText(&p->rows[i], &p->rows[i] == lit);
    for (int i = 0; i < p->rowCount; i++) if (p->rows[i].field) DrawNameField(&p->rows[i], s, kv);
    DrawSettingsControls(p, s, lit, kv);
    const SettingsTable *t = &p->table;
    if (t->shown) DrawScrollBar(SET_ROW_X + SET_ROW_W + 8, (int)t->y, (int)((float)t->visible * t->pitch) - 4, t->top, t->visible, t->count);
    if (p->cardLines > 0) DrawKeyCard(p);
    DrawHelp(p, s, lit, kv);
    DrawFootnote(p->footnote);
    if (s->field.editing && s->board) DrawNameBoard(p, s, lit, kv);
    DrawSettingsHint(p, s, kv);
}

/* ------------------------------------------------------------- overlays */

void Render_ArcadeMenu(const KitView *kv) {
    DrawRectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, Fade(kVeil, 0.55f)); /* the hall dims but keeps moving */
    DrawRectangle(0, 0, MENU_PANEL_W, WINDOW_HEIGHT, (Color){12, 9, 28, 255}); /* opaque: the hall's text must not show through */
    DrawRectangle(MENU_PANEL_W - 2, 0, 2, WINDOW_HEIGHT, kBaseboard);
    int x = MENU_ROW_X + 20;
    PText("Arcade menu", x, 56, TYPE_L, kWarmWhite);
    DrawRectangle(x, 92, PMeasure("Arcade menu", TYPE_L), 4, kPink);
    KitDraw_List(kv);
    PText(ForHints(kv, "Enter open    Esc close", "A open    B close"), x, WINDOW_HEIGHT - 48, TYPE_S, kTextDim);
}

void Render_Dialog(const KitDialog *d, const KitView *kv) {
    DrawRectangle(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, Fade(kVeil, 0.6f));
    int x = KIT_DLG_X, y = KIT_DLG_Y;
    DrawRectangle(x, y, KIT_DLG_W, KIT_DLG_H, (Color){12, 9, 28, 250});
    DrawRectangleLinesEx((Rectangle){(float)x, (float)y, (float)KIT_DLG_W, (float)KIT_DLG_H}, 2.0f, kBaseboard);
    DrawRectangle(x, y, KIT_DLG_W, 4, kPink);
    PText(d->title, x + 32, y + 32, TYPE_M, kWarmWhite);
    for (int i = 0; i < 2; i++) if (d->body[i][0]) PText(d->body[i], x + 32, y + 68 + i * 20, TYPE_S, kTextDim);
    KitDraw_List(kv);
}

void Render_Toast(const char *text, bool bad, float alpha, bool onHall) {
    if (!text || !text[0] || alpha <= 0.0f) return;
    if (alpha > 1.0f) alpha = 1.0f;
    int w = PMeasure(text, TYPE_S) + 36, h = 24;
    int x = WINDOW_WIDTH / 2 - w / 2, y = onHall ? 44 : 16; /* on the hall: over the neon tube, above the row counter */
    DrawRectangle(x, y, w, h, Fade(kPanel, alpha));
    DrawRectangle(x, y, 4, h, Fade(bad ? kCoinRed : kTeal, alpha));
    PText(text, x + 20, y + 8, TYPE_S, Fade(bad ? kCoinRed : kText, alpha));
}
