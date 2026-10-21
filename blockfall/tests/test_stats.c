/* The lifetime stats (stats.c, 3.3.0): the file's round trip and what it
 * puts up with, the fold of a scripted run played on the real rules (which
 * runs count, the counters, the T-spin clears the bar sets apart), the three
 * records and their days, the total over the modes, the bar and the words.
 * The file lives in the temp HOME test_main.c makes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "../src/game.h"
#include "../src/persist.h"
#include "../src/runrecord.h"
#include "../src/stats.h"
#include "../src/tetromino.h"

static void Row(Game *g, int y, const char *pattern) {
    int row = Board_TotalH(&g->board) - 1 - y;
    for (int x = 0; x < g->board.w; x++) {
        char c = x < (int)strlen(pattern) ? pattern[x] : '.';
        g->board.cells[row][x] = c == '#' ? (int8_t)PIECE_J : CELL_EMPTY;
    }
}

static void Place(Game *g, PieceType t, int rot, int px, int py) {
    g->current = t;
    g->rotation = rot;
    g->px = px;
    g->py = py;
    g->curPower = PU_NONE;
    g->fallTimer = g->lockTimer = 0.0f;
    g->lockResets = 0;
    g->lowestRow = py;
    g->grounded = false;
    g->lastMoveWasTurn = false;
    g->lastKick = 0;
}

static Game Fresh(GameMode mode) {
    Game g;
    Game_InitConfig(&g, (GameConfig){mode, 10, 20, true}, 7, 0, 1, 0);
    Board_Init(&g.board, 10, 20);
    return g;
}

/* A vertical I dropped into column 9 (the rightmost): with `full` rows
 * filled all but that column it clears them. */
static void DropIAt9(Game *g) {
    Cell cells[CELLS_PER_PIECE];
    Tetromino_GetCells(PIECE_I, 1, cells);
    Place(g, PIECE_I, 1, 9 - cells[0].x, 0);
    Game_HardDrop(g);
    Game_ConsumeFrameFlags(g);
}

/* The classic T-spin double slot, as test_modern.c has it. */
static void TsdSlot(Game *g) {
    Board_Init(&g->board, 10, 20);
    Row(g, 0, "####.#####");
    Row(g, 1, "###...####");
    Row(g, 2, "###.......");
    g->board.cells[Board_TotalH(&g->board) - 3][3] = PIECE_J; /* the overhang */
}

static bool SameMode(const ModeStats *a, const ModeStats *b) {
    if (a->runs != b->runs || a->lines != b->lines || a->pieces != b->pieces || a->centis != b->centis) return false;
    for (int k = 0; k < 5; k++) if (a->clears[k] != b->clears[k] || a->tspinClears[k] != b->tspinClears[k]) return false;
    if (a->tspins != b->tspins || a->perfects != b->perfects || a->powers != b->powers || a->holds != b->holds) return false;
    const StatRecord *ra[3] = {&a->pps, &a->combo, &a->quads}, *rb[3] = {&b->pps, &b->combo, &b->quads};
    for (int i = 0; i < 3; i++) if (ra[i]->value != rb[i]->value || strcmp(ra[i]->date, rb[i]->date) != 0) return false;
    return true;
}

static bool Same(const LifeStats *a, const LifeStats *b) {
    if (strcmp(a->since, b->since) != 0) return false;
    for (int i = 0; i < MODE_COUNT; i++) if (!SameMode(&a->mode[i], &b->mode[i])) return false;
    return true;
}

static bool FileHas(const char *text) {
    char path[600];
    if (!Persist_DataFile(STATS_FILE, path, sizeof(path))) return false;
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[256];
    bool found = false;
    while (!found && fgets(line, sizeof(line), f)) found = strstr(line, text) != NULL;
    fclose(f);
    return found;
}

static void WriteFile(const char *text) {
    char path[600];
    Persist_DataFile(STATS_FILE, path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
}

static void test_file(void) {
    LifeStats s, back;
    char path[600];
    Persist_DataFile(STATS_FILE, path, sizeof(path));
    remove(path);
    CHECK(!Stats_Load(&s), "file: no stats file yet reads as none");
    CHECK(s.since[0] == '\0' && s.mode[MODE_MARATHON].runs == 0 && s.mode[MODE_ZEN].pps.value == 0, "file: ...and everything is zero");

    Stats_Clear(&s);
    snprintf(s.since, sizeof(s.since), "2026-10-20");
    ModeStats *m = &s.mode[MODE_MARATHON];
    m->runs = 3; m->lines = 120; m->pieces = 310; m->centis = 61240;
    m->clears[1] = 10; m->clears[2] = 20; m->clears[3] = 5; m->clears[4] = 12;
    m->tspinClears[1] = 1; m->tspinClears[2] = 2;
    m->tspins = 4; m->perfects = 1; m->powers = 0; m->holds = 12;
    m->pps.value = 2310; snprintf(m->pps.date, sizeof(m->pps.date), "2026-10-19");
    m->combo.value = 7; snprintf(m->combo.date, sizeof(m->combo.date), "2026-10-20");
    m->quads.value = 12; snprintf(m->quads.date, sizeof(m->quads.date), "2026-10-18");
    s.mode[MODE_SPRINT].runs = 1; s.mode[MODE_SPRINT].lines = 40; s.mode[MODE_SPRINT].centis = 9000;
    s.mode[MODE_ZEN].runs = 2; s.mode[MODE_ZEN].holds = 9;
    CHECK(Stats_Save(&s), "file: saves");
    CHECK(Stats_Load(&back), "file: ...and reads back");
    CHECK(Same(&s, &back), "file: every counter, record and day survives the round trip");
    CHECK(FileHas("[marathon]") && FileHas("best_combo=7|2026-10-20") && FileHas("clears=10|20|5|12") && FileHas("since=2026-10-20"),
          "file: sections per mode, records as value|day, the clears in one line");
    CHECK(!FileHas("best_pps=0") && !FileHas("[sprint]\nbest"), "file: a record not set yet has no line");

    /* What it puts up with: comments, blank lines, CRLF, a section and keys
     * from a later build, a negative count, a record with no day. */
    WriteFile("# hand made\r\n\r\nsince=2026-01-02\r\n[marathon]\r\nruns=5\r\nlines=-3\r\nshiny=9\r\nbest_combo=4\r\n"
              "[puzzle]\r\nruns=99\r\nlines=99\r\n[zen]\r\nruns=1\r\nclears=1|2\r\n");
    CHECK(Stats_Load(&back), "file: a hand-made file reads");
    CHECK(strcmp(back.since, "2026-01-02") == 0 && back.mode[MODE_MARATHON].runs == 5 && back.mode[MODE_MARATHON].lines == 0,
          "file: known lines read, CRLF trimmed, a negative count is zero");
    CHECK(back.mode[MODE_MARATHON].combo.value == 4 && back.mode[MODE_MARATHON].combo.date[0] == '\0', "file: a record with no day keeps its value");
    CHECK(back.mode[MODE_ZEN].runs == 1 && back.mode[MODE_ZEN].clears[1] == 1 && back.mode[MODE_ZEN].clears[2] == 2 && back.mode[MODE_ZEN].clears[3] == 0,
          "file: a short clears line fills what it has");
    long total = 0;
    for (int i = 0; i < MODE_COUNT; i++) total += back.mode[i].runs;
    CHECK(total == 6, "file: a section this build does not know ([puzzle]) leaks into no mode");
    remove(path);
}

static void test_fold(void) {
    char path[600];
    Persist_DataFile(STATS_FILE, path, sizeof(path));
    remove(path);
    LifeStats life;
    Stats_Clear(&life);

    /* Nothing landed: not a run. */
    Game g = Fresh(MODE_MARATHON);
    CHECK(!Stats_Fold(&life, &g, false, "2026-10-20") && life.mode[MODE_MARATHON].runs == 0 && life.since[0] == '\0',
          "fold: an R tap before anything landed counts for nothing");

    /* A scripted Marathon on the real rules: a quad, then a single right
     * after it (a combo of one), 30 s on the clock. */
    for (int y = 0; y < 4; y++) Row(&g, y, "#########.");
    DropIAt9(&g);
    CHECK(g.stats.clears[4] == 1 && g.linesCleared == 4 && g.score > 0, "fold: (the scripted quad cleared)");
    Row(&g, 0, "#########.");
    DropIAt9(&g);
    CHECK(g.stats.clears[1] == 1 && g.linesCleared == 5 && g.stats.bestCombo == 1 && g.piecesLocked == 2, "fold: (then a single, a combo of one)");
    g.elapsed = 30.0f;
    CHECK(Stats_RunPps(&g) == 0, "pps: two pieces are too few for a pieces-per-second record");
    CHECK(!Stats_Fold(&life, &g, true, "2026-10-20") && life.mode[MODE_MARATHON].runs == 0, "fold: a tuned playtest never counts");
    CHECK(Stats_Fold(&life, &g, false, "2026-10-20"), "fold: the run counts");
    ModeStats *m = &life.mode[MODE_MARATHON];
    CHECK(m->runs == 1 && m->lines == 5 && m->pieces == 2 && m->centis == 3000, "fold: runs, lines, pieces and time come from the run");
    CHECK(m->clears[4] == 1 && m->clears[1] == 1 && m->clears[2] == 0 && m->tspinClears[1] == 0, "fold: the clears by size come from the run's counters");
    CHECK(m->quads.value == 1 && strcmp(m->quads.date, "2026-10-20") == 0, "records: the first quad is the most quads in a run, dated");
    CHECK(m->combo.value == 1 && strcmp(m->combo.date, "2026-10-20") == 0, "records: the combo of one is the biggest, dated");
    CHECK(m->pps.value == 0 && m->pps.date[0] == '\0', "records: no pieces-per-second record from a two-piece run");
    CHECK(strcmp(life.since, "2026-10-20") == 0, "fold: the first run that counts starts the count");
    CHECK(life.mode[MODE_SPRINT].runs == 0 && life.mode[MODE_ZEN].runs == 0, "fold: only its own mode");

    /* A Sprint with enough pieces for a rate: 40 pieces in 20 s is 2.00. */
    Game sp = Fresh(MODE_SPRINT);
    for (int y = 0; y < 4; y++) Row(&sp, y, "#########.");
    DropIAt9(&sp);
    sp.piecesLocked = 40;
    sp.elapsed = 20.0f;
    CHECK(Stats_RunPps(&sp) == 2000, "pps: 40 pieces in 20 s is 2.000");
    CHECK(Stats_Fold(&life, &sp, false, "2026-10-21"), "fold: an unfinished Sprint is still a run");
    CHECK(life.mode[MODE_SPRINT].runs == 1 && life.mode[MODE_SPRINT].pps.value == 2000 && strcmp(life.mode[MODE_SPRINT].pps.date, "2026-10-21") == 0,
          "records: Sprint's pieces per second, dated the day it was set");
    CHECK(life.mode[MODE_MARATHON].runs == 1 && strcmp(life.since, "2026-10-20") == 0, "fold: the Marathon and the first day are untouched");

    /* A second Marathon run: fewer quads keep the old record and its day; a
     * bigger combo takes the new day; the counters add up. */
    Game g2 = Fresh(MODE_MARATHON);
    for (int r = 0; r < 3; r++) {
        Board_Init(&g2.board, 10, 20); /* the I's three cells left over from the last single go */
        Row(&g2, 0, "#########.");
        DropIAt9(&g2);
    }
    CHECK(g2.stats.bestCombo == 2 && g2.stats.clears[1] == 3 && g2.stats.clears[4] == 0, "fold: (three singles in a row: a combo of two)");
    g2.elapsed = 10.0f;
    CHECK(Stats_Fold(&life, &g2, false, "2026-10-22"), "fold: the second Marathon counts");
    CHECK(m->runs == 2 && m->lines == 8 && m->pieces == 5 && m->centis == 4000 && m->clears[1] == 4 && m->clears[4] == 1, "fold: the counters add up over runs");
    CHECK(m->quads.value == 1 && strcmp(m->quads.date, "2026-10-20") == 0, "records: a run with no quad leaves the record and its day");
    CHECK(m->combo.value == 2 && strcmp(m->combo.date, "2026-10-22") == 0, "records: a bigger combo takes the record and the new day");

    /* A T-spin double in Ultra: the rules count it among the T-spin clears,
     * and the bar sets its lines apart from plain doubles. */
    Game u = Fresh(MODE_ULTRA);
    TsdSlot(&u);
    int bottom = Board_TotalH(&u.board) - 1;
    Place(&u, PIECE_T, 1, 3, bottom - 2);
    CHECK(Game_Rotate(&u, 1) && Game_TSpinKind(&u) == TSPIN_FULL, "tspin: (the T turns into the slot)");
    Game_HardDrop(&u);
    CHECK(u.lastClearCount == 2 && u.stats.tspins == 1 && u.stats.clears[2] == 1 && u.stats.tspinClears[2] == 1 && u.stats.tspinClears[1] == 0,
          "tspin: a T-spin double counts as a T-spin clear of two rows");
    Game_ConsumeFrameFlags(&u);
    Row(&u, 0, "#########.");
    Row(&u, 1, "#########.");
    DropIAt9(&u);
    CHECK(u.stats.clears[2] == 2 && u.stats.tspinClears[2] == 1, "tspin: a plain double after it is no T-spin clear");
    u.elapsed = 5.0f;
    CHECK(Stats_Fold(&life, &u, false, "2026-10-22"), "fold: the Ultra counts");
    long parts[STATS_BAR_PARTS];
    Stats_Bar(&life.mode[MODE_ULTRA], parts);
    CHECK(parts[0] == 0 && parts[1] == 2 && parts[2] == 0 && parts[3] == 0 && parts[4] == 2 && Stats_BarTotal(parts) == 4,
          "bar: the T-spin's two lines stand apart from the plain double's two");
    Stats_Bar(m, parts);
    CHECK(parts[0] == 4 && parts[3] == 4 && parts[4] == 0 && Stats_BarTotal(parts) == m->lines, "bar: singles and the quad in lines, the whole of the mode's lines");
    CHECK(life.mode[MODE_ULTRA].tspins == 1 && m->tspins == 0, "fold: T-spins are counted too");

    /* Zen counts as a run (it scores); a run that never scored does not. */
    Game z = Fresh(MODE_ZEN);
    Row(&z, 0, "#########.");
    DropIAt9(&z);
    z.elapsed = 3.0f;
    CHECK(Stats_Fold(&life, &z, false, "2026-10-22") && life.mode[MODE_ZEN].runs == 1, "fold: a Zen run counts (it keeps no score, but it is a run)");
    Game dud = Fresh(MODE_POWER);
    Place(&dud, PIECE_O, 0, 4, 0);
    Game_HardDrop(&dud);
    dud.score = 0; /* a drop's points taken away: nothing scored */
    CHECK(!Stats_Fold(&life, &dud, false, "2026-10-22") && life.mode[MODE_POWER].runs == 0, "fold: pieces that scored nothing are not a run");

    /* All modes together: the counters summed, each record the best of the
     * modes' with the mode that set it; a tie goes to the first mode. */
    ModeStats all;
    int who[3];
    Stats_Total(&life, &all, who);
    CHECK(all.runs == 5 && all.lines == 8 + 4 + 4 + 1 && all.pieces == 5 + 40 + 2 + 1, "total: the counters are summed over the modes");
    CHECK(all.pps.value == 2000 && who[0] == MODE_SPRINT, "total: the pieces-per-second record is Sprint's");
    CHECK(all.combo.value == 2 && who[1] == MODE_MARATHON, "total: the biggest combo is the Marathon's");
    CHECK(all.quads.value == 1 && who[2] == MODE_MARATHON, "total: the most quads is the Marathon's (the Sprint's one quad ties and the first mode keeps it)");
    Stats_Bar(&all, parts);
    CHECK(Stats_BarTotal(parts) == all.lines && parts[4] == 2, "total: the bar covers every line, the T-spin's apart");

    /* To the file and back, as the play screen does it. */
    CHECK(Stats_Save(&life), "fold: saved");
    LifeStats back;
    CHECK(Stats_Load(&back) && Same(&life, &back), "fold: the folded stats survive the file");
    remove(path);
}

static void test_words(void) {
    char b[32];
    Stats_FormatTime(4500, b, sizeof(b));
    CHECK(strcmp(b, "45s") == 0, "words: under a minute, seconds");
    Stats_FormatTime(72500, b, sizeof(b));
    CHECK(strcmp(b, "12m 05s") == 0, "words: minutes and seconds");
    Stats_FormatTime(390000, b, sizeof(b));
    CHECK(strcmp(b, "1h 05m") == 0, "words: hours and minutes");
    Stats_FormatTime(0, b, sizeof(b));
    CHECK(strcmp(b, "0s") == 0, "words: nothing played is 0s");
    Stats_FormatTime(-5, b, sizeof(b));
    CHECK(strcmp(b, "0s") == 0, "words: a negative is 0s");
    Stats_FormatPps(2310, b, sizeof(b));
    CHECK(strcmp(b, "2.31") == 0, "words: pieces per second to hundredths");
    Stats_FormatPps(2315, b, sizeof(b));
    CHECK(strcmp(b, "2.32") == 0, "words: ...rounded");
    Stats_FormatPps(0, b, sizeof(b));
    CHECK(strcmp(b, "0.00") == 0, "words: none is 0.00");
    CHECK(strcmp(Stats_BarName(0), "Single") == 0 && strcmp(Stats_BarName(4), "T-spin") == 0, "words: the bar's parts");
    CHECK(strcmp(Stats_ModeKey(MODE_POWER), "power") == 0 && strcmp(Stats_ModeKey((GameMode)99), "marathon") == 0, "words: the file's section names");

    Game g;
    Game_InitConfig(&g, (GameConfig){MODE_MARATHON, 10, 20, true}, 1, 0, 1, 0);
    g.piecesLocked = STATS_PPS_MIN_PIECES - 1;
    g.elapsed = 10.0f;
    CHECK(Stats_RunPps(&g) == 0, "pps: one piece short of the minimum gives no rate");
    g.piecesLocked = STATS_PPS_MIN_PIECES;
    CHECK(Stats_RunPps(&g) == 2000, "pps: the minimum run gives one");
    g.elapsed = 0.0f;
    CHECK(Stats_RunPps(&g) == 0, "pps: no time on the clock gives none");
}

void test_stats_all(void) {
    test_file();
    test_fold();
    test_words();
}
