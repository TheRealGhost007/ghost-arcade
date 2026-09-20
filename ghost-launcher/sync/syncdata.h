#ifndef SYNCDATA_H
#define SYNCDATA_H

#include <stdbool.h>
#include <stddef.h>

/* Everything ghost-sync does that isn't a network call: config, the install
 * id, reading local score files, building upload bodies, parsing the
 * downloaded board, and writing the global cache. No libcurl, no raylib, so
 * it is all covered by `make test`. */

#define SYNC_URL_LEN 256
#define SYNC_KEY_LEN 256
#define SYNC_SLUG_LEN 33      /* server allows 32 */
#define SYNC_MODE_LEN 25      /* server allows 24 */
#define SYNC_NAME_LEN 33      /* server allows 32 */
#define SYNC_DATE_LEN 11      /* YYYY-MM-DD */
#define SYNC_UUID_LEN 37
#define SYNC_MAX_SCORE 100000000L
#define SYNC_MAX_ROWS 512

typedef struct {
    bool enabled;   /* online sharing is OPT-IN: off until the player says yes */
    bool decided;   /* online.conf has an enabled= line, i.e. the player has been asked */
    bool checkUpdates;      /* check_updates=0 switches the GitHub update check off */
    char updateRepo[80];    /* update_repo=owner/name; defaults to the official repository */
    char url[SYNC_URL_LEN];
    char key[SYNC_KEY_LEN];
} SyncConfig;

typedef struct {
    char game[SYNC_SLUG_LEN];
    char mode[SYNC_MODE_LEN];
    char username[SYNC_NAME_LEN];
    long score;
    char date[SYNC_DATE_LEN];
} SyncRow;

/* Defaults from online_config.h, overridden by
 * $XDG_CONFIG_HOME/ghost-launcher/online.conf (enabled= / url= / key=). */
void SyncConfig_Load(SyncConfig *cfg);

/* Records the player's choice: writes (or replaces) the enabled= line in
 * online.conf, keeping every other line. Returns false if it could not write. */
bool SyncConfig_SaveEnabled(bool enabled);

/* $XDG_DATA_HOME/ghost-launcher (not created). False if no HOME at all. */
bool Sync_DataDir(char *out, size_t outSize);

/* Reads the random per-install UUID, creating it on first use. It only
 * exists so re-uploads can be recognised as duplicates server-side. */
bool Sync_InstallId(char *out, size_t outSize);

/* Lowercase letters, digits and '-' only: what the server accepts for game
 * and mode, and what is safe to use as a file name. */
bool Sync_IsSlug(const char *s, size_t maxLen);
bool Sync_IsDate(const char *s);

/* Parses one "mode|username|score|date" line from a local score file into a
 * row that is guaranteed to pass the server's CHECK constraints (username
 * clipped/cleaned, everything else validated). Returns false for comments,
 * blanks and anything that wouldn't be accepted. */
bool Sync_ParseLocalLine(const char *game, const char *line, SyncRow *out);

/* Loads every uploadable row from scores/<game>.txt. */
int Sync_LoadLocalFile(const char *path, const char *game, SyncRow *rows, int maxRows);

/* Builds the JSON body for the submit_scores function:
 *   {"p_install_id":"...","p_rows":[{"game":..,"mode":..,"username":..,"score":..,"played_on":..},...]}
 * Returns bytes written, or 0 if it didn't fit. Rows come from
 * Sync_ParseLocalLine, so every string is already plain printable ASCII. */
size_t Sync_BuildUploadJson(const SyncRow *rows, int count, const char *installId,
                            char *out, size_t outSize);

/* The games this build knows about. Boards for anything else are never asked
 * for, so junk rows on the server cannot crowd the real ones out or make the
 * client create files for invented games. */
int Sync_KnownGameCount(void);
const char *Sync_KnownGame(int i);
bool Sync_IsKnownGame(const char *slug);

/* Replaces anything that is not printable ASCII with '?', in place. For text
 * that came from the network and is about to be shown in a terminal. */
void Sync_MakePrintable(char *s);

/* Parses the CSV the leaderboard endpoint returns (columns: game, mode,
 * username, score, played_on -- in that order, header row first). Rows that
 * fail validation are dropped: the server is not trusted with file names. */
int Sync_ParseBoardCsv(const char *csv, SyncRow *rows, int maxRows);

/* Writes scores/global/<game>.txt for every game present in rows (which
 * must be grouped by game, as the server returns them), atomically. Returns
 * the number of files written. */
int Sync_WriteGlobalCache(const SyncRow *rows, int count);

#endif
