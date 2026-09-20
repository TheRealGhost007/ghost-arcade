#include "render.h"
#include <time.h>
#include "winscale.h"
#include "prefs.h"
#include "achievements.h"
#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* --- Look: a dim arcade hall. Indigo wall, patterned carpet, and a row of
 * cabinets whose only real light is their own marquee. Each game's light
 * colour is pulled from its icon, so the row gets more colourful as the
 * catalog grows without anyone picking colours by hand. */
static const Color kWallTop = {30, 22, 64, 255};
static const Color kWallBottom = {21, 16, 43, 255};
static const Color kFloor = {15, 11, 33, 255};
static const Color kBaseboard = {62, 53, 110, 255};
static const Color kCabinet = {43, 36, 80, 255};
static const Color kCabinetDark = {28, 23, 56, 255};
static const Color kCabinetEdge = {70, 60, 124, 255};
static const Color kScreenOff = {10, 8, 22, 255};
static const Color kScreenOn = {18, 22, 46, 255};
static const Color kInk = {21, 16, 43, 255};          /* text printed on a lit marquee */
static const Color kWarmWhite = {255, 246, 224, 255}; /* backlit plexi */
static const Color kText = {242, 236, 255, 255};
static const Color kTextDim = {154, 145, 196, 255};
static const Color kCoinRed = {255, 59, 48, 255};
static const Color kPink = {255, 79, 163, 255};
static const Color kTeal = {33, 212, 200, 255};
static const Color kYellow = {255, 210, 63, 255};
static const Color kCard = {12, 9, 28, 240};

/* Press Start 2P is drawn on an 8x8 grid, so it only renders with even
 * pixels at multiples of 8. Every piece of text in the launcher uses one of
 * these three sizes -- nothing in between. */
#define TYPE_S 8
#define TYPE_M 16
#define TYPE_L 24

static Font sPixelFont;
static bool sFontLoaded = false;

static float PSpacing(float fontSize) { return fontSize * 0.125f; } /* one font pixel */

static void PText(const char *text, int x, int y, int fontSize, Color color) {
    Font font = sFontLoaded ? sPixelFont : GetFontDefault();
    DrawTextEx(font, text, (Vector2){(float)x, (float)y}, (float)fontSize, PSpacing((float)fontSize), color);
}

static int PMeasure(const char *text, int fontSize) {
    Font font = sFontLoaded ? sPixelFont : GetFontDefault();
    return (int)MeasureTextEx(font, text, (float)fontSize, PSpacing((float)fontSize)).x;
}

static void PTextCentered(const char *text, int fontSize, int centerX, int y, Color color) {
    PText(text, centerX - PMeasure(text, fontSize) / 2, y, fontSize, color);
}

static void PTextRight(const char *text, int fontSize, int rightX, int y, Color color) {
    PText(text, rightX - PMeasure(text, fontSize), y, fontSize, color);
}

static Color Mix(Color a, Color b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return (Color){
        (unsigned char)((float)a.r + ((float)b.r - (float)a.r) * t),
        (unsigned char)((float)a.g + ((float)b.g - (float)a.g) * t),
        (unsigned char)((float)a.b + ((float)b.b - (float)a.b) * t),
        255,
    };
}

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
    char fontPath[512];
    snprintf(fontPath, sizeof(fontPath), "%s/fonts/PressStart2P-Regular.ttf", assetsDir);
    if (FileExists(fontPath)) {
        sPixelFont = LoadFontEx(fontPath, 64, NULL, 0);
        if (sPixelFont.texture.id != 0) {
            SetTextureFilter(sPixelFont.texture, TEXTURE_FILTER_POINT);
            sFontLoaded = true;
        }
    }

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
    if (sFontLoaded) UnloadFont(sPixelFont);
}

/* 7x7 pixel-art X, drawn as blocky flat-filled squares (no rounded corners,
 * no anti-aliased lines) to match the retro pixel-font theme. */
static const char kCloseXPattern[7][8] = {
    "X.....X",
    ".X...X.",
    "..X.X..",
    "...X...",
    "..X.X..",
    ".X...X.",
    "X.....X",
};

static void DrawCloseButton(void) {
    DrawRectangle(CLOSE_BTN_X, CLOSE_BTN_Y, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE, (Color){48, 18, 30, 255});
    DrawRectangleLinesEx((Rectangle){(float)CLOSE_BTN_X, (float)CLOSE_BTN_Y, (float)CLOSE_BTN_SIZE, (float)CLOSE_BTN_SIZE},
                         2.0f, (Color){96, 32, 44, 255});
    int px = 2;
    int ox = CLOSE_BTN_X + (CLOSE_BTN_SIZE - 7 * px) / 2;
    int oy = CLOSE_BTN_Y + (CLOSE_BTN_SIZE - 7 * px) / 2;
    for (int y = 0; y < 7; y++) {
        for (int x = 0; x < 7; x++) {
            if (kCloseXPattern[y][x] == 'X') DrawRectangle(ox + x * px, oy + y * px, px, px, kCoinRed);
        }
    }
}

static void FormatHMS(double seconds, char *out, int outSize) {
    if (seconds < 0) seconds = 0;
    long total = (long)seconds;
    long h = total / 3600;
    long mnt = (total % 3600) / 60;
    if (h > 0) snprintf(out, outSize, "%ldh %ldm", h, mnt);
    else snprintf(out, outSize, "%ldm", mnt);
}

/* ------------------------------------------------------------------ hall */

#define FLOOR_Y 436         /* where the cabinets stand */
#define SLOT_SPACING 200    /* centre-to-centre distance along the row */
#define CAB_W_UNITS 48      /* wide enough for a 10-letter name at 16px on the marquee */
#define CAB_H_UNITS 84

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

static void DrawExitSign(float time) {
    int x = 28, y = 68;
    float on = fmodf(time, 4.0f) < 3.6f ? 1.0f : 0.55f;
    Color green = {90, 230, 130, 255};
    DrawRectangle(x - 1, y - 1, 36, 14, (Color){14, 40, 26, 255});
    BeginBlendMode(BLEND_ADDITIVE);
    DrawRectangle(x - 6, y - 6, 46, 24, Fade(green, 0.06f * on));
    EndBlendMode();
    PText("EXIT", x + 2, y + 2, TYPE_S, Fade(green, on));
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
static void DrawHall(bool full) {
    float time = (float)GetTime();
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
        DrawExitSign(time);
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

static void DrawCabinet(const GameEntry *game, float centerX, float focus, float marqueePower, bool installed, float coinT) {
    float u = 3.0f + focus; /* 3px units in the row, 4px when selected */
    CabFrame f = {centerX - (CAB_W_UNITS * u) / 2.0f, (float)FLOOR_Y - 4.0f - CAB_H_UNITS * u, u};

    IconCacheEntry *icon = GetGameIcon(game);
    Color light = icon ? icon->light : LightFromName(game->name);
    /* A machine that isn't installed is unplugged: even when selected its
     * marquee only gets a little ambient light and the coin slots stay dark. */
    float lit = focus * marqueePower * (installed ? 1.0f : 0.18f);

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
        drewDemo = DrawDemo(game->name, &box, (float)GetTime());
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
    float door = W / 2 - 8;
    CabRect(&f, door, 60, 16, 17, kCabinetDark);
    CabRect(&f, door, 60, 16, 1, kCabinetEdge);
    Color slot = Mix((Color){86, 22, 28, 255}, kCoinRed, installed ? focus : 0.0f);
    bool credited = coinT >= COIN_DING_AT;
    /* Credit accepted: the slots flash white, then strobe until the game is up. */
    if (credited) slot = (fmodf(coinT * 18.0f, 2.0f) < 1.0f) ? kWarmWhite : kYellow;
    CabRect(&f, door + 3, 63, 3, 5, slot);
    CabRect(&f, door + 10, 63, 3, 5, slot);
    CabRect(&f, door + 5, 72, 6, 2, kScreenOff);
    CabRect(&f, 0, 80, W, 4, kCabinetDark);

    if (coinT >= 0.0f) {
        /* The screen warms to white as the machine takes the credit. */
        if (credited) {
            float k = (coinT - COIN_DING_AT) / (1.0f - COIN_DING_AT);
            int sx = (int)roundf(f.x0 + 5 * u), sw = (int)roundf((W - 10) * u);
            int sy = (int)roundf(f.y0 + 18 * u), sh = (int)roundf(26 * u);
            DrawRectangle(sx, sy, sw, sh, Fade(kWarmWhite, k * k * 0.85f));
        }

        /* Flight: tossed up from the player's side of the floor, a lazy arc,
         * then straight down into the left slot. */
        float slotX = f.x0 + (door + 4.5f) * u;
        float slotY = f.y0 + 63.0f * u;
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
}

static void DrawHeader(const char *arcadeName, bool adminUnlocked) {
    if (sGhostIconLoaded) {
        DrawTexturePro(sGhostIcon, (Rectangle){0, 0, (float)sGhostIcon.width, (float)sGhostIcon.height},
                       (Rectangle){24, 12, 32, 32}, (Vector2){0, 0}, 0.0f, WHITE);
    }
    PText("GHOST LAUNCHER", 68, 20, TYPE_M, kWarmWhite);

    int rightX = CLOSE_BTN_X - 24;
    if (arcadeName && arcadeName[0] != '\0') {
        char buf[PROFILE_USERNAME_LEN + 16];
        snprintf(buf, sizeof(buf), "%.24s", arcadeName);
        PTextRight(buf, TYPE_S, rightX, 24, kYellow);
        PTextRight("Playing as ", TYPE_S, rightX - PMeasure(buf, TYPE_S), 24, kTextDim);
    } else {
        /* An empty name is a prompt to act, not a blank. */
        PTextRight("Press U to set your name", TYPE_S, rightX, 24, kCoinRed);
    }

    if (adminUnlocked) {
        int x = 68 + PMeasure("GHOST LAUNCHER", TYPE_M) + 20;
        DrawRectangle(x, 18, PMeasure("ADMIN", TYPE_S) + 16, 20, (Color){80, 28, 70, 255});
        PText("ADMIN", x + 8, 24, TYPE_S, kPink);
        PText("A add   E edit   Del remove", x + PMeasure("ADMIN", TYPE_S) + 32, 24, TYPE_S, kTextDim);
    }
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

#define CARD_X 40
#define CARD_Y 474
#define CARD_W (WINDOW_WIDTH - 80)
#define CARD_H 122

static void DrawCardStat(int x, int y, const char *label, const char *value, const char *detail, bool empty) {
    PText(label, x, y, TYPE_S, kTextDim);
    if (empty) {
        PText(value, x, y + 16, TYPE_S, kTextDim);
        return;
    }
    PText(value, x, y + 14, TYPE_M, kText);
    if (detail && detail[0]) PText(detail, x + PMeasure(value, TYPE_M) + 12, y + 22, TYPE_S, kTextDim);
}

/* The instruction card: what this machine is, and how you're doing on it. */
static void DrawCard(const GameEntry *game, const GameCard *card, const char *errorMsg) {
    DrawRectangle(CARD_X, CARD_Y, CARD_W, CARD_H, kCard);
    DrawRectangleLinesEx((Rectangle){CARD_X, CARD_Y, CARD_W, CARD_H}, 2.0f, kBaseboard);

    int left = CARD_X + 24;
    char name[40];
    snprintf(name, sizeof(name), "%.22s", game->name);
    PText(name, left, CARD_Y + 16, TYPE_L, kWarmWhite);

    char lines[2][WRAP_BUF];
    int n = WrapText(game->desc, TYPE_S, 500, lines, 2);
    for (int i = 0; i < n; i++) PText(lines[i], left, CARD_Y + 52 + i * 14, TYPE_S, kTextDim);

    bool blinkOn = fmod(GetTime(), 1.2) < 0.8; /* attract-mode blink */
    if (card->install == INSTALL_RUNNING) {
        char msg[24];
        snprintf(msg, sizeof(msg), "Installing%.*s", (int)(GetTime() * 3.0) % 4, "...");
        PText(msg, left, CARD_Y + 90, TYPE_M, kYellow);
    } else if (card->install == INSTALL_FAILED) {
        PText("The install failed. Details: ~/.local/share/ghost-launcher/install.log",
              left, CARD_Y + 94, TYPE_S, kCoinRed);
    } else if (errorMsg && errorMsg[0] != '\0') {
        char clipped[80];
        snprintf(clipped, sizeof(clipped), "%.60s", errorMsg);
        PText(clipped, left, CARD_Y + 94, TYPE_S, kCoinRed);
    } else if (card->installed) {
        if (blinkOn) PText("Press Enter to play", left, CARD_Y + 90, TYPE_M, kCoinRed);
    } else if (card->canInstall) {
        if (blinkOn) PText("Press Enter to install", left, CARD_Y + 90, TYPE_M, kYellow);
    } else {
        PText("Not installed, and the catalog doesn't say where to install it from.",
              left, CARD_Y + 94, TYPE_S, kTextDim);
    }

    int col = CARD_X + 600;
    DrawRectangle(col - 24, CARD_Y + 14, 2, CARD_H - 28, kBaseboard);

    char value[48], detail[96];
    if (card->localBest.found) {
        snprintf(value, sizeof(value), "%ld", card->localBest.score);
        snprintf(detail, sizeof(detail), "%.16s", card->localBest.mode);
        DrawCardStat(col, CARD_Y + 12, "Your best", value, detail, false);
    } else {
        DrawCardStat(col, CARD_Y + 12, "Your best", "No runs recorded yet", NULL, true);
    }

    if (card->worldBest.found) {
        snprintf(value, sizeof(value), "%ld", card->worldBest.score);
        snprintf(detail, sizeof(detail), "%.12s", card->worldBest.username);
        DrawCardStat(col, CARD_Y + 48, "World best", value, detail, false);
    } else {
        DrawCardStat(col, CARD_Y + 48, "World best", "Nothing online yet", NULL, true);
    }

    FormatHMS(card->playSeconds, value, sizeof(value));
    detail[0] = '\0';
    if (card->playerName[0] != '\0' && card->nameIsOverride) snprintf(detail, sizeof(detail), "as %.12s", card->playerName);
    DrawCardStat(col, CARD_Y + 84, "Time played", value, detail, false);
}

/* Row animation state. Only the list screen animates, and only in answer to
 * the player moving along the row. */
static float sScroll = 0.0f;
static int sLastSelected = -1;
static float sSinceSelect = 10.0f;

static int sUpdateBehind = 0;
void Render_SetUpdateNotice(int behind) { sUpdateBehind = behind > 0 ? behind : 0; }

static void DrawUpdatePill(void) {
    if (sUpdateBehind <= 0) return;
    char buf[64];
    snprintf(buf, sizeof(buf), sUpdateBehind == 1 ? "UPDATE AVAILABLE" : "%d UPDATES AVAILABLE", sUpdateBehind);
    int w = PMeasure(buf, TYPE_S) + 16;
    float pulse = Prefs_Get()->reducedFlashing ? 1.0f : 0.75f + 0.25f * sinf((float)GetTime() * 3.0f);
    int x = WINDOW_WIDTH - 24 - w; /* right side: the left has the hall's EXIT sign */
    DrawRectangle(x, 52, w, 20, Mix(kCabinetDark, kTeal, 0.35f * pulse));
    DrawRectangle(x, 52, 4, 20, kTeal);
    PText(buf, x + 10, 58, TYPE_S, kWarmWhite);
    PTextRight("V to view", TYPE_S, x - 10, 58, kTextDim);
}

void Render_List(const Manifest *m, int selected, const bool *installed, const GameCard *card,
                 const char *arcadeName, bool adminUnlocked, const char *errorMsg, float dt, float coinT) {
    if (selected != sLastSelected) {
        if (sLastSelected < 0) sScroll = (float)selected; /* first frame: no slide-in */
        sLastSelected = selected;
        sSinceSelect = 0.0f;
    }
    sSinceSelect += dt;
    float target = (float)selected;
    float k = 1.0f - expf(-16.0f * dt); /* frame-rate independent ease-out */
    sScroll += (target - sScroll) * k;
    if (fabsf(target - sScroll) < 0.002f) sScroll = target;

    Win_BeginFrame();
    ClearBackground(kWallBottom);
    if (m->count > 0) EaseTint(LightFor(&m->games[selected]), dt);
    DrawHall(true);

    /* A fluorescent tube warming up: the newly selected marquee stutters on. */
    float power = 1.0f;
    if (sSinceSelect < 0.05f) power = 0.9f;
    else if (sSinceSelect < 0.10f) power = 0.25f;
    else if (sSinceSelect < 0.15f) power = 1.0f;
    else if (sSinceSelect < 0.19f) power = 0.5f;

    /* Far cabinets first so the selected one overlaps its neighbours' glow. */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < m->count; i++) {
            float offset = (float)i - sScroll;
            if (fabsf(offset) > 3.2f) continue;
            float focus = 1.0f - fabsf(offset);
            if (focus < 0.0f) focus = 0.0f;
            bool isFront = (i == selected);
            if ((pass == 1) != isFront) continue;
            DrawCabinet(&m->games[i], WINDOW_WIDTH / 2.0f + offset * SLOT_SPACING, focus, isFront ? power : 1.0f,
                        installed ? installed[i] : true, isFront ? coinT : -1.0f);
        }
    }

    DrawVignette();
    DrawHeader(arcadeName, adminUnlocked);
    DrawUpdatePill();
    DrawCloseButton();

    if (m->count == 0) {
        PTextCentered("The arcade is empty", TYPE_M, WINDOW_WIDTH / 2, 230, kWarmWhite);
        PTextCentered(adminUnlocked ? "Press A to wheel in the first machine"
                                    : "Add games to ~/.config/ghost-launcher/games.txt",
                      TYPE_S, WINDOW_WIDTH / 2, 266, kTextDim);
    } else if (card) {
        DrawCard(&m->games[selected], card, errorMsg);
    }

    /* Row position, only once there is more row than fits. */
    if (m->count > 5) {
        char pos[24];
        snprintf(pos, sizeof(pos), "%d / %d", selected + 1, m->count);
        PTextCentered(pos, TYPE_S, WINDOW_WIDTH / 2, 72, kTextDim);
    }

    PTextCentered((card && card->supportsDaily) ? "Left/Right choose    Enter play    D daily    L stats    H trophies    U your name    O online" : "Left/Right choose    Enter play    L stats    H trophies    U your name    Shift+U name for this game",
                  TYPE_S, WINDOW_WIDTH / 2, WINDOW_HEIGHT - 24, kTextDim);

    Win_EndFrame();
}

/* ------------------------------------------------------- secondary screens */

static void BeginScreen(const char *title) {
    Win_BeginFrame();
    ClearBackground(kWallBottom);
    DrawHall(false);
    DrawCloseButton();
    PText(title, 80, 56, TYPE_L, kWarmWhite);
    DrawRectangle(80, 92, PMeasure(title, TYPE_L), 4, kPink);
}

static const char *kStepPrompts[MANIFEST_FIELD_COUNT] = {
    "Game name",
    "Path to the executable",
    "Path to an icon (optional)",
    "One-line description (optional)",
    "Source folder to install it from (optional)",
};

void Render_GameForm(const char *title, int step, const char buffers[MANIFEST_FIELD_COUNT][MANIFEST_FIELD_LEN], const char *errorMsg) {
    BeginScreen(title);

    int y = 128;
    for (int i = 0; i < MANIFEST_FIELD_COUNT; i++) {
        bool active = (step == i);
        bool done = (step > i);
        PText(kStepPrompts[i], 80, y, TYPE_S, active ? kYellow : (done ? kTeal : kTextDim));

        char shown[MANIFEST_FIELD_LEN + 2];
        if (active) snprintf(shown, sizeof(shown), "%s_", buffers[i]);
        else snprintf(shown, sizeof(shown), "%s", buffers[i][0] ? buffers[i] : "-");
        /* Long paths: keep the END visible, that's where the typing happens. */
        const char *visible = shown;
        while (PMeasure(visible, TYPE_M) > WINDOW_WIDTH - 160 && *visible) visible++;
        PText(visible, 80, y + 18, TYPE_M, active ? kText : kTextDim);

        if (active) DrawRectangle(80, y + 40, WINDOW_WIDTH - 160, 2, kYellow);
        y += 78;
    }

    if (errorMsg && errorMsg[0] != '\0') PText(errorMsg, 80, y, TYPE_S, kCoinRed);

    if (step == MANIFEST_FIELD_COUNT) {
        PText("Enter saves this game    Esc cancels", 80, WINDOW_HEIGHT - 48, TYPE_S, kYellow);
    } else {
        PText("Type, then Enter for the next field    Esc cancels", 80, WINDOW_HEIGHT - 48, TYPE_S, kTextDim);
    }

    Win_EndFrame();
}

void Render_Stats(const PlayStats *stats, const Manifest *m, const RunSummary *sums) {
    BeginScreen("Stats");

    char totalBuf[32], line[120];
    FormatHMS(stats->totalLauncherSeconds, totalBuf, sizeof(totalBuf));
    int totalRuns = 0, trophies = 0, trophyTotal = 0;
    double gameSeconds = 0.0;
    for (int i = 0; i < m->count; i++) {
        char slug[64];
        totalRuns += sums[i].runs;
        gameSeconds += Stats_GetGameTime(stats, m->games[i].name);
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
    y += 26;

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
    for (int k = 0; k < m->count && y < 486; k++) {
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
        PTextRight(runs, TYPE_S, cRuns + 60, y, kText);
        PText(tro, cTro, y, TYPE_S, (n > 0 && done == n) ? kYellow : kText);
        PTextRight(sums[i].last[0] ? sums[i].last : "never", TYPE_S, WINDOW_WIDTH - 80, y, sums[i].last[0] ? kTeal : kTextDim);
        y += 26;
    }

    /* The last two weeks, all games together. */
    int perDay[RUNSTATS_DAYS] = {0}, peak = 1;
    for (int i = 0; i < m->count; i++) for (int d = 0; d < RUNSTATS_DAYS; d++) perDay[d] += sums[i].perDay[d];
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

    PText("Esc goes back", 80, WINDOW_HEIGHT - 26, TYPE_S, kTextDim);
    Win_EndFrame();
}

/* The trophy room: every game's achievements, one game at a time. */
void Render_Achievements(const Manifest *m, int selected) {
    BeginScreen("Trophies");
    int totalDone = 0, total = 0;
    for (int i = 0; i < m->count; i++) {
        char slug[64];
        if (!Scores_SlugFromExec(m->games[i].exec, slug, sizeof(slug))) continue;
        total += Ach_CountForGame(slug);
        totalDone += Ach_UnlockedForGame(slug);
    }
    char line[96];
    snprintf(line, sizeof(line), "%d of %d unlocked across the arcade", totalDone, total);
    PText(line, 80, 116, TYPE_S, kTextDim);
    if (m->count == 0) { PText("No games in the catalog.", 80, 164, TYPE_S, kTextDim); PText("Esc goes back", 80, WINDOW_HEIGHT - 48, TYPE_S, kTextDim); Win_EndFrame(); return; }
    if (selected < 0) selected = 0;
    if (selected >= m->count) selected = m->count - 1;

    const GameEntry *g = &m->games[selected];
    char slug[64] = "";
    Scores_SlugFromExec(g->exec, slug, sizeof(slug));
    int n = Ach_CountForGame(slug), done = Ach_UnlockedForGame(slug);

    /* game name with arrows, and its own progress bar */
    char head[80];
    snprintf(head, sizeof(head), "< %.24s >", g->name);
    PText(head, 80, 156, TYPE_M, kYellow);
    snprintf(line, sizeof(line), "%d / %d", done, n);
    PTextRight(line, TYPE_M, WINDOW_WIDTH - 80, 156, kText);
    int barW = WINDOW_WIDTH - 160;
    DrawRectangle(80, 186, barW, 6, kCabinetDark);
    if (n > 0) DrawRectangle(80, 186, (int)((float)barW * (float)done / (float)n), 6, kPink);

    int y = 214;
    if (n == 0) PText("No achievements for this game yet.", 80, y, TYPE_S, kTextDim);
    for (int i = 0; i < n && y < WINDOW_HEIGHT - 80; i++) {
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
        y += 56;
    }
    PText("Left/Right change game    Esc goes back", 80, WINDOW_HEIGHT - 48, TYPE_S, kTextDim);
    Win_EndFrame();
}

void Render_Update(const UpdateState *u, const char *sourceDir) {
    BeginScreen("Updates");
    char line[200];
    int y = 120;

    if (u->status == UPDATE_BEHIND) {
        snprintf(line, sizeof(line), u->behind == 1 ? "A newer version is on GitHub." : "%d newer versions are on GitHub.", u->behind);
        PText(line, 80, y, TYPE_M, kYellow);
    } else if (u->status == UPDATE_CURRENT) {
        PText("You have the newest version.", 80, y, TYPE_M, kTeal);
    } else {
        PText("No answer yet.", 80, y, TYPE_M, kWarmWhite);
    }
    y += 44;

    PText("This build", 80, y, TYPE_S, kTextDim);
    snprintf(line, sizeof(line), "%.7s", u->current[0] ? u->current : "unknown");
    PText(line, 300, y, TYPE_S, kText);
    y += 26;
    PText("Newest on GitHub", 80, y, TYPE_S, kTextDim);
    snprintf(line, sizeof(line), "%.7s", u->latest[0] ? u->latest : "unknown");
    PText(line, 300, y, TYPE_S, u->status == UPDATE_BEHIND ? kYellow : kText);
    y += 26;
    if (u->title[0]) {
        PText("Latest change", 80, y, TYPE_S, kTextDim);
        snprintf(line, sizeof(line), "%.52s", u->title);
        PText(line, 300, y, TYPE_S, kText);
        y += 26;
    }
    PText("Last checked", 80, y, TYPE_S, kTextDim);
    if (u->checkedAt > 0) {
        long long ago = (long long)time(NULL) - u->checkedAt;
        if (ago < 0) ago = 0;
        if (ago < 120) snprintf(line, sizeof(line), "just now");
        else if (ago < 7200) snprintf(line, sizeof(line), "%lld minutes ago", ago / 60);
        else if (ago < 172800) snprintf(line, sizeof(line), "%lld hours ago", ago / 3600);
        else snprintf(line, sizeof(line), "%lld days ago", ago / 86400);
    } else snprintf(line, sizeof(line), "never");
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
    } else if (u->status == UPDATE_UNKNOWN) {
        PText("Either GitHub has not been reached yet, or this build was made", 80, y, TYPE_S, kTextDim); y += 22;
        PText("from changes that are not on GitHub (your own work in progress).", 80, y, TYPE_S, kTextDim);
    }

    PText("The check is one anonymous request to GitHub, at most every six hours.", 80, WINDOW_HEIGHT - 96, TYPE_S, kTextDim);
    PText("check_updates=0 in ~/.config/ghost-launcher/online.conf turns it off.", 80, WINDOW_HEIGHT - 74, TYPE_S, kTextDim);
    PText("R checks again now    Esc goes back", 80, WINDOW_HEIGHT - 48, TYPE_S, kTextDim);
    Win_EndFrame();
}

void Render_OnlineAsk(bool choice, bool firstTime) {
    BeginScreen("Share scores online?");
    int y = 120;
    PText("Yes: your scores go to the Ghost Arcade leaderboard, so", 80, y, TYPE_S, kText); y += 22;
    PText("other people can see them and you can see theirs.", 80, y, TYPE_S, kText); y += 40;
    PText("What is sent", 80, y, TYPE_S, kYellow); y += 24;
    PText("Your arcade name, the game, the mode, the score and the", 80, y, TYPE_S, kTextDim); y += 20;
    PText("date, plus a random ID made on this computer so repeats", 80, y, TYPE_S, kTextDim); y += 20;
    PText("are not counted twice. No account, nothing else.", 80, y, TYPE_S, kTextDim); y += 40;
    PText("No: nothing leaves this machine. Local scores still work.", 80, y, TYPE_S, kText); y += 60;

    int bx = 80;
    const char *labels[2] = {"No, keep it local", "Yes, share my scores"};
    for (int i = 0; i < 2; i++) {
        bool sel = (i == 1) == choice;
        int w = PMeasure(labels[i], TYPE_M) + 40;
        DrawRectangle(bx, y, w, 48, sel ? kPink : kCabinetDark);
        DrawRectangle(bx, y, 4, 48, sel ? kYellow : kCabinetEdge);
        PText(labels[i], bx + 22, y + 14, TYPE_M, sel ? kWarmWhite : kTextDim);
        bx += w + 24;
    }
    PText(firstTime ? "Left/Right choose    Enter confirms    Esc means no" : "Left/Right choose    Enter saves    Esc leaves it as it was",
          80, WINDOW_HEIGHT - 48, TYPE_S, kTextDim);
    Win_EndFrame();
}

void Render_Profile(const char *gameName, const char *buffer) {
    BeginScreen(gameName ? "Name for this game" : "Your name");

    char explain[160];
    if (gameName) snprintf(explain, sizeof(explain), "Only %.40s uses this. Leave it blank to use your arcade name.", gameName);
    else snprintf(explain, sizeof(explain), "Every game uses this on its score tables, online ones included.");
    PText(explain, 80, 116, TYPE_S, kTextDim);

    char shown[PROFILE_USERNAME_LEN + 2];
    snprintf(shown, sizeof(shown), "%s_", buffer);
    PText(shown, 80, 184, TYPE_L, kText);
    DrawRectangle(80, 220, WINDOW_WIDTH - 160, 2, kYellow);

    PText("Online boards show up to 32 characters.", 80, 236, TYPE_S, kTextDim);
    PText("Enter saves    Esc cancels", 80, WINDOW_HEIGHT - 48, TYPE_S, kTextDim);
    Win_EndFrame();
}
