#include "hudfx.h"
#include "finesse.h"
#include "menumodel.h"
#include <stdio.h>
#include <string.h>

void HudFx_Reset(HudFx *h, const Game *g) {
    memset(h, 0, sizeof(*h));
    long s = g ? g->score : 0;
    h->shown = h->from = h->target = h->lastScore = s;
    h->countT = COUNTUP_SECONDS;
    for (int k = 0; k < STAT_KINDS; k++) h->nudge[k] = 9.0f;
    h->levelAge = 9.0f;
    h->lastSecond = g ? Hud_UltraSeconds(g->timeLeft) : 0;
}

/* Top to bottom when several arrive together. */
static int DisplayRank(CalloutKind k) {
    switch (k) {
        case CALL_PERFECT: return 0;
        case CALL_CLEAR: return 1;
        case CALL_B2B: return 2;
        case CALL_COMBO: return 3;
        case CALL_DUG: return 4;
        case CALL_SPLIT: return 5;
        case CALL_POWER: return 6;
        case CALL_LEVEL: return 7;
        case CALL_NEW_BEST: return 8;
        case CALL_FRESH: return 9;
        case CALL_UNDO: return 10;
        case CALL_FINESSE: return 11; /* the hint, under everything */
    }
    return 12;
}

/* The clear's word (3.2.0): a T-spin names itself and its lines, a mini is
 * T-SPIN MINI whatever it cleared (its popup says the points), a single
 * alone has no word. */
static const char *ClearWord(int cleared, TSpinKind tspin) {
    if (tspin == TSPIN_MINI) return "T-SPIN MINI";
    if (tspin == TSPIN_FULL) {
        static const char *const kT[4] = {"T-SPIN", "T-SPIN SINGLE", "T-SPIN DOUBLE", "T-SPIN TRIPLE"};
        return kT[cleared < 0 ? 0 : (cleared > 3 ? 3 : cleared)];
    }
    if (cleared >= 4) return "QUAD";
    if (cleared == 3) return "TRIPLE";
    if (cleared == 2) return "DOUBLE";
    return NULL;
}

static Callout Make(CalloutKind kind, const char *text, const char *shortText, CalloutTint tint, PowerUp power) {
    Callout c;
    memset(&c, 0, sizeof(c));
    c.kind = kind;
    snprintf(c.text, sizeof(c.text), "%s", text);
    snprintf(c.shortText, sizeof(c.shortText), "%s", shortText ? shortText : "");
    c.tint = tint;
    c.power = power;
    return c;
}

/* What arrived this frame: the most important CALLOUT_MAX of it, in display
 * order. It replaces any lines still showing from an earlier lock, so the
 * lines on screen always arrived together: they rise, hold and go together,
 * and none hops up or changes size when an older one ends (a DOUBLE half a
 * second after a QUAD used to shrink the QUAD, then jump into its place). */
static void Push(HudFx *h, Callout *cand, int n) {
    for (int i = 1; i < n; i++) /* most important first */
        for (int j = i; j > 0 && cand[j].kind > cand[j - 1].kind; j--) {
            Callout t = cand[j]; cand[j] = cand[j - 1]; cand[j - 1] = t;
        }
    if (n > CALLOUT_MAX) n = CALLOUT_MAX;
    for (int i = 1; i < n; i++) /* then top to bottom */
        for (int j = i; j > 0 && DisplayRank(cand[j].kind) < DisplayRank(cand[j - 1].kind); j--) {
            Callout t = cand[j]; cand[j] = cand[j - 1]; cand[j - 1] = t;
        }
    h->calls = 0;
    for (int i = 0; i < n; i++) {
        cand[i].age = 0.0f;
        cand[i].seq = ++h->seq;
        h->call[h->calls++] = cand[i];
    }
}

static void AddPopup(HudFx *h, const Game *g, long points) {
    if (h->pops == HUD_POPUPS) {
        memmove(&h->pop[0], &h->pop[1], sizeof(Popup) * (HUD_POPUPS - 1));
        h->pops--;
    }
    Popup *p = &h->pop[h->pops++];
    memset(p, 0, sizeof(*p));
    char grouped[24];
    Menu_Group(points, grouped, sizeof(grouped));
    snprintf(p->text, sizeof(p->text), "+%s", grouped);
    p->gold = points >= POPUP_GOLD_POINTS;
    /* From where the piece landed, on the rows it cleared. */
    Cell cells[CELLS_PER_PIECE];
    Tetromino_GetCells(g->lastLockType, g->lastLockRotation, cells);
    float cx = 0.0f;
    for (int i = 0; i < CELLS_PER_PIECE; i++) cx += (float)cells[i].x;
    p->col = (float)g->lastLockX + cx / (float)CELLS_PER_PIECE + 0.5f;
    int n = g->lastClearCount > 4 ? 4 : g->lastClearCount;
    float rows = 0.0f;
    for (int i = 0; i < n; i++) rows += (float)(g->lastClearRows[i] - BOARD_HIDDEN);
    /* A T-spin that cleared nothing (3.2.0) pops from the T's centre row. */
    p->row = n > 0 ? rows / (float)n + 0.5f : (float)(g->lastLockY + 1 - BOARD_HIDDEN) + 0.5f;
}

void HudFx_NoteSplit(HudFx *h, const char *text, bool gold) {
    h->splitPending = true;
    h->splitGold = gold;
    snprintf(h->splitText, sizeof(h->splitText), "%s", text ? text : "");
}

void HudFx_React(HudFx *h, const Game *g, bool passedBest) {
    /* The score counts up from what is drawn now to the real value. */
    if (g->score != h->target) {
        if (g->score < h->target) {
            h->shown = h->from = h->target = g->score;
            h->countT = COUNTUP_SECONDS;
        } else {
            h->from = h->shown;
            h->target = g->score;
            h->countT = 0.0f;
        }
    }

    Callout cand[12]; /* room for every kind at once, though no mode can make them all */
    int n = 0;
    long gained = g->score - h->lastScore;
    if (g->justLocked) {
        int cleared = g->lastClearCount;
        TSpinKind tspin = g->lastTSpin;
        const char *word = ClearWord(cleared, tspin);
        /* gold: a quad or a full T-spin; a mini and the smaller clears light */
        if (word) cand[n++] = Make(CALL_CLEAR, word, tspin != TSPIN_NONE ? (tspin == TSPIN_MINI ? "MINI" : "T-SPIN") : NULL,
                                   cleared >= 4 || tspin == TSPIN_FULL ? TINT_GOLD : TINT_LIGHT, PU_NONE);
        if (g->lastPerfectClear) cand[n++] = Make(CALL_PERFECT, "PERFECT CLEAR", "PERFECT", TINT_GOLD, PU_NONE);
        if (g->lastWasB2B) cand[n++] = Make(CALL_B2B, "BACK-TO-BACK", "B2B", TINT_GOLD, PU_NONE);
        if (cleared > 0 && g->lastCombo >= 2) {
            char buf[24];
            snprintf(buf, sizeof(buf), "COMBO %d", g->lastCombo);
            cand[n++] = Make(CALL_COMBO, buf, NULL, TINT_LIGHT, PU_NONE);
        }
        if (g->lastDug >= DUG_CALLOUT_MIN) {
            char buf[24];
            snprintf(buf, sizeof(buf), "DUG %d", g->lastDug);
            cand[n++] = Make(CALL_DUG, buf, NULL, TINT_LIGHT, PU_NONE);
        }
        if (g->lastDug > 0) h->nudge[STAT_DUG] = 0.0f;
        if (g->lastPower != PU_NONE) {
            char buf[24];
            snprintf(buf, sizeof(buf), "%s", Game_PowerName(g->lastPower));
            for (char *q = buf; *q; q++) if (*q >= 'a' && *q <= 'z') *q = (char)(*q - 32);
            cand[n++] = Make(CALL_POWER, buf, NULL, TINT_POWER, g->lastPower);
        }
        if ((cleared > 0 || tspin != TSPIN_NONE) && gained > 0) AddPopup(h, g, gained);
        if (gained >= 100) h->nudge[STAT_SCORE] = 0.0f; /* a clear or a power-up, not the drop points alone */
        if (cleared > 0) h->nudge[STAT_LINES] = h->nudge[STAT_LINES_LEFT] = 0.0f;
        /* The Practice room's finesse hint (3.5.0): a lock that took more
         * presses than it needed says how many, in dim words under whatever
         * else the lock says; a clean lock says nothing. */
        if (g->lastFinesseExtra > 0) {
            char text[24], shortText[12];
            Finesse_Words(g->lastFinesseExtra, text, sizeof(text), shortText, sizeof(shortText));
            cand[n++] = Make(CALL_FINESSE, text, shortText, TINT_DIM, PU_NONE);
        }
        if (g->practice.on) h->nudge[STAT_UNDO] = 0.0f; /* one more piece that can be taken back */
    }
    if (g->justUndid) {
        cand[n++] = Make(CALL_UNDO, "UNDO", NULL, TINT_LIGHT, PU_NONE);
        h->nudge[STAT_UNDO] = 0.0f;
    }
    if (g->lastWasLevelUp) {
        char buf[24];
        snprintf(buf, sizeof(buf), "LEVEL %d", g->level);
        cand[n++] = Make(CALL_LEVEL, buf, NULL, TINT_LIGHT, PU_NONE);
        h->levelAge = 0.0f;
        h->nudge[STAT_LEVEL] = 0.0f;
    }
    if (passedBest) {
        cand[n++] = Make(CALL_NEW_BEST, "NEW BEST", NULL, TINT_GOLD, PU_NONE);
        h->nudge[STAT_BEST] = 0.0f;
    }
    if (h->splitPending) {
        cand[n++] = Make(CALL_SPLIT, h->splitText, NULL, h->splitGold ? TINT_GOLD : TINT_LIGHT, PU_NONE);
        h->nudge[STAT_TIME] = 0.0f;
        h->splitPending = false;
    }
    if (g->justZenReset) {
        cand[n++] = Make(CALL_FRESH, "FRESH START", NULL, TINT_LIGHT, PU_NONE);
        h->nudge[STAT_FRESH] = 0.0f;
    }
    if (n) Push(h, cand, n);

    /* Ultra's last ten seconds tick (a daily's too, 3.6.0). */
    if (Game_Timed(g)) {
        int sec = Hud_UltraSeconds(g->timeLeft);
        if (sec != h->lastSecond && sec <= (int)ULTRA_RED_SECONDS) h->nudge[STAT_TIME_LEFT] = 0.0f;
        h->lastSecond = sec;
    }
    h->lastScore = g->score;
}

void HudFx_Tick(HudFx *h, float dt, bool playing) {
    int keep = 0;
    for (int i = 0; i < h->calls; i++) {
        h->call[i].age += dt;
        if (h->call[i].age < CALLOUT_SECONDS) h->call[keep++] = h->call[i];
    }
    h->calls = keep;
    keep = 0;
    for (int i = 0; i < h->pops; i++) {
        h->pop[i].age += dt;
        if (h->pop[i].age < POPUP_SECONDS) h->pop[keep++] = h->pop[i];
    }
    h->pops = keep;

    if (!playing) {
        h->shown = h->from = h->target;
        h->countT = COUNTUP_SECONDS;
    } else {
        h->countT += dt;
        float k = h->countT / COUNTUP_SECONDS;
        if (k >= 1.0f) h->shown = h->target;
        else {
            float e = 1.0f - (1.0f - k) * (1.0f - k); /* ease out */
            h->shown = h->from + (long)((float)(h->target - h->from) * e);
        }
    }
    for (int k = 0; k < STAT_KINDS; k++) if (h->nudge[k] < 9.0f) h->nudge[k] += dt;
    if (h->levelAge < 9.0f) h->levelAge += dt;
}

int HudFx_Callouts(const HudFx *h, const Callout **out) {
    for (int i = 0; i < h->calls; i++) out[i] = &h->call[i];
    return h->calls;
}

int HudFx_CalloutMaxSize(const HudFx *h, const Callout *c) {
    const Callout *head = NULL;
    for (int i = 0; i < h->calls; i++) /* the most important; of equals, the newest */
        if (!head || h->call[i].kind > head->kind || (h->call[i].kind == head->kind && h->call[i].seq > head->seq)) head = &h->call[i];
    return (!head || head->seq == c->seq) ? 32 : 16;
}

int HudFx_CalloutRise(const Callout *c, bool motion) {
    if (!motion || c->age >= CALLOUT_RISE_SECONDS) return CALLOUT_RISE;
    int r = (int)(c->age / CALLOUT_RISE_SECONDS * (float)CALLOUT_RISE);
    return r < 0 ? 0 : r;
}

int HudFx_PopupRise(const Popup *p, bool motion) {
    if (!motion) return 0;
    int r = (int)(p->age / POPUP_SECONDS * (float)POPUP_RISE);
    return r < 0 ? 0 : (r > POPUP_RISE ? POPUP_RISE : r);
}

int HudFx_Nudge(const HudFx *h, StatKind k, bool motion) {
    if (!motion || k < 0 || k >= STAT_KINDS) return 0;
    return h->nudge[k] < NUDGE_SECONDS ? NUDGE_PX : 0;
}

int HudFx_PipsLit(const HudFx *h, int into, bool motion) {
    if (h->levelAge < LEVELUP_PIPS_SECONDS) {
        if (!motion) return LINES_PER_LEVEL;
        int lit = 1 + (int)(h->levelAge / LEVELUP_PIPS_SECONDS * (float)LINES_PER_LEVEL);
        return lit > LINES_PER_LEVEL ? LINES_PER_LEVEL : lit;
    }
    if (h->levelAge < LEVELUP_FRAME_SECONDS) return LINES_PER_LEVEL;
    return into < 0 ? 0 : (into > LINES_PER_LEVEL ? LINES_PER_LEVEL : into);
}

float HudFx_FrameFlash(const HudFx *h) {
    return h->levelAge < LEVELUP_FRAME_SECONDS ? 1.0f - h->levelAge / LEVELUP_FRAME_SECONDS : 0.0f;
}

float HudFx_GridGlow(const HudFx *h) {
    return h->levelAge < LEVELUP_GRID_SECONDS ? 1.0f - h->levelAge / LEVELUP_GRID_SECONDS : 0.0f;
}

int Hud_UltraSeconds(float timeLeft) { return timeLeft <= 0.0f ? 0 : (int)timeLeft; }

UltraTint Hud_UltraTint(float timeLeft) {
    int s = Hud_UltraSeconds(timeLeft);
    if (s <= (int)ULTRA_RED_SECONDS) return ULTRA_RED;
    if (s <= (int)ULTRA_GOLD_SECONDS) return ULTRA_GOLD;
    return ULTRA_NORMAL;
}

bool Hud_PassesBest(long score, long best, RecordUnit unit, GameMode mode, bool recordable, bool alreadyPassed) {
    return !alreadyPassed && recordable && unit == UNIT_POINTS && mode != MODE_ZEN && best > 0 && score > best;
}
