# Ghost Arcade

Six original retro arcade games and the launcher that ties them together, by
**The Real Ghost**. Plain C17 and [raylib](https://www.raylib.com), built for Linux
(developed on Omarchy: Arch, Hyprland, Wayland). Every game is an original take on a
classic idea: original code, art, sound and music, and no borrowed names or assets.

| Game | In one line |
| --- | --- |
| **Blockfall** | Falling blocks with five modes, custom board sizes and power-ups |
| **Brickburst** | Paddle, ball, bombs and twelve walls of bricks |
| **Skyraid** | Hold the line against the marching formation |
| **Rockdrift** | Vector-line space rocks with real inertia |
| **Crawlshot** | Twelve segments come down. Shoot them before they reach you |
| **Moondrop** | Land it gently. Fuel is the only life you get |

Coilrush, Ghostmaze, Lanehop, Gemdive and Girderclimb left the arcade in October 2026. They are
still in the git history, and updating removes their programs but keeps their saves and scores.

**Pond Kings** is a game of its own beside the arcade's six: hockey, three on three, from
the frozen pond to the town rink and the big arena. Kids in toques, snowbanks that swallow
the puck, goalies and slapshots on the rinks, a Journey where you sign the players you
beat, a daily pond, a shootout, up to four players on one screen, and online games with a
friend by room code. See [`pondkings/`](pondkings/).

**Ghost Launcher** is the arcade hall: attract-mode cabinets, per-game stats and
trophies, a daily challenge, optional online leaderboards, and a notice when a newer
version lands here on GitHub.

**Chipforge** is the music maker: a chiptune tracker that writes the songs the games
play, with an importer that turns a recording into one. See [`chipforge/`](chipforge/).

## Install

    git clone https://github.com/TheRealGhost007/ghost-arcade.git
    cd ghost-arcade
    ./install.sh

That builds everything, runs the tests, and installs the six games, Pond Kings, Chipforge
and Ghost Launcher for your user only (`~/.local`; no root, nothing outside your home folder). Then run
`ghost-launcher`, or find **Ghost Launcher** in your app menu.

- **Arch, Omarchy, Manjaro, EndeavourOS:** if anything is missing the installer offers to
  run `sudo pacman -S --needed base-devel raylib curl git` for you.
- **Debian, Ubuntu, Fedora and others:** they do not package raylib 6 yet. The installer
  prints the exact packages to install and the four commands that build raylib once.
- **Update:** `./install.sh --update` (the launcher tells you when there is one).
- **Remove:** `./install.sh --uninstall`. Your saves, scores and settings are kept.

Prefer to read a script before running it? It is short: [`install.sh`](install.sh).
Releases are listed on the [releases page](https://github.com/TheRealGhost007/ghost-arcade/releases);
cloning as above always gives you the newest one.

### Building by hand

You need `gcc`, `make`, `pkg-config`, **raylib 6** and `libcurl` (for the launcher's
score sync and Pond Kings' online games).

    make            # build everything
    make test       # every headless test suite (needs no display)
    make install    # into ~/.local/bin and ~/.local/share

Each folder also builds on its own (`cd skyraid && make run`).

## Shared across every game

- Keyboard and gamepad, rebindable keys, a **How to play** card on first launch
- **F11** fullscreen with clean scaling, **F2** live tuning panel for playtesting
- Procedural chiptune music and sound effects; screen shake and reduced-flashing options
- Local score tables per mode, achievements, run logs, a shared daily-challenge seed
- Crash-safe saves: every file is written to a temp file and renamed into place

All of that lives in `ghost-common/`, which each game compiles in directly.

## Make your own music

Every game ships with a procedural chiptune, but you can replace it. **Chipforge**
(`chipforge`, or *Chipforge* in the launcher) is a small four-track tracker in the
house style: bass, arp, lead and drums, 64 steps a pattern, played by the same synth the
games use, so what you hear in the editor is what the game plays. Write a tune on the
piano keys or import a recording (WAV, OGG or MP3: it listens for the beat and
makes a very literal cover you can then tidy), press **Ctrl+G** to give it to a game,
and that game plays it from its next launch. Songs are plain text files
(`~/.local/share/ghost-arcade/music/*.gsong`); the assignments live in
`~/.config/ghost-arcade/music.ini`.

## Layout

    ghost-common/     shared code: UI, audio, music synth, input, saves, scores, tuning
    ghost-launcher/   the launcher, the ghost-sync score uploader, the database schema
    chipforge/        the music maker: tracker, importer, the .gsong library
    <game>/           src/ (a raylib-free rules core + rendering), tests/, assets/, tools/

Each game keeps its rules in a core with no raylib in it, so the tests drive real game
logic headlessly: bots land Moondrop, and everything runs clean under AddressSanitizer
and UBSan.

## Online leaderboards and privacy

Sharing scores is **opt-in**: nothing leaves your computer until you answer yes in the
launcher (press **O** to change it later). What is sent: your arcade name, game, mode,
score, date and a random per-install ID. The games themselves never send scores; only
`ghost-sync` does, and it treats everything it downloads as hostile. Pond Kings' online
games and rival crews are the one exception to "the games stay offline": they start only
when you pick them, and say first what they share. The full
threat model and the server rules are in
[`ghost-launcher/online/SECURITY.md`](ghost-launcher/online/SECURITY.md).

To run your own leaderboard, paste `ghost-launcher/online/schema.sql` into a Supabase
project and put its URL and **publishable** key in `~/.config/ghost-launcher/online.conf`.

## Credits

The pixel font is [Press Start 2P](https://fonts.google.com/specimen/Press+Start+2P) by
CodeMan38, under the SIL Open Font License (`OFL.txt` sits beside each copy). Everything
else here is original work. Sounds and icons are generated by the scripts in each
`tools/` folder.

## Licence

Ghost Arcade is free software: you can redistribute it and/or modify it under the terms
of the **GNU General Public License, version 3** as published by the Free Software
Foundation. It is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. The full text is in [`LICENSE`](LICENSE).

In short: play it, study it, change it and share it, but anything you distribute that is
built on this code must be shared under the same licence, with its source.

Copyright (C) 2026 The Real Ghost.

The bundled Press Start 2P font is not covered by the GPL: it remains under the SIL Open
Font License 1.1 (see the `OFL.txt` beside each copy). raylib (zlib licence) and libcurl
(curl licence) are separate libraries that the games link against.
