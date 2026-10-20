#define _POSIX_C_SOURCE 200809L /* st_mtim */
#include "hallmusic.h"
#include "musicsynth.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

MusicMode HallMusic_Mode(HallMusicState s) {
    /* Unfocused covers a game started from the app menu, which the launcher
     * cannot see: the hall never plays under somebody else's music. */
    return s.enabled && s.focused && !s.coinInFlight && !s.gameRunning ? MUSIC_MENU : MUSIC_SILENT;
}

/* Background in a room you browse, not a game's own soundtrack. */
#define HALL_SHARE 60

int HallMusic_Volume(int sharedPercent) {
    if (sharedPercent < 0) sharedPercent = 0;
    if (sharedPercent > 100) sharedPercent = 100;
    return sharedPercent * HALL_SHARE / 100;
}

bool HallMusic_Enabled(const LauncherIni *ini) { return LauncherIni_GetBool(ini, "hall_music", true); }

bool HallMusic_Save(LauncherIni *ini, bool on) { return LauncherIni_Store(ini, "hall_music", on ? "on" : "off"); }

static long long Stamp(const char *path, const char *file) {
    struct stat st;
    long long t = stat(path, &st) == 0 ? (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec : 0;
    /* The file name too: two songs saved in the same instant still differ. */
    unsigned long long h = 1469598103934665603ULL;
    for (const char *c = file; *c; c++) h = (h ^ (unsigned char)*c) * 1099511628211ULL;
    long long v = t ^ (long long)(h & 0x7fffffffffffULL);
    return v ? v : 1; /* 0 means the built-in theme */
}

long long HallMusic_Stamp(void) {
    char path[700], file[SONG_FILE_LEN];
    if (!Song_Assigned(HALL_MUSIC_SLUG, path, sizeof(path))) return 0;
    Song_AssignedName(HALL_MUSIC_SLUG, file, sizeof(file));
    return Stamp(path, file);
}

void HallMusic_Song(HallSong *out) {
    memset(out, 0, sizeof(*out));
    char path[700];
    if (!Song_Assigned(HALL_MUSIC_SLUG, path, sizeof(path))) return; /* none, or its file is gone: the theme plays */
    Song_AssignedName(HALL_MUSIC_SLUG, out->file, sizeof(out->file));
    out->stamp = Stamp(path, out->file);
    static Song song; /* about 17 KB: off the stack; read only when the choice changes */
    /* music.c plays the theme when the file does not load, so it is not
     * named as the hall's song either (the stamp still marks the choice). */
    if (!Song_Load(&song, path)) return;
    out->custom = true;
    if (song.name[0]) snprintf(out->title, sizeof(out->title), "%s", song.name);
    else snprintf(out->title, sizeof(out->title), "%.*s", (int)sizeof(out->title) - 1, out->file);
}

void HallMusic_Tune(Song *out) {
    char path[700];
    if (Song_Assigned(HALL_MUSIC_SLUG, path, sizeof(path)) && Song_Load(out, path)) return;
    Music_ThemeToSong(Music_FindTheme(HALL_MUSIC_SLUG), out);
}

const char *HallMusic_SongWords(const HallSong *s) { return s->custom ? s->title : "Built-in theme"; }

int HallMusic_LibraryCount(void) {
    static char names[1024][SONG_FILE_LEN]; /* far past any real library */
    return Song_LibraryList(names, 1024);
}
