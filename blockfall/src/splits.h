#ifndef SPLITS_H
#define SPLITS_H

#include <stdbool.h>
#include <stddef.h>
#include "game.h"

/* Sprint's splits and pace (REWORK_PLAN.md, "Sprint splits and pace"),
 * raylib-free so it is tested headlessly; panel.c draws them and
 * scr_play.c keeps the file. Looks only: the rules never read any of it.
 *  - The clock is the run's own (Game.elapsed), so it starts on GO: READY
 *    holds it (playstep.h). The panel shows it big, with hundredths.
 *  - Marks at 10, 20 and 30 lines (and the finish at 40): the lock that
 *    reaches or passes one takes the clock's time there. Against your best
 *    pace on the board (the splits of the fastest finished Sprint on this
 *    machine, splits.dat) each mark says how far ahead ("-1.4 s", gold) or
 *    behind ("+0.8 s") you are, over the well for a moment and in the panel
 *    until the next mark. With no pace to race yet it shows the split's
 *    time ("10: 0:24.10").
 *  - A finished Sprint that beats the stored pace (or the first one) is the
 *    new pace. The results card says "New best by 0.8 s" (results.c). */

#define SPLIT_MARKS 4          /* 10, 20, 30 lines and the finish at 40 */
#define SPLITS_FILE "splits.dat"
#define SPLIT_TEXT_LEN 24

/* The clock at each mark, in hundredths of a second. */
typedef struct {
    bool have;
    long at[SPLIT_MARKS];
} SprintPace;

/* The lines a mark stands at: 10, 20, 30, 40. */
int Splits_MarkLines(int i);
/* Seconds to hundredths, as the Sprint boards count them. */
long Splits_Centis(float seconds);

/* The best pace on a board ($XDG_DATA_HOME/blockfall/splits.dat), false
 * when there is none yet. Other boards' lines are left as they are. */
bool Splits_Load(int w, int h, SprintPace *out);
bool Splits_Save(int w, int h, const SprintPace *p);
/* A finished run (every mark taken) that beats the stored pace's finish,
 * or any finished run when there is no pace yet. */
bool Splits_Better(const SprintPace *run, const SprintPace *best);

typedef struct {
    SprintPace best;   /* the pace to race (have false: none yet) */
    SprintPace run;    /* this run's marks, as it takes them */
    int marks;         /* marks taken so far */
    int lastLines;
    bool just;         /* a mark was taken this frame (its callout) */
    float age;         /* seconds since the last mark */
} Splits;

/* A new run or a restart, with the pace to race. */
void Splits_Reset(Splits *s, const SprintPace *best);
/* After the rules moved this frame; true when a mark was taken (a Sprint
 * only; other modes take none). */
bool Splits_React(Splits *s, const Game *g);
void Splits_Tick(Splits *s, float dt);
/* The frame's callout has been drawn. */
void Splits_Consume(Splits *s);

/* The last mark taken, 0-based, or -1 for none. */
int Splits_Last(const Splits *s);
/* The run against the pace at a mark, in hundredths (negative: ahead).
 * False when the mark is not taken yet or there is no pace. */
bool Splits_Diff(const Splits *s, int mark, long *centis);
/* "-1.4 s", "+0.8 s", "0.0 s" (tenths; a pace that is dead level is 0.0). */
void Splits_FormatDiff(long centis, char *out, size_t n);
/* "0.8 s": a gap, unsigned, in tenths. */
void Splits_FormatGap(long centis, char *out, size_t n);
/* The split line as the panel and the callout show it: "10: -1.4 s" (gold
 * when ahead) or "10: 0:24.10" with no pace. False, and out empty, before
 * the first mark. */
bool Splits_Text(const Splits *s, char *out, size_t n, bool *gold);

#endif
