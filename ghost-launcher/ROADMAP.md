# Ghost Arcade Roadmap

The plan for every game in the Ghost Launcher catalog, plus the shared pieces
that make them feel like one arcade instead of ten unrelated binaries.

Status legend: **SHIPPED** · **IN PROGRESS** · **PLANNED**

| # | Game        | Homage to                 | Size | Status      |
|---|-------------|---------------------------|------|-------------|
| 0 | Blockfall   | Tetris                    | M    | SHIPPED     |
| 1 | Coilrush    | Snake / Nibbles           | S    | SHIPPED     |
| 2 | Brickburst  | Breakout / Arkanoid       | S    | SHIPPED     |
| 3 | Skyraid     | Space Invaders            | S-M  | SHIPPED     |
| 4 | Rockdrift   | Asteroids                 | M    | SHIPPED     |
| 5 | Ghostmaze   | Pac-Man                   | M-L  | SHIPPED     |
| 6 | Lanehop     | Frogger                   | M    | SHIPPED     |
| 7 | Crawlshot   | Centipede                 | M    | SHIPPED     |
| 8 | Moondrop    | Lunar Lander              | S-M  | SHIPPED     |
| 9 | Gemdive    | Dig Dug / Boulder Dash    | L    | SHIPPED     |
| 10| Girderclimb | Donkey Kong               | L    | SHIPPED     |

Size: S = one session, M = two, L = three or more. All names are working
titles -- original names on purpose (same rule as Blockfall: homage the
mechanics, never the trademark, art, or audio).

---

## 1. The shared skeleton (every game follows this)

Lifted directly from Blockfall so a new game is "copy the shell, write the
core":

```
<game>/
  src/
    rng.c/h        xorshift64* (copy verbatim from Blockfall)
    <core>.c/h     ALL game rules. No raylib include. Deterministic given seed.
    ghostlink.c/h  Ghost Arcade data contract (section 2). No raylib include.
    persist.c/h    settings (config.ini) + save data (save.dat), XDG dirs
    input.c/h      key-name <-> keycode table, RepeatButton
    audio.c/h      SfxId enum + load/play
    render.c/h     every draw call; owns Begin/EndDrawing
    main.c         window, app state machine (MENU/PLAYING/SETTINGS/UPDATES/...)
    version.h, changelog.h
  tests/test_main.c   CHECK() macro suite, builds WITHOUT raylib
  tools/gen_sounds.py, tools/gen_icon.py   stdlib-only asset generators
  assets/{fonts,icons,sounds}, packaging/<game>.desktop, Makefile, README.md
```

Hard rules:

1. **Core never includes raylib.** Core exposes one-frame `just*` event flags;
   main.c turns them into sounds. This is what makes `make test` headless.
2. **Fixed timestep inside the core** for anything physics-like (Brickburst,
   Rockdrift, Moondrop, Girderclimb): `Game_Update(dt)` accumulates and runs
   `Game_Step()` at 120 Hz so tests are frame-rate independent and replays
   are deterministic.
3. **Levels are text** (ASCII grids in `assets/levels/`), parsed by the core
   from a string so tests can feed layouts inline.
4. **Same chrome everywhere:** 620x720 window unless the game needs landscape,
   Press Start 2P, the Blockfall palette (`kBg 14,15,22`, `kPanelBg`,
   `kAccent`), menu = title / items / high score / "MADE BY THE REAL GHOST".
   Each game gets ONE signature accent colour and its own animated menu
   background.
5. `make`, `make run`, `make test`, `make debug` (ASan/UBSan), `make install`
   to `~/.local`, catalog line appended to Ghost Launcher's `games.txt`.

Worth doing once game #3 exists (not before -- two copies is cheaper than a
premature library): extract `rng`, `ghostlink`, `input`, `audio`, the
`PText`/menu/settings/updates renderers into `ghost-common/` and vendor it.

---

## 2. Ghost Arcade data contract

Plain text under `$XDG_DATA_HOME/ghost-launcher/` (default
`~/.local/share/ghost-launcher/`). The launcher owns the directory; games
only touch their own score file.

| File                  | Writer   | Reader   | Format                          |
|-----------------------|----------|----------|---------------------------------|
| `profiles.txt`        | launcher | games    | `Game Name\|username`           |
| `playtime.txt`        | launcher | launcher | `Game Name\|seconds`            |
| `scores/<slug>.txt`   | game     | both     | `mode\|username\|score\|YYYY-MM-DD` |
| `scores/global/<slug>.txt` | ghost-sync | both | same rows: the online top 10 per mode |
| `install_id`          | ghost-sync | ghost-sync | random UUID, makes re-uploads idempotent |

- `<slug>` is the binary name (`coilrush`). `Game Name` is the catalog name.
- Score files hold the top 10 per mode, sorted descending, rewritten whole.
- Name lookup order in `profiles.txt`: the game's own row, then the
  arcade-wide `__DEFAULT__` row (launcher: U; per-game override: Shift+U),
  then `PLAYER`. `|` in a username is
  stored as `/`.
- Games must work with the directory missing (launched standalone).
- Implemented by `ghostlink.c/h`; first shipped in Coilrush. Blockfall gets it
  retrofitted (mode = `marathon`).

### Online leaderboard (built 2026-09-19)

Games never touch the network. `ghost-sync` (in `sync/`, the only libcurl
user, installed next to the launcher) does both directions against Supabase:

- **Upload:** re-sends every valid row of every `scores/<slug>.txt` as one CSV
  upsert per game. The server's unique constraint on
  `(install_id, game, mode, username, score, played_on)` plus
  `Prefer: resolution=ignore-duplicates` makes that idempotent, so there is
  no "what have I already sent" state to get out of step. A rejected batch is
  retried row by row so one bad line can't block the rest.
- **Download:** one request to the `leaderboard` view (each player's best run
  per game+mode, top 10), split into `scores/global/<slug>.txt`, written
  atomically via rename.
- Games call `GhostLink_TriggerSync()` (double-fork, detached) at startup and
  after each finished run, then just read the text files.
- Server side is `online/schema.sql`: RLS allows select + insert only, CHECK
  constraints bound every column, no update/delete. The shipped key is a
  publishable key (`sync/online_config.h`); never commit a secret key.
- Players can opt out with `enabled=0` in `~/.config/ghost-launcher/online.conf`.
  **Before distributing to other people**, make this an explicit first-run
  opt-in rather than on-by-default.
- Known limit: a client-side game can't prove a score is real. Constraints
  stop vandalism of other rows, not someone faking their own.

---

## 3. The games

### 1. Coilrush -- Snake / Nibbles  (S, SHIPPED 2026-09-19, ~/Work/ghost-arcade/coilrush)
- **Pitch:** grid snake with three modes: CLASSIC (walls kill), WRAP (edges
  wrap), MAZE (Nibbles-style stages -- clear 8 food, advance to the next wall
  layout, snake resets, score carries).
- **Core:** 24x24 occupancy grid, ring-buffer body, 2-deep turn queue that
  rejects reversals, tail-vacate rule (moving into the cell your tail is
  leaving is legal), timed bonus food, speed ramp per level, win on full board.
- **Tests:** movement, reversal rejection, turn buffering, growth, wall/self
  death, wrap, food never spawns on snake/wall, bonus lifecycle, every maze
  layout is fully connected and leaves the spawn lane clear, ghostlink.
- **Look:** green gradient snake with eyes, accent = green. Menu bg: snakes
  crawling across the screen.
- **Why first:** smallest game; proves the skeleton copy + the data contract.

### 2. Brickburst -- Breakout / Arkanoid  (S, SHIPPED 2026-09-19, ~/Work/ghost-arcade/brickburst)
- **As built vs. this plan:** 12 layouts (not 30) embedded as strings in
  `levels.c` rather than files under `assets/levels/` -- same parser-from-
  string idea, less plumbing. Capsules are x3 / wide / slow / fireball / +1
  (no laser or catch yet). Collision is closest-point circle-vs-brick at a
  fixed 120 Hz step, one brick per step, instead of swept AABB: at the speed
  cap the ball moves under 5 px a step, which the no-tunnelling test proves
  is enough. Mouse control is proportional and capped at key speed.
- **Ideas left on the table:** laser + catch capsules, moving enemies,
  18 more layouts, a level editor.

Original plan:
- **Pitch:** paddle, ball, 30 text-file levels, falling powerups.
- **Core:** fixed-step ball physics; swept AABB vs brick grid (resolve on the
  axis of least penetration, one brick per sub-step so the ball can't tunnel);
  paddle reflection angle from hit offset (-60..+60 deg), ball speed ramps
  per paddle hit and caps. Brick types: normal, 2-hit, 3-hit, indestructible,
  explosive (clears neighbours). Powerups: multiball (x3), wide paddle, laser,
  slow, catch, extra life. Lives = 3.
- **Levels:** `assets/levels/NN.txt`, 13 cols x up to 18 rows, chars
  `.`/`1`/`2`/`3`/`#`/`*` + a colour row. Core parses from string.
- **Tests:** reflection angles, corner hits, no tunnelling at max speed,
  multi-hit bricks, explosive chain, level-clear ignores indestructibles,
  each powerup's effect + expiry, every shipped level parses and is clearable.
- **Look:** rainbow brick rows, ball trail, screen shake on explosive. Accent
  = orange. Mouse paddle control optional alongside keys.
- **Risk:** stuck-ball loops (ball bouncing horizontally forever between
  indestructibles) -- add a minimum vertical speed component.

### 3. Skyraid -- Space Invaders  (S-M, SHIPPED 2026-09-19, ~/Work/ghost-arcade/skyraid)
- **As built:** everything in the plan below, with the house brand's own
  cast (bats / skulls / ghosts) instead of aliens, accent cyan rather than
  lime (Coilrush already owns green), CRT scanlines as a Settings toggle,
  and the march bass line done as ONE sample retriggered at four pitches on
  each formation step, so the tempo follows the invaders for free. First
  game built entirely on `ghost-common`.

Original plan:
- **Pitch:** 11x5 marching formation, four eroding bunkers, mystery ship.
- **Core:** formation moves as one unit on a step timer whose interval is a
  function of invaders alive (the authentic speed-up); edge hit = drop + flip.
  One player shot on screen at a time (the classic constraint that makes
  aiming matter). Up to 3 enemy shots, fired from random bottom-most invaders.
  Bunkers are small bitmaps (22x16) eroded by a blast stamp on any hit.
  Mystery ship every ~25 s, score from a shot-count table. Waves start one
  row lower each time.
- **Tests:** speed curve vs alive count, edge/drop/flip, only bottom-most
  invaders shoot, bunker erosion bitmap, one-shot rule, wave start height,
  invasion (formation reaches player row) = game over.
- **Look:** 1-bit two-frame sprites defined as strings in code, colour bands
  by row, CRT scanline overlay toggle. Accent = lime. The 4-note march SFX
  tempo tracks the formation step timer.

### 4. Rockdrift -- Asteroids  (M)
- **SHIPPED 2026-09-19 (~/Work/ghost-arcade/rockdrift).** As built: everything below, in
  lime (the roadmap's cyan went to Skyraid), landscape 800x640, and the glow
  done as a queue of lines drawn twice (additive halo + bright core). The
  small saucer's aim error is `Game_SaucerAimError(score)`, 0.55 rad at 0
  down to 0.08 at 30,000, tested statistically. Not built: a shield, or
  asteroid-on-saucer collisions.
- **Pitch:** inertia ship, wrapping playfield, splitting rocks, two saucers.
- **Core:** fixed-step float physics, thrust + drag-free drift, toroidal wrap
  for everything. Rocks: large -> 2 medium -> 2 small, random jagged polygons
  (8-12 verts from seed). Collision = circle tests (polygon only for drawing).
  Max 4 player bullets with lifetime. Large saucer fires randomly, small
  saucer aims (accuracy rises with score). Hyperspace = random teleport with
  a 1-in-6 chance of exploding. Extra ship every 10,000.
- **Tests:** wrap math, split counts and conservation, bullet lifetime/cap,
  saucer aim error bounds, hyperspace safety odds with fixed seed, spawn
  safety (ship respawns only when centre is clear).
- **Look:** the odd one out on purpose -- pure vector lines with additive
  glow (draw each line 3x at falling alpha), no filled shapes, particle debris
  lines on ship death. Accent = white/cyan. Landscape window 800x600.

### 5. Ghostmaze -- Pac-Man  (M-L, SHIPPED 2026-09-19, ~/Work/ghost-arcade/ghostmaze)
- **As built:** everything below in ONE session rather than three: an original
  symmetric maze (generated + validated by `tools/gen_maze.py`), the four
  targeting rules, scatter/chase schedule with forced about-face, candles and
  the 200-1600 chain, eyes returning home (proven from all 356 open tiles),
  pen release by dots or starvation, tunnel, fruit, extra life. Light colour
  pink; the player is a pale ghost, the chasers are lanterns. **Not built:**
  per-level speed tables beyond a linear ramp, intermissions, a second maze.

Original plan:
- **Pitch:** maze chase; on-brand for "The Real Ghost" and already homaged in
  the Retro Gaming theme wallpapers.
- **Core:** 28x31 tile maze from a text file. Actors move in sub-tile steps;
  turns are pre-buffered and taken at tile centres (cornering). Four chasers
  with the four classic targeting rules: direct chase; 4 tiles ahead of the
  player; vector-doubled from chaser #1 through 2-ahead; chase until within 8
  tiles then retreat to corner. Scatter/chase schedule table per level,
  frightened mode (random turns, slowed, 200/400/800/1600 chain), eyes return
  home, pen release by dot counters, tunnel slow-down, no-reverse rule except
  on mode switch, fruit at 70 and 170 dots, level speed table.
- **Tests:** each targeting rule against hand-computed tiles, mode schedule
  timing, forced reversal on mode switch, frightened chain scoring, pen
  release order, tunnel wrap, maze file: all dots reachable. Fully
  deterministic -> a recorded input sequence must replay to the same score.
- **Look:** neon-blue maze walls drawn from tile neighbours (auto-tiling),
  original character designs (player is NOT a yellow wedge -- make the
  *player* the ghost and the chasers little exorcist lanterns; flips the
  homage and fits the brand). Accent = violet.
- **Plan:** session 1 = maze, movement, dots, one chaser. Session 2 = all four
  AIs, modes, frightened, pen. Session 3 = fruit, level tables, intermission
  polish, second maze.

### 6. Lanehop -- Frogger  (M)
- **SHIPPED 2026-09-19 (~/Work/ghost-arcade/lanehop).** As built: the hero is a hare, the
  bays are burrows; lanes are data rows (direction, speed, pattern string)
  that the renderer, the rules and the tests all read; a lookahead bot in the
  tests proves the lanes are crossable (a hare home in all 6 seeded games).
  Coral light. **Not built:** the lady-frog escort and the snake on the
  verge; the fly and crocodile ARE in.
- **Pitch:** cross 5 traffic lanes and 5 river lanes to fill 5 home bays.
- **Core:** hopper moves in tile hops (with short hop animation lock); lanes
  are data rows `{direction, speed, pattern string, kind}`; objects scroll and
  wrap. Road = touching a vehicle kills; river = NOT standing on a log/turtle
  kills, and you ride at the lane's speed (carried off-screen = death).
  Diving turtles cycle visible/submerged. Timer bar per life; bonus fly and
  lady-frog escort for points; filled bay or bay with croc head = death.
  Difficulty = lane speed table per level.
- **Tests:** ride velocity, carried-off death, turtle dive timing, bay
  alignment tolerance, timer expiry, lane wrap continuity, level-complete
  on 5 bays.
- **Look:** chunky top-down pixel sprites built from string bitmaps; split
  palette (asphalt greys below, river blues above). Accent = teal.

### 7. Crawlshot -- Centipede  (M)
- **SHIPPED 2026-09-20 (~/Work/ghost-arcade/crawlshot).** As built: 30x32 field of 18px
  tiles (540x576). Caterpillars are `Worm` records of cells (seg[0] = head)
  stepped on a timer, so the body always follows the head's exact path; a hit
  segment becomes a 4-hp toadstool and the segments behind it become a new
  worm. Waves split 12 segments into a shorter worm + single heads that enter
  on a delay; the palette rotates through six pairs. Poison dive ends at the
  bottom and continues sideways. The flea only appears below 5 toadstools in
  the garden, so the field starts with 8 garden toadstools. Death heals
  damaged/poisoned toadstools one at a time (+5 each, sparkle per cell)
  before the fresh worm. Optional mouse steering (Settings). 128 checks,
  plain + ASan/UBSan. Not yet played by a human, only bots and screenshots.
- **Pitch:** shoot a segmented crawler descending through a mushroom field.
- **Core:** 30x32 grid of mushrooms (4 hp each). Crawler segments follow
  head logic: move horizontally, on obstacle/edge drop one row and reverse;
  shot segment becomes a mushroom and splits the chain into two crawlers with
  a new head. Player confined to the bottom 6 rows, free 2D movement, rapid
  single-shot. Extras: spider (zig-zags through player zone, eats mushrooms,
  score by distance), flea (drops when few mushrooms are in the player zone,
  leaves a trail), scorpion (poisons mushrooms -> crawler dives straight down).
- **Tests:** split produces correct heads/lengths, drop-and-reverse, poisoned
  dive, mushroom hp + restore bonus on death, flea trigger threshold, spider
  scoring bands.
- **Look:** palette rotates every wave (the original's signature). Accent =
  magenta. Mouse aiming optional (trackball homage).

### 8. Moondrop -- Lunar Lander  (S-M)
- **SHIPPED 2026-09-20 (~/Work/ghost-arcade/moondrop).** As built: 800x640 landscape, 768x480
  field, glow-line look shared with Rockdrift. Constant acceleration is
  integrated exactly, so free fall matches the closed form. Terrain is
  midpoint displacement (wrapping), pads flat and never touching, always an x5
  no wider than 3 points. `Game_Classify` is the landing truth table. A crash
  costs 200 fuel, not a life; a landing refuels 150. A simple landing bot
  (in the tests) lands ~95% of attempts, which is the fairness check. The
  daily-seed mode is NOT built. 92 checks, plain + ASan/UBSan.
- **Pitch:** land on procedurally generated terrain with limited fuel.
- **Core:** fixed-step physics: gravity, rotation, thrust along ship axis,
  fuel burn. Terrain = midpoint-displacement polyline from seed with 3-4
  forced flat pads of differing widths (narrow = x5 multiplier). Landing OK
  iff both feet on a pad, |vx| and vy under thresholds, tilt < 6 deg. Score =
  pad multiplier x base + fuel bonus; fuel carries between landings; game ends
  when fuel is gone. Camera zooms in near the surface.
- **Tests:** physics integration vs closed form (free fall), landing
  classifier truth table (speed/tilt/pad edges), terrain always contains the
  required pads and no overhangs, fuel accounting, seeded terrain is stable.
- **Look:** vector lines like Rockdrift (shares the glow-line helper), HUD
  readouts for altitude / h-speed / v-speed / fuel. Accent = amber. Daily-seed
  mode is a natural fit here.

### 9. Gemdive -- Dig Dug / Boulder Dash  (L)
- **SHIPPED 2026-09-20 (~/Work/ghost-arcade/gemdive).** As built: deterministic 9/s tick
  (player, then a top-left-to-bottom-right rock pass with `moved` flags, then
  crawlers). No RNG anywhere, so a string of inputs replays exactly. The 20
  caves are text files generated by `make caves` (tools/cavegen.c + the
  lookahead bot in src/bot.c); a cave is only written if the bot finishes it,
  and its `solution=` is replayed by `make test`. Caves are generic and on the
  easy side; the in-game cave editor from the risk note is NOT built.
  104 checks, plain + ASan/UBSan.
- **Pitch:** tunnel through dirt, collect all gems, reach the exit, don't get
  crushed.
- **Core:** Boulder Dash-style cellular update scanned top-left to
  bottom-right once per tick: boulders/gems fall, roll off rounded objects,
  crush what's below when *already falling*. Player digs dirt, pushes single
  boulders horizontally. Enemies: wall-hugging crawlers (left-hand rule) that
  explode 3x3 when crushed -- some explode into gems. Level needs N gems to
  open the exit, with a time limit. 20 text-file caves.
- **Tests:** the falling/rolling rule table cell by cell, "resting boulder
  doesn't kill, falling one does", push rules, explosion stamp, wall-hugger
  pathing, exit opens at quota, every shipped cave is solvable (store a
  solution input string per cave and replay it in the test suite).
- **Look:** scrolling camera over a 40x22 cave, sparkle animation on gems,
  dirt texture from a 2-colour hash. Accent = gold.
- **Risk:** level design is the real cost. Build a tiny in-game cave editor
  (admin-only, like the launcher's admin mode) in session 2.

### 10. Girderclimb -- Donkey Kong  (L)  *the flagship*
- **SHIPPED 2026-09-20 (~/Work/ghost-arcade/girderclimb).** As built: all four stages
  (ramps, rivets, lifts, belts) in one pass rather than three sessions, on a
  data-driven stage builder and one hazard system (barrels, wild barrels,
  wisps, springs, pies). Boss is a giant ghost; the captive is a lantern-ghost.
  Jumps are committed and measured for the fall rule from takeoff. The tests
  walk every stage's goal by real ladder routes and brute-force the timing of
  the lift jumps. Not built: elevators' second (descending) column, a second
  hammer power-up type. Not human-played, so barrel speeds and the ladder
  probability are unproven for fun. 140 checks, plain + ASan/UBSan.
- **Pitch:** single-screen platformer: climb sloped girders and ladders to
  the top while hazards roll down.
- **Core:** fixed-step platformer physics on girder segments (sloped lines,
  not tiles); ladders (whole and broken); jump arc is committed (no air
  control, authentic). Rolling hazards follow girders, randomly take ladders
  down, fall off ends; "wild" ones bounce. Jumping a hazard scores by
  proximity check at apex. Hammer powerup (timed, no climbing/jumping while
  held). Four stage types cycled: ramps, rivet removal, elevators, conveyors.
  Bonus timer counts down per stage.
- **Tests:** slope walking keeps feet on girder, ladder attach/detach zones,
  jump arc apex/length, hazard ladder decision with fixed seed, jump-over
  scoring window, hammer timing, rivet stage completion, fall-distance death.
- **Look:** original cast -- the big boss at the top is a giant ghost hurling
  cursed barrels. Accent = red/pink girders. Stage intro "HOW HIGH CAN YOU
  GET?"-style height card.
- **Plan:** session 1 = movement/ladders/jump on the ramp stage. 2 = hazards,
  scoring, hammer, lives. 3+ = the other three stage types.

---

## 4. Ghost Launcher work (in parallel)

**Done 2026-09-19 (launcher redesign):** items 1-3 below, as "the arcade
floor" -- a scrolling row of procedurally drawn cabinets (marquee, screen
with the game's icon, control panel, coin door), the selected one lit in a
colour extracted from its icon, over an instruction card with your best /
world best / time played. Type is strictly 8/16/24 px (Press Start 2P is an
8x8 design and only renders evenly at multiples of 8) -- **carry that rule
into the games next**; they still use 9-14 px sizes. Blockfall 1.2.0 now
writes scores (mode `marathon`) too.

1. **Read `scores/<slug>.txt`** -- show best score per game in the list and a
   per-game leaderboard on the Stats screen. (Needs a `slug` -- derive from
   the exec basename; no catalog format change.)
2. **Tile grid view** with big pixel icons once the catalog passes ~6 games;
   the single-column list won't scale to 11.
3. **Global username** with per-game override (today it's per-game only, so a
   new game always starts as `PLAYER`).
4. **Achievements:** games append `id|date` lines to
   `achievements/<slug>.txt`; the launcher owns the id -> title/description
   table via an `achievements.txt` shipped in each game's assets dir.
5. **Daily challenge:** launcher passes `--seed YYYYMMDD` to games that
   support it; scores land in mode `daily-YYYYMMDD`.
6. **Distribution:** *local half done 2026-09-19* -- the catalog has an
   optional 5th field `install-from` (a source folder); a missing game shows
   a NOT INSTALLED sign and Enter runs `make install` there in the
   background. That only helps on a machine that has the source. Remote
   half, still to do: GitHub Releases + remote `manifest.txt` (name, version,
   tarball URL, sha256); launcher shows Install/Update. Blocked on the GitHub
   repo decision (monorepo vs per-game) and `gh auth login`.
7. **Online leaderboard** via Supabase -- plumbing DONE (`ghost-sync`,
   `online/schema.sql`, Coilrush LOCAL/GLOBAL score views). Remaining: show
   the global board in the launcher, first-run opt-in, retrofit Blockfall.
8. **Tests for the launcher itself** -- `make test` now exists and covers the
   sync data layer; manifest/stats/profile parsing (all raylib-free already)
   still need adding to it.

---

## 5. Build order and why

1. **Coilrush** -- proves skeleton + data contract on the smallest game.
2. **Launcher: score display + tile grid** -- so new games land somewhere nice.
3. **Brickburst** -- DONE. Its fixed-step pattern (`Game_SetInput` +
   `Game_Update` accumulating into `Game_Step` at 120 Hz) is the template
   for 4, 8, 10.
4. **Skyraid** -- first string-bitmap sprite system; reused by 6, 7, 9, 10.
5. **Extract `ghost-common/`** -- DONE, and all five games are migrated onto
   it (rng, ghostlink, keynames, sfx, ui). A house-style change is made once.
6. **Ghostmaze** -- the showpiece, built on a mature skeleton.
7. **Rockdrift + Moondrop** -- back to back; they share the vector-glow
   renderer and the physics step.
8. **Lanehop, Crawlshot** -- SHIPPED.
9. **Gemdive, Girderclimb** -- SHIPPED. The roadmap of ten games is complete.
