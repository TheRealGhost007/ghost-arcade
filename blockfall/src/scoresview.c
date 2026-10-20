#include "scoresview.h"
#include "menumodel.h"
#include "render.h"
#include <stdio.h>
#include <string.h>

#define LEFT 40
#define RIGHT (WINDOW_WIDTH - 40)
#define STAND_Y 562
#define FOOT_Y 580
#define OLD_Y 598

/* A value in its unit: "74 372", or "1:12.40". */
static void Value(RecordUnit unit, long value, char *out, size_t size) {
    if (unit == UNIT_TIME) Record_FormatValue(unit, value, out, size);
    else Menu_Group(value, out, size);
}

void ScoresView_Draw(ScoresViewInfo *v) {
    const UiTheme *t = Ui_Theme();
    GhostScore shown[GHOSTLINK_TOP_N * 2];
    int shownCount = Ui_ScoresFilter(v->entries, v->count, v->period, shown, GHOSTLINK_TOP_N * 2);
    if (shownCount > GHOSTLINK_TOP_N) shownCount = GHOSTLINK_TOP_N;
    v->blinkRow = -1;

    int rowY = SCORES_ROW_Y;
    if (shownCount == 0) {
        Ui_Text("Nothing here yet", LEFT, rowY + 8, UI_T16, t->dim);
        char period[96];
        snprintf(period, sizeof(period), "No runs in this period. Press %s for a wider one.", v->periodKey ? v->periodKey : "T");
        const char *why = v->empty ? v->empty
                        : v->period != UI_PERIOD_ALL ? period
                        : v->world ? "The world board downloads in the background when you are online."
                                   : "Finish a run and it lands on this table.";
        Ui_Text(why, LEFT, rowY + 40, UI_T8, t->dim);
    }

    static const Color kMedal[3] = {{255, 214, 90, 255}, {200, 210, 230, 255}, {214, 140, 90, 255}};
    int myRank = 0;
    for (int i = 0; i < shownCount; i++) {
        bool mine = v->username && strcmp(shown[i].username, v->username) == 0;
        if (mine && !myRank) myRank = i + 1;
        bool fresh = v->blink && v->blinkRow < 0 && mine && shown[i].score == v->blinkValue && v->blinkDate &&
                     strcmp(shown[i].date, v->blinkDate) == 0;
        if (fresh) v->blinkRow = i;
        Color c = mine ? t->light : (i < 3 ? kMedal[i] : t->text);

        int rowH = SCORES_ROW_PITCH;
        int bandY = rowY - 8, bandH = rowH - 2;
        if (fresh && v->blinkLit) DrawRectangle(LEFT - 12, bandY, RIGHT - LEFT + 24, bandH, Ui_Lerp(t->panel, t->gold, 0.25f));
        else if (mine) DrawRectangle(LEFT - 12, bandY, RIGHT - LEFT + 24, bandH, t->panel);
        else if (i % 2 == 0) DrawRectangle(LEFT - 12, bandY, RIGHT - LEFT + 24, bandH, Fade(t->panel, 0.45f));
        if (fresh) DrawRectangle(LEFT - 12, bandY, 4, bandH, t->gold); /* the run just put here: a gold bar */
        else if (mine) DrawRectangle(LEFT - 12, bandY, 4, bandH, t->light);

        char buf[32];
        if (i < 3) {
            DrawRectangle(LEFT, rowY - 4, 26, 24, kMedal[i]);
            snprintf(buf, sizeof(buf), "%d", i + 1);
            Ui_Text(buf, LEFT + 5, rowY, UI_T16, t->bg);
        } else {
            snprintf(buf, sizeof(buf), "%2d", i + 1);
            Ui_Text(buf, LEFT + 2, rowY, UI_T16, t->faint);
        }

        Value(v->unit, shown[i].score, buf, sizeof(buf));
        /* The name as long as it can be and still clear its value (a long
         * name beside a grouped 7-digit score). */
        char name[20];
        snprintf(name, sizeof(name), "%.14s", shown[i].username);
        int room = (RIGHT - 112 - Ui_Measure(buf, UI_T16) - 16) - (LEFT + 48);
        for (size_t n = strlen(name); n > 4 && Ui_Measure(name, UI_T16) > room; n--) name[n - 1] = '\0';
        Ui_Text(name, LEFT + 48, rowY, UI_T16, c);
        Ui_TextRight(buf, UI_T16, RIGHT - 112, rowY, fresh ? t->gold : c);
        Ui_TextRight(shown[i].date, UI_T8, RIGHT, rowY + 4, t->dim);
        rowY += rowH;
    }

    /* Where you stand: your place and the gap to the run above, in the
     * board's own unit (a Sprint gap is in seconds). */
    if (shownCount > 0) {
        char line[96], gap[32];
        if (myRank > 1) {
            if (v->unit == UNIT_TIME) Record_FormatGap(v->unit, Record_ValueCentis(shown[myRank - 1].score), Record_ValueCentis(shown[myRank - 2].score), gap, sizeof(gap));
            else Menu_Group(shown[myRank - 2].score - shown[myRank - 1].score, gap, sizeof(gap));
            snprintf(line, sizeof(line), "You are #%d, %s behind #%d", myRank, gap, myRank - 1);
            Ui_TextCentered(line, UI_T8, WINDOW_WIDTH / 2, STAND_Y, t->gold);
        } else if (myRank == 1) {
            Ui_TextCentered("You hold the top spot", UI_T8, WINDOW_WIDTH / 2, STAND_Y, t->gold);
        } else if (v->count < GHOSTLINK_TOP_N) {
            /* The board is not full: any run gets on it. */
            Ui_TextCentered(v->unit == UNIT_TIME ? "Any finished Sprint gets on this board" : "Any run that scores gets on this board",
                            UI_T8, WINDOW_WIDTH / 2, STAND_Y, t->dim);
        } else {
            /* The whole board's tenth, not the last row this period shows. */
            Value(v->unit, v->entries[v->count - 1].score, gap, sizeof(gap));
            snprintf(line, sizeof(line), "Beat %s to get on this board", gap);
            Ui_TextCentered(line, UI_T8, WINDOW_WIDTH / 2, STAND_Y, t->dim);
        }
    }
    const char *foot = v->footnote ? v->footnote
                     : v->world ? "The world board lists each player's best run" : "Ghost Launcher reads this same table";
    Ui_TextCentered(foot, UI_T8, WINDOW_WIDTH / 2, FOOT_Y, t->faint);
    if (v->oldBest > 0) {
        char line[96], best[24];
        Menu_Group(v->oldBest, best, sizeof(best));
        snprintf(line, sizeof(line), "Your classic best from before the table: %s", best);
        Ui_TextCentered(line, UI_T8, WINDOW_WIDTH / 2, OLD_Y, t->gold);
    }
}
