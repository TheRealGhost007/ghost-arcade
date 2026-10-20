#define _POSIX_C_SOURCE 200809L /* localtime_r */
#include "gamepage.h"
#include "keymap.h"
#include "runstats.h"
#include "hallmusic.h"
#include "achievements.h"
#include "song.h"
#include "../sync/syncdata.h"
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------- words */

void GamePage_Today(time_t now, char *out, size_t size) {
    struct tm t;
    if (!localtime_r(&now, &t)) {
        snprintf(out, size, "daily-00000000");
        return;
    }
    snprintf(out, size, "daily-%04d%02d%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
}

void GamePage_DayWords(const char *date, time_t now, char *out, size_t size) {
    static const char *const kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int y, m, d;
    if (!date || !date[0] || sscanf(date, "%4d-%2d-%2d", &y, &m, &d) != 3 || m < 1 || m > 12) {
        snprintf(out, size, "Never");
        return;
    }
    int ago = RunStats_DaysAgo(date, now);
    struct tm today;
    bool thisYear = localtime_r(&now, &today) && today.tm_year + 1900 == y;
    if (ago == 0) snprintf(out, size, "Today");
    else if (ago == 1) snprintf(out, size, "Yesterday");
    else if (ago > 1 && ago < 7) snprintf(out, size, "%d days ago", ago);
    else if (thisYear) snprintf(out, size, "%d %s", d, kMonths[m - 1]);
    else snprintf(out, size, "%d %s %d", d, kMonths[m - 1], y);
}

/* ----------------------------------------------------------------- data */

static void DateOf(long long when, char *out, size_t size) {
    time_t t = (time_t)when;
    struct tm tm;
    if (when <= 0 || !localtime_r(&t, &tm)) {
        out[0] = '\0';
        return;
    }
    char date[32]; /* room for any year the compiler can imagine; a real one is ten characters */
    snprintf(date, sizeof(date), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    snprintf(out, size, "%.10s", date);
}

static void AddUse(GamePageData *d, const char *machine, const char *song, bool custom) {
    if (d->useCount >= PAGE_SONG_USES) return;
    PageSongUse *u = &d->uses[d->useCount++];
    snprintf(u->machine, sizeof(u->machine), "%.31s", machine);
    snprintf(u->song, sizeof(u->song), "%.47s", song);
    u->custom = custom;
}

/* Chipforge's Songs tab: the hall, then every game with the song it plays. */
static void SongUses(GamePageData *d, const GamePageSource *src) {
    const char *hall = src->hallSong && src->hallSong[0] ? src->hallSong : "Built-in theme";
    AddUse(d, "The hall", hall, strcmp(hall, "Built-in theme") != 0);
    static Song song; /* large: one, off the stack */
    for (int i = 0; src->manifest && i < src->manifest->count; i++) {
        const GameEntry *e = &src->manifest->games[i];
        char slug[SCORES_SLUG_LEN], path[700], file[SONG_FILE_LEN + 8];
        if (Manifest_IsTool(e) || !Scores_SlugFromExec(e->exec, slug, sizeof(slug))) continue;
        if (Song_Assigned(slug, path, sizeof(path)) && Song_Load(&song, path)) {
            Song_AssignedName(slug, file, sizeof(file));
            AddUse(d, e->name, song.name[0] ? song.name : file, true);
        } else {
            AddUse(d, e->name, "Built-in theme", false); /* a song that does not load is not what plays */
        }
    }
}

/* Chipforge's library, by file name, each with its title; and which of
 * them the hall plays. */
static void Library(GamePageData *d, const char *hallFile) {
    static char names[PAGE_LIBRARY_MAX][SONG_FILE_LEN];
    static Song song; /* large: one, off the stack */
    int n = Song_LibraryList(names, PAGE_LIBRARY_MAX);
    d->hallEntry = hallFile && hallFile[0] ? -1 : 0; /* a song past the list's end is marked nowhere */
    for (int i = 0; i < n; i++) {
        PageSong *e = &d->library[i];
        snprintf(e->file, sizeof(e->file), "%.*s", (int)sizeof(e->file) - 1, names[i]);
        char path[700];
        bool titled = Song_LibraryPath(names[i], path, sizeof(path), false) && Song_Load(&song, path) && song.name[0];
        snprintf(e->title, sizeof(e->title), "%.*s", (int)sizeof(e->title) - 1, titled ? song.name : names[i]);
        if (hallFile && strcmp(hallFile, names[i]) == 0) d->hallEntry = i + 1;
    }
    d->libraryCount = n;
}

void GamePage_Load(GamePageData *d, const GamePageSource *src) {
    memset(d, 0, sizeof(*d));
    d->index = -1;
    const GameEntry *e = src->game;
    if (!e) return;
    d->index = src->index;
    d->tool = Manifest_IsTool(e);
    if (!Scores_SlugFromExec(e->exec, d->slug, sizeof(d->slug))) d->slug[0] = '\0';
    d->seconds = src->stats ? Stats_GetGameTime(src->stats, e->name) : 0.0;
    const char *arcade = src->profiles ? Profiles_GetOwnUsername(src->profiles, PROFILE_DEFAULT_KEY) : "";
    const char *plays = src->profiles ? Profiles_GetUsername(src->profiles, e->name) : "";
    snprintf(d->playsAs, sizeof(d->playsAs), "%.63s", plays);
    d->ownName = src->profiles && Profiles_GetOwnUsername(src->profiles, e->name)[0] != '\0';
    DateOf(src->played, d->last, sizeof(d->last));
    d->howto = d->slug[0] ? HowTo_ForSlug(d->slug) : NULL;
    if (d->tool) {
        d->songs = HallMusic_LibraryCount();
        snprintf(d->hallSong, sizeof(d->hallSong), "%.47s", src->hallSong && src->hallSong[0] ? src->hallSong : "Built-in theme");
        SongUses(d, src);
        Library(d, src->hallFile);
        return;
    }
    if (!d->slug[0]) return;
    d->best = Scores_LoadBest(d->slug, false);
    RunSummary sum;
    RunStats_Load(d->slug, src->now, &sum);
    d->runs = sum.runs;
    if (strcmp(sum.last, d->last) > 0) memcpy(d->last, sum.last, sizeof(d->last)); /* whichever is later */
    d->trophies = Ach_UnlockedForGame(d->slug);
    d->trophyTotal = Ach_CountForGame(d->slug);
    char cutoff[24];
    Sync_DailyCutoff(src->now, cutoff, sizeof(cutoff));
    BoardYou you = {{plays, arcade}};
    Boards_Load(&d->boards, d->slug, &you, cutoff);
}

static void Cell(PageStripCell *c, int x, const char *label, const char *value, const char *detail, bool dim) {
    memset(c, 0, sizeof(*c));
    c->x = x;
    snprintf(c->label, sizeof(c->label), "%s", label);
    snprintf(c->value, sizeof(c->value), "%s", value);
    snprintf(c->detail, sizeof(c->detail), "%s", detail ? detail : "");
    c->dim = dim;
}

/* When it was last played. The launcher only knows the launches it made
 * since it began keeping them (and the games' own runs.log), so a machine
 * with time played but no date says so rather than "Never". */
static void LastWords(const GamePageData *d, time_t now, char *out, size_t size) {
    if (!d->last[0] && d->seconds >= 1.0) snprintf(out, size, "Not recorded");
    else GamePage_DayWords(d->last, now, out, size);
}

int GamePage_Strip(const GamePageData *d, time_t now, PageStripCell out[PAGE_STRIP_MAX]) {
    char time[24], day[24], value[48], detail[32];
    Stats_FormatTime(d->seconds, time, sizeof(time));
    LastWords(d, now, day, sizeof(day));
    bool noTime = d->seconds < 60.0; /* "0m": nothing to show yet, faint as Runs 0 is */
    if (d->tool) {
        snprintf(value, sizeof(value), "%d", d->songs);
        Cell(&out[0], PAGE_X, "Songs", value, d->songs == 1 ? "song" : "songs", false);
        Cell(&out[1], 296, "Time played", time, NULL, noTime);
        Cell(&out[2], 432, "Last opened", day, NULL, !d->last[0]);
        Cell(&out[3], 640, "Hall song", d->hallSong, NULL, false);
        return 4;
    }
    if (d->best.found) {
        char today[24];
        GamePage_Today(now, today, sizeof(today));
        Scores_FormatScore(d->best.timed, d->best.score, value, sizeof(value));
        Boards_ModeWordsFor(d->slug, d->best.mode, today, detail, sizeof(detail));
        Cell(&out[0], PAGE_X, "Best", value, detail, false);
    } else {
        Cell(&out[0], PAGE_X, "Best", "None yet", NULL, true);
    }
    Cell(&out[1], 296, "Time played", time, NULL, noTime);
    snprintf(value, sizeof(value), "%d", d->runs);
    Cell(&out[2], 432, "Runs", value, NULL, d->runs == 0);
    Cell(&out[3], 552, "Last played", day, NULL, !d->last[0]);
    if (d->trophyTotal > 0) snprintf(value, sizeof(value), "%d/%d", d->trophies, d->trophyTotal);
    else snprintf(value, sizeof(value), "None"); /* a machine of the admin's own: "0/0" says nothing */
    Cell(&out[4], 752, "Trophies", value, NULL, d->trophyTotal == 0);
    return 5;
}

/* ----------------------------------------------------------------- tabs */

static const char *const kTabLabels[GT_COUNT] = {[GT_ABOUT] = "About", [GT_SCORES] = "Scores", [GT_TROPHIES] = "Trophies", [GT_SONGS] = "Songs"};
static const int kGameTabs[] = {GT_ABOUT, GT_SCORES, GT_TROPHIES};
static const int kToolTabs[] = {GT_ABOUT, GT_SONGS};
static const char *const kGameCells[] = {"About", "Scores", "Trophies"};
static const char *const kToolCells[] = {"About", "Songs"};

const char *GamePage_TabLabel(int tab) { return tab >= 0 && tab < GT_COUNT ? kTabLabels[tab] : ""; }

int GamePage_TabCount(bool tool) { return tool ? 2 : 3; }

int GamePage_TabAt(bool tool, int cell) {
    if (cell < 0 || cell >= GamePage_TabCount(tool)) return GT_ABOUT;
    return tool ? kToolTabs[cell] : kGameTabs[cell];
}

int GamePage_CellOf(bool tool, int tab) {
    for (int c = 0; c < GamePage_TabCount(tool); c++) if (GamePage_TabAt(tool, c) == tab) return c;
    return -1;
}

int GamePage_ShownTab(const GamePage *p, bool tool) { return GamePage_CellOf(tool, p->tab) >= 0 ? p->tab : GT_ABOUT; }

int GamePage_KeepFocus(const KitList *l, int focus) { return Kit_CanFocus(Kit_Get(l, focus)) ? focus : Kit_FirstFocus(l); }

bool GamePage_ClickSettling(double openedAt, double now) { return now >= openedAt && now - openedAt < PAGE_CLICK_SETTLE; }

/* --------------------------------------------------------------- boards */

int GamePage_Board(const GamePage *p, const Boards *b) {
    if (!b || b->count == 0) return -1;
    int at = Boards_Find(b, p->board);
    return at >= 0 ? at : 0;
}

void GamePage_SetBoard(GamePage *p, const Boards *b, int index) {
    if (!b || index < 0 || index >= b->count) return;
    snprintf(p->board, sizeof(p->board), "%s", b->b[index].mode);
}

void GamePage_StartBoard(GamePage *p, const Boards *b, const char *bestMode) {
    p->board[0] = '\0';
    if (!b || b->count == 0) return;
    int at = bestMode ? Boards_Find(b, bestMode) : -1;
    GamePage_SetBoard(p, b, at >= 0 ? at : 0);
}

/* --------------------------------------------------------------- layout */

/* Play in 16 px; the others in 8 px, so "Name for this game" and its key
 * fit under the screen, their labels in one column whether or not a key
 * shows (after pad use only Daily has a button of its own). */
static KitItem *Button(KitList *l, int id, int row, const char *label) {
    KitItem *it = Kit_Add(l, id, KIT_BUTTON, (KitRect){PAGE_X, (float)(PAGE_BTN_Y + row * PAGE_BTN_PITCH), PAGE_LEFT_W, PAGE_BTN_H}, label);
    if (it && row > 0) {
        it->textSize = 8;
        it->flags |= KIT_ALIGN_LEFT;
    }
    return it;
}

/* The key that does the same from anywhere on the page, at the button's
 * right: the keyboard's, or after pad use the pad's (none where it has none). */
static void KeyOf(KitItem *it, KeyAction a, InputDevice device) {
    if (!it) return;
    char cap[8];
    bool pad = false;
    bool onPad = device == DEV_PAD;
    Keymap_Cap(a, onPad ? DEV_PAD : DEV_KEYBOARD, cap, sizeof(cap), &pad);
    if (onPad && !pad) return;
    snprintf(it->key, sizeof(it->key), "%s", cap);
}

static void PlayButton(KitList *l, const GamePageView *v) {
    const char *label = v->tool ? "Open Chipforge" : "Play";
    if (v->play == HP_INSTALL) label = "Install";
    if (v->play == HP_INSTALLING) label = "Installing...";
    KitItem *it = Button(l, WID_PAGE_PLAY, 0, label);
    if (v->play == HP_INSTALLING) Kit_Disable(it, "Installing now. It can be played once it is done.");
    if (v->play == HP_CANNOT) Kit_Disable(it, "Not installed, and no folder to install it from.");
}

/* Chipforge makes music: no daily and no player name, as on the hall. */
static void Buttons(KitList *l, const GamePageView *v) {
    PlayButton(l, v);
    int row = 1;
    if (!v->tool) {
        KitItem *daily = Button(l, WID_PAGE_DAILY, row++, "Daily challenge");
        KeyOf(daily, KA_HALL_DAILY, v->device);
        if (v->dailyReason && v->dailyReason[0]) Kit_Disable(daily, v->dailyReason);
        KeyOf(Button(l, WID_PAGE_NAME, row++, "Name for this game"), KA_OPEN_GAME_NAME, v->device);
    }
    KitItem *fav = Button(l, WID_PAGE_FAV, row, "Favourite");
    KeyOf(fav, KA_HALL_FAVOURITE, v->device);
    if (fav) {
        fav->icon = KIT_ICON_STAR;
        fav->value = v->favourite ? 1 : 0;
        if (!v->canFavourite) Kit_Disable(fav, "Its program name cannot be kept as a favourite.");
    }
}

static void Tabs(KitList *l, const GamePage *p, const GamePageView *v) {
    KitItem *it = Kit_Add(l, WID_PAGE_TABS, KIT_TABS, (KitRect){PAGE_RIGHT_X, PAGE_TABS_Y, 0, PAGE_TABS_H}, "Tabs");
    if (!it) return;
    it->cells = v->tool ? kToolCells : kGameCells;
    it->cellCount = GamePage_TabCount(v->tool);
    it->value = GamePage_CellOf(v->tool, GamePage_ShownTab(p, v->tool));
    it->flags |= KIT_WRAP | KIT_NOFOCUS; /* Left/Right, Tab and LB/RB switch them from anywhere */
}

/* "< Classic >", as wide as the widest board so its arrows hold still. */
static void BoardStepper(KitList *l, const GamePage *p, const GamePageView *v) {
    if (GamePage_ShownTab(p, v->tool) != GT_SCORES || !v->boards || v->boards->count == 0) return;
    char words[KIT_LABEL_LEN], widest[KIT_LABEL_LEN] = "";
    for (int i = 0; i < v->boards->count; i++) {
        Boards_ModeWordsFor(v->boards->slug, v->boards->b[i].mode, v->today, words, sizeof(words));
        if (Kit_TextWidth(words, 16) > Kit_TextWidth(widest, 16)) snprintf(widest, sizeof(widest), "%s", words);
    }
    int at = GamePage_Board(p, v->boards);
    Boards_ModeWordsFor(v->boards->slug, v->boards->b[at].mode, v->today, words, sizeof(words));
    char whole[KIT_LABEL_LEN + 8];
    snprintf(whole, sizeof(whole), "< %s >", widest);
    float w = (float)Kit_TextWidth(whole, 16) + 32.0f;
    if (w > PAGE_RIGHT_W) w = PAGE_RIGHT_W;
    KitItem *it = Kit_Add(l, WID_PAGE_BOARD, KIT_STEPPER, (KitRect){PAGE_RIGHT_X, PAGE_BOARD_Y, w, 32}, words);
    if (!it) return;
    it->value = at;
    it->min = 0;
    it->max = v->boards->count - 1;
    it->flags |= KIT_WRAP | KIT_NOFOCUS; /* X or pad X steps it */
}

static KitItem *Small(KitList *l, int id, KitRect r, const char *label) {
    KitItem *it = Kit_Add(l, id, KIT_BUTTON, r, label);
    if (!it) return NULL;
    it->style = KIT_STYLE_SMALL;
    it->textSize = 8;
    it->flags |= KIT_NOFOCUS;
    return it;
}

/* "< 3 / 12 >" left of the X: two arrows round a slot as wide as the
 * longest count. render.c writes the count. */
static void Flip(KitList *l, const GamePageView *v) {
    char widest[24];
    snprintf(widest, sizeof(widest), "%d / %d", v->count, v->count);
    float nextX = CLOSE_BTN_X - 12 - STRIP_ARROW_W, prevX = nextX - (float)Kit_TextWidth(widest, 8) - 16.0f - STRIP_ARROW_W;
    KitItem *prev = Small(l, WID_PAGE_PREV, (KitRect){prevX, CLOSE_BTN_Y, STRIP_ARROW_W, CLOSE_BTN_SIZE}, "<");
    KitItem *next = Small(l, WID_PAGE_NEXT, (KitRect){nextX, CLOSE_BTN_Y, STRIP_ARROW_W, CLOSE_BTN_SIZE}, ">");
    if (v->count < 2) {
        Kit_Disable(prev, "Only one machine on the row.");
        Kit_Disable(next, "Only one machine on the row.");
    }
}

/* "< Hall song >" where Scores has its board, as wide as the wider choice. */
static const char *const kSongsViews[SONGS_VIEWS] = {[SONGS_HALL] = "Hall song", [SONGS_WHO] = "Who plays what"};

static int ViewOf(const GamePage *p) { return p->songsView == SONGS_WHO ? SONGS_WHO : SONGS_HALL; }

bool GamePage_SongsShown(const GamePage *p, const GamePageView *v) {
    return v->tool && GamePage_ShownTab(p, v->tool) == GT_SONGS && ViewOf(p) == SONGS_HALL;
}

int GamePage_SongCount(const GamePageData *d) { return d ? 1 + d->libraryCount : 1; }

const char *GamePage_SongTitle(const GamePageData *d, int entry) {
    if (entry <= 0 || !d || entry > d->libraryCount) return "Built-in theme";
    return d->library[entry - 1].title;
}

int GamePage_SongOf(int widgetId) {
    int e = widgetId - WID_PAGE_SONG;
    return e >= 0 && e <= PAGE_LIBRARY_MAX ? e : -1;
}

/* A row's title cut so the "In the hall" badge after it still fits. */
static void SongLabel(char *out, size_t size, const char *title, bool badge) {
    int room = PAGE_RIGHT_W - 20 - 12 - (badge ? Kit_TextWidth("In the hall", 8) + 12 + 12 : 0);
    int n = (int)strlen(title);
    snprintf(out, size, "%s", title);
    while (n > 1 && Kit_TextWidth(out, 16) > room) snprintf(out, size, "%.*s...", --n, title);
}

static void SongRows(KitList *l, const GamePage *p, const GamePageView *v) {
    int count = GamePage_SongCount(v->data);
    KitScroll sc = p->songs;
    Kit_ScrollClamp(&sc, count, PAGE_SONGS_VISIBLE);
    for (int k = 0; k < PAGE_SONGS_VISIBLE && sc.top + k < count; k++) {
        int e = sc.top + k;
        bool hall = v->data && v->data->hallEntry == e;
        char label[KIT_LABEL_LEN];
        SongLabel(label, sizeof(label), GamePage_SongTitle(v->data, e), hall);
        KitRect r = {PAGE_RIGHT_X, (float)(PAGE_SONGS_Y + k * PAGE_SONG_PITCH), PAGE_RIGHT_W, PAGE_SONG_H};
        KitItem *it = Kit_Add(l, WID_PAGE_SONG + e, KIT_ROW, r, label);
        if (it && hall) snprintf(it->badge, sizeof(it->badge), "In the hall");
    }
}

static void Songs(KitList *l, const GamePage *p, const GamePageView *v) {
    if (!v->tool || GamePage_ShownTab(p, v->tool) != GT_SONGS) return;
    char whole[KIT_LABEL_LEN + 8];
    snprintf(whole, sizeof(whole), "< %s >", kSongsViews[SONGS_WHO]);
    float w = (float)Kit_TextWidth(whole, 16) + 32.0f;
    KitItem *it = Kit_Add(l, WID_PAGE_SONGS_VIEW, KIT_STEPPER, (KitRect){PAGE_RIGHT_X, PAGE_BOARD_Y, w, 32}, kSongsViews[ViewOf(p)]);
    if (it) {
        it->value = ViewOf(p);
        it->min = 0;
        it->max = SONGS_VIEWS - 1;
        it->flags |= KIT_WRAP | KIT_NOFOCUS; /* X or pad X turns it, as it turns the boards */
    }
    if (ViewOf(p) != SONGS_HALL) return;
    SongRows(l, p, v);
    l->nav = 0; /* the buttons and the songs are one column for the keys: GamePage_SongKeys */
}

#define PAGE_EDIT_W 64
#define PAGE_REMOVE_W 80

static void Admin(KitList *l, const GamePageView *v) {
    if (!v->admin) return;
    float right = PAGE_X + 800;
    Small(l, WID_PAGE_REMOVE, (KitRect){right - PAGE_REMOVE_W, PAGE_ADMIN_Y, PAGE_REMOVE_W, 24}, "Remove");
    Small(l, WID_PAGE_EDIT, (KitRect){right - PAGE_REMOVE_W - 8 - PAGE_EDIT_W, PAGE_ADMIN_Y, PAGE_EDIT_W, 24}, "Edit");
}

void GamePage_Layout(const GamePage *p, const GamePageView *v, KitList *l) {
    Kit_Clear(l, KIT_NAV_Y | KIT_WRAP_Y);
    Screens_AddBack(l);
    Buttons(l, v);
    Tabs(l, p, v);
    BoardStepper(l, p, v);
    Songs(l, p, v);
    Flip(l, v);
    Admin(l, v);
    Screens_AddClose(l);
}

/* ----------------------------------------------------------------- keys */

static GamePageAction Act(GamePageVerb verb, int value) { return (GamePageAction){verb, value}; }

static GamePageAction StepTab(GamePage *p, const GamePageView *v, int dir) {
    int n = GamePage_TabCount(v->tool), cell = GamePage_CellOf(v->tool, GamePage_ShownTab(p, v->tool));
    p->tab = GamePage_TabAt(v->tool, ((cell + dir) % n + n) % n);
    return Act(GA_TAB, p->tab);
}

static GamePageAction Pressed(GamePage *p, const GamePageView *v, const KitEvent *ev) {
    if (ev->type == KIT_EV_CHANGE && ev->id == WID_PAGE_TABS) {
        p->tab = GamePage_TabAt(v->tool, ev->value);
        return Act(GA_TAB, p->tab);
    }
    if (ev->type == KIT_EV_CHANGE && ev->id == WID_PAGE_BOARD) {
        GamePage_SetBoard(p, v->boards, ev->value);
        return Act(GA_BOARD, ev->value);
    }
    if (ev->type == KIT_EV_CHANGE && ev->id == WID_PAGE_SONGS_VIEW) {
        p->songsView = ev->value == SONGS_WHO ? SONGS_WHO : SONGS_HALL;
        return Act(GA_VIEW, p->songsView);
    }
    if (ev->type != KIT_EV_PRESS) return Act(GA_NONE, 0);
    int song = GamePage_SongOf(ev->id);
    if (song >= 0 && GamePage_SongsShown(p, v)) return Act(GA_SONG, song);
    switch (ev->id) {
        case WID_BACK: return Act(GA_BACK, 0);
        case WID_PAGE_PLAY: return Act(GA_PLAY, 0);
        case WID_PAGE_DAILY: return Act(GA_DAILY, 0);
        case WID_PAGE_NAME: return Act(GA_NAME, 0);
        case WID_PAGE_FAV: return Act(GA_FAVOURITE, 0);
        case WID_PAGE_EDIT: return Act(v->admin ? GA_EDIT : GA_NONE, 0);
        case WID_PAGE_REMOVE: return Act(v->admin ? GA_REMOVE : GA_NONE, 0);
        case WID_PAGE_PREV: return Act(GA_FLIP, -1);
        case WID_PAGE_NEXT: return Act(GA_FLIP, 1);
        default: return Act(GA_NONE, 0);
    }
}

GamePageAction GamePage_Read(GamePage *p, const GamePageView *v, const KitEvent *ev, const Input *in) {
    GamePageAction a = Pressed(p, v, ev);
    if (a.verb != GA_NONE) return a;
    if (Keymap_Pressed(KA_BACK, in) || Keymap_Pressed(KA_PAGE_CLOSE, in)) return Act(GA_BACK, 0);
    if (Keymap_Pressed(KA_PAGE_PREV, in)) return Act(GA_FLIP, -1);
    if (Keymap_Pressed(KA_PAGE_NEXT, in)) return Act(GA_FLIP, 1);
    if (Keymap_Pressed(KA_UI_LEFT, in)) return StepTab(p, v, -1);
    if (Keymap_Pressed(KA_UI_RIGHT, in)) return StepTab(p, v, 1);
    if (Keymap_Pressed(KA_PAGE_BOARD, in) && v->tool && GamePage_ShownTab(p, v->tool) == GT_SONGS) {
        p->songsView = ViewOf(p) == SONGS_HALL ? SONGS_WHO : SONGS_HALL;
        return Act(GA_VIEW, p->songsView);
    }
    int board = GamePage_Board(p, v->boards);
    if (Keymap_Pressed(KA_PAGE_BOARD, in) && GamePage_ShownTab(p, v->tool) == GT_SCORES && board >= 0) {
        GamePage_SetBoard(p, v->boards, (board + 1) % v->boards->count);
        return Act(GA_BOARD, GamePage_Board(p, v->boards));
    }
    if (Keymap_Pressed(KA_HALL_DAILY, in)) return Act(GA_DAILY, 0);
    if (Keymap_Pressed(KA_OPEN_DAILY, in)) return Act(GA_DESK, 0);
    if (Keymap_Pressed(KA_HALL_FAVOURITE, in)) return Act(GA_FAVOURITE, 0);
    if (!v->tool && Keymap_Pressed(KA_OPEN_GAME_NAME, in)) return Act(GA_NAME, 0);
    if (v->admin && Keymap_Pressed(KA_ADMIN_EDIT, in)) return Act(GA_EDIT, 0);
    if (v->admin && Keymap_Pressed(KA_ADMIN_REMOVE, in)) return Act(GA_REMOVE, 0);
    return Act(GA_NONE, 0);
}

/* ---------------------------------------------------------------- lines */

static void SongHelp(const GamePageView *v, const GamePageData *d, int entry, char *out, size_t size) {
    if (entry < 0 || !v->tool) return;
    const char *what = entry == 0 ? "The arcade's own tune, back in the hall." : "Plays this in the hall from now on.";
    if (d && d->hallEntry == entry) what = "The hall plays this now.";
    snprintf(out, size, "%s%s", what, v->hallMusic ? "" : " Hall music is off in Settings > Sound.");
}

void GamePage_Help(const GamePageView *v, const GamePageData *d, int id, char *out, size_t size) {
    out[0] = '\0';
    switch (id) {
        case WID_PAGE_PLAY:
            if (v->play == HP_INSTALL) snprintf(out, size, "Builds it from its source folder and installs it.");
            else if (v->play == HP_PLAY)
                snprintf(out, size, "%s", v->tool ? "Back to the hall, where Chipforge opens." : "Back to the hall, where the coin drops and it starts.");
            break;
        case WID_PAGE_DAILY: snprintf(out, size, "The daily challenge desk: today's dailies, your streak and the world's best."); break;
        case WID_PAGE_NAME:
            if (!d || !d->playsAs[0]) snprintf(out, size, "No name yet: give it one of its own, or set your arcade name.");
            else if (d->ownName) snprintf(out, size, "Plays as %.32s here. Only this game uses that name.", d->playsAs);
            else snprintf(out, size, "Plays as %.32s, your arcade name. It can have one of its own.", d->playsAs);
            break;
        case WID_PAGE_FAV:
            snprintf(out, size, "%s", v->favourite ? "A favourite, first on the row. Press again to take the star off." : "Favourites come first on the row.");
            break;
        default: SongHelp(v, d, GamePage_SongOf(id), out, size); break;
    }
}

void GamePage_Hint(const GamePage *p, const GamePageView *v, InputDevice hints, char *out, size_t size) {
    bool pad = hints == DEV_PAD;
    InputDevice dev = pad ? DEV_PAD : DEV_KEYBOARD;
    char press[24], back[24], board[24], prev[24], next[24], tabs[40], line[48] = "";
    Keymap_Hint(KA_UI_PRESS, dev, press, sizeof(press));
    Keymap_Hint(KA_BACK, dev, back, sizeof(back));
    Keymap_Hint(KA_PAGE_BOARD, dev, board, sizeof(board));
    Keymap_Hint(KA_PAGE_PREV, dev, prev, sizeof(prev));
    Keymap_Hint(KA_PAGE_NEXT, dev, next, sizeof(next));
    if (pad) {
        char a[16], b[16];
        Keymap_Hint(KA_UI_PREV_TAB, DEV_PAD, a, sizeof(a));
        Keymap_Hint(KA_UI_NEXT_TAB, DEV_PAD, b, sizeof(b));
        snprintf(tabs, sizeof(tabs), "%s/%s", a, b);
    } else {
        snprintf(tabs, sizeof(tabs), "Left/Right");
    }
    if (GamePage_ShownTab(p, v->tool) == GT_SCORES && v->boards && v->boards->count > 1)
        snprintf(line, sizeof(line), "%s board   ", board);
    if (v->tool && GamePage_ShownTab(p, v->tool) == GT_SONGS) snprintf(line, sizeof(line), "%s list   ", board);
    snprintf(out, size, "%s choose   %s press   %s tab   %s%s/%s machine   %s back", pad ? "D-pad" : "Up/Down", press, tabs, line, prev,
             next, back);
}

/* ---------------------------------------------------------------- songs */

/* The keys' one column while the songs show: the page's buttons that can be
 * pressed, then every song (shown or scrolled away). */
static int SongColumn(const KitList *l, const GamePageView *v, int *col) {
    static const int kButtons[] = {WID_PAGE_PLAY, WID_PAGE_DAILY, WID_PAGE_NAME, WID_PAGE_FAV};
    int n = 0;
    for (size_t b = 0; b < sizeof(kButtons) / sizeof(kButtons[0]); b++)
        if (Kit_CanFocus(Kit_Get(l, kButtons[b]))) col[n++] = kButtons[b];
    for (int e = 0; e < GamePage_SongCount(v->data); e++) col[n++] = WID_PAGE_SONG + e;
    return n;
}

/* The wheel moved the songs: keys on one now out of sight come to the
 * nearest one still shown. */
static bool KeepSongInSight(const GamePage *p, KitState *k) {
    int e = GamePage_SongOf(k->focus), top = p->songs.top;
    if (e < 0 || (e >= top && e < top + PAGE_SONGS_VISIBLE)) return false;
    k->focus = WID_PAGE_SONG + (e < top ? top : top + PAGE_SONGS_VISIBLE - 1);
    return true;
}

bool GamePage_SongKeys(GamePage *p, const GamePageView *v, KitState *k, const KitList *l, const Input *in, bool wheelFree) {
    if (!GamePage_SongsShown(p, v)) return false;
    int count = GamePage_SongCount(v->data);
    int dir = Keymap_Pressed(KA_UI_DOWN, in) ? 1 : (Keymap_Pressed(KA_UI_UP, in) ? -1 : 0);
    if (dir != 0) {
        int col[4 + PAGE_LIBRARY_MAX + 1], n = SongColumn(l, v, col), at = -1;
        for (int i = 0; i < n; i++) if (col[i] == k->focus) at = i;
        int to = at < 0 ? (dir > 0 ? 0 : n - 1) : ((at + dir) % n + n) % n;
        k->focus = col[to];
        int e = GamePage_SongOf(col[to]);
        if (e >= 0) Kit_ScrollShow(&p->songs, e, count, PAGE_SONGS_VISIBLE);
        return true;
    }
    if (!wheelFree || !Kit_ScrollWheel(&p->songs, count, PAGE_SONGS_VISIBLE, in->wheel)) return false;
    return KeepSongInSight(p, k);
}
