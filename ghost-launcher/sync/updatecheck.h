#ifndef UPDATECHECK_H
#define UPDATECHECK_H

#include <stdbool.h>

/* "Is there a newer Ghost Arcade on GitHub than the one installed here?"
 *
 * ghost-sync asks GitHub for the recent commits on main and compares them with
 * the commit this build was made from; the answer goes into a small text file
 * that the launcher reads. The launcher itself never touches the network.
 * Nothing about the player is sent: it is one anonymous GET to a public API.
 *
 * This file is the raylib-free, curl-free part: parsing (GitHub's reply is
 * treated as hostile, like everything else downloaded) and the state file. */

#define UPDATE_SHA_LEN 41
#define UPDATE_TITLE_LEN 80
#define UPDATE_REPO_LEN 80
#define UPDATE_DEFAULT_REPO "TheRealGhost007/ghost-arcade"
#define UPDATE_CHECK_EVERY_SECONDS (6 * 60 * 60)

typedef enum {
    UPDATE_UNKNOWN,     /* never checked, check failed, or this build's commit is not on GitHub's main */
    UPDATE_CURRENT,     /* this build is the newest commit */
    UPDATE_BEHIND,      /* GitHub has `behind` newer commits */
} UpdateStatus;

typedef struct {
    UpdateStatus status;
    int behind;                       /* commits GitHub is ahead by; 0 unless UPDATE_BEHIND */
    char current[UPDATE_SHA_LEN];     /* the commit this build was made from ("" if unknown) */
    char latest[UPDATE_SHA_LEN];      /* newest commit on GitHub ("" if unknown) */
    char title[UPDATE_TITLE_LEN];     /* first line of the newest commit's message, printable ASCII */
    long long checkedAt;              /* unix time of the last successful check, 0 = never */
} UpdateState;

/* Exactly 40 lowercase hex characters. */
bool Update_IsSha(const char *s);
/* owner/name, each part letters, digits, '.', '_' or '-'. */
bool Update_IsRepo(const char *s);

/* Reads GitHub's "list commits" JSON (newest first). Fills latest, title,
 * status and behind from where `currentSha` appears among the top-level
 * commits. Returns false if no commit could be read at all. */
bool Update_ParseCommits(const char *json, const char *currentSha, UpdateState *out);

/* $XDG_DATA_HOME/ghost-launcher/update.txt */
bool Update_Save(const UpdateState *s);
/* Always leaves *s valid (UPDATE_UNKNOWN if there is no usable file). */
void Update_Load(UpdateState *s);

#endif
