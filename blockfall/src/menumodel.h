#ifndef MENUMODEL_H
#define MENUMODEL_H

#include <stdbool.h>
#include <stddef.h>
#include "game.h"
#include "persist.h"
#include "runrecord.h"
#include "controls.h"
#include "stats.h"

/* What the menus decide, kept apart from their drawing so it can be tested
 * without a window: the title's letter drop, the screens' slide, a stepper's
 * hold-to-repeat that speeds up, the play setup's board and size rules, and
 * the words the menus show. No raylib. */

/* ------------------------------------------------------------ numbers */

/* "74 372": thousands set apart, as the menus show points. */
void Menu_Group(long value, char *out, size_t size);
/* A best in its unit: "74 372", or a Sprint time "1:12.40"; 0 gives `none`. */
void Menu_BestText(RecordUnit unit, long value, const char *none, char *out, size_t size);
/* A Stats record tile's words (3.3.0; two lines since 3.6.1): the value as
 * drawn ("2.34", "6", "-" for none) and the lines under it. Under All
 * modes (`all`, with the mode that set it in `who`) the mode, then the day
 * on its own line ("Marathon" / "2026-10-18"); on a mode's row the day
 * alone and line2 empty; with no record "none yet" (or "no run yet" when
 * the row has no run at all). kind: 0 pieces per second, 1 combo, 2 quads. */
void Menu_RecordWords(const StatRecord *r, int kind, int who, bool all, bool anyRun, char *value, size_t vn, char *line1, size_t n1, char *line2, size_t n2);

/* -------------------------------------------------------- the title drop */

/* On launch the nine letters of BLOCKFALL drop in one by one, 0.04 s apart:
 * a fall of 0.22 s from above the screen, then a 2 px bounce (0.6 s in all).
 * Any key skips it; it never delays a press. */
#define TITLE_LETTERS 9
#define TITLE_DROP_GAP 0.04f
#define TITLE_DROP_FALL 0.22f
#define TITLE_DROP_BOUNCE 0.10f
#define TITLE_DROP_FROM 160 /* px above the letter's place: off the top of the screen */
/* The letter's height above its place at time t (whole pixels; 0 = there). */
int Title_LetterLift(int letter, float t);
/* The letter has landed by time t (its thud plays as it does). */
bool Title_LetterLanded(int letter, float t);
float Title_DropSeconds(void);

/* ------------------------------------------------------ the screen slide */

/* A screen slides in 16 px over 0.14 s: from the right going forward
 * (dir 1), from the left going back (dir -1). Whole pixels, so pixel text
 * never blurs; 0 once it is there. Text moves; it never fades. */
#define SLIDE_SECONDS 0.14f
#define SLIDE_PX 16
int Slide_Offset(float t, int dir);

/* ------------------------------------------------ a stepper held down */

/* One step on the press, then after 0.4 s ten a second, and after a whole
 * second 25 a second, so 12 to 36 is no longer 24 presses. */
#define HOLD_DELAY 0.4f
#define HOLD_SLOW 0.1f
#define HOLD_FAST_AFTER 1.0f
#define HOLD_FAST 0.04f
typedef struct {
    float held, next;
    bool armed;
} HoldStep;
/* The steps to take this frame (0, 1, or a few after a long frame, capped). */
int HoldStep_Update(HoldStep *h, bool pressed, bool down, float dt);

/* A box that changes size over a moment (Play setup's preview reshapes in
 * 0.15 s); with motion off, or the first time, it is there at once. */
typedef struct {
    float fromW, fromH, toW, toH, w, h, t;
    bool placed;
} SizeTween;
#define PREVIEW_RESHAPE_SECONDS 0.15f
void SizeTween_Step(SizeTween *s, float w, float h, float dt, float seconds, bool motion);

/* --------------------------------------------------------- play setup */

/* The modes in Play setup's list: a word beside each, and what it is and
 * how it ends. */
const char *Setup_ModeNote(GameMode mode);
const char *Setup_ModeAbout(GameMode mode);

/* The board the setup describes: a preset's size, or Custom's own. */
void Setup_Size(const Settings *s, int *w, int *h);
GameConfig Setup_Config(const Settings *s);
/* "Classic 10x20", "Custom 12x30". */
void Setup_BoardLabel(const Settings *s, char *out, size_t size);
/* A board by its size alone: a preset's name when it is one ("Wide 14x20"),
 * else "Custom 12x30" (what a run and the Scores browser call it). */
void Menu_BoardName(int w, int h, char *out, size_t size);
/* "Marathon on Classic 10x20": Start game's second line on the menu. */
void Setup_Summary(const Settings *s, char *out, size_t size);
/* The board picker: the next or previous preset, Custom last, round the
 * ends. Custom comes back with the size it had. */
void Setup_StepBoard(Settings *s, int dir);
/* Width or Height changed: a size that matches a preset is that preset,
 * anything else is Custom. Clamped to 6..24 x 12..36. True if it changed. */
bool Setup_SetSize(Settings *s, int w, int h);
/* A hand-edited config can hold anything: back inside the tables. */
void Setup_Sanitize(Settings *s);

/* "In play: 30 px cells," / "panel at the side" (or "panel across the
 * top" on a flat board, where the strip gives bigger cells). */
void Setup_InPlay(int w, int h, char *line1, size_t size1, char *line2, size_t size2);

/* The preview: the board at its true shape in a box. */
typedef struct {
    int cell;      /* px per cell */
    int pw, ph;    /* the well in px */
} PreviewFit;
PreviewFit Setup_PreviewFit(int w, int h, int boxW, int boxH);
/* The preview's rubble: a few rows at the bottom with gaps, the same for a
 * given size every time. y = 0 is the bottom row. */
bool Setup_PreviewRubble(int w, int h, int x, int y);
/* The gap column of garbage row y in Dig's preview (3.4.0): the same for a
 * size every time, and never the column of the row under it. */
int Setup_PreviewGap(int w, int h, int y);

/* ------------------------------------------------- the Practice room */

/* The Practice room's options (3.5.0, REWORK_PLAN.md "Practice room"), a
 * screen of its own off Play setup, kept in config.ini: its rows, their
 * words and what a change does, raylib-free; scr_practice.c draws them.
 * A practice run is a Marathon on the board Play setup has, with these
 * rules bent, and nothing it does is recorded. */
typedef enum { PRO_LEVEL, PRO_GRAVITY, PRO_LOCK, PRO_HOLD, PRO_GARBAGE, PRO_HINTS, PRO_COUNT } PracticeOption;
typedef enum { PRO_KIND_NUMBER, PRO_KIND_TOGGLE, PRO_KIND_CHOICE } PracticeOptionKind;
/* "Start level", "Gravity", "Lock delay", "Hold", "Starting garbage", "Finesse hints". */
const char *Practice_OptionLabel(PracticeOption o);
/* The sentence under the rows while the option is chosen. */
const char *Practice_OptionHelp(PracticeOption o);
PracticeOptionKind Practice_OptionKind(PracticeOption o);
/* The control's value (a number, 0/1 for a toggle, the index of a choice)
 * and its range. */
int Practice_OptionValue(const Settings *s, PracticeOption o);
void Practice_OptionRange(PracticeOption o, int *min, int *max);
/* A choice's words by index ("Once", "Infinite"); NULL past them. */
const char *Practice_ChoiceWord(PracticeOption o, int index);
/* The value as the row shows it: "1", "On", "Infinite", "4 rows", "none". */
void Practice_OptionWords(const Settings *s, PracticeOption o, char *out, size_t size);
/* Sets an option from its control (clamped to its range). True if it changed. */
bool Practice_SetOption(Settings *s, PracticeOption o, int value);
/* A hand-edited config.ini: back inside the ranges. */
void Practice_Sanitize(Settings *s);
/* The rules a practice run plays by, from the options. */
PracticeRules Practice_Rules(const Settings *s);
/* The config a practice run plays on: a Marathon on the board you set up,
 * whatever mode Play setup had (Game_InitPractice takes it with the rules). */
GameConfig Practice_Config(const Settings *s);
/* "Practice on Classic 10x20". */
void Practice_Summary(const Settings *s, char *out, size_t size);
/* The undo line: "U or RB takes the last piece back, up to 50 times." (your
 * keys; "Undo has no key or button: set one in Settings > Controls." when
 * it has none). */
void Practice_UndoWords(const ControlWords *w, char *out, size_t size);

/* ----------------------------------------------------------- the menu */

/* Esc or pad B on the main menu: the first press moves the bar to Quit, a
 * second press (on Quit) quits. */
typedef enum { MENU_BACK_TO_QUIT, MENU_BACK_QUIT } MenuBack;
MenuBack Menu_OnBack(bool onQuit);

#endif
