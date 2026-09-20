# Girderclimb

A grapple-swing climber for Ghost Arcade. You climb a burning tower of girders
with a grapple hook: fire at a ring above you, reel in, pump your swing, and
let go to fly to the next one, while cursed fire rises from below and gets
quicker. C17 + raylib.

(This replaces the original Girderclimb, a Donkey Kong-style platformer. The
old code is kept at `../girderclimb-classic` and is not installed.)

## How it plays

- **Space** fires the grapple at the ring you are aiming at: the nearest one
  clearly above you, and leaning left or right prefers rings that way. A
  reticle marks it. Press Space again to let go.
- **Up** reels the rope in (that is how you climb), **Down** lets it out,
  **Left/Right** pump your swing. Letting go keeps the speed you had, so a good
  swing carries you a long way.
- **X** jumps from a ledge. Ledges are one-way (you can rise through them) and
  the ones with a flag are checkpoints: lose a life and you restart from the
  last one you touched.
- The fire rises from the start and accelerates, so hanging around loses. From
  tower two, ghost orbs drift across the shaft. Reach the top ledge to clear the
  tower; each next one is taller and the fire is quicker.
- Points for height, for each ring, for each checkpoint, and a bonus for the
  time left when you clear a tower. Extra life every 8,000.

The rope is a real distance constraint: if you would be further from the ring
than the rope is long, you are pulled back onto the circle and lose the outward
part of your velocity, which is what turns a fall into a swing.

## Build

    make            # build/girderclimb
    make test       # headless tests, including a bot that climbs 180 towers
    make install    # ~/.local/bin + ~/.local/share/girderclimb
    python3 tools/gen_sounds.py && python3 tools/gen_icon.py

Towers are generated from a seed, top down: a ring above the goal ledge, then
evenly spaced rings (never more than ~125 apart) down to one just above the
floor, with a rest ledge under a ring every ~700 units. The tests build hundreds
of them and climb them with a simple reel-and-release bot, so each is provably
reachable (with the orbs off); a bot that ignores the orbs still makes it more
than half the time, so they can be avoided.
