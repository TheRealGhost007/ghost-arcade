#include "look.h"

static bool sPatterns = false;

void Look_SetPatterns(bool on) { sPatterns = on; }
bool Look_Patterns(void) { return sPatterns; }

/* In tetromino.h's order: I O T S Z J L. */
static const char *const kPatterns[PIECE_COUNT][LOOK_PATTERN_N] = {
    {".....", ".....", "#####", ".....", "....."}, /* I: a bar across */
    {".....", ".###.", ".#.#.", ".###.", "....."}, /* O: a ring */
    {".....", "#####", "..#..", "..#..", "....."}, /* T: a T */
    {"....#", "...#.", "..#..", ".#...", "#...."}, /* S: a rising line */
    {"#....", ".#...", "..#..", "...#.", "....#"}, /* Z: a falling line */
    {"..#..", "..#..", "..#..", "..#..", "..#.."}, /* J: a bar down */
    {".....", ".#.#.", ".....", ".#.#.", "....."}, /* L: four dots */
};

bool Look_PatternCell(PieceType t, int x, int y) {
    if ((int)t < 0 || t >= PIECE_COUNT || x < 0 || y < 0 || x >= LOOK_PATTERN_N || y >= LOOK_PATTERN_N) return false;
    return kPatterns[t][y][x] == '#';
}
