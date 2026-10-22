#include "teams.h"
#include "game.h"

static const TeamStyle kCrew = {
    "Rink Rats", {196, 44, 52}, {244, 240, 232}, {170, 30, 40}, {"Zippy", "Sam", "Tank"},
    {TRAIT_NONE, TRAIT_NONE, TRAIT_NONE}, "Mo", GOALIE_BUTTERFLY,
};

static const TeamStyle kRivals[RIVAL_COUNT] = {
    {"Snowplows", {236, 186, 40}, {36, 32, 40}, {40, 38, 46}, {"Dash", "Kit", "Moose"},
     {TRAIT_DANGLER, TRAIT_PLAYMAKER, TRAIT_WALL}, "Plow", GOALIE_SCRAMBLER},
    {"Icicles", {150, 208, 240}, {250, 252, 255}, {96, 160, 214}, {"Flurry", "Rory", "Boulder"},
     {TRAIT_SNIPER, TRAIT_PLAYMAKER, TRAIT_CANNON}, "Frost", GOALIE_BUTTERFLY},
    {"Mitten Gang", {58, 146, 84}, {238, 228, 196}, {200, 60, 50}, {"Pip", "Jo", "Hank"},
     {TRAIT_PEST, TRAIT_SNIPER, TRAIT_WALL}, "Knit", GOALIE_POKER},
    {"Frostbites", {128, 78, 188}, {80, 214, 200}, {60, 40, 110}, {"Nix", "Bea", "Grizz"},
     {TRAIT_DANGLER, TRAIT_SNIPER, TRAIT_CANNON}, "Chill", GOALIE_BUTTERFLY},
    {"Toque Squad", {238, 128, 40}, {30, 44, 88}, {30, 44, 88}, {"Skip", "Lou", "Duke"},
     {TRAIT_PEST, TRAIT_PLAYMAKER, TRAIT_CANNON}, "Bobble", GOALIE_SCRAMBLER},
};

const TeamStyle *Team_Crew(void) { return &kCrew; }

const TeamStyle *Team_Rival(int index) {
    int i = index % RIVAL_COUNT;
    if (i < 0) i += RIVAL_COUNT;
    return &kRivals[i];
}

const TeamStyle *Team_ForSide(int side, int rivalIndex) { return side == 0 ? Team_Crew() : Team_Rival(rivalIndex); }
