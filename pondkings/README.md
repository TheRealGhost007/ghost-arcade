# Pond Kings

Pond hockey with your friends, three on three. Kids in toques play under the
northern lights at the edge of a small town, then take their crew from the
frozen pond to the town rink and on to the big arena. Up to four players on
one screen, or a friend on their own computer online.

It lives in the Ghost Arcade repo and uses the arcade's engine pieces
(sound, music, window scaling, saves, scores, trophies), but it is its own
game, with its own look, music and world.

## Where you play

- **The pond:** no goalies, low narrow nets, snowbanks that swallow a hard
  puck, snow that slows the ice as it piles up, and a shovel break between
  periods. Wrist shots only.
- **The town rink:** boards, goalies and slapshots in the town's old barn,
  with parents in the bleachers and a tractor resurfacing the ice.
- **The arena:** the crowd, the lights, the horn, and the goal again on the
  big screen.

## Modes

- **Journey:** the Rink Rats' road from the pond tournament through the town
  league to the arena playoffs, three games a stage. Win to go on; a loss
  costs one of three chances. Each win lets you sign one of the beaten
  team's kids, signature move and all. Teams you beat badly hold a grudge
  and come back sharper. A friend's crew can turn up in the final.
- **Quick game:** any venue, any rivals; co-op against the computer or versus.
- **Shootout:** you against the goalie. Three misses and you're out, and he
  learns where you like to shoot.
- **Daily pond:** today's game, the same for everyone, with one twist (wind,
  a blizzard, glassy ice or tiny nets). Its own board, and on the
  launcher's daily desk.
- **Online:** host a room and tell a friend its six-letter code, or join
  theirs. Head to head, or the two of you on one team against the computer.
- **Rival crews:** share your crew with an eight-letter code. Friends add
  it, and it can turn up in their Journey's final, played by the computer
  the way you play.

## Controls

| | Shoot | Pass | Check / switch |
| --- | --- | --- | --- |
| Pad | X (or RT) | A | B |
| Keyboard, WASD side | SPACE | LEFT SHIFT | E |
| Keyboard, arrows side | / | . | , |
| One keyboard player | SPACE, Z or J | SHIFT, X or K | E, C or L |

- **Faceoffs:** press Pass the moment the puck drops. Too early and you wait.
- **One-timers:** press Shoot just before a pass reaches you.
- **Slapshots** (on the rinks): hold Shoot to wind up, let go to fire.
- **Who's playing:** each player presses their join button (A on a pad,
  SPACE on the WASD side, / on the arrows side), then picks a team.
- **Drop in, drop out:** a free pad presses A any time to take over a
  computer skater; a pad holding View for a second hands its skater back.
- Check with nobody close by switches you to the teammate nearest the puck.
  Pause with Start or Esc; F11 is fullscreen. Settings > Keyboard keys
  rebinds both keyboard halves.

## Online play

Online games run on rollback: each computer plays its own player's presses
at once and guesses the other's, and when the real presses arrive a few
ticks later and a guess was wrong, the game rewinds and replays those ticks
within the same frame. Both computers fingerprint the game every two
seconds and stop if they ever disagree.

The arcade's server (the leaderboard's Supabase project) only introduces the
two computers: it keeps the room code and both addresses for up to fifteen
minutes. Then they talk directly, or through the server's broadcast channel
when a router blocks that. Only button presses and tick numbers travel, and
every packet is checked field by field. The first time you pick Online or
Rival crews, the game says what is shared and asks before going on.
`ghost-launcher/online/SECURITY.md` has the details.

The server needs a one-time update for rooms and crew codes
(`ghost-launcher/online/schema.sql`, version 3). Until then those two menus
say so; everything else works.

## Building

    make run        # or: make install, then pondkings
    make test       # the rules, the Journey, online play's core (headless, also under ASan in CI)
    make shots      # pictures of every screen into build/shots, from a hidden window

Needs raylib and libcurl (online play). The tests need only a C compiler.

## How it is built

    src/fixed.h       16.16 fixed point and the 32 stick directions
    src/rink.h        sizes, in world units
    src/game.c/h      the rules: skating, the puck, passes, shots, checks,
                      goalies, slapshots, the venues, the twists, the shootout
    src/ai.c/h        the computer players
    src/teams.c/h     the town's teams: kids, colours, signature moves, goalies
    src/journey.c/h   the Journey: schedule, signing, grudges, the save file
    src/wire.c/h      online packets, checked field by field
    src/session.c/h   rollback: guesses, corrections, fingerprints
    src/net.c/h       UDP sockets, the LAN address, STUN
    src/netmsg.c/h    the server's JSON and the relay channel's messages
    src/lobby.c/h     room and crew calls to the server (libcurl, a worker thread)
    src/relay.c/h     the relay channel (libcurl websocket, a worker thread)
    src/crews.c/h     play style, crew codes, friends' crews
    src/netui.c/h     the online and crews screens and the connection steps
    src/input.c/h     keyboards and pads to button presses (the only raylib input)
    src/render.c/h    the town, the pond, the rinks, the skaters and the effects
    src/screens.c/h   the menus
    src/main.c        the screens, the players' devices, the 60-tick loop
    tests/            all of the above that needs no window, headless
    tools/            shots.c and shots_venues.c (pictures), sounds, icon

The rules core is written for online play from line one (see DESIGN.md):
whole-number maths only, button presses as the only input, and the whole
game state in one plain struct that a copy saves and restores.
