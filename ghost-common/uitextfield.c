#include "uitextfield.h"
#include "uikeymap.h"
#include <stdio.h>
#include <string.h>

bool TextField_Allowed(int c) { return c >= 32 && c < 127 && c != '|'; }

int TextField_Length(const TextField *f) { return (int)strlen(f->text); }

void TextField_Begin(TextField *f, const char *text, int limit) {
    snprintf(f->text, sizeof(f->text), "%s", text ? text : "");
    snprintf(f->before, sizeof(f->before), "%s", f->text);
    f->limit = limit > 0 && limit < TEXTFIELD_CAP ? limit : TEXTFIELD_CAP - 1;
    f->cursor = TextField_Length(f);
    f->view = 0;
    f->editing = true;
}

void TextField_End(TextField *f) { f->editing = false; }

bool TextField_Changed(const TextField *f) { return strcmp(f->text, f->before) != 0; }

int TextField_Insert(TextField *f, const char *s) {
    int len = TextField_Length(f), added = 0;
    for (const char *p = s ? s : ""; *p && len < f->limit && len < TEXTFIELD_CAP - 1; p++) {
        if (!TextField_Allowed((unsigned char)*p)) continue;
        memmove(f->text + f->cursor + 1, f->text + f->cursor, (size_t)(len - f->cursor + 1));
        f->text[f->cursor++] = *p;
        len++;
        added++;
    }
    return added;
}

static bool EraseAt(TextField *f, int at) {
    int len = TextField_Length(f);
    if (at < 0 || at >= len) return false;
    memmove(f->text + at, f->text + at + 1, (size_t)(len - at));
    return true;
}

bool TextField_Backspace(TextField *f) {
    if (f->cursor <= 0 || !EraseAt(f, f->cursor - 1)) return false;
    f->cursor--;
    return true;
}

bool TextField_WantsPaste(const Input *in) { return Keymap_Pressed(KA_TEXT_PASTE, in); }

/* A Ctrl chord is a command, never a letter. AltGr (seen as Alt) types
 * characters on many layouts, so Alt alone does not stop them. */
static int TypeChars(TextField *f, const Input *in) {
    if (in->ctrl && !in->alt) return 0;
    char typed[INPUT_MAX_CHARS + 1];
    int n = 0;
    for (int i = 0; i < in->charCount && n < INPUT_MAX_CHARS; i++) {
        int c = in->chars[i];
        if (c > 0 && c < 128) typed[n++] = (char)c;
    }
    typed[n] = '\0';
    return TextField_Insert(f, typed);
}

static bool MoveCursor(TextField *f, const Input *in) {
    int was = f->cursor, len = TextField_Length(f);
    if (Keymap_Pressed(KA_TEXT_LEFT, in) && f->cursor > 0) f->cursor--;
    if (Keymap_Pressed(KA_TEXT_RIGHT, in) && f->cursor < len) f->cursor++;
    if (Keymap_Pressed(KA_TEXT_HOME, in)) f->cursor = 0;
    if (Keymap_Pressed(KA_TEXT_END, in)) f->cursor = len;
    return f->cursor != was;
}

static bool Erase(TextField *f, const Input *in) {
    bool changed = false;
    if (Keymap_Pressed(KA_TEXT_BACKSPACE, in) && TextField_Backspace(f)) changed = true;
    if (Keymap_Pressed(KA_TEXT_DELETE, in) && EraseAt(f, f->cursor)) changed = true;
    return changed;
}

TextFieldResult TextField_Update(TextField *f, const Input *in, const char *clipboard) {
    if (!f->editing) return TF_NONE;
    if (Keymap_Pressed(KA_TEXT_CANCEL, in)) {
        snprintf(f->text, sizeof(f->text), "%s", f->before);
        f->cursor = TextField_Length(f);
        f->editing = false;
        return TF_CANCEL;
    }
    if (Keymap_Pressed(KA_TEXT_SAVE, in)) {
        f->editing = false;
        return TF_SUBMIT;
    }
    bool changed = false;
    if (TextField_WantsPaste(in)) changed = TextField_Insert(f, clipboard) > 0;
    else changed = TypeChars(f, in) > 0;
    if (Erase(f, in)) changed = true;
    if (MoveCursor(f, in)) changed = true;
    return changed ? TF_CHANGED : TF_NONE;
}

void TextField_ClickAt(TextField *f, float x, float textX, int charW) {
    if (charW <= 0) return;
    int gap = f->view + (int)((x - textX + (float)charW * 0.5f) / (float)charW);
    int len = TextField_Length(f);
    f->cursor = gap < 0 ? 0 : (gap > len ? len : gap);
}

void TextField_KeepCursorVisible(TextField *f, int visible) {
    if (visible < 1) visible = 1;
    int len = TextField_Length(f);
    if (f->cursor < f->view) f->view = f->cursor;
    if (f->cursor > f->view + visible - 1) f->view = f->cursor - visible + 1;
    /* Shortened text never leaves empty room on the right while some is hidden on the left. */
    if (f->view > 0 && len - f->view < visible - 1) f->view = len - visible + 1 > 0 ? len - visible + 1 : 0;
    if (f->view < 0) f->view = 0;
}
