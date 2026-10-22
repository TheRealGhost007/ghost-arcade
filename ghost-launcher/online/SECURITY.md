# Ghost Arcade leaderboard: security

The leaderboard is a Supabase (Postgres) project. The games ship a **publishable**
key, so assume everyone on the internet has it. Security therefore cannot depend
on the key being secret. It depends on two things: what the key is allowed to do
on the server, and the client never trusting what comes back.

## Can someone hack a player's computer through the database?

Not through any path we know of, and here is why rather than just "no":

- The database never sends code. The only thing a player's machine ever takes from
  it is text: game, mode, name, score, date.
- `ghost-sync` is the only program that talks to the network about scores. The games
  and the launcher never do; they only read the small text files it leaves behind.
  (Pond Kings' online games and rival crews are the one other network user, and
  never touch the score files: see "Pond Kings online play" below.)
- Everything downloaded is treated as hostile before it touches the disk:
  - the game name becomes a file name, so it must be one of the seven known games
    (no `/`, no `..`, nothing invented);
  - mode must match `[a-z0-9-]{1,24}`, the date must be a date, the score must be
    1 to 100,000,000;
  - names are reduced to printable ASCII, 32 characters, with `|` and `\` replaced.
    No control or terminal escape bytes survive. The games clean names again when
    they read the files;
  - a response larger than 1 MB is dropped, at most 512 rows are read, fields are
    cut at 63 bytes;
  - names are only ever passed to the screen as data with a length limit, never as
    a format string.
- The connection is HTTPS only, certificates verified, redirects refused, a
  non-https `url=` in `online.conf` switches syncing off.
- All of the parsing is unit tested with hostile input and runs clean under
  AddressSanitizer and UBSan (`make test`).

What an attacker with the key *can* do: submit made-up scores under made-up names,
within the limits below. That is true of every game that runs on the player's own
computer. It is a moderation problem, not a break-in.

## The update check

`ghost-sync` makes one other kind of request: an anonymous GET to GitHub's public API
to see whether `main` has moved past the commit the build was made from. No key and no
player data are sent. GitHub's reply is parsed as hostile too (strings are skipped
whole, so a commit message cannot fake a commit; the title shown in the launcher is cut
to printable ASCII; the repository name must match `owner/name` so it cannot bend the
URL). It only ever produces a notice: nothing is downloaded, and nothing is installed
unless the player runs `git pull && make install` themselves.

## What the public key can do on the server (schema.sql, version 3)

| Action | Result |
| --- | --- |
| Read `public.leaderboard` (game, mode, name, score, date, rank), a plain view over the function `leaderboard_rows()` | allowed |
| Call `public.submit_scores(install_id, rows)` | allowed, validated, rate limited |
| Read the `scores` table (would expose install ids) | denied |
| Insert / update / delete on the table | denied |
| Anything in the `private` schema | not exposed by the API |
| Call the Pond Kings functions `pk_room_open/join/poll/close`, `pk_crew_save/get/delete` | allowed, validated, rate limited (below) |

`submit_scores` skips (never errors on) rows with an unknown game, bad mode, non-ASCII
or control characters in the name, impossible scores or dates. Limits: 240 calls an
hour and 500 new rows a day per caller IP (stored only as a salted hash), 500 rows a
call, 5000 rows ever per install id, and the table stops growing near 500k rows so
the free tier's disk cannot be filled.

Verified live on 2026-09-20 with the public key: table read/insert/update/delete all
refused (42501), `private` unreachable, install ids not selectable, SQL injection
strings treated as text, 501-row call refused, duplicate ignored, rate limit trips
and recovers, and the real `ghost-sync` uploads and downloads end to end.

## Pond Kings online play (version 3)

Pond Kings is the one game that talks to the network itself, for its online games
and rival crews (`pondkings/src/lobby.c`, `relay.c`, `netui.c`). What it sends and
keeps:

- **Rooms.** A host's game leaves its two addresses (what its router shows the
  internet, and its LAN address), a random relay key and the SHA-256 of a random
  secret, and gets a six-letter code. A guest's game sends the code and its own
  addresses and gets the host's. The host's game polls with its secret until a
  guest is there, then closes the room. Rooms are deleted after 15 minutes. The
  player is told, before their first online game, that the two computers see each
  other's internet address and that the server keeps both for up to 15 minutes.
- **Rival crews.** One crew per install, owned by the SHA-256 of a secret kept in
  the player's data folder (mode 600). The crew is three names from the game's own
  rosters, builds, signature moves and four numbers for play style, in a strict
  format checked by a regular expression on the server: nothing free-form can be
  stored. "Stop sharing" deletes it.
- **The game itself** goes straight between the two computers over UDP, or through
  the project's Realtime broadcast channel named after the room's random relay key
  when routers block that. Only button presses, tick numbers and state fingerprints
  travel; every packet is checked field by field and dropped if anything is out of
  range, and packets without the room's token are ignored.
- Limits per caller IP an hour: 30 rooms, 60 joins (codes cannot be guessed at any
  useful rate: about 10^9 codes, at most 2000 open rooms), 3600 polls, 120 closes,
  20 crew saves, 240 crew lookups (about 10^12 codes), 30 crew deletions. At most
  2000 open rooms and 50000 crews.
- The relay costs Realtime messages from the free tier's monthly quota (about
  15,000 for a four-minute game). Most games connect directly and cost none.

Tested on 2026-10-22 against a local Postgres 16 with the same roles: the file
applies twice cleanly; open/join/poll/close, a second join refused, the wrong
secret refused, injection strings refused, the 31st room in an hour refused for one
address but not another, crew save/update/get/delete, free text and bad fields
refused, Pond Kings scores accepted, and `anon` and `authenticated` denied every
private table and helper.

## What Supabase's Security Advisor should show

**0 errors, 9 warnings, 6 suggestions**, and all of them are the design, not problems:

- Warnings: "Public can execute SECURITY DEFINER function" for `submit_scores`,
  `leaderboard_rows` and the seven `pk_` functions. True, and intended: a public
  leaderboard needs a public way to read the board and to submit a score, and Pond
  Kings needs a public way to open and join rooms and share crews. Those functions ARE
  that way in, and they are the only things the public key can run. The advisor is
  asking "did you mean this?" and the answer is yes.
- Suggestions: "RLS enabled, no policy" on `scores`, `private.games`,
  `private.rate_limit`, `private.secrets`, `private.pk_rooms`, `private.pk_crews`.
  Also intended: no policy means nobody gets in through the API at all.

Anything else appearing there (especially a red **error**) is new and worth a look.
An earlier version used an owner-rights view, which the advisor flags as a critical
"Security Definer View"; that was replaced on 2026-09-20.

`public.rls_auto_enable()` and the `ensure_rls` event trigger are Supabase's own: they
switch Row Level Security on for any new table in `public`. Keep them.

## Dashboard settings only you can change (recommended)

1. **Account**: turn on two-factor authentication for your Supabase login. Your
   account is the real master key.
2. **Database > Settings > Network restrictions**: restrict direct Postgres
   connections (nobody needs them; the games only use the HTTPS API).
3. **Database > Settings**: keep "Enforce SSL" on.
4. **Authentication > Sign In / Providers**: turn off "Allow new users to sign up".
   The games do not use accounts, so nobody should be able to create one.
5. Never put the `service_role` / secret key or the database password in a game, a
   repo or a chat. Only the publishable key ships. If a secret ever leaks, rotate it
   in Settings > API keys.
6. The free tier pauses after about a week idle. That is an outage, not a breach:
   un-pause it from the dashboard.

## Moderation

Run in the SQL editor:

    delete from public.scores where lower(username) = lower('bad name');
    delete from public.scores where install_id = '...';
    select install_id, count(*) from public.scores group by 1 order by 2 desc limit 20;

When a new game ships: `insert into private.games (slug) values ('newgame');` and add
the slug to `kKnownGames` in `sync/syncdata.c`.
