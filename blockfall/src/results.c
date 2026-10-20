#include "results.h"
#include "layout.h"
#include "menumodel.h"
#include "uikeymap.h"
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ the card */

EndKind Results_KindOf(const Game *g) {
    if (!g->won) return END_TOPOUT;
    return g->cfg.mode == MODE_SPRINT ? END_SPRINT_CLEAR : END_TIME_UP;
}

/* A value in its board's unit: "74 372", or a time "1:12.40". */
static void Value(RecordUnit unit, long value, char *out, size_t size) {
    if (unit == UNIT_TIME) Record_FormatTime(Record_ValueCentis(value), out, size);
    else Menu_Group(value, out, size);
}

static void Stat(ResultsCard *c, int i, const char *label, const char *value) {
    snprintf(c->statLabel[i], sizeof(c->statLabel[i]), "%s", label);
    snprintf(c->statValue[i], sizeof(c->statValue[i]), "%s", value);
}

static void StatNumber(ResultsCard *c, int i, const char *label, long n) {
    char v[16];
    Menu_Group(n, v, sizeof(v));
    Stat(c, i, label, v);
}

static void Clock(float seconds, char *out, size_t size) {
    if (!(seconds >= 0.0f)) seconds = 0.0f;
    long s = (long)seconds;
    snprintf(out, size, "%ld:%02ld", s / 60, s % 60);
}

static void BestLine(ResultsCard *c, const Game *g, const RunFacts *f, const RunRecord *r) {
    RecordUnit unit = Record_UnitOf(g->cfg.mode);
    char best[24], gap[24];
    Value(unit, f->best, best, sizeof(best));
    if (c->newBest) {
        if (f->best > 0) snprintf(c->bestLine, sizeof(c->bestLine), "Old best %s", best);
        else snprintf(c->bestLine, sizeof(c->bestLine), "No best before this one");
    } else if (g->cfg.mode == MODE_ZEN) {
        snprintf(c->bestLine, sizeof(c->bestLine), "Zen keeps no scores");
    } else if (f->tuned) {
        snprintf(c->bestLine, sizeof(c->bestLine), "A tuned run is never recorded");
    } else if (g->cfg.mode == MODE_SPRINT && !g->won) {
        if (f->best > 0) snprintf(c->bestLine, sizeof(c->bestLine), "Best %s", best);
        else snprintf(c->bestLine, sizeof(c->bestLine), "No best yet");
    } else if (!r->counts) {
        /* a run counts once a piece has landed and it has points */
        snprintf(c->bestLine, sizeof(c->bestLine), "%s", g->piecesLocked > 0 ? "No points, so nothing is recorded"
                                                                              : "Nothing landed, so nothing is recorded");
    } else if (f->best <= 0) {
        snprintf(c->bestLine, sizeof(c->bestLine), "No best yet");
    } else if (r->value == f->best) {
        snprintf(c->bestLine, sizeof(c->bestLine), "Best %s, matched", best);
    } else if (unit == UNIT_TIME) {
        Record_FormatGap(unit, Record_ValueCentis(r->value), Record_ValueCentis(f->best), gap, sizeof(gap));
        snprintf(c->bestLine, sizeof(c->bestLine), "Best %s, %s behind", best, gap);
    } else {
        Menu_Group(f->best - r->value, gap, sizeof(gap));
        snprintf(c->bestLine, sizeof(c->bestLine), "Best %s, %s to go", best, gap);
    }
}

void Results_Build(ResultsCard *c, const Game *g, const RunFacts *f) {
    memset(c, 0, sizeof(*c));
    RunRecord r = Record_Classify(g, f->tuned);
    GameMode mode = g->cfg.mode;
    c->kind = Results_KindOf(g);
    static const char *const kTitles[3] = {"GAME OVER", "SPRINT CLEAR", "TIME UP"};
    snprintf(c->title, sizeof(c->title), "%s", kTitles[c->kind]);
    snprintf(c->sub, sizeof(c->sub), "%s on %s", Game_ModeName(mode), f->board && f->board[0] ? f->board : "this board");
    c->timeValue = c->kind == END_SPRINT_CLEAR;
    c->bigValue = c->timeValue ? Record_ValueCentis(Record_SprintValue(g->elapsed)) : g->score;
    c->newBest = r.toBoard && Record_Beats(r.value, f->best);
    BestLine(c, g, f, &r);

    if (r.toBoard) {
        char here[32], world[40];
        if (f->rankHere > 0) snprintf(here, sizeof(here), "#%d on this machine", f->rankHere);
        else snprintf(here, sizeof(here), "Not in the top ten here");
        if (r.local) snprintf(world, sizeof(world), "Custom sizes stay here");
        else if (f->worldRows > 0) snprintf(world, sizeof(world), "World: would be #%d", f->worldRank);
        else snprintf(world, sizeof(world), "World: waits for a sync");
        snprintf(c->rankLine, sizeof(c->rankLine), "%s    %s", here, world);
    }
    if (f->trophy && f->trophy[0]) {
        snprintf(c->note, sizeof(c->note), "Trophy earned: %s", f->trophy);
        c->noteGold = true;
    } else if (mode == MODE_SPRINT && !g->won && !f->tuned) {
        snprintf(c->note, sizeof(c->note), "Only a finished Sprint goes on the board");
    }

    /* The run in numbers, two to a row. */
    char v[24];
    StatNumber(c, 0, "Lines", g->linesCleared);
    StatNumber(c, 1, "Level", g->level);
    StatNumber(c, 2, "Pieces", g->piecesLocked);
    snprintf(v, sizeof(v), "%.2f", g->elapsed > 0.0f ? (double)g->piecesLocked / (double)g->elapsed : 0.0);
    Stat(c, 3, "Per second", v);
    if (c->kind == END_SPRINT_CLEAR) {
        StatNumber(c, 4, "Points", g->score); /* the big number is the time */
    } else {
        Clock(g->elapsed, v, sizeof(v));
        Stat(c, 4, "Time", v);
    }
    StatNumber(c, 5, "Quads", g->stats.clears[4]);
    StatNumber(c, 6, "Best combo", g->stats.bestCombo);
    StatNumber(c, 7, "Back-to-back", g->stats.backToBacks);
    StatNumber(c, 8, "Holds", g->stats.holds);
    if (mode == MODE_POWER) StatNumber(c, 9, "Power-ups", g->stats.powers);
    else if (mode == MODE_ZEN) StatNumber(c, 9, "Fresh starts", g->zenResets);
    else if (mode == MODE_SPRINT && !g->won) StatNumber(c, 9, "Lines left", g->linesCleared < SPRINT_LINES ? SPRINT_LINES - g->linesCleared : 0);
    else {
        double perMin = g->elapsed > 0.0f ? (double)g->linesCleared * 60.0 / (double)g->elapsed : 0.0;
        snprintf(v, sizeof(v), perMin >= 100.0 ? "%.0f" : "%.1f", perMin);
        Stat(c, 9, "Lines/min", v);
    }
}

int Results_WouldRank(const GhostScore *rows, int count, long value, const char *username) {
    int rank = 1;
    for (int i = 0; i < count; i++) {
        if (username && strcmp(rows[i].username, username) == 0) continue;
        if (rows[i].score > value) rank++;
    }
    return rank;
}

int Results_RankHere(const GhostScore *rows, int count, const char *username, long value, const char *date, long rowlessBest) {
    int rank = Record_FindRow(rows, count, username, value, date);
    if (rank <= 0 || rowlessBest <= value) return rank;
    for (int i = 0; i < count; i++)
        if (rows[i].score >= rowlessBest) return rank; /* it has a row after all: counted already */
    return rank + 1;
}

/* ------------------------------------------------------------- timeline */

void Results_Begin(Results *r, const ResultsCard *card, bool motion) {
    memset(r, 0, sizeof(*r));
    r->on = true;
    r->motion = motion;
    r->card = *card;
    Kit_Enter(&r->kit, RI_AGAIN);
    Kit_GlideReset(&r->glide);
}

void Results_End(Results *r) { r->on = false; }

/* Frame times add up in floats, so each mark on the timeline is met a hair
 * early rather than a whole frame late (at 60 fps: the gate on frame 24,
 * a tick every third frame, the stamp on frame 102). */
#define END_EPS 0.001f

float Results_CountSeconds(const Results *r) { return r->motion ? END_COUNT_SECONDS : END_COUNT_REDUCED; }
float Results_DoneAt(const Results *r) { return END_COUNT_AT + Results_CountSeconds(r); }
bool Results_InputOpen(const Results *r) { return r->on && r->t >= END_INPUT_GATE - END_EPS; }
bool Results_Done(const Results *r) { return r->on && (r->counted || r->t >= Results_DoneAt(r) - END_EPS); }

static float Clamp01(float k) { return k < 0.0f ? 0.0f : (k > 1.0f ? 1.0f : k); }
static float Ease(float k) { return 1.0f - (1.0f - k) * (1.0f - k); }

float Results_RowStep(int rows) {
    if (rows <= 0) return END_ROW_STEP;
    float even = END_ROWS_SECONDS / (float)rows;
    return even < END_ROW_STEP ? even : END_ROW_STEP;
}

int Results_RowsTurned(const Results *r, int rows) {
    if (!r->on || r->t < END_ROWS_START || rows <= 0) return 0;
    int n = 1 + (int)((r->t - END_ROWS_START) / Results_RowStep(rows));
    return n > rows ? rows : n;
}

float Results_RowsDim(const Results *r) {
    if (!r->on) return 0.0f;
    return Clamp01((r->t - END_ROWS_START) / END_ROWS_SECONDS);
}

bool Results_PieceRed(const Results *r, bool calm) {
    if (!r->on || r->card.kind != END_TOPOUT || r->t >= END_PIECE_BLINK) return false;
    if (calm) return true; /* steady, never blinking */
    return ((int)(r->t / (END_PIECE_BLINK / 4.0f)) % 2) == 0;
}

bool Results_CardShown(const Results *r) { return r->on && r->t >= END_CARD_AT; }

float Results_ScreenDim(const Results *r) {
    if (!Results_CardShown(r)) return 0.0f;
    return r->motion ? Clamp01((r->t - END_CARD_AT) / END_CARD_DROP) : 1.0f;
}

int Results_CardDrop(const Results *r) {
    if (!r->motion) return 0;
    float k = Clamp01((r->t - END_CARD_AT) / END_CARD_DROP);
    return (int)((float)END_CARD_DROP_PX * (1.0f - Ease(k)) + 0.5f);
}

long Results_Shown(const Results *r) {
    float k = Clamp01((r->t - END_COUNT_AT) / Results_CountSeconds(r));
    if (k >= 1.0f || r->counted) return r->card.bigValue;
    return (long)((double)r->card.bigValue * (double)k);
}

/* ------------------------------------------------------------- buttons */

KitRect Results_CardRect(void) {
    return (KitRect){(float)(LAYOUT_WINDOW_W - RESULTS_CARD_W) / 2.0f, (float)(LAYOUT_WINDOW_H - RESULTS_CARD_H) / 2.0f, RESULTS_CARD_W,
                     RESULTS_CARD_H};
}

KitRect Results_StatRect(int i) {
    KitRect c = Results_CardRect();
    int col = i % 2, row = i / 2;
    return (KitRect){c.x + 24.0f + (float)(col * 228), c.y + 258.0f + (float)(row * 26), 204.0f, 18.0f};
}

void Results_Layout(const Results *r, KitList *l) {
    (void)r;
    KitRect c = Results_CardRect();
    Kit_Clear(l, KIT_NAV_X | KIT_NAV_Y);
    KitItem *it = Kit_Add(l, RI_AGAIN, KIT_BUTTON, (KitRect){c.x + 16.0f, c.y + 396.0f, c.w - 32.0f, 44.0f}, "Play again");
    if (it) it->flags |= KIT_ACCENT;
    static const char *const kSmall[3] = {"Change setup", "Scores", "Menu"};
    float w = (c.w - 32.0f - 16.0f) / 3.0f;
    for (int i = 0; i < 3; i++) {
        it = Kit_Add(l, RI_SETUP + i, KIT_BUTTON, (KitRect){c.x + 16.0f + (float)i * (w + 8.0f), c.y + 452.0f, w, 36.0f}, kSmall[i]);
        if (!it) continue;
        it->style = KIT_STYLE_SMALL;
        it->textSize = 8;
    }
}

/* A key, a pad button or a click: what jumps to the end of the show. */
static bool AnyPress(const Input *in) {
    if (in->modPressed) return true;
    for (int i = 0; i < IK_COUNT; i++) if (in->key[i]) return true;
    for (int i = 0; i < PB_COUNT; i++) if (in->pad[i]) return true;
    for (int i = 0; i < MB_COUNT; i++) if (in->mousePressed[i]) return true;
    return false;
}

static bool ArrowPressed(const Input *in) {
    return Keymap_Pressed(KA_UI_LEFT, in) || Keymap_Pressed(KA_UI_RIGHT, in) || Keymap_Pressed(KA_UI_UP, in) ||
           Keymap_Pressed(KA_UI_DOWN, in);
}

ResultsStep Results_Update(Results *r, const RunKeys *k, const Input *in, float dt) {
    ResultsStep s = {RA_NONE, false, false, false, 1.0f, false};
    if (!r->on) return s;
    if (Results_InputOpen(r)) {
        /* Back first: Esc is Menu here even when a swap gave it to Restart. */
        if (k->back || Keymap_Pressed(KA_BACK, in)) { s.action = RA_MENU; return s; }
        if (k->restart) { s.action = RA_AGAIN; return s; }
        if (!Results_Done(r)) {
            if (AnyPress(in)) {
                r->t = Results_DoneAt(r);
                Kit_Enter(&r->kit, RI_AGAIN); /* the press that skipped never presses a button */
                s.skipped = true;
            }
        } else {
            KitList l;
            Results_Layout(r, &l);
            int before = r->kit.focus;
            KitEvent ev = Kit_Update(&r->kit, &l, in);
            if (r->kit.focus != before && ArrowPressed(in)) s.moved = true;
            if (ev.type == KIT_EV_PRESS) {
                switch (ev.id) {
                    case RI_AGAIN: s.action = RA_AGAIN; break;
                    case RI_SETUP: s.action = RA_SETUP; break;
                    case RI_SCORES: s.action = RA_SCORES; break;
                    case RI_MENU: s.action = RA_MENU; break;
                    default: break;
                }
                if (s.action != RA_NONE) return s;
            }
        }
    }
    /* The clock: the count-up's rising ticks, then the stamp. */
    if (r->t < 600.0f) r->t += dt;
    float doneAt = Results_DoneAt(r);
    if (!r->counted && r->t >= doneAt - END_EPS) {
        r->counted = true;
        s.stamp = true;
    } else if (!r->counted && r->t >= END_COUNT_AT - END_EPS && r->t - r->lastTick >= END_TICK_STEP - END_EPS) {
        r->lastTick = r->t;
        s.tick = true;
        s.tickPitch = 1.0f + 0.6f * Clamp01((r->t - END_COUNT_AT) / Results_CountSeconds(r));
    }
    return s;
}
