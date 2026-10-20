#ifndef SCREENS_H
#define SCREENS_H

#include "app.h"
#include "raylib.h"
#include "uikitdraw.h"
#include "uikeymap.h"
#include "winscale.h"

/* Blockfall's screens, one file each, on the Back stack (app.h). Each has
 *   Enter(app, back): it comes on top (back = it was gone back to);
 *   Update(app, input, dt): one frame of input (main.c's reader);
 *   Draw(app, dt): exactly one frame.
 * Every screen is built from the button kit (uikit.h): the main menu, Play
 * setup, Scores, Settings with its tabs, How to play with its moving
 * examples, and Updates. The run itself, with its pause menu and results
 * card, is scr_play.c. */

/* Blockfall's sound effects, in main.c's table order. */
enum { SFX_MOVE, SFX_ROTATE, SFX_HARD_DROP, SFX_CLEAR, SFX_QUAD, SFX_LEVEL_UP, SFX_GAME_OVER, SFX_COUNT };

void ScrMenu_Enter(App *a, bool back);
void ScrMenu_Update(App *a, const Input *in, float dt);
void ScrMenu_Draw(App *a, float dt);

void ScrSetup_Enter(App *a, bool back);
void ScrSetup_Update(App *a, const Input *in, float dt);
void ScrSetup_Draw(App *a, float dt);

void ScrPlay_Init(App *a);     /* once at launch: the game exists before any run */
void ScrPlay_Enter(App *a, bool back);
void ScrPlay_Update(App *a, const Input *in, float dt);
void ScrPlay_Draw(App *a, float dt);
void ScrPlay_Shutdown(App *a); /* a run still going when the game closes counts */
/* The music for this frame: the menus' tune, or play, pause, silence. */
int ScrPlay_MusicMode(const App *a);

void ScrScores_Enter(App *a, bool back);
void ScrScores_Update(App *a, const Input *in, float dt);
void ScrScores_Draw(App *a, float dt);
/* Opens Scores on a mode and board (Play setup's "Scores for this board"). */
void ScrScores_OpenFor(App *a, GameMode mode, int w, int h);
/* From the main menu: the tab looked at last, on the board last set up. */
void ScrScores_OpenFromMenu(App *a);
/* From the results card: the run's board, its new row blinking. */
void ScrScores_OpenForRun(App *a, GameMode mode, int w, int h, long value, const char *date);

void ScrSettings_Enter(App *a, bool back);
void ScrSettings_Update(App *a, const Input *in, float dt);
void ScrSettings_Draw(App *a, float dt);

void ScrHowTo_Enter(App *a, bool back);
void ScrHowTo_Update(App *a, const Input *in, float dt);
void ScrHowTo_Draw(App *a, float dt);
/* Opens How to play on its pages (from the menu or the pause menu); the
 * first launch's Basics-only page is main.c's (a->howto.firstLaunch). */
void ScrHowTo_Open(App *a);

void ScrUpdates_Enter(App *a, bool back);
void ScrUpdates_Update(App *a, const Input *in, float dt);
void ScrUpdates_Draw(App *a, float dt);

/* The kit screens' controls as they are this frame (for their drawing,
 * the tests and the screenshot harness). */
void ScrMenu_Layout(const App *a, KitList *l);
void ScrSetup_Layout(const App *a, KitList *l);
void ScrUpdates_Layout(const App *a, KitList *l);
void ScrScores_Layout(const App *a, KitList *l);
void ScrSettings_Layout(const App *a, KitList *l);
void ScrHowTo_Layout(const App *a, KitList *l);
/* The play screen's controls: the pause menu's page, its "are you sure?"
 * box, or the results card's buttons; false when none is up. */
bool ScrPlay_Kit(const App *a, KitList *l, const KitState **state);
/* The results card's "again" chip: your Restart key, or the pad's bound
 * Restart button once the pad is in use. False (out empty) when the card
 * shows no chip. */
bool ScrPlay_AgainCap(const App *a, char *out, size_t n);
/* The screen on top's controls and their state; false for a screen that is
 * not built from the kit. */
bool Scr_Kit(App *a, KitList *l, const KitState **state);

/* ------------------------------------------------- shared by the screens */

/* Menu sounds, all from the shared sound code: a soft, slightly varied tick
 * on each move, then confirm and back. */
void Scr_SoundMove(void);
void Scr_SoundConfirm(void);
void Scr_SoundBack(void);

/* The screen on top slides into place (App_SlideOffset): what is drawn
 * between Begin and End moves with it. */
void Scr_BeginSlide(const App *a);
void Scr_EndSlide(void);
/* Ends a kit screen's frame: the block wipe over it while one is on (going
 * into or out of a run), then Win_EndFrame. */
void Scr_EndFrame(const App *a);

/* A screen's title: 32 px, with the 4 px light rule under it. */
void Scr_Title(const char *title, int x, int y);
/* "Playing as NAME", its right edge at rightX. */
void Scr_PlayingAs(const char *username, int rightX, int y);
/* The [<- Back] button at the top left: the mouse's way back (keys and the
 * pad have Esc and B). */
#define SCR_BACK_RECT ((KitRect){20, 14, 80, 24})
KitItem *Scr_AddBack(KitList *l, int id);
/* The view of a list as it is drawn this frame. */
KitView Scr_View(const App *a, const KitList *l, const KitState *s, const KitRect *plate);
/* The pointer is a hand over something that can be pressed. */
void Scr_Cursor(bool hand);

/* Word wrap for 8 or 16 px text (the font is monospaced). Returns lines. */
#define SCR_WRAP_LEN 160
int Scr_Wrap(const char *text, int size, int maxW, char lines[][SCR_WRAP_LEN], int maxLines);

#endif
