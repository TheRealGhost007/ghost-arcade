#ifndef RECIPE_H
#define RECIPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "song.h"
#include "musicsynth.h"
#include "compose.h"

/* "Make a song": a handful of plain choices (a mood, a speed, a story told
 * by the chords, how the bass, the sparkle and the beat move, the sound, a
 * tune number) mapped onto the composer (compose.h). People who do not write
 * music can say what they like far more easily than write it, so the recipe
 * screen offers these as rows and recomposes the song live on every change.
 *
 * No raylib: main.c draws the rows and plays the song, everything that
 * decides the music is here and tested headless. A theme maps onto a recipe
 * exactly (Recipe_FromTheme), so "make one like Skyraid" starts from the very
 * song the game ships. */

/* The rows, in screen order. */
typedef enum {
    RF_MOOD,     /* the scale: Happy (major) .. Mysterious (harmonic minor) */
    RF_SPEED,    /* Slow 84 .. Frantic 168 */
    RF_STORY,    /* the chord progression, by name (compose.h's table) */
    RF_BASS,     /* bass style */
    RF_SPARKLE,  /* arp style */
    RF_BEAT,     /* drum style */
    RF_SOUND,    /* the three waves: Classic, Soft, Buzzy */
    RF_MELODY,   /* the tune's number: the lead writer's seed */
    RF_LENGTH,   /* the one pattern looped, or grown into a song (arrange.h): Loop .. Long */
    RF_HEAR,     /* how much of the song to hear: what a game plays when calm .. everything */
    RF_COUNT
} RecipeField;

#define RECIPE_MOODS 6
#define RECIPE_SPEEDS 5
#define RECIPE_SOUNDS 3
#define RECIPE_HEARS 3
#define RECIPE_LENGTHS 4
#define RECIPE_MELODY_MIN 1
#define RECIPE_MELODY_MAX 999

typedef struct {
    /* One value per row; each is an index into that row's choices. */
    int mood;       /* 0..RECIPE_MOODS-1 */
    int speed;      /* 0..RECIPE_SPEEDS-1 */
    int story;      /* 0..Compose_ProgressionCount()-1 */
    int bass;       /* 0..Compose_StyleCount(TRACK_BASS)-1 */
    int sparkle;    /* 0..Compose_StyleCount(TRACK_ARP)-1 */
    int beat;       /* 0..Compose_StyleCount(TRACK_DRUMS)-1 */
    int sound;      /* 0..RECIPE_SOUNDS-1 */
    int melody;     /* RECIPE_MELODY_MIN..RECIPE_MELODY_MAX */
    int length;     /* 0 Loop (one pattern), 1 Short, 2 Medium, 3 Long (Arrange_Grow's
                     * Short loop, Game loop and Long loop on that pattern) */
    int hear;       /* 0 calm, 1 busy, 2 intense */

    /* Not rows, but part of the song, so a theme can be reproduced exactly
     * and a roll can land anywhere in the safe register. */
    int tonicMidi;  /* the key's tonic in the bass register: 40..51 from a roll, a theme's own rootMidi */
    int bpm;        /* the Speed row's tempo, or a theme's own (the row then names the nearest speed) */
    float swing;    /* follows the Beat row; a theme's own until that row changes */
    int leadOctave; /* 3 for a roll (the tune an octave above the bass's tonic); a theme's own */
    uint32_t beatSeed; /* the ghost notes of the Shuffle and Gallop beats */
} Recipe;

/* Moody, Steady 104, Hero's anthem, Steady, Up, Dance, Classic, tune 17,
 * Loop, Intense, in A minor: a known starting point. */
void Recipe_Default(Recipe *r);
/* Every row (Length set to Loop, Hear it as to Intense) and the key root
 * chosen from the seed; deterministic per seed. */
void Recipe_Random(Recipe *r, uint32_t seed);
/* "Roll everything": a new random recipe that keeps the Length and Hear it
 * as rows (how long a song you want and how you listen are not what a roll
 * is for) and always differs from the one it replaces in at least one
 * musical field. */
void Recipe_Roll(Recipe *r, uint32_t seed);
/* A new tune number, never the current one. */
void Recipe_RollMelody(Recipe *r, uint32_t seed);
/* The recipe a theme was made from: Recipe_Compose then reproduces the
 * theme's pattern 0, tempo, swing, key and voices exactly. Length is Loop
 * and Hear it as Intense. Returns false (and still fills a usable recipe, on the theme's
 * key and styles) when the theme's progression is not in compose.h's table. */
bool Recipe_FromTheme(Recipe *r, const MusicTheme *t);

/* Left/Right on a row: the next or previous choice, wrapping. Speed also
 * sets the tempo to that speed's; Beat also sets its swing. Hear it as,
 * Length and Melody change nothing else. */
void Recipe_Step(Recipe *r, RecipeField f, int delta);

/* The row's label ("Mood" .. "Hear it as") and its current value as shown
 * between the arrows ("Moody", "Steady 104", "Hero's anthem", "17", and for
 * Length how long that makes the song at its tempo: "Loop 9 s", "Medium
 * 1:14"). */
const char *Recipe_FieldLabel(RecipeField f);
void Recipe_FieldValue(const Recipe *r, RecipeField f, char *out, size_t size);
/* "A minor at 104 bpm, about 9 s" (or "about 1:14" once Length grows it):
 * the note line over the rows. */
void Recipe_Note(const Recipe *r, char *out, size_t size);

ComposeKey Recipe_Key(const Recipe *r);
/* The player intensity for the Hear it as row: Calm 0.15 (bass and arp
 * only, the synth brings hats in above 0.15), Busy 0.45, Intense 1.0. */
float Recipe_Intensity(const Recipe *r);

/* Composes the recipe into a song named "New song": pattern 0, tempo, swing,
 * key and the Sound row's waves; volumes at 100. At Loop that is the whole
 * song; Short, Medium and Long then grow pattern 0 into an arrangement
 * (Arrange_Grow: 4, 8 or 16 passes over 3, 7 or 7 patterns), pattern 0
 * untouched and first in the order. Deterministic. */
void Recipe_Compose(const Recipe *r, Song *out);

#endif
