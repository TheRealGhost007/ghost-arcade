#ifndef JOURNEY_H
#define JOURNEY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "game.h"
#include "teams.h"

/* The Journey: the Rink Rats' road from the pond to the arena. Three
 * stages (the pond tournament, the town league, the arena playoffs) of three
 * games each. Win to go on; a loss costs one of three chances and you play
 * that game again. Each win lets you sign one of the beaten team's kids,
 * signature move and all, in place of one of yours. Teams you beat badly
 * hold a grudge: they come back in a later stage, sharper and rougher. A
 * friend's crew (the online rival crews) can be the final.
 *
 * No raylib: this is rules and bookkeeping, tested headlessly, saved to
 * $XDG_DATA_HOME/pondkings/journey.txt between sessions. */

#define JOURNEY_STAGES 3
#define JOURNEY_GAMES 3
#define JOURNEY_CHANCES 3
#define JOURNEY_FRIEND 9   /* an opponent that is the friend's crew, not a town team */
#define JOURNEY_NAME_LEN 16

typedef struct {
    char name[JOURNEY_NAME_LEN];
    uint8_t build, trait;
    int8_t from;            /* -1 an original Rink Rat, else the rival they came from (or JOURNEY_FRIEND) */
} CrewKid;

/* A crew as it travels: yours, or a friend's downloaded one. */
typedef struct {
    char name[24];
    CrewKid kids[TEAM_SIZE];
    AiLean lean;            /* how its owner plays, for the computer to copy */
    char code[12];          /* its online code ("" if it never went online) */
} CrewCard;

typedef struct {
    uint8_t active;         /* a run in progress */
    uint8_t finished;       /* over: won it all, or out of chances */
    uint8_t champion;
    uint8_t stage, game;    /* the next game to play */
    uint8_t chances;
    uint8_t canSign;        /* just won: sign someone, or pass */
    int8_t lastOpponent;    /* the team just beaten */
    CrewKid crew[TEAM_SIZE];
    int8_t opponent[JOURNEY_STAGES][JOURNEY_GAMES]; /* a rival index, JOURNEY_FRIEND, or -1 not yet drawn */
    uint8_t grudge[RIVAL_COUNT];
    int32_t points, wins, losses, goalsFor, goalsAgainst;
    uint64_t seed;
    uint8_t hasFriend;
    CrewCard friendCrew;
} Journey;

typedef struct {
    bool won;
    long points;            /* this game's */
    bool stageCleared;      /* that win finished a stage */
    bool champion;          /* that win was the final */
    bool out;               /* that loss was the last chance */
} JourneyResult;

void Journey_New(Journey *j, uint64_t seed, const CrewCard *friendCrewOrNull);
int Journey_Opponent(const Journey *j);         /* rival index or JOURNEY_FRIEND */
int Journey_Venue(const Journey *j);
const char *Journey_StageName(int stage);       /* "The pond tournament", ... */
const char *Journey_GameName(const Journey *j); /* "Game 2 of 3", "The final" */
const char *Journey_OpponentName(const Journey *j, int opponent);
/* The next game: venue, both line-ups, how sharp the rivals are. Every
 * player in the game goes on the crew. */
void Journey_Setup(const Journey *j, GameSetup *s, const int8_t slotTeam[MAX_PLAYERS], uint64_t seed, int periodMinutes);
/* After a game: points, progress, grudges. */
void Journey_Result(Journey *j, const Game *g, JourneyResult *out);
/* Signing, after a win: the beaten team's three kids. */
void Journey_SignOptions(const Journey *j, CrewKid out[TEAM_SIZE]);
void Journey_Sign(Journey *j, int theirKid, int releaseOurs);
void Journey_Pass(Journey *j);
long Journey_GamePoints(const Game *g, int team);
int Journey_Level(const Journey *j);            /* games won: 0..9 */

bool Journey_Save(const Journey *j);
bool Journey_Load(Journey *j);                  /* false: no run saved (j is then empty) */
void Journey_Delete(void);

/* For saving names: printable ASCII, no field separators, at most size-1 long. */
void Journey_CleanName(const char *in, char *out, size_t size);

#endif
