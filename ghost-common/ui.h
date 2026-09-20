#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include "raylib.h"
#include "ghostlink.h"
#include "howto.h"

/* The Ghost Arcade house style, in one place: the pixel font and its type
 * scale, the indigo palette, bevelled blocks, pixel particles, and the
 * screens every game has (menu, scores, settings, updates). A game supplies
 * its ONE light colour and its content; everything else comes from here, so
 * the games look like machines in the same arcade hall.
 *
 * Rules the helpers enforce by construction:
 *  - Press Start 2P is an 8x8 design, so text exists only at UI_T8/16/32/48.
 *  - Selection is a colour and a 4px bar, never a size change.
 *  - Volume is ten blocks; copy is sentence case. */

#define UI_T8 8
#define UI_T16 16
#define UI_T32 32
#define UI_T48 48

typedef struct {
    Color bg;      /* window */
    Color board;   /* playfield */
    Color panel;   /* selected-row / tab background */
    Color rule;    /* frames, dividers, empty pips */
    Color text;
    Color dim;
    Color faint;
    Color light;   /* THE game's colour: title, selection, accents */
    Color gold;    /* rank 1, player name, bonuses */
    Color danger;
} UiTheme;

/* The shared palette with `light` filled in. */
UiTheme Ui_DefaultTheme(Color light);

/* Loads the font from assetsDir/fonts. Call once after InitWindow(). */
void Ui_Init(const char *assetsDir, const UiTheme *theme, int windowWidth, int windowHeight);
void Ui_Shutdown(void);
const UiTheme *Ui_Theme(void);

void Ui_Text(const char *text, int x, int y, int size, Color color);
int Ui_Measure(const char *text, int size);
void Ui_TextCentered(const char *text, int size, int centerX, int y, Color color);
void Ui_TextRight(const char *text, int size, int rightX, int y, Color color);

Color Ui_Lerp(Color a, Color b, float t);
/* Filled block with a light top/left and dark bottom/right edge. */
void Ui_BevelRect(int x, int y, int w, int h, Color base);

/* Chunky pixel debris. Spawn anywhere; update+draw once per frame, inside
 * whatever camera the playfield is drawn with. */
void Ui_Burst(float cx, float cy, Color color, int count, float speed);
void Ui_UpdateParticles(float dt);
void Ui_ClearParticles(void);

/* Icon badge + version in the top-left corner of a menu. */
void Ui_CornerBadge(const char *iconPath, const char *versionLabel);

/* --- Whole screens. Each owns BeginDrawing/EndDrawing. --- */

typedef struct {
    const char *title;      /* "COILRUSH" */
    const char *tagline;
    const char *const *items;
    const char *const *blurbs; /* optional per-item 8px line under the item (NULL entries/array ok) */
    int itemCount;
    int selected;
    const char *statLine;   /* "Best 210    longest 16" */
    const char *username;
    const char *hints;      /* "Up/Down choose    Enter select" */
    const char *iconPath;
    const char *versionLabel;
    void (*backdrop)(float dt); /* optional animated background, drawn first */
} UiMenu;
void Ui_DrawMenu(const UiMenu *menu, float dt);

typedef enum { UI_ROW_KEY, UI_ROW_TOGGLE, UI_ROW_VOLUME, UI_ROW_DONE, UI_ROW_CHOICE } UiRowKind;
typedef struct {
    UiRowKind kind;
    const char *label;
    const char *keyName; /* UI_ROW_KEY; for UI_ROW_CHOICE, the current value shown as "< value >" */
    bool on;             /* UI_ROW_TOGGLE */
    int percent;         /* UI_ROW_VOLUME */
} UiSettingsRow;
void Ui_DrawSettings(const UiSettingsRow *rows, int count, int selected, bool awaitingKey);
/* The same list under any title, with an optional note line under it and your
 * own key hint: used for a game's play-setup screen. */
void Ui_DrawSettingsTitled(const char *title, const char *note, const char *hint,
                           const UiSettingsRow *rows, int count, int selected, bool awaitingKey);
/* Adds the arcade-wide rows (see prefs.h) before the last ("Done") row. */
void Ui_DrawSettingsPlus(const UiSettingsRow *rows, int count, int selected, bool awaitingKey);

/* boardLabel: what is being ranked ("< Classic >", "Marathon").
 * switchHint: the key-hint line for this game's scores screen. */
void Ui_DrawScores(const char *boardLabel, bool global, const GhostScore *entries, int count,
                   const char *username, const char *switchHint);

/* Scores v2: the same two boards, filtered to a time period. The tables carry
 * each run's date, so "This week" and "Today" are just filters over them. */
typedef enum { UI_PERIOD_ALL, UI_PERIOD_WEEK, UI_PERIOD_TODAY, UI_PERIOD_COUNT } UiScorePeriod;
int Ui_ScoresFilter(const GhostScore *in, int count, UiScorePeriod period, GhostScore *out, int maxOut);
void Ui_DrawScoresV2(const char *boardLabel, bool global, UiScorePeriod period, const GhostScore *entries, int count,
                     const char *username, const char *switchHint);

typedef struct {
    const char *version;
    const char *date;
    const char *const *notes; /* NULL-terminated */
} UiChangelogEntry;
/* *scrollPx is clamped in place, so callers just add/subtract a step. */
void Ui_DrawUpdates(const UiChangelogEntry *entries, int count, int *scrollPx);

/* Centred overlays for the playfield rectangle. */
void Ui_PauseOverlay(int x, int y, int w, int h);

/* The How to play card (see howto.h). Draws a whole frame. */
void Ui_DrawHowTo(const HowTo *h);

#endif
