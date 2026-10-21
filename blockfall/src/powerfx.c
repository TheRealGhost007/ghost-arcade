#include "powerfx.h"
#include <math.h>

static float Clamp01(float k) { return k < 0.0f ? 0.0f : (k > 1.0f ? 1.0f : k); }

/* ------------------------------------------------------------------ bomb */

BombStage PowerFx_BombStage(float age) {
    if (age < 0.0f) return BOMB_NONE;
    if (age < BOMB_BLINK_SECONDS) return BOMB_BLINK;
    if (age < BOMB_BLINK_SECONDS + BOMB_SHOCK_SECONDS) return BOMB_SHOCK;
    return BOMB_NONE;
}

bool PowerFx_BombBursts(float age, float prevAge) { return prevAge < BOMB_BLINK_SECONDS && age >= BOMB_BLINK_SECONDS; }

bool PowerFx_BombLit(float age, bool calm) {
    if (age < 0.0f || age >= BOMB_BLINK_SECONDS) return false;
    if (calm) return true; /* steady: no blink with Reduced flashing */
    int beat = (int)(age / BOMB_BLINK_BEAT);
    return (beat & 1) == 0; /* on, off, on */
}

float PowerFx_BombShock(float age) {
    if (age < BOMB_BLINK_SECONDS || age >= BOMB_BLINK_SECONDS + BOMB_SHOCK_SECONDS) return -1.0f;
    return (age - BOMB_BLINK_SECONDS) / BOMB_SHOCK_SECONDS;
}

/* ----------------------------------------------------------------- laser */

LaserStage PowerFx_LaserStage(float age) {
    if (age < 0.0f) return LASER_NONE;
    if (age < LASER_CHARGE_SECONDS) return LASER_CHARGE;
    if (age < LASER_CHARGE_SECONDS + LASER_BEAM_SECONDS) return LASER_BEAM;
    return LASER_NONE;
}

float PowerFx_LaserCharge(float age, bool motion) {
    if (!motion) return 1.0f;
    return Clamp01(age / LASER_CHARGE_SECONDS);
}

float PowerFx_LaserBeam(float age) {
    if (PowerFx_LaserStage(age) != LASER_BEAM) return 0.0f;
    return 1.0f - (age - LASER_CHARGE_SECONDS) / LASER_BEAM_SECONDS;
}

float PowerFx_LaserPopAt(int dist) { return LASER_CHARGE_SECONDS + (float)(dist < 0 ? -dist : dist) * LASER_POP_PER_ROW; }

/* ---------------------------------------------------------------- freeze */

float PowerFx_FreezeWash(float age, bool motion) {
    if (!motion || age < 0.0f || age >= FREEZE_WASH_SECONDS) return -1.0f;
    return age / FREEZE_WASH_SECONDS;
}

float PowerFx_Frost(float age, float timer, bool motion) {
    if (timer <= 0.0f || age < 0.0f) return 0.0f;
    if (!motion) return 1.0f;
    float k = Clamp01(age / FROST_GROW_SECONDS);
    return 1.0f - (1.0f - k) * (1.0f - k); /* ease out */
}

float PowerFx_FreezeLit(float timer, bool calm) {
    if (timer <= 0.0f) return 0.0f;
    if (timer >= FREEZE_BLINK_SECONDS) return 1.0f;
    if (calm) return FREEZE_CALM_LIT;
    float phase = fmodf(timer * FREEZE_BLINK_HZ, 1.0f);
    return phase < 0.5f ? 1.0f : FREEZE_BLINK_LOW;
}

float PowerFx_Thaw(float age) {
    if (age < 0.0f || age >= THAW_SECONDS) return 0.0f;
    return 1.0f - age / THAW_SECONDS;
}

/* ------------------------------------------------------------------ slow */

bool PowerFx_SlowEnding(float timer) { return timer > 0.0f && timer < SLOW_ENDING_SECONDS; }

int PowerFx_Afterimages(bool motion) { return motion ? SLOW_AFTERIMAGES : 0; }

float PowerFx_AfterimageAlpha(int i) { return i == 0 ? 0.22f : 0.12f; }
