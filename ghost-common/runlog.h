#ifndef RUNLOG_H
#define RUNLOG_H

/* One line per finished (or abandoned) run, appended to
 * $XDG_DATA_HOME/<slug>/runs.log:  YYYY-MM-DD HH:MM:SS|mode|score|level
 * Zero-score runs are logged too, because "died at once" is exactly what a
 * difficulty audit needs to see. Best-effort: a failure to write is silent.
 * No raylib. */
void RunLog_Append(const char *slug, const char *mode, long score, int level);

#endif
