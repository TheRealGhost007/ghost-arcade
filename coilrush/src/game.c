#include "game.h"
#include <string.h>

static const int kDirDX[4] = {0, 1, 0, -1};
static const int kDirDY[4] = {-1, 0, 1, 0};

static Dir Opposite(Dir d) { return (Dir)((d + 2) % 4); }

const char *Game_ModeName(GameMode mode) {
    switch (mode) {
        case MODE_CLASSIC: return "CLASSIC";
        case MODE_WRAP: return "WRAP";
        case MODE_MAZE: return "MAZE";
        default: return "?";
    }
}

const char *Game_ModeKey(GameMode mode) {
    switch (mode) {
        case MODE_CLASSIC: return "classic";
        case MODE_WRAP: return "wrap";
        case MODE_MAZE: return "maze";
        default: return "unknown";
    }
}

float Game_StepIntervalForLevel(int level) {
    float interval = 0.150f - 0.008f * (float)(level - 1);
    if (interval < 0.055f) interval = 0.055f;
    return interval;
}

int Game_LayoutForStage(int stage) {
    if (stage < 1) stage = 1;
    return (stage - 1) % MAZE_LAYOUT_COUNT;
}

static bool InRange(int v, int lo, int hi) { return v >= lo && v <= hi; }

/* Every layout keeps row SPAWN_Y clear on the left half so the snake always
 * has a free run-up after spawning, and leaves all open cells connected
 * (both properties are enforced by the test suite). */
bool Maze_IsWall(int layout, int x, int y) {
    if (x < 0 || x >= GRID_W || y < 0 || y >= GRID_H) return false;

    switch (layout) {
        case 0: /* corner brackets */
            if ((y == 4 || y == 19) && (InRange(x, 4, 8) || InRange(x, 15, 19))) return true;
            if ((x == 4 || x == 19) && (InRange(y, 4, 8) || InRange(y, 15, 19))) return true;
            return false;
        case 1: /* two long bars */
            return (y == 7 || y == 16) && InRange(x, 5, 18);
        case 2: /* four pillars */
            return (x == 8 || x == 15) && (InRange(y, 3, 9) || InRange(y, 14, 20));
        case 3: { /* box with a doorway in the middle of each side */
            bool onH = (y == 7 || y == 16) && InRange(x, 7, 16);
            bool onV = (x == 7 || x == 16) && InRange(y, 7, 16);
            if (onH && (x == 11 || x == 12)) return false;
            if (onV && (y == 11 || y == 12)) return false;
            return onH || onV;
        }
        case 4: /* four solid blocks */
            return (InRange(x, 5, 8) || InRange(x, 15, 18)) &&
                   (InRange(y, 5, 8) || InRange(y, 15, 18));
        case 5: /* staggered dashes */
            if (y == 4 || y == 14) return InRange(x, 2, 9) || InRange(x, 14, 21);
            if (y == 9 || y == 19) return InRange(x, 8, 15);
            return false;
        default:
            return false;
    }
}

Cell Game_Segment(const Game *g, int i) {
    return g->body[(g->headIdx - i + MAX_SNAKE) % MAX_SNAKE];
}

/* Picks a uniformly random empty cell. Returns false when the board is
 * completely full. */
static bool RandomEmptyCell(Game *g, Cell *out) {
    int empties = 0;
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            if (g->grid[y][x] == CELL_EMPTY) empties++;
        }
    }
    if (empties == 0) return false;

    int pick = (int)Rng_Range(&g->rng, (uint32_t)empties);
    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            if (g->grid[y][x] != CELL_EMPTY) continue;
            if (pick-- == 0) {
                out->x = (int8_t)x;
                out->y = (int8_t)y;
                return true;
            }
        }
    }
    return false;
}

static bool PlaceFood(Game *g) {
    Cell c;
    if (!RandomEmptyCell(g, &c)) return false;
    g->food = c;
    g->grid[c.y][c.x] = CELL_FOOD;
    return true;
}

const char *Game_RelicName(RelicType t) {
    static const char *const kNames[RELIC_COUNT] = {"PHASE", "SLOW", "SURGE"};
    return (t >= 0 && t < RELIC_COUNT) ? kNames[t] : "";
}

float Game_CurrentInterval(const Game *g) {
    return g->stepInterval * (g->slowSteps > 0 ? SLOW_FACTOR : 1.0f);
}

static int ScoreMult(const Game *g) { return g->surgeSteps > 0 ? SURGE_MULT : 1; }

static void RemoveRelic(Game *g) {
    if (!g->relicActive) return;
    g->grid[g->relic.y][g->relic.x] = CELL_EMPTY;
    g->relicActive = false;
    g->relicStepsLeft = 0;
}

static void RemoveBonus(Game *g) {
    if (!g->bonusActive) return;
    g->grid[g->bonus.y][g->bonus.x] = CELL_EMPTY;
    g->bonusActive = false;
    g->bonusStepsLeft = 0;
}

/* Lays out walls for the current mode/level, puts a fresh snake on the
 * spawn lane heading right, and drops the first food. Score and level are
 * left alone so maze stages can carry them over. */
static void LoadBoard(Game *g) {
    memset(g->grid, CELL_EMPTY, sizeof(g->grid));

    if (g->mode == MODE_MAZE) {
        int layout = Game_LayoutForStage(g->level);
        for (int y = 0; y < GRID_H; y++) {
            for (int x = 0; x < GRID_W; x++) {
                if (Maze_IsWall(layout, x, y)) g->grid[y][x] = CELL_WALL;
            }
        }
    }

    g->length = START_LENGTH;
    g->headIdx = START_LENGTH - 1;
    for (int i = 0; i < START_LENGTH; i++) {
        int x = SPAWN_HEAD_X - (START_LENGTH - 1) + i;
        g->body[i] = (Cell){(int8_t)x, SPAWN_Y};
        g->grid[SPAWN_Y][x] = CELL_SNAKE;
    }
    g->growPending = 0;
    g->dir = DIR_RIGHT;
    g->turnCount = 0;

    g->bonusActive = false;
    g->bonusStepsLeft = 0;
    g->relicActive = false;
    g->relicStepsLeft = 0;
    g->phaseSteps = g->slowSteps = g->surgeSteps = 0;
    g->stageFood = 0;

    g->stepTimer = 0.0f;
    g->stepInterval = Game_StepIntervalForLevel(g->level);
    g->startDelay = START_DELAY_SECONDS;

    g->stepsOnBoard = 0;
    g->tailMoved = false;
    g->stepsSinceEat = 1000;
    for (int i = 0; i < MAX_BULGES; i++) g->bulgeAge[i] = -1;

    PlaceFood(g);
}

void Game_Init(Game *g, uint64_t seed, GameMode mode, long highScore) {
    memset(g, 0, sizeof(*g));
    Rng_Seed(&g->rng, seed);
    g->mode = mode;
    g->highScore = highScore;
    g->phase = GS_PLAYING;
    g->deathCause = DEATH_NONE;
    g->level = 1;
    LoadBoard(g);
}

void Game_Restart(Game *g, uint64_t seed) {
    long highScore = g->highScore > g->score ? g->highScore : g->score;
    Game_Init(g, seed, g->mode, highScore);
}

bool Game_QueueTurn(Game *g, Dir d) {
    if (g->phase != GS_PLAYING) return false;
    if (g->turnCount >= TURN_QUEUE_LEN) return false;

    /* Compare against where the snake will be heading once the already
     * queued turns have played out, not just its current direction. */
    Dir reference = g->turnCount > 0 ? g->turnQueue[g->turnCount - 1] : g->dir;
    if (d == reference || d == Opposite(reference)) return false;

    g->turnQueue[g->turnCount++] = d;
    return true;
}

static void Die(Game *g, DeathCause cause) {
    g->phase = GS_GAMEOVER;
    g->deathCause = cause;
    g->justDied = true;
    if (g->score > g->highScore) g->highScore = g->score;
}

static void Win(Game *g) {
    g->phase = GS_GAMEOVER;
    g->won = true;
    g->justDied = true;
    if (g->score > g->highScore) g->highScore = g->score;
}

static void NotePickup(Game *g, Cell where, bool wasBonus) {
    g->eatCount++;
    g->lastEatCell = where;
    g->lastEatWasBonus = wasBonus;
    g->stepsSinceEat = 0;
    for (int i = 0; i < MAX_BULGES; i++) {
        if (g->bulgeAge[i] < 0) {
            g->bulgeAge[i] = 0;
            break;
        }
    }
}

static void OnAteFood(Game *g) {
    g->score += 10L * g->level * ScoreMult(g);
    g->foodEaten++;
    g->growPending++;
    g->justAte = true;

    if (g->mode == MODE_MAZE) {
        g->stageFood++;
        if (g->stageFood >= FOOD_PER_STAGE) {
            g->level++;
            g->justLeveledUp = true;
            LoadBoard(g);
            return;
        }
    } else {
        int newLevel = 1 + g->foodEaten / FOOD_PER_LEVEL;
        if (newLevel > g->level) {
            g->level = newLevel;
            g->stepInterval = Game_StepIntervalForLevel(g->level);
            g->justLeveledUp = true;
        }
    }

    if (!PlaceFood(g)) {
        Win(g);
        return;
    }

    if (g->foodEaten % BONUS_EVERY == 0 && !g->bonusActive) {
        Cell c;
        if (RandomEmptyCell(g, &c)) {
            g->bonus = c;
            g->grid[c.y][c.x] = CELL_BONUS;
            g->bonusActive = true;
            g->bonusStepsLeft = BONUS_LIFETIME_STEPS;
            g->justBonusSpawned = true;
        }
    }

    if (g->foodEaten % RELIC_EVERY == 0 && !g->relicActive) {
        Cell c;
        if (RandomEmptyCell(g, &c)) {
            g->relic = c;
            g->grid[c.y][c.x] = CELL_RELIC;
            g->relicActive = true;
            g->relicType = (RelicType)Rng_Range(&g->rng, RELIC_COUNT);
            g->relicStepsLeft = RELIC_LIFETIME_STEPS;
            g->justRelicSpawned = true;
        }
    }
}

/* Is any body segment other than the tail-end one at (x, y)? Used because a
 * phasing snake can lie on top of itself, so a cell only empties when its
 * LAST occupant leaves. */
static bool OccupiedByOther(const Game *g, int x, int y, int skipFrom) {
    for (int i = 0; i < skipFrom; i++) {
        Cell s = Game_Segment(g, i);
        if (s.x == x && s.y == y) return true;
    }
    return false;
}

void Game_Step(Game *g) {
    if (g->phase != GS_PLAYING) return;

    if (g->turnCount > 0) {
        g->dir = g->turnQueue[0];
        for (int i = 1; i < g->turnCount; i++) g->turnQueue[i - 1] = g->turnQueue[i];
        g->turnCount--;
    }

    Cell head = Game_Segment(g, 0);
    int nx = head.x + kDirDX[g->dir];
    int ny = head.y + kDirDY[g->dir];

    if (g->mode == MODE_WRAP) {
        nx = (nx + GRID_W) % GRID_W;
        ny = (ny + GRID_H) % GRID_H;
    } else if (nx < 0 || nx >= GRID_W || ny < 0 || ny >= GRID_H) {
        Die(g, DEATH_EDGE);
        return;
    }

    /* The tail moves out of its cell on the same step the head moves in, so
     * chasing your own tail tip is legal -- vacate it before the collision
     * check (unless the snake is growing, in which case the tail stays). */
    bool growing = g->growPending > 0;
    Cell tail = Game_Segment(g, g->length - 1);
    bool tailShared = !growing && OccupiedByOther(g, tail.x, tail.y, g->length - 1);
    if (!growing && !tailShared) g->grid[tail.y][tail.x] = CELL_EMPTY;

    uint8_t target = g->grid[ny][nx];
    bool phasing = g->phaseSteps > 0 && target == CELL_SNAKE;
    if ((target == CELL_SNAKE && !phasing) || target == CELL_WALL) {
        if (!growing && !tailShared) g->grid[tail.y][tail.x] = CELL_SNAKE; /* keep grid/body in sync for the death frame */
        Die(g, target == CELL_WALL ? DEATH_WALL : DEATH_SELF);
        return;
    }

    g->headIdx = (g->headIdx + 1) % MAX_SNAKE;
    g->body[g->headIdx] = (Cell){(int8_t)nx, (int8_t)ny};
    g->grid[ny][nx] = CELL_SNAKE;
    if (growing) {
        g->length++;
        g->growPending--;
    }

    g->stepsOnBoard++;
    g->prevTail = tail;
    g->tailMoved = !growing;
    if (g->stepsSinceEat < 1000) g->stepsSinceEat++;
    for (int i = 0; i < MAX_BULGES; i++) {
        if (g->bulgeAge[i] >= 0 && ++g->bulgeAge[i] >= g->length) g->bulgeAge[i] = -1;
    }

    if (target == CELL_FOOD) {
        NotePickup(g, (Cell){(int8_t)nx, (int8_t)ny}, false);
        OnAteFood(g);
        return; /* a maze stage change may have rebuilt the board */
    }

    if (target == CELL_RELIC) {
        g->lastRelic = g->relicType;
        g->relicActive = false;
        g->relicStepsLeft = 0;
        g->justRelic = true;
        if (g->relicType == RELIC_PHASE) g->phaseSteps = PHASE_STEPS;
        else if (g->relicType == RELIC_SLOW) g->slowSteps = SLOW_STEPS;
        else g->surgeSteps = SURGE_STEPS;
    }

    /* Spells tick down per step; when phasing ends with the head still
     * inside its own body, that is the end of the run. */
    if (g->slowSteps > 0) g->slowSteps--;
    if (g->surgeSteps > 0) g->surgeSteps--;
    if (g->phaseSteps > 0 && --g->phaseSteps == 0) {
        g->justPhaseEnded = true;
        for (int i = 1; i < g->length; i++) {
            Cell s = Game_Segment(g, i);
            if (s.x == nx && s.y == ny) { Die(g, DEATH_SELF); return; }
        }
    }
    if (g->relicActive && --g->relicStepsLeft <= 0) RemoveRelic(g);

    if (target == CELL_BONUS) {
        NotePickup(g, (Cell){(int8_t)nx, (int8_t)ny}, true);
        g->score += (long)g->level * (20 + 2 * g->bonusStepsLeft) * ScoreMult(g);
        g->bonusActive = false;
        g->bonusStepsLeft = 0;
        g->justAteBonus = true;
        return;
    }

    if (g->bonusActive && --g->bonusStepsLeft <= 0) RemoveBonus(g);
}

void Game_Update(Game *g, float dt) {
    if (g->phase != GS_PLAYING) return;
    if (dt > 0.25f) dt = 0.25f; /* a long hitch shouldn't fast-forward the snake into a wall */

    if (g->startDelay > 0.0f) {
        g->startDelay -= dt;
        return;
    }

    g->stepTimer += dt;
    while (g->stepTimer >= Game_CurrentInterval(g) && g->phase == GS_PLAYING && g->startDelay <= 0.0f) {
        g->stepTimer -= Game_CurrentInterval(g);
        Game_Step(g);
    }
}

void Game_TogglePause(Game *g) {
    if (g->phase == GS_PLAYING) g->phase = GS_PAUSED;
    else if (g->phase == GS_PAUSED) g->phase = GS_PLAYING;
}

void Game_ConsumeFrameFlags(Game *g) {
    g->justAte = false;
    g->justAteBonus = false;
    g->justBonusSpawned = false;
    g->justLeveledUp = false;
    g->justDied = false;
    g->justRelicSpawned = g->justRelic = g->justPhaseEnded = false;
}
