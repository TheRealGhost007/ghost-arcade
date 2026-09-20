# Gemdive

A Boulder Dash / Dig Dug homage for Ghost Arcade: tunnel through dirt, collect
enough gems to open the exit, and get out before the clock runs down. C17 +
raylib; the cave scrolls to follow the digger.

## Rules

- Everything happens on a **tick** (about 9 a second, a little faster each
  time round the set): you move first, then the boulders and gems settle
  (scanned top-left to bottom-right, each moving at most once), then the
  crawlers move. There is no randomness, so a run of inputs always plays out
  the same way.
- **Boulders and gems fall** into empty space, and **roll off anything round**
  (another boulder, a gem, a brick wall) if the space beside and below is free.
- A rock that is **already falling** when it reaches you or a crawler crushes
  it. A boulder that is just sitting on you does nothing.
- Push one boulder sideways onto empty ground. Never up, never down.
- **Crawlers** keep their left hand on the wall and kill on touch. A boulder
  dropped on one blows a 3x3 hole (steel survives); the blast sets off any
  other crawler in it. Some crawlers leave gems where they blow up.
- Gems are 10 points, 15 once the exit is open. Reaching the open exit pays 100
  plus 2 a second for the time left. Extra digger every 3,000 points.
- Lose a life and the cave starts again. There are 20 caves; after the last the
  set repeats, quicker.

## Caves

`assets/caves/NN.cave` are plain text (see `src/cave.h` for the format). They
are made by `make caves`: a generator lays out candidate caves, a bot plays
each one, and only a cave the bot can finish is written -- with the bot's
inputs recorded in the file as `solution=`. `make test` replays every solution,
so a cave that ships is a cave that has been finished. You can drop in your own
by number.

## Controls

Arrows or WASD to dig and move (hold to keep going, or tap for one step),
P pauses, R restarts, Esc returns to the menu. All rebindable in Settings.

## Build

    make            # build/gemdive
    make test       # rules tests + replay of all 20 solutions (no raylib needed)
    make caves      # regenerate assets/caves
    make install    # ~/.local/bin + ~/.local/share/gemdive
    python3 tools/gen_sounds.py && python3 tools/gen_icon.py

Shared UI, sound, keys and the online score client come from
`../ghost-common`. Scores are shared with Ghost Launcher under the "arcade"
mode of the `gemdive` table.
