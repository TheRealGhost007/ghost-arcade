#ifndef TUNING_H
#define TUNING_H

#include <stdbool.h>

/* Live tuning knobs for playtesting (F2 in a game). A game registers a few
 * numbers, the panel edits them while it runs, and "save" keeps them in
 * ~/.config/ghost-arcade/tuning/<game>.ini for next time. Cores read a knob
 * through the pointer Tune_Add returns (or Tune_Get by key), so no raylib
 * here. A run played with any knob off its default is NOT recorded: the
 * leaderboards stay honest. */
#define TUNE_MAX 16

typedef struct {
    char key[24];
    char label[32];
    float value, def, min, max, step;
} Tunable;

void Tune_Init(const char *slug);   /* reads the saved file; registers "speed" (game speed) */
float *Tune_Add(const char *key, const char *label, float def, float min, float max, float step);
float Tune_Get(const char *key, float fallback);
float Tune_Speed(void);             /* multiply the frame's dt by this */
int Tune_Count(void);
Tunable *Tune_At(int i);
bool Tune_Modified(void);           /* any knob off its default */
void Tune_ResetAll(void);
bool Tune_Save(void);               /* writes only knobs that differ from their default */

#endif
