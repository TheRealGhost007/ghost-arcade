#ifndef ACHIEVEMENTS_H
#define ACHIEVEMENTS_H

#include <stdbool.h>
#include <stddef.h>

/* Achievements for every Ghost Arcade game. Each game reports a finished run
 * (Ach_CheckRun) and any newly earned achievements are recorded under
 * $XDG_DATA_HOME/ghost-launcher/achievements/<slug>.txt as  id|YYYY-MM-DD ;
 * Ghost Launcher reads the same files and the same table, so the two always
 * agree. Newly earned ones are queued as toasts for the next menu screen.
 * Best-effort and silent on any file error. No raylib. */
typedef enum { ACH_RUNS, ACH_SCORE, ACH_LEVEL } AchKind;

typedef struct {
    const char *slug;
    const char *id;
    const char *title;
    const char *desc;
    AchKind kind;     /* RUNS: runs played; SCORE / LEVEL: reached in a single run */
    long threshold;
} AchDef;

int Ach_Count(void);
const AchDef *Ach_At(int i);
/* The i-th achievement of one game, or NULL. */
const AchDef *Ach_ForGame(const char *slug, int i);
int Ach_CountForGame(const char *slug);

/* True if earned; fills date (at least 12 bytes) with its YYYY-MM-DD. */
bool Ach_IsUnlocked(const char *slug, const char *id, char *date);
int Ach_UnlockedForGame(const char *slug);

/* Call once per finished run. Returns how many achievements it unlocked. */
int Ach_CheckRun(const char *slug, long score, int level);

/* Toasts: one newly earned title per call, oldest first. */
bool Ach_PopToast(char *title, size_t size);

#endif
