#ifndef CAVE_H
#define CAVE_H

#include <stdbool.h>

/* A cave is a small text file:
 *
 *   name=Loose Ends
 *   gems=12            gems needed to open the exit
 *   time=150           seconds
 *   solution=LLDDR.U   one character per tick: U D L R or . (wait)
 *   map
 *   <22 rows of 40 characters>
 *
 * Map characters:  # steel wall   B brick wall   : dirt   . empty
 *                  o boulder      * gem          P start  X exit
 *                  f crawler that blows up into nothing
 *                  g crawler that blows up into gems
 *
 * The solution is recorded by the cave generator's bot and replayed by the
 * test suite, so a cave that ships is a cave that has been finished. */
#define CAVE_COLS 40
#define CAVE_ROWS 22
#define CAVE_SOLUTION_MAX 6000
#define CAVE_NAME_MAX 40

typedef struct {
    char name[CAVE_NAME_MAX];
    int quota;
    int timeLimit;
    char solution[CAVE_SOLUTION_MAX];
    char map[CAVE_ROWS][CAVE_COLS + 1];
} Cave;

/* Parses cave text. On failure returns false and, if err is not NULL, a short reason. */
bool Cave_Parse(const char *text, Cave *out, const char **err);
bool Cave_LoadFile(const char *path, Cave *out, const char **err);
/* Writes a cave in the format above (used by the generator). */
bool Cave_Write(const char *path, const Cave *cave);

#endif
