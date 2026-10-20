#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include "kit.h"
#include "keymap.h"
#include "textfield.h"
#include "manifest.h"
#include "profile.h"
#include "launcherini.h"
#include "nameboard.h"
#include "lineup.h"

/* The Settings screen, without raylib: its tabs and rows, where each control
 * goes (a KitList, as every screen has), how the arrows move between rows,
 * typing a name, and saving each kind of setting where it belongs:
 *   - shared with every game (marked *): ~/.config/ghost-arcade/prefs.ini,
 *     read again right before each save, since any game may have changed it;
 *   - the launcher's own: launcher.ini (launcherini.h);
 *   - online sharing and update checks: online.conf, one line at a time;
 *   - names: profiles.txt.
 * render.c draws the page; main.c does what an action asks (play a sound,
 * start a check) and says "Saved". */

/* The tabs across the top (a Data tab would go before ST_COUNT). */
typedef enum { ST_PLAYER, ST_SOUND, ST_SCREEN, ST_ONLINE, ST_CONTROLS, ST_MACHINES, ST_COUNT } SettingsTab;

const char *Settings_TabLabel(int tab);

#define SET_NAME_LIMIT 32   /* new names stop here: what the online boards keep */
#define SET_ARCADE_NAME (-1) /* a name index meaning the arcade-wide name */

/* Geometry, logical 960x640. */
#define SET_TABS_Y 112
#define SET_TABS_H 32
#define SET_INTRO_Y 160
#define SET_ROW_X 80
#define SET_ROW_W 800
#define SET_ROW_Y 184
#define SET_ROW_PITCH 44
#define SET_ROW_H 36
#define SET_LABEL_X 96
#define SET_CONTROL_X 440
#define SET_FIELD_X 304
#define SET_FIELD_W 336
#define SET_FIELD_H 32
#define SET_FIELD_PAD 12
#define SET_CHAR_W 18 /* one 16 px character and its gap */
#define SET_NAMES_HEAD_Y 280 /* under the arcade name and Start on */
#define SET_NAMES_Y 300
#define SET_NAMES_PITCH 40
#define SET_NAMES_VISIBLE 6
#define SET_MACH_HEAD_Y 236  /* Machines: under Row order */
#define SET_MACH_Y 256
#define SET_MACH_VISIBLE 7
#define SET_CARD_Y 236
#define SET_CARD_PITCH 18
#define SET_CARD_WHAT_X 96  /* the reference card's columns */
#define SET_CARD_KEYS_X 304
#define SET_CARD_PAD_X 528
#define SET_CARD_MOUSE_X 760
#define SET_CARD_END_X 880
#define SET_HELP_Y 560
#define SET_FOOT_Y 580

/* Characters of a name a field shows: all of its width at rest, less the
 * counter while typing. */
int Settings_FieldChars(bool typing);

/* What stays between frames. */
typedef struct {
    int tab;
    KitScroll names;  /* Player: the names for single games */
    KitScroll machines; /* Machines: the machines in your order */
    int card;         /* Controls: which reference card */
    TextField field;  /* the name being typed, if field.editing */
    int fieldFor;     /* SET_ARCADE_NAME or a catalog index */
    bool quick;       /* typing began with U or Shift+U: Enter and Esc also leave */
    bool board;       /* the pad's name board is up under the name (nameboard.h) */
    NameBoardPos boardPos;
    KitState editKit; /* the buttons that still answer the mouse while typing */
} Settings;

void Settings_Init(Settings *s);

/* What the hint lines, the strip and the menu name: what was used last
 * (the keyboard, or the pad after pad use), or always one of them. */
typedef enum { HINTS_AUTO, HINTS_KEYS, HINTS_PAD, HINTS_COUNT } ButtonHints;

/* What the page shows. */
typedef struct {
    int musicVolume; /* 0-100 */
    bool fullscreen, reducedFlashing, screenShake;
    int hallSounds;  /* 0-10 */
    int buttonHints; /* ButtonHints */
    int attract;     /* AttractSetting */
    bool soundReady; /* there is a sound device to test the coin on */
    bool hallMusic;  /* launcher.ini hall_music */
    const char *hallSong; /* what the hall plays: "Built-in theme" or the song's title */
    bool startOnLast;
    int rowOrder;          /* RowOrder */
    const RowPrefs *row;   /* each machine's star, hiding and place (NULL = none) */
    const Lineup *mine;    /* your order, favourites first, hidden ones too */
    const bool *installed; /* per catalog index: its program is there (NULL = none are) */
    int installing;        /* the catalog index being installed, or -1 */
    bool sharing, checkUpdates;
    bool checking, checkNoAnswer;
    int updateStatus;       /* UpdateStatus (updatecheck.h) */
    long long checkedAt;    /* 0 = never */
    bool syncing, syncNoAnswer;
    long long lastSync;     /* seconds since 1970, 0 = never */
    long long now;
    const char *serverUrl;
    const Manifest *manifest;
    const Profiles *profiles;
    bool admin;
} SettingsView;

typedef enum { SROW_CONTROL, SROW_HEADING, SROW_INFO } SettingsRowKind;
typedef enum { NOTE_DIM, NOTE_TEXT, NOTE_GOOD, NOTE_HOT, NOTE_BAD } NoteTone;

#define SET_NOTE_LEN 56
#define SET_MAX_ROWS 16
#define SET_ROW_CONTROLS 6 /* a machine, for admin: Favourite, Up, Down, Hide, Edit, Remove */

typedef struct {
    SettingsRowKind kind;
    KitRect r;               /* the whole row, label to controls */
    char label[40];          /* "" when a button stands where the label would */
    bool shared;             /* stored in prefs.ini, the same in every game */
    const char *help;        /* the sentence under the rows when this one is chosen */
    char note[2][SET_NOTE_LEN];
    NoteTone tone[2];
    int noteX;
    int controls[SET_ROW_CONTROLS]; /* its focusable controls, left to right */
    int controlCount;
    int field;               /* its text field's id, or 0 */
    int nameIndex;           /* the field's name: SET_ARCADE_NAME or a catalog index */
    char value[TEXTFIELD_CAP]; /* the field's text at rest */
    bool placeholder;        /* value is a hint, not a name */
    int tablePos;            /* its place in the page's scrolling list, or -1 */
    bool star, dim;          /* Machines: a favourite; a hidden machine */
    char sub[32];            /* Machines: a line under the label (its status) */
    NoteTone subTone;
    int labelPad;            /* room before the label (Machines keeps a column for the star) */
} SettingsRow;

/* The one list on a page that scrolls (Player's names, the machines). */
typedef struct {
    bool shown;
    int top, count, visible;
    float y, pitch;
} SettingsTable;

typedef struct {
    KitList list;
    SettingsRow rows[SET_MAX_ROWS];
    int rowCount;
    const char *intro;
    const char *footnote;
    SettingsTable table;       /* the names, or the machines: for the arrows, the wheel and the scroll bar */
    int cardLines;
    KeymapLine lines[KEYMAP_CARD_LINES];
    const char *cardNote;
} SettingsPage;

/* Lays the open tab out. The tab bar comes after the rows, so a page that
 * opens fresh starts on its first row. */
void Settings_Layout(const Settings *s, const SettingsView *v, SettingsPage *page);
/* The row holding a control (by id), or NULL. */
const SettingsRow *Settings_RowOf(const SettingsPage *page, int id);

/* --- keys and pad: which control the arrows reach --- */
/* Up/Down go from row to row (the tab bar is above the first), scrolling
 * the names list; Left/Right go along the row, except on a value control,
 * whose Left/Right the kit turns into a change. Disabled controls are
 * skipped. Run after Kit_Update. True if the focus moved. */
bool Settings_Navigate(Settings *s, KitState *k, const SettingsPage *page, const SettingsView *v, const Input *in);
/* What Left/Right do where the focus is, for the hint line: "change" a
 * value, "move" along a row of buttons, or NULL when they do nothing. */
const char *Settings_SideKeys(const SettingsPage *page, int focus);
/* The wheel over the names list scrolls it; a focus that scrolls out of
 * sight moves to the nearest row still shown. */
void Settings_Wheel(Settings *s, KitState *k, const SettingsPage *page, const SettingsView *v, const Input *in);
/* Opens a tab with the first control of its first row focused (or the tab
 * bar, if that is where the focus was). */
void Settings_OpenTab(Settings *s, KitState *k, const SettingsView *v, int tab);
/* The names list scrolls so the name `index` shows. */
void Settings_ShowName(Settings *s, int index, int count);
/* The machines list scrolls so the machine at `pos` in your order shows. */
void Settings_ShowMachine(Settings *s, int pos, int count);

/* --- what a press or change means --- */
typedef enum {
    SA_NONE,
    SA_TAB,            /* the tab changed: value */
    SA_SHARED,         /* which: SharedPref, value */
    SA_HALL_SOUNDS,    /* value 0-10 */
    SA_HALL_MUSIC,     /* value 0/1 */
    SA_START_ON,       /* value 1 = the machine played last, 0 = the first */
    SA_ROW_ORDER,      /* value: RowOrder */
    SA_MACH_FAVOURITE, /* which: catalog index (Favourite or Unfavourite) */
    SA_MACH_UP,        /* which: catalog index */
    SA_MACH_DOWN,
    SA_MACH_HIDE,      /* which: catalog index (Hide or Show) */
    SA_MACH_ADD,       /* admin: the form, empty */
    SA_MACH_EDIT,      /* admin, which: catalog index */
    SA_MACH_REMOVE,    /* admin, which: catalog index (asks first) */
    SA_BUTTON_HINTS,   /* value: ButtonHints */
    SA_ATTRACT,        /* value: AttractSetting */
    SA_PLAY_COIN,
    SA_SHARING,        /* value 0/1 */
    SA_CHECKS,         /* value 0/1 */
    SA_CHECK_NOW,
    SA_UPDATE_DETAILS,
    SA_SYNC_NOW,
    SA_EDIT_NAME,      /* which: name index; value 1 = by a click on the field */
    SA_CLEAR_NAME,     /* which: catalog index */
    SA_SAVE_NAME,      /* the typed name is to be kept; value 1 = only if it changed */
    SA_CANCEL_NAME,    /* the old name is back */
    SA_QUIT,           /* the X, while typing */
} SettingsActionType;

typedef enum { SP_MUSIC, SP_SHAKE, SP_FLASHING, SP_FULLSCREEN } SharedPref;

typedef struct {
    SettingsActionType type;
    int which;
    int value;
} SettingsAction;

/* A kit event on the page. Tab and card changes are done here. */
SettingsAction Settings_Event(Settings *s, const KitEvent *ev);

/* --- typing a name --- */
/* Starts typing the name `index`; the names list scrolls to it. From the
 * pad, the name board comes up with it. */
void Settings_BeginEdit(Settings *s, const Profiles *p, const Manifest *m, int index, bool quick, bool fromPad);
/* Typing begun by a click on a name at rest: the cursor goes where the
 * click landed on the name as it was drawn (from its first character), and
 * the field then scrolls to keep it in sight. */
void Settings_ClickIntoName(Settings *s, float x);
/* The name the field edits, as profiles.txt keys it. */
const char *Settings_NameKey(const Settings *s, const Manifest *m);
/* While typing, only these answer the mouse: the field, its Save and
 * Cancel, Back (which cancels), the name board's cells when it is up, and
 * the X, over a veil: a click anywhere else keeps what was typed, as
 * leaving a field does. */
void Settings_EditLayer(const SettingsPage *page, const Settings *s, KitList *out);
/* The name board's panel, from the box being typed in. */
KitRect Settings_BoardPanel(const SettingsPage *page, const Settings *s);
/* How a button of the page looks while a name is typed: with the pad on the
 * name board its cursor there is the one focus, so Save (Enter's button)
 * shows none of its own. */
KitLook Settings_TypingLook(const Settings *s, KitLook look, InputDevice device);
/* One frame of typing. The keyboard types into the field; the pad drives
 * the name board, which its first press brings up when typing began some
 * other way. clipboard: see TextField_Update. */
SettingsAction Settings_Type(Settings *s, const SettingsPage *page, const Input *in, const char *clipboard);
/* The field ends; Cancel puts the old text back first. */
void Settings_EndEdit(Settings *s, bool keep);

/* --- saving --- */
/* prefs.ini is read again first, so a change a game made since stays. */
bool Settings_SaveShared(SharedPref which, int value);
int Settings_SharedValue(SharedPref which);
int Settings_HallSounds(const LauncherIni *ini);
bool Settings_SaveHallSounds(LauncherIni *ini, int level);
/* Button hints (launcher.ini `button_hints`: auto, keys or pad), and the
 * device they name when the player last used `used`. */
int Settings_ButtonHints(const LauncherIni *ini);
bool Settings_SaveButtonHints(LauncherIni *ini, int hints);
InputDevice Settings_HintDevice(int hints, InputDevice used);
/* key: "enabled" or "check_updates". */
bool Settings_SaveOnline(const char *key, bool on);
/* A name for a key (PROFILE_DEFAULT_KEY or a game's); a failed save puts
 * back what is on disk, which is what the games will use. */
bool Settings_SaveName(Profiles *p, const char *key, const char *name);

/* --- words --- */
/* When the online boards last came down: the newest change in
 * scores/global, which only ghost-sync writes, in nanoseconds since 1970
 * (fine enough to see a sync that ends within the second). 0 = never. */
long long Settings_LastSync(void);
/* "just now", "5 minutes ago", "2 hours ago", "3 days ago", "never". */
void Settings_Ago(long long then, long long now, char *out, size_t size);
/* The server's host, for showing: "abc.supabase.co". */
void Settings_ServerHost(const char *url, char *out, size_t size);

#endif
