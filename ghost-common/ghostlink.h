#ifndef GHOSTLINK_H
#define GHOSTLINK_H

#include <stdbool.h>
#include <stddef.h>

/* Ghost Arcade data contract: the small set of plain-text files games share
 * with Ghost Launcher under $XDG_DATA_HOME/ghost-launcher/ (see ROADMAP.md
 * in the ghost-launcher project).
 *
 *   profiles.txt               launcher writes, games read   Game Name|username
 *   scores/<slug>.txt          games write, anyone reads     mode|username|score|date
 *   scores/global/<slug>.txt   ghost-sync writes, games read (same row format)
 *
 * Games never touch the network: the online leaderboard is handled entirely
 * by the separate `ghost-sync` helper, which uploads the local score files
 * and leaves the downloaded boards in scores/global/.
 *
 * No raylib dependency, and everything degrades quietly when the launcher
 * has never been installed -- the game must run fine standalone. */

#define GHOSTLINK_NAME_LEN 64
#define GHOSTLINK_MODE_LEN 24
#define GHOSTLINK_DATE_LEN 12
#define GHOSTLINK_TOP_N 10       /* entries kept per mode */
#define GHOSTLINK_MAX_ENTRIES 512 /* total rows read from one score file */
#define GHOSTLINK_DAILY_KEEP_DAYS 14 /* daily-YYYYMMDD boards older than this are dropped when a score is recorded */
#define GHOSTLINK_DEFAULT_NAME "PLAYER"

typedef struct {
    char mode[GHOSTLINK_MODE_LEN];
    char username[GHOSTLINK_NAME_LEN];
    long score;
    char date[GHOSTLINK_DATE_LEN]; /* YYYY-MM-DD */
} GhostScore;

/* The launcher's reserved profile key for the player's arcade-wide name. */
#define GHOSTLINK_ARCADE_KEY "__DEFAULT__"

/* Looks up the name to play under, as set in Ghost Launcher: this game's
 * own override if it has one (gameName is the catalog name, e.g.
 * "Coilrush"), otherwise the arcade-wide name. Always leaves a usable name
 * in out: GHOSTLINK_DEFAULT_NAME when there's no launcher or nothing is
 * set. Returns true only if a real profile name was found. */
bool GhostLink_GetUsername(const char *gameName, char *out, size_t outSize);

/* Records a finished run in scores/<slug>.txt, keeping the file sorted
 * (by mode, then score descending) and trimmed to the top GHOSTLINK_TOP_N
 * per mode. Scores <= 0 are ignored. Returns the 1-based rank the run
 * achieved in its mode, or 0 if it didn't make the table / couldn't be
 * saved. */
int GhostLink_SubmitScore(const char *slug, const char *mode, const char *username,
                           long score, const char *date);

/* Reads up to maxOut entries for one mode, best first. Returns the count. */
int GhostLink_LoadScores(const char *slug, const char *mode, GhostScore *out, int maxOut);

/* Same, but from the online board ghost-sync last downloaded. Empty if the
 * player is offline, has sync disabled, or has never synced. */
int GhostLink_LoadGlobalScores(const char *slug, const char *mode, GhostScore *out, int maxOut);

/* Fire-and-forget: starts `ghost-sync --quiet` fully detached (no zombie, no
 * shared stdio) and returns immediately. Does nothing, silently, if the
 * helper isn't installed. */
void GhostLink_TriggerSync(void);

#endif
