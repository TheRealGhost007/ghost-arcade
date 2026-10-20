#ifndef HALLMUSIC_H
#define HALLMUSIC_H

#include <stdbool.h>
#include <stddef.h>
#include "music.h" /* MusicMode only: this module never plays anything */
#include "launcherini.h"
#include "song.h"

/* The hall's music, without raylib: when it plays, how loud, and what. The
 * launcher plays ghost-common's music (music.c) unchanged, with the
 * "ghostarcade" theme, or the Chipforge song music.ini gives that slug. */

#define HALL_MUSIC_SLUG "ghostarcade"

typedef struct {
    bool enabled;      /* Settings > Sound > Hall music */
    bool focused;      /* the launcher is the window in front */
    bool coinInFlight; /* the coin drops: the chime has the room */
    bool gameRunning;  /* a game (or Chipforge) the launcher started */
} HallMusicState;

/* The menu layer (bass, arpeggio, soft hats) only while the hall is in
 * front with nothing else going on; silent otherwise. music.c fades either
 * way, so a change is never a cut. */
MusicMode HallMusic_Mode(HallMusicState s);
/* The shared music volume, made softer for the hall (0-100). */
int HallMusic_Volume(int sharedPercent);

/* launcher.ini hall_music (on unless switched off). */
bool HallMusic_Enabled(const LauncherIni *ini);
bool HallMusic_Save(LauncherIni *ini, bool on);

/* What the hall plays: the built-in theme, or the player's song. stamp
 * changes whenever the choice or the song's file does, so the launcher
 * knows when to load it again (after Chipforge, say). */
typedef struct {
    bool custom;              /* the player's song loads, so it is what plays */
    char file[SONG_FILE_LEN];
    char title[SONG_NAME_LEN];
    long long stamp;
} HallSong;

void HallMusic_Song(HallSong *out);
/* Just the stamp (0 for the built-in theme): cheap enough to ask whenever
 * the player may have been in Chipforge, before reading the song itself. */
long long HallMusic_Stamp(void);
/* The song itself, as music.c plays it: the player's song when it loads,
 * else the hall's theme composed into one (the jukebox shows its notes). */
void HallMusic_Tune(Song *out);
/* "Built-in theme", or the song's title (its file name if it has none). */
const char *HallMusic_SongWords(const HallSong *s);
/* .gsong files in the player's library (Song_LibraryList's count). */
int HallMusic_LibraryCount(void);

#endif
