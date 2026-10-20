#ifndef GAMEFORM_H
#define GAMEFORM_H

#include <stdbool.h>
#include <stddef.h>
#include "kit.h"
#include "textfield.h"
#include "manifest.h"
#include "machines.h"

/* Add a machine / Edit a machine: admin's form, all five catalog fields in
 * sight at once, without raylib. One field has the keys at a time (its text
 * is the TextField); the others show what they hold.
 *   - Enter or pad A goes to the next field, as the old one-field-at-a-time
 *     form did (an empty name or program is refused there), and from the
 *     last field to Save, where Enter saves;
 *   - Tab / Shift+Tab (pad RB / LB) and Up / Down go between the fields and
 *     the buttons, Left / Right between Save and Cancel; a click puts the
 *     cursor where it lands;
 *   - while a field has the keys only the mouse reaches the buttons, so
 *     Space and Enter never press one by accident;
 *   - Save checks it all: a name and a program are needed, no '|' (it
 *     separates games.txt's fields), and no two machines with one name
 *     (playtime and names are kept by name).
 * A rename shows what stays under the old name (machines.h) and a
 * "Carry them over" switch, on at first. */

#define FORM_ON_BUTTONS (-1) /* the keys are on Save, Cancel or the switch */

/* Geometry, logical 960x640. */
#define FORM_X 80
#define FORM_W 800
#define FORM_Y 112      /* the first field's prompt */
#define FORM_PITCH 60
#define FORM_BOX_DY 14  /* the box under its prompt */
#define FORM_BOX_H 32
#define FORM_PAD 12
#define FORM_CHAR_W 18  /* one 16 px character and its gap */
#define FORM_STAYS_Y 410
#define FORM_CARRY_Y 444
#define FORM_MOVES_Y 484
#define FORM_ERROR_Y 500
#define FORM_BUTTONS_Y 516
#define FORM_BUTTON_W 176
#define FORM_BUTTON_H 40
#define FORM_TOGGLE_X 440

typedef struct {
    int editing;  /* the catalog index being edited, or -1 when adding */
    char text[MANIFEST_FIELD_COUNT][MANIFEST_FIELD_LEN];
    char was[MANIFEST_FIELD_COUNT][MANIFEST_FIELD_LEN]; /* as the form opened */
    int active;   /* the field with the keys, or FORM_ON_BUTTONS */
    TextField field;
    bool carry;   /* a rename carries playtime, the game's name and row settings over */
    char error[96];
    int errorField; /* the field the error is about, or -1 */
} GameForm;

typedef enum { GF_NONE, GF_SAVE, GF_CANCEL, GF_QUIT } GameFormResult;

void GameForm_OpenAdd(GameForm *f);
void GameForm_OpenEdit(GameForm *f, const GameEntry *e, int index);
/* "Name", "Path to the program", ... */
const char *GameForm_Prompt(int field);
/* How many characters a box shows. */
int GameForm_Chars(void);
/* How a field at rest shows a text too long for its box: a path its end
 * after "..." (*lead is 3, the cells the dots take), anything else its
 * start, then "...". Returns the first character drawn (*view). */
bool GameForm_RestCut(int field, const char *text, int *view, int *lead);
KitRect GameForm_BoxRect(int field);
/* A field's text now (the one being typed is the TextField's). */
const char *GameForm_Text(const GameForm *f, int field);
bool GameForm_Changed(const GameForm *f);
/* The values as they will be saved: spaces trimmed from both ends. */
void GameForm_Values(const GameForm *f, char out[MANIFEST_FIELD_COUNT][MANIFEST_FIELD_LEN]);
/* What a save would leave under the old name and program (nothing to say
 * when adding). */
void GameForm_WhatStays(const GameForm *f, const Manifest *m, const PlayStats *stats, const Profiles *p, const LauncherIni *ini,
                        CarryOver *c);
/* False with the field and the reason when something must change first. */
bool GameForm_Validate(const GameForm *f, const Manifest *m, int *field, char *msg, size_t size);

/* Back, the five boxes (mouse only: the keys move between fields themselves),
 * the switch when a rename has something to carry, Save, Cancel and the X. */
void GameForm_Layout(const GameForm *f, const CarryOver *c, KitList *l);
/* One frame. The X quits; Back, Esc, pad B and Cancel ask to leave (the
 * caller asks first when something changed); Save, or Enter on it, returns
 * GF_SAVE once the form is valid. */
GameFormResult GameForm_Update(GameForm *f, KitState *k, const KitList *l, const Input *in, const char *clipboard,
                               const Manifest *m);
/* While a field has the keys, the buttons show no focus of their own. */
KitLook GameForm_Look(const GameForm *f, KitLook look);

#endif
