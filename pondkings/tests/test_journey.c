/* The Journey (journey.c): schedule, line-ups, results, signing, grudges,
 * the final and the save file. Compiled by inclusion from test_main.c (it
 * shares CHECK); the save tests run in a throwaway XDG_DATA_HOME. */

static void FakeResult(Game *g, int us, int them) {
    memset(g, 0, sizeof(*g));
    g->phase = PH_OVER;
    g->score[0] = us;
    g->score[1] = them;
    g->winner = (int8_t)(us > them ? 0 : 1);
}

static void test_journey_basics(void) {
    Journey j;
    Journey_New(&j, 4242, NULL);
    CHECK(j.active && !j.finished && j.chances == JOURNEY_CHANCES && j.stage == 0 && j.game == 0, "journey: a fresh run");
    CHECK(strcmp(j.crew[0].name, "Zippy") == 0 && strcmp(j.crew[2].name, "Tank") == 0 && j.crew[1].build == BUILD_BALANCED,
          "journey: the Rink Rats as they start");
    bool distinct = j.opponent[0][0] != j.opponent[0][1] && j.opponent[0][1] != j.opponent[0][2] && j.opponent[0][0] != j.opponent[0][2];
    bool inRange = true;
    for (int g = 0; g < JOURNEY_GAMES; g++) if (j.opponent[0][g] < 0 || j.opponent[0][g] >= RIVAL_COUNT) inRange = false;
    CHECK(distinct && inRange && j.opponent[1][0] == -1 && j.opponent[2][0] == -1, "journey: three different pond teams, later stages drawn later");

    GameSetup s;
    int8_t slots[MAX_PLAYERS] = {1, 0, -1, -1}; /* a player who picked the rivals still plays for the crew */
    Journey_Setup(&j, &s, slots, 99, 2);
    CHECK(s.venue == VENUE_POND && s.periodSeconds == 120 && s.slotTeam[0] == 0 && s.slotTeam[1] == 0 && s.slotTeam[2] == -1,
          "journey: the first games are on the pond, every player on the crew");
    const TeamStyle *t = Team_Rival(Journey_Opponent(&j));
    CHECK(s.trait[1][0] == t->traits[0] && s.goalieStyle[1] == t->goalieStyle && s.teamLevel[1] == DIFF_EASY,
          "journey: the rivals bring their own moves, and the opener is gentle");

    /* a big win: points, the next game, a grudge, and a signing */
    int beaten = Journey_Opponent(&j);
    Game g;
    FakeResult(&g, 5, 1);
    JourneyResult r;
    Journey_Result(&j, &g, &r);
    CHECK(r.won && r.points > 0 && j.wins == 1 && j.game == 1 && j.canSign && j.lastOpponent == beaten, "journey: a win moves you on");
    CHECK(j.grudge[beaten] == 2, "journey: a heavy defeat leaves a big grudge");
    CrewKid options[TEAM_SIZE];
    Journey_SignOptions(&j, options);
    CHECK(strcmp(options[0].name, t->kids[0]) == 0 && options[0].trait == t->traits[0] && options[0].from == beaten,
          "journey: you may sign the kids you just beat");
    Journey_Sign(&j, 2, 1);
    CHECK(!j.canSign && strcmp(j.crew[1].name, t->kids[2]) == 0 && j.crew[1].trait == t->traits[2] && j.crew[1].build == BUILD_BIG,
          "journey: signing puts them in your line-up");
    Journey_Sign(&j, 0, 0);
    CHECK(strcmp(j.crew[0].name, "Zippy") == 0, "journey: one signing per win");

    /* losses cost chances; the same game is played again */
    int stage = j.stage, game = j.game;
    FakeResult(&g, 1, 3);
    Journey_Result(&j, &g, &r);
    CHECK(!r.won && j.chances == JOURNEY_CHANCES - 1 && j.stage == stage && j.game == game && !j.canSign, "journey: a loss costs a chance");
    Journey_Result(&j, &g, &r);
    Journey_Result(&j, &g, &r);
    CHECK(r.out && j.finished && !j.active && !j.champion, "journey: three losses and the run is over");
}

static void test_journey_road(void) {
    Journey j;
    CrewCard friendCrew;
    memset(&friendCrew, 0, sizeof(friendCrew));
    snprintf(friendCrew.name, sizeof(friendCrew.name), "Ghost's Rink Rats");
    for (int k = 0; k < TEAM_SIZE; k++) {
        snprintf(friendCrew.kids[k].name, JOURNEY_NAME_LEN, "Pal%d", k);
        friendCrew.kids[k].build = (uint8_t)(2 - k);
        friendCrew.kids[k].trait = TRAIT_SNIPER;
    }
    friendCrew.lean.shoot = 20;
    friendCrew.lean.side = 1;
    Journey_New(&j, 777, &friendCrew);
    Game g;
    JourneyResult r;
    bool venues = true, stageOneRight = false, levels = true;
    for (int n = 0; n < JOURNEY_STAGES * JOURNEY_GAMES; n++) {
        GameSetup s;
        int8_t slots[MAX_PLAYERS] = {0, -1, -1, -1};
        Journey_Setup(&j, &s, slots, (uint64_t)n, 1);
        int wantVenue = n < 3 ? VENUE_POND : n < 6 ? VENUE_RINK : VENUE_ARENA;
        if (s.venue != wantVenue) venues = false;
        if (s.teamLevel[1] >= DIFF_COUNT) levels = false;
        if (n == 3) {
            /* the town league: the two you haven't met, then the one with the biggest grudge */
            bool met = false;
            for (int k = 0; k < 2; k++)
                for (int m = 0; m < JOURNEY_GAMES; m++) if (j.opponent[1][k] == j.opponent[0][m]) met = true;
            int most = 0;
            for (int x = 0; x < RIVAL_COUNT; x++) if (j.grudge[x] > j.grudge[most]) most = x;
            stageOneRight = !met && j.grudge[j.opponent[1][2]] == j.grudge[most];
        }
        if (n == 8) {
            CHECK(Journey_Opponent(&j) == JOURNEY_FRIEND && strcmp(Journey_OpponentName(&j, JOURNEY_FRIEND), "Ghost's Rink Rats") == 0,
                  "journey: a friend's crew is the final");
            CHECK(s.trait[1][0] == TRAIT_SNIPER && s.build[1][0] == BUILD_BIG && s.lean[1].shoot == 20 && s.lean[1].side == 1 && s.teamLevel[1] == DIFF_HARD,
                  "journey: playing the way your friend plays");
            CHECK(strcmp(Journey_GameName(&j), "The final") == 0, "journey: it's called the final");
        }
        FakeResult(&g, 4, 1 + (n % 2));
        Journey_Result(&j, &g, &r);
        if (j.canSign) Journey_Pass(&j);
    }
    CHECK(venues, "journey: the pond, then the town rink, then the arena");
    CHECK(stageOneRight, "journey: the town league brings back the team with the grudge");
    CHECK(levels, "journey: rival levels stay in range");
    CHECK(r.champion && j.champion && j.finished && j.wins == 9 && Journey_Level(&j) == 9, "journey: nine wins and you're the Pond Kings");
    CHECK(j.points > 9 * 300 + 1000, "journey: points add up, with a bonus for the title");

    /* a grudge makes them sharper and rougher */
    Journey k;
    Journey_New(&k, 5, NULL);
    int opp = Journey_Opponent(&k);
    k.grudge[opp] = 4;
    GameSetup s;
    int8_t slots[MAX_PLAYERS] = {0, -1, -1, -1};
    Journey_Setup(&k, &s, slots, 1, 2);
    CHECK(s.teamLevel[1] == DIFF_NORMAL && s.lean[1].check == 25, "journey: a team with a grudge plays a level up and checks more");
}

static void test_journey_file(void) {
    char dir[] = "/tmp/pondkings-journey-XXXXXX";
    if (!mkdtemp(dir)) { CHECK(false, "journey file: temp dir"); return; }
    char *old = getenv("XDG_DATA_HOME");
    char saved[512] = "";
    if (old) snprintf(saved, sizeof(saved), "%s", old);
    setenv("XDG_DATA_HOME", dir, 1);

    Journey a, b;
    CHECK(!Journey_Load(&b), "journey file: nothing saved yet");
    Journey_New(&a, 31337, NULL);
    Game g;
    JourneyResult r;
    FakeResult(&g, 3, 0);
    Journey_Result(&a, &g, &r);
    Journey_Sign(&a, 1, 0);
    CHECK(Journey_Save(&a) && Journey_Load(&b), "journey file: saved and read back");
    bool same = b.active == a.active && b.stage == a.stage && b.game == a.game && b.chances == a.chances && b.points == a.points &&
                b.wins == a.wins && b.seed == a.seed && memcmp(b.grudge, a.grudge, sizeof(a.grudge)) == 0 &&
                memcmp(b.opponent, a.opponent, sizeof(a.opponent)) == 0;
    for (int k = 0; k < TEAM_SIZE; k++)
        if (strcmp(a.crew[k].name, b.crew[k].name) || a.crew[k].build != b.crew[k].build || a.crew[k].trait != b.crew[k].trait || a.crew[k].from != b.crew[k].from)
            same = false;
    CHECK(same, "journey file: everything comes back the same");

    /* a hand-edited file: out-of-range values are clamped, nonsense ignored */
    char path[600];
    snprintf(path, sizeof(path), "%s/pondkings/journey.txt", dir);
    FILE *f = fopen(path, "w");
    if (f) {
        fputs("active=1\nstage=7\ngame=-3\nchances=99\nkid0=Ha|ck=er,9,99,42\ngrudge=9,-2,x\nopp0=50,-7,2\nnonsense\n", f);
        fclose(f);
    }
    CHECK(Journey_Load(&b), "journey file: a hand-edited file still loads");
    CHECK(b.stage == JOURNEY_STAGES - 1 && b.game == 0 && b.chances == JOURNEY_CHANCES && b.crew[0].build == BUILD_COUNT - 1 &&
              b.crew[0].trait == TRAIT_COUNT - 1 && b.crew[0].from == JOURNEY_FRIEND && strcmp(b.crew[0].name, "Hacker") == 0 && b.grudge[0] == 5 && b.grudge[1] == 0,
          "journey file: every value is kept in range");
    bool drawn = true;
    for (int s = 0; s <= b.stage; s++)
        for (int k = 0; k < JOURNEY_GAMES; k++) if (b.opponent[s][k] < 0 || b.opponent[s][k] >= RIVAL_COUNT) drawn = false;
    CHECK(drawn, "journey file: a broken schedule is drawn again");
    Journey_Delete();
    CHECK(!Journey_Load(&b), "journey file: deleted");
    rmdir(path); /* (a file, so this does nothing; the folder goes below) */
    char sub[600];
    snprintf(sub, sizeof(sub), "%s/pondkings", dir);
    rmdir(sub);
    rmdir(dir);
    if (old) setenv("XDG_DATA_HOME", saved, 1);
    else unsetenv("XDG_DATA_HOME");
}
