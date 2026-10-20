#ifndef TEXTFIELD_H
#define TEXTFIELD_H

#include <stdbool.h>
#include "input.h"

/* One line of text being typed: a name, or a field of the admin game form.
 *   - a cursor, moved with Left/Right/Home/End;
 *   - Backspace and Delete, both repeating while held;
 *   - Ctrl+V pastes (the caller fetches the clipboard only on that key);
 *   - only printable ASCII goes in, and never '|', which separates the
 *     fields of games.txt and profiles.txt;
 *   - new text stops at `limit` characters, but a longer text it was given
 *     is kept whole (and can be shortened);
 *   - Enter saves, Esc puts back what was there before.
 * The keys are keymap.h's KA_TEXT_*. No raylib. */

#define TEXTFIELD_CAP 256 /* the longest text it holds, plus the terminator */

typedef enum {
    TF_NONE,    /* nothing happened */
    TF_CHANGED, /* the text or the cursor moved */
    TF_SUBMIT,  /* Enter: keep it */
    TF_CANCEL,  /* Esc: the old text is back */
} TextFieldResult;

typedef struct {
    char text[TEXTFIELD_CAP];
    char before[TEXTFIELD_CAP]; /* what Esc puts back */
    int cursor;                 /* 0..length: the gap the next letter goes into */
    int limit;                  /* new text stops at this many characters */
    int view;                   /* the first character drawn (long text scrolls) */
    bool editing;
    double steadySince;         /* the cursor shows solid from here, then blinks (the caller's clock) */
} TextField;

/* Typeable: printable ASCII except '|'. */
bool TextField_Allowed(int c);

/* Starts editing text (cut to TEXTFIELD_CAP - 1), the cursor at its end. */
void TextField_Begin(TextField *f, const char *text, int limit);
/* Stops editing; the text stays as it is. */
void TextField_End(TextField *f);
int TextField_Length(const TextField *f);
/* Text changed since Begin. */
bool TextField_Changed(const TextField *f);

/* Inserts what is allowed of s at the cursor, up to the limit; returns how
 * many characters went in. */
int TextField_Insert(TextField *f, const char *s);
/* Deletes the character before the cursor (the name board's Del); false
 * at the start. */
bool TextField_Backspace(TextField *f);
/* Ctrl+V this frame: the caller hands the clipboard to TextField_Update. */
bool TextField_WantsPaste(const Input *in);
/* One frame of typing. clipboard is read only when pasting (NULL = empty,
 * which a Wayland clipboard can be: nothing happens). */
TextFieldResult TextField_Update(TextField *f, const Input *in, const char *clipboard);

/* The cursor goes to the gap nearest x, for a click on the text drawn from
 * textX with `charW` pixels per character (from character f->view). */
void TextField_ClickAt(TextField *f, float x, float textX, int charW);
/* Scrolls the view so the cursor is inside a box `visible` characters wide. */
void TextField_KeepCursorVisible(TextField *f, int visible);

#endif
