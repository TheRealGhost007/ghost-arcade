/* Headless tests for the text field (src/textfield.c), the key table and
 * its reference cards (src/keymap.c) and the Settings page (src/settings.c):
 * layout, the arrows, typing a name. Saving to disk is in test_main.c, which
 * owns the temp folders. */
#include <string.h>
#include <stdio.h>
#include "check.h"
#include "../src/textfield.h"
#include "../src/keymap.h"
#include "../src/settings.h"
#include "../src/screens.h"

/* ------------------------------------------------------------- helpers */

static Input Blank(void) {
    Input in;
    memset(&in, 0, sizeof(in));
    in.focused = true;
    in.lastDevice = DEV_KEYBOARD;
    in.mouseX = in.mouseY = -100.0f;
    return in;
}

static Input Press(InputKey k) {
    Input in = Blank();
    in.key[k] = in.keyRepeat[k] = in.keyDown[k] = true;
    return in;
}

static Input PadPress(PadButton b) {
    Input in = Blank();
    in.pad[b] = in.padRepeat[b] = in.padDown[b] = true;
    in.lastDevice = DEV_PAD;
    return in;
}

static Input Chars(const char *s) {
    Input in = Blank();
    for (int i = 0; s[i] && i < INPUT_MAX_CHARS; i++) in.chars[in.charCount++] = (unsigned char)s[i];
    return in;
}

static TextFieldResult Type(TextField *f, const char *s) { Input in = Chars(s); return TextField_Update(f, &in, NULL); }
static TextFieldResult Key(TextField *f, InputKey k) { Input in = Press(k); return TextField_Update(f, &in, NULL); }

static Input Pointer(float x, float y, bool moved) {
    Input in = Blank();
    in.mouseX = x;
    in.mouseY = y;
    in.mouseInside = true;
    in.mouseMoved = moved;
    in.lastDevice = DEV_MOUSE;
    return in;
}

/* --------------------------------------------------------- text field */

static void test_textfield_editing(void) {
    TextField f;
    TextField_Begin(&f, "TRG", 32);
    CHECK(f.editing && f.cursor == 3 && TextField_Length(&f) == 3 && !TextField_Changed(&f), "text: begins with the cursor at the end");
    CHECK(Type(&f, "abc") == TF_CHANGED && strcmp(f.text, "TRGabc") == 0 && f.cursor == 6, "text: letters go in at the cursor");
    Key(&f, IK_LEFT);
    Key(&f, IK_LEFT);
    Type(&f, "X");
    CHECK(strcmp(f.text, "TRGaXbc") == 0 && f.cursor == 5, "text: Left moves the cursor; typing goes in there");
    Key(&f, IK_HOME);
    CHECK(f.cursor == 0 && Key(&f, IK_LEFT) == TF_NONE && f.cursor == 0, "text: Home goes to the start, and Left stops there");
    CHECK(Key(&f, IK_BACKSPACE) == TF_NONE && strcmp(f.text, "TRGaXbc") == 0, "text: Backspace at the start does nothing");
    Key(&f, IK_DELETE);
    CHECK(strcmp(f.text, "RGaXbc") == 0 && f.cursor == 0, "text: Delete takes the character after the cursor");
    Key(&f, IK_END);
    CHECK(f.cursor == 6 && Key(&f, IK_DELETE) == TF_NONE && Key(&f, IK_RIGHT) == TF_NONE, "text: End; Delete and Right stop there");
    Key(&f, IK_LEFT);
    Key(&f, IK_BACKSPACE);
    CHECK(strcmp(f.text, "RGaXc") == 0 && f.cursor == 4, "text: Backspace takes the character before the cursor");

    /* Held Backspace: the repeat steps arrive as keyRepeat without a new press. */
    Input rep = Blank();
    rep.keyRepeat[IK_BACKSPACE] = rep.keyDown[IK_BACKSPACE] = true;
    TextField_Update(&f, &rep, NULL);
    TextField_Update(&f, &rep, NULL);
    CHECK(strcmp(f.text, "RGc") == 0, "text: Backspace repeats while held");
    Key(&f, IK_HOME);
    Input repDel = Blank();
    repDel.keyRepeat[IK_DELETE] = repDel.keyDown[IK_DELETE] = true;
    TextField_Update(&f, &repDel, NULL);
    CHECK(strcmp(f.text, "Gc") == 0, "text: Delete repeats while held");

    TextField_Begin(&f, "", 32);
    Type(&f, "a|b");
    CHECK(strcmp(f.text, "ab") == 0, "text: '|' never goes in (it splits games.txt and profiles.txt)");
    Input odd = Blank();
    int weird[] = {7, 9, 127, 200, 233, 0x263A, 'z'};
    for (int i = 0; i < 7; i++) odd.chars[odd.charCount++] = weird[i];
    TextField_Update(&f, &odd, NULL);
    CHECK(strcmp(f.text, "abz") == 0, "text: only printable ASCII goes in");
    CHECK(TextField_Allowed(' ') && TextField_Allowed('~') && !TextField_Allowed('|') && !TextField_Allowed(31) && !TextField_Allowed(127),
          "text: allowed is space to ~, but not |");

    Input ctrlA = Chars("a");
    ctrlA.ctrl = true;
    TextField_Update(&f, &ctrlA, NULL);
    CHECK(strcmp(f.text, "abz") == 0, "text: a Ctrl chord types nothing");
    Input altGr = Chars("@");
    altGr.alt = true;
    TextField_Update(&f, &altGr, NULL);
    CHECK(strcmp(f.text, "abz@") == 0, "text: AltGr (seen as Alt) still types");
}

static void test_textfield_limits_and_paste(void) {
    TextField f;
    TextField_Begin(&f, "", 5);
    Type(&f, "abcdefg");
    CHECK(strcmp(f.text, "abcde") == 0 && f.cursor == 5, "text: new text stops at the limit");

    char longName[41];
    memset(longName, 'n', 40);
    longName[40] = '\0';
    TextField_Begin(&f, longName, 32);
    CHECK(TextField_Length(&f) == 40, "text: a longer name it was given is kept whole");
    CHECK(Type(&f, "x") == TF_NONE && TextField_Length(&f) == 40, "text: ...nothing more goes in");
    Key(&f, IK_BACKSPACE);
    Type(&f, "x");
    CHECK(TextField_Length(&f) == 39, "text: ...until it is shorter than the limit");

    TextField_Begin(&f, "ab", 10);
    Key(&f, IK_LEFT);
    Input paste = Press(IK_V);
    paste.ctrl = true;
    paste.chars[0] = 'v'; /* if a platform types the v as well, it must not go in */
    paste.charCount = 1;
    CHECK(TextField_WantsPaste(&paste), "text: Ctrl+V asks for the clipboard");
    TextField_Update(&f, &paste, "Hi|there\nyou");
    CHECK(strcmp(f.text, "aHithereyb") == 0 && f.cursor == 9, "text: paste goes in at the cursor, cleaned, up to the limit");
    TextField_Begin(&f, "ab", 10);
    CHECK(TextField_Update(&f, &paste, NULL) == TF_NONE && TextField_Update(&f, &paste, "") == TF_NONE && strcmp(f.text, "ab") == 0,
          "text: an empty clipboard (Wayland) pastes nothing, quietly");
    Input plainV = Press(IK_V);
    plainV.chars[0] = 'v';
    plainV.charCount = 1;
    CHECK(!TextField_WantsPaste(&plainV), "text: V alone is a letter");
}

static void test_textfield_finish(void) {
    TextField f;
    TextField_Begin(&f, "TRG", 32);
    Type(&f, "x");
    CHECK(TextField_Changed(&f), "text: changed after typing");
    Input altEnter = Press(IK_ENTER);
    altEnter.alt = true;
    CHECK(TextField_Update(&f, &altEnter, NULL) != TF_SUBMIT && f.editing, "text: Alt+Enter is fullscreen, not save");
    CHECK(Key(&f, IK_ENTER) == TF_SUBMIT && !f.editing && strcmp(f.text, "TRGx") == 0, "text: Enter keeps it");
    CHECK(Key(&f, IK_X) == TF_NONE && strcmp(f.text, "TRGx") == 0, "text: a field not being edited takes nothing");

    TextField_Begin(&f, "TRG", 32);
    Type(&f, "xyz");
    CHECK(Key(&f, IK_ESCAPE) == TF_CANCEL && !f.editing && strcmp(f.text, "TRG") == 0, "text: Esc puts the old text back");
    TextField_Begin(&f, "TRG", 32);
    Type(&f, "q");
    Input b = PadPress(PB_B);
    CHECK(TextField_Update(&f, &b, NULL) == TF_CANCEL && strcmp(f.text, "TRG") == 0, "text: pad B puts it back too");
    TextField_Begin(&f, "TRG", 32);
    Type(&f, "q");
    Input back = Blank();
    back.mousePressed[MB_BACK] = true;
    CHECK(TextField_Update(&f, &back, NULL) == TF_CANCEL, "text: so does the mouse's back button");
    TextField_Begin(&f, "TRG", 32);
    Input a = PadPress(PB_A);
    CHECK(TextField_Update(&f, &a, NULL) == TF_NONE && f.editing, "text: pad A does not save a name at once");

    TextField_Begin(&f, "abcdef", 32);
    f.view = 0;
    TextField_ClickAt(&f, 100.0f, 100.0f, 18);
    CHECK(f.cursor == 0, "text: a click at the left edge: the start");
    TextField_ClickAt(&f, 100.0f + 18 * 2 + 5, 100.0f, 18);
    CHECK(f.cursor == 2, "text: a click on a character's left half: before it");
    TextField_ClickAt(&f, 100.0f + 18 * 2 + 12, 100.0f, 18);
    CHECK(f.cursor == 3, "text: on its right half: after it");
    TextField_ClickAt(&f, 900.0f, 100.0f, 18);
    CHECK(f.cursor == 6, "text: past the end: the end");

    char longName[41];
    memset(longName, 'n', 40);
    longName[40] = '\0';
    TextField_Begin(&f, longName, 64);
    TextField_KeepCursorVisible(&f, 13);
    CHECK(f.view == 28, "text: a long text scrolls so the cursor at its end shows");
    Key(&f, IK_HOME);
    TextField_KeepCursorVisible(&f, 13);
    CHECK(f.view == 0, "text: ...and back for Home");
    Key(&f, IK_END);
    TextField_KeepCursorVisible(&f, 13);
    for (int i = 0; i < 30; i++) Key(&f, IK_BACKSPACE);
    TextField_KeepCursorVisible(&f, 13);
    CHECK(f.view == 0 && f.cursor == 10, "text: shortened text scrolls back so nothing hides on the left");
}

/* --------------------------------------------------------------- keymap */

/* An Input in which exactly binding b fires. */
static Input Fire(const KeyBind *b) {
    Input in = Blank();
    switch (b->kind) {
        case KB_KEY:
            in.key[b->code] = in.keyRepeat[b->code] = in.keyDown[b->code] = true;
            in.shift = (b->mods & KM_SHIFT) != 0;
            in.ctrl = (b->mods & KM_CTRL) != 0;
            in.alt = (b->mods & KM_ALT) != 0;
            break;
        case KB_PAD: in.pad[b->code] = in.padRepeat[b->code] = in.padDown[b->code] = true; break;
        case KB_MOUSE: in.mousePressed[b->code] = in.mouseDown[b->code] = true; break;
    }
    return in;
}

static void test_keymap(void) {
    bool everyActionBound = true, everyBindingFires = true, everyActionOnCard = true;
    for (int a = 0; a < KA_COUNT; a++) {
        bool bound = false;
        for (int i = 0; i < Keymap_Count(); i++) if (Keymap_Get(i)->action == (KeyAction)a) bound = true;
        if (!bound) everyActionBound = false;
        if (!Keymap_OnCard((KeyAction)a)) everyActionOnCard = false;
    }
    for (int i = 0; i < Keymap_Count(); i++) {
        Input in = Fire(Keymap_Get(i));
        if (!Keymap_Pressed(Keymap_Get(i)->action, &in) || Keymap_Fired(Keymap_Get(i)->action, &in) < 0) everyBindingFires = false;
    }
    CHECK(everyActionBound, "keymap: every action has a key or button");
    CHECK(everyBindingFires, "keymap: every binding fires its action");
    CHECK(everyActionOnCard, "keymap: every action is on a reference card, so the cards miss nothing");

    Input u = Press(IK_U);
    CHECK(Keymap_Pressed(KA_OPEN_NAME, &u) && !Keymap_Pressed(KA_OPEN_GAME_NAME, &u), "keymap: U is your name");
    u.shift = true;
    CHECK(!Keymap_Pressed(KA_OPEN_NAME, &u) && Keymap_Pressed(KA_OPEN_GAME_NAME, &u), "keymap: Shift+U is this game's name");
    Input tab = Press(IK_TAB);
    CHECK(Keymap_Pressed(KA_OPEN_SETTINGS, &tab) && Keymap_Pressed(KA_UI_NEXT_TAB, &tab) && !Keymap_Pressed(KA_UI_PREV_TAB, &tab),
          "keymap: Tab opens Settings, and is the next tab inside");
    tab.shift = true;
    CHECK(Keymap_Pressed(KA_UI_PREV_TAB, &tab) && !Keymap_Pressed(KA_UI_NEXT_TAB, &tab), "keymap: Shift+Tab is the tab before");
    Input lb = PadPress(PB_LB);
    CHECK(Keymap_Pressed(KA_UI_PREV_TAB, &lb) && Keymap_Pressed(KA_SCROLL_PAGE_UP, &lb), "keymap: LB is the tab before (a page up in lists)");

    Input enter = Press(IK_ENTER);
    int fired = Keymap_Fired(KA_UI_PRESS, &enter);
    Input held = Blank();
    held.keyDown[IK_ENTER] = true;
    CHECK(fired >= 0 && Keymap_Held(fired, &held) && !Keymap_Held(fired, &(Input){0}), "keymap: the key that pressed is held until let go");
    Input space = Press(IK_SPACE);
    int bySpace = Keymap_Fired(KA_UI_PRESS, &space);
    CHECK(bySpace >= 0 && bySpace != fired && !Keymap_Held(bySpace, &held), "keymap: Enter held does not hold a press Space began");

    char name[48];
    for (int i = 0; i < Keymap_Count(); i++) {
        const KeyBind *b = Keymap_Get(i);
        if (b->action == KA_OPEN_GAME_NAME) { Keymap_BindName(b, name, sizeof(name)); CHECK(strcmp(name, "Shift+U") == 0, "keymap: named Shift+U"); }
        if (b->action == KA_TEXT_PASTE) { Keymap_BindName(b, name, sizeof(name)); CHECK(strcmp(name, "Ctrl+V") == 0, "keymap: named Ctrl+V"); }
    }
    Keymap_Names(KA_FULLSCREEN, KB_KEY, name, sizeof(name));
    CHECK(strcmp(name, "F11 / Alt+Enter") == 0, "keymap: fullscreen's keys, joined");
    Keymap_Names(KA_HALL_PREV, KB_PAD, name, sizeof(name));
    CHECK(strcmp(name, "D-pad left / D-pad up") == 0, "keymap: pad buttons by name");
    Keymap_Names(KA_BACK, KB_MOUSE, name, sizeof(name));
    CHECK(strcmp(name, "Back button") == 0, "keymap: the mouse's back button");
    Keymap_Hint(KA_UI_PRESS, DEV_KEYBOARD, name, sizeof(name));
    CHECK(strcmp(name, "Enter") == 0, "keymap: hint for keys");
    Keymap_Hint(KA_UI_PRESS, DEV_PAD, name, sizeof(name));
    CHECK(strcmp(name, "A") == 0, "keymap: hint after pad use");
    Keymap_Hint(KA_OPEN_SETTINGS, DEV_PAD, name, sizeof(name));
    CHECK(strcmp(name, "Tab") == 0, "keymap: no pad button for it: the key");
    bool pad = true;
    Keymap_Cap(KA_ADMIN_REMOVE, DEV_KEYBOARD, name, sizeof(name), &pad);
    CHECK(strcmp(name, "Del") == 0 && !pad, "keymap: a cap uses the short name (Del)");
    Keymap_Cap(KA_OPEN_STATS, DEV_PAD, name, sizeof(name), &pad);
    CHECK(strcmp(name, "View") == 0 && pad, "keymap: after pad use, a cap is the pad's button (View for Stats)");
    Keymap_Cap(KA_HALL_MENU, DEV_PAD, name, sizeof(name), &pad);
    CHECK(strcmp(name, "Menu") == 0 && pad, "keymap: the Arcade menu's pad button is Menu first (B also works)");
    Keymap_Cap(KA_ADMIN_ADD, DEV_PAD, name, sizeof(name), &pad);
    CHECK(strcmp(name, "A") == 0 && !pad, "keymap: no pad button: the key, drawn as a key");
    char tiny[4];
    Keymap_Names(KA_HALL_PREV, KB_KEY, tiny, sizeof(tiny));
    CHECK(strlen(tiny) < sizeof(tiny), "keymap: names never overrun");
}

static void test_keymap_cards(void) {
    CHECK(Keymap_CardCount() >= 8, "cards: one per screen");
    bool fit = true, filled = true;
    KeymapLine lines[KEYMAP_CARD_LINES];
    for (int c = 0; c < Keymap_CardCount(); c++) {
        int n = Keymap_CardLines(c, true, lines, KEYMAP_CARD_LINES);
        if (n == 0 || !Keymap_CardTitle(c)[0]) filled = false;
        if (Kit_TextWidth(Keymap_CardTitle(c), 16) + 2 * 18 + 32 + SET_CONTROL_X > SET_CARD_END_X + 64) fit = false;
        for (int i = 0; i < n; i++) {
            const KeymapLine *l = &lines[i];
            if (!l->keys[0] && !l->pad[0] && !l->mouse[0]) filled = false;
            if (Kit_TextWidth(l->what, 8) > SET_CARD_KEYS_X - SET_CARD_WHAT_X - 8) fit = false;
            if (Kit_TextWidth(l->keys, 8) > SET_CARD_PAD_X - SET_CARD_KEYS_X - 8) fit = false;
            if (Kit_TextWidth(l->pad, 8) > SET_CARD_MOUSE_X - SET_CARD_PAD_X - 8) fit = false;
            if (Kit_TextWidth(l->mouse, 8) > SET_CARD_END_X - SET_CARD_MOUSE_X + 16) fit = false;
        }
        if (SET_CARD_Y + 24 + n * SET_CARD_PITCH > SET_HELP_Y - 16) fit = false;
    }
    CHECK(filled, "cards: every line names a key, a button or a mouse button");
    CHECK(fit, "cards: every column fits, and every card fits above the help line");

    int hall = 0, screens = 1;
    int n = Keymap_CardLines(hall, true, lines, KEYMAP_CARD_LINES);
    bool settings = false, remove = false, jump = false, thisGame = false, daily = false, menu = false;
    for (int i = 0; i < n; i++) {
        if (strcmp(lines[i].keys, "Delete") == 0) remove = true;
        if (strcmp(lines[i].keys, "PgUp / PgDn") == 0 && strcmp(lines[i].pad, "LB / RB") == 0) jump = true;
        if (strcmp(lines[i].keys, "I") == 0 && strcmp(lines[i].pad, "X") == 0) thisGame = true;
        if (strcmp(lines[i].keys, "D") == 0 && strcmp(lines[i].pad, "Y") == 0) daily = true;
        if (strcmp(lines[i].keys, "Esc") == 0 && strcmp(lines[i].pad, "Menu / B") == 0) menu = true;
    }
    CHECK(strcmp(Keymap_CardTitle(hall), "The hall") == 0 && remove && jump && thisGame && daily && menu,
          "cards: the hall's has the pad map (Y daily, X this game, LB/RB five along, Menu or B) and Delete for admin");
    KeymapLine more[KEYMAP_CARD_LINES];
    int m = Keymap_CardLines(screens, false, more, KEYMAP_CARD_LINES);
    for (int i = 0; i < m; i++) {
        if (strcmp(more[i].what, "Settings") == 0 && strcmp(more[i].keys, "Tab") == 0) settings = true;
        if (strcmp(more[i].what, "Stats") == 0 && strcmp(more[i].pad, "View") != 0) settings = false;
    }
    CHECK(strcmp(Keymap_CardTitle(screens), "Screens from the hall") == 0 && settings, "cards: the hall's letters, Tab for Settings, View for Stats");
    int plain = Keymap_CardLines(hall, false, lines, KEYMAP_CARD_LINES);
    remove = false;
    for (int i = 0; i < plain; i++) if (strcmp(lines[i].keys, "Delete") == 0) remove = true;
    CHECK(plain == n - 3 && !remove, "cards: admin keys only for admin");
    CHECK(Keymap_CardCountFor(true) == Keymap_CardCount() && Keymap_CardCountFor(false) == Keymap_CardCount() - 1,
          "cards: admin's own card is last, and only for admin");
}

/* ------------------------------------------------------------- settings */

static Manifest sManifest;
static Profiles sProfiles;
static RowPrefs sRow;
static Lineup sMine;
static LauncherIni sRowIni;

/* Your row: Skyraid and Moondrop favourites, Lanehop hidden. */
static void Row(const char *const *lines) {
    memset(&sRowIni, 0, sizeof(sRowIni));
    for (int i = 0; lines && lines[i]; i++) snprintf(sRowIni.lines[sRowIni.count++], LINI_LINE_LEN, "%s", lines[i]);
    Row_Read(&sRow, &sRowIni, &sManifest, NULL);
    Lineup_Build(&sMine, &sRow, ORDER_YOURS, true);
}

static SettingsView View(void) {
    static const char *const kNames[] = {"Blockfall", "Coilrush", "Brickburst", "Skyraid", "Ghostmaze", "Rockdrift",
                                         "Lanehop", "Crawlshot", "Moondrop", "Girderclimb", "Gemdive", "Chipforge"};
    memset(&sManifest, 0, sizeof(sManifest));
    for (int i = 0; i < 12; i++) {
        snprintf(sManifest.games[i].name, MANIFEST_FIELD_LEN, "%s", kNames[i]);
        snprintf(sManifest.games[i].exec, MANIFEST_FIELD_LEN, "~/.local/bin/%s", kNames[i]);
        for (char *c = strrchr(sManifest.games[i].exec, '/'); *c; c++) if (*c >= 'A' && *c <= 'Z') *c = (char)(*c + 32);
    }
    sManifest.count = 12;
    static const char *const kRow[] = {"fav.skyraid=1", "fav.moondrop=1", "hide.lanehop=1", NULL};
    Row(kRow);
    memset(&sProfiles, 0, sizeof(sProfiles));
    Profiles_SetUsername(&sProfiles, PROFILE_DEFAULT_KEY, "TRG");
    Profiles_SetUsername(&sProfiles, "Blockfall", "Arcade");
    Profiles_SetUsername(&sProfiles, "Gemdive", "Digger");
    SettingsView v;
    memset(&v, 0, sizeof(v));
    v.musicVolume = 70;
    v.screenShake = true;
    v.hallSounds = 10;
    v.soundReady = true;
    v.sharing = true;
    v.checkUpdates = true;
    v.serverUrl = "https://example.supabase.co/rest";
    v.manifest = &sManifest;
    v.profiles = &sProfiles;
    v.now = 100000;
    v.hallMusic = true;
    v.hallSong = "Built-in theme";
    v.startOnLast = true;
    v.row = &sRow;
    v.mine = &sMine;
    return v;
}

static SettingsPage sPage;

static float RowY0(void) { return (float)SET_ROW_Y; }

static bool PageSane(const SettingsPage *p) {
    const KitList *l = &p->list;
    if (l->count < 3 || l->items[l->count - 1].id != WID_CLOSE || !Kit_Get(l, WID_BACK) || !Kit_Get(l, WID_SET_TABS)) return false;
    for (int i = 0; i < l->count; i++) {
        KitRect r = Kit_Bounds(&l->items[i]);
        if (r.x < 0 || r.y < 0 || r.x + r.w > KIT_SCREEN_W || r.y + r.h > KIT_SCREEN_H) return false;
        const KitItem *it = &l->items[i];
        bool drawnLabel = it->kind == KIT_BUTTON && !(it->flags & KIT_SELFDRAWN);
        if (drawnLabel && it->label[0] && Kit_TextWidth(it->label, it->textSize) + 8 > it->r.w) return false;
        for (int j = i + 1; j < l->count; j++) {
            if (!Kit_CanFocus(it) || !Kit_CanFocus(&l->items[j])) continue;
            KitRect o = Kit_Bounds(&l->items[j]);
            if (r.x < o.x + o.w && o.x < r.x + r.w && r.y < o.y + o.h && o.y < r.y + r.h) return false;
        }
    }
    for (int i = 0; i < p->rowCount; i++) {
        const SettingsRow *r = &p->rows[i];
        if (r->label[0] && r->kind != SROW_HEADING && SET_LABEL_X + Kit_TextWidth(r->label, 16) + (r->shared ? 24 : 0) > SET_FIELD_X - 4 &&
            (r->field || r->controlCount)) {
            /* a label may run long only over empty room: never under a control */
            for (int c = 0; c < r->controlCount; c++)
                if (Kit_Get(&p->list, r->controls[c])->r.x < SET_LABEL_X + Kit_TextWidth(r->label, 16) + 8) return false;
        }
        for (int n = 0; n < 2; n++) if (r->note[n][0] && r->noteX + Kit_TextWidth(r->note[n], 8) > SET_ROW_X + SET_ROW_W + 40) return false;
    }
    return true;
}

static void test_settings_layout(void) {
    Settings s;
    Settings_Init(&s);
    SettingsView v = View();
    bool sane = true;
    for (int t = 0; t < ST_COUNT; t++) {
        s.tab = t;
        Settings_Layout(&s, &v, &sPage);
        if (!PageSane(&sPage)) { sane = false; printf("  tab %s not sane\n", Settings_TabLabel(t)); }
        const KitItem *tabs = Kit_Get(&sPage.list, WID_SET_TABS);
        if (!tabs || tabs->value != t || tabs->cellCount != ST_COUNT) sane = false;
    }
    CHECK(sane, "settings: every tab's controls are on screen, labels fit, nothing overlaps, X on top");

    /* Room for Machines (Phase 5): six tabs still fit across the top. */
    static const char *const six[] = {"Player", "Sound", "Screen", "Online", "Controls", "Machines"};
    KitItem probe;
    memset(&probe, 0, sizeof(probe));
    probe.kind = KIT_TABS;
    probe.textSize = 16;
    probe.cells = six;
    probe.cellCount = 6;
    CHECK(Kit_Bounds(&probe).w <= KIT_SCREEN_W - 32, "settings: a sixth tab (Machines) still fits across the top");
    s.tab = ST_PLAYER;
    Settings_Layout(&s, &v, &sPage);
    KitRect tb = Kit_Bounds(Kit_Get(&sPage.list, WID_SET_TABS));
    CHECK(tb.x == 80 && tb.y == SET_TABS_Y && tb.x + tb.w <= KIT_SCREEN_W - 40,
          "settings: the tabs line up under the title and end with the rule under them (x 40..920)");

    /* LB/RB, Tab/Shift+Tab: the kit steps the tab bar, round the ends. */
    KitState k;
    Kit_Init(&k);
    Input lb = PadPress(PB_LB);
    KitEvent ev = Kit_Update(&k, &sPage.list, &lb);
    CHECK(ev.type == KIT_EV_CHANGE && ev.id == WID_SET_TABS && ev.value == ST_COUNT - 1, "tabs: LB on the first tab comes round to the last");
    Input rb = PadPress(PB_RB);
    ev = Kit_Update(&k, &sPage.list, &rb);
    CHECK(ev.type == KIT_EV_CHANGE && ev.value == ST_SOUND, "tabs: RB is the next tab");
    Input tabKey = Press(IK_TAB);
    ev = Kit_Update(&k, &sPage.list, &tabKey);
    CHECK(ev.type == KIT_EV_CHANGE && ev.value == ST_SOUND, "tabs: Tab is the next tab");
    tabKey.shift = true;
    ev = Kit_Update(&k, &sPage.list, &tabKey);
    CHECK(ev.type == KIT_EV_CHANGE && ev.value == ST_COUNT - 1, "tabs: Shift+Tab the one before");
    const KitItem *bar = Kit_Get(&sPage.list, WID_SET_TABS);
    KitRect online = Kit_CellRect(bar, ST_ONLINE);
    CHECK(Kit_CellAt(bar, online.x + 4, online.y + 4) == ST_ONLINE, "tabs: each tab is a click target");

    /* Player. */
    CHECK(Kit_Get(&sPage.list, WID_SET_NAME_FIELD) && Kit_Get(&sPage.list, WID_SET_NAME_EDIT) && !Kit_Get(&sPage.list, WID_SET_NAME_CANCEL),
          "player: the arcade name, with Edit");
    CHECK(!Kit_CanFocus(Kit_Get(&sPage.list, WID_SET_NAME_FIELD)), "player: the name box is for the mouse; keys use Edit");
    CHECK(Kit_Get(&sPage.list, WID_SET_GAME_EDIT + 0) && Kit_Get(&sPage.list, WID_SET_GAME_EDIT + SET_NAMES_VISIBLE - 1) &&
          !Kit_Get(&sPage.list, WID_SET_GAME_EDIT + SET_NAMES_VISIBLE), "player: six names show at a time");
    CHECK(Kit_Get(&sPage.list, WID_SET_GAME_CLEAR + 0) && !Kit_Get(&sPage.list, WID_SET_GAME_CLEAR + 1), "player: Clear only where a game has its own name");
    CHECK(strcmp(Kit_Get(&sPage.list, WID_SET_GAME_EDIT + 0)->label, "Edit") == 0 && strcmp(Kit_Get(&sPage.list, WID_SET_GAME_EDIT + 1)->label, "Set") == 0,
          "player: Edit a name a game has, Set one it has not");
    const SettingsRow *r1 = Settings_RowOf(&sPage, WID_SET_GAME_EDIT + 1);
    CHECK(r1 && r1->placeholder && strcmp(r1->value, "Uses your arcade name") == 0, "player: a game without its own name says it uses yours");
    CHECK(Settings_FieldChars(false) == 17 && Settings_FieldChars(true) == 13, "player: a name box shows 17 characters, 13 while typing");
    CHECK(Kit_FirstFocus(&sPage.list) == WID_SET_NAME_EDIT, "player: opens on the arcade name");

    s.tab = ST_SOUND;
    Settings_Layout(&s, &v, &sPage);
    const KitItem *music = Kit_Get(&sPage.list, WID_SET_MUSIC);
    CHECK(music && music->kind == KIT_SLIDER && music->value == 7 && music->max == 10 && Settings_RowOf(&sPage, WID_SET_MUSIC)->shared,
          "sound: music volume is ten blocks, shared with every game");
    CHECK(Kit_Get(&sPage.list, WID_SET_HALL_SOUNDS)->value == 10 && !Settings_RowOf(&sPage, WID_SET_HALL_SOUNDS)->shared,
          "sound: hall sounds are the launcher's own");
    v.soundReady = false;
    Settings_Layout(&s, &v, &sPage);
    CHECK(Kit_Get(&sPage.list, WID_SET_COIN_TEST)->flags & KIT_DISABLED, "sound: no sound device, no coin to play (and it says why)");
    v.soundReady = true;
    v.musicVolume = 75;
    Settings_Layout(&s, &v, &sPage);
    CHECK(Kit_Get(&sPage.list, WID_SET_MUSIC)->value == 8, "sound: a volume between blocks shows rounded");

    s.tab = ST_SCREEN;
    Settings_Layout(&s, &v, &sPage);
    CHECK(Kit_Get(&sPage.list, WID_SET_FULLSCREEN)->value == 0 && Kit_Get(&sPage.list, WID_SET_SHAKE)->value == 1 &&
          Settings_RowOf(&sPage, WID_SET_FLASHING)->shared && sPage.footnote, "screen: three shared switches, with the * note");
    const KitItem *hints = Kit_Get(&sPage.list, WID_SET_BUTTON_HINTS);
    CHECK(hints && hints->kind == KIT_STEPPER && hints->value == HINTS_AUTO && strcmp(hints->label, "Auto") == 0 &&
              (hints->flags & KIT_WRAP) && hints->max == HINTS_COUNT - 1 && !Settings_RowOf(&sPage, WID_SET_BUTTON_HINTS)->shared,
          "screen: Button hints < Auto | Keyboard | Xbox pad >, the launcher's own");
    v.buttonHints = HINTS_PAD;
    Settings_Layout(&s, &v, &sPage);
    KitRect padRect = Kit_Get(&sPage.list, WID_SET_BUTTON_HINTS)->r;
    v.buttonHints = HINTS_KEYS;
    Settings_Layout(&s, &v, &sPage);
    CHECK(strcmp(Kit_Get(&sPage.list, WID_SET_BUTTON_HINTS)->label, "Keyboard") == 0 &&
              Kit_Get(&sPage.list, WID_SET_BUTTON_HINTS)->r.w == padRect.w,
          "screen: each choice in the same place (the arrows hold still)");
    v.buttonHints = HINTS_AUTO;
    KitEvent hintsEv = {KIT_EV_CHANGE, WID_SET_BUTTON_HINTS, HINTS_PAD};
    SettingsAction hintsAct = Settings_Event(&s, &hintsEv);
    CHECK(hintsAct.type == SA_BUTTON_HINTS && hintsAct.value == HINTS_PAD, "screen: changing Button hints asks for its save");
    CHECK(Settings_HintDevice(HINTS_AUTO, DEV_PAD) == DEV_PAD && Settings_HintDevice(HINTS_AUTO, DEV_MOUSE) == DEV_KEYBOARD &&
              Settings_HintDevice(HINTS_KEYS, DEV_PAD) == DEV_KEYBOARD && Settings_HintDevice(HINTS_PAD, DEV_KEYBOARD) == DEV_PAD,
          "screen: hints follow the device used last on Auto, else the choice");

    s.tab = ST_ONLINE;
    v.sharing = false;
    v.updateStatus = 2; /* UPDATE_BEHIND */
    v.checkedAt = v.now - 7200;
    Settings_Layout(&s, &v, &sPage);
    const SettingsRow *chk = Settings_RowOf(&sPage, WID_SET_CHECK_NOW);
    CHECK(Kit_Get(&sPage.list, WID_SET_SHARING)->value == 0 && (Kit_Get(&sPage.list, WID_SET_SYNC_NOW)->flags & KIT_DISABLED),
          "online: sharing off: Sync scores now is faint");
    CHECK(chk && strcmp(chk->note[0], "Last checked 2 hours ago") == 0 && strcmp(chk->note[1], "A newer version is on GitHub.") == 0,
          "online: Check now says when, and what GitHub said");
    v.checking = true;
    Settings_Layout(&s, &v, &sPage);
    CHECK(strcmp(Kit_Get(&sPage.list, WID_SET_CHECK_NOW)->label, "Checking...") == 0 && (Kit_Get(&sPage.list, WID_SET_CHECK_NOW)->flags & KIT_DISABLED),
          "online: Checking... while it waits");
    v.checking = false;
    v.checkUpdates = false;
    Settings_Layout(&s, &v, &sPage);
    CHECK((Kit_Get(&sPage.list, WID_SET_CHECK_NOW)->flags & KIT_DISABLED) && Kit_Get(&sPage.list, WID_SET_CHECKS)->value == 0,
          "online: update checks off: Check now is faint");
    bool server = false;
    for (int i = 0; i < sPage.rowCount; i++)
        if (strcmp(sPage.rows[i].label, "Server") == 0 && sPage.rows[i].kind == SROW_INFO && strcmp(sPage.rows[i].note[0], "example.supabase.co") == 0)
            server = true;
    CHECK(server, "online: the server is shown, not editable");
    v.sharing = true;
    v.syncing = true;
    Settings_Layout(&s, &v, &sPage);
    CHECK(strcmp(Kit_Get(&sPage.list, WID_SET_SYNC_NOW)->label, "Syncing...") == 0, "online: Syncing... while it waits");

    s.tab = ST_CONTROLS;
    s.card = 2;
    Settings_Layout(&s, &v, &sPage);
    const KitItem *card = Kit_Get(&sPage.list, WID_SET_CARD);
    CHECK(card && card->kind == KIT_STEPPER && card->value == 2 && strcmp(card->label, Keymap_CardTitle(2)) == 0 && sPage.cardLines > 0,
          "controls: a stepper picks the card, and its lines are laid out");
}

static void test_settings_navigation(void) {
    Settings s;
    Settings_Init(&s);
    SettingsView v = View();
    KitState k;
    Kit_Init(&k);
    Settings_Layout(&s, &v, &sPage);
    k.focus = WID_SET_TABS;
    Input down = Press(IK_DOWN), up = Press(IK_UP), right = Press(IK_RIGHT), left = Press(IK_LEFT);
    Settings_Navigate(&s, &k, &sPage, &v, &down);
    CHECK(k.focus == WID_SET_NAME_EDIT, "nav: Down from the tabs: the first row");
    Settings_Navigate(&s, &k, &sPage, &v, &down);
    CHECK(k.focus == WID_SET_START_ON, "nav: Down to Start on");
    Settings_Navigate(&s, &k, &sPage, &v, &down);
    CHECK(k.focus == WID_SET_GAME_EDIT + 0, "nav: over the heading to the first game");
    Settings_Navigate(&s, &k, &sPage, &v, &right);
    CHECK(k.focus == WID_SET_GAME_CLEAR + 0, "nav: Right along the row to Clear");
    CHECK(!Settings_Navigate(&s, &k, &sPage, &v, &right) && k.focus == WID_SET_GAME_CLEAR + 0, "nav: Right stops at the row's end");
    Settings_Navigate(&s, &k, &sPage, &v, &down);
    CHECK(k.focus == WID_SET_GAME_EDIT + 1, "nav: Down into a row without Clear lands on Set");
    for (int i = 0; i < SET_NAMES_VISIBLE - 2; i++) Settings_Navigate(&s, &k, &sPage, &v, &down);
    CHECK(k.focus == WID_SET_GAME_EDIT + SET_NAMES_VISIBLE - 1 && s.names.top == 0, "nav: down to the last name shown");
    Settings_Navigate(&s, &k, &sPage, &v, &down);
    Settings_Layout(&s, &v, &sPage);
    CHECK(k.focus == WID_SET_GAME_EDIT + SET_NAMES_VISIBLE && s.names.top == 1 && Kit_Get(&sPage.list, k.focus), "nav: one more scrolls the names");
    for (int i = 0; i < 10; i++) { Settings_Navigate(&s, &k, &sPage, &v, &down); Settings_Layout(&s, &v, &sPage); }
    CHECK(k.focus == WID_SET_GAME_EDIT + 11 && s.names.top == 12 - SET_NAMES_VISIBLE, "nav: and stops at the last machine");
    Settings_Navigate(&s, &k, &sPage, &v, &right);
    Settings_Navigate(&s, &k, &sPage, &v, &up);
    Settings_Layout(&s, &v, &sPage);
    CHECK(k.focus == WID_SET_GAME_CLEAR + 10 || k.focus == WID_SET_GAME_EDIT + 10, "nav: Up keeps to the column where it can");
    for (int i = 0; i < 20; i++) { Settings_Navigate(&s, &k, &sPage, &v, &up); Settings_Layout(&s, &v, &sPage); }
    CHECK(k.focus == WID_SET_TABS && s.names.top == 0, "nav: all the way up scrolls back, through Start on and the arcade name, to the tabs");
    CHECK(!Settings_Navigate(&s, &k, &sPage, &v, &up), "nav: nothing above the tabs");
    CHECK(!Settings_Navigate(&s, &k, &sPage, &v, &left) && k.focus == WID_SET_TABS, "nav: Left on the tabs is the kit's (it changes tab)");

    /* Sound: a slider's Left/Right change it, they never move the focus. */
    s.tab = ST_SOUND;
    v.soundReady = false;
    Settings_Layout(&s, &v, &sPage);
    k.focus = WID_SET_MUSIC;
    CHECK(!Settings_Navigate(&s, &k, &sPage, &v, &left) && k.focus == WID_SET_MUSIC, "nav: Left on the blocks stays (the kit turns it down)");
    Settings_Navigate(&s, &k, &sPage, &v, &down);
    CHECK(k.focus == WID_SET_HALL_MUSIC, "nav: Down to hall music");
    CHECK(!Settings_Navigate(&s, &k, &sPage, &v, &right) && k.focus == WID_SET_HALL_MUSIC, "nav: Right on the switch is the kit's (it switches)");
    Settings_Navigate(&s, &k, &sPage, &v, &down);
    CHECK(k.focus == WID_SET_HALL_SOUNDS, "nav: Down to hall sounds");
    CHECK(!Settings_Navigate(&s, &k, &sPage, &v, &down) && k.focus == WID_SET_HALL_SOUNDS, "nav: a faint button is skipped");

    /* Online: the server line is never a stop. */
    s.tab = ST_ONLINE;
    v.sharing = false;
    Settings_Layout(&s, &v, &sPage);
    k.focus = WID_SET_UPDATE_DETAILS;
    CHECK(!Settings_Navigate(&s, &k, &sPage, &v, &down) && k.focus == WID_SET_UPDATE_DETAILS, "nav: faint Sync and the server line: Down stays");
    v.sharing = true;
    Settings_Layout(&s, &v, &sPage);
    Settings_Navigate(&s, &k, &sPage, &v, &down);
    CHECK(k.focus == WID_SET_SYNC_NOW, "nav: Down to Sync scores now");
    Input pad = PadPress(PB_UP);
    Settings_Navigate(&s, &k, &sPage, &v, &pad);
    CHECK(k.focus == WID_SET_UPDATE_DETAILS, "nav: the d-pad moves as the arrows do");

    /* Tabs: a new tab opens on its first control, unless the tabs had the focus. */
    k.focus = WID_SET_SYNC_NOW;
    Settings_OpenTab(&s, &k, &v, ST_SCREEN);
    CHECK(s.tab == ST_SCREEN && k.focus == WID_SET_FULLSCREEN, "tabs: opens on the first row");
    k.focus = WID_SET_TABS;
    Settings_OpenTab(&s, &k, &v, ST_ONLINE);
    CHECK(s.tab == ST_ONLINE && k.focus == WID_SET_TABS, "tabs: stepping along the tabs stays on them");

    /* The wheel scrolls the names; a focus scrolled away follows. */
    s.tab = ST_PLAYER;
    s.names.top = 0;
    Settings_Layout(&s, &v, &sPage);
    k.focus = WID_SET_GAME_EDIT + 0;
    Input wheel = Pointer(500, SET_NAMES_Y + 60, false);
    wheel.wheel = -3.0f;
    Settings_Wheel(&s, &k, &sPage, &v, &wheel);
    CHECK(s.names.top == 3 && k.focus == WID_SET_GAME_EDIT + 3, "wheel: scrolls the names; the focus moves to a name still shown");
    wheel.wheel = -20.0f;
    Settings_Layout(&s, &v, &sPage);
    Settings_Wheel(&s, &k, &sPage, &v, &wheel);
    CHECK(s.names.top == 12 - SET_NAMES_VISIBLE, "wheel: stops at the end");
    s.names = (KitScroll){0, 0.0f};
    wheel.wheel = -0.5f;
    for (int i = 0; i < 4; i++) {
        Settings_Layout(&s, &v, &sPage);
        Settings_Wheel(&s, &k, &sPage, &v, &wheel);
    }
    CHECK(s.names.top == 2, "wheel: half notches from the top add up");
    s.names.top = 5;
    Input away = Pointer(500, 180, false);
    away.wheel = 2.0f;
    Settings_Layout(&s, &v, &sPage);
    Settings_Wheel(&s, &k, &sPage, &v, &away);
    CHECK(s.names.top == 5, "wheel: only over the names list");
    s.names.top = 30; /* a catalog that shrank since the list was scrolled */
    Settings_Layout(&s, &v, &sPage);
    int last = 12 - SET_NAMES_VISIBLE;
    CHECK(sPage.table.top == last && Kit_Get(&sPage.list, WID_SET_GAME_EDIT + 11) && Kit_Get(&sPage.list, WID_SET_GAME_EDIT + last) &&
          !Kit_Get(&sPage.list, WID_SET_GAME_EDIT + last - 1), "names: a list scrolled past its end shows its last six");
    k.focus = WID_SET_GAME_EDIT + last;
    Settings_Navigate(&s, &k, &sPage, &v, &up);
    CHECK(s.names.top == last - 1 && k.focus == WID_SET_GAME_EDIT + last - 1, "names: and Up from there scrolls from what is shown");
    Settings_ShowName(&s, 1, 12);
    CHECK(s.names.top == 1, "names: showing one above scrolls up to it");
    Settings_ShowName(&s, 11, 12);
    CHECK(s.names.top == last, "names: and one below, down to it");

    /* The hint names Left/Right only where they do something. */
    s.names.top = 0;
    Settings_Layout(&s, &v, &sPage);
    CHECK(Settings_SideKeys(&sPage, WID_SET_NAME_EDIT) == NULL, "hint: Edit alone in its row: Left/Right do nothing, so no hint");
    CHECK(strcmp(Settings_SideKeys(&sPage, WID_SET_GAME_EDIT + 0), "move") == 0, "hint: Edit and Clear: Left/Right move along");
    CHECK(strcmp(Settings_SideKeys(&sPage, WID_SET_TABS), "change") == 0, "hint: on the tabs they change tab");
    s.tab = ST_SOUND;
    Settings_Layout(&s, &v, &sPage);
    CHECK(strcmp(Settings_SideKeys(&sPage, WID_SET_MUSIC), "change") == 0, "hint: on the blocks they change the level");
    s.tab = ST_ONLINE;
    Settings_Layout(&s, &v, &sPage);
    CHECK(Settings_SideKeys(&sPage, WID_SET_UPDATE_DETAILS) == NULL, "hint: a lone button: none");
    s.tab = ST_PLAYER;
}

/* Player's Start on, Sound's Hall music, and the Machines tab: your row. */
static void test_settings_your_hall(void) {
    Settings s;
    Settings_Init(&s);
    SettingsView v = View();
    s.tab = ST_PLAYER;
    Settings_Layout(&s, &v, &sPage);
    const KitItem *start = Kit_Get(&sPage.list, WID_SET_START_ON);
    CHECK(start && start->kind == KIT_STEPPER && start->value == 0 && strcmp(start->label, "Last played") == 0 && (start->flags & KIT_WRAP) &&
              strcmp(Settings_RowOf(&sPage, WID_SET_START_ON)->label, "Start on") == 0 && !Settings_RowOf(&sPage, WID_SET_START_ON)->shared,
          "player: Start on < Last played >, the launcher's own");
    v.startOnLast = false;
    Settings_Layout(&s, &v, &sPage);
    CHECK(strcmp(Kit_Get(&sPage.list, WID_SET_START_ON)->label, "First machine") == 0, "player: or < First machine >");
    KitEvent ev = {KIT_EV_CHANGE, WID_SET_START_ON, 0};
    SettingsAction act = Settings_Event(&s, &ev);
    CHECK(act.type == SA_START_ON && act.value == 1, "player: choosing Last played asks to start on it");
    ev.value = 1;
    act = Settings_Event(&s, &ev);
    CHECK(act.type == SA_START_ON && act.value == 0, "player: and First machine, the first");

    s.tab = ST_SOUND;
    Settings_Layout(&s, &v, &sPage);
    const KitItem *music = Kit_Get(&sPage.list, WID_SET_HALL_MUSIC);
    const SettingsRow *mr = Settings_RowOf(&sPage, WID_SET_HALL_MUSIC);
    CHECK(music && music->kind == KIT_TOGGLE && music->value == 1 && mr && strcmp(mr->label, "Hall music") == 0 && !mr->shared &&
              strcmp(mr->note[0], "Built-in theme") == 0 && mr->r.y == SET_ROW_Y + SET_ROW_PITCH,
          "sound: Hall music On|Off under the music volume, saying what it plays");
    v.hallSong = "Ghost Arcade (launcher)";
    Settings_Layout(&s, &v, &sPage);
    CHECK(strcmp(Settings_RowOf(&sPage, WID_SET_HALL_MUSIC)->note[0], "Ghost Arcade (launcher)") == 0, "sound: ...or the Chipforge song the hall has");
    v.hallMusic = false;
    Settings_Layout(&s, &v, &sPage);
    CHECK(Kit_Get(&sPage.list, WID_SET_HALL_MUSIC)->value == 0 && strstr(Settings_RowOf(&sPage, WID_SET_HALL_MUSIC)->note[0], "quiet"),
          "sound: Off, and the hall is quiet");
    ev = (KitEvent){KIT_EV_CHANGE, WID_SET_HALL_MUSIC, 1};
    act = Settings_Event(&s, &ev);
    CHECK(act.type == SA_HALL_MUSIC && act.value == 1, "sound: switching it asks to save it");
    v.hallMusic = true;

    /* Machines: Row order, then every machine in your order, favourites first. */
    s.tab = ST_MACHINES;
    Settings_Layout(&s, &v, &sPage);
    const KitItem *order = Kit_Get(&sPage.list, WID_SET_ROW_ORDER);
    CHECK(order && order->kind == KIT_STEPPER && order->max == ORDER_COUNT - 1 && strcmp(order->label, "Your order") == 0 && (order->flags & KIT_WRAP),
          "machines: Row order < Your order >, round the four");
    CHECK(sPage.table.shown && sPage.table.count == 12 && sPage.table.visible == SET_MACH_VISIBLE && sPage.table.top == 0,
          "machines: all twelve, hidden ones too, seven at a time");
    const SettingsRow *first = Settings_RowOf(&sPage, WID_SET_MACH_FAV + 3), *second = Settings_RowOf(&sPage, WID_SET_MACH_FAV + 8);
    CHECK(first && second && first->tablePos == 0 && second->tablePos == 1 && first->star && strcmp(first->label, "Skyraid") == 0,
          "machines: favourites lead your order, starred");
    CHECK(strcmp(Kit_Get(&sPage.list, WID_SET_MACH_FAV + 3)->label, "Unfavourite") == 0 &&
              strcmp(Kit_Get(&sPage.list, WID_SET_MACH_FAV + 0)->label, "Favourite") == 0,
          "machines: Favourite, or Unfavourite on a favourite");
    const KitItem *up = Kit_Get(&sPage.list, WID_SET_MACH_UP + 3), *down = Kit_Get(&sPage.list, WID_SET_MACH_DOWN + 8);
    CHECK((up->flags & KIT_DISABLED) && strcmp(up->reason, "Already first.") == 0 && (down->flags & KIT_DISABLED) &&
              strcmp(down->reason, "Favourites stay first.") == 0 && !(Kit_Get(&sPage.list, WID_SET_MACH_DOWN + 3)->flags & KIT_DISABLED),
          "machines: Up and Down are faint, with the reason, where they cannot move it");
    CHECK((Kit_Get(&sPage.list, WID_SET_MACH_UP + 0)->flags & KIT_DISABLED) && strcmp(Kit_Get(&sPage.list, WID_SET_MACH_UP + 0)->reason, "Favourites stay first.") == 0,
          "machines: the first of the rest cannot pass the favourites");
    s.machines.top = 5;
    Settings_Layout(&s, &v, &sPage);
    const SettingsRow *lane = Settings_RowOf(&sPage, WID_SET_MACH_HIDE + 6);
    CHECK(lane && lane->dim && strcmp(Kit_Get(&sPage.list, WID_SET_MACH_HIDE + 6)->label, "Show") == 0 &&
              Kit_Get(&sPage.list, WID_SET_MACH_HIDE + 4) && strcmp(Kit_Get(&sPage.list, WID_SET_MACH_HIDE + 4)->label, "Hide") == 0,
          "machines: a hidden machine is dim, and Show brings it back");
    CHECK(PageSane(&sPage), "machines: scrolled, everything still fits");
    s.machines.top = 0;

    /* Events: each button names its machine by catalog index. */
    static const struct { int id; SettingsActionType type; } kPress[] = {
        {WID_SET_MACH_FAV + 2, SA_MACH_FAVOURITE}, {WID_SET_MACH_UP + 2, SA_MACH_UP}, {WID_SET_MACH_DOWN + 2, SA_MACH_DOWN}, {WID_SET_MACH_HIDE + 2, SA_MACH_HIDE},
    };
    bool events = true;
    for (size_t i = 0; i < sizeof(kPress) / sizeof(kPress[0]); i++) {
        KitEvent pe = {KIT_EV_PRESS, kPress[i].id, 0};
        SettingsAction a = Settings_Event(&s, &pe);
        if (a.type != kPress[i].type || a.which != 2) events = false;
    }
    CHECK(events, "machines: Favourite, Up, Down and Hide, each for its machine");
    ev = (KitEvent){KIT_EV_CHANGE, WID_SET_ROW_ORDER, ORDER_AZ};
    act = Settings_Event(&s, &ev);
    CHECK(act.type == SA_ROW_ORDER && act.value == ORDER_AZ, "machines: a new row order");

    /* The keys: Down from Row order into the list, along a row, down past the
     * seventh scrolls; the focus stays on a machine's button as the list moves. */
    KitState k;
    Kit_Init(&k);
    Settings_Layout(&s, &v, &sPage);
    k.focus = WID_SET_ROW_ORDER;
    Input dn = Press(IK_DOWN), right = Press(IK_RIGHT);
    Settings_Navigate(&s, &k, &sPage, &v, &dn);
    CHECK(k.focus == WID_SET_MACH_FAV + 3, "machines: Down from Row order to the first machine's Favourite");
    Settings_Navigate(&s, &k, &sPage, &v, &right);
    CHECK(k.focus == WID_SET_MACH_DOWN + 3, "machines: Right skips the faint Up to Down");
    for (int i = 0; i < SET_MACH_VISIBLE; i++) { Settings_Navigate(&s, &k, &sPage, &v, &dn); Settings_Layout(&s, &v, &sPage); }
    CHECK(s.machines.top == 1 && Settings_RowOf(&sPage, k.focus) && Settings_RowOf(&sPage, k.focus)->tablePos == SET_MACH_VISIBLE,
          "machines: past the seventh, the list scrolls");
    Settings_ShowMachine(&s, 11, 12);
    CHECK(s.machines.top == 12 - SET_MACH_VISIBLE, "machines: a machine shown at the end");
    Settings_ShowMachine(&s, 0, 12);
    CHECK(s.machines.top == 0, "machines: and at the top");
    Settings_Layout(&s, &v, &sPage);
    Input wheel = Pointer(500, SET_MACH_Y + 40, false);
    wheel.wheel = -20.0f;
    Settings_Wheel(&s, &k, &sPage, &v, &wheel);
    CHECK(s.machines.top == 12 - SET_MACH_VISIBLE && s.names.top == 0, "machines: the wheel scrolls this list, not the names");

    /* One machine left on the row: its Hide is faint. */
    static const char *const kAllButOne[] = {"hide.blockfall=1", "hide.coilrush=1", "hide.brickburst=1", "hide.skyraid=1", "hide.ghostmaze=1",
                                             "hide.rockdrift=1", "hide.lanehop=1", "hide.crawlshot=1", "hide.moondrop=1", "hide.girderclimb=1",
                                             "hide.gemdive=1", NULL};
    Row(kAllButOne);
    s.machines.top = 5;
    Settings_Layout(&s, &v, &sPage);
    const KitItem *last = Kit_Get(&sPage.list, WID_SET_MACH_HIDE + 11);
    CHECK(last && (last->flags & KIT_DISABLED) && strstr(last->reason, "stays"), "machines: the last one on the row cannot be hidden");
    s.machines.top = 0;
    SettingsView none = View();
    none.row = NULL;
    none.mine = NULL;
    Settings_Layout(&s, &none, &sPage);
    CHECK(sPage.table.count == 0 && PageSane(&sPage), "machines: without a row, an empty list");
}

static void test_settings_events(void) {
    Settings s;
    Settings_Init(&s);
    KitEvent ev = {KIT_EV_CHANGE, WID_SET_MUSIC, 4};
    SettingsAction a = Settings_Event(&s, &ev);
    CHECK(a.type == SA_SHARED && a.which == SP_MUSIC && a.value == 40, "events: four blocks is music volume 40");
    ev = (KitEvent){KIT_EV_CHANGE, WID_SET_FULLSCREEN, 1};
    a = Settings_Event(&s, &ev);
    CHECK(a.type == SA_SHARED && a.which == SP_FULLSCREEN && a.value == 1, "events: fullscreen on");
    ev = (KitEvent){KIT_EV_CHANGE, WID_SET_FLASHING, 1};
    CHECK(Settings_Event(&s, &ev).which == SP_FLASHING, "events: reduced flashing");
    ev = (KitEvent){KIT_EV_CHANGE, WID_SET_SHAKE, 0};
    CHECK(Settings_Event(&s, &ev).which == SP_SHAKE, "events: screen shake");
    ev = (KitEvent){KIT_EV_CHANGE, WID_SET_HALL_SOUNDS, 3};
    a = Settings_Event(&s, &ev);
    CHECK(a.type == SA_HALL_SOUNDS && a.value == 3, "events: hall sounds");
    ev = (KitEvent){KIT_EV_CHANGE, WID_SET_SHARING, 1};
    CHECK(Settings_Event(&s, &ev).type == SA_SHARING, "events: sharing");
    ev = (KitEvent){KIT_EV_CHANGE, WID_SET_CHECKS, 0};
    CHECK(Settings_Event(&s, &ev).type == SA_CHECKS, "events: update checks");
    ev = (KitEvent){KIT_EV_CHANGE, WID_SET_TABS, ST_ONLINE};
    a = Settings_Event(&s, &ev);
    CHECK(a.type == SA_TAB && a.value == ST_ONLINE, "events: a tab");
    ev = (KitEvent){KIT_EV_CHANGE, WID_SET_CARD, 3};
    CHECK(Settings_Event(&s, &ev).type == SA_NONE && s.card == 3, "events: the card turns by itself");
    ev = (KitEvent){KIT_EV_PRESS, WID_SET_CHECK_NOW, 0};
    CHECK(Settings_Event(&s, &ev).type == SA_CHECK_NOW, "events: Check now");
    ev = (KitEvent){KIT_EV_PRESS, WID_SET_SYNC_NOW, 0};
    CHECK(Settings_Event(&s, &ev).type == SA_SYNC_NOW, "events: Sync scores now");
    ev = (KitEvent){KIT_EV_PRESS, WID_SET_UPDATE_DETAILS, 0};
    CHECK(Settings_Event(&s, &ev).type == SA_UPDATE_DETAILS, "events: Update details");
    ev = (KitEvent){KIT_EV_PRESS, WID_SET_COIN_TEST, 0};
    CHECK(Settings_Event(&s, &ev).type == SA_PLAY_COIN, "events: the coin");
    ev = (KitEvent){KIT_EV_PRESS, WID_SET_NAME_EDIT, 0};
    a = Settings_Event(&s, &ev);
    CHECK(a.type == SA_EDIT_NAME && a.which == SET_ARCADE_NAME && a.value == 0, "events: Edit the arcade name");
    ev = (KitEvent){KIT_EV_PRESS, WID_SET_GAME_FIELD + 4, 0};
    a = Settings_Event(&s, &ev);
    CHECK(a.type == SA_EDIT_NAME && a.which == 4 && a.value == 1, "events: a click on a game's name box types there");
    ev = (KitEvent){KIT_EV_PRESS, WID_SET_GAME_CLEAR + 9, 0};
    a = Settings_Event(&s, &ev);
    CHECK(a.type == SA_CLEAR_NAME && a.which == 9, "events: Clear a game's name");
    ev = (KitEvent){KIT_EV_PRESS, WID_BACK, 0};
    CHECK(Settings_Event(&s, &ev).type == SA_NONE, "events: Back is the screen's");
}

static SettingsAction TypeFrame(Settings *s, const SettingsView *v, Input in, const char *clip) {
    Settings_Layout(s, v, &sPage);
    return Settings_Type(s, &sPage, &in, clip);
}

static void test_settings_typing(void) {
    Settings s;
    Settings_Init(&s);
    SettingsView v = View();
    Settings_BeginEdit(&s, &sProfiles, &sManifest, SET_ARCADE_NAME, true, false);
    CHECK(s.field.editing && strcmp(s.field.text, "TRG") == 0 && s.quick && s.tab == ST_PLAYER && s.field.limit == SET_NAME_LIMIT,
          "typing: begins on the arcade name, limited to 32");
    Settings_Layout(&s, &v, &sPage);
    CHECK(strcmp(Kit_Get(&sPage.list, WID_SET_NAME_EDIT)->label, "Save") == 0 && Kit_Get(&sPage.list, WID_SET_NAME_CANCEL),
          "typing: the row shows Save and Cancel");
    SettingsAction a = TypeFrame(&s, &v, Chars("lhuov "), NULL);
    CHECK(a.type == SA_NONE && strcmp(s.field.text, "TRGlhuov ") == 0, "typing: letters (even L H U O V) and Space only type");
    a = TypeFrame(&s, &v, Press(IK_TAB), NULL);
    CHECK(a.type == SA_NONE && s.field.editing, "typing: Tab does nothing");
    a = TypeFrame(&s, &v, PadPress(PB_A), NULL);
    CHECK(a.type == SA_NONE && s.board && strcmp(s.field.text, "TRGlhuov ") == 0,
          "typing: the pad's first press brings up the name board, and types nothing");
    a = TypeFrame(&s, &v, Press(IK_ENTER), NULL);
    CHECK(a.type == SA_SAVE_NAME && a.value == 0 && a.which == SET_ARCADE_NAME, "typing: Enter saves");
    Settings_EndEdit(&s, true);
    CHECK(!s.field.editing && !s.quick && strcmp(s.field.text, "TRGlhuov ") == 0, "typing: ended, text kept");

    Settings_BeginEdit(&s, &sProfiles, &sManifest, 10, false, true);
    CHECK(s.fieldFor == 10 && strcmp(s.field.text, "Digger") == 0 && s.names.top == 10 - SET_NAMES_VISIBLE + 1 && s.board && !s.quick,
          "typing: a game's name, scrolled into sight; begun from the pad, with the name board up");
    CHECK(strcmp(Settings_NameKey(&s, &sManifest), "Gemdive") == 0, "typing: kept under the game's name");
    TypeFrame(&s, &v, Chars("zz"), NULL);
    a = TypeFrame(&s, &v, Press(IK_ESCAPE), NULL);
    CHECK(a.type == SA_CANCEL_NAME && strcmp(s.field.text, "Digger") == 0, "typing: Esc puts it back");
    Settings_EndEdit(&s, false);

    /* The mouse while typing: only the field's buttons, Back and the X. */
    Settings_BeginEdit(&s, &sProfiles, &sManifest, SET_ARCADE_NAME, false, false);
    Settings_Layout(&s, &v, &sPage);
    KitList layer;
    Settings_EditLayer(&sPage, &s, &layer);
    CHECK(layer.count == 6 && layer.items[0].id == WID_SET_EDIT_VEIL && layer.items[layer.count - 1].id == WID_CLOSE &&
          Kit_Get(&layer, WID_SET_NAME_FIELD) && Kit_Get(&layer, WID_SET_NAME_EDIT) && Kit_Get(&layer, WID_SET_NAME_CANCEL) && Kit_Get(&layer, WID_BACK),
          "typing: veil, field, Save, Cancel, Back, the X on top");
    const KitItem *save = Kit_Get(&layer, WID_SET_NAME_EDIT);
    float sx = save->r.x + 10, sy = save->r.y + 10;
    Input down = Pointer(sx, sy, true);
    down.mousePressed[MB_LEFT] = down.mouseDown[MB_LEFT] = true;
    Input upIn = Pointer(sx, sy, false);
    upIn.mouseReleased[MB_LEFT] = true;
    TypeFrame(&s, &v, down, NULL);
    a = TypeFrame(&s, &v, upIn, NULL);
    CHECK(a.type == SA_SAVE_NAME && a.value == 0, "typing: a click on Save saves");
    const KitItem *tab = Kit_Get(&sPage.list, WID_SET_TABS);
    float tx = tab->r.x + 200, ty = tab->r.y + 10;
    Input vd = Pointer(tx, ty, true);
    vd.mousePressed[MB_LEFT] = vd.mouseDown[MB_LEFT] = true;
    Input vu = Pointer(tx, ty, false);
    vu.mouseReleased[MB_LEFT] = true;
    TypeFrame(&s, &v, vd, NULL);
    a = TypeFrame(&s, &v, vu, NULL);
    CHECK(a.type == SA_SAVE_NAME && a.value == 1 && s.tab == ST_PLAYER, "typing: a click elsewhere keeps the name (if it changed) and does nothing else");
    const KitItem *back = Kit_Get(&sPage.list, WID_BACK);
    Input bd = Pointer(back->r.x + 4, back->r.y + 4, true);
    bd.mousePressed[MB_LEFT] = bd.mouseDown[MB_LEFT] = true;
    Input bu = Pointer(back->r.x + 4, back->r.y + 4, false);
    bu.mouseReleased[MB_LEFT] = true;
    TypeFrame(&s, &v, bd, NULL);
    a = TypeFrame(&s, &v, bu, NULL);
    CHECK(a.type == SA_CANCEL_NAME, "typing: Back cancels the typing first");
    Input xd = Pointer(CLOSE_BTN_X + 8, CLOSE_BTN_Y + 8, true);
    xd.mousePressed[MB_LEFT] = xd.mouseDown[MB_LEFT] = true;
    Input xu = Pointer(CLOSE_BTN_X + 8, CLOSE_BTN_Y + 8, false);
    xu.mouseReleased[MB_LEFT] = true;
    TypeFrame(&s, &v, xd, NULL);
    CHECK(TypeFrame(&s, &v, xu, NULL).type == SA_QUIT, "typing: the X still leaves at once");

    /* A press on the text puts the cursor there; Ctrl+V pastes. */
    const KitItem *box = Kit_Get(&sPage.list, WID_SET_NAME_FIELD);
    Input click = Pointer(box->r.x + SET_FIELD_PAD + SET_CHAR_W + 4, box->r.y + 10, true);
    click.mousePressed[MB_LEFT] = click.mouseDown[MB_LEFT] = true;
    TypeFrame(&s, &v, click, NULL);
    CHECK(s.field.cursor == 1, "typing: a click on the text puts the cursor there");
    Input paste = Press(IK_V);
    paste.ctrl = true;
    TypeFrame(&s, &v, paste, "ace|");
    CHECK(strcmp(s.field.text, "TaceRG") == 0, "typing: Ctrl+V pastes at the cursor, '|' dropped");
    Input alt = Press(IK_ENTER);
    alt.alt = true;
    CHECK(TypeFrame(&s, &v, alt, NULL).type == SA_NONE && s.field.editing, "typing: Alt+Enter is fullscreen, not save");
    Settings_EndEdit(&s, false);
    CHECK(strcmp(s.field.text, "TRG") == 0, "typing: Cancel puts the old name back");

    /* A click on a long name at rest: Begin scrolls the field to the end
     * (the cursor's place), but the click was on the name drawn from its
     * start. */
    TextField_Begin(&s.field, "Montgomery-Abernathy", SET_NAME_LIMIT);
    TextField_KeepCursorVisible(&s.field, Settings_FieldChars(true));
    CHECK(s.field.view > 0, "typing: a long name scrolls to its end when typing begins");
    Settings_ClickIntoName(&s, SET_FIELD_X + SET_FIELD_PAD + 3 * SET_CHAR_W);
    CHECK(s.field.cursor == 3 && s.field.view == 0, "typing: a click on a long name puts the cursor on the letter clicked");
    Settings_ClickIntoName(&s, SET_FIELD_X + SET_FIELD_W - 4);
    CHECK(s.field.cursor > Settings_FieldChars(true) && s.field.cursor - s.field.view < Settings_FieldChars(true),
          "typing: a click near the box's end scrolls the field to keep the cursor in sight");
    TextField_End(&s.field);
}

/* A whole click through the typing layer: press, then let go, at (x, y). */
static SettingsAction ClickTyping(Settings *s, const SettingsView *v, float x, float y) {
    Input down = Pointer(x, y, true), up = Pointer(x, y, false);
    down.mousePressed[MB_LEFT] = down.mouseDown[MB_LEFT] = true;
    up.mouseReleased[MB_LEFT] = true;
    SettingsAction a = TypeFrame(s, v, down, NULL);
    SettingsAction b = TypeFrame(s, v, up, NULL);
    return b.type != SA_NONE ? b : a;
}

static void ClickCell(Settings *s, const SettingsView *v, int cell) {
    Settings_Layout(s, v, &sPage);
    KitRect r = NameBoard_CellRect(Settings_BoardPanel(&sPage, s), cell);
    ClickTyping(s, v, r.x + r.w / 2, r.y + r.h / 2);
}

/* The pad's name board: the d-pad moves, A types, X deletes, Y spaces,
 * Menu saves, B puts the old name back; the mouse clicks cells; the
 * keyboard still types. */
static void test_name_board(void) {
    Settings s;
    Settings_Init(&s);
    SettingsView v = View();
    Settings_BeginEdit(&s, &sProfiles, &sManifest, SET_ARCADE_NAME, false, true);
    CHECK(s.board && s.boardPos.cell == 0 && strcmp(s.field.text, "TRG") == 0, "board: up from the pad, on A, the name as it was");
    SettingsAction a = TypeFrame(&s, &v, PadPress(PB_A), NULL);
    CHECK(a.type == SA_NONE && strcmp(s.field.text, "TRGA") == 0, "board: A types the letter under the cursor");
    TypeFrame(&s, &v, PadPress(PB_RIGHT), NULL);
    TypeFrame(&s, &v, PadPress(PB_RIGHT), NULL);
    TypeFrame(&s, &v, PadPress(PB_A), NULL);
    CHECK(strcmp(s.field.text, "TRGAC") == 0, "board: the d-pad moves, A types");
    Input rep = Blank();
    rep.padRepeat[PB_DOWN] = rep.padDown[PB_DOWN] = true;
    rep.lastDevice = DEV_PAD;
    TypeFrame(&s, &v, rep, NULL);
    CHECK(s.boardPos.cell == NameBoard_CellFor('P'), "board: a held d-pad keeps moving");
    TypeFrame(&s, &v, PadPress(PB_X), NULL);
    CHECK(strcmp(s.field.text, "TRGA") == 0, "board: X deletes");
    TypeFrame(&s, &v, PadPress(PB_Y), NULL);
    CHECK(strcmp(s.field.text, "TRGA ") == 0, "board: Y types a space");
    Input held = Blank();
    held.padRepeat[PB_X] = held.padDown[PB_X] = true;
    TypeFrame(&s, &v, held, NULL);
    TypeFrame(&s, &v, held, NULL);
    CHECK(strcmp(s.field.text, "TRG") == 0, "board: X held keeps deleting");
    TypeFrame(&s, &v, Chars("x"), NULL);
    TypeFrame(&s, &v, Press(IK_LEFT), NULL);
    TypeFrame(&s, &v, Press(IK_BACKSPACE), NULL);
    CHECK(strcmp(s.field.text, "TRx") == 0 && s.boardPos.cell == NameBoard_CellFor('P'),
          "board: the keyboard still types, and its arrows move the text cursor, not the board's");
    TypeFrame(&s, &v, Press(IK_END), NULL);
    for (int i = 0; i < 40; i++) TypeFrame(&s, &v, PadPress(PB_A), NULL);
    CHECK(TextField_Length(&s.field) == SET_NAME_LIMIT, "board: it stops at 32, as the keyboard does");
    a = TypeFrame(&s, &v, PadPress(PB_MENU), NULL);
    CHECK(a.type == SA_SAVE_NAME && a.value == 0, "board: Menu saves");
    Settings_EndEdit(&s, true);
    CHECK(!s.board, "board: gone when typing ends");

    Settings_BeginEdit(&s, &sProfiles, &sManifest, SET_ARCADE_NAME, false, true);
    /* One focus at a time: with the pad on the board its cursor is it; Save
     * (Enter's button) shows the focus for the keyboard only. */
    CHECK(Settings_TypingLook(&s, KIT_LOOK_FOCUSED, DEV_PAD) == KIT_LOOK_NORMAL &&
              Settings_TypingLook(&s, KIT_LOOK_FOCUSED, DEV_KEYBOARD) == KIT_LOOK_FOCUSED &&
              Settings_TypingLook(&s, KIT_LOOK_PRESSED, DEV_PAD) == KIT_LOOK_PRESSED,
          "board: the pad's cursor on the board is the only focus; Save keeps it for the keyboard");
    TypeFrame(&s, &v, PadPress(PB_A), NULL);
    a = TypeFrame(&s, &v, PadPress(PB_B), NULL);
    CHECK(a.type == SA_CANCEL_NAME && strcmp(s.field.text, "TRG") == 0, "board: B puts the old name back");
    Settings_EndEdit(&s, false);
    CHECK(Settings_TypingLook(&s, KIT_LOOK_FOCUSED, DEV_PAD) == KIT_LOOK_FOCUSED, "board: no board, the pad's focus shows as ever");

    /* Done and Del are keys on the board too. */
    Settings_BeginEdit(&s, &sProfiles, &sManifest, SET_ARCADE_NAME, false, true);
    NameBoard_Set(&s.boardPos, NameBoard_Count() - 2);
    TypeFrame(&s, &v, PadPress(PB_A), NULL);
    CHECK(strcmp(s.field.text, "TR") == 0, "board: Del deletes");
    NameBoard_Move(&s.boardPos, KIT_RIGHT);
    a = TypeFrame(&s, &v, PadPress(PB_A), NULL);
    CHECK(a.type == SA_SAVE_NAME && strcmp(s.field.text, "TR") == 0, "board: Done saves");
    Settings_EndEdit(&s, true);

    /* The mouse: click letters, Del and Done; a near miss on the panel is no click away. */
    Settings_BeginEdit(&s, &sProfiles, &sManifest, SET_ARCADE_NAME, false, true);
    ClickCell(&s, &v, NameBoard_CellFor('Z'));
    ClickCell(&s, &v, NameBoard_CellFor('9'));
    CHECK(strcmp(s.field.text, "TRGZ9") == 0 && s.boardPos.cell == NameBoard_CellFor('9'), "board: a click types the letter, and the pad carries on from it");
    Settings_Layout(&s, &v, &sPage);
    KitRect panel = Settings_BoardPanel(&sPage, &s);
    a = ClickTyping(&s, &v, panel.x + 4, panel.y + 4);
    CHECK(a.type == SA_NONE && s.field.editing, "board: a click on the panel between keys keeps typing");
    ClickCell(&s, &v, NameBoard_Count() - 2);
    CHECK(strcmp(s.field.text, "TRGZ") == 0, "board: clicking Del deletes");
    Settings_Layout(&s, &v, &sPage);
    KitRect done = NameBoard_CellRect(Settings_BoardPanel(&sPage, &s), NameBoard_Count() - 1);
    a = ClickTyping(&s, &v, done.x + 8, done.y + 8);
    CHECK(a.type == SA_SAVE_NAME && a.value == 0, "board: clicking Done saves");
    Settings_EndEdit(&s, true);

    /* Typing begun from the keyboard or mouse: no board, until the pad is used. */
    Settings_BeginEdit(&s, &sProfiles, &sManifest, 10, false, false);
    Settings_Layout(&s, &v, &sPage);
    KitList layer;
    Settings_EditLayer(&sPage, &s, &layer);
    CHECK(!s.board && !Kit_Get(&layer, WID_SET_BOARD), "board: down while the keyboard types");
    a = TypeFrame(&s, &v, PadPress(PB_X), NULL);
    CHECK(a.type == SA_NONE && s.board && strcmp(s.field.text, "Digger") == 0 && s.boardPos.cell == 0,
          "board: the pad's first press brings it up and deletes nothing");
    Settings_Layout(&s, &v, &sPage);
    Settings_EditLayer(&sPage, &s, &layer);
    const KitItem *box = Kit_Get(&layer, WID_SET_GAME_FIELD + 10);
    CHECK(Kit_Get(&layer, WID_SET_BOARD) && Kit_Get(&layer, WID_SET_BOARD + NameBoard_Count() - 1) && Kit_Get(&layer, WID_SET_BOARD_PANEL) &&
              layer.items[layer.count - 1].id == WID_CLOSE && box && !Kit_Inside(Settings_BoardPanel(&sPage, &s), box->r.x + 4, box->r.y + 4),
          "board: its keys answer the mouse, the X stays on top, and the name stays in sight");
    Settings_EndEdit(&s, false);
    Settings_BeginEdit(&s, &sProfiles, &sManifest, SET_ARCADE_NAME, false, false);
    a = TypeFrame(&s, &v, PadPress(PB_B), NULL);
    CHECK(a.type == SA_CANCEL_NAME && !s.board, "board: B with the board down still puts the name back at once");
    Settings_EndEdit(&s, false);
}

static void test_settings_words(void) {
    char buf[64];
    Settings_Ago(0, 1000, buf, sizeof(buf));
    CHECK(strcmp(buf, "never") == 0, "words: never");
    Settings_Ago(1000, 1050, buf, sizeof(buf));
    CHECK(strcmp(buf, "just now") == 0, "words: just now");
    Settings_Ago(1000, 1000 + 600, buf, sizeof(buf));
    CHECK(strcmp(buf, "10 minutes ago") == 0, "words: minutes");
    Settings_Ago(1000, 1000 + 3 * 3600, buf, sizeof(buf));
    CHECK(strcmp(buf, "3 hours ago") == 0, "words: hours");
    Settings_Ago(1000, 1000 + 4 * 86400, buf, sizeof(buf));
    CHECK(strcmp(buf, "4 days ago") == 0, "words: days");
    Settings_Ago(5000, 1000, buf, sizeof(buf));
    CHECK(strcmp(buf, "just now") == 0, "words: a clock that went back is just now");
    Settings_ServerHost("https://abc.supabase.co/rest/v1?x=1", buf, sizeof(buf));
    CHECK(strcmp(buf, "abc.supabase.co") == 0, "words: the server's host");
    Settings_ServerHost("plain.example", buf, sizeof(buf));
    CHECK(strcmp(buf, "plain.example") == 0, "words: a host without a scheme");
    Settings_ServerHost(NULL, buf, sizeof(buf));
    CHECK(buf[0] == '\0', "words: no server");
    CHECK(strcmp(Settings_TabLabel(ST_PLAYER), "Player") == 0 && strcmp(Settings_TabLabel(ST_CONTROLS), "Controls") == 0 &&
          Settings_TabLabel(ST_COUNT)[0] == '\0', "words: the tabs");
}

/* Settings > Machines for admin (Phase 5): every machine's status under its
 * name, Add a machine on the heading's line, Edit and Remove on each row. */
static void test_settings_machines_admin(void) {
    Settings s;
    Settings_Init(&s);
    SettingsView v = View();
    bool installed[12];
    for (int i = 0; i < 12; i++) installed[i] = i != 10; /* Gemdive is not */
    v.installed = installed;
    v.installing = -1;
    s.tab = ST_MACHINES;
    Settings_Layout(&s, &v, &sPage);
    CHECK(!Kit_Get(&sPage.list, WID_SET_MACH_ADD) && !Kit_Get(&sPage.list, WID_SET_MACH_EDIT + 0) && !Kit_Get(&sPage.list, WID_SET_MACH_REMOVE + 0) &&
              Settings_RowOf(&sPage, WID_SET_MACH_FAV + 0)->controlCount == 4,
          "machines: a player has Favourite, Up, Down and Hide, and no Add, Edit or Remove");
    const SettingsRow *block = Settings_RowOf(&sPage, WID_SET_MACH_FAV + 0), *sky = Settings_RowOf(&sPage, WID_SET_MACH_FAV + 3);
    CHECK(block && strcmp(block->sub, "Installed") == 0 && sky && strcmp(sky->sub, "Installed") == 0, "machines: every machine says it is installed...");
    s.machines.top = 5;
    v.installing = 9;
    Settings_Layout(&s, &v, &sPage);
    const SettingsRow *gem = Settings_RowOf(&sPage, WID_SET_MACH_FAV + 10), *girder = Settings_RowOf(&sPage, WID_SET_MACH_FAV + 9),
                      *lane = Settings_RowOf(&sPage, WID_SET_MACH_FAV + 6);
    CHECK(gem && strcmp(gem->sub, "Not installed") == 0 && girder && strcmp(girder->sub, "Installing") == 0 && girder->subTone == NOTE_HOT &&
              lane && strcmp(lane->sub, "Installed, hidden") == 0,
          "machines: ...or not, or that it is installing, and when it is hidden");
    s.machines.top = 0;
    v.installing = -1;

    v.admin = true;
    Settings_Layout(&s, &v, &sPage);
    const KitItem *add = Kit_Get(&sPage.list, WID_SET_MACH_ADD);
    const SettingsRow *head = Settings_RowOf(&sPage, WID_SET_MACH_ADD);
    CHECK(add && strcmp(add->label, "Add a machine") == 0 && head && head->kind == SROW_HEADING && strcmp(head->label, "Your order") == 0 &&
              add->r.x + add->r.w == SET_ROW_X + SET_ROW_W && add->r.y + add->r.h <= SET_MACH_Y && add->r.y >= RowY0() + SET_ROW_H,
          "machines: admin's Add a machine at the right of the Your order line, clear of Row order and the list");
    bool rows = true;
    for (int k = 0; k < SET_MACH_VISIBLE; k++) {
        int i = sMine.index[k];
        const SettingsRow *r = Settings_RowOf(&sPage, WID_SET_MACH_FAV + i);
        static const int kBase[6] = {WID_SET_MACH_FAV, WID_SET_MACH_UP, WID_SET_MACH_DOWN, WID_SET_MACH_HIDE, WID_SET_MACH_EDIT, WID_SET_MACH_REMOVE};
        if (!r || r->controlCount != 6) { rows = false; continue; }
        for (int c = 0; c < 6; c++) {
            const KitItem *b = Kit_Get(&sPage.list, r->controls[c]);
            if (!b || b->id != kBase[c] + i || b->r.y < r->r.y || b->r.y + b->r.h > r->r.y + r->r.h || b->r.x + b->r.w > SET_ROW_X + SET_ROW_W) rows = false;
            if (c > 0 && Kit_Get(&sPage.list, r->controls[c - 1])->r.x + Kit_Get(&sPage.list, r->controls[c - 1])->r.w > b->r.x) rows = false;
        }
        if (SET_LABEL_X + r->labelPad + Kit_TextWidth(r->label, 16) > Kit_Get(&sPage.list, r->controls[0])->r.x - 8) rows = false;
    }
    CHECK(rows && PageSane(&sPage), "machines: six buttons a row, left to right, inside it, after a name that fits");
    s.machines.top = 5;
    Settings_Layout(&s, &v, &sPage);
    CHECK(strcmp(Settings_RowOf(&sPage, WID_SET_MACH_FAV + 9)->label, "Girderclimb") == 0 && PageSane(&sPage), "machines: Girderclimb whole beside its buttons");
    s.machines.top = 0;
    Settings_Layout(&s, &v, &sPage);

    static const struct { int id; SettingsActionType type; int which; } kPress[] = {
        {WID_SET_MACH_EDIT + 4, SA_MACH_EDIT, 4}, {WID_SET_MACH_REMOVE + 11, SA_MACH_REMOVE, 11}, {WID_SET_MACH_ADD, SA_MACH_ADD, 0},
    };
    bool events = true;
    for (size_t i = 0; i < sizeof(kPress) / sizeof(kPress[0]); i++) {
        KitEvent pe = {KIT_EV_PRESS, kPress[i].id, 0};
        SettingsAction a = Settings_Event(&s, &pe);
        if (a.type != kPress[i].type || a.which != kPress[i].which) events = false;
    }
    CHECK(events, "machines: Edit and Remove for their machine, and Add");

    KitState k;
    Kit_Init(&k);
    k.focus = WID_SET_ROW_ORDER;
    Input dn = Press(IK_DOWN), right = Press(IK_RIGHT), up = Press(IK_UP);
    Settings_Navigate(&s, &k, &sPage, &v, &dn);
    bool keys = k.focus == WID_SET_MACH_ADD;
    Settings_Navigate(&s, &k, &sPage, &v, &dn);
    keys &= k.focus == WID_SET_MACH_FAV + 3;
    for (int i = 0; i < 5; i++) Settings_Navigate(&s, &k, &sPage, &v, &right);
    keys &= k.focus == WID_SET_MACH_REMOVE + 3;
    Settings_Navigate(&s, &k, &sPage, &v, &dn);
    keys &= k.focus == WID_SET_MACH_REMOVE + 8;
    Settings_Navigate(&s, &k, &sPage, &v, &up);
    Settings_Navigate(&s, &k, &sPage, &v, &up);
    keys &= k.focus == WID_SET_MACH_ADD;
    CHECK(keys, "machines: the keys reach Add (between Row order and the list) and along a row to Remove");
}

void test_settings_all(void) {
    test_settings_your_hall();
    test_settings_machines_admin();
    test_textfield_editing();
    test_textfield_limits_and_paste();
    test_textfield_finish();
    test_keymap();
    test_keymap_cards();
    test_settings_layout();
    test_settings_navigation();
    test_settings_events();
    test_settings_typing();
    test_name_board();
    test_settings_words();
}
