# Ghost Arcade: making the games actually fun

Status: PLAN, nothing started. Written 2026-09-20 after the ten-game roadmap
shipped. Every game has correct rules and passing tests; none of that proves
it is *fun*. Most of them have never been played by a person. This plan is
about feel, onboarding, difficulty, variety and reasons to come back.

## Principles

1. **Playtest data beats my guesses.** I can't play, so the plan builds tools
   that make your ten minutes of play turn into concrete fixes.
2. **Fix the shared layer once** (ghost-common), then do per-game passes.
3. **Feel before features.** Sound, hit feedback and controls matter more than
   new modes.
4. **Every change keeps the tests green** and the house style (one light
   colour, T8/16/32/48 type, render-only effects).

## Phase 0: Playtest and tuning tools (do first, ~1 session)

- **Tuning panel (F2) in ghost-common.** Each game registers its key constants
  (speeds, gravity, spawn intervals, lives, ...) and you drag sliders live;
  values save to a per-game `tuning.ini` that the game reads. You find the
  fun numbers by feel and I bake them in.
- **Run log.** Each run appends one line to `~/.local/share/<game>/runs.log`:
  score, time survived, level reached, cause of death, inputs per second. I
  read these to find spikes (e.g. 60% of deaths on level 2 = too hard).
- **Feedback key (F1).** One key drops a timestamped note plus a screenshot
  into `~/.local/share/<game>/notes/`, so "that felt bad here" costs you one
  keypress and me a precise place to look.
- **Playtest checklist per game** (5 questions: understood the goal without
  help? first death felt fair? wanted one more go? anything confusing? best
  moment?).

## Phase 1: Shared foundation in ghost-common (~2 sessions)

Benefits all 11 games at once.

- **Music.** There is none today, and it is the biggest single gap. A small
  chiptune sequencer (square/triangle/noise channels, pattern data per game,
  generated WAV loops like the SFX) with one theme per game, menu music, and
  intensity layers (extra channel when danger is high). Volume split: music
  and SFX separately.
- **Game-feel toolkit.** Hit-stop (2 to 4 frame freeze on big events), screen
  shake with presets, particle presets (dust, spark, confetti), score pop-up
  and combo helper, subtle vignette/CRT pass. Used consistently instead of
  each game reinventing it.
- **Input.** Gamepad support (buttons and stick, rebindable), input buffering
  and coyote time helpers for the platformers, mouse where it fits.
- **Window.** Fullscreen and integer scaling, pause on focus loss, remembered
  window position.
- **Onboarding.** A shared "How to play" screen (goal, controls, one tip),
  shown once on first launch and always reachable from the menu.
- **Difficulty select.** Chill / Normal / Hard (multipliers on the game's
  tuning values), scores tracked per difficulty.
- **Attract mode.** Idle on the menu and a scripted/bot demo plays (we already
  have bots for Gemdive and Moondrop tests).
- **Accessibility.** Reduced flashing, colour-blind-safe alternates for the
  colour-coded things (Ghostmaze ghosts, Gemdive gems), bigger text option.
- **Opt-in online sync** with a clear first-run prompt.

## Phase 2: Per-game "fun audit" (~1 session per game, in priority order)

Each game gets the same audit, then targeted changes:

1. **First 30 seconds:** is the goal obvious? is there a safe first moment?
2. **Difficulty curve:** from the run logs; smooth ramp, no cliffs.
3. **Feedback:** every action has a sound, a visual and, where it matters, a
   hit-stop or shake.
4. **Risk and reward:** is there a greedy option worth the danger?
5. **Variety:** does minute 5 differ from minute 1?
6. **One-more-go:** what does the death screen say? Is restart instant?

Known issues to address, by game:

- **Girderclimb:** barrel speed and ladder odds are unproven; add the descending
  lift column, a second power-up, barrel "near miss" combo, better death
  readability, and a level-1 tutorial hint. Highest priority: it is the
  flagship and the least tested by a human.
- **Gemdive:** caves are generated and easy. Hand-design 10 to 20 real caves,
  add the in-game cave editor, magic walls / amoebas / butterflies for
  variety, and a par-time medal per cave.
- **Moondrop:** daily seed, cargo/bonus pads, wind or moving-pad late levels,
  a "perfect landing" streak.
- **Crawlshot:** trackball-feel mouse tuning, scoring for shooting spiders
  close, wave variety (bonus waves), a proper lives/extra-life cadence.
- **Ghostmaze / Lanehop / Rockdrift / Skyraid / Brickburst / Coilrush /
  Blockfall:** run the same audit once the run logs exist; likely wins are
  power-up variety, boss/mini-boss moments, and combo scoring.

## Phase 3: Reasons to come back (~2 sessions)

- **Achievements:** games append unlocks; the launcher shows titles and a
  progress bar. About 5 to 8 per game, some meta ones ("play all 11").
- **Daily challenge:** launcher passes a date seed; separate daily leaderboard.
  Fits Moondrop, Coilrush, Crawlshot, Girderclimb, Gemdive first.
- **Launcher stats and profile:** play time, streaks, favourite game, personal
  bests across the arcade; a "play something new" nudge.
- **Unlockables:** cosmetic palettes and ship/hero skins earned by
  achievements (render-only, cheap).
- **Two-player** for the games where it is natural (Blockfall versus, Rockdrift
  co-op).

## Phase 4: Distribution (blocked on one decision from you)

GitHub layout (monorepo vs per game), `gh auth login`, release builds, install
script, screenshots and a README per game. Do it after Phase 1 so what ships is
already good.

## Suggested order and rough size

| Step | What | Size |
|---|---|---|
| 0 | Tuning panel, run log, feedback key | 1 session |
| 1a | Music system + a theme per game | 1 to 2 sessions |
| 1b | Feel toolkit, gamepad, fullscreen, how-to-play, difficulty | 1 to 2 sessions |
| 2 | Fun audit: Girderclimb, Gemdive, Moondrop, Crawlshot first | 4 sessions |
| 2b | Fun audit: the other seven | 7 sessions |
| 3 | Achievements, daily challenge, stats, unlocks | 2 sessions |
| 4 | Distribution | 1 session |

## Definition of done for "enjoyable"

- A new player with no instructions clears the first stage/level of every game
  or dies knowing exactly why.
- Median first run lasts long enough to feel progress (target: 2 to 4 minutes),
  and the run log shows players choosing to restart.
- Every game has music, gamepad support, a how-to-play, three difficulties and
  at least five achievements.
- You, playing each game for ten minutes, name a moment you liked and nothing
  you'd call broken.

## Open decisions

1. Which games do you actually enjoy already (so I protect what works) and
   which feel weakest (so the first audits go there)?
2. Music style: authentic chiptune, or something more modern and synthy?
3. Do you want to share these publicly (drives Phase 4 and how polished the
   first-run experience must be), or is this for you?

---

# Direction from your feedback (2026-09-20)

Supersedes the "Open decisions" above. Music: chiptune core with modern
layering (decided by default, easy to change). Sharing publicly: undecided;
assume "maybe", so originality matters (see Ghostmaze and Girderclimb).

Your priorities: **settings, leaderboard, music, arcade sounds, new mechanics,
and a much better Ghost Launcher.**

## Shared systems (ghost-common), built first

- **Settings v2.** One shared screen with tabs: Audio (master / music / SFX
  sliders, test sound), Controls (rebinding with conflict detection, gamepad
  mapping), Video (fullscreen, scale, scanlines/CRT strength, screen shake,
  reduced flashing), Gameplay (difficulty, per-game options such as board size
  or aim assist). Settings apply live and every game gets the same screen.
- **Leaderboard v2.** Tabs for All-time / This week / Today / This machine;
  per mode and difficulty (and board size for Blockfall); your rank and the
  gap to the next score; date and run stats (level, time); better empty and
  offline states; the game-over card shows "you placed #N today".
- **Music engine.** Chiptune sequencer, one original theme per game plus menu
  and game-over stingers, intensity layers, separate music volume.
- **Arcade sound pass.** Richer SFX (layered, pitch-varied, no identical
  repeats), coin/ready/go jingles, combo and milestone stingers, a shared
  "GET READY / GAME OVER" voice-style synth cue, and ambient sound for the
  launcher (cabinet hum, distant machines, coin drop).
- **Feel toolkit, gamepad, fullscreen, tuning panel, run log** as in Phases 0
  and 1 above.

## Ghost Launcher redesign

The hall background is too plain. Plan:

- **Living arcade interior:** parallax back wall with neon signs, patterned
  carpet with light pools, drifting dust motes, flickering tube lights, a
  ceiling with cables and speakers. The selected cabinet throws coloured light
  on the floor and neighbours.
- **Cabinets that show their game:** each screen plays a short looping demo
  (attract mode from the games' bots or recorded input), not just an icon.
- **Better chrome:** marquee header with the Ghost Arcade logo, smoother
  transitions, a proper Settings screen (shared with the games), a Stats and
  Achievements page, Daily Challenge tile, and a coin counter that carries
  meaning (earned by playing, spent on unlocks).
- **Fixes:** I will audit it with screenshots for layout, scaling, focus and
  install-flow bugs; you tell me anything specific that annoys you.
- Ambient hall audio and music as above.

## Per-game direction

**Blockfall** (biggest opportunity)
- **Custom board sizes:** width 6 to 20 and height 12 to 30, chosen from a
  setup screen with presets (Classic 10x20, Wide, Tall, Tiny, Custom). The
  renderer scales cells to fit; scores are tracked per size.
- **Modes:** Marathon, Sprint (40 lines), Ultra (2 minutes), Zen, and
  **Power mode**.
- **Power-ups (Power mode):** Bomb (clears 3x3), Laser (clears a row/column),
  Freeze (stops gravity briefly), Slow, Sweep (removes the top junk rows),
  Wild piece (pick the next piece). They appear as special cells inside some
  pieces and trigger when the row clears or the piece locks.
- Modern basics if missing: hold, 7-bag, ghost piece, T-spin and combo
  scoring, back-to-back bonus, garbage-line mode against a bot.

**Coilrush** (restyle to your house look)
- Rework its art and sound to the Ghost Arcade style (one light colour, ghost
  motifs), keeping the fun core. Add power-ups, portals, moving hazards and
  themed boards; a proper "campaign" of boards plus the endless mode.

**Brickburst** (you like it: deepen it)
- Power-ups (multiball, laser, sticky paddle, wide/narrow, slow, shield),
  special bricks (explosive, multi-hit, moving, indestructible, ghost-brick
  that phases), boss levels, combo scoring, a level editor, more levels.

**Skyraid**
- Weapon upgrade paths and pickups, bombs, wingmen/options, mid-bosses and
  bosses with patterns, stage themes with scrolling backgrounds, score
  multiplier chains, risk/reward (grazing).

**Ghostmaze: rebuild, not tweak (it is too close to Pac-Man)**
- Keep the lantern idea and throw out dots, pellets and chase-the-player
  ghosts. New core, **Ghostmaze 2.0: light-cone stealth**: you are a ghost in
  a procedurally generated haunted mansion; hunters carry lanterns with real
  light cones and line of sight; you collect souls and reach the exit unseen.
  Fog of war, doors and secret passages, abilities with cooldowns (phase
  through one wall, hush footsteps, decoy echo), hunter types with different
  senses, new mansion every level from a seed (daily seed fits).

**Rockdrift**
- Design a real ship (hull, wings, cockpit, engine glow, thrust flame,
  banking as it turns) in the same vector-glow style; ship variants,
  power-ups (shield, spread shot, ram), UFO variety, a boss rock.

**Lanehop** (you are unsure: my recommendation)
- Make it stop being "Frogger with a hare": add an **endless procedural mode**
  where the camera scrolls up through generated roads, rivers and rail tracks
  with coins, power-ups and unlockable characters; keep the classic five-bay
  mode as a second mode. Daily seed.

**Crawlshot** (a good one: extend it)
- Power-ups, a boss centipede, new enemy types, bonus waves, a two-player
  co-op mode, a mouse/trackball option tuned properly.

**Moondrop** (needs improvement)
- Bigger, scrolling terrain with a lander sprite that looks like a lander,
  particle exhaust, wind and gravity variants, moving and crumbling pads,
  cargo and rescue missions, night levels with a searchlight, time attack,
  daily seed, proper landing replay on crash.

**Gemdive to Gemdive (rename)**
- Rename everywhere (folder, binary, catalog, scores slug). Concept: dive
  deeper each cave. Hand-designed caves plus a cave editor; magic walls,
  amoebas, butterflies, keys and doors, dig-and-pump enemies, lighting that
  darkens with depth, par-time medals.

**Girderclimb: reinvent (it is too close to Donkey Kong)**
- Drop the ape-with-barrels, princess and rivet-screen homage. New core:
  **a vertical-scrolling haunted tower climb**: wall-jump and ledge-grab,
  moving and crumbling platforms, ghosts patrolling floors, a lantern that
  reveals hidden platforms, checkpoints, and a boss ghost at the top of each
  tower. Keep the good engine parts (committed jumps, ladders, hazards,
  hammer-like power-up) but change the structure and theme completely.

## Order of work

| Wave | What | Why first |
|---|---|---|
| A | ghost-common: settings v2, leaderboard v2, music engine, sound pass, feel toolkit, gamepad/fullscreen, tuning panel, run log | Every game benefits, and the rest builds on it |
| B | Launcher redesign (living hall, demo cabinets, stats/achievements, shared settings) | It is the front door and you called it out |
| C | Quick, visible wins: Rockdrift ship, Gemdive to Gemdive rename, Blockfall overhaul | Most feedback, least risk |
| D | Rebuilds: Ghostmaze 2.0, Girderclimb reinvention | Biggest design work; needs your concept sign-off |
| E | Deepen the rest: Brickburst, Skyraid, Crawlshot, Moondrop, Coilrush, Lanehop | Uses everything from A |
| F | Achievements, daily challenge, unlocks, distribution | Meta layer once the games are worth it |

## Decisions (2026-09-20)

- **Ghostmaze rebuild:** possession puzzle-action (you are a ghost that
  possesses objects and people; each host has its own move; a priest with a bell
  hunts you). Not the light-cone stealth or twin-stick options.
- **Girderclimb reinvention:** grapple swing climber (grapple hook and
  momentum, wall-run, a rising hazard from below). Not the haunted tower or
  puzzle floors options.
- **Start:** waves A, B and C together, in that order: shared foundation,
  launcher redesign, quick wins (Rockdrift ship, Gemdive rename, Blockfall).

## Progress log

- 2026-09-20 wave A (part): music engine (12 procedural themes, all games), SFX
  upgrade and jingles, shared prefs + scrolling settings with music volume /
  screen shake / reduced flashing rows, leaderboard v2 (medals, standing,
  All/Week/Today filter on T). All 11 games rebuilt, tests green.
  STILL TO DO in wave A: gamepad, fullscreen scaling, tuning panel, run log,
  feel toolkit (hit-stop etc.), music intensity per game, make the shake and
  flashing prefs actually affect rendering.

- 2026-09-20 wave B and C (part): launcher redesign (attract-mode cabinet
  screens, wallpaper, neon sign, lamps, floor reflection, vignette); Rockdrift
  real ship; Gemdelve renamed Gemdive; Blockfall 2.0.0 (custom board sizes,
  five modes, hold, combos, five power-ups, per-mode-and-size scores, adaptive
  renderer). All 12 projects rebuilt, tests green.
  NEXT: gamepad and remaining wave A items; launcher stats/achievements page;
  Coilrush restyle; deepen Brickburst/Skyraid/Crawlshot/Moondrop/Lanehop;
  then the two rebuilds (Ghostmaze possession, Girderclimb grapple).

- Daily challenge: shipped for 7 games (D key in launcher, `--daily`, seed per local date, `daily-YYYYMMDD` score mode). Blockfall/Coilrush/Ghostmaze/Gemdive not yet.
