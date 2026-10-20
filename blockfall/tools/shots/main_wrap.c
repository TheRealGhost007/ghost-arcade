/* Blockfall's real src/main.c with its main() renamed blockfall_main, so the
 * screenshot driver (driver.c) can run it, plus read-only views of where it
 * is for the driver's log and `expect app`. Nothing in the game is changed:
 * the driver sees the game through -Wl,--wrap of the functions it calls
 * (Game_*, Render_Frame, the screens) and of raylib's input/audio/window
 * calls. */
#define main blockfall_main
#include "../../src/main.c"
#undef main

#include <stdlib.h>
#include <string.h>

/* In the order of app.h's ScreenId: the names the scripts use. */
static const char *const kScreenNames[] = {"MENU", "SETUP", "PLAYING", "SCORES", "SETTINGS", "HOWTO", "UPDATES"};
_Static_assert(sizeof(kScreenNames) / sizeof(kScreenNames[0]) == SCR_COUNT, "kScreenNames must name every screen");

bool drv_is_demo(const Game *g) { return g == &sApp.howto.demo.game; }

const char *drv_screen(void) {
    int s = Nav_Top(&sApp.nav);
    return (s >= 0 && s < SCR_COUNT) ? kScreenNames[s] : "?";
}

static const char *const kLookNames[] = {"normal", "hover", "focused", "pressed", "disabled"};

/* A control's name: its caption when it has one (Width's label is its
 * number), else its label. */
static const char *NameOf(const KitItem *it) { return it->caption[0] ? it->caption : it->label; }

static const char *LabelOf(const KitList *l, int id) {
    const KitItem *it = Kit_Get(l, id);
    return it ? NameOf(it) : "";
}

/* look:LABEL = the look of the item labelled LABEL on the screen on top
 * ('_' stands for a space: look:Start_game). */
static void LookOf(const char *key, char *out, size_t n) {
    char label[KIT_LABEL_LEN];
    snprintf(label, sizeof(label), "%s", key);
    for (char *c = label; *c; c++) if (*c == '_') *c = ' ';
    KitList l;
    const KitState *k;
    if (!Scr_Kit(&sApp, &l, &k)) { snprintf(out, n, "nokit"); return; }
    for (int i = 0; i < l.count; i++) {
        if (strcmp(NameOf(&l.items[i]), label) != 0) continue;
        snprintf(out, n, "%s", kLookNames[Kit_Look(k, &l.items[i], sApp.input.lastDevice)]);
        return;
    }
    snprintf(out, n, "absent");
}

static const char *DeviceName(InputDevice d) { return d == DEV_PAD ? "pad" : (d == DEV_MOUSE ? "mouse" : "keyboard"); }

/* `expect app KEY VALUE` reads these. */
void drv_app(const char *key, char *out, size_t n) {
    App *a = &sApp;
    KitList l;
    const KitState *k = NULL;
    bool kit = Scr_Kit(a, &l, &k);
    if (strncmp(key, "look:", 5) == 0) LookOf(key + 5, out, n);
    else if (strcmp(key, "focus") == 0) snprintf(out, n, "%s", kit ? LabelOf(&l, k->focus) : "nokit");
    else if (strcmp(key, "hover") == 0) snprintf(out, n, "%s", kit ? LabelOf(&l, k->hover) : "nokit");
    else if (strcmp(key, "depth") == 0) snprintf(out, n, "%d", Nav_Depth(&a->nav));
    else if (strcmp(key, "under") == 0) {
        int u = Nav_Under(&a->nav);
        snprintf(out, n, "%s", u >= 0 && u < SCR_COUNT ? kScreenNames[u] : "?");
    } else if (strcmp(key, "device") == 0) snprintf(out, n, "%s", DeviceName(a->input.lastDevice));
    else if (strcmp(key, "hints") == 0) snprintf(out, n, "%s", DeviceName(a->input.hintDevice));
    else if (strcmp(key, "slide") == 0) snprintf(out, n, "%d", App_SlideOffset(a));
    else if (strcmp(key, "drop") == 0) snprintf(out, n, "%d", (int)a->menu.dropping);
    else if (strcmp(key, "toast") == 0) snprintf(out, n, "%s", a->menu.toastOn ? a->menu.toast : "");
    else if (strcmp(key, "mode") == 0) snprintf(out, n, "%s", Game_ModeName((GameMode)(a->settings.setupMode % MODE_COUNT)));
    else if (strcmp(key, "board") == 0) Setup_BoardLabel(&a->settings, out, n);
    else if (strcmp(key, "summary") == 0) Setup_Summary(&a->settings, out, n);
    else if (strcmp(key, "motion") == 0) snprintf(out, n, "%s", a->settings.reducedMotion ? "reduced" : "full");
    else if (strcmp(key, "piecemotion") == 0) snprintf(out, n, "%s", a->settings.pieceSnap ? "snap" : "smooth");
    else if (strcmp(key, "readygo") == 0) snprintf(out, n, "%s", a->settings.readyGo ? "on" : "off");
    else if (strcmp(key, "bestsboard") == 0) snprintf(out, n, "%s", a->menu.bestsBoard);
    else if (strncmp(key, "best:", 5) == 0) {
        static const char *const kNames[4] = {"Marathon", "Sprint", "Ultra", "Power"};
        snprintf(out, n, "?");
        for (int i = 0; i < 4; i++) if (strcmp(key + 5, kNames[i]) == 0) snprintf(out, n, "%s", a->menu.bests[i]);
    } else if (strcmp(key, "yourbest") == 0) snprintf(out, n, "%s", a->setup.yourBest);
    else if (strcmp(key, "yourrank") == 0) snprintf(out, n, "%s", a->setup.yourRank);
    else if (strcmp(key, "worldbest") == 0) snprintf(out, n, "%s %s", a->setup.worldBest, a->setup.worldWho);
    else if (strcmp(key, "scroll") == 0) snprintf(out, n, "%d", (int)a->updates.scroll);
    else if (strcmp(key, "settab") == 0) snprintf(out, n, "%s", SetPage_TabLabel(a->set.tab));
    else if (strcmp(key, "setrow") == 0) {
        /* the label of the row the focus is on ("Tabs" on the tab bar) */
        static SetPage page;
        SetView v = {&a->settings, Prefs_Get(), a->set.tab, KIT_NONE, false};
        SetPage_Layout(&v, &page);
        const SetRow *r = SetPage_RowOf(&page, a->set.kit.focus);
        snprintf(out, n, "%s", a->set.kit.focus == SID_TABS ? "Tabs" : (r ? (r->label[0] ? r->label : "Reset controls") : "?"));
    } else if (strcmp(key, "awaiting") == 0) snprintf(out, n, "%d", (int)a->set.capture.on);
    else if (strcmp(key, "capture") == 0) {
        const Capture *c = &a->set.capture;
        if (!c->on) snprintf(out, n, "none");
        else snprintf(out, n, "%s:%s:%d", c->pad ? "pad" : "key", Controls_ActionLabel(c->action), c->column % 2 + 1);
    } else if (strcmp(key, "note") == 0) snprintf(out, n, "%s", a->set.note);
    else if (strcmp(key, "dialogset") == 0) snprintf(out, n, "%s", a->set.dialog.open ? a->set.dialog.title : "none");
    else if (strncmp(key, "key:", 4) == 0 || strncmp(key, "pad:", 4) == 0) {
        /* key:ACTION:SLOT / pad:ACTION:SLOT, ACTION with '_' for spaces (key:Rotate_left:1) */
        char label[40];
        int slot = 1;
        snprintf(label, sizeof(label), "%s", key + 4);
        char *colon = strrchr(label, ':');
        if (colon) { slot = atoi(colon + 1); *colon = '\0'; }
        for (char *c = label; *c; c++) if (*c == '_') *c = ' ';
        snprintf(out, n, "?");
        for (int act = 0; act < ACT_COUNT; act++) {
            if (strcmp(Controls_ActionLabel(act), label) != 0) continue;
            if (key[0] == 'k') snprintf(out, n, "%s", Controls_KeyConst(&a->settings, act, slot - 1));
            else snprintf(out, n, "%s", Persist_PadName(Controls_Pad(&a->settings, act, slot - 1)));
        }
    }
    else if (strcmp(key, "das") == 0) snprintf(out, n, "%d", a->settings.repeatDelayMs);
    else if (strcmp(key, "arr") == 0) snprintf(out, n, "%d", a->settings.repeatSpeedMs);
    else if (strcmp(key, "softdrop") == 0) snprintf(out, n, "%s", Persist_SoftName(a->settings.softDropSpeed));
    else if (strcmp(key, "grid") == 0) snprintf(out, n, "%s", Persist_GridName(a->settings.grid));
    else if (strcmp(key, "patterns") == 0) snprintf(out, n, "%d", (int)a->settings.blockPatterns);
    else if (strcmp(key, "autopause") == 0) snprintf(out, n, "%d", (int)a->settings.autoPause);
    else if (strcmp(key, "showhowto") == 0) snprintf(out, n, "%d", (int)a->settings.showHowTo);
    else if (strcmp(key, "strip") == 0) snprintf(out, n, "%d", a->set.strip.x);
    else if (strcmp(key, "howtopage") == 0) snprintf(out, n, "%s", HowTo_PageName(a->howto.page));
    else if (strcmp(key, "howtofirst") == 0) snprintf(out, n, "%d", (int)a->howto.firstLaunch);
    else if (strcmp(key, "demopress") == 0) snprintf(out, n, "%d", (int)a->howto.demo.press);
    else if (strcmp(key, "democurrent") == 0) snprintf(out, n, "%s", Tetromino_Name(a->howto.demo.game.current));
    else if (strcmp(key, "demonext") == 0) snprintf(out, n, "%s", Tetromino_Name(a->howto.demo.game.next));
    else if (strcmp(key, "demohold") == 0) snprintf(out, n, "%s", (int)a->howto.demo.game.hold < 0 ? "none" : Tetromino_Name(a->howto.demo.game.hold));
    else if (strcmp(key, "democlears") == 0) {
        out[0] = '\0';
        for (int i = 0; i < a->howto.demo.clearCount; i++) {
            size_t used = strlen(out);
            snprintf(out + used, n - used, "%s%d", i ? "," : "", a->howto.demo.clears[i]);
        }
        if (!out[0]) snprintf(out, n, "none");
    } else if (strcmp(key, "demovariant") == 0) {
        const HowToDemo *d = &a->howto.demo;
        snprintf(out, n, "%s", d->page == HOWTO_POWER ? Game_PowerName((PowerUp)d->variant) : Game_ModeName(d->game.cfg.mode));
    } else if (strcmp(key, "democallout") == 0) snprintf(out, n, "%s", a->howto.demo.callout[0] ? a->howto.demo.callout : "none");
    else if (strcmp(key, "skylanded") == 0) snprintf(out, n, "%d", a->menu.sky.landed);
    else if (strcmp(key, "skyheight") == 0) snprintf(out, n, "%d", Sky_MaxHeight(&a->menu.sky));
    else if (strcmp(key, "pause") == 0) snprintf(out, n, "%s", a->play.pause.open ? "open" : "closed");
    else if (strcmp(key, "pausepage") == 0) snprintf(out, n, "%s", a->play.pause.page == PAUSE_QUICK ? "settings" : "main");
    else if (strcmp(key, "dialog") == 0) snprintf(out, n, "%s", PauseMenu_Asking(&a->play.pause) ? a->play.pause.dialog.title : "none");
    else if (strcmp(key, "dialogbody") == 0) snprintf(out, n, "%s", PauseMenu_Asking(&a->play.pause) ? a->play.pause.dialog.body[0] : "none");
    else if (strcmp(key, "results") == 0) snprintf(out, n, "%s", a->play.results.on ? "on" : "off");
    else if (strcmp(key, "againchip") == 0) { /* the results card's "again" chip as the last frame drew it, or none */
        if (!a->play.results.on || !a->play.againChip[0]) snprintf(out, n, "none");
        else snprintf(out, n, "%s", a->play.againChip);
    }
    else if (strcmp(key, "resultsdone") == 0) snprintf(out, n, "%d", (int)Results_Done(&a->play.results));
    else if (strcmp(key, "shown") == 0) snprintf(out, n, "%ld", Results_Shown(&a->play.results));
    else if (strncmp(key, "card:", 5) == 0) {
        const ResultsCard *c = &a->play.results.card;
        const char *k = key + 5;
        if (strcmp(k, "title") == 0) snprintf(out, n, "%s", c->title);
        else if (strcmp(k, "sub") == 0) snprintf(out, n, "%s", c->sub);
        else if (strcmp(k, "big") == 0) snprintf(out, n, "%ld", c->bigValue);
        else if (strcmp(k, "newbest") == 0) snprintf(out, n, "%d", (int)c->newBest);
        else if (strcmp(k, "best") == 0) snprintf(out, n, "%s", c->bestLine);
        else if (strcmp(k, "rank") == 0) snprintf(out, n, "%s", c->rankLine);
        else if (strcmp(k, "note") == 0) snprintf(out, n, "%s", c->note);
        else if (strncmp(k, "stat", 4) == 0 && k[4] >= '0' && k[4] <= '9')
            snprintf(out, n, "%s=%s", c->statLabel[k[4] - '0'], c->statValue[k[4] - '0']);
        else snprintf(out, n, "?unknown %.40s", key);
    }
    else if (strcmp(key, "wipe") == 0) snprintf(out, n, "%d", (int)Wipe_Active(&a->wipe));
    else if (strcmp(key, "ghost") == 0) snprintf(out, n, "%d", a->settings.ghostPiece);
    else if (strcmp(key, "quickrestart") == 0) snprintf(out, n, "%d", (int)a->settings.quickRestart);
    else if (strcmp(key, "countdown") == 0) snprintf(out, n, "%d", (int)a->settings.resumeCountdown);
    else if (strcmp(key, "scorestab") == 0) snprintf(out, n, "%d", a->scores.tab);
    else if (strcmp(key, "boards") == 0) {
        out[0] = '\0';
        for (int i = 0; i < a->scores.boards; i++) {
            size_t used = strlen(out);
            snprintf(out + used, n - used, "%s%dx%d%s", i ? "|" : "", a->scores.board[i].w, a->scores.board[i].h, a->scores.boardOld[i] ? "old" : "");
        }
    }
    else snprintf(out, n, "?unknown %.40s", key);
}
