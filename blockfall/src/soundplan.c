#include "soundplan.h"
#include "feel.h"
#include "hudfx.h"
#include "sfx.h"
#include <math.h>
#include <string.h>

static void Add(SoundPlan *p, SoundHow how, int id, float arg, SoundWeight w) {
    if (p->count >= SOUND_CUES) return;
    p->now[p->count++] = (SoundCue){how, id, arg, w};
}

/* The shared WARNING jingle, at most once in a frame's cues: danger and
 * Ultra's last ten seconds both ask for it, and two at once would only be
 * louder. */
static void AddWarning(SoundPlan *p) {
    for (int i = 0; i < p->count; i++)
        if (p->now[i].how == SND_JINGLE && p->now[i].id == JINGLE_WARNING) return;
    Add(p, SND_JINGLE, JINGLE_WARNING, 0.0f, SND_SMALL);
}

static void AddLater(SoundPlan *p, SoundHow how, int id, float arg, SoundWeight w, float wait) {
    if (p->laters >= SOUND_LATER) return;
    p->later[p->laters].cue = (SoundCue){how, id, arg, w};
    p->later[p->laters].due = wait;
    p->laters++;
}

void SoundPlan_Reset(SoundPlan *p, const Game *g) {
    memset(p, 0, sizeof(*p));
    if (g) {
        p->freezeWas = g->freezeTimer;
        p->slowWas = g->slowTimer;
        p->inPlay = g->phase == GS_PLAYING;
        p->ultraSecond = Hud_UltraSeconds(g->timeLeft);
    }
}

float SoundPlan_ClearPitch(int count, bool b2b, int combo) {
    float pitch = Feel_ClearPitch(count, b2b);
    if (combo > COMBO_PITCH_STEPS) combo = COMBO_PITCH_STEPS;
    for (int i = 0; i < combo; i++) pitch *= SEMITONE;
    return pitch;
}

int SoundPlan_PowerSfx(PowerUp p) {
    switch (p) {
        case PU_BOMB: return SFX_BOMB;
        case PU_LASER: return SFX_LASER;
        case PU_FREEZE: return SFX_FREEZE;
        case PU_SLOW: return SFX_SLOW;
        case PU_SWEEP: return SFX_SWEEP;
        default: return -1;
    }
}

/* The clear's sound at its pitch: a single and a quad as they always
 * sounded when nothing raises them. */
static void AddClear(SoundPlan *p, int count, bool b2b, int combo) {
    int id = count >= 4 ? SFX_QUAD : SFX_CLEAR;
    float pitch = SoundPlan_ClearPitch(count, b2b, combo);
    if (pitch == 1.0f) Add(p, SND_PLAY, id, 1.0f, SND_BIG);
    else Add(p, SND_PITCHED, id, pitch, SND_BIG);
}

void SoundPlan_React(SoundPlan *p, const Game *g, const SoundEvents *e) {
    bool playing = g->phase == GS_PLAYING;
    if (e->ending) {
        /* The end's one sound is the caller's; nothing waiting plays over it. */
        p->laters = 0;
        p->count = 0;
        p->inPlay = false;
        return;
    }
    /* The first time the run passes its best: a soft coin (first, as it
     * always was). */
    if (e->passedBest) Add(p, SND_JINGLE, JINGLE_COIN, 0.0f, SND_SMALL);
    if (e->moved || g->justMoved) Add(p, SND_VARIED, SFX_MOVE, MOVE_VARIANCE, SND_SMALL);
    if (e->rotated || g->justRotated) Add(p, SND_PLAY, SFX_ROTATE, 1.0f, SND_SMALL);
    else if (e->rotateBlocked) Add(p, SND_PITCHED, SFX_ROTATE, ROTATE_BLOCKED_PITCH, SND_SMALL); /* the press registered */
    if (e->held || g->justHeld) Add(p, SND_PLAY, SFX_HOLD, 1.0f, SND_SMALL);

    if (g->justLocked) {
        int cleared = g->lastClearCount;
        PowerUp power = g->lastPower;
        int combo = g->stats.combo > 0 ? g->stats.combo : 0; /* counted on every board */
        /* The accent first (the thud came before the clear's sound). */
        if (g->lastWasB2B) Add(p, SND_PLAY, SFX_B2B, 1.0f, SND_ACCENT);
        else if (g->lastTSpin != TSPIN_NONE) Add(p, SND_PITCHED, SFX_B2B, TSPIN_PITCH, SND_ACCENT);
        else if (power != PU_NONE) Add(p, SND_JINGLE, JINGLE_COIN, 0.0f, SND_ACCENT);
        else if (g->justHardDropped) Add(p, SND_PLAY, SFX_HARD_DROP, 1.0f, SND_ACCENT);
        else Add(p, SND_PLAY, SFX_LOCK, 1.0f, SND_ACCENT);
        /* Then the one big sound. */
        if (power != PU_NONE) Add(p, SND_PLAY, SoundPlan_PowerSfx(power), 1.0f, SND_BIG);
        else if (g->lastPerfectClear) Add(p, SND_PITCHED, SFX_QUAD, PERFECT_CLEAR_PITCH, SND_BIG);
        else if (cleared > 0) AddClear(p, cleared, g->lastWasB2B, combo);
    }
    if (g->lastWasLevelUp) AddLater(p, SND_PLAY, SFX_LEVEL_UP, 1.0f, SND_SMALL, LEVEL_UP_DELAY_SECONDS);
    if (g->justRose) Add(p, SND_PITCHED, SFX_HARD_DROP, DIG_RISE_PITCH, SND_SMALL); /* Dig: the row pushing up */
    if (g->justUndid) Add(p, SND_PITCHED, SFX_HOLD, UNDO_PITCH, SND_SMALL);       /* the Practice room: a piece taken back */
    if (e->go) Add(p, SND_JINGLE, JINGLE_GO, 0.0f, SND_SMALL);
    /* Danger: the shared WARNING (3.6.1; never in Zen, which has no danger). */
    if (e->danger && playing && g->cfg.mode != MODE_ZEN) AddWarning(p);

    /* A Freeze or a Slow running out (in play: a pause holds them). */
    if (playing && p->inPlay) {
        if (p->freezeWas > 0.0f && g->freezeTimer <= 0.0f) Add(p, SND_PLAY, SFX_THAW, 1.0f, SND_SMALL);
        if (p->slowWas > 0.0f && g->slowTimer <= 0.0f) Add(p, SND_PLAY, SFX_WINDUP, 1.0f, SND_SMALL);
    }
    p->freezeWas = g->freezeTimer;
    p->slowWas = g->slowTimer;
    /* Ultra's last ten seconds: WARNING once (one WARNING if danger began
     * on the same frame). */
    if (playing && Game_Timed(g) && !p->ultraWarned && Hud_UltraSeconds(g->timeLeft) <= ULTRA_WARN_SECONDS && g->timeLeft > 0.0f) {
        p->ultraWarned = true;
        AddWarning(p);
    }
    /* ...and its last five seconds each tick, a little higher each time. */
    if (Game_Timed(g)) {
        int sec = Hud_UltraSeconds(g->timeLeft);
        if (playing && Danger_UltraTick(p->ultraSecond, sec)) Add(p, SND_PITCHED, SFX_MOVE, ULTRA_TICK_PITCH(sec), SND_SMALL);
        p->ultraSecond = sec;
    }
    p->inPlay = playing;
}

void SoundPlan_Tick(SoundPlan *p, float dt) {
    int keep = 0;
    for (int i = 0; i < p->laters; i++) {
        p->later[i].due -= dt;
        if (p->later[i].due <= 0.0f) {
            if (p->count < SOUND_CUES) p->now[p->count++] = p->later[i].cue;
        } else {
            p->later[keep++] = p->later[i];
        }
    }
    p->laters = keep;
}

int SoundPlan_Take(SoundPlan *p, SoundCue *out, int max) {
    int n = p->count < max ? p->count : max;
    for (int i = 0; i < n; i++) out[i] = p->now[i];
    p->count = 0;
    return n;
}
