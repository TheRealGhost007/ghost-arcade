#define _POSIX_C_SOURCE 200809L /* st_mtim */
#include "settings.h"
#include "screens.h"
#include "machines.h"
#include "attract.h"
#include "prefs.h"
#include "../sync/syncdata.h"
#include "../sync/updatecheck.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *const kTabs[ST_COUNT] = {
    [ST_PLAYER] = "Player", [ST_SOUND] = "Sound",       [ST_SCREEN] = "Screen",
    [ST_ONLINE] = "Online", [ST_CONTROLS] = "Controls", [ST_MACHINES] = "Machines",
};

const char *Settings_TabLabel(int tab) { return (tab >= 0 && tab < ST_COUNT) ? kTabs[tab] : ""; }

#define EDIT_X 652
#define EDIT_W 96
#define CLEAR_X 760
#define CLEAR_W 120
#define ACTION_W 300   /* a button standing where a row's label would */
#define TOGGLE_W 144
#define COUNTER_W 70   /* "32 / 32" and its gap, inside a field being typed in */

int Settings_FieldChars(bool typing) {
    int room = SET_FIELD_W - 2 * SET_FIELD_PAD - (typing ? COUNTER_W : 0);
    return (room + 2) / SET_CHAR_W; /* the last character needs no gap after it */
}

void Settings_Init(Settings *s) {
    memset(s, 0, sizeof(*s));
    s->fieldFor = SET_ARCADE_NAME;
    Kit_Init(&s->editKit);
}

/* ------------------------------------------------------------ building */

static SettingsRow *AddRow(SettingsPage *p, SettingsRowKind kind, float y, float h, const char *label, const char *help) {
    if (p->rowCount >= SET_MAX_ROWS) return NULL;
    SettingsRow *r = &p->rows[p->rowCount++];
    memset(r, 0, sizeof(*r));
    r->kind = kind;
    r->r = (KitRect){SET_ROW_X, y, SET_ROW_W, h};
    snprintf(r->label, sizeof(r->label), "%s", label ? label : "");
    r->help = help;
    r->noteX = SET_CONTROL_X;
    r->tablePos = -1;
    return r;
}

static float RowY(int i) { return (float)(SET_ROW_Y + i * SET_ROW_PITCH); }

/* A control in a row; focusable ones are remembered left to right. */
static KitItem *Control(SettingsPage *p, SettingsRow *row, int id, KitKind kind, KitRect r, const char *label) {
    KitItem *it = Kit_Add(&p->list, id, kind, r, label);
    if (it && row && row->controlCount < SET_ROW_CONTROLS) row->controls[row->controlCount++] = id;
    return it;
}

static KitRect ControlRect(const SettingsRow *row, float x, float w) {
    return (KitRect){x, row->r.y + (SET_ROW_H - SET_FIELD_H) / 2.0f, w, SET_FIELD_H};
}

static void Note(SettingsRow *row, int line, NoteTone tone, const char *text) {
    snprintf(row->note[line], sizeof(row->note[line]), "%s", text);
    row->tone[line] = tone;
}

/* Value controls carry their row's label: never drawn, but it names them
 * (the screenshot harness asks for controls by label). */
static KitItem *Toggle(SettingsPage *p, SettingsRow *row, int id, bool on) {
    KitItem *it = Control(p, row, id, KIT_TOGGLE, ControlRect(row, SET_CONTROL_X, TOGGLE_W), row->label);
    if (it) it->value = on ? 1 : 0;
    row->noteX = SET_CONTROL_X + TOGGLE_W + 16;
    return it;
}

static KitItem *Blocks(SettingsPage *p, SettingsRow *row, int id, int value) {
    KitItem *it = Control(p, row, id, KIT_SLIDER, ControlRect(row, SET_CONTROL_X, KIT_BLOCK_PITCH * 11), row->label);
    if (it) {
        it->value = value;
        it->max = 10;
    }
    row->noteX = SET_CONTROL_X + KIT_BLOCK_PITCH * 11 + 16;
    return it;
}

/* --------------------------------------------------------------- names */

static const char *OwnName(const SettingsView *v, int index) {
    const char *key = index == SET_ARCADE_NAME ? PROFILE_DEFAULT_KEY : v->manifest->games[index].name;
    return Profiles_GetOwnUsername(v->profiles, key);
}

static void NameIds(int index, int *field, int *edit, int *cancel) {
    bool arcade = index == SET_ARCADE_NAME;
    *field = arcade ? WID_SET_NAME_FIELD : WID_SET_GAME_FIELD + index;
    *edit = arcade ? WID_SET_NAME_EDIT : WID_SET_GAME_EDIT + index;
    *cancel = arcade ? WID_SET_NAME_CANCEL : WID_SET_GAME_CLEAR + index;
}

/* The box, then Edit (or Set) and, for a game with a name of its own,
 * Clear; while typing in it, Save and Cancel. */
static void NameRow(SettingsPage *p, const Settings *s, const SettingsView *v, SettingsRow *row, int index) {
    int fieldId, editId, cancelId;
    NameIds(index, &fieldId, &editId, &cancelId);
    bool typing = s->field.editing && s->fieldFor == index;
    const char *own = OwnName(v, index);
    row->field = fieldId;
    row->nameIndex = index;
    snprintf(row->value, sizeof(row->value), "%s", own);
    if (!own[0]) {
        snprintf(row->value, sizeof(row->value), "%s", index == SET_ARCADE_NAME ? "Not set yet" : "Uses your arcade name");
        row->placeholder = true;
    }
    char boxLabel[KIT_LABEL_LEN];
    snprintf(boxLabel, sizeof(boxLabel), "%.40s box", row->label);
    KitItem *box = Kit_Add(&p->list, fieldId, KIT_BUTTON, ControlRect(row, SET_FIELD_X, SET_FIELD_W), boxLabel);
    if (box) box->flags |= KIT_NOFOCUS | KIT_SELFDRAWN; /* a click starts typing; keys and the pad use Edit */
    const char *editLabel = typing ? "Save" : (own[0] || index == SET_ARCADE_NAME ? "Edit" : "Set");
    Control(p, row, editId, KIT_BUTTON, ControlRect(row, EDIT_X, EDIT_W), editLabel);
    if (typing) {
        Control(p, row, cancelId, KIT_BUTTON, ControlRect(row, CLEAR_X, CLEAR_W), "Cancel");
    } else if (index != SET_ARCADE_NAME && own[0]) {
        Control(p, row, cancelId, KIT_BUTTON, ControlRect(row, CLEAR_X, CLEAR_W), "Clear");
    }
}

/* The game's name cut to the label column. */
static void GameLabel(char *out, size_t size, const char *name) {
    int room = SET_FIELD_X - 8 - SET_LABEL_X, n = (int)strlen(name);
    snprintf(out, size, "%s", name);
    while (n > 1 && Kit_TextWidth(out, 16) > room) snprintf(out, size, "%.*s...", --n, name);
}

/* "< Last played >": as wide as the widest choice, so the arrows hold still. */
static KitItem *Choice(SettingsPage *p, SettingsRow *row, int id, const char *const *words, int count, int value, const char *widest) {
    int v = value >= 0 && value < count ? value : 0;
    char whole[KIT_LABEL_LEN + 8];
    snprintf(whole, sizeof(whole), "< %s >", widest);
    float w = (float)Kit_TextWidth(whole, 16) + 32.0f;
    KitItem *it = Control(p, row, id, KIT_STEPPER, ControlRect(row, SET_CONTROL_X, w), words[v]);
    if (it) {
        it->value = v;
        it->min = 0;
        it->max = count - 1;
        it->flags |= KIT_WRAP;
    }
    row->noteX = SET_CONTROL_X + (int)w + 16;
    return it;
}

/* The page's scrolling list: `top` kept inside it (the catalog may have
 * shrunk since it was scrolled). */
static int Table(SettingsPage *p, int top, int count, int visible, float y) {
    int maxTop = count > visible ? count - visible : 0;
    if (top > maxTop) top = maxTop;
    if (top < 0) top = 0;
    p->table = (SettingsTable){true, top, count, visible, y, SET_NAMES_PITCH};
    return top;
}

static const char *const kStartOn[2] = {"Last played", "First machine"};

static void StartOnRow(SettingsPage *p, int i, bool last) {
    SettingsRow *row = AddRow(p, SROW_CONTROL, RowY(i), SET_ROW_H, "Start on", "Which machine the hall opens on: the one you played last, or the first on the row.");
    if (!row) return;
    Choice(p, row, WID_SET_START_ON, kStartOn, 2, last ? 0 : 1, "First machine"); /* the help line says the rest */
}

static void LayoutPlayer(SettingsPage *p, const Settings *s, const SettingsView *v) {
    p->intro = "The names the games put on their score tables, and where the hall opens.";
    SettingsRow *row = AddRow(p, SROW_CONTROL, RowY(0), SET_ROW_H, "Arcade name",
                              "Every game puts this name on its score tables, online ones included.");
    if (row) NameRow(p, s, v, row, SET_ARCADE_NAME);
    StartOnRow(p, 1, v->startOnLast);
    AddRow(p, SROW_HEADING, SET_NAMES_HEAD_Y, 16, "Names for single games", NULL);
    int count = v->manifest ? v->manifest->count : 0;
    int top = Table(p, s->names.top, count, SET_NAMES_VISIBLE, SET_NAMES_Y);
    if (count == 0) {
        SettingsRow *none = AddRow(p, SROW_INFO, SET_NAMES_Y, SET_ROW_H, "", NULL);
        if (none) Note(none, 0, NOTE_DIM, "No machines in the arcade yet.");
        if (none) none->noteX = SET_LABEL_X;
        return;
    }
    for (int k = 0; k < SET_NAMES_VISIBLE && top + k < count; k++) {
        int g = top + k;
        char label[40];
        GameLabel(label, sizeof(label), v->manifest->games[g].name);
        SettingsRow *gr = AddRow(p, SROW_CONTROL, (float)(SET_NAMES_Y + k * SET_NAMES_PITCH), SET_ROW_H, label,
                                 "Only this game uses it. Clear it to use your arcade name again.");
        if (!gr) continue;
        gr->tablePos = g;
        NameRow(p, s, v, gr, g);
    }
}

static void LevelNote(SettingsRow *row, int level) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", level * 10);
    Note(row, 0, level ? NOTE_TEXT : NOTE_DIM, level ? buf : "Off");
}

/* What the hall plays, beside its switch. */
static void HallMusicRow(SettingsPage *p, int i, const SettingsView *v) {
    SettingsRow *row = AddRow(p, SROW_CONTROL, RowY(i), SET_ROW_H, "Hall music",
                              "A soft tune in the hall, quiet while a game is up. Chipforge can give the hall a song.");
    if (!row) return;
    Toggle(p, row, WID_SET_HALL_MUSIC, v->hallMusic);
    char line[SET_NOTE_LEN];
    snprintf(line, sizeof(line), "%.44s", v->hallSong && v->hallSong[0] ? v->hallSong : "Built-in theme");
    /* What it plays even without a sound device: Coin sound says that. */
    Note(row, 0, v->hallMusic ? NOTE_TEXT : NOTE_DIM, v->hallMusic ? line : "The hall is quiet.");
}

static void LayoutSound(SettingsPage *p, const SettingsView *v) {
    p->intro = "Music volume is shared with every game. Hall music and sounds are the launcher's own.";
    p->footnote = "* Shared with every game: a change here shows up in all of them.";
    SettingsRow *row = AddRow(p, SROW_CONTROL, RowY(0), SET_ROW_H, "Music volume", "How loud the music is, in every game and in the hall.");
    if (row) {
        row->shared = true;
        int level = (v->musicVolume + 5) / 10;
        Blocks(p, row, WID_SET_MUSIC, level);
        LevelNote(row, level);
    }
    HallMusicRow(p, 1, v);
    row = AddRow(p, SROW_CONTROL, RowY(2), SET_ROW_H, "Hall sounds",
                 "The coin and the clicks in the hall. Each game keeps its own effects volume.");
    if (row) {
        Blocks(p, row, WID_SET_HALL_SOUNDS, v->hallSounds);
        LevelNote(row, v->hallSounds);
    }
    row = AddRow(p, SROW_CONTROL, RowY(3), SET_ROW_H, "Coin sound", "Plays the coin at the hall sounds volume.");
    if (row) {
        KitItem *b = Control(p, row, WID_SET_COIN_TEST, KIT_BUTTON, ControlRect(row, SET_CONTROL_X, 120), "Play");
        row->noteX = SET_CONTROL_X + 120 + 16;
        if (!v->soundReady) {
            Kit_Disable(b, "No sound device");
            Note(row, 0, NOTE_DIM, "No sound device here.");
        } else if (v->hallSounds == 0) {
            Note(row, 0, NOTE_DIM, "Hall sounds are off.");
        }
    }
}

static void SharedToggle(SettingsPage *p, int i, const char *label, const char *help, int id, bool on) {
    SettingsRow *row = AddRow(p, SROW_CONTROL, RowY(i), SET_ROW_H, label, help);
    if (!row) return;
    row->shared = true;
    Toggle(p, row, id, on);
}

static const char *const kHints[HINTS_COUNT] = {[HINTS_AUTO] = "Auto", [HINTS_KEYS] = "Keyboard", [HINTS_PAD] = "Xbox pad"};
static const char *const kHintNotes[HINTS_COUNT] = {
    [HINTS_AUTO] = "What you used last.",
    [HINTS_KEYS] = "Always keys.",
    [HINTS_PAD] = "Always pad buttons.",
};

static void HintsRow(SettingsPage *p, int i, int hints) {
    SettingsRow *row = AddRow(p, SROW_CONTROL, RowY(i), SET_ROW_H, "Button hints",
                              "What hints name: keyboard keys or the pad's buttons. Auto follows what you used last.");
    if (!row) return;
    int h = hints >= 0 && hints < HINTS_COUNT ? hints : HINTS_AUTO;
    Choice(p, row, WID_SET_BUTTON_HINTS, kHints, HINTS_COUNT, h, "Keyboard");
    Note(row, 0, NOTE_DIM, kHintNotes[h]);
}

static const char *const kAttract[ATTRACT_SETTINGS] = {[ATTRACT_OFF] = "Off", [ATTRACT_1MIN] = "1 min", [ATTRACT_2MIN] = "2 min"};
static const char *const kAttractNotes[ATTRACT_SETTINGS] = {
    [ATTRACT_OFF] = "The hall waits as it is.",
    [ATTRACT_1MIN] = "After a quiet minute.",
    [ATTRACT_2MIN] = "After two quiet minutes.",
};

static void AttractRow(SettingsPage *p, int i, int setting) {
    SettingsRow *row = AddRow(p, SROW_CONTROL, RowY(i), SET_ROW_H, "Attract mode",
                              "Left alone, the hall shows off its machines one by one. Any key or button wakes it.");
    if (!row) return;
    int a = setting >= 0 && setting < ATTRACT_SETTINGS ? setting : ATTRACT_1MIN;
    Choice(p, row, WID_SET_ATTRACT, kAttract, ATTRACT_SETTINGS, a, "1 min");
    Note(row, 0, NOTE_DIM, kAttractNotes[a]);
}

static void LayoutScreen(SettingsPage *p, const SettingsView *v) {
    p->intro = "How the arcade looks. The three marked * are shared with every game.";
    p->footnote = "* Shared with every game: a change here shows up in all of them.";
    SharedToggle(p, 0, "Fullscreen", "Fills the whole screen. F11 or Alt+Enter does the same, in every game.", WID_SET_FULLSCREEN,
                 v->fullscreen);
    SharedToggle(p, 1, "Reduced flashing", "Steadies flickering lights and flashes, here and in every game.", WID_SET_FLASHING,
                 v->reducedFlashing);
    SharedToggle(p, 2, "Screen shake", "Lets the games shake the screen on hits and explosions.", WID_SET_SHAKE, v->screenShake);
    HintsRow(p, 3, v->buttonHints);
    AttractRow(p, 4, v->attract);
}

/* A button where the label would be, its news to the right. */
static SettingsRow *ActionRow(SettingsPage *p, int i, int id, const char *label, const char *help, KitItem **out) {
    SettingsRow *row = AddRow(p, SROW_CONTROL, RowY(i), SET_ROW_H, "", help);
    if (!row) return NULL;
    *out = Control(p, row, id, KIT_BUTTON, ControlRect(row, SET_LABEL_X, ACTION_W), label);
    return row;
}

static void CheckNotes(SettingsRow *row, const SettingsView *v) {
    char ago[48], line[SET_NOTE_LEN];
    Settings_Ago(v->checkedAt, v->now, ago, sizeof(ago));
    snprintf(line, sizeof(line), v->checkedAt > 0 ? "Last checked %s" : "Not checked yet", ago);
    Note(row, 0, NOTE_DIM, v->checking ? "Asking GitHub..." : line);
    if (!v->checkUpdates) Note(row, 1, NOTE_DIM, "Update checks are off.");
    else if (v->checkNoAnswer && !v->checking) Note(row, 1, NOTE_BAD, "No answer from GitHub. Try again later.");
    else if (v->updateStatus == UPDATE_BEHIND) Note(row, 1, NOTE_HOT, "A newer version is on GitHub.");
    else if (v->updateStatus == UPDATE_CURRENT) Note(row, 1, NOTE_GOOD, "You have the newest version.");
    else if (v->checkedAt > 0) Note(row, 1, NOTE_TEXT, "GitHub does not know this build.");
}

static void SyncNotes(SettingsRow *row, const SettingsView *v) {
    char ago[48], line[SET_NOTE_LEN];
    Settings_Ago(v->lastSync, v->now, ago, sizeof(ago));
    snprintf(line, sizeof(line), v->lastSync > 0 ? "Last sync %s" : "No sync yet", ago);
    Note(row, 0, NOTE_DIM, v->syncing ? "Sending scores, fetching the boards..." : line);
    if (!v->sharing) Note(row, 1, NOTE_DIM, "Turn on Share my scores to sync.");
    else if (v->syncNoAnswer && !v->syncing) Note(row, 1, NOTE_BAD, "Nothing back from the server yet.");
}

static void LayoutOnline(SettingsPage *p, const SettingsView *v) {
    p->intro = "Scores and update checks. Nothing is sent unless it is on.";
    SettingsRow *row = AddRow(p, SROW_CONTROL, RowY(0), SET_ROW_H, "Share my scores",
                              "Sends your arcade name, the game, mode, score and date, and a random ID.");
    if (row) {
        Toggle(p, row, WID_SET_SHARING, v->sharing);
        Note(row, 0, NOTE_DIM, v->sharing ? "Your scores go online." : "Scores stay on this computer.");
    }
    row = AddRow(p, SROW_CONTROL, RowY(1), SET_ROW_H, "Check for updates", "One anonymous request to GitHub, at most every six hours.");
    if (row) {
        Toggle(p, row, WID_SET_CHECKS, v->checkUpdates);
        Note(row, 0, NOTE_DIM, v->checkUpdates ? "At most every six hours." : "Nothing is asked of GitHub.");
    }
    KitItem *b = NULL;
    row = ActionRow(p, 2, WID_SET_CHECK_NOW, v->checking ? "Checking..." : "Check now", "Asks GitHub now whether a newer version exists.", &b);
    if (row) {
        if (!v->checkUpdates) Kit_Disable(b, "Update checks are off");
        else if (v->checking) Kit_Disable(b, "Asking GitHub now");
        CheckNotes(row, v);
    }
    row = ActionRow(p, 3, WID_SET_UPDATE_DETAILS, "Update details", "Opens the Updates screen: this build, the newest, how to update.", &b);
    if (row) Note(row, 0, NOTE_DIM, "This build, the newest, how to update.");
    row = ActionRow(p, 4, WID_SET_SYNC_NOW, v->syncing ? "Syncing..." : "Sync scores now", "Sends your new scores and fetches the online boards now.", &b);
    if (row) {
        if (!v->sharing) Kit_Disable(b, "Share my scores is off");
        else if (v->syncing) Kit_Disable(b, "Syncing now");
        SyncNotes(row, v);
    }
    row = AddRow(p, SROW_INFO, RowY(5), SET_ROW_H, "Server", NULL);
    if (row) {
        char host[SET_NOTE_LEN];
        Settings_ServerHost(v->serverUrl, host, sizeof(host));
        Note(row, 0, NOTE_TEXT, host[0] ? host : "None set");
        Note(row, 1, NOTE_DIM, "Set in online.conf, on purpose, not here.");
    }
}

/* ------------------------------------------------------------ machines */

/* Small buttons with 8 px words, so a machine's six (admin's Edit and
 * Remove too) fit beside its name and status. */
#define MACH_BTN_H 24
#define MACH_FAV_X 344
#define MACH_FAV_W 120 /* as wide as "Unfavourite", so the row never shifts */
#define MACH_UP_X 472
#define MACH_UP_W 48
#define MACH_DOWN_X 528
#define MACH_DOWN_W 64
#define MACH_HIDE_X 600
#define MACH_HIDE_W 64
#define MACH_EDIT_X 680 /* admin's, a little apart */
#define MACH_EDIT_W 64
#define MACH_REMOVE_X 752
#define MACH_REMOVE_W 80
#define MACH_ADD_W 136
#define MACH_STAR_W 22 /* the 16 px star before a favourite's name, and its gap */
#define MACH_NAME_ROOM (MACH_FAV_X - 8 - SET_LABEL_X - MACH_STAR_W) /* "Girderclimb" fits */

/* Why Up or Down cannot move this machine, or NULL when it can. */
static const char *MoveBlock(const SettingsView *v, int index, int dir) {
    if (Row_CanMove(v->row, v->mine, index, dir)) return NULL;
    int at = Lineup_PlaceOf(v->mine, index), other = Lineup_IndexAt(v->mine, at + dir);
    if (other < 0) return dir < 0 ? "Already first." : "Already last.";
    if (v->row->m[index].favourite != v->row->m[other].favourite) return "Favourites stay first.";
    return "Its program name cannot be kept in launcher.ini.";
}

static void MachineButton(SettingsPage *p, SettingsRow *row, int id, float x, float w, const char *label, const char *block) {
    KitRect r = {x, row->r.y + (SET_ROW_H - MACH_BTN_H) / 2.0f, w, MACH_BTN_H};
    KitItem *b = Control(p, row, id, KIT_BUTTON, r, label);
    if (!b) return;
    b->style = KIT_STYLE_SMALL;
    b->textSize = 8;
    if (block) Kit_Disable(b, block);
}

/* Under the name: Installed, Not installed or Installing, and hidden. */
static void StatusLine(SettingsRow *row, const SettingsView *v, int i, bool hidden) {
    bool installed = v->installed && v->installed[i];
    MachineStatus st = Machines_Status(installed, v->installing == i);
    snprintf(row->sub, sizeof(row->sub), "%s%s", Machines_StatusWords(st), hidden ? ", hidden" : "");
    row->subTone = st == MS_INSTALLING ? NOTE_HOT : (st == MS_NOT_INSTALLED ? NOTE_TEXT : NOTE_DIM);
}

/* One machine: its star, name and status, then Favourite, Up, Down and
 * Hide, and for admin Edit and Remove. Ids are its catalog index, so the
 * focus follows it when it moves. */
static void MachineRow(SettingsPage *p, const SettingsView *v, int pos, int k) {
    int i = v->mine->index[pos];
    const RowMachine *m = &v->row->m[i];
    char label[40];
    GameLabel(label, sizeof(label), v->manifest->games[i].name);
    int n = (int)strlen(label);
    while (n > 1 && Kit_TextWidth(label, 16) > MACH_NAME_ROOM)
        snprintf(label, sizeof(label), "%.*s...", --n, v->manifest->games[i].name);
    const char *help = v->admin ? "Up and Down set your order; Hide keeps its scores. Edit and Remove change the arcade."
                                : "Favourites come first. Up and Down set your order. Hide keeps its scores.";
    SettingsRow *row = AddRow(p, SROW_CONTROL, (float)(SET_MACH_Y + k * SET_NAMES_PITCH), SET_ROW_H, label, help);
    if (!row) return;
    row->tablePos = pos;
    row->star = m->favourite;
    row->dim = m->hidden;
    row->labelPad = MACH_STAR_W;
    StatusLine(row, v, i, m->hidden);
    const char *noKey = m->slug[0] ? NULL : "Its program name cannot be kept in launcher.ini.";
    MachineButton(p, row, WID_SET_MACH_FAV + i, MACH_FAV_X, MACH_FAV_W, m->favourite ? "Unfavourite" : "Favourite", noKey);
    MachineButton(p, row, WID_SET_MACH_UP + i, MACH_UP_X, MACH_UP_W, "Up", MoveBlock(v, i, -1));
    MachineButton(p, row, WID_SET_MACH_DOWN + i, MACH_DOWN_X, MACH_DOWN_W, "Down", MoveBlock(v, i, 1));
    const char *hide = noKey ? noKey : (Row_CanHide(v->row, i) ? NULL : "One machine always stays on the row.");
    MachineButton(p, row, WID_SET_MACH_HIDE + i, MACH_HIDE_X, MACH_HIDE_W, m->hidden ? "Show" : "Hide", hide);
    if (!v->admin) return;
    MachineButton(p, row, WID_SET_MACH_EDIT + i, MACH_EDIT_X, MACH_EDIT_W, "Edit", NULL);
    MachineButton(p, row, WID_SET_MACH_REMOVE + i, MACH_REMOVE_X, MACH_REMOVE_W, "Remove", NULL);
}

/* "Your order", and for admin Add a machine at the line's right end. */
static void MachinesHeading(SettingsPage *p, const SettingsView *v) {
    SettingsRow *head = AddRow(p, SROW_HEADING, SET_MACH_HEAD_Y, 16, "Your order",
                               v->admin ? "Adds a machine to the arcade, for everyone who plays on this computer." : NULL);
    if (!head || !v->admin) return;
    KitRect r = {SET_ROW_X + SET_ROW_W - MACH_ADD_W, SET_MACH_HEAD_Y - 6, MACH_ADD_W, MACH_BTN_H};
    KitItem *add = Control(p, head, WID_SET_MACH_ADD, KIT_BUTTON, r, "Add a machine");
    if (!add) return;
    add->style = KIT_STYLE_SMALL;
    add->textSize = 8;
}

static void LayoutMachines(SettingsPage *p, const Settings *s, const SettingsView *v) {
    p->intro = v->admin ? "Your row is yours alone. Add, Edit and Remove change the arcade for everyone."
                        : "The row in the hall, for you alone. T on the hall changes the order too.";
    SettingsRow *row = AddRow(p, SROW_CONTROL, RowY(0), SET_ROW_H, "Row order", "How the hall lines the machines up. Favourites always come first.");
    const char *orders[ORDER_COUNT];
    for (int o = 0; o < ORDER_COUNT; o++) orders[o] = Row_OrderName(o);
    if (row) Choice(p, row, WID_SET_ROW_ORDER, orders, ORDER_COUNT, v->rowOrder, "Recently played"); /* no room for a note */
    MachinesHeading(p, v);
    int count = v->mine && v->row && v->manifest ? v->mine->count : 0;
    int top = Table(p, s->machines.top, count, SET_MACH_VISIBLE, SET_MACH_Y);
    if (count == 0) {
        SettingsRow *none = AddRow(p, SROW_INFO, SET_MACH_Y, SET_ROW_H, "", NULL);
        if (none) Note(none, 0, NOTE_DIM, "No machines in the arcade yet.");
        if (none) none->noteX = SET_LABEL_X;
        return;
    }
    for (int k = 0; k < SET_MACH_VISIBLE && top + k < count; k++) MachineRow(p, v, top + k, k);
}

static void LayoutControls(SettingsPage *p, const Settings *s, const SettingsView *v) {
    p->intro = "Every key and pad button, screen by screen. Each game's keys are in that game.";
    p->footnote = "The mouse clicks any button; the wheel scrolls lists and turns volume blocks.";
    SettingsRow *row = AddRow(p, SROW_CONTROL, RowY(0), SET_ROW_H, "Keys for", "Pick a screen to see its keys and pad buttons.");
    int cards = Keymap_CardCountFor(v->admin), card = s->card >= 0 && s->card < cards ? s->card : 0;
    if (row) {
        char whole[KIT_LABEL_LEN + 8];
        snprintf(whole, sizeof(whole), "< %s >", Keymap_CardTitle(card));
        KitItem *it = Control(p, row, WID_SET_CARD, KIT_STEPPER, ControlRect(row, SET_CONTROL_X, (float)Kit_TextWidth(whole, 16) + 32.0f),
                              Keymap_CardTitle(card));
        if (it) {
            it->value = card;
            it->min = 0;
            it->max = cards - 1;
            it->flags |= KIT_WRAP;
        }
    }
    p->cardLines = Keymap_CardLines(card, v->admin, p->lines, KEYMAP_CARD_LINES);
    p->cardNote = Keymap_CardNote(card);
}

/* Left-aligned with the title, or further left when the tabs need the room. */
static void AddTabs(SettingsPage *p, const Settings *s) {
    KitItem probe;
    memset(&probe, 0, sizeof(probe));
    probe.kind = KIT_TABS;
    probe.textSize = 16;
    probe.cells = kTabs;
    probe.cellCount = ST_COUNT;
    KitRect b = Kit_Bounds(&probe);
    float x = KIT_SCREEN_W - 16 - b.w;
    if (x > 80) x = 80;
    KitItem *it = Kit_Add(&p->list, WID_SET_TABS, KIT_TABS, (KitRect){x, SET_TABS_Y, 0, SET_TABS_H}, "Tabs"); /* drawn from its cells */
    if (!it) return;
    it->cells = kTabs;
    it->cellCount = ST_COUNT;
    it->value = s->tab;
    it->flags |= KIT_WRAP; /* RB past the last tab comes round to the first */
}

void Settings_Layout(const Settings *s, const SettingsView *v, SettingsPage *page) {
    memset(page, 0, sizeof(*page));
    Kit_Clear(&page->list, 0); /* settings.c moves the focus itself: rows, then along a row */
    switch (s->tab) {
        case ST_PLAYER: LayoutPlayer(page, s, v); break;
        case ST_SOUND: LayoutSound(page, v); break;
        case ST_SCREEN: LayoutScreen(page, v); break;
        case ST_ONLINE: LayoutOnline(page, v); break;
        case ST_MACHINES: LayoutMachines(page, s, v); break;
        default: LayoutControls(page, s, v); break;
    }
    AddTabs(page, s);
    Screens_AddBack(&page->list);
    Screens_AddClose(&page->list);
}

const SettingsRow *Settings_RowOf(const SettingsPage *page, int id) {
    if (id == KIT_NONE) return NULL;
    for (int i = 0; i < page->rowCount; i++) {
        const SettingsRow *r = &page->rows[i];
        if (r->field == id) return r;
        for (int c = 0; c < r->controlCount; c++) if (r->controls[c] == id) return r;
    }
    return NULL;
}

/* ---------------------------------------------------------- navigation */

static bool Usable(const SettingsPage *p, int id) { return Kit_CanFocus(Kit_Get(&p->list, id)); }

static int ColumnOf(const SettingsRow *r, int id) {
    for (int c = 0; c < r->controlCount; c++) if (r->controls[c] == id) return c;
    return 0;
}

/* The control at col, or the nearest usable one beside it. */
static int PickInRow(const SettingsPage *p, const SettingsRow *r, int col) {
    if (r->controlCount == 0) return KIT_NONE;
    if (col >= r->controlCount) col = r->controlCount - 1;
    for (int d = 0; d < r->controlCount; d++) {
        if (col - d >= 0 && Usable(p, r->controls[col - d])) return r->controls[col - d];
        if (col + d < r->controlCount && Usable(p, r->controls[col + d])) return r->controls[col + d];
    }
    return KIT_NONE;
}

/* The game a names-list control belongs to, or -1. */
static int GameOf(int id) {
    if (id >= WID_SET_GAME_FIELD && id < WID_SET_GAME_FIELD + MANIFEST_MAX_GAMES) return id - WID_SET_GAME_FIELD;
    if (id >= WID_SET_GAME_EDIT && id < WID_SET_GAME_EDIT + MANIFEST_MAX_GAMES) return id - WID_SET_GAME_EDIT;
    if (id >= WID_SET_GAME_CLEAR && id < WID_SET_GAME_CLEAR + MANIFEST_MAX_GAMES) return id - WID_SET_GAME_CLEAR;
    return -1;
}

static int IndexOfRow(const SettingsPage *p, const SettingsRow *r) { return r ? (int)(r - p->rows) : -1; }

/* The next row that way with something to focus, from row index i. */
static int NextRow(const SettingsPage *p, int i, int dir, int col) {
    for (int j = i + dir; j >= 0 && j < p->rowCount; j += dir) {
        int id = PickInRow(p, &p->rows[j], col);
        if (id != KIT_NONE) return id;
    }
    return KIT_NONE;
}

static void SetFocus(KitState *k, int id) {
    if (id == KIT_NONE || id == k->focus) return;
    k->focus = id;
    k->keyDown = KIT_NONE; /* a press does not follow the focus */
}

/* The page's scrolling list keeps its place per tab. */
static KitScroll *TableScroll(Settings *s) { return s->tab == ST_MACHINES ? &s->machines : &s->names; }

static bool Vertical(Settings *s, KitState *k, const SettingsPage *page, const SettingsView *v, int dir) {
    if (k->focus == WID_SET_TABS) {
        if (dir < 0) return false;
        int id = NextRow(page, -1, 1, 0);
        SetFocus(k, id);
        return id != KIT_NONE;
    }
    const SettingsRow *r = Settings_RowOf(page, k->focus);
    if (!r) return false;
    int col = ColumnOf(r, k->focus), pos = r->tablePos;
    /* The next row of the list is out of sight: scroll it in, then step to it. */
    const SettingsTable *t = &page->table; /* what is shown, which the layout kept inside the list */
    if (pos >= 0 && pos + dir >= 0 && pos + dir < t->count && (pos + dir < t->top || pos + dir >= t->top + t->visible)) {
        TableScroll(s)->top = t->top + dir;
        static SettingsPage moved; /* big: off the stack */
        Settings_Layout(s, v, &moved);
        int id = NextRow(&moved, IndexOfRow(&moved, Settings_RowOf(&moved, k->focus)), dir, col);
        SetFocus(k, id);
        return id != KIT_NONE;
    }
    int id = NextRow(page, IndexOfRow(page, r), dir, col);
    if (id == KIT_NONE && dir < 0) id = WID_SET_TABS; /* above the first row: the tabs */
    SetFocus(k, id);
    return id != KIT_NONE;
}

static bool IsValue(const KitItem *it) {
    return it && (it->kind == KIT_TOGGLE || it->kind == KIT_STEPPER || it->kind == KIT_SLIDER || it->kind == KIT_TABS);
}

static bool Horizontal(KitState *k, const SettingsPage *page, int dir) {
    if (IsValue(Kit_Get(&page->list, k->focus))) return false; /* the kit changed its value */
    const SettingsRow *r = Settings_RowOf(page, k->focus);
    if (!r) return false;
    for (int c = ColumnOf(r, k->focus) + dir; c >= 0 && c < r->controlCount; c += dir) {
        if (!Usable(page, r->controls[c])) continue;
        SetFocus(k, r->controls[c]);
        return true;
    }
    return false;
}

const char *Settings_SideKeys(const SettingsPage *page, int focus) {
    const KitItem *it = Kit_Get(&page->list, focus);
    if (IsValue(it) && Kit_CanFocus(it)) return "change";
    const SettingsRow *r = Settings_RowOf(page, focus);
    int usable = 0;
    for (int c = 0; r && c < r->controlCount; c++) usable += Usable(page, r->controls[c]);
    return usable > 1 ? "move" : NULL;
}

bool Settings_Navigate(Settings *s, KitState *k, const SettingsPage *page, const SettingsView *v, const Input *in) {
    if (Keymap_Pressed(KA_UI_UP, in)) return Vertical(s, k, page, v, -1);
    if (Keymap_Pressed(KA_UI_DOWN, in)) return Vertical(s, k, page, v, 1);
    if (Keymap_Pressed(KA_UI_LEFT, in)) return Horizontal(k, page, -1);
    if (Keymap_Pressed(KA_UI_RIGHT, in)) return Horizontal(k, page, 1);
    return false;
}

static int MaxTop(int count, int visible) { return count > visible ? count - visible : 0; }

void Settings_Wheel(Settings *s, KitState *k, const SettingsPage *page, const SettingsView *v, const Input *in) {
    const SettingsTable *t = &page->table;
    KitRect area = {SET_ROW_X, t->y, SET_ROW_W + 24, (float)t->visible * t->pitch};
    if (!t->shown || in->wheel == 0.0f || !in->mouseInside || !Kit_Inside(area, in->mouseX, in->mouseY)) return;
    KitScroll *sc = TableScroll(s);
    sc->wheel += in->wheel;
    int notches = (int)sc->wheel;
    sc->wheel -= (float)notches;
    int top = t->top - notches; /* away from you = up the list */
    int maxTop = MaxTop(t->count, t->visible);
    top = top < 0 ? 0 : (top > maxTop ? maxTop : top);
    if ((top == 0 && sc->wheel > 0.0f) || (top == maxTop && sc->wheel < 0.0f)) sc->wheel = 0.0f;
    if (top == sc->top) return;
    sc->top = top;
    const SettingsRow *r = Settings_RowOf(page, k->focus);
    int pos = r ? r->tablePos : -1;
    if (pos < 0) return;
    /* The focused row scrolled away: the nearest one still shown takes it. */
    int keep = pos < top ? top : (pos >= top + t->visible ? top + t->visible - 1 : pos);
    if (keep == pos) return;
    int col = ColumnOf(r, k->focus);
    static SettingsPage moved;
    Settings_Layout(s, v, &moved);
    for (int i = 0; i < moved.rowCount; i++)
        if (moved.rows[i].tablePos == keep) SetFocus(k, PickInRow(&moved, &moved.rows[i], col));
}

void Settings_OpenTab(Settings *s, KitState *k, const SettingsView *v, int tab) {
    s->tab = tab >= 0 && tab < ST_COUNT ? tab : 0;
    k->keyDown = KIT_NONE;
    if (k->focus == WID_SET_TABS) return; /* stepping along the tabs: stay on them */
    static SettingsPage page;
    Settings_Layout(s, v, &page);
    k->focus = Kit_FirstFocus(&page.list);
}

static void Show(KitScroll *sc, int pos, int count, int visible) {
    if (pos < 0 || pos >= count) return;
    if (pos < sc->top) sc->top = pos;
    if (pos >= sc->top + visible) sc->top = pos - visible + 1;
    if (sc->top > MaxTop(count, visible)) sc->top = MaxTop(count, visible);
    if (sc->top < 0) sc->top = 0;
}

void Settings_ShowName(Settings *s, int index, int count) { Show(&s->names, index, count, SET_NAMES_VISIBLE); }

void Settings_ShowMachine(Settings *s, int pos, int count) { Show(&s->machines, pos, count, SET_MACH_VISIBLE); }

/* -------------------------------------------------------------- events */

SettingsAction Settings_Event(Settings *s, const KitEvent *ev) {
    SettingsAction none = {SA_NONE, 0, 0};
    if (ev->type == KIT_EV_CHANGE) {
        switch (ev->id) {
            case WID_SET_TABS: return (SettingsAction){SA_TAB, 0, ev->value};
            case WID_SET_MUSIC: return (SettingsAction){SA_SHARED, SP_MUSIC, ev->value * 10};
            case WID_SET_FULLSCREEN: return (SettingsAction){SA_SHARED, SP_FULLSCREEN, ev->value};
            case WID_SET_FLASHING: return (SettingsAction){SA_SHARED, SP_FLASHING, ev->value};
            case WID_SET_SHAKE: return (SettingsAction){SA_SHARED, SP_SHAKE, ev->value};
            case WID_SET_HALL_SOUNDS: return (SettingsAction){SA_HALL_SOUNDS, 0, ev->value};
            case WID_SET_HALL_MUSIC: return (SettingsAction){SA_HALL_MUSIC, 0, ev->value};
            case WID_SET_START_ON: return (SettingsAction){SA_START_ON, 0, ev->value == 0}; /* the first choice is Last played */
            case WID_SET_ROW_ORDER: return (SettingsAction){SA_ROW_ORDER, 0, ev->value};
            case WID_SET_BUTTON_HINTS: return (SettingsAction){SA_BUTTON_HINTS, 0, ev->value};
            case WID_SET_ATTRACT: return (SettingsAction){SA_ATTRACT, 0, ev->value};
            case WID_SET_SHARING: return (SettingsAction){SA_SHARING, 0, ev->value};
            case WID_SET_CHECKS: return (SettingsAction){SA_CHECKS, 0, ev->value};
            case WID_SET_CARD: s->card = ev->value; return none;
            default: return none;
        }
    }
    if (ev->type != KIT_EV_PRESS) return none;
    switch (ev->id) {
        case WID_SET_MACH_ADD: return (SettingsAction){SA_MACH_ADD, 0, 0};
        case WID_SET_COIN_TEST: return (SettingsAction){SA_PLAY_COIN, 0, 0};
        case WID_SET_CHECK_NOW: return (SettingsAction){SA_CHECK_NOW, 0, 0};
        case WID_SET_UPDATE_DETAILS: return (SettingsAction){SA_UPDATE_DETAILS, 0, 0};
        case WID_SET_SYNC_NOW: return (SettingsAction){SA_SYNC_NOW, 0, 0};
        case WID_SET_NAME_EDIT: return (SettingsAction){SA_EDIT_NAME, SET_ARCADE_NAME, 0};
        case WID_SET_NAME_FIELD: return (SettingsAction){SA_EDIT_NAME, SET_ARCADE_NAME, 1};
        default: break;
    }
    static const struct { int base; SettingsActionType type; } kMachine[] = {
        {WID_SET_MACH_FAV, SA_MACH_FAVOURITE}, {WID_SET_MACH_UP, SA_MACH_UP},         {WID_SET_MACH_DOWN, SA_MACH_DOWN},
        {WID_SET_MACH_HIDE, SA_MACH_HIDE},     {WID_SET_MACH_EDIT, SA_MACH_EDIT},     {WID_SET_MACH_REMOVE, SA_MACH_REMOVE},
    };
    for (size_t b = 0; b < sizeof(kMachine) / sizeof(kMachine[0]); b++)
        if (ev->id >= kMachine[b].base && ev->id < kMachine[b].base + MANIFEST_MAX_GAMES)
            return (SettingsAction){kMachine[b].type, ev->id - kMachine[b].base, 0};
    int g = GameOf(ev->id);
    if (g < 0) return none;
    if (ev->id == WID_SET_GAME_CLEAR + g) return (SettingsAction){SA_CLEAR_NAME, g, 0};
    return (SettingsAction){SA_EDIT_NAME, g, ev->id == WID_SET_GAME_FIELD + g};
}

/* -------------------------------------------------------------- typing */

const char *Settings_NameKey(const Settings *s, const Manifest *m) {
    if (s->fieldFor == SET_ARCADE_NAME || !m || s->fieldFor >= m->count) return PROFILE_DEFAULT_KEY;
    return m->games[s->fieldFor].name;
}

void Settings_BeginEdit(Settings *s, const Profiles *p, const Manifest *m, int index, bool quick, bool fromPad) {
    if (index != SET_ARCADE_NAME && (!m || index < 0 || index >= m->count)) index = SET_ARCADE_NAME;
    s->fieldFor = index;
    s->tab = ST_PLAYER;
    if (index != SET_ARCADE_NAME) Settings_ShowName(s, index, m->count);
    TextField_Begin(&s->field, Profiles_GetOwnUsername(p, Settings_NameKey(s, m)), SET_NAME_LIMIT);
    TextField_KeepCursorVisible(&s->field, Settings_FieldChars(true));
    s->quick = quick;
    s->board = fromPad;
    NameBoard_Home(&s->boardPos);
    int field, save, cancel;
    NameIds(index, &field, &save, &cancel);
    Kit_Enter(&s->editKit, save); /* Enter saves: shown on Save */
}

void Settings_ClickIntoName(Settings *s, float x) {
    s->field.view = 0; /* at rest the name is drawn from its start */
    TextField_ClickAt(&s->field, x, SET_FIELD_X + SET_FIELD_PAD, SET_CHAR_W);
    TextField_KeepCursorVisible(&s->field, Settings_FieldChars(true));
}

void Settings_EndEdit(Settings *s, bool keep) {
    if (!keep) snprintf(s->field.text, sizeof(s->field.text), "%s", s->field.before);
    TextField_End(&s->field);
    s->quick = false;
    s->board = false;
}

static void CopyItem(const SettingsPage *page, KitList *out, int id) {
    const KitItem *it = Kit_Get(&page->list, id);
    if (it && out->count < KIT_MAX_ITEMS) out->items[out->count++] = *it;
}

KitRect Settings_BoardPanel(const SettingsPage *page, const Settings *s) {
    int field, save, cancel;
    NameIds(s->fieldFor, &field, &save, &cancel);
    const KitItem *box = Kit_Get(&page->list, field);
    KitRect r = box ? box->r : (KitRect){SET_FIELD_X, SET_ROW_Y, SET_FIELD_W, SET_FIELD_H};
    return NameBoard_Panel(r);
}

KitLook Settings_TypingLook(const Settings *s, KitLook look, InputDevice device) {
    bool padOnBoard = s->field.editing && s->board && device == DEV_PAD;
    return look == KIT_LOOK_FOCUSED && padOnBoard ? KIT_LOOK_NORMAL : look;
}

/* The panel takes clicks between its cells, so a near miss is not a click
 * away (which would end the typing). The cells are mouse-only: the pad has
 * its own cursor on the board. */
static void AddBoard(const SettingsPage *page, const Settings *s, KitList *out) {
    KitRect panel = Settings_BoardPanel(page, s);
    Kit_Add(out, WID_SET_BOARD_PANEL, KIT_AREA, panel, "");
    for (int i = 0; i < NameBoard_Count(); i++) {
        KitItem *it = Kit_Add(out, WID_SET_BOARD + i, KIT_BUTTON, NameBoard_CellRect(panel, i), NameBoard_Cell(i)->label);
        if (it) it->flags |= KIT_NOFOCUS | KIT_SELFDRAWN;
    }
}

void Settings_EditLayer(const SettingsPage *page, const Settings *s, KitList *out) {
    int field, save, cancel;
    NameIds(s->fieldFor, &field, &save, &cancel);
    Kit_Clear(out, 0);
    Kit_Add(out, WID_SET_EDIT_VEIL, KIT_AREA, (KitRect){0, 0, KIT_SCREEN_W, KIT_SCREEN_H}, "");
    CopyItem(page, out, field);
    CopyItem(page, out, save);
    CopyItem(page, out, cancel);
    CopyItem(page, out, WID_BACK);
    if (s->board) AddBoard(page, s, out);
    CopyItem(page, out, WID_CLOSE);
}

/* A press on the text puts the cursor there at once, as a text box does. */
static void ClickIntoText(Settings *s, const KitList *layer, int field, const Input *in) {
    const KitItem *box = Kit_Get(layer, field);
    if (!box || !in->mousePressed[MB_LEFT] || !in->mouseInside || !Kit_Inside(box->r, in->mouseX, in->mouseY)) return;
    TextField_ClickAt(&s->field, in->mouseX, box->r.x + SET_FIELD_PAD, SET_CHAR_W);
}

/* A cell, pressed with pad A or clicked: it types, deletes or finishes. */
static SettingsAction BoardPress(Settings *s, int cell) {
    SettingsAction none = {SA_NONE, s->fieldFor, 0};
    const NameBoardCell *c = NameBoard_Cell(cell);
    if (!c) return none;
    char typed[2] = {c->ch, '\0'};
    switch (c->kind) {
        case NBK_CHAR:
        case NBK_SPACE: TextField_Insert(&s->field, typed); break;
        case NBK_DELETE: TextField_Backspace(&s->field); break;
        case NBK_DONE: return (SettingsAction){SA_SAVE_NAME, s->fieldFor, 0};
    }
    return none;
}

static bool AnyPad(const Input *in) {
    for (int b = 0; b < PB_COUNT; b++) if (in->pad[b]) return true;
    return false;
}

static bool BoardDir(const Input *in, KitDir *dir) {
    if (Keymap_Pressed(KA_BOARD_LEFT, in)) *dir = KIT_LEFT;
    else if (Keymap_Pressed(KA_BOARD_RIGHT, in)) *dir = KIT_RIGHT;
    else if (Keymap_Pressed(KA_BOARD_UP, in)) *dir = KIT_UP;
    else if (Keymap_Pressed(KA_BOARD_DOWN, in)) *dir = KIT_DOWN;
    else return false;
    return true;
}

/* The pad on the board. Its first press while the board is down only
 * brings it up: nobody types a letter they could not see. B is left to the
 * field, where it puts the old name back. */
static SettingsAction BoardPad(Settings *s, const Input *in) {
    SettingsAction none = {SA_NONE, s->fieldFor, 0};
    if (!s->board) {
        if (AnyPad(in) && !Keymap_Pressed(KA_TEXT_CANCEL, in)) {
            s->board = true;
            NameBoard_Home(&s->boardPos);
        }
        return none;
    }
    if (Keymap_Pressed(KA_BOARD_DONE, in)) return (SettingsAction){SA_SAVE_NAME, s->fieldFor, 0};
    KitDir dir;
    if (BoardDir(in, &dir)) NameBoard_Move(&s->boardPos, dir);
    if (Keymap_Pressed(KA_BOARD_TYPE, in)) return BoardPress(s, s->boardPos.cell);
    if (Keymap_Pressed(KA_BOARD_DELETE, in)) TextField_Backspace(&s->field);
    if (Keymap_Pressed(KA_BOARD_SPACE, in)) TextField_Insert(&s->field, " ");
    return none;
}

SettingsAction Settings_Type(Settings *s, const SettingsPage *page, const Input *in, const char *clipboard) {
    SettingsAction none = {SA_NONE, s->fieldFor, 0};
    int field, save, cancel;
    NameIds(s->fieldFor, &field, &save, &cancel);
    KitList layer;
    Settings_EditLayer(page, s, &layer);
    Input mouse;
    Input_MouseOnly(in, &mouse);
    KitEvent ev = Kit_Update(&s->editKit, &layer, &mouse);
    if (ev.type == KIT_EV_PRESS) {
        if (ev.id == WID_CLOSE) return (SettingsAction){SA_QUIT, s->fieldFor, 0};
        if (ev.id == save) return (SettingsAction){SA_SAVE_NAME, s->fieldFor, 0};
        if (ev.id == cancel || ev.id == WID_BACK) return (SettingsAction){SA_CANCEL_NAME, s->fieldFor, 0};
        if (ev.id == WID_SET_EDIT_VEIL) return (SettingsAction){SA_SAVE_NAME, s->fieldFor, 1}; /* clicked away: keep it */
        int cell = ev.id - WID_SET_BOARD;
        if (cell >= 0 && cell < NameBoard_Count()) {
            NameBoard_Set(&s->boardPos, cell);
            SettingsAction act = BoardPress(s, cell);
            TextField_KeepCursorVisible(&s->field, Settings_FieldChars(true));
            return act;
        }
    }
    ClickIntoText(s, &layer, field, in);
    SettingsAction act = BoardPad(s, in);
    TextField_KeepCursorVisible(&s->field, Settings_FieldChars(true));
    if (act.type != SA_NONE) return act;
    TextFieldResult r = TextField_Update(&s->field, in, clipboard);
    TextField_KeepCursorVisible(&s->field, Settings_FieldChars(true));
    if (r == TF_SUBMIT) return (SettingsAction){SA_SAVE_NAME, s->fieldFor, 0};
    if (r == TF_CANCEL) return (SettingsAction){SA_CANCEL_NAME, s->fieldFor, 0};
    return none;
}

/* -------------------------------------------------------------- saving */

static int Clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static bool SamePrefs(const ArcadePrefs *a, const ArcadePrefs *b) {
    return a->musicVolume == b->musicVolume && a->screenShake == b->screenShake && a->reducedFlashing == b->reducedFlashing &&
           a->fullscreen == b->fullscreen;
}

bool Settings_SaveShared(SharedPref which, int value) {
    Prefs_Load(); /* a game may have changed the other three since */
    ArcadePrefs *p = Prefs_Get();
    switch (which) {
        case SP_MUSIC: p->musicVolume = Clamp(value, 0, 100); break;
        case SP_SHAKE: p->screenShake = value != 0; break;
        case SP_FLASHING: p->reducedFlashing = value != 0; break;
        case SP_FULLSCREEN: p->fullscreen = value != 0; break;
    }
    ArcadePrefs wanted = *p;
    /* The shared Prefs_Save does not report a failed last step (the
     * rename), so read the file back: what is there is what every game
     * will use, and what the page should show. */
    bool opened = Prefs_Save();
    Prefs_Load();
    return opened && SamePrefs(Prefs_Get(), &wanted);
}

int Settings_SharedValue(SharedPref which) {
    const ArcadePrefs *p = Prefs_Get();
    switch (which) {
        case SP_MUSIC: return p->musicVolume;
        case SP_SHAKE: return p->screenShake;
        case SP_FLASHING: return p->reducedFlashing;
        case SP_FULLSCREEN: return p->fullscreen;
    }
    return 0;
}

#define HALL_SOUNDS_KEY "hall_sounds"

int Settings_HallSounds(const LauncherIni *ini) { return LauncherIni_GetInt(ini, HALL_SOUNDS_KEY, 10, 0, 10); }

bool Settings_SaveHallSounds(LauncherIni *ini, int level) { return LauncherIni_StoreInt(ini, HALL_SOUNDS_KEY, Clamp(level, 0, 10)); }

#define BUTTON_HINTS_KEY "button_hints"
static const char *const kHintWords[HINTS_COUNT] = {[HINTS_AUTO] = "auto", [HINTS_KEYS] = "keys", [HINTS_PAD] = "pad"};

/* Anything else in the file (a typo, a hand edit) is Auto. */
int Settings_ButtonHints(const LauncherIni *ini) {
    const char *v = LauncherIni_Get(ini, BUTTON_HINTS_KEY);
    for (int i = 0; v && i < HINTS_COUNT; i++) if (strcmp(v, kHintWords[i]) == 0) return i;
    return HINTS_AUTO;
}

bool Settings_SaveButtonHints(LauncherIni *ini, int hints) {
    return LauncherIni_Store(ini, BUTTON_HINTS_KEY, kHintWords[Clamp(hints, 0, HINTS_COUNT - 1)]);
}

InputDevice Settings_HintDevice(int hints, InputDevice used) {
    if (hints == HINTS_KEYS) return DEV_KEYBOARD;
    if (hints == HINTS_PAD) return DEV_PAD;
    return used == DEV_PAD ? DEV_PAD : DEV_KEYBOARD;
}

bool Settings_SaveOnline(const char *key, bool on) { return SyncConfig_SetValue(key, on ? "1" : "0"); }

bool Settings_SaveName(Profiles *p, const char *key, const char *name) {
    Profiles_SetUsername(p, key, name);
    bool ok = Profiles_Save(p);
    if (!ok) Profiles_Load(p);
    return ok;
}

/* --------------------------------------------------------------- words */

long long Settings_LastSync(void) {
    char dir[512], global[600];
    if (!Sync_DataDir(dir, sizeof(dir))) return 0;
    snprintf(global, sizeof(global), "%s/scores/global", dir);
    struct stat st;
    return stat(global, &st) == 0 ? (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec : 0;
}

void Settings_Ago(long long then, long long now, char *out, size_t size) {
    if (then <= 0) { snprintf(out, size, "never"); return; }
    long long ago = now - then;
    if (ago < 0) ago = 0;
    if (ago < 120) snprintf(out, size, "just now");
    else if (ago < 7200) snprintf(out, size, "%lld minutes ago", ago / 60);
    else if (ago < 172800) snprintf(out, size, "%lld hours ago", ago / 3600);
    else snprintf(out, size, "%lld days ago", ago / 86400);
}

void Settings_ServerHost(const char *url, char *out, size_t size) {
    const char *p = url ? url : "";
    const char *scheme = strstr(p, "://");
    if (scheme) p = scheme + 3;
    size_t n = strcspn(p, "/?#");
    if (n > 48) n = 48;
    snprintf(out, size, "%.*s", (int)n, p);
}
