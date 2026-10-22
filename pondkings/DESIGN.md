# Pond Kings: design

Agreed with the owner on 2026-10-21 and 2026-10-22.

## The game

Kids in a small frozen town. You and two friends start on the pond and play
your way up through the town rink to the big arena, three on three, with
arcade controls. Its world is its own: no crossover with the other arcade
games, and its own look and music instead of the arcade's house style. It
lives in the ghost-arcade repo and can start from Ghost Launcher.

- **The pond:** no goalies, low narrow nets, snowbanks that swallow loose
  pucks, wrist shots only, snow that slows the ice, a shovel break between
  periods.
- **The town rink:** boards and glass, goalies, slapshots, a tractor
  resurfacing the ice.
- **The arena:** lights, horn, replays on the big screen.
- **Your crew:** start with three builds (quick, balanced, big). Each win
  signs a player from the beaten team, with a signature move. Rivals
  remember.
- **Modes:** Journey (pond to arena), Quick game, Daily pond, Shootout,
  Head to head.

## Multiplayer, in three layers

1. **Couch:** co-op (friends on your crew against the computer, drop in and
   out) and versus on one screen, up to four players. *Milestone 1.*
2. **Online with friends:** room codes, rollback netcode (only button
   presses cross the network), the existing Supabase project to hand out
   codes and introduce the two machines, a relay when a direct connection is
   blocked. Opt-in, fixed-size messages only, friends-only codes.
3. **Rival crews:** your crew and your habits uploaded; friends meet it in
   their Journey, played by the computer the way you play.

## The groundwork online play needs, kept from line one

- The simulation (game.c, ai.c) uses no floating point: 16.16 fixed point
  (fixed.h), so the same presses give the same game on every machine.
- Button presses are the only input: a direction byte and three buttons per
  player per tick (PadInput). Computer players are worked out inside the
  step from the state and the game's own random stream.
- The whole state is one plain struct (Game): a copy saves it, a copy back
  restores it; Game_Hash fingerprints it for desync checks.
- The core takes up to four players' input from the start.
- tests/test_main.c proves it: identical games tick for tick, a save,
  replay and restore that lands on the same game, and a hash that notices a
  changed press.

## Milestone 1: the pond with couch play (built 2026-10-22, v0.1.0)

The rules core and the computer players; up to four players on pads and a
split keyboard; co-op and versus; drop in and drop out; faceoffs as a timing
duel; one-timers; body and poke checks; switching skater; snowbanks and the
shovel break; the night-time town, the pond and the skaters; menus, pause
and results; sounds and its own music theme (a swung jig in the shared
synth, so Chipforge can give it your own songs too).

## Everything else (built 2026-10-22, v1.0.0)

- **Venues:** each venue's ice is data (Ice: size, corner radius, nets,
  boards or snowbanks, goalies, slapshots, snow). The rinks are a little
  narrower with tighter corners and wider nets than the pond.
- **Goalies:** three styles (scrambler, butterfly, poker) with their own
  reach, depth and speed; a reaction delay by difficulty; rebounds; a slow
  puck frozen for a whistle and a faceoff. Saves are counted.
- **Slapshots:** hold Shoot to wind up (about two thirds of a second to
  full), release to fire: faster and wilder the longer the wind-up.
- **Signature moves:** Cannon (harder shots), Sniper (tighter aim), Dangler
  (hard to check, no slowdown with the puck), Pest (checks harder and from
  further), Playmaker (crisper passes, easier to receive), Wall (hard to
  knock off the puck, blocks more).
- **Journey:** three stages of three games (the pond tournament, the town
  league, the arena playoffs). Three chances; signing after each win; grudges
  that bring beaten teams back sharper and rougher; a friend's crew in the
  final when you have added one. Saved between sessions. Points: goals,
  one-timer goals, the win, the margin, shutouts, the stage, the title.
- **Shootout:** three misses and out; the goalie leans toward the side you
  favour and sharpens as you score.
- **Daily pond:** the day's seed picks the rivals and one twist (wind,
  blizzard, glassy ice, tiny nets); its own board; on the launcher's desk.
- **Scores and trophies:** the Journey, Shootout and Daily boards go through
  the arcade's score files (and ghost-sync to the online boards); six
  trophies; runs for the launcher's stats.
- **Keys:** both keyboard halves rebind in Settings.
- **Online (layer 2):** rooms by six-letter code through the leaderboard's
  Supabase project; STUN to learn each machine's public address; UDP hole
  punching (HELLO / HELLO_ACK at every address, ten a second); the relay
  (the project's Realtime broadcast channel, named after the room's random
  key) when that fails; rollback with two ticks of input delay, up to twelve
  ticks of guessing, the last 32 states kept, fingerprints every 120 ticks;
  the host's setup acked by the guest. Head to head (the same skills on both
  sides) or both players on one team against the computer.
- **Rival crews (layer 3):** this machine's players' habits (shots, passes,
  checks, puck time, where they aim) add up over games into four numbers the
  computer plays a crew by. A crew travels as one strict line of text under
  an eight-letter code; friends' crews are kept on the machine (up to eight)
  and one is drawn for each new Journey's final.

## What is verified, and what is not

- Headless tests (`make test`): the rules, the venues, goalies, slapshots,
  traits, twists, the shootout, the Journey and its save file, the packet
  format (including noise and damaged packets), rollback over a pretend
  network with lag, jitter, loss, duplicates and reordering (both machines
  end on the same game as one machine playing every press), crews, the
  STUN parser, a loopback socket, the server's JSON and the relay's messages.
- Two processes on one computer through the real online code (a stand-in
  for the room server): a direct game, and a relay game through the live
  Realtime channel with about 150 ms of lag; both ended with matching
  fingerprints, and the guest saw the host leave.
- The server's functions (schema.sql, version 3) on a local Postgres 16:
  every call, its refusals, its limits and the roles' access.
- Not yet: a game between two real computers on different home networks;
  the room and crew calls against the live server (they wait for the
  version 3 schema to be applied there); how the computer plays a friend's
  crew, by eye.
