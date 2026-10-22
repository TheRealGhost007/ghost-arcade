# Chipforge

The Ghost Arcade music maker: a four-track chiptune tracker that writes the
songs the games play, and an importer that turns a recording (WAV, OGG or
MP3) into one of those songs. Built like the games: plain C17,
[raylib](https://www.raylib.com) 6 and [ghost-common](../ghost-common), with a
headless test suite.

What you hear in Chipforge is exactly what a game plays: both use the synth in
`ghost-common/musicsynth.c`. Give a song to a game (Ctrl+G) and that game
plays it instead of its built-in theme from its next launch.

## The screens

- **Menu**: New song, Open song, Import a recording, Game themes, Settings,
  How to play, Updates, Quit. The stat line says how many songs are in your
  library and how many games have one assigned.
- **Make a song**: what New song opens. A song is already playing, made from
  a recipe of plain choices (mood, speed, chords, how the bass, sparkle and
  beat move, the sound, a tune number, how long it is); change any of them
  and you hear it on the next step. Open it in the editor, give it to a game,
  or start blank. See [Make a song](#make-a-song).
- **Editor**: the tracker. Order list on the left, the 64-step pattern grid
  in the middle (Bass, Arp, Lead, Drums), a key-hint card on the right and
  the status line along the bottom. The step column names the chord each bar
  is on and the header the song's key. While playing, the view follows the
  playhead. Ctrl+F fills a track and Ctrl+K picks the chords, see [Filling a
  track](#filling-a-track) and [Chords](#chords); Ctrl+R writes a variation
  and F9 grows the loop into a song, see [Growing a loop into a
  song](#growing-a-loop-into-a-song); F4, F3 and Ctrl+Shift+K keep every
  note in key, see [In key, mood and fit to key](#in-key-mood-and-fit-to-key);
  F10 records you playing along with the loop, see [Playing
  along](#playing-along).
- **Open song**: your library, with the game each file is assigned to.
  Enter opens, Delete (then Y) deletes.
- **Import a recording**: a folder browser starting in `~/Music`. Pick a file,
  choose Tempo (auto, or 60..200), Drums on/off and Chords on/off, then Import.
  The report shows the tempo it heard and how sure it was, the length,
  patterns and notes found; Enter opens the result in the editor, unsaved.
  You can also drop an audio file onto the window, or start
  `chipforge <file>` (a `.gsong` opens in the editor, audio imports).
- **Game themes**: the eight built-in themes (the seven games and the
  launcher) as presets. Space auditions one; Enter opens a copy in the editor;
  R opens Make a song on that theme's own recipe, to make one like it.
- **Give it to a game**: the seven games, and Ghost Arcade (launcher) for the
  launcher's hall, with what each plays now. Enter assigns the current song
  (saving it first if needed), Delete clears a slot.
- **Settings**, **How to play**, **Updates**: the shared arcade screens.
  Settings also has Chipforge's own Timing row and its test, see [Playing
  along](#playing-along).

## Make a song

New song on the menu opens **Make a song** with a song already looping, made
from a random recipe. Each row changes one thing about it and you hear the
change on the next step; nothing is kept until you open or save it.

| Row | Choices |
| --- | --- |
| Mood | Happy (major), Adventurous (mixolydian), Dreamy (dorian), Moody (minor), Spooky (phrygian), Mysterious (harmonic minor). The key's root stays; a roll picks a new one |
| Speed | Slow 84, Steady 104, Quick 124, Fast 144, Frantic 168 bpm |
| Story | The chords, by name: Hero's anthem (I V vi IV), Last level, Sad and racing, Spooky stairs, Lazy groove, Royal road, and eight more named after the game themes that use them |
| Bass | Steady, Bouncy, Jumping, Driving, Walking, Long notes |
| Sparkle | The arpeggio: Up, Up and down, Skipping, Broken, Stabs, Held |
| Beat | Dance, Rock, Laid back, Breakbeat, Shuffle, Gallop. Laid back, breakbeat and shuffle bring a little swing |
| Sound | Classic (triangle bass, thin pulse arp, square lead), Soft (triangle bass and arp, thin lead), Buzzy (saw bass, very thin arp, square lead) |
| Melody | The tune's number: Left/Right for the next or previous tune, Enter for a random one |
| Length | Loop (the one pattern), Short, Medium or Long: the loop grown into a song of 4, 8 or 16 passes, the arranger's Short loop, Game loop and Long loop (see [Growing a loop into a song](#growing-a-loop-into-a-song)). Each choice shows how long that is at the song's tempo, "Medium 1:14" |
| Hear it as | Calm (bass and arp only, the layer a game always plays), Busy (hats, kick and most of the tune, no snare yet), Intense (everything). It changes how you listen, not the song |

Up/Down picks a row, Left/Right changes it (Enter moves it on too), R rolls
everything at once (Length and Hear it as stay as you set them). The line
under the title gives the key, the tempo and how long the song lasts. The
list scrolls to reach its last rows. Under the rows:

- **Roll everything**: a whole new recipe, the same as R.
- **Open in the editor**: the song keeps playing in the tracker, unsaved, for
  you to rework like any other.
- **Save and give to a game**: the assign screen; pick a game and name the song.
- **Blank song instead**: an empty song to write from scratch.

On Game themes, R opens Make a song on the highlighted theme's own recipe:
the rows start exactly at that theme (its key and tempo, so a 96 bpm theme
reads "Steady 96"), and you can turn one dial at a time away from a tune you
already know. Esc goes back to the screen you came from.

Every recipe stays in key and keeps a bass and an arpeggio in every bar, so
the song works in a game even when only those two layers are playing. A
grown recipe keeps that in every part, and starts on the loop itself, which
is what a game plays first and comes back to.

## Keys in the editor

Piano keys play on the current octave (shown in the header). With **in key**
on (the header shows a teal `in key` tag), the white-key positions play the
song's scale in order and the black-key positions are silent, so every note
you type is in key:

| Keys | Notes |
| --- | --- |
| `Z X C V B N M` | the scale's seven notes, from the key's root in the current octave |
| `Q W E R T Y U` | the same an octave up |
| `I O P` | continuing into the next octave |

In C major those are exactly the plain white keys; in A minor at octave 4,
`Z` is A4. In key starts on for a song that carries its key (Make a song, a
game theme, anything saved after Chords) and off for imports and older
files; F4 switches it.

**All notes (F4)**: with in key off, the rows are FastTracker's layout:

| Keys | Notes |
| --- | --- |
| `Z S X D C V G B H N J M` | C C# D D# E F F# G G# A A# B of the current octave |
| `Q 2 W 3 E R 5 T 6 Y 7 U` | the octave above |
| `I 9 O 0 P` | continuing into the next octave |

On the drums track the lower row doubles as pads either way: `Z` or `K`
toggles the kick, `X` or `S` the snare, `C` or `H` the hat.

| Key | What it does |
| --- | --- |
| Arrows | Move the cursor (up/down wrap within the pattern) |
| PageUp / PageDown | A bar (16 steps) |
| Home / End | Step 0 / step 63 |
| Tab / Shift+Tab | Next / previous track |
| `'` (apostrophe) | Enter a hold (`===`) on a tone track |
| Delete | Clear the cell |
| Backspace | Move up one and clear |
| `=` / `-` | Octave up / down |
| Ctrl+`]` / Ctrl+`[` | Edit step up / down (how far the cursor advances after a note) |
| Space | Play from the start of this pattern, following the order; stop |
| Shift+Space | Loop this pattern |
| Enter | Play from the cursor step |
| Escape | Stop if playing, else leave (asks if there are unsaved changes) |
| `]` / `[` | BPM +1 / -1 (with Shift, +10 / -10) |
| `.` / `,` | Swing up / down |
| F5 / F6 | Previous / next wave on the current tone track |
| F7 / F8 | Volume down / up on the current track |
| Ctrl+Up / Ctrl+Down | Transpose this track in this pattern a semitone |
| Ctrl+Alt+Up / Ctrl+Alt+Down | Transpose it an octave |
| Ctrl+N | New pattern (appended to the song and to the order after this position) |
| Ctrl+D | Duplicate this pattern, likewise |
| Ctrl+Delete | Delete this pattern (never the last one) |
| Ctrl+Backspace | Clear this track in this pattern |
| Ctrl+F | Fill this track with a style (Left/Right choose, R new take, Enter keep, Shift+Enter every pattern, Esc put it back) |
| Ctrl+K | Pick this pattern's chords by name (Left/Right choose, Up/Down key, F fit the tune, `/` type them, Enter keep, Shift+Enter every pattern, Esc put it back) |
| Ctrl+R | Vary this pattern: a new pattern after it in the order, an answer, a lift, a drop, a fill or a new tune (Left/Right kind, R new take, Enter keep, Esc put it back) |
| F9 | Grow this pattern into a song of about a minute (Left/Right shape, R new take, Enter keep, Esc put it back) |
| F4 | In key on / off: the piano keys play only the song's scale |
| F3 / Shift+F3 | Change the mood of the whole song a step brighter / darker |
| Ctrl+Shift+K | Fit to key: every out-of-key note moves to the nearest in-key note |
| F10 | Play along: record over this pattern as it loops, caught to the beat (see [Playing along](#playing-along)) |
| Ctrl+Left / Ctrl+Right | Move along the order |
| Ctrl+PageUp / Ctrl+PageDown | Edit the previous / next pattern (the order is not changed) |
| Ctrl+Shift+Up / Down | Change which pattern this order position plays |
| Insert | Repeat this pattern after this order position |
| Shift+Delete | Remove this order entry (never the last one) |
| Ctrl+Z | Undo (24 levels) |
| Ctrl+S | Save (asks for a name the first time) |
| F2 | Rename |
| Ctrl+E | Export a WAV to `~/Music/<file>.wav` (or the library folder if `~/Music` is missing) |
| Ctrl+G | Give it to a game |
| F1 | The how-to card |

Leaving with unsaved changes asks: Enter saves, D discards, Esc stays.

## Filling a track

Put the cursor on a track and press Ctrl+F. The pattern loops (if it was not
already) and the status strip becomes a picker, `Fill drums  < Breakbeat >`.
Left/Right steps through the styles and you hear each one on the next step;
R gives another take of the styles that have more than one (a new tune,
other ghost notes). Enter keeps the one you like as a single undo step,
Shift+Enter keeps it and fills that track in every pattern of the song, and
Esc puts the track back exactly as it was. While the picker is open the note
keys do nothing, Space stops and restarts the loop, and the key-hint card
lists the picker's keys.

| Track | Styles |
| --- | --- |
| Bass | Steady, Bouncy, Jumping, Driving, Walking, Long notes |
| Arp | Up, Up and down, Skipping, Broken, Stabs, Held |
| Drums | Dance, Rock, Laid back, Breakbeat, Shuffle, Gallop |
| Lead | New tune, Fit my rhythm |

The picker opens on the style the track already sounds like, so it names the
beat you have before you change it. Bass, arpeggio and tune follow the
chords the pattern is on: they are read from its bass (or, with no bass, its
arpeggio) before anything is rewritten, and shown beside the label. A
pattern with neither is filled on i VI iv V. With Shift+Enter each pattern
is filled on its own chords.

**Fit my rhythm** keeps every note, hold and rest of the lead where you put
them and only chooses the pitches. Type any notes in the rhythm you hear in
your head, press Ctrl+F, and you get a tune in that rhythm that fits the
chords: strong beats on chord tones, steps in between, the last note on its
bar's chord. That is why Ctrl+F on a lead with notes opens on Fit my rhythm;
on an empty lead it opens on New tune.

## Chords

Ctrl+K opens the same picker on the pattern's chords, `Chords  < Hero's
anthem >  A minor`. Left/Right steps through the named progressions (the six
Make a song offers, then the game themes' own) and the bass and arpeggio
move onto each one at once, keeping their rhythm: only the pitches change.
An empty bass gets Steady and an empty arpeggio Up, so Ctrl+K on an empty
pattern gives it a backing.

- **Up/Down** moves the key's root a semitone at a time (the scale stays)
  and the chords move with it. The key starts as the song's own, or as a
  guess from its notes shown with a `?`.
- **F** fits the tune to the new chords too: notes on the beat move to the
  nearest chord tone, the rest into the key, two semitones at most
  (three for an out-of-key note midway between two tones of a diminished
  chord, so fitting again changes nothing). F again
  leaves the tune as it was.
- **Enter** keeps it as a single undo step and writes the key into the song
  (the file's `key` line); **Shift+Enter** moves every pattern onto these
  chords, each from its own; **Esc** puts it back.
- **`/`** types the chords instead, one per bar: numbers (`1 6 4 5`), chord
  names in the key (`Am F Dm Em` in A minor; the root is what counts, the
  key decides major or minor) or Roman numerals (`i VI iv V`), separated by
  spaces, commas or dashes. Fewer than four repeat in order (`1 5` is
  1 5 1 5); more than four is refused. Enter tries them in the picker, where
  you can still keep them or put everything back; Esc returns to the picker.

Chords not in the table show as their numbers, `< 1 2 3 4 >`.

## Growing a loop into a song

An 8-second loop played for a whole level wears thin; the same idea heard as
verse, chorus, lift and breakdown does not. **F9** (Grow) builds that from
the pattern you are on. The picker opens on `Grow  < Game loop, about 1 min
>`, the new patterns appear, the order list fills, and playback starts from
the top so you hear it as a game would. Left/Right changes the shape and
the arrangement is rebuilt at once; the band shows how many patterns
it has and how long it really lasts at your tempo.

| Shape | Order (A is your pattern) |
| --- | --- |
| Short loop, about 30 s | A B A T |
| Game loop, about 1 min | A A B A A' B B' T |
| Long loop, about 2 min | A A B A A' B B' A A B B A' A' B B T |
| Song with an ending, for a WAV | I A A B A' B B' A O |

B is a chorus: your pattern moved onto answering chords with a new tune. A'
is a lift (a driving bass, a busier or higher arpeggio, doubled hats, the
tune higher), B' a drop (a breakdown: half-time drums, long bass notes, a
sparse tune), T a turnaround whose last bar leads back into A, I and O an
intro and an outro. Some parts end with a fill into the one after. The times
are at 120 bpm.

Games loop the order from the top, so the three loops have no intro: they
start on your pattern and end on a turnaround that leads back into it. Only
Song with an ending, meant for a WAV export, opens with an intro and closes
on an outro.

R gives another take (another chorus tune, other fills). Enter keeps it as
a single undo step, `Grew into 7 patterns, about 1:04`; Esc removes every
added pattern and puts the order back exactly. Shapes the song has no room
for (64 patterns at most) are skipped. Grow follows the chords your bass (or
arpeggio) is on, so a pattern with neither is refused: Ctrl+K gives it a
backing first. Any other patterns the song has are kept but left out of
the new order, and Enter says how many.

**Ctrl+R** (Vary) is the one-pattern version: it puts a variation of your
pattern right after it in the order, as Ctrl+D places a copy, and opens
`Vary 00 into 03  < Answer >`. Playback starts on the last bar of your
pattern, so you hear the change happen.

| Kind | What the new pattern does |
| --- | --- |
| Fill at the end | Your pattern with a drum fill and a run in its last bar, into the pattern that follows |
| Answer | Bars 3 and 4 of the tune echo bars 1 and 2 and end on the root: a question and its answer |
| New tune | The same backing with a new lead |
| Lift | A chorus lift: driving bass, busier or higher arpeggio, doubled hats, the tune higher |
| Drop | A breakdown: half-time drums, long bass notes, a sparse tune |

R gives another take where a kind has one. Enter keeps the pattern as a
single undo step and leaves the cursor on it, ready to edit (one Ctrl+Z
takes it back and returns to your pattern); Esc removes it and its order
entry exactly.

## In key, mood and fit to key

The header shows the song's key beside its name: `key A minor` when the song
says its key (the file's `key` line), `key A minor?` when it is a guess from
the notes. The teal `in key` tag beside it means in-key typing is on.

- **F4** switches in-key typing (see the piano tables above). Turning it on
  for a song without a key guesses one from its notes and stores it; a song
  with no notes yet says so and stays on all notes.
- **F3** changes the mood of the whole song a step brighter, **Shift+F3** a
  step darker. Every note keeps its place in the scale while the scale
  changes, along spooky (phrygian), moody (minor), mysterious (harmonic
  minor), dreamy (dorian), adventurous (mixolydian) and happy (major), round
  again past either end. The key line follows, and the status line names the
  new mood, `Mood happy (C major)`. Stray notes are fitted to the key first.
- **Ctrl+Shift+K** fits the song to its key: every out-of-key note (typed
  with all notes, or left by the importer) moves to the nearest in-key note,
  in every pattern, rhythm untouched, and the status line says how many
  moved. It is the one-key version of the tidying an import needs.

F3 and Ctrl+Shift+K are each a single undo step. A song without a key is
worked in its guessed key, and a change stores that key with it.

## Playing along

**F10** records you playing over the pattern you are on. The pattern starts
looping from its last bar, which is the count-in: four clicks while the strip
along the bottom counts `4 3 2 1`, then the loop comes round and you play.
The header shows `REC 1` (the pass you are on) where it says PLAYING, and
`catch 1/8` under it.

Play the piano keys (in key or all notes, as F4 has them) or, on the drums
track, the pads. Every press sounds at once; hold a key for a long note and
let go to end it. Each time the loop comes round, the pass you just played is
written into the pattern, caught to the beat, as a single undo step, and the
status line says so: `Pass 3: 7 notes`. The notes of the pass you are in
show dim in the cells they will land in. Passes add up: the next one plays
over the last, so you can build a part an idea at a time, and a note you play
takes the step it lands on.

| Key, while recording | What it does |
| --- | --- |
| F10 or Esc | Stop recording, keeping the pass you are in. The loop plays on so you hear it (Esc again stops it) |
| Space | Stop recording and playback |
| Backspace | Throw away the pass you are playing |
| Tab / Shift+Tab | Record on the next / previous track without stopping: lay down the bass, then the lead |
| F12 | The catch grid: 1/16, 1/8 (where it starts) or 1/4 |
| Ctrl+M | The click on or off (the song's own beat keeps time without it) |
| `=` / `-`, F4 | Octave up / down, in key on / off |

A note is caught to the nearest point of the catch grid, so 1/8 forgives a
lot, and one played a moment before the loop comes round lands on the first
step of the next pass, where it was meant. A note sounds for as long as you
held it, to the nearest step, and stops at the end of the pattern. A tone
track plays one note at a time, so of a chord the last key pressed wins; on
the drums a pad only ever adds a hit, it never takes one away. Tempo and
swing stay put while you record (their keys say so), and Ctrl+Z waits until
you stop, when each pass undoes on its own.

**Timing.** Sound leaves the speakers a little after Chipforge plays it:
20 to 60 ms on most speakers, 150 ms or more over Bluetooth, and that is long
enough to catch a note on the wrong step. Settings has a **Timing** row
(40 ms to start with; Left/Right changes it by 5 ms) and Enter on it runs a
test: 16 clicks at 100 bpm, tap Space with each one (the first four are for
finding the beat), and it tells you `Your timing is 52 ms. Enter keeps it`.
Run it again when you change speakers or headphones. It is kept in
`$XDG_CONFIG_HOME/chipforge/config.ini` (usually
`~/.config/chipforge/config.ini`), Chipforge's own file, since the
arcade-wide `prefs.ini` is shared by every game.

## The .gsong format

A song is a small text file. Patterns are 64 steps (four bars of sixteenths);
each step has a note for bass, arp and lead (`---` silence, `===` hold the
previous note) and the drums as three letters (kick, snare, hat, `.` for off).
The `order` line says which patterns play, in what sequence.

```
GSONG 1
name My Tune
bpm 128
swing 0.00
wave tri pulse25 pulse50
volume 100 100 100 100
key A minor
order 0 0 1
pattern 0
00 C-2 C-4 --- K.H
01 === E-4 --- ..H
```

Notes are `C-4`, `C#4` (flats like `Db4` are read too); octave 4 holds middle
C. Waves are `tri`, `pulse50`, `pulse25`, `pulse12` and `saw`; volumes are
0..100 per track. The `key` line is optional: it names the song's key (`key A
minor`, `key C# harmonic minor`; roots with sharps or flats, scales `minor`,
`major`, `dorian`, `phrygian`, `harmonic minor` and `mixolydian`) so the
composer's tools can work in it instead of guessing. The game themes carry
theirs; a file without one loads exactly as before. Rows you leave out are
silent; unknown lines are skipped and out-of-range values are clamped, so a
hand-edited file still loads. The full contract is `ghost-common/song.h`.

The library is `$XDG_DATA_HOME/ghost-arcade/music/` (usually
`~/.local/share/ghost-arcade/music/`), one `.gsong` per song.

## How games pick up songs

`$XDG_CONFIG_HOME/ghost-arcade/music.ini` (usually
`~/.config/ghost-arcade/music.ini`) holds one line per game:

```
# Which Chipforge song each Ghost Arcade game plays: slug=file.gsong
skyraid=my-tune.gsong
blockfall=lobby.gsong
```

When a game starts it looks for its slug there; if the file exists in the
library it plays that, otherwise its built-in theme. The launcher's hall music
is the slug `ghostarcade` (Ghost Arcade (launcher) on the assign screen); the
launcher picks a new song up when you come back to it. Chipforge writes this
file for you (Ctrl+G), but it is plain text if you would rather edit it.
Games bring the layers in with the action: bass and arp are always there, the
lead comes in above 0.35 intensity and the drums above 0.15 (hats) and 0.5
(snare), so a song whose bass and arp can stand on their own works best.

## Importing a recording

The importer is a cover version by a very literal-minded chip, not a
transcription a musician would sign. It listens for the beat and follows it
through the whole track, then for each sixteenth asks what the bass is doing,
what the melody is doing, what chord the bar is on and where the kicks, snares
and hats fall. Expect the melody in the right place and the right key, and
expect to tidy it: a wrong octave here, a ghost note there, a chord it heard
as two. Force the tempo if the auto guess is off (it is often a factor of two
out on half-time or double-time material), and turn Chords off for sparse
material where the arpeggio only adds mud. Everything it makes is an ordinary
song: the editor takes it apart like any other.

## Building

Requires `gcc`, `make`, `pkg-config`, raylib 6.0+ and the `../ghost-common`
folder next to this one.

```sh
make          # builds build/chipforge
make run
make test     # headless suite: song format, synth, editor, composer, recipes, in-key tools, the picker (Fill, Chords, Vary, Grow), the arranger, playing along (takes, step timing against the synth, the clock, the Timing test), browser, transcriber
make debug    # ASan/UBSan build
make install  # to ~/.local, with a .desktop launcher and the icon
```

Or, from the top of the monorepo, `./install.sh` builds and installs the
games, Chipforge and the launcher together.

Ghost Launcher catalog line:

```
Chipforge|~/.local/bin/chipforge|~/.local/share/chipforge/assets/icons/chipforge.png|Make the music your games play|~/Work/ghost-arcade/chipforge
```

## How it's put together

```
src/
  editor.c/h      the tracker model: cursor, edits, undo. No raylib.
  transcribe.c/h  recording -> Song. No raylib; the caller decodes the audio.
  browser.c/h     folder listing for the import picker. No raylib.
  compose.c/h     the composer: bass, arp, beat and tune writers, chords, key tools. No raylib.
  recipe.c/h      Make a song: the recipe rows mapped onto the composer. No raylib.
  keymode.c/h     in-key notes, fit to key and mood change over a whole song. No raylib.
  pick.c/h        the picker behind Fill, Chords, Vary and Grow: try choices live, keep as one undo or put back. No raylib.
  arrange.c/h     the arranger: variations of a pattern and a loop grown into a song (Vary, Grow, the recipe's Length). No raylib.
  take.c/h        playing along: the notes of a take, caught onto the grid and written as a pass. No raylib.
  jam.c/h         playing along: the synth's step times, the clock that follows the audio, the Timing test, the session. No raylib.
  appini.c/h      Chipforge's own config file (the Timing). No raylib.
  player.c/h      the synth streamed through raylib, plus the click and the clock the recording follows
  render.c/h      the tracker screen
  main.c          window, the app state machine, the other screens (Make a song included)
../ghost-common/
  song.c/h        the Song model, .gsong format, library and music.ini
  musicsynth.c/h  the synth and the eight built-in themes
  ui, winscale, prefs, sfx, howto
tests/            test_main.c includes test_song.c, test_editor.c,
                  test_browser.c, test_transcribe.c, test_compose.c,
                  test_recipe.c, test_keymode.c, test_pick.c,
                  test_arrange.c, test_take.c and test_jam.c;
                  runs against a temporary XDG_DATA_HOME so your
                  library is never touched
tools/gen_icon.py the icon, generated
```
