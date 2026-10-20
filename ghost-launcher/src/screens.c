#include "screens.h"
#include "hall.h"
#include <stdio.h>
#include <string.h>

static const struct {
    const char *label;
    KeyAction key; /* the hall's key for it, shown at the row's right */
} kMenu[AM_COUNT] = {
    [AM_HALL] = {"Back to the hall", KA_MENU_CLOSE},
    /* Shift+D opens it too, but its cap would run into the words. */
    [AM_DAILY] = {"Daily challenge", KA_NONE},
    [AM_SETTINGS] = {"Settings", KA_OPEN_SETTINGS},
    [AM_STATS] = {"Stats", KA_OPEN_STATS},
    [AM_TROPHIES] = {"Trophies", KA_OPEN_TROPHIES},
    [AM_UPDATES] = {"Updates", KA_OPEN_UPDATES},
    [AM_LEAVE] = {"Leave the arcade", KA_NONE},
};

const char *Screens_MenuLabel(int item) { return (item >= 0 && item < AM_COUNT) ? kMenu[item].label : ""; }

const char *Screens_MenuKey(int item, InputDevice device) {
    static char key[AM_COUNT][16];
    if (item < 0 || item >= AM_COUNT) return "";
    key[item][0] = '\0';
    if (kMenu[item].key == KA_NONE) return key[item];
    bool pad = device == DEV_PAD, isPad = false;
    Keymap_Cap(kMenu[item].key, pad ? DEV_PAD : DEV_KEYBOARD, key[item], sizeof(key[item]), &isPad);
    if (pad && !isPad) key[item][0] = '\0'; /* a key would be no use to someone on the pad */
    return key[item];
}

int Screens_MenuItemFor(int widgetId) {
    int item = widgetId - WID_MENU_ITEM;
    return (item >= 0 && item < AM_COUNT) ? item : -1;
}

KeyAction Screens_Shortcut(const Input *in) {
    static const KeyAction kOpen[] = {KA_OPEN_SETTINGS, KA_OPEN_GAME_NAME, KA_OPEN_NAME, KA_OPEN_ONLINE,
                                      KA_OPEN_STATS, KA_OPEN_TROPHIES, KA_OPEN_UPDATES, KA_OPEN_DAILY};
    for (size_t i = 0; i < sizeof(kOpen) / sizeof(kOpen[0]); i++) if (Keymap_Pressed(kOpen[i], in)) return kOpen[i];
    return KA_NONE;
}

/* The header's left part ends after the title, "GHOST LAUNCHER" at x 68. */
static int TitleEnd(void) { return 68 + Kit_TextWidth("GHOST LAUNCHER", 16); }

/* Admin's badge sits 20 px after the title, 16 px wider than its word. */
int Screens_HeaderLeft(bool admin) {
    return admin ? TitleEnd() + 20 + Kit_TextWidth("ADMIN", 8) + 16 + 16 : TitleEnd() + 24;
}

#define CHIP_PAD 8

void Screens_PlayingAs(PlayingAs *p, const char *arcadeName, bool admin, bool updateBehind) {
    memset(p, 0, sizeof(*p));
    int chipRight = (updateBehind ? PILL_X : MENU_CHIP_X) - HEADER_GAP;
    int rightX = chipRight - CHIP_PAD, room = rightX - (Screens_HeaderLeft(admin) + CHIP_PAD);
    p->y = HEADER_TEXT_Y;
    if (!arcadeName || arcadeName[0] == '\0') {
        snprintf(p->name, sizeof(p->name), "Set your name"); /* an empty name is a prompt to act, not a blank */
        p->empty = true;
    } else {
        int n = 0;
        while (n < 24 && arcadeName[n]) n++; /* the header has always shown 24 at most */
        snprintf(p->name, sizeof(p->name), "%.*s", n, arcadeName);
        /* "Playing as" goes first when room is short: the name matters more. */
        bool label = Kit_TextWidth("Playing as ", 8) + Kit_TextWidth(p->name, 8) <= room;
        while (!label && n > 1 && Kit_TextWidth(p->name, 8) > room) snprintf(p->name, sizeof(p->name), "%.*s...", --n, arcadeName);
        if (label) snprintf(p->label, sizeof(p->label), "Playing as ");
    }
    p->nameX = rightX - Kit_TextWidth(p->name, 8);
    p->labelX = p->nameX - Kit_TextWidth(p->label, 8);
    p->r = (KitRect){(float)(p->labelX - CHIP_PAD), (float)CLOSE_BTN_Y, (float)(chipRight - p->labelX + CHIP_PAD),
                     (float)CLOSE_BTN_SIZE};
}

/* Added last, so it sits on top of everything, a dialog's veil included. */
void Screens_AddClose(KitList *l) {
    KitItem *it = Kit_Add(l, WID_CLOSE, KIT_BUTTON, (KitRect){CLOSE_BTN_X, CLOSE_BTN_Y, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE}, "");
    if (!it) return;
    it->style = KIT_STYLE_CLOSE;
    it->flags |= KIT_NOFOCUS;
}

/* Mouse only: the keyboard and pad have Esc and B. */
void Screens_AddBack(KitList *l) {
    KitItem *it = Kit_Add(l, WID_BACK, KIT_BUTTON, (KitRect){BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H}, "Back");
    if (!it) return;
    it->style = KIT_STYLE_SMALL;
    it->icon = KIT_ICON_BACK;
    it->textSize = 8;
    it->flags |= KIT_NOFOCUS;
}

/* The name in the header opens Settings > Player; its screen draws it. */
static void AddPlayingAs(KitList *l, const HallView *v) {
    PlayingAs pa;
    Screens_PlayingAs(&pa, v->arcadeName, v->admin, v->updateBehind);
    KitItem *it = Kit_Add(l, WID_PLAYING_AS, KIT_BUTTON, pa.r, pa.name);
    if (!it) return;
    it->textSize = 8;
    it->flags |= KIT_NOFOCUS | KIT_SELFDRAWN;
}

static KitItem *MouseButton(KitList *l, int id, KitRect r, const char *label, KitStyle style) {
    KitItem *it = Kit_Add(l, id, KIT_BUTTON, r, label);
    if (!it) return NULL;
    it->style = style;
    it->textSize = 8;
    it->flags |= KIT_NOFOCUS;
    return it;
}

static void AddHeader(KitList *l, const HallView *v) {
    AddPlayingAs(l, v);
    KitItem *sign = MouseButton(l, WID_EXIT_SIGN, (KitRect){20, 60, 52, 28}, "EXIT", KIT_STYLE_SIGN);
    if (sign) sign->flags |= KIT_SELFDRAWN; /* part of the wall: drawn under the room's shading */
    if (v->updateBehind) MouseButton(l, WID_UPDATE_PILL, (KitRect){PILL_X, CLOSE_BTN_Y, PILL_W, CLOSE_BTN_SIZE}, "NEW VERSION", KIT_STYLE_PILL);
    KitItem *menu = MouseButton(l, WID_MENU_CHIP, (KitRect){MENU_CHIP_X, CLOSE_BTN_Y, MENU_CHIP_W, CLOSE_BTN_SIZE}, "Menu", KIT_STYLE_SMALL);
    if (menu) menu->icon = KIT_ICON_MENU;
}

int Screens_RowCount(const HallView *v) {
    if (v->lineup) return v->lineup->count;
    return v->manifest ? v->manifest->count : 0;
}

int Screens_RowIndex(const HallView *v, int place) {
    if (place < 0 || place >= Screens_RowCount(v)) return -1;
    return v->lineup ? v->lineup->index[place] : place;
}

/* In the order they are drawn, the lit one last: the one on top takes the
 * click. Positions are places in the row; the label is that machine's. */
static void AddCabinets(KitList *l, const HallView *v) {
    const Manifest *m = v->manifest;
    int n = Screens_RowCount(v);
    for (int pass = 0; pass < 2; pass++) {
        for (int p = 0; p < n; p++) {
            if ((pass == 1) != (p == v->place) || !Hall_CabinetShown(v->scroll, p)) continue;
            int i = Screens_RowIndex(v, p);
            KitRect r = Hall_CabinetRect(v->scroll, p);
            float x0 = r.x < 0.0f ? 0.0f : r.x, x1 = r.x + r.w > KIT_SCREEN_W ? KIT_SCREEN_W : r.x + r.w;
            KitItem *it = Kit_Add(l, WID_CABINET + p, KIT_BUTTON, (KitRect){x0, r.y, x1 - x0, r.h}, m->games[i].name);
            if (!it) continue;
            it->textSize = 8;
            it->flags |= KIT_NOFOCUS | KIT_SELFDRAWN; /* render.c draws the machine and its glow */
        }
    }
}

/* The strip's chips: what each one says, and the key (or pad button) it
 * shows for the device in use. This game opens the lit machine's page (I,
 * pad X); H still opens the whole arcade's trophies. */
typedef struct {
    int id;
    const char *label;
    KeyAction key, pad;
    bool admin;
} StripChip;

static const StripChip kStrip[] = {
    {WID_STRIP_PLAY, "Play", KA_HALL_PLAY, KA_HALL_PLAY, false},
    {WID_STRIP_DAILY, "Daily", KA_HALL_DAILY, KA_HALL_DAILY, false},
    {WID_STRIP_THIS_GAME, "This game", KA_HALL_THIS_GAME, KA_HALL_THIS_GAME, false},
    {WID_STRIP_STATS, "Stats", KA_OPEN_STATS, KA_OPEN_STATS, false},
    {WID_STRIP_MENU, "Menu", KA_HALL_MENU, KA_HALL_MENU, false},
    {WID_STRIP_ADD, "Add", KA_ADMIN_ADD, KA_ADMIN_ADD, true},
    {WID_STRIP_EDIT, "Edit", KA_ADMIN_EDIT, KA_ADMIN_EDIT, true},
    {WID_STRIP_REMOVE, "Remove", KA_ADMIN_REMOVE, KA_ADMIN_REMOVE, true},
};

/* "Installing" when the strip has room for it, else a faint "Install" (the
 * card says Installing... either way). Chipforge is opened, not played. */
static const char *PlayLabel(const HallView *v, bool roomy) {
    switch (v->play) {
        case HP_INSTALL: return "Install";
        case HP_INSTALLING: return roomy ? "Installing" : "Install";
        case HP_PLAY: return v->tool ? (roomy ? "Open Chipforge" : "Open") : "Play";
        default: return "Play";
    }
}

static const char *EmptyRow(const HallView *v) {
    return v->allHidden ? "Every machine is hidden. Settings > Machines shows them." : "No machines in the arcade yet.";
}

/* Faint, with the reason, when it can do nothing here. */
static void ChipState(KitItem *it, const HallView *v) {
    int n = Screens_RowCount(v);
    switch (it->id) {
        case WID_STRIP_PLAY:
            if (n == 0) Kit_Disable(it, EmptyRow(v));
            else if (v->play == HP_INSTALLING) Kit_Disable(it, "Installing now. It lights up when it is done.");
            else if (v->play == HP_CANNOT) Kit_Disable(it, "Not installed, and no folder to install it from.");
            break;
        case WID_STRIP_DAILY:
            if (n == 0) Kit_Disable(it, EmptyRow(v));
            else if (v->dailyReason && v->dailyReason[0]) Kit_Disable(it, v->dailyReason);
            break;
        case WID_STRIP_THIS_GAME:
        case WID_STRIP_EDIT:
        case WID_STRIP_REMOVE:
            if (n == 0) Kit_Disable(it, EmptyRow(v));
            break;
        default: break;
    }
}

static float ChipWidth(const StripChip *c, const HallView *v, bool roomy, char *cap, size_t capSize, bool *pad) {
    Keymap_Cap(v->device == DEV_PAD ? c->pad : c->key, v->device, cap, capSize, pad);
    const char *label = c->id == WID_STRIP_PLAY ? PlayLabel(v, roomy) : c->label;
    return (float)Kit_ChipWidth(cap, *pad, label);
}

static float AddChip(KitList *l, const StripChip *c, const HallView *v, bool roomy, float x) {
    char cap[8];
    bool pad = false;
    float w = ChipWidth(c, v, roomy, cap, sizeof(cap), &pad);
    const char *label = c->id == WID_STRIP_PLAY ? PlayLabel(v, roomy) : c->label;
    KitItem *it = MouseButton(l, c->id, (KitRect){x, STRIP_Y, w, STRIP_H}, label, KIT_STYLE_CHIP);
    if (!it) return x;
    snprintf(it->key, sizeof(it->key), "%s", cap);
    if (pad) it->flags |= KIT_PADCAP;
    ChipState(it, v);
    return x + w + STRIP_GAP;
}

/* Admin's chips only for admin. Chipforge makes music and has no daily at
 * all, so it gets no Daily chip (D and pad Y there still say why). */
static bool Shown(const StripChip *c, const HallView *v) {
    if (c->id == WID_STRIP_DAILY && v->tool) return false;
    return !c->admin || v->admin;
}

/* Where the chips end: the player's, a gap, then admin's. */
static float ChipsEnd(const HallView *v, bool roomy) {
    float x = STRIP_X;
    bool admin = false;
    for (size_t i = 0; i < sizeof(kStrip) / sizeof(kStrip[0]); i++) {
        if (!Shown(&kStrip[i], v)) continue;
        if (kStrip[i].admin && !admin) {
            admin = true;
            x += STRIP_GROUP_GAP - STRIP_GAP;
        }
        char cap[8];
        bool pad;
        x += ChipWidth(&kStrip[i], v, roomy, cap, sizeof(cap), &pad) + STRIP_GAP;
    }
    return x - STRIP_GAP;
}

/* "< 4 / 12 >": two arrows round a slot as wide as the longest count, so
 * they never move as the number changes. render.c writes the count. */
static float CounterX(const HallView *v) {
    int n = Screens_RowCount(v);
    if (n == 0) return STRIP_RIGHT;
    char widest[24];
    snprintf(widest, sizeof(widest), "%d / %d", n, n);
    return STRIP_RIGHT - STRIP_ARROW_W - (float)Kit_TextWidth(widest, 8) - 16.0f - STRIP_ARROW_W;
}

static void AddCounter(KitList *l, const HallView *v) {
    int n = Screens_RowCount(v);
    if (n == 0) return;
    float prevX = CounterX(v), nextX = STRIP_RIGHT - STRIP_ARROW_W;
    KitItem *prev = MouseButton(l, WID_STRIP_PREV, (KitRect){prevX, STRIP_Y, STRIP_ARROW_W, STRIP_H}, "<", KIT_STYLE_SMALL);
    KitItem *next = MouseButton(l, WID_STRIP_NEXT, (KitRect){nextX, STRIP_Y, STRIP_ARROW_W, STRIP_H}, ">", KIT_STYLE_SMALL);
    if (n < 2) {
        Kit_Disable(prev, "Only one machine in the arcade.");
        Kit_Disable(next, "Only one machine in the arcade.");
    }
}

static void AddStrip(KitList *l, const HallView *v) {
    bool roomy = ChipsEnd(v, true) + STRIP_GROUP_GAP <= CounterX(v);
    float x = STRIP_X;
    bool admin = false;
    for (size_t i = 0; i < sizeof(kStrip) / sizeof(kStrip[0]); i++) {
        if (!Shown(&kStrip[i], v)) continue;
        if (kStrip[i].admin && !admin) {
            admin = true;
            x += STRIP_GROUP_GAP - STRIP_GAP;
        }
        x = AddChip(l, &kStrip[i], v, roomy, x);
    }
    AddCounter(l, v);
}

/* The card leaves with attract mode: it slides down, its button with it. */
static bool CardShown(const HallView *v) { return Screens_RowCount(v) > 0 && v->cardSlide < (float)(KIT_SCREEN_H - HALL_CARD_Y); }

/* The card itself: a click opens the lit machine's page. Added before the
 * Favourite on it, so that button stays on top. */
static void AddCard(KitList *l, const HallView *v) {
    if (!CardShown(v)) return;
    KitRect r = {HALL_CARD_X, HALL_CARD_Y + v->cardSlide, HALL_CARD_W, HALL_CARD_H};
    KitItem *it = MouseButton(l, WID_CARD, r, "Card", KIT_STYLE_SMALL);
    if (it) it->flags |= KIT_SELFDRAWN; /* render.c draws the card, lit when pointed at */
}

/* At the right of the machine's name on the card: F, a star (gold when it
 * is a favourite) and the word. */
static void AddFavourite(KitList *l, const HallView *v) {
    if (!CardShown(v)) return;
    char cap[8];
    bool pad = false;
    Keymap_Cap(KA_HALL_FAVOURITE, v->device, cap, sizeof(cap), &pad);
    float w = (float)(Kit_ChipWidth(cap, pad, "Favourite") + KIT_CHIP_ICON_W);
    KitRect r = {HALL_FAV_RIGHT - w, HALL_FAV_Y + v->cardSlide, w, STRIP_H};
    KitItem *it = MouseButton(l, WID_CARD_FAVOURITE, r, "Favourite", KIT_STYLE_CHIP);
    if (!it) return;
    snprintf(it->key, sizeof(it->key), "%s", cap);
    if (pad) it->flags |= KIT_PADCAP;
    it->icon = KIT_ICON_STAR;
    it->value = v->favourite ? 1 : 0;
    if (!v->canFavourite) Kit_Disable(it, "Its program name cannot be kept as a favourite.");
}

/* Part of the wall, so added first: anything in front of it takes the click. */
static void AddChalkboard(KitList *l, const HallView *v) {
    if (!v->deskLine || !v->deskLine[0]) return;
    KitItem *it = MouseButton(l, WID_CHALKBOARD, (KitRect){CHALK_X, CHALK_Y, CHALK_W, CHALK_H}, "Daily challenge", KIT_STYLE_SMALL);
    if (it) it->flags |= KIT_SELFDRAWN; /* render.c draws it on the wall, under the room's shading */
}

void Screens_Hall(KitList *l, const HallView *v) {
    Kit_Clear(l, 0);
    if (!v->coinInFlight) {
        AddChalkboard(l, v);
        AddCabinets(l, v);
        AddHeader(l, v);
        AddCard(l, v);
        AddFavourite(l, v);
        AddStrip(l, v);
    }
    Screens_AddClose(l);
}

void Screens_Menu(KitList *l, bool updateBehind, InputDevice device) {
    Kit_Clear(l, KIT_NAV_Y | KIT_WRAP_Y);
    Kit_Add(l, WID_MENU_VEIL, KIT_AREA, (KitRect){0, 0, KIT_SCREEN_W, KIT_SCREEN_H}, "");
    Kit_Add(l, WID_MENU_PANEL, KIT_AREA, (KitRect){0, 0, MENU_PANEL_W, KIT_SCREEN_H}, "");
    for (int i = 0; i < AM_COUNT; i++) {
        float y = (float)(MENU_ROW_Y + i * MENU_ROW_PITCH + (i == AM_LEAVE ? MENU_LEAVE_GAP : 0));
        KitItem *it = Kit_Add(l, WID_MENU_ITEM + i, KIT_ROW, (KitRect){MENU_ROW_X, y, MENU_ROW_W, MENU_ROW_H}, kMenu[i].label);
        if (!it) continue;
        snprintf(it->key, sizeof(it->key), "%s", Screens_MenuKey(i, device));
        /* One new version to install, however many changes it holds. */
        if (i == AM_UPDATES && updateBehind) snprintf(it->badge, sizeof(it->badge), "1 new");
    }
    Screens_AddClose(l);
}

/* Where the old screen drew its two answer boxes. */
#define ONLINE_BTN_Y 346
#define ONLINE_BTN_H 48

void Screens_Online(KitList *l) {
    Kit_Clear(l, KIT_NAV_X | KIT_WRAP_X); /* two answers: Left/Right flip between them, as they always did */
    Screens_AddBack(l);
    const char *no = "No, keep it local", *yes = "Yes, share my scores";
    float wNo = (float)Kit_TextWidth(no, 16) + 40.0f, wYes = (float)Kit_TextWidth(yes, 16) + 40.0f;
    Kit_Add(l, WID_ONLINE_NO, KIT_BUTTON, (KitRect){80, ONLINE_BTN_Y, wNo, ONLINE_BTN_H}, no);
    Kit_Add(l, WID_ONLINE_YES, KIT_BUTTON, (KitRect){80 + wNo + 24, ONLINE_BTN_Y, wYes, ONLINE_BTN_H}, yes);
    Screens_AddClose(l);
}

/* "< Blockfall >" where the old header was, its arrows 40 px wide so they
 * are easy to hit. Left/Right stay the screen's (no list navigation). */
void Screens_Trophies(KitList *l, const char *gameName, int game, int count) {
    Kit_Clear(l, 0);
    Screens_AddBack(l);
    if (count > 0) {
        char shown[KIT_LABEL_LEN], whole[KIT_LABEL_LEN + 8];
        snprintf(shown, sizeof(shown), "%.24s", gameName ? gameName : "");
        snprintf(whole, sizeof(whole), "< %s >", shown);
        float w = (float)Kit_TextWidth(whole, 16) + 32.0f;
        KitItem *it = Kit_Add(l, WID_TROPHY_GAME, KIT_STEPPER, (KitRect){64, 148, w, 32}, shown);
        if (it) {
            it->value = game;
            it->min = 0;
            it->max = count - 1;
            it->flags |= KIT_WRAP;
        }
    }
    Screens_AddClose(l);
}

void Screens_Updates(KitList *l, bool checking, bool checksOff) {
    Kit_Clear(l, 0);
    Screens_AddBack(l);
    KitItem *it = Kit_Add(l, WID_CHECK_NOW, KIT_BUTTON, (KitRect){80, 440, 244, 44}, checking ? "Checking..." : "Check now");
    if (it && checksOff) Kit_Disable(it, "Update checks are off in Settings");
    else if (it && checking) Kit_Disable(it, "Asking GitHub now");
    Screens_AddClose(l);
}

void Screens_Plain(KitList *l) {
    Kit_Clear(l, 0);
    Screens_AddBack(l);
    Screens_AddClose(l);
}

void Screens_Dialog(KitList *l, const KitDialog *d) {
    Kit_DialogLayout(d, l);
    Screens_AddClose(l);
}
