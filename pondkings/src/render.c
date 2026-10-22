#include "render.h"
#include "teams.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ the view */

#define POND_CX 480.0f
#define POND_CY 392.0f
#define Y_SQUASH 0.72f /* the three-quarter view: the far bank is higher up the screen */

Vector2 Render_ToScreen(V2 p) { return (Vector2){POND_CX + Fx_ToFloat(p.x), POND_CY + Fx_ToFloat(p.y) * Y_SQUASH}; }
static Vector2 WorldF(float x, float y) { return (Vector2){POND_CX + x, POND_CY + y * Y_SQUASH}; }

/* ------------------------------------------------------------ palette */

static const Color kNight = {10, 14, 38, 255};
static const Color kSkyLow = {34, 42, 92, 255};
static const Color kSnow = {226, 234, 248, 255};
static const Color kSnowShade = {170, 186, 220, 255};
static const Color kIce = {178, 214, 238, 255};
static const Color kIceDeep = {140, 186, 222, 255};
static const Color kInk = {16, 18, 34, 255};
static const Color kGold = {250, 206, 84, 255};
static const Color kWarm = {255, 196, 110, 255};
/* indoors */
static const Color kRinkIce = {228, 237, 246, 255};
static const Color kArenaIce = {241, 246, 252, 255};
static const Color kLineRed = {212, 46, 56, 255};
static const Color kLineBlue = {46, 98, 212, 255};
static const Color kCrease = {124, 182, 244, 255};
static const Color kBoard = {244, 243, 236, 255};
static const Color kKick = {242, 198, 46, 255};
static const Color kPaper = {250, 240, 220, 255};
static const Color kAmber = {255, 182, 64, 255};

static const Color kPlayers[MAX_PLAYERS] = {{238, 72, 72, 255}, {72, 150, 250, 255}, {84, 214, 120, 255}, {250, 210, 60, 255}};

Color Render_PlayerColor(int slot) { return kPlayers[(slot >= 0 && slot < MAX_PLAYERS) ? slot : 0]; }
static Color RGB(TeamRGB c) { return (Color){c.r, c.g, c.b, 255}; }
Color Render_TeamColor(int side, int rivals) { return RGB(Team_ForSide(side, rivals)->jersey); }

/* ------------------------------------------------------------ small helpers */

static float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static float Smooth(float e0, float e1, float v) {
    float t = Clamp01((v - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}
static Color Shade(Color c, float k) {
    return (Color){(unsigned char)fminf(c.r * k, 255.0f), (unsigned char)fminf(c.g * k, 255.0f), (unsigned char)fminf(c.b * k, 255.0f), c.a};
}
static Color Alpha(Color c, float a) {
    c.a = (unsigned char)(Clamp01(a) * 255.0f);
    return c;
}
/* DrawTriangle wants its corners anticlockwise on screen and culls the other
 * way round; these take them either way. */
static void Tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross < 0.0f) DrawTriangle(a, b, c, col);
    else if (cross > 0.0f) DrawTriangle(a, c, b, col);
}
static void Quad(Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color col) {
    Tri(a, b, c, col);
    Tri(a, c, d, col);
}

/* ------------------------------------------------------------ text */

void Render_Text(const char *text, int x, int y, int size, Color c) {
    DrawText(text, x + size / 10, y + size / 10, size, (Color){0, 0, 0, (unsigned char)(c.a * 0.55f)});
    DrawText(text, x, y, size, c);
}
int Render_TextWidth(const char *text, int size) { return MeasureText(text, size); }
void Render_TextCentred(const char *text, int cx, int y, int size, Color c) { Render_Text(text, cx - MeasureText(text, size) / 2, y, size, c); }

/* A dark band behind a line of text, so it reads over the busy skyline. */
static void Backing(int cx, int y, int w, int h) {
    DrawRectangle(cx - w / 2, y, w, h, (Color){8, 12, 34, 150});
}

/* Big banner text with a heavy outline. */
static void BannerText(const char *text, int cx, int y, int size, Color c) {
    int w = MeasureText(text, size), x = cx - w / 2, o = size / 15 + 2;
    for (int dx = -o; dx <= o; dx += o)
        for (int dy = -o; dy <= o; dy += o) DrawText(text, x + dx, y + dy, size, kInk);
    DrawText(text, x, y, size, c);
}

/* ------------------------------------------------------------ render-only randomness */

static uint32_t sSeed = 0x2468ace1u;
static float Rand01(void) { sSeed = sSeed * 1664525u + 1013904223u; return (float)(sSeed >> 8) / 16777216.0f; }
static float RandRange(float a, float b) { return a + (b - a) * Rand01(); }

/* The indoor scenery has a stream of its own, so the pond's scenery (made
 * first, in the same order as always) and its effects never change. */
static uint32_t sVenueSeed = 0x51ed2701u;
static float VRand(void) { sVenueSeed = sVenueSeed * 1664525u + 1013904223u; return (float)(sVenueSeed >> 8) / 16777216.0f; }
static float VRange(float a, float b) { return a + (b - a) * VRand(); }
static int VPick(int n) {
    int k = (int)(VRand() * (float)n);
    return k < n ? k : n - 1;
}
/* Sparkle worked out at draw time: the same input gives the same answer and
 * spends nothing from either stream. */
static uint32_t Hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

/* ------------------------------------------------------------ scenery, made once */

#define STAR_N 80
#define HOUSE_N 9
#define TREE_N 26
#define SPECK_N 320
#define FLAKE_N 180
#define BULB_N 18

typedef struct { float x, y, phase; } Star;
typedef struct { float x, w, h, roof; int windows; bool lit[6]; bool chimney; } House;
typedef struct { float x, y, h; } Tree;
typedef struct { float x, y, vy, phase, size; } Flake;

static Star sStars[STAR_N];
static House sHouses[HOUSE_N];
static Tree sTrees[TREE_N];
static Vector2 sSpecks[SPECK_N];
static Flake sFlakes[FLAKE_N];
static float sTime = 0.0f;

/* ------------------------------------------------------------ the shape of the ice */

/* Every venue's ice is a rounded rectangle (g->ice); its outlines are worked
 * out once per shape and kept. */
#define CORNER_STEPS 12
#define OUTLINE_N (4 * (CORNER_STEPS + 1))

typedef struct {
    float hl, hw, r;
    int n;                   /* points; 0 until made */
    Vector2 ice[OUTLINE_N];  /* the edge of the ice */
    Vector2 bank[OUTLINE_N]; /* the pond's snowbank: its top */
    Vector2 lip[OUTLINE_N];  /* and its shadowed lip */
    Vector2 rail[OUTLINE_N]; /* the rinks' boards: their outer edge */
    Vector2 nrm[OUTLINE_N];  /* outward, in world terms */
} Shape;

/* the rounded rectangle, grown by `grow` world units, as screen points
 * going round anticlockwise on screen (what DrawTriangleFan wants) */
static int Outline(Vector2 *out, Vector2 *nrm, float hl, float hw, float r0, float grow) {
    float r = r0 + grow;
    float cx = hl - r0, cy = hw - r0;
    const float corners[4][3] = {{cx, -cy, 0.0f}, {-cx, -cy, -90.0f}, {-cx, cy, -180.0f}, {cx, cy, -270.0f}};
    int n = 0;
    for (int c = 0; c < 4; c++) {
        for (int k = 0; k <= CORNER_STEPS; k++) {
            float a = (corners[c][2] - 90.0f * (float)k / 12.0f) * DEG2RAD;
            if (nrm) nrm[n] = (Vector2){cosf(a), sinf(a)};
            out[n++] = WorldF(corners[c][0] + cosf(a) * r, corners[c][1] + sinf(a) * r);
        }
    }
    return n;
}

static Shape sShapes[3];
static int sShapeNext = 0;

static const Shape *ShapeOf(const Ice *ice) {
    float hl = Fx_ToFloat(ice->hl), hw = Fx_ToFloat(ice->hw), r = Fx_ToFloat(ice->r);
    for (int i = 0; i < 3; i++) {
        const Shape *s = &sShapes[i];
        if (s->n > 0 && s->hl == hl && s->hw == hw && s->r == r) return s;
    }
    Shape *s = &sShapes[sShapeNext];
    sShapeNext = (sShapeNext + 1) % 3;
    s->hl = hl;
    s->hw = hw;
    s->r = r;
    s->n = Outline(s->ice, s->nrm, hl, hw, r, 0.0f);
    Outline(s->bank, NULL, hl, hw, r, 30.0f);
    Outline(s->lip, NULL, hl, hw, r, 9.0f);
    Outline(s->rail, NULL, hl, hw, r, 8.0f);
    return s;
}

static Ice IceOf(int venue) {
    Ice ice;
    Game_IceFor(venue, TWIST_NONE, &ice);
    return ice;
}

/* How far the ice reaches across (world y) at a given x... */
static float HalfWidthAt(const Shape *s, float x) {
    float d = fabsf(x) - (s->hl - s->r);
    if (d <= 0.0f) return s->hw;
    if (d >= s->r) return 0.0f;
    return (s->hw - s->r) + sqrtf(s->r * s->r - d * d);
}
/* ...and along (world x) at a given y. */
static float HalfLengthAt(const Shape *s, float y) {
    float d = fabsf(y) - (s->hw - s->r);
    if (d <= 0.0f) return s->hl;
    if (d >= s->r) return 0.0f;
    return (s->hl - s->r) + sqrtf(s->r * s->r - d * d);
}

static void FillOutline(const Vector2 *pts, int n, Color c) {
    Vector2 fan[OUTLINE_N + 2];
    fan[0] = WorldF(0, 0);
    for (int i = 0; i < n; i++) fan[i + 1] = pts[i];
    fan[n + 1] = pts[0];
    DrawTriangleFan(fan, n + 2, c);
}

/* ------------------------------------------------------------ state kept between frames */

static RenderTexture2D sMarks;
static bool sMarksReady = false, sClearMarks = true;
static Texture2D sScan; /* the replay's scanlines: one dark row in three */
static bool sScanReady = false;
static RenderTexture2D sBowl; /* the arena's stands as they stand still, drawn once */
static bool sBowlReady = false;
static void BowlStill(void);

typedef struct { Vector2 a, b; float w; Color c; } Mark;
#define MARK_MAX 128
static Mark sPending[MARK_MAX];
static int sPendingN = 0;
static Vector2 sLastFeet[SKATERS];
static bool sHaveLast[SKATERS];
static float sStride[SKATERS];
#define TRAIL_SHORT 6
#define TRAIL_LONG 14 /* a slapshot's */
static Vector2 sPuckTrail[TRAIL_LONG];
static int sTrailN = 0, sTrailMax = TRAIL_SHORT;
static float sBreakSnow = 0.0f;
static int sLastPhase = -1;
/* indoors */
static float sHype = 0.0f;  /* 1 just after a goal on a rink, fading: the crowd and the lights go wild */
static int sHypeTeam = 0;
static Vector2 sRattleAt;   /* where the puck last rattled the boards */
static float sRattleT = 0.0f;
static float sFresh = 0.0f; /* the gloss of new ice after a resurfacing, fading */
static bool sSoSaved = false; /* this shootout try: the goalie got something on it */

/* ------------------------------------------------------------ indoor scenery, made once */

/* The arena's crowd: rows round the bowl worked out from the arena's ice,
 * shirts merged into runs of one colour, heads drawn one by one so they can
 * bob. */
#define CROWD_ROWS 11
#define FAN_MAX 1700
#define SHIRT_MAX 900
enum { FAN_BOB = 1, FAN_ARMS = 2 };
typedef struct { float x, y, phase; uint8_t size, flags; Color head, shirt; } Fan;
typedef struct { float x, y, w, h; Color c; } Shirt;
static Fan sFans[FAN_MAX];
static int sFanN = 0;
static Shirt sShirts[SHIRT_MAX];
static int sShirtN = 0;
static float sRowY[CROWD_ROWS], sRowH[CROWD_ROWS]; /* each row of the far stands: its foot, its height */

/* The town rink's parents on the bleachers. */
#define PARENT_MAX 18
typedef struct { float x, phase; int row; uint8_t hat, waves, cup; Color coat, scarf, cap, skin; } Parent;
static Parent sParents[PARENT_MAX];
static int sParentN = 0;
static const float kBenchY[3] = {216.0f, 196.0f, 176.0f};

/* The WIND twist's drifting snow. */
#define STREAK_N 28
typedef struct { float x, y, len, speed; } Streak;
static Streak sStreaks[STREAK_N];

#define NCOLORS(a) ((int)(sizeof(a) / sizeof((a)[0])))
static const Color kFanShirts[] = {
    {196, 44, 52, 255},   {196, 44, 52, 255},  {196, 44, 52, 255},   {244, 240, 232, 255}, {30, 40, 84, 255},  {110, 112, 124, 255},
    {28, 28, 34, 255},    {236, 186, 40, 255}, {58, 146, 84, 255},   {150, 208, 240, 255}, {238, 128, 40, 255}, {128, 78, 188, 255},
};
static const Color kSkins[] = {{244, 204, 170, 255}, {226, 178, 140, 255}, {198, 144, 104, 255}, {150, 100, 70, 255}, {110, 72, 50, 255}};
static const Color kHats[] = {{214, 50, 56, 255}, {40, 96, 200, 255}, {240, 200, 60, 255}, {60, 150, 90, 255}, {236, 236, 240, 255}, {120, 70, 170, 255}};
static const Color kHair[] = {{40, 30, 26, 255}, {90, 60, 36, 255}, {200, 160, 90, 255}, {150, 70, 40, 255}};

static void MakeCrowd(void) {
    Ice ice = IceOf(VENUE_ARENA);
    float hl = Fx_ToFloat(ice.hl), hw = Fx_ToFloat(ice.hw), r = Fx_ToFloat(ice.r);
    float farEdge = WorldF(0, -hw).y;
    float y = 210.0f;
    sFanN = sShirtN = 0;
    for (int k = 0; k < CROWD_ROWS; k++) {
        float step = 14.0f - 0.45f * (float)k; /* the rows close up toward the back */
        float grow = 22.0f + 10.0f * (float)k;
        float lift = farEdge - grow * Y_SQUASH - y;
        float light = 0.95f - 0.05f * (float)k; /* and sit further in the dark */
        float spacing = 11.0f - 0.25f * (float)k;
        int size = k < 3 ? 5 : (k < 7 ? 4 : 3);
        float shirtW = (float)size + 3.0f, shirtH = (float)size + 2.0f;
        sRowY[k] = y;
        sRowH[k] = step;
        Vector2 pts[OUTLINE_N], nrm[OUTLINE_N];
        int n = Outline(pts, nrm, hl, hw, r, grow);
        float carry = spacing * 0.5f;
        int blockLeft = 0, last = -1;
        Color shirt = kFanShirts[0];
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            Vector2 a = {pts[i].x, pts[i].y - lift}, b = {pts[j].x, pts[j].y - lift};
            float ny = 0.5f * (nrm[i].y + nrm[j].y);
            float len = hypotf(b.x - a.x, b.y - a.y);
            float d = carry;
            for (; d < len; d += spacing) {
                float t = d / len;
                float x = a.x + (b.x - a.x) * t, yb = a.y + (b.y - a.y) * t;
                if (ny > 0.5f || x < -4.0f || x > SCREEN_W + 4.0f) { last = -1; continue; } /* not the near side */
                if (x > 238.0f && x < 722.0f && yb < 92.0f) { last = -1; continue; }        /* behind the big screen */
                if (ny < -0.98f && fmodf(x + 30.0f, 180.0f) < 8.0f) { last = -1; continue; } /* stairs */
                if (VRand() < 0.04f) { last = -1; continue; }                                  /* an empty seat */
                if (blockLeft <= 0) {
                    shirt = kFanShirts[VPick(NCOLORS(kFanShirts))];
                    blockLeft = 2 + VPick(5);
                }
                blockLeft--;
                if (sFanN >= FAN_MAX) break;
                Fan *f = &sFans[sFanN++];
                float h = VRand();
                Color head = h < 0.62f ? kSkins[VPick(NCOLORS(kSkins))] : (h < 0.85f ? kHats[VPick(NCOLORS(kHats))] : kHair[VPick(NCOLORS(kHair))]);
                f->x = x;
                f->y = yb - shirtH - (float)size + 1.0f;
                f->size = (uint8_t)size;
                f->head = Shade(head, light);
                f->shirt = Shade(shirt, light);
                f->phase = VRange(0.0f, 6.2832f);
                f->flags = (uint8_t)((VRand() < 0.35f ? FAN_BOB : 0) | (VRand() < 0.3f ? FAN_ARMS : 0));
                /* neighbours along a row in the same colour share one run of shirt */
                Shirt *prev = last >= 0 ? &sShirts[last] : NULL;
                float sx = x - shirtW * 0.5f, sy = yb - shirtH;
                if (prev && fabsf(prev->y - sy) < 1.5f && prev->c.r == f->shirt.r && prev->c.g == f->shirt.g && prev->c.b == f->shirt.b &&
                    sx < prev->x + prev->w + 3.0f && sx + shirtW > prev->x - 3.0f) {
                    float x0 = fminf(prev->x, sx), x1 = fmaxf(prev->x + prev->w, sx + shirtW);
                    prev->x = x0;
                    prev->w = x1 - x0;
                } else if (sShirtN < SHIRT_MAX) {
                    sShirts[sShirtN] = (Shirt){sx, sy, shirtW, shirtH, f->shirt};
                    last = sShirtN++;
                }
            }
            carry = d - len;
        }
        y -= step;
    }
}

static void MakeParents(void) {
    static const Color kCoats[] = {{70, 110, 170, 255}, {170, 60, 50, 255}, {60, 120, 80, 255}, {200, 150, 60, 255},
                                   {90, 70, 130, 255},  {60, 64, 76, 255},  {150, 90, 60, 255}};
    sParentN = 0;
    for (int row = 0; row < 3; row++) {
        int count = 4 + VPick(3);
        float slot = 740.0f / (float)count;
        for (int k = 0; k < count && sParentN < PARENT_MAX; k++) {
            Parent *p = &sParents[sParentN++];
            p->x = 118.0f + slot * ((float)k + VRange(0.2f, 0.8f));
            p->row = row;
            p->phase = VRange(0.0f, 6.2832f);
            p->hat = (uint8_t)VPick(3); /* a toque, a cap, or none */
            p->waves = VRand() < 0.35f;
            p->cup = !p->waves && VRand() < 0.4f;
            p->coat = kCoats[VPick(NCOLORS(kCoats))];
            p->scarf = kHats[VPick(NCOLORS(kHats))];
            p->cap = VRand() < 0.6f ? kHats[VPick(NCOLORS(kHats))] : kHair[VPick(NCOLORS(kHair))];
            p->skin = kSkins[VPick(NCOLORS(kSkins))];
        }
    }
}

void Render_Init(void) {
    sSeed = 0x13572468u;
    for (int i = 0; i < STAR_N; i++) sStars[i] = (Star){RandRange(0, SCREEN_W), RandRange(4, 200), RandRange(0, 6.28f)};
    float x = -20.0f;
    for (int i = 0; i < HOUSE_N; i++) {
        House *h = &sHouses[i];
        h->w = RandRange(48, 86);
        h->h = RandRange(26, 46);
        h->roof = RandRange(14, 26);
        h->x = x + RandRange(30, 70);
        x = h->x + h->w;
        if (x > 400 && x < 560) x += 140; /* leave the middle of the skyline to the trees */
        h->windows = 1 + (int)RandRange(1, 4);
        for (int k = 0; k < 6; k++) h->lit[k] = Rand01() < 0.7f;
        h->chimney = Rand01() < 0.6f;
    }
    for (int i = 0; i < TREE_N; i++) sTrees[i] = (Tree){RandRange(-10, SCREEN_W + 10), RandRange(212, 230), RandRange(34, 70)};
    for (int i = 0; i < SPECK_N; i++) {
        float a = RandRange(-400, 400), b = RandRange(-195, 195);
        sSpecks[i] = WorldF(a, b);
    }
    for (int i = 0; i < FLAKE_N; i++) sFlakes[i] = (Flake){RandRange(0, SCREEN_W), RandRange(0, SCREEN_H), RandRange(18, 48), RandRange(0, 6.28f), RandRange(1, 2.6f)};
    sMarks = LoadRenderTexture(SCREEN_W, SCREEN_H);
    sMarksReady = sMarks.id != 0;
    sClearMarks = true;
    /* indoors */
    sVenueSeed = 0x51ed2701u;
    MakeCrowd();
    MakeParents();
    for (int i = 0; i < STREAK_N; i++) sStreaks[i] = (Streak){VRange(0, SCREEN_W), VRange(200, 600), VRange(14, 36), VRange(220, 380)};
    Image scan = GenImageColor(1, 3, (Color){0, 0, 0, 0});
    ImageDrawPixel(&scan, 0, 0, (Color){0, 0, 0, 84});
    sScan = LoadTextureFromImage(scan);
    UnloadImage(scan);
    sScanReady = sScan.id != 0;
    if (sScanReady) SetTextureWrap(sScan, TEXTURE_WRAP_REPEAT);
    sBowl = LoadRenderTexture(SCREEN_W, SCREEN_H);
    sBowlReady = sBowl.id != 0;
    if (sBowlReady) {
        BeginTextureMode(sBowl);
        ClearBackground((Color){14, 16, 30, 255});
        BowlStill();
        EndTextureMode();
    }
}

void Render_Shutdown(void) {
    if (sMarksReady) UnloadRenderTexture(sMarks);
    sMarksReady = false;
    if (sScanReady) UnloadTexture(sScan);
    sScanReady = false;
    if (sBowlReady) UnloadRenderTexture(sBowl);
    sBowlReady = false;
}

/* ------------------------------------------------------------ effects */

typedef enum { PK_SNOW, PK_BREATH, PK_STAR, PK_CONFETTI, PK_SMOKE, PK_FLASH, PK_CRACK } PartKind;
typedef struct { Vector2 p, v; float life, max, size; Color c; uint8_t kind; } Part;
#define PART_MAX 700
static Part sParts[PART_MAX];

static void Spawn(PartKind kind, Vector2 p, Vector2 v, float life, float size, Color c) {
    for (int i = 0; i < PART_MAX; i++) {
        if (sParts[i].life > 0.0f) continue;
        sParts[i] = (Part){p, v, life, life, size, c, (uint8_t)kind};
        return;
    }
}

static void Burst(Vector2 at, int n, float speed, Color c, PartKind kind, float life) {
    for (int i = 0; i < n; i++) {
        float a = RandRange(0, 6.2832f), s = RandRange(speed * 0.3f, speed);
        Spawn(kind, at, (Vector2){cosf(a) * s, sinf(a) * s * 0.6f - RandRange(10, 40)}, RandRange(life * 0.6f, life), RandRange(1.0f, 2.4f), c);
    }
}

void Render_NewGame(void) {
    memset(sParts, 0, sizeof(sParts));
    memset(sHaveLast, 0, sizeof(sHaveLast));
    sPendingN = 0;
    sTrailN = 0;
    sTrailMax = TRAIL_SHORT;
    sClearMarks = true;
    sLastPhase = -1;
    sHype = 0.0f;
    sRattleT = 0.0f;
    sFresh = 0.0f;
    sSoSaved = false;
}

static Vector2 HeadOf(Vector2 feet) { return (Vector2){feet.x, feet.y - 26.0f}; }

/* The wind's way on screen, as a unit vector. */
static Vector2 WindDir(const Game *g) {
    if (!g || V2_IsZero(g->wind)) return (Vector2){1.0f, 0.0f};
    float x = Fx_ToFloat(g->wind.x), y = Fx_ToFloat(g->wind.y) * Y_SQUASH, l = sqrtf(x * x + y * y);
    return l > 0.0f ? (Vector2){x / l, y / l} : (Vector2){1.0f, 0.0f};
}

/* ------------------------------------------------------------ the resurfacing run */

/* On the rinks the break brings out a machine: in along the far third of
 * the ice, round behind the right-hand net and back along the near third,
 * at a crawl. Where it is follows from the break's phaseTicks alone. */
#define MACH_SPEED 5.5f /* world units a tick */

/* where it is; d: which way it faces on screen (+1 right), squeezed toward 0
 * in the turn; hx, hy: its heading on the ice */
typedef struct { float x, y, d, hx, hy; } MachPose;

/* how far behind the machine its wet cloth drags */
static float MachBack(int venue) { return venue == VENUE_ARENA ? 70.0f : 91.0f; }

static void MachPath(const Shape *s, float *R, float *x0, float *xt) {
    *R = s->hw * 2.0f / 3.0f;
    *x0 = -(s->hl + 60.0f);
    *xt = s->hl - *R - 34.0f;
}

static MachPose MachineAt(const Shape *s, int ticks) {
    float R, x0, xt;
    MachPath(s, &R, &x0, &xt);
    float dist = (float)ticks * MACH_SPEED, la = xt - x0, lb = PI * R;
    if (dist < la) return (MachPose){x0 + dist, -R, 1.0f, 1.0f, 0.0f};
    if (dist < la + lb) {
        float a = -PI / 2.0f + (dist - la) / R;
        float d = -sinf(a), hx = d, hy = cosf(a);
        if (fabsf(d) < 0.42f) d = d < 0.0f ? -0.42f : 0.42f;
        return (MachPose){xt + R * cosf(a), R * sinf(a), d, hx, hy};
    }
    return (MachPose){xt - (dist - la - lb), R, -1.0f, -1.0f, 0.0f};
}

static Rectangle ScreenRect(float wx0, float wy0, float wx1, float wy1) {
    Vector2 a = WorldF(fminf(wx0, wx1), fminf(wy0, wy1)), b = WorldF(fmaxf(wx0, wx1), fmaxf(wy0, wy1));
    float x0 = fmaxf(a.x, 0.0f), y0 = fmaxf(a.y, 0.0f), x1 = fminf(b.x, (float)SCREEN_W), y1 = fminf(b.y, (float)SCREEN_H);
    return (Rectangle){x0, y0, fmaxf(x1 - x0, 0.0f), fmaxf(y1 - y0, 0.0f)};
}

/* The ice the run has covered so far (up to its cloth), as up to three
 * screen rectangles. Their outer sides lie off the ice. */
static int FreshRects(const Shape *s, int ticks, float back, Rectangle out[3]) {
    float R, x0, xt;
    MachPath(s, &R, &x0, &xt);
    float dist = (float)ticks * MACH_SPEED, la = xt - x0, lb = PI * R;
    MachPose m = MachineAt(s, ticks);
    int n = 0;
    out[n++] = ScreenRect(x0, -s->hw - 20.0f, dist < la ? fmaxf(m.x - back, x0) : xt, -R * 0.5f);
    if (dist > la) out[n++] = ScreenRect(xt, -s->hw - 20.0f, s->hl + 20.0f, dist < la + lb ? m.y : s->hw + 20.0f);
    if (dist > la + lb) out[n++] = ScreenRect(fminf(m.x + back, xt), R * 0.5f, xt, s->hw + 20.0f);
    return n;
}

/* The rinks' own effects: the crack of a shot, the boards, a save, the crowd. */
static void RinkEvents(const Game *g, Vector2 pk) {
    uint32_t ev = g->events;
    if (ev & EV_SHOT) {
        Vector2 at = pk;
        for (int i = 0; i < SKATERS; i++)
            if (g->sk[i].flags & SKF_SHOT) at = Render_ToScreen(Game_StickPoint(&g->sk[i]));
        bool slap = (ev & (EV_SLAPSHOT | EV_ONE_TIMER)) != 0; /* on a rink a one-timer is a slapshot off the pass */
        Spawn(PK_FLASH, at, (Vector2){0, 0}, slap ? 0.26f : 0.14f, slap ? 9.0f : 4.0f, (Color){255, 255, 255, 230});
        if (slap) Spawn(PK_CRACK, at, (Vector2){0, 0}, 0.32f, 18.0f, (Color){255, 248, 214, 255});
        Burst(at, slap ? 12 : 4, slap ? 130.0f : 70.0f, kSnow, PK_SNOW, slap ? 0.5f : 0.3f);
    }
    if (ev & EV_BOARDS) {
        Spawn(PK_FLASH, pk, (Vector2){0, 0}, 0.2f, 6.0f, (Color){255, 255, 255, 220});
        for (int k = 0; k < 4; k++) Spawn(PK_SNOW, pk, (Vector2){RandRange(-60, 60), RandRange(-80, -20)}, 0.3f, 1.4f, (Color){255, 255, 255, 230});
        sRattleAt = pk;
        sRattleT = 0.3f;
    }
    if (ev & EV_SAVE) {
        int t = 0;
        if (g->gk[1].present && (!g->gk[0].present || V2_Len2(V2_Sub(g->gk[1].pos, g->puck.pos)) < V2_Len2(V2_Sub(g->gk[0].pos, g->puck.pos)))) t = 1;
        Vector2 at = Render_ToScreen(g->gk[t].pos);
        float out = t == 0 ? 1.0f : -1.0f;
        for (int k = 0; k < 16; k++) {
            Color c = k % 3 ? kSnow : (Color){190, 220, 250, 255};
            Spawn(PK_SNOW, (Vector2){at.x + RandRange(-9, 9), at.y - RandRange(0, 6)}, (Vector2){out * RandRange(20, 150), RandRange(-120, -20)},
                  RandRange(0.35f, 0.7f), RandRange(1.2f, 2.6f), c);
        }
        if (g->setup.mode == MODE_SHOOTOUT) sSoSaved = true;
    }
    if (ev & EV_GOAL) {
        sHype = 1.0f;
        sHypeTeam = g->lastGoalTeam >= 0 ? g->lastGoalTeam : 0;
    }
    if (g->phase == PH_SO_READY) sSoSaved = false;
}

void Render_OnStep(const Game *g) {
    bool pond = g->setup.venue == VENUE_POND;
    for (int i = 0; i < SKATERS; i++) {
        const Skater *s = &g->sk[i];
        if (s->benched) { sHaveLast[i] = false; continue; }
        Vector2 f = Render_ToScreen(s->pos);
        float speed = Fx_ToFloat(V2_Len(s->vel));
        if (sHaveLast[i] && speed > 0.6f && sPendingN < MARK_MAX) {
            Color c;
            if (pond) c = (s->flags & SKF_STOPPED) ? (Color){255, 255, 255, 70} : (Color){236, 244, 252, 34};
            else c = (s->flags & SKF_STOPPED) ? (Color){168, 190, 222, 96} : (Color){150, 172, 206, 44}; /* scratches on white ice */
            sPending[sPendingN++] = (Mark){sLastFeet[i], f, (s->flags & SKF_STOPPED) ? 3.0f : 1.4f, c};
        }
        sLastFeet[i] = f;
        sHaveLast[i] = true;
        sStride[i] += speed * 0.22f;
        if (s->flags & SKF_STOPPED) {
            for (int k = 0; k < 3; k++)
                Spawn(PK_SNOW, (Vector2){f.x + RandRange(-5, 5), f.y}, (Vector2){RandRange(-70, 70), RandRange(-70, -20)}, RandRange(0.25f, 0.5f), RandRange(1.2f, 2.4f), kSnow);
        }
        if (s->flags & SKF_HIT) {
            Burst(HeadOf(f), 10, 90, kSnow, PK_SNOW, 0.6f);
            for (int k = 0; k < 4; k++) Spawn(PK_STAR, HeadOf(f), (Vector2){RandRange(-40, 40), RandRange(-60, -20)}, 0.7f, 2.0f, kGold);
        }
        if ((g->tick + (uint32_t)i * 13u) % 80u == 0u)
            Spawn(PK_BREATH, (Vector2){f.x + Fx_ToFloat(s->face.x) * 5.0f, f.y - 25.0f}, (Vector2){Fx_ToFloat(s->face.x) * 8.0f + RandRange(-3, 3), -10.0f}, 1.2f, 2.2f, (Color){255, 255, 255, 150});
    }
    Vector2 pk = Render_ToScreen(g->puck.pos);
    if (V2_Len2(g->puck.vel) > (int64_t)FX_INT(5) * FX_INT(5) && g->puck.carrier < 0) {
        if (sTrailN < sTrailMax) sTrailN++;
        for (int k = sTrailN - 1; k > 0; k--) sPuckTrail[k] = sPuckTrail[k - 1];
        sPuckTrail[0] = pk;
    } else if (sTrailN > 0) {
        sTrailN--;
    }
    if (sTrailN == 0) sTrailMax = TRAIL_SHORT;
    if ((g->events & EV_SLAPSHOT) || (!pond && (g->events & EV_ONE_TIMER))) sTrailMax = TRAIL_LONG;
    uint32_t ev = g->events;
    if (ev & (EV_BANK | EV_DIG_OUT)) Burst(pk, ev & EV_BANK ? 16 : 8, 110, kSnow, PK_SNOW, 0.7f);
    if (ev & EV_BANK_SOFT) Burst(pk, 4, 50, kSnow, PK_SNOW, 0.4f);
    if (ev & EV_GOAL) {
        Burst(pk, 30, 160, kSnow, PK_SNOW, 0.9f);
        Color tc = g->lastGoalTeam >= 0 ? (Color){255, 255, 255, 255} : kGold;
        for (int k = 0; k < 40; k++) {
            Color c = k % 3 == 0 ? kGold : k % 3 == 1 ? tc : (Color){255, 120, 120, 255};
            Spawn(PK_CONFETTI, (Vector2){pk.x + RandRange(-20, 20), pk.y - 30}, (Vector2){RandRange(-120, 120), RandRange(-220, -80)}, RandRange(1.0f, 1.8f), RandRange(1.5f, 3.0f), c);
        }
    }
    if (!pond) RinkEvents(g, pk);
    if (ev & EV_BREAK_DONE) {
        sClearMarks = true;
        if (!pond) sFresh = 1.0f;
    }
    if (g->phase == PH_BREAK && sLastPhase != PH_BREAK) sBreakSnow = Fx_ToFloat(g->snow);
    sLastPhase = g->phase;
}

void Render_Prepare(const Game *g, float dt) {
    sTime += dt;
    for (int i = 0; i < PART_MAX; i++) {
        Part *p = &sParts[i];
        if (p->life <= 0.0f) continue;
        p->life -= dt;
        p->p.x += p->v.x * dt;
        p->p.y += p->v.y * dt;
        if (p->kind == PK_SNOW || p->kind == PK_CONFETTI || p->kind == PK_STAR) p->v.y += 260.0f * dt;
        if (p->kind == PK_CONFETTI) p->v.x *= 1.0f - 1.5f * dt;
        if (p->kind == PK_BREATH || p->kind == PK_SMOKE) p->size += dt * 3.0f;
    }
    /* chimney smoke, out on the pond */
    if (!g || g->setup.venue == VENUE_POND) {
        for (int i = 0; i < HOUSE_N; i++) {
            House *h = &sHouses[i];
            if (!h->chimney || Rand01() > dt * 2.5f) continue;
            Spawn(PK_SMOKE, (Vector2){h->x + h->w * 0.75f, 228.0f - h->h - h->roof + 4}, (Vector2){RandRange(4, 10), -12.0f}, 3.0f, 3.0f, (Color){120, 130, 170, 90});
        }
    }
    float heavy = g ? Fx_ToFloat(g->snow) : 0.3f;
    bool windy = g && !V2_IsZero(g->wind), blizzard = g && g->setup.twist == TWIST_BLIZZARD;
    Vector2 wd = WindDir(g);
    for (int i = 0; i < FLAKE_N; i++) {
        Flake *f = &sFlakes[i];
        f->y += f->vy * dt * (0.8f + heavy);
        if (blizzard) f->y += f->vy * dt * 0.6f * (0.8f + heavy);
        f->x += sinf(sTime * 0.9f + f->phase) * 12.0f * dt;
        if (windy) {
            f->x += wd.x * 60.0f * dt;
            if (f->x < -4.0f) f->x += SCREEN_W + 8.0f;
            else if (f->x > SCREEN_W + 4.0f) f->x -= SCREEN_W + 8.0f;
        }
        if (f->y > SCREEN_H + 4) { f->y = -4; f->x = RandRange(0, SCREEN_W); }
    }
    if (sHype > 0.0f) sHype = fmaxf(0.0f, sHype - dt / 7.0f);
    if (sRattleT > 0.0f) sRattleT -= dt;
    if (sFresh > 0.0f) sFresh = fmaxf(0.0f, sFresh - dt / 3.0f);
    if (windy) {
        for (int i = 0; i < STREAK_N; i++) {
            Streak *s = &sStreaks[i];
            s->x += wd.x * s->speed * dt;
            s->y += wd.y * s->speed * dt;
            if (s->x < -40.0f) s->x += SCREEN_W + 80.0f;
            else if (s->x > SCREEN_W + 40.0f) s->x -= SCREEN_W + 80.0f;
            if (s->y < 200.0f) s->y += 400.0f;
            else if (s->y > 600.0f) s->y -= 400.0f;
        }
    }
    if (!sMarksReady) return;
    if (sClearMarks) {
        BeginTextureMode(sMarks);
        ClearBackground((Color){0, 0, 0, 0});
        EndTextureMode();
        sClearMarks = false;
        sPendingN = 0;
    }
    if (sPendingN > 0) {
        BeginTextureMode(sMarks);
        for (int i = 0; i < sPendingN; i++) DrawLineEx(sPending[i].a, sPending[i].b, sPending[i].w, sPending[i].c);
        EndTextureMode();
        sPendingN = 0;
    }
    /* the resurfacing leaves no skate marks where it has been */
    if (g && g->ice.boards && g->phase == PH_BREAK) {
        const Shape *s = ShapeOf(&g->ice);
        Rectangle r[3];
        int n = FreshRects(s, g->phaseTicks, MachBack(g->setup.venue), r);
        BeginTextureMode(sMarks);
        for (int i = 0; i < n; i++) {
            if (r[i].width < 1.0f || r[i].height < 1.0f) continue;
            BeginScissorMode((int)r[i].x, (int)r[i].y, (int)r[i].width + 1, (int)r[i].height + 1);
            ClearBackground((Color){0, 0, 0, 0});
            EndScissorMode();
        }
        EndTextureMode();
    }
}

static void DrawParts(bool behindPond) {
    for (int i = 0; i < PART_MAX; i++) {
        Part *p = &sParts[i];
        if (p->life <= 0.0f) continue;
        if ((p->kind == PK_SMOKE) != behindPond) continue;
        float k = p->life / p->max;
        Color c = p->c;
        c.a = (unsigned char)(c.a * (k < 0.5f ? k * 2.0f : 1.0f));
        if (p->kind == PK_STAR) DrawPoly(p->p, 4, p->size + 1, sTime * 200.0f, c);
        else if (p->kind == PK_CONFETTI) DrawRectangleV(p->p, (Vector2){p->size * 1.6f, p->size}, c);
        else if (p->kind == PK_FLASH) {
            c.a = (unsigned char)(p->c.a * k);
            DrawCircleV(p->p, p->size * (1.0f + (1.0f - k) * 1.6f), c);
        } else if (p->kind == PK_CRACK) {
            float r0 = p->size * (0.3f + (1.0f - k) * 0.5f), r1 = p->size * (0.75f + (1.0f - k) * 0.6f);
            for (int s = 0; s < 8; s++) {
                float a = (float)s * 0.785f + 0.2f;
                DrawLineEx((Vector2){p->p.x + cosf(a) * r0, p->p.y + sinf(a) * r0 * Y_SQUASH}, (Vector2){p->p.x + cosf(a) * r1, p->p.y + sinf(a) * r1 * Y_SQUASH}, 2.0f, c);
            }
        } else DrawCircleV(p->p, p->size, c);
    }
}

/* ------------------------------------------------------------ the pond: backdrop */

static void Sky(void) {
    DrawRectangleGradientV(0, 0, SCREEN_W, 240, kNight, kSkyLow);
    for (int i = 0; i < STAR_N; i++) {
        float tw = 0.55f + 0.45f * sinf(sTime * 1.7f + sStars[i].phase);
        DrawRectangle((int)sStars[i].x, (int)sStars[i].y, 2, 2, (Color){230, 236, 255, (unsigned char)(200 * tw)});
    }
    /* northern lights: two slow curtains (slow enough for reduced flashing) */
    {
        for (int band = 0; band < 2; band++) {
            Color c = band == 0 ? (Color){80, 240, 170, 46} : (Color){150, 110, 240, 34};
            for (int x = 0; x < SCREEN_W; x += 4) {
                float fx = (float)x;
                float top = 30.0f + band * 26.0f + 26.0f * sinf(fx * 0.007f + sTime * 0.25f + band) + 12.0f * sinf(fx * 0.019f - sTime * 0.4f);
                float h = 70.0f + 30.0f * sinf(fx * 0.011f + sTime * 0.33f + band * 2.0f);
                DrawRectangleGradientV(x, (int)top, 4, (int)h, c, (Color){c.r, c.g, c.b, 0});
            }
        }
    }
}

static void Town(void) {
    /* far hills */
    for (int x = 0; x < SCREEN_W; x += 8) {
        float y = 196.0f + 10.0f * sinf((float)x * 0.012f) + 6.0f * sinf((float)x * 0.031f + 1.0f);
        DrawRectangle(x, (int)y, 8, 240 - (int)y, (Color){28, 34, 70, 255});
    }
    for (int i = 0; i < HOUSE_N; i++) {
        House *h = &sHouses[i];
        float base = 232.0f;
        Color wall = {22, 26, 54, 255};
        DrawRectangle((int)h->x, (int)(base - h->h), (int)h->w, (int)h->h, wall);
        DrawTriangle((Vector2){h->x - 4, base - h->h}, (Vector2){h->x + h->w + 4, base - h->h}, (Vector2){h->x + h->w / 2, base - h->h - h->roof}, (Color){18, 20, 44, 255});
        /* snow on the roof */
        DrawTriangle((Vector2){h->x + h->w * 0.18f, base - h->h - h->roof * 0.64f}, (Vector2){h->x + h->w * 0.82f, base - h->h - h->roof * 0.64f},
                     (Vector2){h->x + h->w / 2, base - h->h - h->roof}, (Color){200, 210, 236, 255});
        if (h->chimney) DrawRectangle((int)(h->x + h->w * 0.7f), (int)(base - h->h - h->roof + 2), 6, (int)(h->roof * 0.6f), wall);
        for (int k = 0; k < h->windows; k++) {
            float wx = h->x + 8 + k * (h->w - 16) / (float)(h->windows > 1 ? h->windows - 1 : 1) - (h->windows == 1 ? -h->w / 2 + 14 : 0);
            Color win = h->lit[k] ? kWarm : (Color){40, 46, 80, 255};
            if (h->lit[k] && sinf(sTime * 0.3f + h->x + k) > 0.97f) win = (Color){255, 150, 90, 255};
            DrawRectangle((int)wx, (int)(base - h->h + 8), 7, 8, win);
            if (h->lit[k]) DrawRectangle((int)wx - 2, (int)(base - h->h + 6), 11, 12, (Color){255, 200, 120, 26});
        }
    }
    for (int i = 0; i < TREE_N; i++) {
        Tree *t = &sTrees[i];
        Color c = {14, 34, 40, 255};
        for (int k = 0; k < 3; k++) {
            float w = t->h * (0.42f - 0.1f * k), top = t->y - t->h + k * t->h * 0.22f;
            DrawTriangle((Vector2){t->x - w, top + t->h * 0.5f}, (Vector2){t->x + w, top + t->h * 0.5f}, (Vector2){t->x, top}, c);
            DrawTriangle((Vector2){t->x - w * 0.5f, top + t->h * 0.2f}, (Vector2){t->x + w * 0.5f, top + t->h * 0.2f}, (Vector2){t->x, top}, (Color){210, 222, 244, 255});
        }
    }
}

/* The snowfield, and the bank piled round the pond: bright on top, with a
 * shadowed lip where it drops to the ice (deeper on the far side, which
 * faces us). */
static void Snowfield(const Shape *s) {
    DrawRectangleGradientV(0, 226, SCREEN_W, SCREEN_H - 226, (Color){190, 202, 234, 255}, kSnow);
    FillOutline(s->bank, s->n, (Color){244, 248, 255, 255});
    for (int i = 0; i < s->n; i++) {
        Vector2 a = s->bank[i], b = s->bank[(i + 1) % s->n];
        DrawLineEx(a, b, 3, (Color){206, 218, 242, 255});
    }
    FillOutline(s->lip, s->n, kSnowShade);
    for (int i = 0; i < s->n; i++) {
        /* little drifts along the top of the bank */
        Vector2 p = s->bank[i], q = s->lip[i];
        Vector2 m = {(p.x * 2 + q.x) / 3, (p.y * 2 + q.y) / 3};
        DrawCircleV(m, 5.0f + (float)((i * 7) % 4), (Color){252, 253, 255, 255});
    }
}

/* the string of bulbs along the far bank */
static const Color kBulbs[4] = {{255, 90, 90, 255}, {255, 210, 90, 255}, {120, 230, 140, 255}, {110, 170, 255, 255}};
static void StringLights(void) {
    Vector2 a = WorldF(-330, -238), b = WorldF(330, -238);
    DrawLineEx((Vector2){a.x, a.y - 40}, a, 2, (Color){60, 50, 40, 255});
    DrawLineEx((Vector2){b.x, b.y - 40}, b, 2, (Color){60, 50, 40, 255});
    Vector2 prev = {a.x, a.y - 40};
    for (int i = 0; i <= BULB_N; i++) {
        float t = (float)i / BULB_N;
        Vector2 p = {a.x + (b.x - a.x) * t, a.y - 40 + sinf(t * PI) * 16.0f};
        DrawLineEx(prev, p, 1, (Color){40, 34, 30, 255});
        prev = p;
        if (i == 0 || i == BULB_N) continue;
        Color c = kBulbs[i % 4];
        float glow = 0.6f + 0.4f * sinf(sTime * 2.0f + i * 1.3f);
        DrawCircleV((Vector2){p.x, p.y + 4}, 7, (Color){c.r, c.g, c.b, (unsigned char)(40 * glow)});
        DrawCircleV((Vector2){p.x, p.y + 4}, 2.4f, c);
    }
}

static void PondIce(const Game *g, const Shape *s) {
    FillOutline(s->ice, s->n, kIce);
    /* a darker, older-ice band near the edges and a few cracks */
    for (int i = 0; i < s->n; i++) DrawLineEx(s->ice[i], s->ice[(i + 1) % s->n], 3, kIceDeep);
    Color crack = {150, 190, 220, 160};
    DrawLineEx(WorldF(-120, -60), WorldF(-60, -40), 1, crack);
    DrawLineEx(WorldF(-60, -40), WorldF(-30, -70), 1, crack);
    DrawLineEx(WorldF(140, 80), WorldF(210, 60), 1, crack);
    DrawLineEx(WorldF(210, 60), WorldF(250, 95), 1, crack);
    if (sMarksReady) DrawTextureRec(sMarks.texture, (Rectangle){0, 0, (float)SCREEN_W, -(float)SCREEN_H}, (Vector2){0, 0}, WHITE);
    float snow = g ? Fx_ToFloat(g->snow) : 0.0f;
    if (g && g->phase == PH_BREAK) snow = sBreakSnow;
    if (snow > 0.01f) {
        float clearedTo = -1e9f;
        if (g && g->phase == PH_BREAK) clearedTo = WorldF(-440.0f + 880.0f * (float)g->phaseTicks / 220.0f, 0).x;
        if (clearedTo < SCREEN_W) {
            BeginScissorMode((int)fmaxf(0.0f, clearedTo), 0, SCREEN_W, SCREEN_H);
            Color dust = {248, 250, 255, (unsigned char)(snow * 120.0f)};
            FillOutline(s->ice, s->n, dust);
            int n = (int)(snow * SPECK_N);
            for (int i = 0; i < n; i++) DrawRectangleV(sSpecks[i], (Vector2){2, 2}, (Color){255, 255, 255, 200});
            EndScissorMode();
        }
    }
}

void Render_Backdrop(bool withPond) {
    Ice ice = IceOf(VENUE_POND);
    const Shape *s = ShapeOf(&ice);
    Sky();
    DrawParts(true);
    Town();
    Snowfield(s);
    StringLights();
    if (withPond) PondIce(NULL, s);
}

/* ------------------------------------------------------------ the town rink */

static const float kLampX[4] = {150.0f, 370.0f, 590.0f, 810.0f};
#define LAMP_Y 100.0f

static void LampShades(void) {
    Color enamel = {44, 86, 64, 255};
    for (int i = 0; i < 4; i++) {
        float x = kLampX[i];
        DrawLineEx((Vector2){x, 53}, (Vector2){x, LAMP_Y - 9}, 1, (Color){30, 22, 18, 255});
        Quad((Vector2){x - 5, LAMP_Y - 10}, (Vector2){x + 5, LAMP_Y - 10}, (Vector2){x + 14, LAMP_Y}, (Vector2){x - 14, LAMP_Y}, enamel);
        DrawRectangle((int)x - 14, (int)LAMP_Y, 28, 2, (Color){30, 62, 46, 255});
        DrawCircle((int)x, (int)LAMP_Y + 3, 16, (Color){255, 220, 150, 36});
        DrawCircle((int)x, (int)LAMP_Y + 2, 4, (Color){255, 238, 190, 255});
    }
}

/* the warm light the lamps throw down on the ice (none of it spills past the ends) */
static void LampLight(const Shape *s) {
    int x0 = (int)WorldF(-s->hl, 0).x;
    BeginScissorMode(x0, 0, (int)(2.0f * s->hl), SCREEN_H);
    for (int i = 0; i < 4; i++) {
        float x = kLampX[i];
        Quad((Vector2){x - 12, LAMP_Y + 2}, (Vector2){x + 12, LAMP_Y + 2}, (Vector2){x + 100, 384}, (Vector2){x - 100, 384}, (Color){255, 214, 140, 13});
        DrawEllipse((int)x, 386, 112, 50, (Color){255, 222, 160, 18});
    }
    EndScissorMode();
}

static void ParentSprite(const Parent *p, float yb, float cheer) {
    int x = (int)p->x, y = (int)yb;
    DrawRectangle(x - 6, y - 13, 12, 13, p->coat);
    DrawRectangle(x - 6, y - 13, 12, 2, p->scarf);
    DrawRectangle(x - 4, y - 21, 8, 8, p->skin);
    DrawRectangle(x - 2, y - 18, 1, 2, kInk);
    DrawRectangle(x + 1, y - 18, 1, 2, kInk);
    if (p->hat == 0) {
        DrawRectangle(x - 5, y - 24, 10, 5, p->cap);
        DrawRectangle(x - 1, y - 26, 3, 2, p->scarf);
    } else if (p->hat == 1) {
        DrawRectangle(x - 5, y - 23, 10, 3, p->cap);
        DrawRectangle(x + 3, y - 21, 4, 1, p->cap);
    } else {
        DrawRectangle(x - 4, y - 22, 8, 3, p->cap);
    }
    if (p->waves || cheer > 0.15f) {
        float sw = sinf(sTime * (cheer > 0.15f ? 11.0f : 5.0f) + p->phase) * 3.0f;
        Vector2 sh = {(float)x + 5, (float)y - 11}, hand = {(float)x + 8 + sw, (float)y - 26};
        DrawLineEx(sh, hand, 2, p->coat);
        DrawRectangle((int)hand.x - 1, (int)hand.y - 2, 3, 3, p->skin);
    } else if (p->cup) {
        DrawRectangle(x - 2, y - 9, 4, 5, (Color){240, 240, 236, 255});
        DrawRectangle(x - 2, y - 9, 4, 1, (Color){120, 70, 40, 255});
    }
}

/* three rows of wooden benches behind the far boards, and the parents on them */
static void Bleachers(void) {
    for (int row = 2; row >= 0; row--) {
        float yb = kBenchY[row];
        DrawRectangle(96, (int)yb, 768, 22, row == 1 ? (Color){92, 62, 40, 255} : (Color){102, 70, 44, 255});
        DrawRectangle(96, (int)yb - 3, 768, 4, (Color){178, 126, 78, 255});
        DrawRectangle(96, (int)yb + 1, 768, 1, (Color){70, 46, 30, 255});
        for (int i = 0; i < sParentN; i++)
            if (sParents[i].row == row) ParentSprite(&sParents[i], yb - 2.0f, sHype);
    }
    DrawRectangle(90, 150, 7, 92, (Color){84, 56, 36, 255});
    DrawRectangle(863, 150, 7, 92, (Color){84, 56, 36, 255});
}

/* An old wooden barn of a rink: plank walls, a timber roof, a few lamps. */
static void RinkHall(void) {
    /* the far wall */
    DrawRectangleGradientV(0, 0, SCREEN_W, 250, (Color){62, 40, 28, 255}, (Color){128, 86, 54, 255});
    for (int x = 10; x < SCREEN_W; x += 26) DrawRectangle(x, 54, 2, 190, (Color){84, 56, 36, 130});
    /* the roof: rafters running back to the far wall, a tie beam across */
    DrawRectangleGradientV(0, 0, SCREEN_W, 46, (Color){24, 16, 14, 255}, (Color){50, 34, 24, 255});
    for (int k = 0; k < 4; k++) DrawRectangle(0, 5 + k * 10, SCREEN_W, 2, (Color){64, 44, 30, 255});
    for (int x = -60; x <= SCREEN_W + 60; x += 120) {
        float lean = ((float)x - 480.0f) * 0.08f;
        DrawLineEx((Vector2){(float)x, 0}, (Vector2){(float)x + lean, 46}, 5, (Color){72, 48, 32, 255});
    }
    DrawRectangle(0, 44, SCREEN_W, 9, (Color){88, 60, 38, 255});
    DrawRectangle(0, 44, SCREEN_W, 2, (Color){132, 94, 62, 255});
    DrawRectangle(0, 53, SCREEN_W, 2, (Color){38, 26, 18, 255});
    /* windows onto the snowy night */
    for (int w = 0; w < 2; w++) {
        int wx = w == 0 ? 22 : 898, wy = 98;
        DrawRectangle(wx - 4, wy - 4, 48, 42, (Color){72, 48, 30, 255});
        DrawRectangleGradientV(wx, wy, 40, 34, kNight, kSkyLow);
        for (int k = 0; k < 6; k++) {
            float fy = fmodf(sTime * (12.0f + (float)k * 3.0f) + (float)k * 9.0f, 33.0f);
            DrawRectangle(wx + 3 + (k * 7) % 35, wy + (int)fy, 2, 2, (Color){230, 236, 255, 220});
        }
        DrawRectangle(wx + 19, wy, 2, 34, (Color){72, 48, 30, 255});
        DrawRectangle(wx, wy + 16, 40, 2, (Color){72, 48, 30, 255});
        DrawRectangle(wx - 4, wy + 34, 48, 3, (Color){236, 240, 250, 255}); /* snow on the sill */
    }
    /* the posts, and painted signs either side of the middle (where the banners go) */
    DrawRectangle(76, 55, 10, 190, (Color){96, 64, 40, 255});
    DrawRectangle(874, 55, 10, 190, (Color){96, 64, 40, 255});
    DrawRectangle(186, 102, 150, 28, (Color){40, 72, 54, 255});
    DrawRectangleLines(186, 102, 150, 28, (Color){214, 190, 140, 255});
    Render_TextCentred("TOWN RINK", 261, 106, 20, (Color){238, 226, 196, 255});
    DrawRectangle(634, 103, 132, 26, (Color){238, 230, 212, 255});
    DrawRectangleLines(634, 103, 132, 26, (Color){150, 62, 50, 255});
    Render_TextCentred("HOT COCOA", 700, 106, 20, (Color){178, 52, 44, 255});
    /* pennants for every team in town */
    for (int k = 0; k < 24; k++) {
        float t0 = (float)k / 24.0f, t1 = (float)(k + 1) / 24.0f;
        float x0 = 96.0f + 768.0f * t0, x1 = 96.0f + 768.0f * t1;
        float y0 = 72.0f + sinf(t0 * PI) * 12.0f, y1 = 72.0f + sinf(t1 * PI) * 12.0f;
        DrawLineEx((Vector2){x0, y0}, (Vector2){x1, y1}, 1, (Color){48, 32, 22, 255});
        Color c = k % 6 == 0 ? RGB(Team_Crew()->jersey) : RGB(Team_Rival(k % 6 - 1)->jersey);
        Tri((Vector2){x0 + 3, y0 + 1}, (Vector2){x1 - 3, y1 + 1}, (Vector2){(x0 + x1) / 2, (y0 + y1) / 2 + 12}, c);
    }
    LampShades();
    /* the floor round the ice */
    DrawRectangle(0, 238, SCREEN_W, SCREEN_H - 238, (Color){46, 44, 50, 255});
    DrawRectangle(0, 560, SCREEN_W, 3, (Color){150, 40, 40, 255});
    Bleachers();
}

/* ------------------------------------------------------------ the arena */

/* the crowd's heads, which bob (their shirts are part of the stands, BowlStill) */
static void CrowdHeads(void) {
    bool wild = sHype > 0.05f;
    float rate = 3.0f + 10.0f * sHype, amp = 1.2f + 1.6f * sHype; /* livelier, but heads stay on their shoulders */
    for (int i = 0; i < sFanN; i++) {
        const Fan *f = &sFans[i];
        float dy = 0.0f;
        if (wild || (f->flags & FAN_BOB)) dy = -fabsf(sinf(sTime * rate + f->phase)) * (wild ? amp : 1.2f);
        int x = (int)(f->x - (float)f->size * 0.5f), y = (int)(f->y + dy);
        if (sHype > 0.3f && (f->flags & FAN_ARMS)) DrawRectangle((i & 1) ? x + f->size : x - 1, y - 4, 1, 5, f->shirt);
        DrawRectangle(x, y, f->size, f->size, f->head);
    }
    /* cameras flashing */
    int flashes = wild ? (int)(18.0f * sHype) + 2 : 1;
    uint32_t tick = (uint32_t)(sTime * 12.0f);
    for (int k = 0; k < flashes && sFanN > 0; k++) {
        uint32_t h = Hash(tick * 131u + (uint32_t)k * 7919u);
        if (!wild && (h & 7u) != 0u) continue;
        const Fan *f = &sFans[h % (uint32_t)sFanN];
        DrawRectangle((int)f->x - 1, (int)f->y - 1, 3, 3, (Color){255, 255, 255, 230});
        DrawRectangle((int)f->x - 3, (int)f->y, 7, 1, (Color){255, 255, 255, 140});
    }
}

/* an LED ribbon round the top of the stands */
static void Ribbon(float y, int rivals) {
    DrawRectangle(0, (int)y, SCREEN_W, 8, (Color){8, 10, 18, 255});
    Color a = Render_TeamColor(sHype > 0.05f ? sHypeTeam : 0, rivals), b = {240, 244, 255, 255}, off = {28, 36, 72, 255};
    for (int x = 2; x < SCREEN_W; x += 12) {
        float w = sinf((float)x * 0.035f - sTime * (2.0f + 6.0f * sHype));
        DrawRectangle(x, (int)y + 2, 10, 4, w > 0.2f ? a : (w < -0.6f ? b : off));
    }
}

static void TeamBanner(float x, float w, const TeamStyle *t) {
    float top = 8.0f, h = 50.0f;
    int cx = (int)(x + w / 2);
    DrawRectangle(cx, 0, 1, (int)top, (Color){60, 64, 84, 255});
    DrawRectangle((int)x, (int)top, (int)w, (int)h, RGB(t->jersey));
    DrawRectangle((int)x, (int)top + 4, (int)w, 3, RGB(t->trim));
    DrawRectangle((int)x, (int)(top + h) - 8, (int)w, 3, RGB(t->trim));
    Tri((Vector2){x, top + h}, (Vector2){x + w, top + h}, (Vector2){x + w / 2, top + h + 12}, RGB(t->jersey));
    DrawRectangle((int)(x + w) - 4, (int)top, 4, (int)h, (Color){0, 0, 0, 46});
    DrawCircle(cx, (int)(top + h / 2), 11, RGB(t->toque));
    char letter[2] = {t->name[0], 0};
    Render_TextCentred(letter, cx + 1, (int)(top + h / 2) - 9, 20, RGB(t->trim));
}

/* Everything in the arena's bowl that never moves: the walls, the tiers and
 * their stairs, the crowd's shirts, the roof with its light rigs and a
 * banner for every team. Drawn once into sBowl (or every frame without it). */
static void BowlStill(void) {
    DrawRectangleGradientV(0, 0, SCREEN_W, 250, (Color){4, 6, 14, 255}, (Color){20, 24, 48, 255});
    DrawRectangle(0, 250, SCREEN_W, SCREEN_H - 250, (Color){14, 16, 30, 255});
    for (int k = 0; k < CROWD_ROWS; k++)
        DrawRectangle(0, (int)(sRowY[k] - sRowH[k]), SCREEN_W, (int)sRowH[k] + 1, k % 2 ? (Color){24, 26, 50, 255} : (Color){30, 33, 60, 255});
    float top = sRowY[CROWD_ROWS - 1] - sRowH[CROWD_ROWS - 1];
    for (int x = 150; x < SCREEN_W; x += 180) {
        DrawRectangle(x, (int)top, 8, (int)(sRowY[0] - top), (Color){44, 48, 76, 255});
        for (int k = 0; k < CROWD_ROWS; k++) DrawRectangle(x, (int)sRowY[k] - 1, 8, 1, (Color){74, 80, 116, 255});
    }
    for (int i = 0; i < sShirtN; i++) {
        const Shirt *s = &sShirts[i];
        DrawRectangle((int)s->x, (int)s->y, (int)(s->w + 0.5f), (int)s->h, s->c);
    }
    DrawRectangle(0, 2, SCREEN_W, 2, (Color){34, 38, 56, 255});
    static const float kRigX[4] = {214.0f, 746.0f, 10.0f, 950.0f};
    static const float kRigY[4] = {2.0f, 2.0f, 62.0f, 62.0f};
    for (int i = 0; i < 4; i++) DrawRectangle((int)kRigX[i] - 7, (int)kRigY[i] - 2, 14, 8, (Color){40, 44, 60, 255});
    TeamBanner(26.0f, 44.0f, Team_Crew());
    for (int k = 0; k < RIVAL_COUNT; k++) {
        float x = k < 2 ? 86.0f + 60.0f * (float)k : 770.0f + 60.0f * (float)(k - 2);
        TeamBanner(x, 44.0f, Team_Rival(k));
    }
}

/* The big time: a dark bowl packed to the rafters. */
static void ArenaBowl(int rivals) {
    if (sBowlReady) DrawTextureRec(sBowl.texture, (Rectangle){0, 0, (float)SCREEN_W, -(float)SCREEN_H}, (Vector2){0, 0}, WHITE);
    else BowlStill();
    CrowdHeads();
    Ribbon(sRowY[CROWD_ROWS - 1] - sRowH[CROWD_ROWS - 1] - 9.0f, rivals);
}

/* spotlights sweeping the stands, and the ice after a goal */
static void Spotlights(int rivals) {
    static const Vector2 kSrc[4] = {{214.0f, 6.0f}, {746.0f, 6.0f}, {10.0f, 66.0f}, {950.0f, 66.0f}};
    bool wild = sHype > 0.05f;
    BeginBlendMode(BLEND_ADDITIVE);
    for (int i = 0; i < 4; i++) {
        if (!wild && i >= 2) continue;
        float speed = wild ? 1.4f + 1.2f * sHype : 0.3f;
        float ph = sTime * speed + (float)i * 1.9f;
        float tx = 480.0f + (wild ? 360.0f : 400.0f) * sinf(ph);
        float ty = wild ? 392.0f + 100.0f * sinf(ph * 1.37f + (float)i) : 166.0f + 24.0f * sinf(ph * 0.71f + (float)i);
        Color c = wild ? ((i & 1) ? (Color){255, 255, 255, 255} : Render_TeamColor(sHypeTeam, rivals)) : (Color){255, 244, 222, 255};
        float rw = wild ? 46.0f : 36.0f;
        int a = wild ? (int)(40.0f * sHype) + 16 : 20;
        Tri(kSrc[i], (Vector2){tx - rw, ty}, (Vector2){tx + rw, ty}, (Color){c.r, c.g, c.b, (unsigned char)a});
        DrawEllipse((int)tx, (int)ty, rw, rw * 0.42f, (Color){c.r, c.g, c.b, (unsigned char)(a + 14)});
        DrawCircle((int)kSrc[i].x, (int)kSrc[i].y, 5, (Color){255, 250, 230, 200});
    }
    EndBlendMode();
}

/* the near stands: the backs of heads at the foot of the screen */
static void NearCrowd(void) {
    DrawRectangleGradientV(0, 562, SCREEN_W, 78, (Color){14, 16, 30, 255}, (Color){6, 7, 14, 255});
    bool wild = sHype > 0.05f;
    for (int row = 0; row < 2; row++) {
        float y = row == 0 ? 608.0f : 628.0f;
        Color c = row == 0 ? (Color){24, 26, 44, 255} : (Color){12, 13, 24, 255};
        for (int k = 0; k < 46; k++) {
            float x = (float)k * 22.0f + (row ? 11.0f : 0.0f) + (float)(Hash((uint32_t)(k * 3 + row)) % 7u);
            float bob = wild ? -fabsf(sinf(sTime * 9.0f + (float)k * 1.3f + (float)row)) * 4.0f * sHype : 0.0f;
            DrawRectangle((int)x - 9, (int)(y + bob) + 6, 18, 16, c);
            DrawRectangle((int)x - 5, (int)(y + bob) - 5, 10, 12, c);
        }
    }
}

/* ------------------------------------------------------------ the rinks' ice */

static void Crease(float gx, float inward, float R, bool bright) {
    Vector2 c = WorldF(gx, 0.0f), prev = WorldF(gx, -R);
    Color fill = Alpha(kCrease, bright ? 0.5f : 0.32f), line = Alpha(kLineRed, bright ? 0.8f : 0.45f);
    for (int k = 1; k <= 16; k++) {
        float a = -PI / 2.0f + PI * (float)k / 16.0f;
        Vector2 p = WorldF(gx + inward * cosf(a) * R, sinf(a) * R);
        Tri(c, prev, p, fill);
        DrawLineEx(prev, p, 2.0f, line);
        prev = p;
    }
}

static void Ring(Vector2 c, float r, Color col) {
    DrawEllipseLines((int)c.x, (int)c.y, r, r * Y_SQUASH, col);
    DrawEllipseLines((int)c.x, (int)c.y, r - 1.0f, r * Y_SQUASH - 1.0f, col);
}

static void Dot(float x, float y, Color c) {
    Vector2 p = WorldF(x, y);
    DrawEllipse((int)p.x, (int)p.y, 5.0f, 3.6f, c);
}

/* Hockey markings under the ice: faint at the town rink, bright in the arena. */
static void Markings(const Ice *ice, const Shape *s, bool bright) {
    float a = bright ? 1.0f : 0.55f;
    float goalX = Fx_ToFloat(ice->goalX), NW = Fx_ToFloat(ice->netHalfW);
    Color red = Alpha(kLineRed, 0.85f * a), blue = Alpha(kLineBlue, 0.85f * a), thin = Alpha(kLineRed, 0.7f * a);
    float hw = s->hw - 1.0f, blueX = goalX * 0.36f, zoneX = goalX - 88.0f, dotY = s->hw * 0.45f;
    for (int side = -1; side <= 1; side += 2) {
        float sd = (float)side, gx = goalX * sd, w = HalfWidthAt(s, gx) - 1.0f;
        DrawLineEx(WorldF(gx, -w), WorldF(gx, w), 2.0f, thin);                      /* goal line */
        DrawLineEx(WorldF(blueX * sd, -hw), WorldF(blueX * sd, hw), 5.0f, blue);    /* blue line */
        Crease(gx, -sd, NW + 14.0f, bright);
        for (int t = -1; t <= 1; t += 2) {
            Ring(WorldF(zoneX * sd, dotY * (float)t), 46.0f, Alpha(kLineRed, 0.55f * a));
            Dot(zoneX * sd, dotY * (float)t, red);
            Dot((blueX - 24.0f) * sd, dotY * (float)t, red);
        }
    }
    DrawLineEx(WorldF(0, -hw), WorldF(0, hw), 6.0f, red); /* the centre line */
    Ring(WorldF(0, 0), 58.0f, blue);
    Dot(0.0f, 0.0f, blue);
}

/* Wet, glossy new ice inside a screen rectangle. */
static void FreshSheen(const Shape *s, Rectangle r, float k) {
    if (r.width < 1.0f || r.height < 1.0f || k <= 0.0f) return;
    BeginScissorMode((int)r.x, (int)r.y, (int)r.width + 1, (int)r.height + 1);
    FillOutline(s->ice, s->n, (Color){150, 190, 236, (unsigned char)(38.0f * k)});
    for (int i = 0; i < 16; i++) {
        float y = r.y + 5.0f + (r.height - 10.0f) * ((float)i + 0.5f) / 16.0f;
        float ext = HalfLengthAt(s, (y - POND_CY) / Y_SQUASH) - 10.0f;
        if (ext <= 0.0f) continue;
        float x0 = fmaxf(r.x + fmodf((float)i * 137.0f + 30.0f, fmaxf(r.width, 1.0f)), POND_CX - ext);
        float x1 = fminf(x0 + 30.0f + (float)((i * 53) % 90), POND_CX + ext);
        if (x1 > x0) DrawRectangle((int)x0, (int)y, (int)(x1 - x0), 1, (Color){255, 255, 255, (unsigned char)((i % 3 ? 120.0f : 190.0f) * k)});
    }
    EndScissorMode();
}

/* Behind the machine, across its whole lane and lying on the ice: a spray
 * pipe dripping water onto a wet towel, which leaves the new ice. */
static void Mat(const Game *g, const Shape *s) {
    MachPose m = MachineAt(s, g->phaseTicks);
    float back = MachBack(g->setup.venue), half = s->hw / 3.0f - 4.0f, thick = 6.0f;
    float px = -m.hy, py = m.hx, cx = m.x - m.hx * back, cy = m.y - m.hy * back;
    Vector2 a = WorldF(cx - m.hx * thick + px * half, cy - m.hy * thick + py * half);
    Vector2 b = WorldF(cx + m.hx * thick + px * half, cy + m.hy * thick + py * half);
    Vector2 c = WorldF(cx + m.hx * thick - px * half, cy + m.hy * thick - py * half);
    Vector2 d = WorldF(cx - m.hx * thick - px * half, cy - m.hy * thick - py * half);
    BeginScissorMode((int)WorldF(-s->hl, 0).x, 0, (int)(2.0f * s->hl), SCREEN_H);
    Quad(a, b, c, d, (Color){176, 204, 236, 150});          /* the towel */
    DrawLineEx(a, d, 2.0f, (Color){255, 255, 255, 220});      /* water left behind it */
    DrawLineEx(b, c, 2.0f, (Color){108, 116, 134, 255});      /* the spray pipe */
    for (int k = 0; k < 7; k++) {
        float t = ((float)k + 0.5f) / 7.0f, wob = fmodf(sTime * 5.0f + (float)k * 0.37f, 1.0f);
        Vector2 p = {b.x + (c.x - b.x) * t, b.y + (c.y - b.y) * t};
        DrawRectangle((int)(p.x - m.d * 3.0f * wob), (int)p.y + 1, 2, 2, (Color){150, 200, 255, (unsigned char)(220.0f * (1.0f - wob))});
    }
    EndScissorMode();
}

static void RinkIce(const Game *g, const Ice *ice, const Shape *s, int venue) {
    bool arena = venue == VENUE_ARENA;
    FillOutline(s->ice, s->n, arena ? kArenaIce : kRinkIce);
    Color edge = arena ? (Color){216, 226, 240, 255} : (Color){204, 216, 230, 255};
    for (int i = 0; i < s->n; i++) DrawLineEx(s->ice[i], s->ice[(i + 1) % s->n], 4, edge);
    Markings(ice, s, arena);
    if (!g) return;
    if (sMarksReady) DrawTextureRec(sMarks.texture, (Rectangle){0, 0, (float)SCREEN_W, -(float)SCREEN_H}, (Vector2){0, 0}, WHITE);
    if (g->phase == PH_BREAK) {
        Rectangle r[3];
        int n = FreshRects(s, g->phaseTicks, MachBack(venue), r);
        for (int i = 0; i < n; i++) FreshSheen(s, r[i], 1.0f);
        Mat(g, s);
    } else if (sFresh > 0.0f) {
        FreshSheen(s, (Rectangle){0, 0, (float)SCREEN_W, (float)SCREEN_H}, sFresh);
    }
}

/* GLASS_ICE: clear, slick ice with the lights shining in it. */
static void GlassSheen(const Shape *s, int venue) {
    bool pond = venue == VENUE_POND;
    FillOutline(s->ice, s->n, pond ? (Color){80, 140, 220, 46} : (Color){130, 180, 240, 50});
    float inner = s->hl - s->r - 96.0f;
    for (int k = 0; k < 4; k++) {
        float cx = -inner + 2.0f * inner * ((float)k + 0.5f) / 4.0f + 24.0f * sinf(sTime * 0.25f + (float)k);
        static const float kWide[4] = {14.0f, 34.0f, 20.0f, 40.0f};
        float w = kWide[k], top = -(s->hw - 34.0f), bottom = s->hw - 34.0f;
        float glow = (k % 2 ? 0.08f : 0.12f) + 0.04f * sinf(sTime * 0.9f + (float)k * 1.7f);
        Quad(WorldF(cx - w + 60.0f, top), WorldF(cx + w + 60.0f, top), WorldF(cx + w - 60.0f, bottom), WorldF(cx - w - 60.0f, bottom), Alpha(WHITE, glow));
    }
    /* the lights above, mirrored in it */
    float farY = WorldF(0, -s->hw).y + 4.0f;
    if (pond) {
        Vector2 a = WorldF(-330, -238), b = WorldF(330, -238);
        for (int i = 1; i < BULB_N; i++) {
            float x = a.x + (b.x - a.x) * (float)i / BULB_N;
            DrawRectangleGradientV((int)x - 1, (int)farY, 3, 46, Alpha(kBulbs[i % 4], 0.5f), Alpha(kBulbs[i % 4], 0.0f));
        }
    } else {
        for (int i = 0; i < 4; i++) DrawRectangleGradientV((int)kLampX[i] - 6, (int)farY, 12, 90, (Color){255, 230, 170, 90}, (Color){255, 230, 170, 0});
    }
    /* glints */
    for (int i = 0; i < 14; i++) {
        uint32_t h = Hash((uint32_t)i * 2654435761u);
        float x = -inner - 60.0f + (float)(h % 1000u) / 1000.0f * (2.0f * inner + 120.0f);
        float y = -(s->hw - 40.0f) + (float)((h >> 10) % 1000u) / 1000.0f * (2.0f * s->hw - 80.0f);
        float tw = sinf(sTime * 2.3f + (float)i * 2.1f);
        if (tw < 0.3f) continue;
        Vector2 p = WorldF(x, y);
        DrawRectangle((int)p.x - 3, (int)p.y, 7, 1, Alpha(WHITE, tw));
        DrawRectangle((int)p.x, (int)p.y - 2, 1, 5, Alpha(WHITE, tw));
    }
}

/* ------------------------------------------------------------ the boards */

/* 1 where the boards' inner face shows (the far side), 0 on the near side. */
static float Rise(Vector2 n) { return Smooth(-0.55f, 0.55f, -n.y); }

/* The rinks' boards follow the ice all the way round. On the far side: a
 * white wall with a yellow kick plate at its foot, the rail on top and glass
 * above. On the near side the rail sits at the ice's edge with the boards'
 * outer face below it, so they never hide the play. */
static void Boards(const Shape *s, int venue) {
    bool arena = venue == VENUE_ARENA;
    const float H = 15.0f, G = arena ? 34.0f : 26.0f, OUT = 14.0f;
    int n = s->n;
    float f[OUTLINE_N];
    Vector2 top[OUTLINE_N], rim[OUTLINE_N];
    for (int i = 0; i < n; i++) {
        f[i] = Rise(s->nrm[i]);
        float lift = H * f[i];
        if (sRattleT > 0.0f) {
            float dx = s->ice[i].x - sRattleAt.x, dy = s->ice[i].y - sRattleAt.y;
            if (dx * dx + dy * dy < 80.0f * 80.0f) lift += sinf(sTime * 90.0f + (float)i) * 1.5f * (sRattleT / 0.3f);
        }
        top[i] = (Vector2){s->ice[i].x, s->ice[i].y - lift};
        rim[i] = (Vector2){s->rail[i].x, s->rail[i].y - lift};
    }
    Color glass = arena ? (Color){196, 222, 246, 44} : (Color){214, 232, 246, 38};
    Color rail = arena ? (Color){30, 36, 70, 255} : (Color){150, 104, 62, 255};
    /* glass over the far boards, and the posts that hold it */
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        if (f[i] < 0.02f && f[j] < 0.02f) continue;
        Vector2 a = rim[i], b = rim[j], at = {a.x, a.y - G * f[i]}, bt = {b.x, b.y - G * f[j]};
        Quad(a, b, bt, at, glass);
        DrawLineEx(at, bt, 2.0f, (Color){255, 255, 255, (unsigned char)(80.0f * fminf(f[i], f[j]))});
    }
    float farRim = WorldF(0, -s->hw - 8.0f).y - H;
    for (float x = -(s->hl - s->r); x <= s->hl - s->r + 1.0f; x += 70.0f) {
        Vector2 p = WorldF(x, 0);
        DrawRectangle((int)p.x - 1, (int)(farRim - G), 2, (int)G, (Color){240, 248, 255, 70});
    }
    /* the inner face, with the kick plate at its foot */
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        if (f[i] < 0.02f && f[j] < 0.02f) continue;
        Quad(s->ice[i], s->ice[j], top[j], top[i], kBoard);
        Vector2 ki = {s->ice[i].x, s->ice[i].y - 3.0f * f[i]}, kj = {s->ice[j].x, s->ice[j].y - 3.0f * f[j]};
        Quad(s->ice[i], s->ice[j], kj, ki, kKick);
    }
    if (arena) {
        /* adverts along the far boards */
        static const Color kAds[5] = {{40, 96, 210, 255}, {214, 46, 56, 255}, {30, 30, 40, 255}, {60, 150, 90, 255}, {240, 140, 40, 255}};
        float yt = WorldF(0, -s->hw).y - H;
        int k = 0;
        for (float x = -(s->hl - s->r) + 6.0f; x + 80.0f <= s->hl - s->r - 6.0f; x += 92.0f, k++) {
            Vector2 p = WorldF(x, 0);
            DrawRectangle((int)p.x, (int)yt + 2, 80, (int)H - 6, kAds[k % 5]);
            DrawRectangle((int)p.x + 8, (int)yt + 5, 34 + (k * 13) % 30, 3, (Color){255, 255, 255, 220});
        }
    }
    /* the rail along the top */
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        Quad(top[i], top[j], rim[j], rim[i], rail);
    }
    /* the near side's outer face */
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float hi = OUT * (1.0f - f[i]), hj = OUT * (1.0f - f[j]);
        if (hi < 0.5f && hj < 0.5f) continue;
        Vector2 bi = {rim[i].x, rim[i].y + hi}, bj = {rim[j].x, rim[j].y + hj};
        Quad(rim[i], rim[j], bj, bi, kBoard);
        DrawLineEx(bi, bj, 2.0f, (Color){190, 190, 186, 255});
    }
    if (arena) {
        static const Color kAds[4] = {{214, 46, 56, 255}, {40, 96, 210, 255}, {240, 140, 40, 255}, {30, 30, 40, 255}};
        float yt = WorldF(0, s->hw + 8.0f).y + 3.0f;
        int k = 0;
        for (float x = -(s->hl - s->r) + 10.0f; x + 84.0f <= s->hl - s->r - 10.0f; x += 98.0f, k++) {
            Vector2 p = WorldF(x, 0);
            DrawRectangle((int)p.x, (int)yt, 84, (int)OUT - 6, kAds[k % 4]);
            DrawRectangle((int)p.x + 10, (int)yt + 3, 30 + (k * 17) % 34, 3, (Color){255, 255, 255, 220});
        }
    }
}

/* ------------------------------------------------------------ nets */

static void Net(int side, const Ice *ice, bool rink) {
    float out = side == 0 ? -1.0f : 1.0f;
    float goalX = Fx_ToFloat(ice->goalX), NW = Fx_ToFloat(ice->netHalfW), ND = Fx_ToFloat(ice->netDepth);
    float gx = goalX * out, back = (goalX + ND) * out;
    Vector2 fl = WorldF(gx, -NW), fr = WorldF(gx, NW), bl = WorldF(back, -NW), br = WorldF(back, NW);
    float h = rink ? 20.0f : 16.0f, hb = rink ? 11.0f : 9.0f;
    Color mesh = {255, 255, 255, 120}, frame = {214, 40, 40, 255};
    /* mesh: back and sides, under the frame */
    Vector2 tbl = {bl.x, bl.y - hb}, tbr = {br.x, br.y - hb}, tfl = {fl.x, fl.y - h}, tfr = {fr.x, fr.y - h};
    if (rink) {
        Tri(tfl, bl, tbl, mesh);
        Tri(tfl, fl, bl, mesh);
        Tri(tfr, tbr, br, mesh);
        Tri(tfr, br, fr, mesh);
    } else { /* both nets alike: every panel drawn whichever way round it faces */
        Tri(tfl, bl, tbl, mesh);
        Tri(tfl, fl, bl, mesh);
        Tri(tfr, tbr, br, mesh);
        Tri(tfr, br, fr, mesh);
        Tri(tbl, br, tbr, mesh);
        Tri(tbl, bl, br, mesh);
    }
    for (int k = 1; k < 5; k++) {
        float t = k / 5.0f;
        DrawLineEx((Vector2){bl.x + (br.x - bl.x) * t, bl.y + (br.y - bl.y) * t}, (Vector2){tbl.x + (tbr.x - tbl.x) * t, tbl.y + (tbr.y - tbl.y) * t}, 1, (Color){255, 255, 255, 110});
    }
    DrawEllipse((int)((fl.x + bl.x) / 2), (int)((fl.y + fr.y) / 2) + 2, 16.0f * ND / 22.0f, 26.0f * NW / 30.0f, (Color){40, 60, 110, 40});
    DrawLineEx(fl, tfl, 4, frame);
    DrawLineEx(fr, tfr, 4, frame);
    DrawLineEx(tfl, tfr, 4, frame);
    DrawLineEx(tfl, tbl, 2, frame);
    DrawLineEx(tfr, tbr, 2, frame);
    DrawLineEx(tbl, tbr, 2, frame);
    DrawLineEx(bl, br, 2, frame);
    DrawLineEx(fl, bl, 2, frame);
    DrawLineEx(fr, br, 2, frame);
}

/* ------------------------------------------------------------ skaters */

const char *Render_SkaterName(const Game *g, int i, int rivals) {
    static char buf[8];
    const Skater *s = &g->sk[i];
    if (s->player >= 0) { snprintf(buf, sizeof(buf), "P%d", s->player + 1); return buf; }
    return Team_ForSide(s->team, rivals)->kids[i % TEAM_SIZE];
}

const char *Render_SkaterNameIn(const Game *g, int i, const RenderInfo *ri) {
    static char buf[8];
    const Skater *s = &g->sk[i];
    if (s->player >= 0) { snprintf(buf, sizeof(buf), "P%d", s->player + 1); return buf; }
    if (ri) {
        const char *name = s->team == 0 ? ri->crewNames[i % TEAM_SIZE] : ri->rivalNames[i % TEAM_SIZE];
        if (name && name[0]) return name;
    }
    return Team_ForSide(s->team, ri ? ri->rivals : 0)->kids[i % TEAM_SIZE];
}

static void Skater_(const Game *g, int i, int rivals, bool shovel, Vector2 feetOverride, bool useOverride) {
    const Skater *s = &g->sk[i];
    const TeamStyle *team = Team_ForSide(s->team, rivals);
    Vector2 f = useOverride ? feetOverride : Render_ToScreen(s->pos);
    float fxs = Fx_ToFloat(s->face.x), fys = Fx_ToFloat(s->face.y);
    float vx = Fx_ToFloat(s->vel.x), vy = Fx_ToFloat(s->vel.y);
    float speed = sqrtf(vx * vx + vy * vy);
    Color jersey = RGB(team->jersey), trim = RGB(team->trim), toque = RGB(team->toque);
    Color skin = {244, 204, 170, 255}, pants = {40, 44, 72, 255};

    DrawEllipse((int)f.x, (int)f.y + 1, 11, 4, (Color){40, 60, 110, 70});
    if (s->player >= 0 && !useOverride) {
        Color pc = Render_PlayerColor(s->player);
        DrawEllipseLines((int)f.x, (int)f.y + 1, 15, 6, pc);
        DrawEllipseLines((int)f.x, (int)f.y + 1, 14, 5, pc);
    }
    if (s->stun > 0 && !useOverride) {
        /* down on the ice, seeing stars */
        DrawRectangle((int)f.x - 11, (int)f.y - 7, 18, 7, jersey);
        DrawRectangle((int)f.x - 11, (int)f.y - 4, 18, 2, trim);
        DrawRectangle((int)f.x + 6, (int)f.y - 8, 7, 7, skin);
        DrawRectangle((int)f.x + 6, (int)f.y - 10, 8, 3, toque);
        for (int k = 0; k < 3; k++) {
            float a = sTime * 6.0f + k * 2.1f;
            DrawPoly((Vector2){f.x + 9 + cosf(a) * 9, f.y - 14 + sinf(a) * 3}, 4, 2, a * 40, kGold);
        }
        return;
    }
    float stride = speed > 0.3f ? sinf(sStride[i]) : 0.0f;
    /* legs and skates */
    DrawRectangle((int)(f.x - 4 + stride * 2), (int)f.y - 10, 3, 9, pants);
    DrawRectangle((int)(f.x + 1 - stride * 2), (int)f.y - 10, 3, 9, pants);
    DrawRectangle((int)(f.x - 5 + stride * 2), (int)f.y - 2, 5, 2, kInk);
    DrawRectangle((int)(f.x + 1 - stride * 2), (int)f.y - 2, 5, 2, kInk);
    /* scarf, streaming behind */
    Vector2 neck = {f.x, f.y - 21};
    Vector2 tail = {neck.x - vx * 3.0f + sinf(sTime * 8 + i) * 1.5f, neck.y - vy * 2.0f + 3.0f};
    DrawLineEx(neck, tail, 3, trim);
    /* body */
    DrawRectangle((int)f.x - 6, (int)f.y - 21, 12, 12, jersey);
    DrawRectangle((int)f.x - 6, (int)f.y - 15, 12, 2, trim);
    /* head and toque */
    DrawRectangle((int)f.x - 4, (int)f.y - 29, 8, 8, skin);
    if (fys > -0.6f) {
        int ex = (int)(f.x + fxs * 2.0f);
        DrawRectangle(ex - 2, (int)f.y - 26, 1, 2, kInk);
        DrawRectangle(ex + 1, (int)f.y - 26, 1, 2, kInk);
    }
    DrawRectangle((int)f.x - 5, (int)f.y - 33, 10, 5, toque);
    DrawRectangle((int)f.x - 5, (int)f.y - 29, 10, 2, trim);
    DrawCircle((int)f.x, (int)f.y - 34, 2.2f, trim);
    /* stick: hands at the side it points to, blade on the ice */
    Vector2 hands = {f.x + fxs * 5.0f, f.y - 14};
    Vector2 blade = useOverride ? (Vector2){f.x + 14, f.y + 2} : Render_ToScreen(Game_StickPoint(s));
    if (shovel) {
        DrawLineEx(hands, blade, 2, (Color){120, 90, 60, 255});
        DrawRectangle((int)blade.x - 3, (int)blade.y - 6, 7, 8, (Color){90, 100, 120, 255});
        return;
    }
    if (s->charge > 0 && !useOverride) {
        /* winding up a slapshot: the stick swings back and up, the higher the longer it's held */
        float c = fminf(1.0f, (float)s->charge / 40.0f), side = fxs >= 0.0f ? 1.0f : -1.0f;
        float a0 = atan2f(blade.y - hands.y, blade.x - hands.x), a1 = side > 0.0f ? 4.2f : -1.06f;
        if (side < 0.0f && a0 < a1) a0 += 2.0f * PI; /* back round underneath, never over the front */
        float e = 1.0f - (1.0f - c) * (1.0f - c), a = a0 + (a1 - a0) * e, len = 16.0f + 5.0f * c;
        Vector2 grip = {hands.x - side * 2.0f * c, hands.y - 3.0f * c};
        Vector2 end = {grip.x + cosf(a) * len, grip.y + sinf(a) * len};
        DrawLineEx(grip, end, 2, (Color){186, 136, 78, 255});
        float hx = fxs * (1.0f - c) - side * 0.7f * c, hy = fys * 0.4f * (1.0f - c) + 0.7f * c, hn = sqrtf(hx * hx + hy * hy);
        if (hn > 0.01f) DrawLineEx(end, (Vector2){end.x + hx / hn * 5.0f, end.y + hy / hn * 5.0f}, 3, kInk);
        return;
    }
    DrawLineEx(hands, blade, 2, (Color){186, 136, 78, 255});
    Vector2 toe = {blade.x + fxs * 5.0f, blade.y + fys * 2.0f};
    DrawLineEx(blade, toe, 3, kInk);
}

/* ------------------------------------------------------------ goalies */

static void GoalieHead(int x, int top, int s, Color paint) {
    Color mask = {236, 238, 242, 255}, cage = {50, 54, 70, 255};
    DrawRectangle(x - 5, top, 10, 10, mask);
    DrawRectangle(x - 5, top, 10, 3, paint);
    DrawRectangle(x - 3 + s, top + 4, 6, 5, cage);
    DrawRectangle(x - 2 + s, top + 4, 1, 5, mask);
    DrawRectangle(x + 1 + s, top + 4, 1, 5, mask);
    DrawRectangle(x - 3 + s, top + 6, 6, 1, mask);
}

static void Pad(int x, int y, int w, int h, Color stripe) {
    DrawRectangle(x - 1, y - 1, w + 2, h + 2, (Color){150, 156, 178, 255});
    DrawRectangle(x, y, w, h, (Color){244, 244, 240, 255});
    DrawRectangle(x, y + h / 3, w, 2, stripe);
}

/* A goalie crouched in front of the net: the team's sweater, big white pads,
 * a blocker and a glove, a mask with a cage. A butterfly sits lower and
 * wider; after a save it's sprawled; holding on, the puck is under the glove. */
static void GoalieSprite(const Game *g, int t, int rivals) {
    const Goalie *k = &g->gk[t];
    const TeamStyle *team = Team_ForSide(t, rivals);
    Vector2 f = Render_ToScreen(k->pos);
    int x = (int)f.x, y = (int)f.y, s = t == 0 ? 1 : -1; /* it faces out toward the play */
    Color jersey = RGB(team->jersey), trim = RGB(team->trim), paint = RGB(team->toque);
    Color pants = {40, 44, 72, 255}, glove = {150, 100, 56, 255}, pocket = {108, 70, 36, 255}, stick = {186, 136, 78, 255}, pad = {244, 244, 240, 255};
    DrawEllipse(x, y + 1, 18, 5, (Color){40, 60, 110, 70});
    if (k->recover > 0) {
        /* down in a sprawl, pads flat out along the ice, glove up */
        Pad(x - 23, y - 6, 16, 6, jersey);
        Pad(x + 7, y - 6, 16, 6, jersey);
        DrawRectangle(x - 7, y - 11, 14, 6, pants);
        DrawRectangle(x - 10, y - 21, 20, 11, jersey);
        DrawRectangle(x - 10, y - 16, 20, 2, trim);
        GoalieHead(x, y - 30, s, paint);
        DrawLineEx((Vector2){(float)(x + 8 * s), (float)(y - 18)}, (Vector2){(float)(x + 15 * s), (float)(y - 27)}, 3, jersey);
        DrawCircle(x + 16 * s, y - 29, 4.6f, glove);
        DrawCircle(x + 16 * s, y - 29, 2.2f, pocket);
        DrawRectangle(x - 19 * s - 3, y - 17, 7, 9, pad);
        DrawLineEx((Vector2){(float)(x - 16 * s), (float)(y - 12)}, (Vector2){(float)(x - 6 * s), (float)(y - 1)}, 2, stick);
        DrawLineEx((Vector2){(float)(x - 6 * s), (float)(y - 1)}, (Vector2){(float)(x + 6 * s), (float)y}, 3, kInk);
        return;
    }
    bool fly = k->style == GOALIE_BUTTERFLY;
    int waist = fly ? y - 10 : y - 15, wide = fly ? 3 : 0;
    if (fly) {
        /* knees in, pads flared out on the ice */
        Pad(x - 17, y - 8, 12, 7, jersey);
        Pad(x + 5, y - 8, 12, 7, jersey);
    } else {
        DrawRectangle(x - 11, y - 2, 7, 2, kInk);
        DrawRectangle(x + 4, y - 2, 7, 2, kInk);
        Pad(x - 11, y - 14, 7, 12, jersey);
        Pad(x + 4, y - 14, 7, 12, jersey);
    }
    DrawRectangle(x - 8, waist - 4, 16, 5, pants);
    DrawRectangle(x - 10 - wide, waist - 15, 20 + 2 * wide, 12, jersey);
    DrawRectangle(x - 10 - wide, waist - 9, 20 + 2 * wide, 2, trim);
    GoalieHead(x, waist - 25, s, paint);
    /* the stick: paddle down across the pads, blade along the ice (further out for a poke checker) */
    int reach = k->style == GOALIE_POKER ? 17 : 11;
    DrawLineEx((Vector2){(float)(x - (11 + wide) * s), (float)(waist - 6)}, (Vector2){(float)(x + s), (float)(y - 2)}, 2, stick);
    DrawLineEx((Vector2){(float)(x - 4 * s), (float)(y - 8)}, (Vector2){(float)(x + s), (float)(y - 2)}, 4, stick);
    DrawLineEx((Vector2){(float)(x + s), (float)(y - 1)}, (Vector2){(float)(x + reach * s), (float)y}, 3, kInk);
    /* the blocker on the stick hand */
    DrawRectangle(x - (13 + wide) * s - 3, waist - 12, 7, 9, pad);
    DrawRectangle(x - (13 + wide) * s - 3, waist - 12, 7, 2, jersey);
    if (k->hold > 0) {
        /* the puck tucked under the glove */
        int gx = x + 9 * s;
        DrawEllipse(gx + 4 * s, y + 1, 4.5f, 2.6f, kInk);
        DrawEllipse(gx + 3 * s, y, 2.0f, 1.0f, (Color){90, 94, 110, 255});
        DrawLineEx((Vector2){(float)(x + 8 * s), (float)(waist - 12)}, (Vector2){(float)gx, (float)(y - 4)}, 3, jersey);
        DrawCircle(gx, y - 3, 5.0f, glove);
        DrawCircle(gx, y - 3, 2.2f, pocket);
    } else {
        DrawCircle(x + (13 + wide) * s, waist - 8, 4.6f, glove);
        DrawCircle(x + (13 + wide) * s, waist - 8, 2.2f, pocket);
    }
}

/* ------------------------------------------------------------ the resurfacing machines */

/* Drawn side on, facing m->d (squeezed toward 0 while it turns). */
typedef struct { Vector2 p; float d; } Rig;

static float RX(const Rig *m, float lx) { return m->p.x + lx * m->d; }
static void RRect(const Rig *m, float lx, float ly, float w, float h, Color c) {
    float a = RX(m, lx), b = RX(m, lx + w);
    int x0 = (int)floorf(fminf(a, b) + 0.5f), x1 = (int)floorf(fmaxf(a, b) + 0.5f);
    if (x1 <= x0) x1 = x0 + 1;
    DrawRectangle(x0, (int)(m->p.y + ly), x1 - x0, (int)h, c);
}
static void RWheel(const Rig *m, float lx, float ly, float r, Color tyre, Color hub) {
    float sx = fmaxf(fabsf(m->d), 0.3f);
    int cx = (int)RX(m, lx), cy = (int)(m->p.y + ly);
    DrawEllipse(cx, cy, r * sx, r, tyre);
    DrawEllipse(cx, cy, r * 0.42f * sx, r * 0.42f, hub);
}
static void RLine(const Rig *m, float lx0, float ly0, float lx1, float ly1, float w, Color c) {
    DrawLineEx((Vector2){RX(m, lx0), m->p.y + ly0}, (Vector2){RX(m, lx1), m->p.y + ly1}, w, c);
}

/* The town rink's: an old farm tractor towing a water tank, a wet cloth dragging behind. */
static void Tractor(const Rig *m) {
    Color tyre = {34, 32, 36, 255}, red = {196, 52, 40, 255}, tank = {168, 178, 190, 255}, tankDark = {120, 130, 146, 255}, hub = {236, 196, 64, 255};
    DrawEllipse((int)RX(m, -34), (int)m->p.y + 1, 66.0f * fabsf(m->d) + 14.0f, 6, (Color){40, 60, 110, 60});
    RRect(m, -88, -10, 6, 7, tankDark);                     /* the drip bar (the cloth is on the ice: Mat) */
    RLine(m, -86, -6, -91, 0, 1, (Color){60, 60, 66, 255});
    RWheel(m, -62, -5, 5, tyre, (Color){90, 90, 96, 255});
    RRect(m, -86, -28, 44, 20, tank);                       /* the tank */
    RRect(m, -86, -28, 44, 3, (Color){206, 214, 226, 255});
    RRect(m, -76, -28, 2, 20, tankDark);
    RRect(m, -54, -28, 2, 20, tankDark);
    RRect(m, -68, -32, 9, 4, tankDark);
    RLine(m, -42, -9, -16, -9, 2, (Color){60, 60, 66, 255}); /* the drawbar */
    RWheel(m, -6, -12, 12, tyre, hub);
    RRect(m, -20, -27, 26, 5, red);                          /* the mudguard */
    RRect(m, 0, -25, 30, 13, red);                           /* the bonnet */
    RRect(m, 0, -25, 30, 2, (Color){236, 112, 92, 255});
    RRect(m, 28, -23, 4, 10, (Color){60, 60, 66, 255});      /* the grille */
    RWheel(m, 22, -6, 6, tyre, hub);
    RRect(m, 16, -37, 3, 12, (Color){52, 52, 58, 255});       /* the exhaust */
    float puff = fmodf(sTime * 1.6f, 1.0f);
    DrawCircle((int)RX(m, 18), (int)(m->p.y - 40.0f - puff * 12.0f), 2.0f + puff * 3.0f, (Color){200, 200, 210, (unsigned char)(160.0f * (1.0f - puff))});
    /* at the wheel, somebody's dad in a toque */
    RRect(m, -16, -31, 9, 4, (Color){40, 40, 46, 255});
    RRect(m, -15, -43, 10, 13, (Color){70, 96, 150, 255});
    RRect(m, -15, -43, 10, 2, (Color){236, 196, 64, 255});
    RRect(m, -13, -51, 8, 8, (Color){236, 196, 160, 255});
    RRect(m, -14, -55, 10, 5, (Color){200, 58, 50, 255});
    RLine(m, -6, -39, 6, -33, 2, (Color){70, 96, 150, 255});
    RLine(m, 4, -36, 8, -30, 2, (Color){30, 30, 34, 255});
}

/* The arena's: a proper ice resurfacer. */
static void Resurfacer(const Rig *m) {
    Color tyre = {30, 30, 36, 255}, body = {236, 238, 242, 255}, hub = {170, 176, 190, 255};
    DrawEllipse((int)RX(m, -10), (int)m->p.y + 1, 60.0f * fabsf(m->d) + 14.0f, 6, (Color){40, 60, 110, 60});
    RRect(m, -64, -13, 14, 13, (Color){70, 76, 92, 255});      /* the conditioner (its towel is on the ice: Mat) */
    RRect(m, -52, -33, 88, 27, body);
    RRect(m, -52, -21, 88, 5, kLineBlue);
    RRect(m, -52, -15, 88, 1, kLineRed);
    RRect(m, -48, -43, 56, 10, (Color){214, 218, 226, 255});   /* the snow tank */
    RRect(m, -48, -43, 56, 2, (Color){250, 250, 252, 255});
    RRect(m, 14, -47, 22, 14, (Color){44, 48, 60, 255});       /* the cab */
    RRect(m, 18, -53, 10, 12, (Color){30, 40, 84, 255});       /* the driver */
    RRect(m, 20, -60, 7, 7, (Color){236, 196, 160, 255});
    RRect(m, 19, -63, 9, 3, (Color){200, 40, 50, 255});
    RRect(m, 26, -61, 4, 1, (Color){200, 40, 50, 255});
    RLine(m, 36, -33, 31, -54, 2, (Color){70, 74, 92, 255});
    RLine(m, 28, -48, 33, -44, 2, kInk);
    bool on = fmodf(sTime * 3.0f, 1.0f) < 0.5f;               /* the beacon */
    RRect(m, -10, -48, 5, 5, on ? (Color){255, 170, 40, 255} : (Color){150, 90, 30, 255});
    if (on) DrawCircle((int)RX(m, -8), (int)m->p.y - 46, 8, (Color){255, 170, 40, 60});
    RWheel(m, -36, -7, 8, tyre, hub);
    RWheel(m, 22, -7, 8, tyre, hub);
}

static void Machine(const Game *g, const Shape *s) {
    MachPose mp = MachineAt(s, g->phaseTicks);
    Rig m = {WorldF(mp.x, mp.y), mp.d};
    if (g->setup.venue == VENUE_ARENA) Resurfacer(&m);
    else Tractor(&m);
}

/* ------------------------------------------------------------ the puck, and everything in depth order */

static void DrawPuck(const Game *g) {
    if (g->puck.buried > 0) return;
    int held = g->puck.heldBy;
    if (held >= 0 && held < 2 && g->gk[held].present && g->gk[held].hold > 0) return; /* under the goalie's glove */
    if (g->phase == PH_FACEOFF && !Game_PuckDropped(g)) {
        int left = g->dropAt - g->phaseTicks;
        if (left > 36) return;
        Vector2 c = WorldF(0, 0);
        float h = (float)left * 3.2f;
        DrawEllipse((int)c.x, (int)c.y, 4.5f * (1.0f - left / 60.0f), 2.0f, (Color){40, 60, 110, 90});
        DrawEllipse((int)c.x, (int)(c.y - h), 4.5f, 2.6f, kInk);
        return;
    }
    if (sTrailMax == TRAIL_LONG) {
        /* a slapshot leaves a streak, not just a few dots */
        for (int k = sTrailN - 1; k > 0; k--) {
            float f = 1.0f - (float)k / (float)TRAIL_LONG;
            DrawLineEx(sPuckTrail[k], sPuckTrail[k - 1], 1.0f + 3.0f * f, (Color){20, 24, 40, (unsigned char)(120.0f * f)});
        }
    }
    for (int k = sTrailN - 1; k > 0; k--) {
        int a = sTrailMax == TRAIL_SHORT ? 90 - k * 14 : 90 - k * 84 / sTrailMax;
        Color t = {20, 24, 40, (unsigned char)a};
        DrawEllipse((int)sPuckTrail[k].x, (int)sPuckTrail[k].y, 3.5f, 2.0f, t);
    }
    Vector2 p = Render_ToScreen(g->puck.pos);
    DrawEllipse((int)p.x, (int)p.y, 4.5f, 2.6f, kInk);
    DrawEllipse((int)p.x - 1, (int)p.y - 1, 2.0f, 1.0f, (Color){90, 94, 110, 255});
}

/* skaters, goalies, nets, the puck and the break's machine, far to near */
static void Entities(const Game *g, int rivals) {
    int order[SKATERS + 6];
    float key[SKATERS + 6];
    int n = 0;
    for (int i = 0; i < SKATERS; i++) {
        if (g->sk[i].benched) continue;
        order[n] = i;
        key[n++] = Fx_ToFloat(g->sk[i].pos.y);
    }
    for (int t = 0; t < 2; t++) {
        /* a net goes down just before its goalie, who stands in front of it */
        float netKey = 0.0f;
        if (g->gk[t].present) {
            float gy = Fx_ToFloat(g->gk[t].pos.y);
            netKey = gy - 0.01f;
            order[n] = 110 + t;
            key[n++] = gy;
        }
        order[n] = 100 + t;
        key[n++] = netKey;
    }
    order[n] = 102; key[n++] = Fx_ToFloat(g->puck.pos.y) + 0.5f; /* puck */
    const Shape *shape = NULL;
    if (g->phase == PH_BREAK && g->ice.boards) {
        shape = ShapeOf(&g->ice);
        order[n] = 104;
        key[n++] = MachineAt(shape, g->phaseTicks).y;
    }
    for (int a = 1; a < n; a++) {
        for (int b = a; b > 0 && key[b - 1] > key[b]; b--) {
            float tk = key[b]; key[b] = key[b - 1]; key[b - 1] = tk;
            int to = order[b]; order[b] = order[b - 1]; order[b - 1] = to;
        }
    }
    bool rink = g->setup.venue != VENUE_POND;
    for (int k = 0; k < n; k++) {
        if (order[k] == 100) Net(0, &g->ice, rink);
        else if (order[k] == 101) Net(1, &g->ice, rink);
        else if (order[k] == 102) DrawPuck(g);
        else if (order[k] == 104) Machine(g, shape);
        else if (order[k] >= 110) GoalieSprite(g, order[k] - 110, rivals);
        else Skater_(g, order[k], rivals, false, (Vector2){0, 0}, false);
    }
}

static void Tags(const Game *g) {
    for (int i = 0; i < SKATERS; i++) {
        const Skater *s = &g->sk[i];
        if (s->player < 0 || s->benched) continue;
        Vector2 f = Render_ToScreen(s->pos);
        char tag[8];
        snprintf(tag, sizeof(tag), "P%d", s->player + 1);
        Color c = Render_PlayerColor(s->player);
        int y = (int)f.y - (s->stun > 0 ? 30 : 52);
        Render_TextCentred(tag, (int)f.x, y, 10, c);
        DrawTriangle((Vector2){f.x - 3, (float)y + 11}, (Vector2){f.x + 3, (float)y + 11}, (Vector2){f.x, (float)y + 15}, c);
        if (s->charge >= 3) {
            /* the slapshot's wind-up, full at 40 ticks (a tap is a wrist shot: no bar for that) */
            float k = fminf(1.0f, (float)s->charge / 40.0f);
            int bw = 24, bx = (int)f.x - bw / 2, by = y - 8;
            DrawRectangle(bx - 1, by - 1, bw + 2, 6, (Color){8, 10, 24, 210});
            Color fill = k >= 1.0f ? (fmodf(sTime * 8.0f, 2.0f) < 1.0f ? (Color){255, 255, 255, 255} : kGold) : c;
            DrawRectangle(bx, by, (int)((float)bw * k), 4, fill);
        }
    }
}

static void Shovellers(const Game *g, int rivals) {
    if (g->phase != PH_BREAK) return;
    float progress = (float)g->phaseTicks / 220.0f;
    if (progress > 1.15f) return;
    float x = -440.0f + 880.0f * progress;
    for (int row = 0; row < 3; row++) {
        int i = row * 2 % SKATERS;
        Vector2 feet = WorldF(x - 16.0f - (row % 2) * 10.0f, -130.0f + row * 130.0f);
        Skater_(g, i, rivals, true, feet, true);
        DrawEllipse((int)feet.x + 18, (int)feet.y, 8, 4, (Color){250, 252, 255, 220});
    }
}

/* WIND: snow driven across the ice, each flake with a streak behind it */
static void WindStreaks(const Game *g) {
    if (V2_IsZero(g->wind)) return;
    Vector2 d = WindDir(g);
    Color c = g->setup.venue == VENUE_POND ? (Color){255, 255, 255, 255} : (Color){150, 172, 206, 255};
    for (int i = 0; i < STREAK_N; i++) {
        const Streak *s = &sStreaks[i];
        Vector2 head = {s->x, s->y}, mid = {s->x - d.x * s->len * 0.4f, s->y - d.y * s->len * 0.4f}, tail = {s->x - d.x * s->len, s->y - d.y * s->len};
        DrawLineEx(mid, tail, 1.0f, Alpha(c, 0.22f));
        DrawLineEx(head, mid, 1.5f, Alpha(c, 0.5f));
        DrawRectangle((int)head.x - 1, (int)head.y - 1, 2 + (i % 3 == 0), 2 + (i % 3 == 0), Alpha(c, 0.9f));
    }
}

/* ------------------------------------------------------------ HUD */

#define BOARD_X 250
#define BOARD_Y 8
#define BOARD_W 460
#define BOARD_H 58

static const char *SideName(int side, const RenderInfo *ri) {
    if (side == 1 && ri && ri->rivalName && ri->rivalName[0]) return ri->rivalName;
    return Team_ForSide(side, ri ? ri->rivals : 0)->name;
}

static void PeriodLine(const Game *g, char *line, size_t size) {
    if (Game_SuddenDeath(g)) {
        snprintf(line, size, "SUDDEN DEATH");
        return;
    }
    int secs = Game_ClockSeconds(g);
    static const char *const kPeriod[3] = {"1ST", "2ND", "3RD"};
    int p = g->period - 1;
    snprintf(line, size, "%s  %d:%02d", kPeriod[p < 0 ? 0 : p > 2 ? 2 : p], secs / 60, secs % 60);
}

/* the pond's wooden sign */
static void Scoreboard(const Game *g, const RenderInfo *ri) {
    int rivals = ri ? ri->rivals : 0;
    int x = BOARD_X, w = BOARD_W, y = BOARD_Y, h = BOARD_H;
    DrawRectangle(x + 3, y + 4, w, h, (Color){0, 0, 0, 90});
    DrawRectangle(x, y, w, h, (Color){120, 78, 46, 255});
    for (int k = 1; k < 3; k++) DrawLine(x + 4, y + k * h / 3, x + w - 4, y + k * h / 3, (Color){96, 60, 34, 255});
    DrawRectangleLines(x, y, w, h, (Color){70, 44, 26, 255});
    DrawRectangle(x - 2, y - 2, 6, 6, (Color){200, 200, 210, 255});
    DrawRectangle(x + w - 4, y - 2, 6, 6, (Color){200, 200, 210, 255});
    const TeamStyle *a = Team_ForSide(0, rivals), *b = Team_ForSide(1, rivals);
    const char *bn = SideName(1, ri);
    char num[12];
    DrawRectangle(x + 12, y + 12, 10, 10, RGB(a->jersey));
    Render_Text(a->name, x + 28, y + 11, 10, (Color){250, 240, 220, 255});
    int bw = Render_TextWidth(bn, 10);
    DrawRectangle(x + w - 22, y + 12, 10, 10, RGB(b->jersey));
    Render_Text(bn, x + w - 28 - bw, y + 11, 10, (Color){250, 240, 220, 255});
    snprintf(num, sizeof(num), "%d", g->score[0]);
    Render_TextCentred(num, x + w / 2 - 40, y + 8, 30, kGold);
    snprintf(num, sizeof(num), "%d", g->score[1]);
    Render_TextCentred(num, x + w / 2 + 40, y + 8, 30, kGold);
    Render_TextCentred("-", x + w / 2, y + 12, 20, (Color){250, 240, 220, 255});
    char line[40];
    PeriodLine(g, line, sizeof(line));
    Render_TextCentred(line, x + w / 2, y + 40, 10, (Color){250, 240, 220, 255});
}

/* the town rink's old scoreboard, hung on chains from the roof */
static void RinkBoardFrame(void) {
    int x = BOARD_X, y = BOARD_Y, w = BOARD_W, h = BOARD_H;
    for (int k = 0; k < 2; k++) {
        int cx = k == 0 ? x + 50 : x + w - 53;
        for (int yy = -1; yy < y; yy += 4) DrawRectangle(cx, yy, 3, 3, (Color){74, 74, 80, 255});
    }
    DrawRectangle(x + 3, y + 4, w, h, (Color){0, 0, 0, 90});
    DrawRectangle(x, y, w, h, (Color){92, 62, 38, 255});
    DrawRectangleLines(x, y, w, h, (Color){54, 36, 22, 255});
    DrawRectangle(x + 5, y + 5, w - 10, h - 10, (Color){18, 22, 20, 255});
    for (int k = 0; k < 4; k++) DrawRectangle(k % 2 ? x + w - 4 : x + 2, k < 2 ? y + 2 : y + h - 4, 2, 2, (Color){214, 200, 160, 255});
}

/* the arena's big screen, hanging over centre ice */
static void JumboFrame(void) {
    int x = BOARD_X, y = BOARD_Y, w = BOARD_W, h = BOARD_H;
    DrawRectangle(x + 70, 0, 2, y, (Color){60, 64, 84, 255});
    DrawRectangle(x + w - 72, 0, 2, y, (Color){60, 64, 84, 255});
    DrawRectangle(x - 10, y - 6, w + 20, h + 20, (Color){16, 18, 26, 255});
    DrawRectangle(x - 10, y - 6, w + 20, 2, (Color){74, 80, 106, 255});
    DrawRectangleGradientV(x, y, w, h, (Color){18, 30, 70, 255}, (Color){8, 12, 34, 255});
    DrawRectangleLines(x - 1, y - 1, w + 2, h + 2, (Color){90, 170, 255, 150});
    for (int i = 0; i < w; i += 10) {
        float v = sinf((float)i * 0.05f - sTime * (3.0f + 6.0f * sHype));
        Color c = v > 0.3f ? (Color){90, 170, 255, 255} : (v < -0.5f ? (Color){255, 255, 255, 255} : (Color){30, 50, 110, 255});
        DrawRectangle(x + i + 1, y + h + 4, 8, 4, c);
    }
}

static void BoardMatch(const Game *g, const RenderInfo *ri, Color label, Color digits, Color clock) {
    int x = BOARD_X, y = BOARD_Y, w = BOARD_W;
    int rivals = ri ? ri->rivals : 0;
    const TeamStyle *a = Team_ForSide(0, rivals), *b = Team_ForSide(1, rivals);
    const char *bn = SideName(1, ri);
    DrawRectangle(x + 12, y + 12, 10, 10, RGB(a->jersey));
    Render_Text(a->name, x + 28, y + 11, 10, label);
    DrawRectangle(x + w - 22, y + 12, 10, 10, RGB(b->jersey));
    Render_Text(bn, x + w - 28 - Render_TextWidth(bn, 10), y + 11, 10, label);
    char num[12];
    for (int t = 0; t < 2; t++) {
        int cx = x + w / 2 + (t == 0 ? -40 : 40);
        snprintf(num, sizeof(num), "%d", g->score[t]);
        DrawRectangle(cx - 17, y + 6, 34, 32, Alpha(digits, 0.12f));
        Render_TextCentred(num, cx, y + 8, 30, digits);
    }
    Render_TextCentred("-", x + w / 2, y + 12, 20, label);
    char line[40];
    PeriodLine(g, line, sizeof(line));
    Render_TextCentred(line, x + w / 2, y + 40, 10, clock);
}

static void BoardShootout(const Game *g, Color label, Color digits, Color accent) {
    int x = BOARD_X, y = BOARD_Y;
    char buf[32];
    Render_Text("SHOOTOUT", x + 16, y + 11, 10, accent);
    int shot = g->phase == PH_SO_READY ? g->soAttempts + 1 : g->soAttempts;
    snprintf(buf, sizeof(buf), "Shot %d", shot > 0 ? shot : 1);
    Render_Text(buf, x + 16, y + 29, 10, label);
    Render_TextCentred("GOALS", x + 186, y + 6, 10, label);
    snprintf(buf, sizeof(buf), "%d", g->score[0]);
    Render_TextCentred(buf, x + 186, y + 19, 30, digits);
    Render_TextCentred("STREAK", x + 286, y + 6, 10, label);
    snprintf(buf, sizeof(buf), "%d", g->soStreak);
    Render_TextCentred(buf, x + 286, y + 19, 20, digits);
    snprintf(buf, sizeof(buf), "best %d", g->soBestStreak);
    Render_TextCentred(buf, x + 286, y + 41, 10, label);
    Render_TextCentred("MISSES", x + 392, y + 6, 10, label);
    for (int k = 0; k < 3; k++) {
        Vector2 c = {(float)(x + 370 + 22 * k), (float)(y + 33)};
        bool miss = k < g->soMisses;
        DrawCircleV(c, 8.0f, miss ? (Color){206, 46, 50, 255} : (Color){0, 0, 0, 110});
        DrawCircleLinesV(c, 8.0f, label);
        if (miss) {
            DrawLineEx((Vector2){c.x - 4, c.y - 4}, (Vector2){c.x + 4, c.y + 4}, 2, WHITE);
            DrawLineEx((Vector2){c.x + 4, c.y - 4}, (Vector2){c.x - 4, c.y + 4}, 2, WHITE);
        }
    }
}

/* the day's twist, with a little picture of it (the wind's arrow points its way) */
static void TwistBadge(const Game *g, int x, int y) {
    int tw = g->setup.twist;
    if (tw <= TWIST_NONE || tw >= TWIST_COUNT) return;
    const char *name = Game_TwistName(tw);
    int w = Render_TextWidth(name, 10) + 30, h = 16;
    DrawRectangle(x, y, w, h, (Color){14, 18, 44, 225});
    DrawRectangleLines(x, y, w, h, Alpha(kGold, 0.8f));
    Vector2 c = {(float)x + 10.0f, (float)y + 8.0f};
    if (tw == TWIST_WIND) {
        Vector2 d = WindDir(g), p = {-d.y, d.x};
        Vector2 tip = {c.x + d.x * 6.0f, c.y + d.y * 6.0f}, tail = {c.x - d.x * 5.0f, c.y - d.y * 5.0f};
        DrawLineEx(tail, tip, 2, WHITE);
        Tri(tip, (Vector2){tip.x - d.x * 4.0f + p.x * 3.0f, tip.y - d.y * 4.0f + p.y * 3.0f}, (Vector2){tip.x - d.x * 4.0f - p.x * 3.0f, tip.y - d.y * 4.0f - p.y * 3.0f}, WHITE);
    } else if (tw == TWIST_BLIZZARD) {
        for (int k = 0; k < 3; k++) {
            float a = (float)k * 1.047f + 0.52f;
            DrawLineEx((Vector2){c.x - cosf(a) * 5.0f, c.y - sinf(a) * 5.0f}, (Vector2){c.x + cosf(a) * 5.0f, c.y + sinf(a) * 5.0f}, 1, WHITE);
        }
    } else if (tw == TWIST_GLASS_ICE) {
        Quad((Vector2){c.x, c.y - 5}, (Vector2){c.x + 5, c.y}, (Vector2){c.x, c.y + 5}, (Vector2){c.x - 5, c.y}, (Color){170, 220, 255, 255});
    } else {
        DrawRectangleLines((int)c.x - 4, (int)c.y - 3, 8, 6, (Color){255, 120, 120, 255});
    }
    Render_Text(name, x + 20, y + 3, 10, kGold);
}

/* the extra line under the scoreboard */
static void InfoLine(const RenderInfo *ri, int y) {
    if (!ri || !ri->banner || !ri->banner[0]) return;
    Backing(SCREEN_W / 2, y, Render_TextWidth(ri->banner, 10) + 24, 16);
    Render_TextCentred(ri->banner, SCREEN_W / 2, y + 3, 10, kGold);
}

static void Hud(const Game *g, const RenderInfo *ri) {
    int venue = g->setup.venue;
    bool so = g->setup.mode == MODE_SHOOTOUT, arena = venue == VENUE_ARENA;
    if (venue == VENUE_POND && !so) {
        Scoreboard(g, ri);
    } else {
        if (arena) JumboFrame();
        else RinkBoardFrame();
        if (so) BoardShootout(g, arena ? (Color){170, 200, 255, 255} : kPaper, arena ? WHITE : kAmber, kGold);
        else BoardMatch(g, ri, arena ? (Color){220, 232, 255, 255} : kPaper, arena ? WHITE : kAmber, arena ? kAmber : (Color){255, 100, 76, 255});
    }
    int under = BOARD_Y + BOARD_H + (arena ? 18 : 6);
    if (so) {
        TwistBadge(g, BOARD_X + 10, under);
        if (g->setup.twist != TWIST_NONE) under += 20;
    } else {
        TwistBadge(g, BOARD_X + 10, BOARD_Y + 38);
    }
    InfoLine(ri, under);
}

/* ------------------------------------------------------------ banners */

static const char *GoalieName(int t, const RenderInfo *ri) {
    if (t == 1 && ri && ri->rivalName && ri->rivalName[0]) return NULL; /* a friend's crew: we don't know their goalie */
    return Team_ForSide(t, ri ? ri->rivals : 0)->goalie;
}

static void SubLine(const char *msg, int y, Color c) {
    Backing(SCREEN_W / 2, y, Render_TextWidth(msg, 20) + 30, 32);
    Render_TextCentred(msg, SCREEN_W / 2, y + 6, 20, c);
}

static void ShootoutBanners(const Game *g, const RenderInfo *ri) {
    if (g->phase == PH_SO_READY) {
        BannerText("GET SET", SCREEN_W / 2, 150, 40, (Color){240, 248, 255, 255});
        const Skater *s = &g->sk[0];
        if (s->player >= 0) {
            char msg[80];
            const char *key = ri ? ri->shootKey[s->player] : NULL;
            if (key && key[0]) snprintf(msg, sizeof(msg), "P%d: skate in and shoot (%s)", s->player + 1, key);
            else snprintf(msg, sizeof(msg), "P%d: skate in and shoot", s->player + 1);
            Render_TextCentred(msg, SCREEN_W / 2, 584, 20, Render_PlayerColor(s->player));
        }
        return;
    }
    if (g->phase != PH_SO_RESULT) return;
    const Goalie *k = &g->gk[1];
    bool saved = k->hold > 0 || k->recover > 0 || sSoSaved;
    bool scored = !saved && g->soStreak > 0;
    float t = (float)g->phaseTicks;
    int size = t < 10 ? 40 + (int)t * 3 : 70;
    char msg[80];
    const char *who = GoalieName(1, ri);
    if (scored) {
        BannerText("SCORE!", SCREEN_W / 2, 124, size, kGold);
        if (g->soStreak == 1) snprintf(msg, sizeof(msg), "That's one");
        else if (g->soStreak == g->soBestStreak) snprintf(msg, sizeof(msg), "%d in a row: your best yet", g->soStreak);
        else snprintf(msg, sizeof(msg), "%d in a row", g->soStreak);
    } else if (saved) {
        BannerText("SAVED", SCREEN_W / 2, 124, size, (Color){150, 210, 255, 255});
        snprintf(msg, sizeof(msg), k->hold > 0 ? "%s smothers it" : "%s gets a pad on it", who ? who : "The goalie");
    } else {
        BannerText("MISSED", SCREEN_W / 2, 124, size, (Color){255, 150, 130, 255});
        snprintf(msg, sizeof(msg), "Miss %d of 3", g->soMisses);
    }
    if (!scored && g->soMisses >= 3) snprintf(msg, sizeof(msg), "Three misses: that's the shootout");
    if (t > 16) SubLine(msg, 206, (Color){255, 255, 255, 255});
}

static void Banners(const Game *g, const RenderInfo *ri) {
    int rivals = ri ? ri->rivals : 0;
    if (g->setup.mode == MODE_SHOOTOUT) {
        ShootoutBanners(g, ri);
        return;
    }
    if (g->phase == PH_FACEOFF) {
        if (!Game_PuckDropped(g)) {
            if (Game_SuddenDeath(g) && g->phaseTicks < g->dropAt - 36) BannerText("NEXT GOAL WINS", SCREEN_W / 2, 150, 30, kGold);
            else BannerText(g->dropAt - g->phaseTicks > 36 ? "GET READY" : "WATCH THE PUCK", SCREEN_W / 2, 150, 30, (Color){240, 248, 255, 255});
        } else if (g->phaseTicks - g->dropAt < 30) {
            BannerText("GO!", SCREEN_W / 2, 140, 50, kGold);
        }
        for (int t = 0; t < 2; t++) {
            if (g->falseStart[t] <= 0) continue;
            Vector2 f = Render_ToScreen(g->sk[g->centre[t]].pos);
            Render_TextCentred("TOO EARLY!", (int)f.x, (int)f.y - 64, 10, (Color){255, 120, 120, 255});
        }
        int line = 0;
        for (int t = 0; t < 2 && ri; t++) {
            const Skater *c = &g->sk[g->centre[t]];
            if (c->player < 0 || !ri->passKey[c->player]) continue;
            char msg[64];
            snprintf(msg, sizeof(msg), "P%d: press %s when the puck drops", c->player + 1, ri->passKey[c->player]);
            Render_TextCentred(msg, SCREEN_W / 2, 584 + line * 22, 20, Render_PlayerColor(c->player));
            line++;
        }
    }
    if (g->phase == PH_GOAL) {
        float t = (float)g->phaseTicks;
        int size = t < 10 ? 40 + (int)t * 4 : 80;
        Color tc = g->lastGoalTeam >= 0 ? Render_TeamColor(g->lastGoalTeam, rivals) : kGold;
        BannerText("GOAL!", SCREEN_W / 2, 120, size, tc);
        if (t > 20) {
            char msg[80];
            if (g->lastScorer >= 0) {
                char who[24];
                snprintf(who, sizeof(who), "%s", Render_SkaterNameIn(g, g->lastScorer, ri));
                if (g->lastAssist >= 0) snprintf(msg, sizeof(msg), "%s scores, from %s", who, Render_SkaterNameIn(g, g->lastAssist, ri));
                else snprintf(msg, sizeof(msg), "%s scores", who);
            } else {
                snprintf(msg, sizeof(msg), "Into their own net!");
            }
            Backing(SCREEN_W / 2, 200, Render_TextWidth(msg, 20) + 30, 32);
            Render_TextCentred(msg, SCREEN_W / 2, 206, 20, (Color){255, 255, 255, 255});
        }
    }
    if (g->phase == PH_STOPPAGE) {
        BannerText("WHISTLE", SCREEN_W / 2, 140, 40, (Color){240, 248, 255, 255});
        int t = g->puck.heldBy;
        const char *who = t >= 0 && t < 2 ? GoalieName(t, ri) : NULL;
        char msg[64];
        snprintf(msg, sizeof(msg), "%s covers it", who ? who : "The goalie");
        if (g->phaseTicks > 6) SubLine(msg, 190, kGold);
    }
    if (g->phase == PH_BREAK) {
        BannerText(g->setup.venue == VENUE_POND ? "SHOVEL BREAK" : "RESURFACING", SCREEN_W / 2, 120, 40, (Color){240, 248, 255, 255});
        const char *next = g->period >= g->setup.periods ? "Sudden death next: first goal wins" : "Clearing the ice for the next period";
        Backing(SCREEN_W / 2, 166, Render_TextWidth(next, 20) + 30, 32);
        Render_TextCentred(next, SCREEN_W / 2, 172, 20, kGold);
    }
}

/* ------------------------------------------------------------ the big screen's replay, and online */

/* The frame being drawn is an old state shown again: put it on the big
 * screen, washed out a little and scanlined, with a REPLAY tag and a bar
 * for how far through it is. */
static void ReplayFrame(const RenderInfo *ri) {
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){118, 126, 140, 70});
    if (sScanReady) DrawTexturePro(sScan, (Rectangle){0, 0, (float)SCREEN_W, (float)SCREEN_H}, (Rectangle){0, 0, (float)SCREEN_W, (float)SCREEN_H}, (Vector2){0, 0}, 0.0f, WHITE);
    const int b = 16, foot = 46;
    Color frame = {16, 18, 26, 255};
    DrawRectangle(0, 0, SCREEN_W, b, frame);
    DrawRectangle(0, SCREEN_H - foot, SCREEN_W, foot, frame);
    DrawRectangle(0, 0, b, SCREEN_H, frame);
    DrawRectangle(SCREEN_W - b, 0, b, SCREEN_H, frame);
    DrawRectangleLines(b - 1, b - 1, SCREEN_W - 2 * b + 2, SCREEN_H - b - foot + 2, (Color){90, 170, 255, 170});
    for (int x = 30; x < SCREEN_W - 20; x += 30) DrawRectangle(x, 6, 3, 3, (Color){60, 70, 100, 255});
    float pulse = 0.5f + 0.5f * sinf(sTime * 6.0f);
    DrawRectangle(b + 12, b + 12, 136, 32, (Color){12, 14, 24, 255});
    DrawRectangleLines(b + 12, b + 12, 136, 32, Alpha(kLineRed, 0.6f + 0.4f * pulse));
    DrawCircle(b + 30, b + 28, 8, Alpha(kLineRed, 0.45f + 0.55f * pulse));
    Render_Text("REPLAY", b + 46, b + 18, 20, Alpha(WHITE, 0.85f + 0.15f * pulse));
    int pw = 420, px = (SCREEN_W - pw) / 2, py = SCREEN_H - foot + 18;
    DrawRectangle(px - 2, py - 2, pw + 4, 12, (Color){44, 48, 66, 255});
    DrawRectangle(px, py, (int)((float)pw * Clamp01(ri->replayProgress)), 8, kLineRed);
}

static void Online(const RenderInfo *ri) {
    if (!ri) return;
    if (ri->pingMs >= 0) {
        char t[32];
        snprintf(t, sizeof(t), "online %d ms", ri->pingMs);
        int w = Render_TextWidth(t, 10) + 26, x = SCREEN_W - w - 10, y = SCREEN_H - 28;
        DrawRectangle(x, y, w, 18, (Color){8, 12, 34, 190});
        Color dot = ri->pingMs < 80 ? (Color){84, 214, 120, 255} : (ri->pingMs < 160 ? (Color){250, 210, 60, 255} : (Color){238, 72, 72, 255});
        DrawCircle(x + 10, y + 9, 4, dot);
        Render_Text(t, x + 18, y + 4, 10, (Color){240, 244, 255, 255});
    }
    if (ri->waiting) {
        const char *m = "Waiting for the other player...";
        Backing(SCREEN_W / 2, 300, Render_TextWidth(m, 20) + 40, 40);
        Render_TextCentred(m, SCREEN_W / 2, 310, 20, Alpha(WHITE, 0.75f + 0.25f * sinf(sTime * 4.0f)));
    }
}

/* ------------------------------------------------------------ the whole scene */

void Render_Game(const Game *g, const RenderInfo *ri) {
    int rivals = ri ? ri->rivals : 0;
    int venue = g->setup.venue;
    const Shape *s = ShapeOf(&g->ice);
    if (venue == VENUE_POND) {
        Sky();
        DrawParts(true);
        Town();
        Snowfield(s);
        StringLights();
        PondIce(g, s);
    } else {
        if (venue == VENUE_ARENA) ArenaBowl(rivals);
        else RinkHall();
        RinkIce(g, &g->ice, s, venue);
    }
    if (g->setup.twist == TWIST_GLASS_ICE) GlassSheen(s, venue);
    if (venue != VENUE_POND) {
        Boards(s, venue);
        if (venue == VENUE_ARENA) Spotlights(rivals);
        else LampLight(s);
    }
    Entities(g, rivals);
    if (venue == VENUE_POND) Shovellers(g, rivals);
    DrawParts(false);
    if (venue == VENUE_POND) {
        for (int i = 0; i < FLAKE_N; i++) {
            float heavy = Fx_ToFloat(g->snow);
            if (i > (int)(FLAKE_N * (0.45f + 0.55f * heavy))) break;
            DrawRectangleV((Vector2){sFlakes[i].x, sFlakes[i].y}, (Vector2){sFlakes[i].size, sFlakes[i].size}, (Color){255, 255, 255, 210});
        }
        if (g->setup.twist == TWIST_BLIZZARD) DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){236, 242, 255, (unsigned char)(24.0f + 40.0f * Fx_ToFloat(g->snow))});
    }
    WindStreaks(g);
    if (venue == VENUE_ARENA) NearCrowd();
    Tags(g);
    if (ri && ri->replay) {
        ReplayFrame(ri);
    } else {
        Hud(g, ri);
        Banners(g, ri);
    }
    Online(ri);
}

void Render_VenueBackdrop(int venue) {
    if (venue != VENUE_RINK && venue != VENUE_ARENA) {
        Render_Backdrop(true);
        return;
    }
    Ice ice = IceOf(venue);
    const Shape *s = ShapeOf(&ice);
    if (venue == VENUE_ARENA) ArenaBowl(0);
    else RinkHall();
    RinkIce(NULL, &ice, s, venue);
    Boards(s, venue);
    if (venue == VENUE_ARENA) {
        Spotlights(0);
        NearCrowd();
    } else {
        LampLight(s);
    }
}
