#ifndef FAKE_LAUNCH_H
#define FAKE_LAUNCH_H

#include <stdbool.h>

/* The harness's stand-in for src/launch.c: the same API (launch.h), but no
 * process is ever started, so a screenshot run can press Enter on a machine
 * without a game, `make install` or ghost-sync running. The driver ends the
 * pretend game and install when its script says so. */
void FakeLaunch_ExitGame(void);
void FakeLaunch_FinishInstall(bool ok);
/* The pretend Update now ends: with this exit status, as install.sh would. */
void FakeLaunch_FinishUpdate(int exitCode);
/* An update not this launcher's: 0 none, 1 another launcher's, 2 a terminal's. */
void FakeLaunch_SetElsewhere(int kind);
/* Every launch, install and sync asked for so far, one per line. */
const char *FakeLaunch_Log(void);

#endif
