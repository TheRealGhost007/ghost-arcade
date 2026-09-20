#ifndef RENDER_H
#define RENDER_H

#include <stdbool.h>
#include "manifest.h"
#include "stats.h"
#include "runstats.h"
#include "../sync/updatecheck.h"
#include "profile.h"
#include "scores.h"
#include "launch.h"

/* Wide enough for a row of five arcade cabinets (the selected one full
 * size in the middle, two smaller ones either side). */
#define WINDOW_WIDTH 960
#define WINDOW_HEIGHT 640

/* Shared geometry for the top-right close ("X") button, so main.c can hit-
 * test mouse clicks against exactly what render.c draws. */
#define CLOSE_BTN_SIZE 24
#define CLOSE_BTN_X (WINDOW_WIDTH - CLOSE_BTN_SIZE - 16)
#define CLOSE_BTN_Y 16

void Render_Init(const char *assetsDir);
void Render_Shutdown(void);

/* Icons that failed to load are remembered so a missing file isn't retried
 * every frame. Call this when files may have appeared (an install just
 * finished) to give them another chance. */
void Render_ForgetMissingIcons(void);

/* Everything the instruction card under the cabinets says about the
 * selected game. main.c refreshes it when the selection changes (and on a
 * slow timer, so a background sync shows up) -- never per frame. */
typedef struct {
    ScoreBest localBest;
    ScoreBest worldBest;
    double playSeconds;
    const char *playerName; /* name this game will play under; "" if none set */
    bool nameIsOverride;    /* playerName is a per-game override, not the arcade name */
    bool installed;         /* the catalog's exec exists */
    bool canInstall;        /* not installed, but the catalog has a source folder */
    bool supportsDaily;     /* installed, and plays a daily challenge (D) */
    InstallStatus install;  /* state of an install OF THIS GAME (IDLE if none) */
} GameCard;

/* The arcade floor: one cabinet per catalog entry, the selected one lit.
 * installed[i] says whether game i's executable exists; machines that
 * aren't installed stay dark with a paper sign on the screen. card may be
 * NULL when the catalog is empty. arcadeName is the player's
 * arcade-wide name ("" if unset). errorMsg (may be NULL/empty) replaces the
 * play prompt briefly, e.g. after a failed launch. dt drives the scroll and
 * marquee animation. coinT: see COIN_ANIM_SECONDS below. */
void Render_List(const Manifest *m, int selected, const bool *installed, const GameCard *card,
                 const char *arcadeName, bool adminUnlocked, const char *errorMsg, float dt, float coinT);

/* The coin-drop that plays between pressing Enter and the game starting.
 * coinT runs 0..1 over COIN_ANIM_SECONDS (pass a negative value when no
 * coin is in flight); the coin reaches the slot at COIN_DING_AT, which is
 * when main.c plays the chime. */
#define COIN_ANIM_SECONDS 0.95f
#define COIN_DING_AT 0.58f

/* The "add/edit game" form, one catalog field per step. title is e.g.
 * "Add a game". step: 0=name,1=exec,2=icon,3=desc,4=install-from,
 * MANIFEST_FIELD_COUNT=confirm. buffers holds the in-progress text.
 * errorMsg (may be NULL/empty) shows inline validation feedback. */
void Render_GameForm(const char *title, int step, const char buffers[MANIFEST_FIELD_COUNT][MANIFEST_FIELD_LEN], const char *errorMsg);

/* Local playtime board: total time the launcher has been open, and
 * per-game cumulative time, most-played first. */
void Render_Stats(const PlayStats *stats, const Manifest *m, const RunSummary *sums);

/* Name entry. gameName == NULL edits the arcade-wide name; otherwise the
 * override for that one game. buffer is the in-progress text. */
void Render_Profile(const char *gameName, const char *buffer);
/* The trophy room: one game's achievements at a time (selected = catalog index). */
void Render_Achievements(const Manifest *m, int selected);
/* The opt-in question for the online leaderboard. choice is the highlighted
 * answer (true = share); firstTime changes the wording of the footer. */
void Render_OnlineAsk(bool choice, bool firstTime);

/* Update notice. The list screen shows a pill while `behind` > 0 (0 hides it). */
void Render_SetUpdateNotice(int behind);
/* The details screen: this build, the newest on GitHub, and how to update. */
void Render_Update(const UpdateState *u, const char *sourceDir);

#endif
