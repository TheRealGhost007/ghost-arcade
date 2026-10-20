#include "machines.h"
#include "lineup.h"
#include <stdio.h>
#include <string.h>

MachineStatus Machines_Status(bool installed, bool installing) {
    if (installing) return MS_INSTALLING;
    return installed ? MS_INSTALLED : MS_NOT_INSTALLED;
}

const char *Machines_StatusWords(MachineStatus s) {
    switch (s) {
        case MS_INSTALLING: return "Installing";
        case MS_NOT_INSTALLED: return "Not installed";
        default: return "Installed";
    }
}

static void SlugOf(const char *exec, char *out, size_t size) {
    if (!exec || !Scores_SlugFromExec(exec, out, size)) out[0] = '\0';
}

void Machines_WhatStays(CarryOver *c, const char *oldName, const char *newName, const char *oldExec, const char *newExec,
                        const PlayStats *stats, const Profiles *p, const LauncherIni *ini) {
    memset(c, 0, sizeof(*c));
    c->renamed = oldName && newName && newName[0] && strcmp(oldName, newName) != 0;
    if (c->renamed) {
        c->seconds = stats ? Stats_GetGameTime(stats, oldName) : 0.0;
        if (p) snprintf(c->ownName, sizeof(c->ownName), "%.63s", Profiles_GetOwnUsername(p, oldName));
    }
    SlugOf(oldExec, c->oldSlug, sizeof(c->oldSlug));
    SlugOf(newExec, c->newSlug, sizeof(c->newSlug));
    /* A program that has no slug yet (still being typed) moves nothing. */
    c->programChanged = c->oldSlug[0] && c->newSlug[0] && strcmp(c->oldSlug, c->newSlug) != 0;
    c->rowKeys = c->programChanged && ini && Row_HasKeys(ini, c->oldSlug);
}

bool Machines_CanCarry(const CarryOver *c) { return c->seconds > 0.0 || c->ownName[0] || c->rowKeys; }

void Machines_CarryWords(const CarryOver *c, const char *oldName, char *stays, char *scores, char *moves, size_t size) {
    stays[0] = scores[0] = moves[0] = '\0';
    char time[24], name[48];
    Stats_FormatTime(c->seconds, time, sizeof(time));
    snprintf(name, sizeof(name), "%.24s", c->ownName);
    bool hasTime = c->seconds > 0.0, hasName = c->ownName[0] != '\0';
    if (hasTime && hasName) snprintf(stays, size, "Its %s of play and its own name (%s) are kept under %.24s.", time, name, oldName);
    else if (hasTime) snprintf(stays, size, "Its %s of play is kept under %.24s.", time, oldName);
    else if (hasName) snprintf(stays, size, "Its own name (%s) is kept under %.24s.", name, oldName);
    if (c->programChanged) snprintf(scores, size, "Scores and trophies follow the program's file name: they stay with %.24s.", c->oldSlug);
    const char *parts[3];
    int n = 0;
    if (hasTime) parts[n++] = "its playtime";
    if (hasName) parts[n++] = "its own name";
    if (c->rowKeys) parts[n++] = "your row settings";
    if (n == 0) return;
    char list[96] = "";
    for (int i = 0; i < n; i++) {
        size_t used = strlen(list);
        snprintf(list + used, sizeof(list) - used, "%s%s", i == 0 ? "" : (i == n - 1 ? " and " : ", "), parts[i]);
    }
    list[0] = (char)(list[0] - 'a' + 'A');
    bool plural = n > 1 || c->rowKeys; /* "your row settings" are many */
    snprintf(moves, size, "%s %s with it.", list, plural ? "move" : "moves");
}

bool Machines_Carry(const CarryOver *c, const char *oldName, const char *newName, PlayStats *stats, Profiles *p,
                    LauncherIni *ini) {
    bool ok = true;
    if (c->renamed && c->seconds > 0.0 && stats && Stats_Fold(stats, oldName, newName) && !Stats_Save(stats)) {
        ok = false;
        Stats_Load(stats);
    }
    if (c->renamed && c->ownName[0] && p && Profiles_Rename(p, oldName, newName) && !Profiles_Save(p)) {
        ok = false;
        Profiles_Load(p);
    }
    if (c->rowKeys && ini && !Row_CarryKeys(ini, c->oldSlug, c->newSlug)) {
        ok = false;
        LauncherIni_Load(ini);
    }
    return ok;
}
