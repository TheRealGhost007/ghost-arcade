#define _POSIX_C_SOURCE 200809L /* fileno, open flags */
#include "crews.h"
#include "safefile.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define LEAN_MAX 30

static int Clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ------------------------------------------------------------ play style */

void Crews_AddGame(PlayStyle *ps, const Habits *h) {
    if (!ps || !h) return;
    ps->games++;
    ps->shots += h->shots;
    ps->passes += h->passes;
    ps->checks += h->checks;
    ps->carryTicks += h->carryTicks;
    for (int k = 0; k < 3; k++) ps->sideShots[k] += h->sideShots[k];
    /* keep it about the recent games: halve everything now and then */
    if (ps->games >= 40) {
        ps->games /= 2;
        ps->shots /= 2;
        ps->passes /= 2;
        ps->checks /= 2;
        ps->carryTicks /= 2;
        for (int k = 0; k < 3; k++) ps->sideShots[k] /= 2;
    }
}

AiLean Crews_LeanFrom(const PlayStyle *ps) {
    AiLean l = {0, 0, 0, 0};
    if (!ps || ps->games < 2) return l;
    uint32_t moves = ps->shots + ps->passes;
    if (moves >= 10) {
        /* half shots and half passes is the middle: +-30 at all one or all the other */
        int passPermille = (int)(ps->passes * 1000u / moves);
        l.pass = (int8_t)Clampi((passPermille - 500) * 60 / 1000, -LEAN_MAX, LEAN_MAX);
    }
    if (ps->carryTicks >= 600) {
        /* about one shot for every three seconds with the puck is the middle */
        int perThousand = (int)((uint64_t)ps->shots * 1000u * 10u / ps->carryTicks); /* tenths */
        l.shoot = (int8_t)Clampi((perThousand - 55) * 4 / 10, -LEAN_MAX, LEAN_MAX);
    }
    /* about four checks a game is the middle */
    int checksTenths = (int)(ps->checks * 10u / ps->games);
    l.check = (int8_t)Clampi((checksTenths - 40) * 5 / 10, -LEAN_MAX, LEAN_MAX);
    uint32_t aimed = ps->sideShots[0] + ps->sideShots[1] + ps->sideShots[2];
    if (aimed >= 8) {
        if (ps->sideShots[0] * 100u > aimed * 45u) l.side = -1;
        else if (ps->sideShots[2] * 100u > aimed * 45u) l.side = 1;
    }
    return l;
}

void Crews_Describe(const AiLean *l, char *out, size_t size) {
    if (!out || size == 0) return;
    out[0] = '\0';
    if (!l) return;
    const char *parts[4];
    int n = 0;
    if (l->shoot >= 10) parts[n++] = "shoots on sight";
    else if (l->shoot <= -10) parts[n++] = "waits for the shot";
    if (l->pass >= 10) parts[n++] = "loves to pass";
    else if (l->pass <= -10) parts[n++] = "keeps the puck";
    if (l->check >= 10) parts[n++] = "hits hard";
    else if (l->check <= -10) parts[n++] = "plays it clean";
    if (l->side < 0) parts[n++] = "aims high";
    else if (l->side > 0) parts[n++] = "aims low";
    if (n == 0) {
        snprintf(out, size, "Plays it straight");
        return;
    }
    size_t used = 0;
    for (int k = 0; k < n && used + 1 < size; k++) {
        int w = snprintf(out + used, size - used, "%s%s", k ? ", " : "", parts[k]);
        if (w < 0) break;
        used += (size_t)w;
    }
    if (out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 'a' + 'A');
}

/* ------------------------------------------------------------ text form */

void Crews_CleanName(const char *in, char *out, size_t size) {
    if (!out || size == 0) return;
    size_t cap = size < 24 ? size : 24, n = 0;
    for (const char *p = in; p && *p && n + 1 < cap; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 32 || c > 126 || c == ',' || c == '|' || c == '=' || c == ';') continue;
        if (n == 0 && c == ' ') continue;
        out[n++] = (char)c;
    }
    while (n > 0 && out[n - 1] == ' ') n--;
    out[n] = '\0';
}

static bool KidNameOk(const char *s) {
    size_t n = strlen(s);
    if (n == 0 || n >= JOURNEY_NAME_LEN) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 32 || c > 126 || c == ',' || c == '|' || c == '=' || c == ';') return false;
    }
    return true;
}

bool Crews_Encode(const CrewCard *c, char *out, size_t size) {
    if (!c || !out || size < 16) return false;
    int used = snprintf(out, size, "1");
    for (int k = 0; k < TEAM_SIZE; k++) {
        const CrewKid *kid = &c->kids[k];
        if (!KidNameOk(kid->name) || kid->build >= BUILD_COUNT || kid->trait >= TRAIT_COUNT) return false;
        int w = snprintf(out + used, size - (size_t)used, ";%s,%u,%u", kid->name, (unsigned)kid->build, (unsigned)kid->trait);
        if (w < 0 || (size_t)(used + w) >= size) return false;
        used += w;
    }
    int w = snprintf(out + used, size - (size_t)used, ";%d,%d,%d,%d", Clampi(c->lean.shoot, -LEAN_MAX, LEAN_MAX),
                     Clampi(c->lean.pass, -LEAN_MAX, LEAN_MAX), Clampi(c->lean.check, -LEAN_MAX, LEAN_MAX), Clampi(c->lean.side, -1, 1));
    if (w < 0 || (size_t)(used + w) >= size || used + w > CREW_TEXT_MAX) return false;
    return true;
}

/* A whole number in [lo, hi] up to the next separator; advances *p past it. */
static bool ReadInt(const char **p, char stop, int lo, int hi, int *v) {
    const char *s = *p;
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    if (*s < '0' || *s > '9') return false;
    long n = 0;
    int digits = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        if (++digits > 6) return false;
        s++;
    }
    if (*s != stop) return false;
    if (neg) n = -n;
    if (n < lo || n > hi) return false;
    *v = (int)n;
    *p = *s ? s + 1 : s;
    return true;
}

bool Crews_Decode(const char *name, const char *text, CrewCard *out) {
    if (!name || !text || !out || strlen(text) > CREW_TEXT_MAX) return false;
    CrewCard c;
    memset(&c, 0, sizeof(c));
    Crews_CleanName(name, c.name, sizeof(c.name));
    if (!c.name[0]) return false;
    const char *p = text;
    if (strncmp(p, "1;", 2) != 0) return false;
    p += 2;
    for (int k = 0; k < TEAM_SIZE; k++) {
        const char *comma = strchr(p, ',');
        if (!comma || comma == p || (size_t)(comma - p) >= JOURNEY_NAME_LEN) return false;
        memcpy(c.kids[k].name, p, (size_t)(comma - p));
        c.kids[k].name[comma - p] = '\0';
        if (!KidNameOk(c.kids[k].name)) return false;
        p = comma + 1;
        int build, trait;
        if (!ReadInt(&p, ',', 0, BUILD_COUNT - 1, &build) || !ReadInt(&p, ';', 0, TRAIT_COUNT - 1, &trait)) return false;
        c.kids[k].build = (uint8_t)build;
        c.kids[k].trait = (uint8_t)trait;
        c.kids[k].from = JOURNEY_FRIEND;
    }
    int sh, pa, ch, si;
    if (!ReadInt(&p, ',', -100, 100, &sh) || !ReadInt(&p, ',', -100, 100, &pa) || !ReadInt(&p, ',', -100, 100, &ch) ||
        !ReadInt(&p, '\0', -1, 1, &si))
        return false;
    c.lean.shoot = (int8_t)Clampi(sh, -LEAN_MAX, LEAN_MAX);
    c.lean.pass = (int8_t)Clampi(pa, -LEAN_MAX, LEAN_MAX);
    c.lean.check = (int8_t)Clampi(ch, -LEAN_MAX, LEAN_MAX);
    c.lean.side = (int8_t)si;
    *out = c;
    return true;
}

bool Crews_ValidCode(const char *code, int len) {
    if (!code || (int)strlen(code) != len) return false;
    for (int i = 0; i < len; i++) {
        char ch = code[i];
        bool letter = ch >= 'A' && ch <= 'Z' && ch != 'I' && ch != 'O';
        bool digit = ch >= '2' && ch <= '9';
        if (!letter && !digit) return false;
    }
    return true;
}

/* ------------------------------------------------------------ files */

static bool DataPath(const char *file, char *out, size_t size) {
    const char *xdg = getenv("XDG_DATA_HOME");
    char base[480];
    if (xdg && xdg[0]) snprintf(base, sizeof(base), "%s", xdg);
    else {
        const char *home = getenv("HOME");
        if (!home || !home[0]) return false;
        snprintf(base, sizeof(base), "%s/.local/share", home);
    }
    char dir[520];
    snprintf(dir, sizeof(dir), "%s/pondkings", base);
    mkdir(base, 0755);
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) return false;
    int n = snprintf(out, size, "%s/%s", dir, file);
    return n > 0 && (size_t)n < size;
}

static bool WriteText(const char *path, const char *text) {
    FILE *f = SafeFile_Open(path);
    if (!f) return false;
    fputs(text, f);
    return SafeFile_Close(f);
}

bool Crews_LoadStyle(PlayStyle *ps) {
    memset(ps, 0, sizeof(*ps));
    char path[600];
    if (!DataPath("style.txt", path, sizeof(path))) return false;
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char key[24];
        unsigned long v;
        if (sscanf(line, "%23[a-z0-9]=%lu", key, &v) != 2 || v > 100000000ul) continue;
        uint32_t u = (uint32_t)v;
        if (!strcmp(key, "games")) ps->games = u;
        else if (!strcmp(key, "shots")) ps->shots = u;
        else if (!strcmp(key, "passes")) ps->passes = u;
        else if (!strcmp(key, "checks")) ps->checks = u;
        else if (!strcmp(key, "carry")) ps->carryTicks = u;
        else if (!strcmp(key, "high")) ps->sideShots[0] = u;
        else if (!strcmp(key, "middle")) ps->sideShots[1] = u;
        else if (!strcmp(key, "low")) ps->sideShots[2] = u;
    }
    fclose(f);
    return true;
}

bool Crews_SaveStyle(const PlayStyle *ps) {
    char path[600], text[400];
    if (!DataPath("style.txt", path, sizeof(path))) return false;
    int n = snprintf(text, sizeof(text), "games=%u\nshots=%u\npasses=%u\nchecks=%u\ncarry=%u\nhigh=%u\nmiddle=%u\nlow=%u\n", ps->games,
                     ps->shots, ps->passes, ps->checks, ps->carryTicks, ps->sideShots[0], ps->sideShots[1], ps->sideShots[2]);
    return n > 0 && (size_t)n < sizeof(text) && WriteText(path, text);
}

int Crews_LoadFriends(CrewCard *out, int max) {
    char path[600];
    if (!out || max <= 0 || !DataPath("crews.txt", path, sizeof(path))) return 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int n = 0;
    char line[600];
    while (n < max && n < CREWS_MAX_FRIENDS && fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *bar1 = strchr(line, '|');
        char *bar2 = bar1 ? strchr(bar1 + 1, '|') : NULL;
        if (!bar1 || !bar2) continue;
        *bar1 = '\0';
        *bar2 = '\0';
        if (!Crews_ValidCode(line, CREW_CODE_LEN)) continue;
        CrewCard c;
        if (!Crews_Decode(bar1 + 1, bar2 + 1, &c)) continue;
        memcpy(c.code, line, CREW_CODE_LEN);
        c.code[CREW_CODE_LEN] = '\0';
        bool dup = false;
        for (int k = 0; k < n; k++) if (!strcmp(out[k].code, c.code)) dup = true;
        if (!dup) out[n++] = c;
    }
    fclose(f);
    return n;
}

bool Crews_SaveFriends(const CrewCard *list, int count) {
    char path[600];
    if (!DataPath("crews.txt", path, sizeof(path))) return false;
    static char text[CREWS_MAX_FRIENDS * 520 + 16];
    size_t used = 0;
    text[0] = '\0';
    for (int k = 0; k < count && k < CREWS_MAX_FRIENDS; k++) {
        char crew[CREW_TEXT_MAX + 8];
        if (!Crews_ValidCode(list[k].code, CREW_CODE_LEN) || !Crews_Encode(&list[k], crew, sizeof(crew))) continue;
        int w = snprintf(text + used, sizeof(text) - used, "%s|%s|%s\n", list[k].code, list[k].name, crew);
        if (w < 0 || used + (size_t)w >= sizeof(text)) break;
        used += (size_t)w;
    }
    return WriteText(path, text);
}

void Crews_LoadMyCode(char *code, size_t size) {
    if (!code || size == 0) return;
    code[0] = '\0';
    char path[600];
    if (!DataPath("mycrew.txt", path, sizeof(path))) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[32];
    if (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (Crews_ValidCode(line, CREW_CODE_LEN)) snprintf(code, size, "%s", line);
    }
    fclose(f);
}

bool Crews_SaveMyCode(const char *code) {
    char path[600], text[32];
    if (!DataPath("mycrew.txt", path, sizeof(path))) return false;
    snprintf(text, sizeof(text), "%s\n", Crews_ValidCode(code, CREW_CODE_LEN) ? code : "");
    return WriteText(path, text);
}

static bool IsHex64(const char *s) {
    if (strlen(s) != 64) return false;
    for (int i = 0; i < 64; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

bool Crews_Secret(char *out, size_t size) {
    if (!out || size < 65) return false;
    char path[600];
    if (!DataPath("crew-secret", path, sizeof(path))) return false;
    FILE *f = fopen(path, "r");
    if (f) {
        char line[80] = "";
        bool got = fgets(line, sizeof(line), f) != NULL;
        fclose(f);
        line[strcspn(line, "\r\n")] = '\0';
        if (got && IsHex64(line)) {
            snprintf(out, size, "%s", line);
            return true;
        }
    }
    /* a new one: 32 random bytes, readable by this user only */
    static const char kHex[] = "0123456789abcdef";
    unsigned char raw[32];
    int rfd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (rfd < 0) return false;
    size_t got = 0;
    while (got < sizeof(raw)) {
        ssize_t n = read(rfd, raw + got, sizeof(raw) - got);
        if (n <= 0) { if (n < 0 && errno == EINTR) continue; break; }
        got += (size_t)n;
    }
    close(rfd);
    if (got < sizeof(raw)) return false;
    for (int i = 0; i < 32; i++) {
        out[i * 2] = kHex[raw[i] >> 4];
        out[i * 2 + 1] = kHex[raw[i] & 15];
    }
    out[64] = '\0';
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    char line[80];
    int n = snprintf(line, sizeof(line), "%s\n", out);
    bool ok = write(fd, line, (size_t)n) == n;
    fchmod(fd, 0600);
    close(fd);
    return ok;
}
