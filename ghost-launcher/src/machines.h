#ifndef MACHINES_H
#define MACHINES_H

#include <stdbool.h>
#include <stddef.h>
#include "manifest.h"
#include "stats.h"
#include "profile.h"
#include "launcherini.h"
#include "scores.h"

/* The Machines manager without raylib: what each machine's status says in
 * Settings > Machines, and what a rename leaves behind. Playtime
 * (playtime.txt) and a game's own player name (profiles.txt) are kept under
 * the machine's NAME; the row's star, place and hiding (launcher.ini) under
 * its PROGRAM's file name, as are its scores and trophies, which belong to
 * the games' own files and never move. The admin's form says so before a
 * save, and can carry the launcher's own records over to the new name. */

typedef enum { MS_INSTALLED, MS_NOT_INSTALLED, MS_INSTALLING } MachineStatus;

MachineStatus Machines_Status(bool installed, bool installing);
/* "Installed", "Not installed", "Installing". */
const char *Machines_StatusWords(MachineStatus s);

/* What stays under a machine's old name and program when the form saves
 * new ones. */
typedef struct {
    bool renamed;                 /* the name changed */
    bool programChanged;          /* the program's file name changed */
    double seconds;               /* playtime kept under the old name */
    char ownName[64];             /* the game's own player name under the old name, or "" */
    bool rowKeys;                 /* launcher.ini has a star, place, hiding or played time for the old program */
    char oldSlug[SCORES_SLUG_LEN];
    char newSlug[SCORES_SLUG_LEN];
} CarryOver;

void Machines_WhatStays(CarryOver *c, const char *oldName, const char *newName, const char *oldExec, const char *newExec,
                        const PlayStats *stats, const Profiles *p, const LauncherIni *ini);
/* Something the launcher can move to the new name. */
bool Machines_CanCarry(const CarryOver *c);
/* For the form, under the fields:
 *   stays: "Its 38m of play and its own name (Arcade) are kept under Blockfall."
 *   scores: "Scores and trophies follow the program's file name: they stay with blockfall."
 *   moves:  what Carry them over moves ("Its playtime and its own name move with it.")
 * Each is "" when it does not apply. */
void Machines_CarryWords(const CarryOver *c, const char *oldName, char *stays, char *scores, char *moves, size_t size);
/* Moves them, saving each file. A file that could not be saved is read back,
 * so memory matches disk; false if anything was not kept. */
bool Machines_Carry(const CarryOver *c, const char *oldName, const char *newName, PlayStats *stats, Profiles *p,
                    LauncherIni *ini);

#endif
