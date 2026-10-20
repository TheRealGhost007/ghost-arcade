#ifndef BROWSER_H
#define BROWSER_H

#include <stdbool.h>
#include <stddef.h>

/* A plain folder listing for picking a recording to import: subfolders first,
 * then files with the wanted extensions, both sorted, hidden entries skipped.
 * No raylib (POSIX dirent), so it is tested headless. */

#define BROWSER_MAX 512
#define BROWSER_NAME_LEN 128
#define BROWSER_PATH_LEN 1024

typedef struct {
    char dir[BROWSER_PATH_LEN];
    char names[BROWSER_MAX][BROWSER_NAME_LEN];
    bool isDir[BROWSER_MAX];
    int count;
    int selected;
    bool truncated;   /* more entries than BROWSER_MAX */
} Browser;

/* Lists dir. exts are lower-case without the dot ("mp3", "wav"); NULL/0 lists
 * every file. Returns false (and leaves an empty listing with dir set) if the
 * folder cannot be read. */
bool Browser_Open(Browser *b, const char *dir, const char *const *exts, int extCount);
/* Re-reads the current folder, keeping the selection where it can. */
bool Browser_Refresh(Browser *b, const char *const *exts, int extCount);
void Browser_MoveSelection(Browser *b, int delta);
/* Enter on the selection: a subfolder is opened (returns false); a file's
 * full path is written to pathOut (returns true). */
bool Browser_Enter(Browser *b, const char *const *exts, int extCount, char *pathOut, size_t pathSize);
/* Goes to the parent folder; the folder we came from is selected. */
bool Browser_Up(Browser *b, const char *const *exts, int extCount);

/* True for the audio formats raylib can decode. */
bool Browser_IsAudioName(const char *name);
/* The audio extension list for Browser_Open. */
const char *const *Browser_AudioExts(int *count);
/* $HOME/Music if it exists, else $HOME. */
void Browser_StartDir(char *out, size_t size);
/* "song.mp3" -> "song", for a default title. */
void Browser_TitleFromPath(const char *path, char *out, size_t size);

#endif
