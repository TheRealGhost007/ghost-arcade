/* The real src/main.c, its main() renamed so the driver can run it, plus
 * read-only views of where it is for the driver's log and `expect`. */
#define main launcher_main
#include "../../src/main.c"
#undef main

/* In the order of main.c's UIState: a new screen needs its name here. */
static const char *const kUiNames[] = {"LIST", "ADD_GAME", "EDIT_GAME", "STATS", "SETTINGS", "TROPHIES", "ONLINE", "UPDATE", "MENU", "GAME", "DAILY"};
_Static_assert(sizeof(kUiNames) / sizeof(kUiNames[0]) == UI_COUNT, "kUiNames must name every UIState");

static const char *UiName(int ui) {
    return (ui >= 0 && ui < (int)(sizeof(kUiNames) / sizeof(kUiNames[0]))) ? kUiNames[ui] : "?";
}

const char *drv_ui_name(void) { return UiName((int)sApp.ui); }
int drv_selected(void) { return sApp.selected; }
int drv_trophy_game(void) { return sApp.trophyGame; }
/* The name being typed in Settings, or the admin form's field. */
const char *drv_profile_text(void) {
    if (sApp.ui == UI_ADD_GAME || sApp.ui == UI_EDIT_GAME) return GameForm_Text(&sApp.form, sApp.form.active);
    return sApp.settings.field.text;
}

static const char *DialogName(void) {
    if (!sApp.dialog.open) return "none";
    switch (sApp.dialog.kind) {
        case DLG_REMOVE: return "remove";
        case DLG_LEAVE: return "leave";
        case DLG_DISCARD: return "discard";
        default: return "?";
    }
}

/* What is up now: the box if one is open, else the screen (whose buttons
 * are the typing layer's while a name is typed, as Draw shows them). */
static const KitState *ActiveKit(KitList *l) {
    if (sApp.dialog.open) {
        Screens_Dialog(l, &sApp.dialog);
        return &sApp.dialogKit;
    }
    Layout(&sApp, sApp.ui, l, false); /* what takes clicks now */
    if (Typing(&sApp)) Settings_EditLayer(SettingsPageNow(&sApp), &sApp.settings, l); /* only the typing layer does */
    return ScreenKit(&sApp);
}

static const char *LabelOf(const KitList *l, int id) {
    const KitItem *it = Kit_Get(l, id);
    if (!it) return "";
    if (it->id == WID_CLOSE) return "X";
    return it->label;
}

static const char *const kLookNames[] = {"normal", "hover", "focused", "pressed", "disabled"};

/* look:LABEL = the look of the item labelled LABEL ("X" is the close button). */
static void LookOfLabel(const char *label, char *out, size_t n) {
    KitList l;
    const KitState *k = ActiveKit(&l);
    InputDevice device = sApp.dialog.open ? Kit_DialogDevice(k, &l, sApp.input.lastDevice) : sApp.input.lastDevice; /* as Draw */
    for (int i = 0; i < l.count; i++) {
        if (strcmp(LabelOf(&l, l.items[i].id), label) != 0) continue;
        KitLook look = Kit_Look(k, &l.items[i], device);
        if (Typing(&sApp)) look = Settings_TypingLook(&sApp.settings, look, device); /* as Render_Settings */
        snprintf(out, n, "%s", kLookNames[look]);
        return;
    }
    snprintf(out, n, "absent");
}

/* chip:LABEL = a strip button's cap and look ("Esc normal", "Y disabled"):
 * the strip's Menu shares its label with the header's chip, so it is asked
 * for by its place in the strip. */
static void ChipOf(const char *label, char *out, size_t n) {
    KitList l;
    const KitState *k = ActiveKit(&l);
    for (int i = 0; i < l.count; i++) {
        const KitItem *it = &l.items[i];
        if (it->id < WID_STRIP_PLAY || it->id > WID_STRIP_NEXT || strcmp(it->label, label) != 0) continue;
        snprintf(out, n, "%s %s", it->key[0] ? it->key : "-", kLookNames[Kit_Look(k, it, sApp.input.lastDevice)]);
        return;
    }
    snprintf(out, n, "absent");
}

/* The card's line as Draw would write it this frame. */
static void StatusOf(char *out, size_t n) {
    KitList l;
    const KitState *k = ActiveKit(&l);
    KitView kv = {&l, k, sApp.input.lastDevice, HintDevice(&sApp)};
    HallStatus st;
    HallStatusNow(&sApp, &kv, &st);
    snprintf(out, n, "%s", st.text);
}

/* The row as the hall shows it, by name: "Blockfall,Coilrush,...". */
static void RowNames(const Lineup *l, char *out, size_t n) {
    size_t used = 0;
    out[0] = '\0';
    for (int k = 0; k < l->count && used + 1 < n; k++) {
        int w = snprintf(out + used, n - used, "%s%s", k ? "," : "", sApp.manifest.games[l->index[k]].name);
        if (w < 0 || (size_t)w >= n - used) break;
        used += (size_t)w;
    }
}

/* fav:NAME / hidden:NAME: the machine called NAME, as the row has it. */
static void RowFlag(const char *name, bool fav, char *out, size_t n) {
    for (int i = 0; i < sApp.manifest.count && i < sApp.rowPrefs.count; i++) {
        if (strcmp(sApp.manifest.games[i].name, name) != 0) continue;
        snprintf(out, n, "%d", (int)(fav ? sApp.rowPrefs.m[i].favourite : sApp.rowPrefs.m[i].hidden));
        return;
    }
    snprintf(out, n, "absent");
}

static const char *MusicName(MusicMode m) {
    switch (m) {
        case MUSIC_MENU: return "menu";
        case MUSIC_PLAY: return "play";
        case MUSIC_PAUSED: return "paused";
        default: return "silent";
    }
}

/* `expect app KEY VALUE` reads these. */
void drv_app(const char *key, char *out, size_t n) {
    const App *a = &sApp;
    KitList l;
    const KitState *k = ActiveKit(&l);
    if (strncmp(key, "look:", 5) == 0) LookOfLabel(key + 5, out, n);
    else if (strcmp(key, "dialog") == 0) snprintf(out, n, "%s", DialogName());
    else if (strcmp(key, "dialogtitle") == 0) snprintf(out, n, "%s", a->dialog.open ? a->dialog.title : "");
    else if (strcmp(key, "focus") == 0) snprintf(out, n, "%s", LabelOf(&l, k->focus));
    else if (strcmp(key, "hover") == 0) snprintf(out, n, "%s", LabelOf(&l, k->hover));
    else if (strcmp(key, "toast") == 0) snprintf(out, n, "%s", a->toastTimer > 0.0f ? a->toast : "");
    else if (strcmp(key, "checking") == 0) snprintf(out, n, "%d", (int)a->checking);
    else if (strcmp(key, "noanswer") == 0) snprintf(out, n, "%d", (int)a->checkNoAnswer);
    else if (strcmp(key, "statstop") == 0) snprintf(out, n, "%d", a->statsScroll.top);
    else if (strcmp(key, "trophytop") == 0) snprintf(out, n, "%d", a->trophyScroll.top);
    else if (strcmp(key, "depth") == 0) snprintf(out, n, "%d", Nav_Depth(&a->nav));
    else if (strcmp(key, "under") == 0) snprintf(out, n, "%s", UiName(Nav_Under(&a->nav)));
    else if (strcmp(key, "cursor") == 0) snprintf(out, n, "%s", a->cursor == MOUSE_CURSOR_POINTING_HAND ? "hand" : "default");
    else if (strcmp(key, "choice") == 0) snprintf(out, n, "%s", a->onlineChoice ? "yes" : "no");
    else if (strcmp(key, "games") == 0) snprintf(out, n, "%d", a->manifest.count);
    else if (strcmp(key, "device") == 0) snprintf(out, n, "%d", (int)a->input.lastDevice);
    else if (strcmp(key, "tab") == 0) snprintf(out, n, "%s", Settings_TabLabel(a->settings.tab));
    else if (strcmp(key, "focusrow") == 0) {
        const SettingsRow *r = sApp.ui == UI_SETTINGS ? Settings_RowOf(SettingsPageNow(&sApp), k->focus) : NULL;
        snprintf(out, n, "%s", r ? (r->label[0] ? r->label : "(button)") : "");
    }
    else if (strcmp(key, "typing") == 0) snprintf(out, n, "%d", (int)a->settings.field.editing);
    else if (strcmp(key, "cursor_at") == 0) snprintf(out, n, "%d", a->settings.field.editing ? a->settings.field.cursor : a->form.field.cursor);
    else if (strcmp(key, "namestop") == 0) snprintf(out, n, "%d", a->settings.names.top);
    else if (strcmp(key, "card") == 0) snprintf(out, n, "%s", Keymap_CardTitle(a->settings.card));
    else if (strcmp(key, "hallsounds") == 0) snprintf(out, n, "%d", a->hallSounds);
    else if (strcmp(key, "syncing") == 0) snprintf(out, n, "%d", (int)a->syncing);
    else if (strcmp(key, "syncnoanswer") == 0) snprintf(out, n, "%d", (int)a->syncNoAnswer);
    else if (strcmp(key, "sharing") == 0) snprintf(out, n, "%d", (int)a->syncCfg.enabled);
    else if (strcmp(key, "checks") == 0) snprintf(out, n, "%d", (int)a->syncCfg.checkUpdates);
    else if (strcmp(key, "board") == 0) snprintf(out, n, "%d", (int)a->settings.board);
    else if (strcmp(key, "boardcell") == 0) {
        const NameBoardCell *c = NameBoard_Cell(a->settings.boardPos.cell);
        snprintf(out, n, "%s", c ? c->label : "");
    }
    else if (strncmp(key, "chip:", 5) == 0) { /* '_' for a space: "chip:This_game" */
        char label[KIT_LABEL_LEN];
        snprintf(label, sizeof(label), "%s", key + 5);
        for (char *p = label; *p; p++) if (*p == '_') *p = ' ';
        ChipOf(label, out, n);
    }
    else if (strcmp(key, "status") == 0) StatusOf(out, n);
    else if (strcmp(key, "note") == 0) snprintf(out, n, "%s", a->noteTimer > 0.0f ? a->note : "");
    else if (strcmp(key, "pointer") == 0) snprintf(out, n, "%s", a->pointerHidden ? "hidden" : "shown");
    else if (strcmp(key, "hint") == 0) snprintf(out, n, "%d", (int)a->input.hintDevice);
    else if (strcmp(key, "hints") == 0) snprintf(out, n, "%d", (int)HintDevice(a)); /* after Button hints */
    else if (strcmp(key, "settled") == 0) snprintf(out, n, "%d", (int)(a->row.scroll == (float)Place(a))); /* the row counts places */
    else if (strcmp(key, "name") == 0) snprintf(out, n, "%s", Profiles_GetOwnUsername(&a->profiles, PROFILE_DEFAULT_KEY));
    else if (strncmp(key, "name:", 5) == 0) snprintf(out, n, "%s", Profiles_GetOwnUsername(&a->profiles, key + 5));
    /* Phase 4: your row, hall music, the quiet hall */
    else if (strcmp(key, "row") == 0) RowNames(&a->lineup, out, n);
    else if (strcmp(key, "playlabel") == 0) {
        const KitItem *play = Kit_Get(&l, WID_STRIP_PLAY);
        snprintf(out, n, "%s", play ? play->label : "absent");
    }
    else if (strcmp(key, "mine") == 0) RowNames(&a->mine, out, n);
    else if (strcmp(key, "lit") == 0) snprintf(out, n, "%s", RowShown(a) ? a->manifest.games[a->selected].name : "");
    else if (strcmp(key, "place") == 0) snprintf(out, n, "%d", Place(a));
    else if (strcmp(key, "order") == 0) snprintf(out, n, "%s", Row_OrderName(a->rowPrefs.order));
    else if (strcmp(key, "startlast") == 0) snprintf(out, n, "%d", (int)a->rowPrefs.startOnLast);
    else if (strncmp(key, "fav:", 4) == 0) RowFlag(key + 4, true, out, n);
    else if (strncmp(key, "hidden:", 7) == 0) RowFlag(key + 7, false, out, n);
    else if (strcmp(key, "music") == 0) snprintf(out, n, "%s", MusicName(a->musicMode));
    else if (strcmp(key, "musicvolume") == 0) snprintf(out, n, "%d", a->musicVolume);
    else if (strcmp(key, "hallmusic") == 0) snprintf(out, n, "%d", (int)a->hallMusic);
    else if (strcmp(key, "hallsong") == 0) snprintf(out, n, "%s", HallMusic_SongWords(&a->hallSong));
    else if (strcmp(key, "fps") == 0) snprintf(out, n, "%d", a->fps);
    else if (strcmp(key, "quiet") == 0) snprintf(out, n, "%d", (int)a->quiet);
    else if (strcmp(key, "clock") == 0) snprintf(out, n, "%.2f", KitDraw_Clock());
    else if (strcmp(key, "scroll") == 0) snprintf(out, n, "%.2f", (double)a->row.scroll);
    else if (strcmp(key, "cardtool") == 0) snprintf(out, n, "%d %d %s", (int)a->card.tool, a->card.songs, a->card.hallSong);
    /* Phase 5: a machine's page, admin's form, Settings > Machines */
    else if (strcmp(key, "pagetab") == 0) snprintf(out, n, "%s", GamePage_TabLabel(GamePage_ShownTab(&a->page, a->pageData.tool)));
    else if (strcmp(key, "pageboard") == 0) { /* the board Scores shows, as its stepper reads */
        char words[48] = "", today[24];
        int at = GamePage_Board(&a->page, &a->pageData.boards);
        GamePage_Today(time(NULL), today, sizeof(today));
        if (at >= 0) Boards_ModeWordsFor(a->pageData.boards.slug, a->pageData.boards.b[at].mode, today, words, sizeof(words));
        snprintf(out, n, "%s", words);
    }
    else if (strcmp(key, "boards") == 0) snprintf(out, n, "%d", a->pageData.boards.count);
    else if (strcmp(key, "worldrank") == 0) {
        int at = GamePage_Board(&a->page, &a->pageData.boards);
        snprintf(out, n, "%d", at >= 0 ? a->pageData.boards.b[at].worldRank : 0);
    }
    else if (strcmp(key, "pagerows") == 0) { /* this computer's rows / the world's, on the board shown */
        int at = GamePage_Board(&a->page, &a->pageData.boards);
        const Board *b = at >= 0 ? &a->pageData.boards.b[at] : NULL;
        snprintf(out, n, "%d/%d", b ? b->localCount : 0, b ? b->worldCount : 0);
    }
    else if (strcmp(key, "pagefor") == 0) snprintf(out, n, "%s", a->pageData.index >= 0 && a->pageData.index < a->manifest.count ? a->manifest.games[a->pageData.index].name : "");
    else if (strcmp(key, "pagehelp") == 0) {
        KitView kv = {&l, k, sApp.input.lastDevice, HintDevice(a)};
        PageViewNow b;
        ViewPage(a, &b);
        PageHelp(a, &kv, &b.v, out, n);
    }
    else if (strcmp(key, "pagestatus") == 0) {
        HallStatus st;
        PageStatus(a, &st);
        snprintf(out, n, "%s", st.text);
    }
    else if (strcmp(key, "runs") == 0) snprintf(out, n, "%d", a->pageData.runs);
    else if (strncmp(key, "strip:", 6) == 0) { /* the page's strip, cell N (0 = Best or Songs): its value, "(faint)" when dim */
        PageStripCell cells[PAGE_STRIP_MAX];
        int count = GamePage_Strip(&a->pageData, time(NULL), cells), at = atoi(key + 6);
        if (at < 0 || at >= count) snprintf(out, n, "absent");
        else snprintf(out, n, "%s%s", cells[at].value, cells[at].dim ? " (faint)" : "");
    }
    else if (strcmp(key, "formactive") == 0) snprintf(out, n, "%s", a->form.active == FORM_ON_BUTTONS ? "buttons" : GameForm_Prompt(a->form.active));
    else if (strcmp(key, "formerror") == 0) snprintf(out, n, "%s", a->form.error);
    else if (strcmp(key, "carry") == 0) {
        CarryOver c;
        FormCarry(a, &c);
        snprintf(out, n, "%s", !Machines_CanCarry(&c) ? "none" : (a->form.carry ? "on" : "off"));
    }
    else if (strncmp(key, "playtime:", 9) == 0) snprintf(out, n, "%.1f", Stats_GetGameTime(&a->stats, key + 9));
    else if (strncmp(key, "status:", 7) == 0) { /* Settings > Machines' line under a machine's name */
        const SettingsPage *page = SettingsPageNow(&sApp);
        snprintf(out, n, "absent");
        for (int i = 0; i < page->rowCount; i++)
            if (strcmp(page->rows[i].label, key + 7) == 0 && page->rows[i].sub[0]) snprintf(out, n, "%s", page->rows[i].sub);
    }
    /* Phase 6: the daily desk, attract mode, the jukebox */
    else if (strcmp(key, "desk") == 0) snprintf(out, n, "%d/%d", a->desk.played, a->desk.count);
    else if (strcmp(key, "chalk") == 0) Desk_BoardLine(&a->desk, out, n);
    else if (strcmp(key, "streak") == 0) snprintf(out, n, "%d/%d", a->desk.streak.current, a->desk.streak.best);
    else if (strcmp(key, "featured") == 0) snprintf(out, n, "%s", Desk_GameSlug(a->desk.featured));
    else if (strcmp(key, "deskrows") == 0) { /* "Skyraid,Brickburst,...": the desk's rows in order, * for played */
        size_t used = 0;
        out[0] = '\0';
        for (int r = 0; r < a->desk.count && used + 1 < n; r++) {
            int w = snprintf(out + used, n - used, "%s%s%s", r ? "," : "", a->desk.rows[r].name, a->desk.rows[r].played ? "*" : "");
            if (w < 0 || (size_t)w >= n - used) break;
            used += (size_t)w;
        }
    }
    else if (strcmp(key, "deskfocus") == 0) {
        int r = Desk_RowOf(a->kit[UI_DAILY].focus);
        snprintf(out, n, "%s", r >= 0 && r < a->desk.count ? a->desk.rows[r].name : "");
    }
    else if (strcmp(key, "desktop") == 0) snprintf(out, n, "%d", a->deskScroll.top);
    else if (strcmp(key, "attract") == 0) snprintf(out, n, "%d", (int)a->attract.on);
    else if (strcmp(key, "attractsetting") == 0) snprintf(out, n, "%s", Attract_SettingName(a->attractSetting));
    else if (strcmp(key, "shown") == 0) { /* the machine lit in the middle: attract mode's tour, or yours */
        int i = Lineup_IndexAt(&a->lineup, ShownPlace(a));
        snprintf(out, n, "%s", i >= 0 ? a->manifest.games[i].name : "");
    }
    else if (strcmp(key, "cardout") == 0) snprintf(out, n, "%.2f", (double)a->attract.cardOut);
    else if (strcmp(key, "juke") == 0) snprintf(out, n, "%s", !a->hallMusic ? "title" : (a->tune.orderCount > 0 ? "notes" : "none"));
    else if (strcmp(key, "tune") == 0) snprintf(out, n, "%s %d", a->tune.name, a->tune.bpm);
    else if (strcmp(key, "hallfile") == 0) {
        char file[SONG_FILE_LEN + 8];
        snprintf(out, n, "%s", Song_AssignedName(HALL_MUSIC_SLUG, file, sizeof(file)) ? file : "");
    }
    else if (strcmp(key, "songsview") == 0) snprintf(out, n, "%s", a->page.songsView == SONGS_WHO ? "who" : "hall");
    else if (strcmp(key, "songstop") == 0) snprintf(out, n, "%d", a->page.songs.top);
    else if (strcmp(key, "hallentry") == 0) snprintf(out, n, "%d", a->pageData.hallEntry);
    /* Update now */
    else if (strcmp(key, "updview") == 0) {
        static const char *const kViews[] = {"idle", "running", "done", "failed"};
        snprintf(out, n, "%s", kViews[a->updView]);
    }
    else if (strcmp(key, "updseen") == 0) snprintf(out, n, "%d", (int)a->updSeen);
    else if (strcmp(key, "updfollow") == 0) snprintf(out, n, "%d", (int)a->updFollow);
    else if (strcmp(key, "updelsewhere") == 0) snprintf(out, n, "%d", (int)a->updElsewhere);
    else if (strcmp(key, "updstep") == 0) snprintf(out, n, "%s", SelfUpdate_StepLabel(a->updLog.step));
    else if (strcmp(key, "updfail") == 0) {
        char w0[128], w1[128];
        SelfUpdate_FailWords(a->updFail, &a->updLog, w0, w1, sizeof(w0));
        snprintf(out, n, "%s", w0);
    }
    else if (strcmp(key, "updwhy") == 0) snprintf(out, n, "%s", a->updWhy);
    else if (strcmp(key, "updversion") == 0) snprintf(out, n, "%.7s", a->updLog.version);
    else if (strcmp(key, "restart") == 0) snprintf(out, n, "%d", (int)a->restart);
    else if (strcmp(key, "behind") == 0) snprintf(out, n, "%d", (int)UpdateBehind(a));
    else snprintf(out, n, "?unknown key %s", key);
}

void drv_describe(char *buf, size_t n) {
    const App *a = &sApp;
    char focus[64];
    drv_app("focus", focus, sizeof(focus));
    snprintf(buf, n, "ui=%s depth=%d dialog=%s focus='%s' selected=%d/%d place=%d coin=%.2f trophyGame=%d online=%d/%d tab=%s typing=%d text='%s' formActive=%d error='%s' toast='%s' lastDevice=%d attract=%d",
             drv_ui_name(), Nav_Depth(&a->nav), DialogName(), focus, a->selected, a->manifest.count, Place(a), (double)a->coinTimer,
             a->trophyGame, (int)a->onlineChoice, (int)a->onlineFirstTime, Settings_TabLabel(a->settings.tab),
             (int)a->settings.field.editing, drv_profile_text(), a->form.active, a->errorMsg, a->toastTimer > 0.0f ? a->toast : "",
             (int)a->input.lastDevice, (int)a->attract.on);
}
