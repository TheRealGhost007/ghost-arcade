#define _POSIX_C_SOURCE 200809L
#include "journey.h"
#include "safefile.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* How sharp the rivals are, game by game, before any grudge. */
static const uint8_t kLevel[JOURNEY_STAGES][JOURNEY_GAMES] = {
    {DIFF_EASY, DIFF_NORMAL, DIFF_NORMAL},
    {DIFF_NORMAL, DIFF_NORMAL, DIFF_HARD},
    {DIFF_NORMAL, DIFF_HARD, DIFF_HARD},
};

void Journey_CleanName(const char *in, char *out, size_t size) {
    size_t n = 0;
    for (const char *p = in; p && *p && n + 1 < size; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 32 || c > 126 || c == ',' || c == '|' || c == '=') continue;
        out[n++] = (char)c;
    }
    while (n > 0 && out[n - 1] == ' ') n--;
    out[n] = '\0';
    if (n == 0 && size > 1) snprintf(out, size, "?");
}

/* A small seeded shuffle, so a run's schedule is the run's seed's. */
static uint32_t Next(uint64_t *s) {
    *s ^= *s << 13;
    *s ^= *s >> 7;
    *s ^= *s << 17;
    return (uint32_t)(*s >> 32);
}

static int MostGrudge(const Journey *j, const bool skip[RIVAL_COUNT]) {
    int best = -1;
    for (int r = 0; r < RIVAL_COUNT; r++) {
        if (skip && skip[r]) continue;
        if (best < 0 || j->grudge[r] > j->grudge[best]) best = r;
    }
    return best;
}

static void Schedule(Journey *j, int stage) {
    uint64_t s = j->seed ^ (0x9E3779B97F4A7C15ull * (uint64_t)(stage + 1));
    if (s == 0) s = 1;
    int order[RIVAL_COUNT];
    for (int r = 0; r < RIVAL_COUNT; r++) order[r] = r;
    for (int r = RIVAL_COUNT - 1; r > 0; r--) {
        int k = (int)(Next(&s) % (uint32_t)(r + 1));
        int t = order[r]; order[r] = order[k]; order[k] = t;
    }
    if (stage == 0) {
        for (int g = 0; g < JOURNEY_GAMES; g++) j->opponent[0][g] = (int8_t)order[g];
        return;
    }
    bool played[RIVAL_COUNT] = {false};
    for (int g = 0; g < JOURNEY_GAMES; g++) if (j->opponent[0][g] >= 0 && j->opponent[0][g] < RIVAL_COUNT) played[j->opponent[0][g]] = true;
    if (stage == 1) {
        /* the two town teams you haven't met, then the one that wants you most */
        int n = 0;
        for (int r = 0; r < RIVAL_COUNT && n < 2; r++) if (!played[order[r]]) j->opponent[1][n++] = (int8_t)order[r];
        bool skip[RIVAL_COUNT] = {false};
        for (int g = 0; g < n; g++) skip[j->opponent[1][g]] = true;
        int grudge = MostGrudge(j, skip);
        while (n < JOURNEY_GAMES) {
            j->opponent[1][n] = (int8_t)(grudge >= 0 ? grudge : order[n % RIVAL_COUNT]);
            n++;
        }
        return;
    }
    /* the playoffs: anyone, then the biggest grudge, then the final */
    bool skip[RIVAL_COUNT] = {false};
    int grudge = MostGrudge(j, NULL);
    int first = order[0] == grudge ? order[1] : order[0];
    skip[first] = true;
    int second = MostGrudge(j, skip);
    skip[second] = true;
    j->opponent[2][0] = (int8_t)first;
    j->opponent[2][1] = (int8_t)second;
    if (j->hasFriend) {
        j->opponent[2][2] = JOURNEY_FRIEND;
    } else {
        int last = MostGrudge(j, skip);
        j->opponent[2][2] = (int8_t)(last >= 0 ? last : order[2]);
    }
}

void Journey_New(Journey *j, uint64_t seed, const CrewCard *friendCrew) {
    memset(j, 0, sizeof(*j));
    j->active = 1;
    j->chances = JOURNEY_CHANCES;
    j->seed = seed ? seed : 1;
    j->lastOpponent = -1;
    const TeamStyle *crew = Team_Crew();
    for (int k = 0; k < TEAM_SIZE; k++) {
        Journey_CleanName(crew->kids[k], j->crew[k].name, JOURNEY_NAME_LEN);
        j->crew[k].build = (uint8_t)k;
        j->crew[k].trait = crew->traits[k];
        j->crew[k].from = -1;
    }
    for (int s = 0; s < JOURNEY_STAGES; s++)
        for (int g = 0; g < JOURNEY_GAMES; g++) j->opponent[s][g] = -1;
    if (friendCrew) {
        j->hasFriend = 1;
        j->friendCrew = *friendCrew;
    }
    Schedule(j, 0);
}

int Journey_Opponent(const Journey *j) {
    int s = j->stage < JOURNEY_STAGES ? j->stage : JOURNEY_STAGES - 1;
    int g = j->game < JOURNEY_GAMES ? j->game : JOURNEY_GAMES - 1;
    return j->opponent[s][g];
}

int Journey_Venue(const Journey *j) { return j->stage == 0 ? VENUE_POND : j->stage == 1 ? VENUE_RINK : VENUE_ARENA; }

const char *Journey_StageName(int stage) {
    static const char *const kNames[JOURNEY_STAGES] = {"The pond tournament", "The town league", "The arena playoffs"};
    return (stage >= 0 && stage < JOURNEY_STAGES) ? kNames[stage] : "";
}

const char *Journey_GameName(const Journey *j) {
    static const char *const kNames[JOURNEY_GAMES] = {"Game 1 of 3", "Game 2 of 3", "Game 3 of 3"};
    if (j->stage == JOURNEY_STAGES - 1 && j->game == JOURNEY_GAMES - 1) return "The final";
    if (j->stage == JOURNEY_STAGES - 1 && j->game == 0) return "The semifinal";
    if (j->stage == JOURNEY_STAGES - 1 && j->game == 1) return "The second round";
    return kNames[j->game < JOURNEY_GAMES ? j->game : 0];
}

const char *Journey_OpponentName(const Journey *j, int opponent) {
    if (opponent == JOURNEY_FRIEND) return j->hasFriend ? j->friendCrew.name : "?";
    return Team_Rival(opponent)->name;
}

void Journey_Setup(const Journey *j, GameSetup *s, const int8_t slotTeam[MAX_PLAYERS], uint64_t seed, int periodMinutes) {
    Game_DefaultSetup(s, seed);
    s->venue = (uint8_t)Journey_Venue(j);
    s->periodSeconds = (periodMinutes < 1 ? 1 : periodMinutes > 3 ? 3 : periodMinutes) * 60;
    s->periods = 2;
    for (int p = 0; p < MAX_PLAYERS; p++) s->slotTeam[p] = slotTeam[p] >= 0 ? 0 : -1;
    for (int k = 0; k < TEAM_SIZE; k++) {
        s->build[0][k] = j->crew[k].build;
        s->trait[0][k] = j->crew[k].trait;
    }
    s->teamLevel[0] = DIFF_NORMAL;
    s->goalieStyle[0] = Team_Crew()->goalieStyle;
    int opp = Journey_Opponent(j);
    int stage = j->stage < JOURNEY_STAGES ? j->stage : JOURNEY_STAGES - 1, game = j->game < JOURNEY_GAMES ? j->game : JOURNEY_GAMES - 1;
    int level = kLevel[stage][game];
    if (opp == JOURNEY_FRIEND && j->hasFriend) {
        for (int k = 0; k < TEAM_SIZE; k++) {
            s->build[1][k] = j->friendCrew.kids[k].build < BUILD_COUNT ? j->friendCrew.kids[k].build : BUILD_BALANCED;
            s->trait[1][k] = j->friendCrew.kids[k].trait < TRAIT_COUNT ? j->friendCrew.kids[k].trait : TRAIT_NONE;
        }
        s->lean[1] = j->friendCrew.lean;
        s->goalieStyle[1] = GOALIE_BUTTERFLY;
        level = DIFF_HARD;
    } else {
        const TeamStyle *t = Team_Rival(opp);
        for (int k = 0; k < TEAM_SIZE; k++) {
            s->build[1][k] = (uint8_t)k;
            s->trait[1][k] = t->traits[k];
        }
        s->goalieStyle[1] = t->goalieStyle;
        int grudge = opp >= 0 && opp < RIVAL_COUNT ? j->grudge[opp] : 0;
        if (grudge >= 2 && level < DIFF_HARD) level++;
        s->lean[1].check = (int8_t)(grudge >= 4 ? 25 : grudge >= 2 ? 15 : 0); /* they remember */
    }
    s->teamLevel[1] = (uint8_t)level;
}

long Journey_GamePoints(const Game *g, int team) {
    int other = 1 - team;
    long p = 100L * g->score[team] + 50L * g->oneTimerGoals[team];
    if (g->phase == PH_OVER && g->winner == team) {
        p += 300 + 50L * (g->score[team] - g->score[other]);
        if (g->score[other] == 0) p += 200;
    }
    return p;
}

int Journey_Level(const Journey *j) { return j->wins; }

void Journey_Result(Journey *j, const Game *g, JourneyResult *out) {
    memset(out, 0, sizeof(*out));
    if (!j->active || j->finished || g->phase != PH_OVER) return;
    int opp = Journey_Opponent(j);
    out->won = g->winner == 0;
    out->points = Journey_GamePoints(g, 0);
    j->goalsFor += g->score[0];
    j->goalsAgainst += g->score[1];
    if (out->won) {
        int margin = g->score[0] - g->score[1];
        out->points += 100L * j->stage;
        j->wins++;
        if (opp >= 0 && opp < RIVAL_COUNT) {
            int add = margin >= 3 ? 2 : 1; /* beat them badly and they won't forget */
            j->grudge[opp] = (uint8_t)(j->grudge[opp] + add > 5 ? 5 : j->grudge[opp] + add);
        }
        j->lastOpponent = (int8_t)opp;
        j->canSign = 1;
        if (j->game + 1 < JOURNEY_GAMES) {
            j->game++;
        } else if (j->stage + 1 < JOURNEY_STAGES) {
            j->stage++;
            j->game = 0;
            out->stageCleared = true;
            Schedule(j, j->stage);
        } else {
            out->stageCleared = true;
            out->champion = true;
            out->points += 1000;
            j->champion = 1;
            j->finished = 1;
            j->active = 0;
            j->canSign = 0;
        }
    } else {
        j->losses++;
        if (opp >= 0 && opp < RIVAL_COUNT && j->grudge[opp] > 0) j->grudge[opp]--;
        if (j->chances > 0) j->chances--;
        if (j->chances == 0) {
            out->out = true;
            j->finished = 1;
            j->active = 0;
        }
    }
    j->points += (int32_t)out->points;
}

void Journey_SignOptions(const Journey *j, CrewKid out[TEAM_SIZE]) {
    int opp = j->lastOpponent;
    for (int k = 0; k < TEAM_SIZE; k++) {
        memset(&out[k], 0, sizeof(out[k]));
        if (opp == JOURNEY_FRIEND && j->hasFriend) {
            out[k] = j->friendCrew.kids[k];
            out[k].from = JOURNEY_FRIEND;
        } else if (opp >= 0 && opp < RIVAL_COUNT) {
            const TeamStyle *t = Team_Rival(opp);
            Journey_CleanName(t->kids[k], out[k].name, JOURNEY_NAME_LEN);
            out[k].build = (uint8_t)k;
            out[k].trait = t->traits[k];
            out[k].from = (int8_t)opp;
        }
    }
}

void Journey_Sign(Journey *j, int theirKid, int releaseOurs) {
    if (!j->canSign) return;
    j->canSign = 0;
    if (theirKid < 0 || theirKid >= TEAM_SIZE || releaseOurs < 0 || releaseOurs >= TEAM_SIZE) return;
    CrewKid options[TEAM_SIZE];
    Journey_SignOptions(j, options);
    if (options[theirKid].name[0] == '\0') return;
    j->crew[releaseOurs] = options[theirKid];
}

void Journey_Pass(Journey *j) { j->canSign = 0; }

/* ------------------------------------------------------------ the file */

static bool DataPath(char *out, size_t size, bool make) {
    const char *xdg = getenv("XDG_DATA_HOME");
    char base[512];
    if (xdg && xdg[0]) snprintf(base, sizeof(base), "%s", xdg);
    else {
        const char *home = getenv("HOME");
        if (!home || !home[0]) return false;
        snprintf(base, sizeof(base), "%s/.local/share", home);
    }
    char dir[600];
    snprintf(dir, sizeof(dir), "%s/pondkings", base);
    if (make) {
        mkdir(base, 0755);
        if (mkdir(dir, 0755) != 0 && errno != EEXIST) return false;
    }
    snprintf(out, size, "%s/journey.txt", dir);
    return true;
}

static void PutKid(FILE *f, const char *key, const CrewKid *k) { fprintf(f, "%s=%s,%u,%u,%d\n", key, k->name, k->build, k->trait, k->from); }

bool Journey_Save(const Journey *j) {
    char path[700];
    if (!DataPath(path, sizeof(path), true)) return false;
    FILE *f = SafeFile_Open(path);
    if (!f) return false;
    fprintf(f, "# Pond Kings: the Journey in progress\nversion=1\n");
    fprintf(f, "active=%u\nfinished=%u\nchampion=%u\nstage=%u\ngame=%u\nchances=%u\ncansign=%u\nlast=%d\n",
            j->active, j->finished, j->champion, j->stage, j->game, j->chances, j->canSign, j->lastOpponent);
    fprintf(f, "seed=%llu\npoints=%d\nwins=%d\nlosses=%d\ngf=%d\nga=%d\n", (unsigned long long)j->seed, j->points, j->wins, j->losses, j->goalsFor, j->goalsAgainst);
    for (int k = 0; k < TEAM_SIZE; k++) { char key[8]; snprintf(key, sizeof(key), "kid%d", k); PutKid(f, key, &j->crew[k]); }
    for (int s = 0; s < JOURNEY_STAGES; s++) fprintf(f, "opp%d=%d,%d,%d\n", s, j->opponent[s][0], j->opponent[s][1], j->opponent[s][2]);
    fprintf(f, "grudge=");
    for (int r = 0; r < RIVAL_COUNT; r++) fprintf(f, "%s%u", r ? "," : "", j->grudge[r]);
    fprintf(f, "\nfriend=%u\n", j->hasFriend);
    if (j->hasFriend) {
        const CrewCard *c = &j->friendCrew;
        fprintf(f, "fname=%s\nfcode=%s\nflean=%d,%d,%d,%d\n", c->name, c->code, c->lean.shoot, c->lean.pass, c->lean.check, c->lean.side);
        for (int k = 0; k < TEAM_SIZE; k++) { char key[8]; snprintf(key, sizeof(key), "fkid%d", k); PutKid(f, key, &c->kids[k]); }
    }
    return SafeFile_Close(f);
}

static int Clampi(long v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : (int)v; }

static void GetKid(const char *v, CrewKid *k) {
    char name[64] = "";
    const char *comma = strchr(v, ',');
    if (!comma) return;
    size_t n = (size_t)(comma - v);
    if (n >= sizeof(name)) n = sizeof(name) - 1;
    memcpy(name, v, n);
    name[n] = '\0';
    Journey_CleanName(name, k->name, JOURNEY_NAME_LEN);
    long b = 0, t = 0, from = -1;
    if (sscanf(comma + 1, "%ld,%ld,%ld", &b, &t, &from) != 3) return;
    k->build = (uint8_t)Clampi(b, 0, BUILD_COUNT - 1);
    k->trait = (uint8_t)Clampi(t, 0, TRAIT_COUNT - 1);
    k->from = (int8_t)Clampi(from, -1, JOURNEY_FRIEND);
}

static int8_t Opp(long v) { return (int8_t)((v >= 0 && v < RIVAL_COUNT) || v == JOURNEY_FRIEND ? v : -1); }

bool Journey_Load(Journey *j) {
    memset(j, 0, sizeof(*j));
    char path[700];
    if (!DataPath(path, sizeof(path), false)) return false;
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[256];
    bool any = false;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#') continue;
        *eq = '\0';
        const char *k = line, *v = eq + 1;
        long n = strtol(v, NULL, 10);
        any = true;
        if (!strcmp(k, "active")) j->active = n != 0;
        else if (!strcmp(k, "finished")) j->finished = n != 0;
        else if (!strcmp(k, "champion")) j->champion = n != 0;
        else if (!strcmp(k, "stage")) j->stage = (uint8_t)Clampi(n, 0, JOURNEY_STAGES - 1);
        else if (!strcmp(k, "game")) j->game = (uint8_t)Clampi(n, 0, JOURNEY_GAMES - 1);
        else if (!strcmp(k, "chances")) j->chances = (uint8_t)Clampi(n, 0, JOURNEY_CHANCES);
        else if (!strcmp(k, "cansign")) j->canSign = n != 0;
        else if (!strcmp(k, "last")) j->lastOpponent = Opp(n);
        else if (!strcmp(k, "seed")) j->seed = strtoull(v, NULL, 10);
        else if (!strcmp(k, "points")) j->points = Clampi(n, 0, 100000000);
        else if (!strcmp(k, "wins")) j->wins = Clampi(n, 0, 99);
        else if (!strcmp(k, "losses")) j->losses = Clampi(n, 0, 99);
        else if (!strcmp(k, "gf")) j->goalsFor = Clampi(n, 0, 9999);
        else if (!strcmp(k, "ga")) j->goalsAgainst = Clampi(n, 0, 9999);
        else if (!strncmp(k, "kid", 3) && k[3] >= '0' && k[3] < '0' + TEAM_SIZE && !k[4]) GetKid(v, &j->crew[k[3] - '0']);
        else if (!strncmp(k, "fkid", 4) && k[4] >= '0' && k[4] < '0' + TEAM_SIZE && !k[5]) GetKid(v, &j->friendCrew.kids[k[4] - '0']);
        else if (!strncmp(k, "opp", 3) && k[3] >= '0' && k[3] < '0' + JOURNEY_STAGES && !k[4]) {
            long a = -1, b = -1, c = -1;
            if (sscanf(v, "%ld,%ld,%ld", &a, &b, &c) == 3) {
                int s = k[3] - '0';
                j->opponent[s][0] = Opp(a); j->opponent[s][1] = Opp(b); j->opponent[s][2] = Opp(c);
            }
        } else if (!strcmp(k, "grudge")) {
            const char *p = v;
            for (int r = 0; r < RIVAL_COUNT && p; r++) {
                j->grudge[r] = (uint8_t)Clampi(strtol(p, NULL, 10), 0, 5);
                p = strchr(p, ',');
                if (p) p++;
            }
        } else if (!strcmp(k, "friend")) j->hasFriend = n != 0;
        else if (!strcmp(k, "fname")) Journey_CleanName(v, j->friendCrew.name, sizeof(j->friendCrew.name));
        else if (!strcmp(k, "fcode")) Journey_CleanName(v, j->friendCrew.code, sizeof(j->friendCrew.code));
        else if (!strcmp(k, "flean")) {
            long a = 0, b = 0, c = 0, d = 0;
            if (sscanf(v, "%ld,%ld,%ld,%ld", &a, &b, &c, &d) == 4) {
                j->friendCrew.lean.shoot = (int8_t)Clampi(a, -40, 40);
                j->friendCrew.lean.pass = (int8_t)Clampi(b, -40, 40);
                j->friendCrew.lean.check = (int8_t)Clampi(c, -40, 40);
                j->friendCrew.lean.side = (int8_t)Clampi(d, -1, 1);
            }
        }
    }
    fclose(f);
    if (!any) return false;
    if (j->seed == 0) j->seed = 1;
    /* a schedule a hand edit broke is drawn again */
    for (int s = 0; s <= j->stage && s < JOURNEY_STAGES; s++) {
        bool ok = true;
        for (int g = 0; g < JOURNEY_GAMES; g++) if (j->opponent[s][g] < 0 || (j->opponent[s][g] == JOURNEY_FRIEND && !j->hasFriend)) ok = false;
        if (!ok) Schedule(j, s);
    }
    if (j->chances == 0 && !j->finished) j->finished = 1;
    return j->active || j->finished;
}

void Journey_Delete(void) {
    char path[700];
    if (DataPath(path, sizeof(path), false)) remove(path);
}
