# Chipforge design brief

Chipforge is Ghost Arcade's music maker: a chiptune tracker that writes songs
the games can play, and an importer that turns a recording (WAV/OGG/MP3)
into one of those songs. It lives in the `ghost-arcade` monorepo beside the
eleven games and the launcher, and it is built like them: C17, raylib 6,
`ghost-common` compiled straight from `../ghost-common`, `make`, `make test`
(headless, no raylib), `make install` into `~/.local`.

## What already exists (do not rewrite)

- `ghost-common/song.h/.c` — the `Song` data model and `.gsong` text format,
  the player's library (`$XDG_DATA_HOME/ghost-arcade/music/`), and the
  per-game assignment file (`$XDG_CONFIG_HOME/ghost-arcade/music.ini`,
  `slug=file.gsong`). Read the header: it is the contract.
- `ghost-common/musicsynth.h/.c` — the synth. `Synth_Init(&s, &song, rate)`,
  `Synth_Render`, `Synth_SetPosition`, `Synth_SetLoopPattern`, `Synth_Preview`,
  `Synth_CurrentPattern`, `Synth_RenderWavFile`, and `Music_ThemeToSong` which
  turns any of the twelve built-in game themes (`Music_ThemeAt(i)`,
  `Music_ThemeCount()`) into a one-pattern song: Chipforge's presets.
- `ghost-common/music.c` — what the GAMES use. Already prefers an assigned
  Chipforge song over the built-in theme. Chipforge does not use it; it has
  its own `player.c`.
- `ghost-common/ui.h` — the house style (fonts, palette, text helpers, menu /
  settings / updates / how-to screens). `ghost-common/winscale.h` — logical
  size + fullscreen; use `Win_BeginFrame`/`Win_EndFrame` instead of
  `BeginDrawing`/`EndDrawing`. `ghost-common/prefs.h` — arcade-wide prefs
  (music volume, fullscreen). `ghost-common/sfx.h` — opens the audio device
  (`Sfx_Init`) and has built-in jingles (`Sfx_Jingle(JINGLE_CONFIRM)` etc.).
- `chipforge/src/*.h` — the interfaces for this app: `editor.h`, `player.h`,
  `browser.h`, `transcribe.h`. Implement them exactly; do not change a
  signature without a good reason, and if you must, say so in your report.
- `chipforge/Makefile` — already written. `CORE_SOURCES` (the headless test
  build) = song.c musicsynth.c safefile.c + editor.c transcribe.c browser.c,
  and since 1.1 compose.c recipe.c keymode.c pick.c arrange.c take.c jam.c
  appini.c, so those app files MUST NOT include raylib. `render.c`,
  `player.c`, `main.c` may.

## Files to write

| File | raylib? | Owner |
|------|---------|-------|
| `src/transcribe.c` | no | transcriber agent |
| `src/editor.c` | no | editor agent |
| `src/browser.c`, `src/player.c` | browser no, player yes | player/browser agent |
| `src/render.c/.h`, `src/main.c`, `src/version.h` | yes | app agent |
| `tests/test_main.c` | no | each agent appends its own `test_*` functions; see below |
| `tools/gen_icon.py`, `packaging/chipforge.desktop`, `README.md`, howto entry, launcher catalog row, top-level lists | | assets agent |

Tests: `tests/test_main.c` is ONE file. To avoid four agents editing it at
once, each agent writes its tests in its own file `tests/test_<area>.c`
(`test_song.c`, `test_editor.c`, `test_browser.c`, `test_transcribe.c`) with
`#include`-able functions `void test_<area>_all(void)` using a shared
`CHECK` macro from `tests/check.h` (the assets agent writes `check.h` and
`test_main.c`, which `#include`s the four files and calls the four `_all`
functions, then prints `N checks, M failures` and exits non-zero on failure,
exactly like the games' suites). Test files are compiled by inclusion, so they
must not define `main` and must use `static` helpers with unique names
(prefix them with the area name).

## The app (main.c + render.c)

Window 960x640 logical (landscape, like the launcher), title "Chipforge",
light colour teal `(Color){40, 220, 190, 255}`, `Ui_DefaultTheme(light)`.
Slug `chipforge`. Version 1.1.0.

Screens (an `AppState` enum in main.c):

- **MENU** — `Ui_DrawMenu` with items: `New song`, `Open song`,
  `Import a recording`, `Game themes`, `Settings`, `How to play`, `Updates`,
  `Quit`. Tagline "Make the music your games play". Stat line: how many songs
  are in the library and how many games have one assigned.
- **EDITOR** — the tracker (render.c draws it, see below). Keys:
  - arrows move; PageUp/PageDown a bar; Home/End; Tab / Shift+Tab next/prev
    track.
  - Piano keys like FastTracker: `Z S X D C V G B H N J M` = C..B of the
    current octave, `Q 2 W 3 E R 5 T 6 Y 7 U` = the octave above
    (`I 9 O 0 P` continue into the next octave). That is the layout with in
    key off; with in key on (`F4`, below) the white-key positions play the
    song's scale and the black ones are silent. Entering a note also previews
    it (`Player_Preview`). On the drums track `Z`/`K` toggles kick, `X`/`S`
    snare, `C`/`H` hat (so the lower piano row doubles as drum pads).
  - `=`/`-` octave up/down (shown in the header). `'` (apostrophe) enters a
    hold (`===`) on a tone track. Delete clears, Backspace clears-back.
  - Space play/stop from the start of the current pattern following the order
    (`Player_Play(ed.orderPos, 0, -1)`); Shift+Space loops the current pattern
    (`loopPattern = ed.pattern`); Enter plays from the cursor step; Escape
    stops if playing, else asks to leave (see leaving).
  - `[`/`]` BPM -1/+1, with Shift -10/+10. `;`/`:`... keep it simple: `,`/`.`
    swing down/up.
  - `F5`/`F6` wave prev/next on the current tone track; `F7`/`F8` volume
    down/up on the current track.
  - `Ctrl+Up`/`Ctrl+Down` transpose the current tone track in this pattern by
    a semitone, with Alt an octave.
  - `Ctrl+N` new pattern, `Ctrl+D` duplicate pattern, `Ctrl+Delete` delete
    pattern, `Ctrl+Backspace` clear this track in this pattern.
  - `Ctrl+Left`/`Ctrl+Right` move along the order; `Ctrl+Shift+Up/Down`
    change which pattern this order position plays; `Insert` repeats the
    current pattern after this position; `Ctrl+Insert`... no: `Shift+Delete`
    removes this order entry.
  - `Ctrl+F` Fill and `Ctrl+K` Chords open the picker (pick.c): a modal band
    in place of the status line, every choice previewed live, Enter keeps it
    as one undo, Esc puts it back. See README "Filling a track" and "Chords".
  - `Ctrl+R` Vary and `F9` Grow open the same picker (arrange.c builds the
    music): Vary adds one new pattern and its order entry right after the
    cursor's position (as `Ctrl+D`), Grow rewrites the order into a whole
    arrangement of the cursor's pattern. Playback follows the order while
    they are open; Esc removes everything they added, byte for byte. See
    README "Growing a loop into a song".
  - `F4` in-key typing on/off, `F3` / `Shift+F3` the whole song's mood a
    step brighter / darker, `Ctrl+Shift+K` fit every note to the key
    (keymode.c; F3 and Ctrl+Shift+K are one undo each). See README "In key,
    mood and fit to key".
  - `F10` play along (jam.c, take.c): loops the cursor's pattern from its
    last bar (step 48: the count-in, four clicks) and records the piano, or
    the pads on the drums track, over it. Each time the loop comes round the
    pass just played is written in, caught to the grid, as one undo entry.
    Recording is modal like the picker, with the same band in place of the
    status line: `F12` the catch grid (1/16, 1/8 to start, 1/4), `Ctrl+M`
    the click on/off (on to start, every beat), `Backspace` drops the pass
    being played, `Tab` / `Shift+Tab` record on the next/prev track, `=`/`-`
    and `F4` as in the editor; `F10` or `Esc` stop recording, keeping the
    pass and the loop playing, `Space` stops both. Tempo, swing and `Ctrl+Z`
    refuse with a reason until it stops; every other key is quiet. With no
    sound after 2 s it gives up and says why. See README "Playing along".
  - `Ctrl+Z` undo. `Ctrl+S` save (asks for a name the first time: NAME
    prompt). `F2` rename. `Ctrl+E` export WAV to `~/Music/<file>.wav` (falls
    back to the library folder if `~/Music` is missing); status line reports
    the path. `Ctrl+G` opens the ASSIGN screen. `F1` the how-to card.
  - The editor's `status` line shows the result of the last action for ~3 s.
  - Edit step shown in the header; `Ctrl+[`/`Ctrl+]` change it.
  Leaving with unsaved changes: a small confirm overlay "Unsaved changes.
  Enter saves, D discards, Esc stays". Follow-cursor: while playing, the view
  centres on the playhead and the cursor is left alone.
- **RECIPE** (Make a song) — what `New song` opens: a song made from a
  recipe loops while `Ui_DrawSettingsTitled` rows change it live (recipe.c,
  `RecipeField` order: Mood, Speed, Story, Bass, Sparkle, Beat, Sound,
  Melody, Length, Hear it as), then Roll everything, Open in the editor,
  Save and give to a game, Blank song instead. Length grows the loop with
  the arranger (Loop, Short, Medium, Long); Hear it as only sets
  `Player_SetIntensity`, which main.c's per-frame line holds at 1.0 on
  every other screen. See README "Make a song".
- **BROWSE_SONGS** — a list (from `Song_LibraryList`) to open; Enter opens
  into the EDITOR; Delete asks to delete (Y confirms); Escape back. Shows the
  assigned game beside a file if any.
- **BROWSE_AUDIO** — `Browser` listing starting at `Browser_StartDir`
  (folders first); Enter opens a folder or picks a file; Backspace goes up;
  Escape back. Also: a file dropped onto the window (`IsFileDropped` /
  `LoadDroppedFiles`) in ANY state starts an import; `chipforge <file>` on the
  command line too (`.gsong` opens in the editor, audio imports).
- **IMPORT** — options before running: `Tempo < auto >` (left/right steps
  through auto, 60..200 in 2s), `Drums on/off`, `Chords on/off`, `Import`.
  Running: draw one frame "Listening to <name>..." then decode with
  `LoadWave` + `LoadWaveSamples` (mix channels to mono, free the raylib
  buffers), call `Transcribe_Run`, then show the report (bpm + confidence,
  length, patterns, notes found, "truncated" if so) and `Enter` opens the
  result in the EDITOR with the title from `Browser_TitleFromPath`, unsaved.
  Failure (LoadWave invalid, Transcribe false) shows a plain message.
- **PRESETS** — the twelve themes by name (`Music_ThemeAt(i)->slug`,
  capitalised; "ghostarcade" shown as "Ghost Arcade"); Enter opens
  a copy in the editor named after the game, unsaved. Up/Down previews
  nothing (keep it simple); Space plays the highlighted theme through the
  player so you can audition it, Space again stops.
- **ASSIGN** — the twelve slugs (the 11 games + `ghostarcade`) with the
  currently assigned file for each (from `Song_AssignedName`); Enter assigns
  the current song's file (saving it first if it has never been saved: goes
  through the NAME prompt, then assigns), Delete clears that slug, Escape
  back. Teal highlight on rows playing THIS song.
- **NAME** prompt — text entry (printable ASCII, `GetCharPressed`, backspace;
  max SONG_NAME_LEN-1), Enter confirms (song name set; file name =
  `Song_FileNameFor(name)`; saves via `Song_LibraryPath` + `Song_Save`), Esc
  cancels. Used by save/rename/assign.
- **SETTINGS** — `Ui_DrawSettingsPlus` with Chipforge's Timing row, then the
  shared rows, then Done: pass the array `{Timing, Done}` and it slots the
  shared rows in before the last row given, so row 0 is Timing, rows
  1..`PREFS_ROWS` are the shared ones (`Prefs_HandleRow(row - 1, ...)`
  exactly as the games do; see any game's main.c for the pattern) and the
  last is Done. Music volume pref drives `Player_SetVolume`. Timing
  (`Timing (Enter tests it)`, `< 40 ms >`) is how much earlier a played note
  is caught when playing along: Left/Right 5 ms within 0..300, saved at once
  to `$XDG_CONFIG_HOME/chipforge/config.ini` (appini.c: Chipforge's own
  file, since `prefs.ini` is shared by every game); Enter opens CALIBRATE.
- **CALIBRATE** — the Timing test, reached only from that row.
  `Player_ClickTest` plays 16 clicks at 100 bpm through the player, the path
  the music takes; Space taps along, and the first 4 clicks are warm-up for
  finding the beat. Past the last click `Jam_LatencyFromTaps` turns the taps
  into ms (it needs 6 counted clicks tapped once each, else "Not enough taps
  to tell"); Enter keeps the result as the Timing, Space tries again, Esc
  goes back to Settings. With no sound after 2 s it says so instead of
  waiting.
- **HOWTO**, **UPDATES** — the shared cards (`Ui_DrawHowTo(HowTo_ForSlug
  ("chipforge"))`, `Ui_DrawUpdates`). First launch shows the how-to.

Startup order, as in every game's main.c: `SetConfigFlags(FLAG_VSYNC_HINT |
FLAG_WINDOW_HIGHDPI)`, `InitWindow`, `SetExitKey(KEY_NULL)`,
`SetTargetFPS(60)`, resolve assets dir (copy `ResolveAssetsDir` from
skyraid/src/main.c, slug chipforge), `Ui_Init`, `Win_Init`, `Sfx_Init(assets,
NULL, 0)` (no sound files; jingles still work), `Player_Init(&ed.song)`,
`Player_SetVolume(Prefs_Get()->musicVolume)`. Each frame: `Win_Update()`,
`Pad_Update` is NOT needed (no gamepad module). Shutdown in reverse.

### The tracker screen (render.c)

Draw with `Ui_Text` at UI_T8 and UI_T16 only (no other sizes), sentence-case
copy, the theme palette from `Ui_Theme()` (`bg`, `board`, `panel`, `rule`,
`text`, `dim`, `faint`, `light`, `gold`, `danger`). One bold element per
screen: the playhead row in `light`.

Layout at 960x640:
- Header strip (y 0..56): song name (T16, `text`), then at T8 in `dim`:
  `BPM 120   swing 0.00   octave 4   step 1   pattern 02/05   pos 03/12`,
  a `PLAYING` / `LOOP` tag in `light` when the player runs, `*` after the
  name when dirty. On the name line, left of the `PLAYING` slot, the song's
  key at T8 in `dim` (`key A minor`, `key A minor?` when guessed) and an
  `in key` tag in `light` while in-key typing is on. While recording (F10),
  `REC 3` (the pass) in `danger` takes the `PLAYING` slot, `catch 1/8` sits
  under it at the end of the readout line, the band counts in `4 3 2 1` at
  T16, and the notes of the pass being played show in `dim` in their cells.
- Left column (x 16..136): the ORDER list: `00 P02` rows at T8, the current
  position in `panel` with a 4px `light` bar, the playing one marked with `>`.
  Scrolls to keep the current position visible.
- Centre (x 152..808): the pattern grid. A row per step at 16px: step number
  `00`..`63` (T8, `dim`; bar starts `00 16 32 48` in `text`), then four
  columns Bass / Arp / Lead / Drums, each 3 characters at T16 (24px wide
  glyphs: 3*16+2 spacing ≈ 50px, give each column 120px) with the column
  headings above showing name, wave and volume (`BASS tri 100`). Beat rows
  (every 4th) get a slightly lighter background (`board` vs `bg`), bar rows a
  `rule` line. The cursor cell is `panel` with `light` text; the playhead row
  is a `light` bar at 25% alpha behind the row. Notes in `text`, holds `===`
  in `dim`, empty `---`/`...` in `faint`, drums `K S H` letters in `gold`.
  Show 24 rows; keep the cursor (or playhead while playing) within the
  middle, scrolling the window. Nothing scrolls past 0 or 63.
- Right column (x 824..944): a key-hint card at T8 in `dim` (two or three
  words a line): the dozen most useful keys, and the status line in `light`
  near the bottom (fades after 3 s using `statusAge`).
- Bottom strip: a 16-block volume readout per track is NOT needed; keep
  the bottom for the status line + "F1 help".

Other screens are simple centred lists: title at T16 in `light`, rows at T8
or T16, selection = `panel` row + 4px `light` bar on the left (never a size
change), hints at T8 in `dim` at the bottom. Use `Ui_BevelRect` sparingly.

## Headless tests (what the suite must prove)

- song: Init defaults; Write→Parse round trip equals the original for a song
  using every feature (holds, all waves, volumes, 3 patterns, order of 5);
  parse of garbage/empty/truncated text returns false without crashing; out of
  range bpm/volume/notes are clamped; order entries pointing at missing
  patterns are dropped; `Song_NoteName`/`Song_NoteFromName` agree for every
  MIDI 0..127 and accept flats; Add/Duplicate/Remove pattern and the order
  helpers respect their limits and fix the order up; `Song_FileNameFor`
  sanitises; the library/assign functions work against a temp
  `XDG_DATA_HOME`/`XDG_CONFIG_HOME` (setenv to a mkdtemp folder): save, list,
  assign, read back, clear.
- synth: `Music_ThemeToSong` is deterministic and fills every track for every
  theme; rendering 2 s of each theme has no sample at ±32000 for more than 1%
  of frames and is not silent; a HOLD keeps the lead envelope above 0.5 a
  step later while a NONE lets it decay below; a two-pattern order advances
  (`Synth_CurrentPattern` changes after 64 steps); `Synth_SetLoopPattern`
  does not; `Synth_RenderWavFile` writes a RIFF header with the right sizes to
  a temp path; a song with 0 patterns/order renders silence without crashing.
- editor: cursor wrapping, note entry advances by editStep, drums toggle,
  hold, clear, undo restores exactly (memcmp), pattern new/dup/delete keep
  order consistent, order insert/remove limits, transpose clamps to
  NOTE_MIN..NOTE_MAX, status messages set.
- browser: a temp folder with files `a.mp3 B.WAV c.txt sub/` lists `sub`
  first then `a.mp3 B.WAV` (case-insensitive extension match), hidden files
  skipped, Enter on `sub` descends, Up returns and reselects `sub`,
  `Browser_TitleFromPath`, `Browser_IsAudioName`.
- transcribe: synthetic signals — a 120 bpm sequence of square-wave quarter
  notes C4 E4 G4 C5 over 8 bars at 22050 Hz with a 60 Hz sine-burst kick on
  every beat: `Transcribe_EstimateBpm` within ±2 of 120; `Transcribe_Run`
  bpm within ±2; the lead track has a new note (not hold/none) at step 0 of
  each beat with the right pitch for at least 80% of beats; kicks on at least
  80% of beat steps and on fewer than 15% of off-beat steps; a forced bpm
  (`opt.bpm = 100`) yields song.bpm 100; silence yields a song with no notes
  and returns true; 100 samples returns false and a valid empty song;
  `Transcribe_Salience` peaks at the right MIDI note for a synthetic harmonic
  spectrum. Also a 48000 Hz input works (decimation).

## Style rules (from the rest of the arcade)

- Comments explain WHY; sentence-case UI copy ("Nothing to undo", not
  "NOTHING TO UNDO"); no em-dashes in UI strings.
- No `printf` debugging left in; `-Wall -Wextra` clean.
- Everything that writes a file goes through `safefile.h` or the song
  helpers.
- Never `rm -rf` the data dir in Makefile uninstall (already right).
