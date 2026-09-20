#ifndef ONLINE_CONFIG_H
#define ONLINE_CONFIG_H

/* Built-in defaults for the Ghost Arcade online leaderboard. Both values can
 * be overridden in
 * $XDG_CONFIG_HOME/ghost-launcher/online.conf. Sharing scores is OPT-IN:
 * nothing is uploaded until enabled=1 is there (the launcher asks on first run).
 *
 * The key below is a Supabase PUBLISHABLE key: it is designed to ship inside
 * client apps and only grants what the Row Level Security policies in
 * online/schema.sql allow (read the board, insert a well-formed score).
 * NEVER put a secret / service_role key or the database password here. */
#define ONLINE_DEFAULT_URL "https://fezjcppaftfafnvzacss.supabase.co"
#define ONLINE_DEFAULT_KEY "sb_publishable_xc3VIscokqiga3k9xIcI-Q_g-rE7VhA"

#endif
