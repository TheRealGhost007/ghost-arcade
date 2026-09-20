# Ghostmaze

A possession puzzle-action game for Ghost Arcade. You are a ghost in a haunted
house, and a priest with a bell walks the halls. **His sight destroys a ghost**
(you can see exactly what he sees: the yellow beam along the way he faces). So
you possess things, and slip from one to the next, to reach the exit mirror,
which only a ghost can use. C17 + raylib.

(This replaces the original Ghostmaze, a Pac-Man-style maze game. The old code
is kept at `../ghostmaze-classic` and is not installed.)

## The hosts

- **Cat**: small enough to squeeze through **cat flaps** and grates.
- **Armor**: slow (a move every other tick), but strong. It smashes **cracked
  walls** and pushes **crates**.
- **Servant**: picks up **keys** by walking over them, and opens **locked
  doors**.

A ghost floats over grates, but cannot pass doors, cat flaps, cracked walls, or
another host. **Pressure plates** open gates while something is standing on
them (a host or a crate; a ghost is too light). Being seen as a ghost, or
walking a host into the priest, costs a life.

## How it plays

The house moves on a tick (about three a second): you get one move, or
possess/release, per tick, and the priests take a step. Nothing is random, so
a plan always plays out the same way. Steps and par are shown under the room;
par is the solver's shortest solution, and beating it pays a bonus.

## Controls

Arrows or WASD move (hold to keep going), Space possess or leave your host,
P pause, R restart, Esc menu. All rebindable in Settings.

## Build

    make            # build/ghostmaze
    make test       # rules tests + solves every room (no raylib needed)
    make install    # ~/.local/bin + ~/.local/share/ghostmaze
    python3 tools/gen_sounds.py && python3 tools/gen_icon.py

Every room is proven solvable by `Solver_Solve`, a breadth-first search over the
whole game state, and its par is checked against the solver in the tests. New
rooms go in `src/levels.c`: add the map, run `make test`, and put the solver's
number in as the par.

## Rooms 16-23

Eight more rooms, each proved solvable (and its par set) by the solver: Grate Run (hide in alcoves), Sentry Flap, Two Keys,
Push and Hold (a crate holds the plate), Three Bells (three rhythms, lots of waiting), The Vault, Watched Hall and Grand Tour
(cat, armor and servant in one room). Checked that no room is solved without the hosts it is about.
