#ifndef BOARDS_H
#define BOARDS_H

#include <stdbool.h>
#include <stddef.h>
#include "scores.h"

/* A machine's score boards, as its page shows them: for every mode (board),
 * this computer's top ten beside the world's, from the files the games and
 * ghost-sync already write (scores.h: mode|username|score|date). Regular
 * boards come first, A-Z, then the daily ones, newest first; daily boards
 * older than the games keep are left out. No raylib. */

#define BOARD_TOP 10
#define BOARD_MAX 40
#define BOARD_NAME_LEN 33 /* the server keeps 32 */
#define BOARD_YOU_MAX 2

typedef struct {
    char name[BOARD_NAME_LEN];
    long score;
    char date[11];
    bool you; /* one of your names: drawn highlighted */
} BoardRow;

typedef struct {
    char mode[SCORES_MODE_LEN];
    BoardRow local[BOARD_TOP]; /* this computer, best first */
    int localCount;
    BoardRow world[BOARD_TOP]; /* the online board, best first */
    int worldCount;
    int worldRank; /* your best place on the world board, 1-based; 0 = not on it */
    bool timed;    /* ranked by time (scores.h: Scores_IsTimeBoard) */
} Board;

typedef struct {
    Board b[BOARD_MAX];
    int count;
    char slug[SCORES_SLUG_LEN]; /* whose boards (Boards_Load) */
} Boards;

/* The names that are you: this game's own and the arcade's (NULL or "" for
 * none). Matched without regard to case, as the server tells players apart. */
typedef struct {
    const char *names[BOARD_YOU_MAX];
} BoardYou;

void Boards_Clear(Boards *bs);
/* One score file's text. Rows that do not parse are skipped; a board keeps
 * its best ten (ties keep the order they came in). dailyCutoff is the
 * oldest daily mode kept ("daily-YYYYMMDD"), or NULL for all. */
void Boards_AddText(Boards *bs, const char *text, bool world, const BoardYou *you, const char *dailyCutoff);
/* Puts the boards in order and works out your world places. */
void Boards_Finish(Boards *bs);
/* Both of a game's files, read and finished. */
void Boards_Load(Boards *bs, const char *slug, const BoardYou *you, const char *dailyCutoff);
/* The board for a mode, or -1. */
int Boards_Find(const Boards *bs, const char *mode);

/* "daily-" and eight digits. */
bool Boards_IsDaily(const char *mode);
/* How a mode reads: "Classic", "Power 14x20", "Today's daily" (today is
 * "daily-YYYYMMDD" for today), "Daily 13 Oct". */
void Boards_ModeWords(const char *mode, const char *today, char *out, size_t size);
/* The same for one game's boards, with the names a game gives its own:
 * Blockfall's "sprint40" is "Sprint 40" (ranked by time) and its old
 * points-ranked "sprint" board "Sprint (old)"; its "marathon-10x20" is
 * "Marathon" (the classic board under 3.2.0's modern rules) and the bare
 * "marathon" "Classic (old rules)". */
void Boards_ModeWordsFor(const char *slug, const char *mode, const char *today, char *out, size_t size);

#endif
