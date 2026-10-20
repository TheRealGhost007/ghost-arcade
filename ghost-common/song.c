#define _POSIX_C_SOURCE 200809L
#include "song.h"
#include "safefile.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void Song_ClearPattern(SongPattern *p) {
    memset(p->tone, NOTE_NONE, sizeof(p->tone));
    memset(p->drums, 0, sizeof(p->drums));
}

void Song_Init(Song *s, const char *name) {
    memset(s, 0, sizeof(*s));
    snprintf(s->name, sizeof(s->name), "%s", name && name[0] ? name : "Untitled");
    s->bpm = 120;
    s->swing = 0.0f;
    s->keyRoot = -1;
    s->keyScale = 0;
    s->wave[TRACK_BASS] = WAVE_TRI;
    s->wave[TRACK_ARP] = WAVE_PULSE25;
    s->wave[TRACK_LEAD] = WAVE_PULSE50;
    for (int i = 0; i < SONG_TRACKS; i++) s->volume[i] = 100;
    s->patternCount = 1;
    Song_ClearPattern(&s->patterns[0]);
    s->orderCount = 1;
    s->order[0] = 0;
}

/* ------------------------------------------------------------------ edits */

bool Song_AddPattern(Song *s, int *indexOut) {
    if (s->patternCount >= SONG_MAX_PATTERNS) return false;
    Song_ClearPattern(&s->patterns[s->patternCount]);
    if (indexOut) *indexOut = s->patternCount;
    s->patternCount++;
    return true;
}

bool Song_DuplicatePattern(Song *s, int from, int *indexOut) {
    if (from < 0 || from >= s->patternCount || s->patternCount >= SONG_MAX_PATTERNS) return false;
    s->patterns[s->patternCount] = s->patterns[from];
    if (indexOut) *indexOut = s->patternCount;
    s->patternCount++;
    return true;
}

bool Song_RemovePattern(Song *s, int index) {
    if (index < 0 || index >= s->patternCount || s->patternCount <= 1) return false;
    for (int i = index; i < s->patternCount - 1; i++) s->patterns[i] = s->patterns[i + 1];
    s->patternCount--;
    /* order entries pointing at it fall back to the previous pattern; later ones shift down */
    for (int i = 0; i < s->orderCount; i++) {
        if (s->order[i] == index) s->order[i] = (uint8_t)(index > 0 ? index - 1 : 0);
        else if (s->order[i] > index) s->order[i]--;
    }
    return true;
}

bool Song_OrderInsert(Song *s, int at, int pattern) {
    if (s->orderCount >= SONG_MAX_ORDER || pattern < 0 || pattern >= s->patternCount) return false;
    at = ClampI(at, 0, s->orderCount);
    for (int i = s->orderCount; i > at; i--) s->order[i] = s->order[i - 1];
    s->order[at] = (uint8_t)pattern;
    s->orderCount++;
    return true;
}

bool Song_OrderRemove(Song *s, int at) {
    if (at < 0 || at >= s->orderCount || s->orderCount <= 1) return false;
    for (int i = at; i < s->orderCount - 1; i++) s->order[i] = s->order[i + 1];
    s->orderCount--;
    return true;
}

bool Song_OrderSet(Song *s, int at, int pattern) {
    if (at < 0 || at >= s->orderCount || pattern < 0 || pattern >= s->patternCount) return false;
    s->order[at] = (uint8_t)pattern;
    return true;
}

float Song_StepSeconds(const Song *s) { return 60.0f / ((float)s->bpm * 4.0f); }
float Song_Seconds(const Song *s) { return Song_StepSeconds(s) * (float)SONG_STEPS * (float)s->orderCount; }

/* ------------------------------------------------------------------ names */

static const char *const kWaveNames[WAVE_COUNT] = {"tri", "pulse50", "pulse25", "pulse12", "saw"};
const char *Song_WaveName(SongWave w) { return (w >= 0 && w < WAVE_COUNT) ? kWaveNames[w] : "?"; }

static const char *const kNoteNames[12] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};

void Song_NoteName(int note, char out[4]) {
    if (note == NOTE_HOLD) { memcpy(out, "===", 4); return; }
    if (note < 0 || note > 127) { memcpy(out, "---", 4); return; }
    int oct = note / 12 - 1; /* MIDI 60 = C4 */
    const char *n = kNoteNames[note % 12];
    out[0] = n[0];
    out[1] = n[1];
    out[2] = (char)(oct < 0 ? '0' : (oct > 9 ? '9' : '0' + oct));
    out[3] = '\0';
}

static const char *const kScaleNames[SONG_SCALE_COUNT] = {"minor", "major", "dorian", "phrygian", "harmonic minor", "mixolydian"};
const char *Song_ScaleName(int scale) { return (scale >= 0 && scale < SONG_SCALE_COUNT) ? kScaleNames[scale] : "?"; }
/* A scale value nothing set properly reads as minor, the default, rather than
 * whichever scale happens to sit at the clamped end of the table. */
static int ScaleOrMinor(int scale) { return (scale >= 0 && scale < SONG_SCALE_COUNT) ? scale : 0; }

int Song_ScaleFromName(const char *text) {
    if (!text) return -1;
    /* Compare without case and let the two-word name match on its first word
     * too, so a hand-typed "key A harmonic" still reads. */
    char low[32];
    size_t n = 0;
    while (text[n] && n < sizeof(low) - 1) { low[n] = (char)tolower((unsigned char)text[n]); n++; }
    low[n] = '\0';
    while (n > 0 && (low[n - 1] == ' ' || low[n - 1] == '\t')) low[--n] = '\0';
    for (int i = 0; i < SONG_SCALE_COUNT; i++) {
        if (strcmp(low, kScaleNames[i]) == 0) return i;
        const char *space = strchr(kScaleNames[i], ' ');
        if (space && strncmp(low, kScaleNames[i], (size_t)(space - kScaleNames[i])) == 0 && low[space - kScaleNames[i]] == '\0') return i;
    }
    return -1;
}

static const char *const kRootNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char *Song_KeyRootName(int pitchClass) { return (pitchClass >= 0 && pitchClass < 12) ? kRootNames[pitchClass] : "?"; }

int Song_KeyRootFromName(const char *t) {
    if (!t || !t[0]) return -1;
    int base;
    switch (toupper((unsigned char)t[0])) {
        case 'C': base = 0; break;
        case 'D': base = 2; break;
        case 'E': base = 4; break;
        case 'F': base = 5; break;
        case 'G': base = 7; break;
        case 'A': base = 9; break;
        case 'B': base = 11; break;
        default: return -1;
    }
    const char *p = t + 1;
    if (*p == '#') { base++; p++; }
    else if (*p == 'b') { base--; p++; }
    if (*p != '\0') return -1;
    return (base % 12 + 12) % 12;
}

void Song_KeyName(int keyRoot, int keyScale, char *out, size_t size) {
    if (!out || size == 0) return;
    if (keyRoot < 0 || keyRoot >= 12) { out[0] = '\0'; return; }
    snprintf(out, size, "%s %s", kRootNames[keyRoot], Song_ScaleName(ScaleOrMinor(keyScale)));
}

int Song_NoteFromName(const char *t) {
    if (!t || !t[0]) return NOTE_NONE;
    if (strcmp(t, "===") == 0) return NOTE_HOLD;
    int base;
    switch (toupper((unsigned char)t[0])) {
        case 'C': base = 0; break;
        case 'D': base = 2; break;
        case 'E': base = 4; break;
        case 'F': base = 5; break;
        case 'G': base = 7; break;
        case 'A': base = 9; break;
        case 'B': base = 11; break;
        default: return NOTE_NONE;
    }
    const char *p = t + 1;
    if (*p == '#') { base++; p++; }
    else if (*p == 'b') { base--; p++; }
    else if (*p == '-') p++;
    if (!isdigit((unsigned char)*p)) return NOTE_NONE;
    int oct = *p - '0';
    if (p[1] != '\0') return NOTE_NONE;
    int midi = (oct + 1) * 12 + base;
    return (midi >= 0 && midi <= 127) ? midi : NOTE_NONE;
}

/* ------------------------------------------------------------ file format */

/* The one rule for a song's name, used both when a file is read and when it
 * is written, so a loaded song's name is exactly what the next save writes:
 * control characters (a newline would inject a directive) and DEL become
 * spaces, outer spaces go, and nothing left means "Untitled". In place; the
 * bytes after the end are zeroed so equal names are equal byte for byte. */
static void TidyName(char *name, size_t size) {
    for (char *c = name; *c; c++) if ((unsigned char)*c < 32 || *c == 127) *c = ' ';
    const char *start = name;
    while (*start == ' ') start++;
    size_t n = strlen(start);
    while (n > 0 && start[n - 1] == ' ') n--;
    memmove(name, start, n);
    memset(name + n, 0, size - n);
    if (n == 0) snprintf(name, size, "Untitled");
}

void Song_Write(const Song *s, FILE *f) {
    fprintf(f, "GSONG 1\n");
    fprintf(f, "# A Ghost Arcade chiptune. Rows: step bass arp lead drums(K S H).\n");
    /* Written tidied, the same way Song_Parse tidies what it reads, so the
     * name reads back exactly (and a newline in it cannot inject a directive). */
    char name[SONG_NAME_LEN];
    snprintf(name, sizeof(name), "%s", s->name);
    TidyName(name, sizeof(name));
    fprintf(f, "name %s\n", name);
    fprintf(f, "bpm %d\n", s->bpm);
    fprintf(f, "swing %.2f\n", (double)s->swing);
    fprintf(f, "wave %s %s %s\n", Song_WaveName(s->wave[0]), Song_WaveName(s->wave[1]), Song_WaveName(s->wave[2]));
    fprintf(f, "volume %d %d %d %d\n", s->volume[0], s->volume[1], s->volume[2], s->volume[3]);
    /* Only a known key is written: readers that predate the line skip it as an
     * unknown line, and a song that never said keeps saying nothing. */
    if (s->keyRoot >= 0 && s->keyRoot < 12) fprintf(f, "key %s %s\n", Song_KeyRootName(s->keyRoot), Song_ScaleName(ScaleOrMinor(s->keyScale)));
    fprintf(f, "order");
    for (int i = 0; i < s->orderCount; i++) fprintf(f, " %d", s->order[i]);
    fprintf(f, "\n");
    for (int p = 0; p < s->patternCount; p++) {
        const SongPattern *pat = &s->patterns[p];
        fprintf(f, "pattern %d\n", p);
        for (int st = 0; st < SONG_STEPS; st++) {
            char a[4], b[4], c[4];
            Song_NoteName(pat->tone[0][st], a);
            Song_NoteName(pat->tone[1][st], b);
            Song_NoteName(pat->tone[2][st], c);
            uint8_t d = pat->drums[st];
            fprintf(f, "%02d %s %s %s %c%c%c\n", st, a, b, c, (d & DRUM_KICK) ? 'K' : '.', (d & DRUM_SNARE) ? 'S' : '.', (d & DRUM_HAT) ? 'H' : '.');
        }
    }
}

static SongWave WaveFromName(const char *n, SongWave fallback) {
    for (int i = 0; i < WAVE_COUNT; i++) if (strcmp(n, kWaveNames[i]) == 0) return (SongWave)i;
    return fallback;
}

/* Copies the next whitespace-delimited token (at most max-1 chars) and advances *p. */
static bool Tok(const char **p, const char *end, char *out, size_t max) {
    const char *s = *p;
    while (s < end && (*s == ' ' || *s == '\t')) s++;
    if (s >= end || *s == '\n' || *s == '\r') { *p = s; return false; }
    size_t n = 0;
    while (s < end && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r') {
        if (n < max - 1) out[n++] = *s;
        s++;
    }
    out[n] = '\0';
    *p = s;
    return true;
}

bool Song_Parse(Song *s, const char *text, size_t len) {
    if (!text) return false;
    Song_Init(s, "Untitled");
    const char *p = text, *end = text + len;
    bool header = false, anyRow = false, anyPattern = false;
    int curPattern = -1;
    int maxPattern = -1;
    int orderCount = 0;
    char tok[64];

    while (p < end) {
        const char *lineEnd = memchr(p, '\n', (size_t)(end - p));
        if (!lineEnd) lineEnd = end;
        const char *q = p;
        if (Tok(&q, lineEnd, tok, sizeof(tok)) && tok[0] != '#') {
            if (strcmp(tok, "GSONG") == 0) header = true;
            else if (strcmp(tok, "name") == 0) {
                /* Cut to fit first, then tidied as Song_Write tidies it, so a
                 * cut that ends on a space or a trailing tab or CR leaves no
                 * outer space behind. */
                while (q < lineEnd && (*q == ' ' || *q == '\t')) q++;
                size_t n = (size_t)(lineEnd - q);
                if (n >= sizeof(s->name)) n = sizeof(s->name) - 1;
                memcpy(s->name, q, n);
                s->name[n] = '\0';
                TidyName(s->name, sizeof(s->name));
            } else if (strcmp(tok, "bpm") == 0) {
                if (Tok(&q, lineEnd, tok, sizeof(tok))) s->bpm = ClampI(atoi(tok), SONG_BPM_MIN, SONG_BPM_MAX);
            } else if (strcmp(tok, "swing") == 0) {
                if (Tok(&q, lineEnd, tok, sizeof(tok))) {
                    /* atof reads "nan" and "inf"; a clamp lets NaN through
                     * (every comparison is false) and the synth then never
                     * triggers a step, so a hand-edited file would play silence.
                     * Kept to the hundredths Song_Write writes, so the song in
                     * memory is the one the next save writes back ("-0" is
                     * plain 0, not a "-0.00" in the next save). */
                    float v = (float)atof(tok);
                    float c = isfinite(v) ? ClampF(v, 0.0f, 0.3f) : 0.0f;
                    s->swing = c > 0.0f ? roundf(c * 100.0f) / 100.0f : 0.0f;
                }
            } else if (strcmp(tok, "wave") == 0) {
                for (int i = 0; i < SONG_TONE_TRACKS && Tok(&q, lineEnd, tok, sizeof(tok)); i++) s->wave[i] = WaveFromName(tok, s->wave[i]);
            } else if (strcmp(tok, "volume") == 0) {
                for (int i = 0; i < SONG_TRACKS && Tok(&q, lineEnd, tok, sizeof(tok)); i++) s->volume[i] = ClampI(atoi(tok), 0, 100);
            } else if (strcmp(tok, "key") == 0) {
                /* "key A minor" or "key C# harmonic minor". The scale is read
                 * word by word like every other value, so tabs or a doubled
                 * space inside "harmonic minor" read the same, and anything
                 * after the scale is ignored as it is on the other lines. A
                 * two-word name is tried first, then the first word alone
                 * ("harmonic" alone is harmonic minor too). A root that does
                 * not read leaves the song without a key; a scale that does
                 * not read falls back to minor. */
                if (Tok(&q, lineEnd, tok, sizeof(tok))) {
                    int root = Song_KeyRootFromName(tok);
                    if (root >= 0) {
                        char word[32], next[32], both[72];
                        int scale = -1;
                        if (Tok(&q, lineEnd, word, sizeof(word))) {
                            if (Tok(&q, lineEnd, next, sizeof(next))) {
                                snprintf(both, sizeof(both), "%s %s", word, next);
                                scale = Song_ScaleFromName(both);
                            }
                            if (scale < 0) scale = Song_ScaleFromName(word);
                        }
                        s->keyRoot = root;
                        s->keyScale = scale >= 0 ? scale : 0;
                    }
                }
            } else if (strcmp(tok, "order") == 0) {
                while (orderCount < SONG_MAX_ORDER && Tok(&q, lineEnd, tok, sizeof(tok))) {
                    int v = atoi(tok);
                    if (v >= 0 && v < SONG_MAX_PATTERNS) s->order[orderCount++] = (uint8_t)v;
                }
            } else if (strcmp(tok, "pattern") == 0) {
                curPattern = Tok(&q, lineEnd, tok, sizeof(tok)) ? atoi(tok) : -1;
                if (curPattern < 0 || curPattern >= SONG_MAX_PATTERNS) curPattern = -1;
                else {
                    if (curPattern > maxPattern) {
                        for (int i = maxPattern + 1; i <= curPattern; i++) Song_ClearPattern(&s->patterns[i]);
                        maxPattern = curPattern;
                    }
                    anyPattern = true;
                }
            } else if (curPattern >= 0 && isdigit((unsigned char)tok[0])) {
                int st = atoi(tok);
                if (st >= 0 && st < SONG_STEPS) {
                    SongPattern *pat = &s->patterns[curPattern];
                    for (int t = 0; t < SONG_TONE_TRACKS; t++) {
                        if (!Tok(&q, lineEnd, tok, sizeof(tok))) break;
                        int n = Song_NoteFromName(tok);
                        if (n >= 0) n = ClampI(n, NOTE_MIN, NOTE_MAX);
                        pat->tone[t][st] = (int8_t)n;
                    }
                    if (Tok(&q, lineEnd, tok, sizeof(tok))) {
                        uint8_t d = 0;
                        for (const char *c = tok; *c; c++) {
                            if (*c == 'K' || *c == 'k') d |= DRUM_KICK;
                            if (*c == 'S' || *c == 's') d |= DRUM_SNARE;
                            if (*c == 'H' || *c == 'h') d |= DRUM_HAT;
                        }
                        pat->drums[st] = d;
                    }
                    anyRow = true;
                }
            }
        }
        p = lineEnd < end ? lineEnd + 1 : end;
    }
    if (!header || !anyPattern) return false;
    s->patternCount = maxPattern + 1;
    /* the order may only name patterns that exist */
    int kept = 0;
    for (int i = 0; i < orderCount; i++) if (s->order[i] < s->patternCount) s->order[kept++] = s->order[i];
    s->orderCount = kept;
    if (s->orderCount == 0) { s->order[0] = 0; s->orderCount = 1; }
    (void)anyRow;
    return true;
}

bool Song_Load(Song *s, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long n = ftell(f);
    if (n < 0 || n > 4 * 1024 * 1024) { fclose(f); return false; }
    rewind(f);
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return false; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    bool ok = Song_Parse(s, buf, got);
    free(buf);
    return ok;
}

bool Song_Save(const Song *s, const char *path) {
    FILE *f = SafeFile_Open(path);
    if (!f) return false;
    Song_Write(s, f);
    return SafeFile_Close(f);
}

/* ---------------------------------------------------------------- library */

static bool XdgDir(const char *envVar, const char *fallback, char *out, size_t size) {
    const char *x = getenv(envVar);
    if (x && x[0]) { snprintf(out, size, "%s", x); return true; }
    const char *home = getenv("HOME");
    if (!home || !home[0]) return false;
    snprintf(out, size, "%s/%s", home, fallback);
    return true;
}

static bool MkdirP(const char *a, const char *b, const char *c) {
    char path[700];
    snprintf(path, sizeof(path), "%s", a);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/%s", a, b);
    if (mkdir(path, 0755) != 0 && errno != EEXIST) return false;
    if (c) {
        snprintf(path, sizeof(path), "%s/%s/%s", a, b, c);
        if (mkdir(path, 0755) != 0 && errno != EEXIST) return false;
    }
    return true;
}

bool Song_LibraryDir(char *out, size_t size, bool create) {
    char base[512];
    if (!XdgDir("XDG_DATA_HOME", ".local/share", base, sizeof(base))) return false;
    if (create && !MkdirP(base, "ghost-arcade", "music")) return false;
    snprintf(out, size, "%s/ghost-arcade/music", base);
    return true;
}

static bool HasSuffix(const char *s, const char *suf) {
    size_t n = strlen(s), m = strlen(suf);
    return n >= m && strcmp(s + n - m, suf) == 0;
}

static void SanitiseFileName(const char *in, char *out, size_t size) {
    size_t n = 0;
    for (const char *p = in; *p && n < size - 7; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '/' || c == '\\' || c < 32 || c == 127) continue;
        if (c == '.' && n == 0) continue; /* no hidden files, no ".." */
        out[n++] = (char)c;
    }
    out[n] = '\0';
    if (n == 0) snprintf(out, size, "untitled");
    if (!HasSuffix(out, ".gsong")) {
        size_t len = strlen(out);
        if (len + 6 < size) memcpy(out + len, ".gsong", 7);
    }
}

bool Song_LibraryPath(const char *fileName, char *out, size_t size, bool create) {
    char dir[600], name[SONG_FILE_LEN + 8];
    if (!fileName || !Song_LibraryDir(dir, sizeof(dir), create)) return false;
    SanitiseFileName(fileName, name, sizeof(name));
    snprintf(out, size, "%s/%s", dir, name);
    return true;
}

void Song_FileNameFor(const char *title, char *out, size_t size) {
    size_t n = 0;
    bool dash = true; /* swallow leading separators */
    for (const char *p = title; *p && n < size - 7 && n < SONG_FILE_LEN - 7; p++) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c)) { out[n++] = (char)tolower(c); dash = false; }
        else if (!dash) { out[n++] = '-'; dash = true; }
    }
    while (n > 0 && out[n - 1] == '-') n--;
    out[n] = '\0';
    if (n == 0) snprintf(out, size, "untitled");
    size_t len = strlen(out);
    if (len + 6 < size) memcpy(out + len, ".gsong", 7);
}

static int CmpNames(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

int Song_LibraryList(char names[][SONG_FILE_LEN], int maxNames) {
    char dir[600];
    if (maxNames <= 0 || !Song_LibraryDir(dir, sizeof(dir), false)) return 0;
    DIR *d = opendir(dir);
    if (!d) return 0;
    /* Gather every name before sorting: stopping at maxNames in readdir
     * order would make which songs fall off a full library depend on the
     * filesystem, not on the names the list shows. */
    char (*all)[SONG_FILE_LEN] = NULL;
    int n = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' || !HasSuffix(e->d_name, ".gsong")) continue;
        size_t len = strlen(e->d_name);
        if (len >= SONG_FILE_LEN) continue;
        /* List only names that open and save as themselves: every path goes
         * through SanitiseFileName, so a name it would rewrite ("a\\b.gsong")
         * would show one file and touch another. */
        char canon[SONG_FILE_LEN + 8];
        SanitiseFileName(e->d_name, canon, sizeof(canon));
        if (strcmp(canon, e->d_name) != 0) continue;
        if (n == cap) {
            int ncap = cap ? cap * 2 : 64;
            char (*grown)[SONG_FILE_LEN] = realloc(all, (size_t)ncap * SONG_FILE_LEN);
            if (!grown) break; /* out of memory: list what we have */
            all = grown;
            cap = ncap;
        }
        memcpy(all[n++], e->d_name, len + 1);
    }
    closedir(d);
    if (n > 1) qsort(all, (size_t)n, SONG_FILE_LEN, CmpNames);
    if (n > maxNames) n = maxNames;
    if (n > 0) memcpy(names, all, (size_t)n * SONG_FILE_LEN);
    free(all);
    return n;
}

/* ------------------------------------------------------------ assignments */

static bool AssignPath(char *out, size_t size, bool create) {
    char base[512];
    if (!XdgDir("XDG_CONFIG_HOME", ".config", base, sizeof(base))) return false;
    if (create && !MkdirP(base, "ghost-arcade", NULL)) return false;
    snprintf(out, size, "%s/ghost-arcade/music.ini", base);
    return true;
}

bool Song_AssignedName(const char *slug, char *out, size_t size) {
    char path[700];
    out[0] = '\0';
    if (!slug || !AssignPath(path, sizeof(path), false)) return false;
    FILE *f = fopen(path, "r");
    if (!f) return false;
    /* Whole lines, as Song_Assign reads them: a line ends at its real newline
     * whatever bytes it holds, so neither the tail of an over-long comment
     * nor the line after a stray NUL byte (which hides a newline from strlen)
     * is taken for, or hidden from, a slug's line. */
    char *line = NULL;
    size_t cap = 0;
    bool found = false;
    size_t slen = strlen(slug);
    while (getline(&line, &cap, f) > 0) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, slug, slen) == 0 && line[slen] == '=') {
            const char *v = line + slen + 1;
            if (!strchr(v, '/') && v[0] != '\0') { /* a bare library file name only */
                snprintf(out, size, "%s", v);
                found = true;
            }
        }
    }
    free(line);
    fclose(f);
    return found;
}

bool Song_Assigned(const char *slug, char *out, size_t size) {
    char name[SONG_FILE_LEN + 8];
    if (!Song_AssignedName(slug, name, sizeof(name))) return false;
    if (!Song_LibraryPath(name, out, size, false)) return false;
    struct stat st;
    return stat(out, &st) == 0;
}

bool Song_Assign(const char *slug, const char *fileName) {
    if (!slug || !slug[0] || strchr(slug, '=') || strchr(slug, '\n')) return false;
    char path[700];
    if (!AssignPath(path, sizeof(path), true)) return false;
    /* Stream the old file through, dropping only this slug's lines, so hand
     * edits of any length or count survive. */
    FILE *in = fopen(path, "r");
    FILE *w = SafeFile_Open(path);
    if (!w) { if (in) fclose(in); return false; }
    bool wroteAny = false;
    size_t slen = strlen(slug);
    if (in) {
        /* Whole lines, as Song_AssignedName reads them, and each kept line
         * written back byte for byte: fputs would stop at a stray NUL and drop
         * the newline, gluing the next line onto it. */
        char *line = NULL;
        size_t cap = 0;
        ssize_t got;
        while ((got = getline(&line, &cap, in)) > 0) {
            size_t n = (size_t)got;
            bool blank = line[0] == '\n' || (line[0] == '\r' && line[1] == '\n');
            if (blank || (strncmp(line, slug, slen) == 0 && line[slen] == '=')) continue;
            fwrite(line, 1, n, w);
            if (line[n - 1] != '\n') fputc('\n', w); /* the last line had no newline */
            wroteAny = true;
        }
        free(line);
        fclose(in);
    }
    if (!wroteAny) fprintf(w, "# Which Chipforge song each Ghost Arcade game plays: slug=file.gsong\n");
    if (fileName && fileName[0]) {
        char clean[SONG_FILE_LEN + 8];
        SanitiseFileName(fileName, clean, sizeof(clean));
        fprintf(w, "%s=%s\n", slug, clean);
    }
    return SafeFile_Close(w);
}
