#include "safefile.h"
#include "tuning.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static Tunable sTunables[TUNE_MAX];
static int sCount = 0;
static char sSlug[48] = "";
static float *sSpeed = NULL;

static struct { char key[24]; float value; } sSaved[TUNE_MAX];
static int sSavedCount = 0;

static bool TunePath(char *out, size_t size, bool makeDir) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    char base[512];
    if (xdg && xdg[0]) snprintf(base, sizeof(base), "%s", xdg);
    else {
        const char *home = getenv("HOME");
        if (!home || !home[0]) return false;
        snprintf(base, sizeof(base), "%s/.config", home);
    }
    char d1[600], d2[700];
    snprintf(d1, sizeof(d1), "%s/ghost-arcade", base);
    snprintf(d2, sizeof(d2), "%s/tuning", d1);
    if (makeDir) {
        mkdir(base, 0755);
        mkdir(d1, 0755);
        if (mkdir(d2, 0755) != 0 && errno != EEXIST) return false;
    }
    snprintf(out, size, "%s/%s.ini", d2, sSlug);
    return true;
}

static float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void Tune_Init(const char *slug) {
    sCount = 0;
    sSavedCount = 0;
    snprintf(sSlug, sizeof(sSlug), "%s", slug ? slug : "game");
    char path[900];
    if (TunePath(path, sizeof(path), false)) {
        FILE *f = fopen(path, "r");
        if (f) {
            char line[96];
            while (fgets(line, sizeof(line), f) && sSavedCount < TUNE_MAX) {
                char *eq = strchr(line, '=');
                if (!eq) continue;
                *eq = '\0';
                snprintf(sSaved[sSavedCount].key, sizeof(sSaved[0].key), "%s", line);
                sSaved[sSavedCount].value = (float)atof(eq + 1);
                sSavedCount++;
            }
            fclose(f);
        }
    }
    sSpeed = Tune_Add("speed", "Game speed", 1.0f, 0.5f, 1.5f, 0.05f);
}

float *Tune_Add(const char *key, const char *label, float def, float min, float max, float step) {
    for (int i = 0; i < sCount; i++) if (strcmp(sTunables[i].key, key) == 0) return &sTunables[i].value;
    if (sCount >= TUNE_MAX) return NULL;
    Tunable *t = &sTunables[sCount++];
    snprintf(t->key, sizeof(t->key), "%s", key);
    snprintf(t->label, sizeof(t->label), "%s", label);
    t->def = def; t->min = min; t->max = max; t->step = step;
    t->value = def;
    for (int i = 0; i < sSavedCount; i++) {
        if (strcmp(sSaved[i].key, key) == 0) t->value = Clamp(sSaved[i].value, min, max);
    }
    return &t->value;
}

float Tune_Get(const char *key, float fallback) {
    for (int i = 0; i < sCount; i++) if (strcmp(sTunables[i].key, key) == 0) return sTunables[i].value;
    return fallback;
}

float Tune_Speed(void) { return sSpeed ? *sSpeed : 1.0f; }
int Tune_Count(void) { return sCount; }
Tunable *Tune_At(int i) { return (i >= 0 && i < sCount) ? &sTunables[i] : NULL; }

bool Tune_Modified(void) {
    for (int i = 0; i < sCount; i++) {
        float d = sTunables[i].value - sTunables[i].def;
        if (d > 0.0001f || d < -0.0001f) return true;
    }
    return false;
}

void Tune_ResetAll(void) {
    for (int i = 0; i < sCount; i++) sTunables[i].value = sTunables[i].def;
}

bool Tune_Save(void) {
    char path[900];
    if (!TunePath(path, sizeof(path), true)) return false;
    if (!Tune_Modified()) { remove(path); return true; }
    FILE *f = SafeFile_Open(path);
    if (!f) return false;
    for (int i = 0; i < sCount; i++) {
        float d = sTunables[i].value - sTunables[i].def;
        if (d > 0.0001f || d < -0.0001f) fprintf(f, "%s=%.4f\n", sTunables[i].key, sTunables[i].value);
    }
    SafeFile_Close(f);
    return true;
}
