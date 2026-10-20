/* The button kit's gallery, for the screenshot harness (SHOTS_GALLERY=1):
 * every kind of control in each of its five looks, side by side, and below
 * them a live set that the script can point at, click, drag, scroll and
 * drive with keys or the pad, all through the launcher's own input reader
 * and kit. It only exists so the controls the launcher does not use yet
 * (switch, ten blocks, tabs) can be looked at before a screen needs them. */
#include "raylib.h"
#include "winscale.h"
#include "kit.h"
#include "kitdraw.h"
#include "palette.h"
#include "pixtext.h"
#include "input.h"
#include <stdio.h>
#include <stdlib.h>

int gallery_main(void);

static const char *const kTabs[] = {"Player", "Sound", "Screen"};
static const char *const kTabsSmall[] = {"One", "Two"};
static const char *const kLookNames[] = {"Normal", "Hover", "Focused", "Pressed", "Disabled"};

typedef struct {
    int toggle, stepper, slider, tab, presses;
} Live;

/* One example of each kind, placed at x, y. */
static KitItem Sample(int row, float x, float y) {
    KitList l;
    Kit_Clear(&l, 0);
    KitItem *it = NULL;
    switch (row) {
        case 0: it = Kit_Add(&l, 1, KIT_BUTTON, (KitRect){x, y, 144, 36}, "Play"); break;
        case 1:
            it = Kit_Add(&l, 1, KIT_BUTTON, (KitRect){x, y + 6, 72, 24}, "Back");
            it->style = KIT_STYLE_SMALL;
            it->icon = KIT_ICON_BACK;
            it->textSize = 8;
            break;
        case 2:
            it = Kit_Add(&l, 1, KIT_ROW, (KitRect){x, y, 144, 36}, "Row");
            snprintf(it->key, sizeof(it->key), "L");
            break;
        case 3: it = Kit_Add(&l, 1, KIT_TOGGLE, (KitRect){x + 12, y + 4, 120, 28}, ""); it->value = 1; it->textSize = 8; break;
        case 4: it = Kit_Add(&l, 1, KIT_STEPPER, (KitRect){x, y + 2, Kit_TextWidth("< Two >", 16) + 32.0f, 32}, "Two"); it->max = 3; break;
        case 5: it = Kit_Add(&l, 1, KIT_SLIDER, (KitRect){x, y + 2, KIT_BLOCK_PITCH * 9, 32}, ""); it->max = 8; it->value = 5; break;
        case 6:
            it = Kit_Add(&l, 1, KIT_TABS, (KitRect){x, y, 0, 36}, "");
            it->cells = kTabsSmall;
            it->cellCount = 2;
            it->textSize = 8;
            break;
        case 7: it = Kit_Add(&l, 1, KIT_BUTTON, (KitRect){x + 60, y + 6, 24, 24}, ""); it->style = KIT_STYLE_CLOSE; break;
        default:
            it = Kit_Add(&l, 1, KIT_BUTTON, (KitRect){x + 30, y + 8, Kit_TextWidth("NEW VERSION", 8) + 16.0f, 20}, "NEW VERSION");
            it->style = KIT_STYLE_PILL;
            it->textSize = 8;
            break;
    }
    return *it;
}

static void DrawMatrix(void) {
    static const char *const rows[] = {"Button", "Small", "Row", "Switch", "Stepper", "Blocks", "Tabs", "Close", "Notice"};
    for (int c = 0; c < 5; c++) Pix_Text(kLookNames[c], 120 + c * 168, 20, 8, kTextDim);
    for (int r = 0; r < 9; r++) {
        float y = 40.0f + (float)r * 44.0f;
        Pix_Text(rows[r], 16, (int)y + 14, 8, kTextDim);
        for (int c = 0; c < 5; c++) {
            KitItem it = Sample(r, 120.0f + (float)c * 168.0f, y);
            if (c == KIT_LOOK_DISABLED) it.flags |= KIT_DISABLED;
            int hoverCell = c == KIT_LOOK_HOVER ? (it.kind == KIT_TOGGLE ? 0 : 1) : KIT_NONE;
            int pressedCell = c == KIT_LOOK_PRESSED ? 0 : KIT_NONE;
            if (it.kind == KIT_BUTTON || it.kind == KIT_ROW || it.kind == KIT_SLIDER) hoverCell = c == KIT_LOOK_HOVER ? 0 : KIT_NONE;
            KitDraw_Item(&it, (KitLook)c, hoverCell, pressedCell);
        }
    }
}

static void BuildLive(KitList *l, const Live *v) {
    Kit_Clear(l, KIT_NAV_X | KIT_NAV_Y);
    float y = 488.0f;
    Kit_Add(l, 1, KIT_BUTTON, (KitRect){120, y, 160, 40}, "Press me");
    KitItem *t = Kit_Add(l, 2, KIT_TOGGLE, (KitRect){312, y + 4, 160, 32}, "");
    t->value = v->toggle;
    KitItem *s = Kit_Add(l, 3, KIT_STEPPER, (KitRect){504, y + 4, Kit_TextWidth("< 3 >", 16) + 32.0f, 32}, "");
    snprintf(s->label, sizeof(s->label), "%d", v->stepper);
    s->value = v->stepper;
    s->max = 9;
    KitItem *b = Kit_Add(l, 4, KIT_SLIDER, (KitRect){680, y + 4, KIT_BLOCK_PITCH * 11, 32}, "");
    b->value = v->slider;
    b->max = 10;
    y += 64.0f;
    KitItem *tabs = Kit_Add(l, 5, KIT_TABS, (KitRect){120, y, 0, 40}, "");
    tabs->cells = kTabs;
    tabs->cellCount = 3;
    tabs->value = v->tab;
    KitItem *off = Kit_Add(l, 6, KIT_BUTTON, (KitRect){600, y, 200, 40}, "Not now");
    Kit_Disable(off, "Waiting for something else");
}

static void Apply(Live *v, KitEvent ev) {
    if (ev.type == KIT_EV_PRESS && ev.id == 1) v->presses++;
    if (ev.type != KIT_EV_CHANGE) return;
    if (ev.id == 2) v->toggle = ev.value;
    if (ev.id == 3) v->stepper = ev.value;
    if (ev.id == 4) v->slider = ev.value;
    if (ev.id == 5) v->tab = ev.value;
}

static void AssetsDir(char *out, size_t size) {
    const char *xdg = getenv("XDG_DATA_HOME");
    snprintf(out, size, "%s/ghost-launcher/assets", xdg ? xdg : ".");
}

int gallery_main(void) {
    SetConfigFlags(FLAG_WINDOW_HIGHDPI);
    InitWindow(KIT_SCREEN_W, KIT_SCREEN_H, "kit gallery");
    Win_Init(KIT_SCREEN_W, KIT_SCREEN_H);
    SetTargetFPS(60);
    char assets[512];
    AssetsDir(assets, sizeof(assets));
    Pix_Load(assets);
    InputState ist;
    Input_Init(&ist);
    KitState ks;
    Kit_Init(&ks);
    Live live = {1, 3, 7, 1, 0};
    while (!WindowShouldClose()) {
        InputRaw raw;
        InputRL_Read(&raw);
        Input in;
        Input_Step(&ist, &raw, GetFrameTime(), &in);
        Win_Update();
        KitList l;
        BuildLive(&l, &live);
        Apply(&live, Kit_Update(&ks, &l, &in));
        BuildLive(&l, &live);
        Win_BeginFrame();
        ClearBackground(kWallBottom);
        DrawMatrix();
        Pix_Text("Live: point, click, drag, scroll, or use the keys and the pad", 16, 460, 8, kTextDim);
        KitView kv = {&l, &ks, in.lastDevice, in.hintDevice};
        KitDraw_List(&kv);
        char note[96];
        snprintf(note, sizeof(note), "Pressed %d times    %s", live.presses, Kit_Reason(&ks, &l, in.lastDevice));
        Pix_Text(note, 120, 616, 8, kTextDim);
        Win_EndFrame();
    }
    Pix_Unload();
    Win_Shutdown();
    CloseWindow();
    return 0;
}
