# Ghost Launcher rework plan

Status: PLANNING. Nothing here is built yet. Written 2026-10-13 from a map of the current code and three design proposals, merged by a planner. Decisions marked open are the owner's to make.

## Decided by the owner (2026-10-13)

- **Settings layout:** tabs across the top (not the recommended side list). The pad's LB/RB switch tabs.
- **Esc / pad B on the hall:** opens the Arcade menu, with "Leave the arcade" inside. Leaving asks only if a game or install is still running; a click on the X quits at once.
- **Extras after the core:** all of them: hall music, game pages, favourites and start on last played, the Daily challenge desk and attract mode, and Chipforge as a jukebox.
- **Checking the look:** an invisible screen (weston, headless) for screenshots of every screen, plus the owner playing each phase for feel.
- **Defaults taken from the plan's recommendations unless the owner objects:** a click on the lit machine plays it; pad map A play, Y daily, X this game, View stats, Menu/B the Arcade menu, LB/RB jump 5; hints drawn in house colours; hall music on by default, quietly, silent when the launcher is not the front window; game pages open with I / pad X / a click on the card, Enter still plays at once; Chipforge stays in the row (drawn as a jukebox); new names limited to 32 characters; the old Gemdelve playtime is folded into Gemdive once; the button kit lives inside the launcher first, games untouched; Sharp scaling tried in the launcher only.

## Summary

The rework leaves the arcade hall looking as it does now and puts real buttons on top of it. Anything you can do will work with a mouse click, with the Xbox pad, or with the letter keys you already use. The centre of it is a new Settings screen where every option is a button, an On/Off switch or a volume bar. An Arcade menu also gives the pad and keyboard a way to every screen and a way to quit. The work comes in seven steps (Phase 0 to 6), and after each one you have a working launcher. Phases 0-3 are the core you asked for, Phases 4-5 are recommended extras, and Phase 6 is a pick-list. None of it changes the 11 games or Chipforge unless you say so.

## The launcher today

- One home screen, the arcade hall (a row of animated cabinets), plus 7 other screens: Stats, Trophies, Updates, "Share scores online?", Your name / Name for this game, and Add / Edit a game (admin only).
- There is no Settings screen. Fullscreen is F11 only. Reduced flashing, music volume and screen shake can only be changed inside a game. Turning update checks off means editing a file.
- The mouse can only press the small X. The Yes/No boxes on the online screen and the trophy arrows look like buttons but cannot be clicked.
- On the Xbox pad you can move, play and go back. Stats, Trophies, Name, Online, Updates, the Daily and quitting are all out of reach.
- Hidden keys: V (updates) is shown only on the update pill. O and Shift+U are each missing from one of the two hint lines. F11 is never shown.
- No way to quit from the keyboard or pad. The green EXIT sign on the wall is only decoration.
- Bugs:
  - Alt+Enter also starts the selected game.
  - Delete removes a machine at once, with no undo.
  - A failed save says nothing.
  - F11 in the launcher can undo a setting you just changed in a game.
  - Two card messages run into the right-hand column.
- Smaller gaps:
  - It always starts on the first machine, and holding an arrow does not repeat.
  - No music, although a Ghost Arcade theme and your own "Ghost Arcade (launcher)" song already exist.
  - Chipforge looks like a broken game.
  - Only one "World best" number; the full score boards are only inside each game.

## Core: the rework you asked for

### Groundwork: one input reader and a safer main loop (M)

You will not see anything new after this step, but several bugs go away.
- Alt+Enter only switches fullscreen. Today it also starts the selected game, saves your name or moves the form on.
- Holding an arrow key, the d-pad or Backspace repeats: after 0.4 s, about 10 steps a second.
- The launcher reads the keyboard, pad and mouse once per frame and remembers which one you used last, so hints can later show the right buttons.
- The launcher gets its own small pad reader for LB, RB, View and Menu. The shared pad code stays as it is.
- Every pass of the main loop draws exactly one frame. Without this, a click or Esc can 'fall through' onto the next screen.
- Arcade settings changed inside a game are read again when that game closes and when the launcher gets focus back. So F11 in the launcher no longer undoes them.
- A small settings file used only by the launcher (~/.config/ghost-launcher/launcher.ini), and a safe writer for any line of online.conf.
- New tests that run without a window: key repeat, Alt+Enter, which device was used last, and a check that every screen can be reached and left with pad buttons alone.

**Why:** Fixes pain F (Alt+Enter fires twice), M (no key repeat) and P (stale shared settings). Every button and screen that follows stands on this. Today's 233 tests cover none of the input code.

**Needs:** Nothing. This comes first.

### Button kit (built inside the launcher) (L)

The set of controls every new screen is built from. Each control has five looks:
- normal: a dark bevelled block;
- hover: lighter, with a hand pointer when the mouse is over it;
- focused: chosen with keys or pad, shown as the panel colour plus a 4 px bar, never a size change (house rule);
- pressed: lights pink and the label sinks 2 px, like an arcade button;
- disabled: faint, and the hint line says why.
A click acts when you let go on the same control. Drag off first and nothing happens.
The pieces:
- button;
- On/Off switch (click either half);
- ten-block volume bar (click a block, drag, scroll the wheel or press Left/Right);
- '< choice >' picker;
- text field;
- section list;
- scrolling list;
- 'are you sure?' box, where the safe answer is picked first;
- 'Saved' note;
- a hint bar of clickable key chips that show keyboard keys or Xbox buttons, matching what you used last.
It uses the launcher's own 8/16/24 px text and colours. The games do not change.

**Why:** This is the 'settings as buttons' you asked for. It also fixes pain B (only the X reacts to the mouse; the online Yes/No boxes and trophy arrows look clickable but are not) and part of E (every hint names a keyboard key).

**Needs:** Groundwork

**Open decision:** Build it inside the launcher first, with the games untouched (recommended)? Or put it in the shared ghost-common code now, so the games can use it sooner?

### One rule for Back, Leave and 'are you sure?' (S)

- Every screen except the hall gets a [< BACK] button at the top left. Esc, pad B, that button and the mouse's back side-button all go back one level. Inside a text field, Esc first cancels the edit.
- On the hall, Esc, pad B or the pad's Menu button opens the Arcade menu, which holds 'Leave the arcade'. The EXIT sign on the wall becomes a real exit.
- Leaving asks first only if a game or an install is still running.
- Delete asks 'Remove BLOCKFALL from the arcade? Its scores and playtime stay on disk.' [Keep] [Remove].
- Esc on a half-filled Add/Edit form asks 'Throw away your changes?'.
- The X lights up on hover and acts when you let go.
- The key that opened a screen also closes it. Today only V does.
- Every save reports back: 'Saved', or a red 'Couldn't save' instead of failing silently.
- The first-run 'share scores?' question still has to be answered. Esc there still means 'No, keep it local', as the screen says.

**Why:** Fixes pain A (no way out without the mouse), G (Back and cancel behave differently on each screen; Delete is instant) and H (saves give no feedback). It is also the safety net the clickable screens need.

**Needs:** Groundwork, Button kit

**Open decision:** Should leaving always ask, or only when a game or install is still running (recommended)? Should a click on the X still quit at once (recommended: yes)?

### Arcade menu: one place that reaches everything (S)

Esc, pad B, the pad's Menu button or a new [= MENU] chip in the header opens a panel over the empty left wall. The hall dims and keeps moving behind it.
- Items: Back to the hall, Settings, Stats, Trophies, Updates (with an 'N new' badge), Leave the arcade.
- Until Settings exists (Phase 2), it also lists Your name and Online sharing.
- Each item shows its old letter key, and the letters still work without the menu.
- Use the d-pad and A, a click, or the letter. Esc, B, Menu or a click outside the panel closes it.

**Why:** Fixes pain E (from the pad, Stats, Trophies, Name, Online and Updates are out of reach) and pain C (V is shown only on the update pill; O and Shift+U each drop out of one hint line). It is how the pad gets to Settings.

**Needs:** Button kit, Back rule

### Today's screens rebuilt with buttons (nothing disappears) (M)

Every screen and key that exists today stays.
- Hall: the row, attract demos, room tint, marquee flicker and coin drop are unchanged.
- Keys: Enter/Space play, Enter installs a machine that is not installed, D daily, L stats, H trophies, U name, Shift+U name for this game, O online, V updates, R check now, W/S and the arrows, F11/Alt+Enter fullscreen, and admin A/E/Delete all keep working.
- Stats: stays, and now scrolls (wheel or pad), so more than about 13 machines fit.
- Trophies: stays for the whole arcade. The '< Game >' arrows become clickable and the list scrolls past 6.
- Updates: stays. [Check now] shows 'Checking...'. The pill says 'NEW VERSION' instead of counting commits as updates. 'No answer yet' is no longer shown after a check that did answer.
- Online question: the Yes/No boxes become real buttons. On later visits, O opens Settings > Online.
- Name screens: become the name field in Settings > Player. U and Shift+U jump straight there.
- Add/Edit form: stays for admin, now with the better text field.
- Background jobs keep running exactly as now: install, score sync, update check and playtime.

**Why:** You asked that nothing be lost. This step also fixes pain B, H and M on the existing screens.

**Needs:** Button kit, Back rule

### Proper text entry (M)

- A text cursor you can move with Left/Right/Home/End.
- Backspace and Delete repeat when held.
- Ctrl+V pastes. Characters other than plain letters, digits and symbols, and the '|' sign, are dropped.
- A counter such as '12 / 32'.
- Enter saves and shows 'Saved'. Esc puts the old text back.
- Letter shortcuts never fire while you type, so typing 'l' does not open Stats.
- In the admin game form, '|' is refused (it breaks games.txt), and path fields say 'found', 'not found' or 'not executable' as you type.
- Pad A on a name field no longer saves at once. Until the pad name board exists, it says 'Type with the keyboard'.

**Why:** Fixes pain J (no cursor, paste or repeat; '|' corrupts the game list; paths never checked) and part of E (pad A on the name screen saves immediately).

**Needs:** Button kit

**Open decision:** Limit new names to 32 characters, which is what the online boards keep (recommended), or keep today's 63? Longer names you already have are kept but shown cut short.

### Settings screen with buttons (L)

Open it from the Arcade menu, with Tab, or by clicking the 'Playing as' chip, which opens the Player section. U still jumps to Player and O to Online.
The sections run down the left. Each row is a real control. Changes save at once, with a 'Saved' note, the same as the games' Settings. Rows marked * are shared with every game.
- PLAYER: your arcade name with [Edit]. 'Names for single games' lists every machine as 'uses your arcade name' or 'as TRG', with [Set] and [Clear]. This replaces the hidden Shift+U, which still works.
- SOUND:
  - Music volume* as ten clickable blocks (used by every game now, and by the hall once hall music exists);
  - Hall sounds, the coin and move clicks, launcher only, where 0 = mute;
  - [Play coin sound].
- SCREEN:
  - Fullscreen* (F11 still works);
  - Reduced flashing* (the launcher already obeys it but could never change it);
  - Screen shake* (used by the games);
  - Button hints < Auto | Keyboard | Xbox | PlayStation >.
- ONLINE:
  - Share my scores, with the plain 'what is sent' text;
  - Check for updates, which replaces 'edit online.conf';
  - [Check now], which shows 'Checking...';
  - [Sync scores now] (turning sharing on also syncs straight away);
  - [Update details];
  - the server address, shown but not editable on purpose.
- CONTROLS: reference cards for keyboard, Xbox pad and mouse. Each game's own keys stay inside that game.
Machines and Data sections are added in later phases.
Launcher-only values live in launcher.ini, never in the shared prefs.ini. The shared file is read again right before every save.

**Why:** Your request. It also fixes pain D (check_updates, reduced flashing, music volume and screen shake are changeable only in a file or inside a game; fullscreen is F11 only), H (turning sharing on doesn't sync; R gives no feedback) and O (fixed-volume sounds, no mute). IMPROVEMENT_PLAN.md promised a Settings screen that was never built.

**Needs:** Button kit, Arcade menu, Text entry

**Open decision:** Sections down the left, console style (recommended, room for 7), or tabs across the top?

### A hall you can click and drive from the pad (M)

- Mouse:
  - point at a side machine: it glows and shows a hand pointer;
  - click it: it slides to the middle;
  - click the lit machine: coin drop and play;
  - the wheel moves one machine per notch.
  Clicks are locked during the coin drop, as keys are today.
- Header chips: 'Playing as TRG' (red 'Set your name' when empty), the update pill, [= MENU] and X, all clickable.
- The grey hint line becomes a control strip of real buttons:
  - PLAY, which reads INSTALL or Installing... when that fits;
  - DAILY, faint with the reason on machines that have no daily or are not installed (today D silently does nothing);
  - TROPHIES, which becomes THIS GAME when the machine page exists;
  - STATS;
  - MENU;
  - a clickable '< 4 / 12 >'.
- Pad on the hall: d-pad or stick move with repeat, A play or install, Y daily, X trophies / this game, View stats, Menu or B the Arcade menu, LB/RB jump 5 machines. In menus: d-pad moves, A presses, B goes back, LB/RB switch section.
- Hints switch between keyboard keys and pad buttons to match what you touched last. The mouse pointer hides after key or pad use and comes back when the mouse moves.
- Card fixes:
  - the 'install failed' and 'no source' lines fit their column;
  - the install-log path is the real one;
  - the card moves up 8 px to make room for the strip.
- New keys: I for this game, Tab for Settings. All of today's letter keys keep working.

**Why:** Fixes pain B (the mouse does nothing on the hall), E (the pad is stuck on the row), C (hidden keys), A (the EXIT sign is fake), H (D silently fails) and I (text runs into the next column).

**Needs:** Groundwork, Button kit, Arcade menu

**Open decision:** Should a click on the lit machine play it (recommended), or only select it, so that PLAY, Enter or A is always needed to start?

## Recommended

### Pad name board (S)

An arcade high-score style letter grid under the name field: A-Z, 0-9, space . - _ !, plus DEL and DONE.
- D-pad moves, A types, X deletes, Y adds a space, Menu finishes, B cancels.
- The keyboard still types straight into the field, and you can click letters with the mouse.

**Why:** You play on an Xbox pad, and today a name cannot be typed on a pad at all (pain E).

**Needs:** Text entry, Hall pad support

### Your row: remember, favourite, order, hide (M)

- The launcher opens on the machine you played last. Settings > Player gets a 'Start on: Last played | First machine' row.
- F, or a FAVOURITE button, puts a gold star on a machine's marquee. Favourites come first.
- Row order: Your order, Recently played, Most played or A-Z (T, or a row in Settings).
- Settings > Machines gets Up/Down to reorder and Hide to take a machine off the row without touching its scores.
- All of this is kept in the launcher's own file, under each game's program name rather than its title, so renaming a game does not lose it. games.txt is not rewritten.
- The row is sorted only when the launcher opens or when you change the order, so machines never shuffle while you browse.
- Wrapping from the last machine to the first jumps instead of sliding across the whole row.

**Why:** Fixes pain M (always starts on machine 1; long wrap slide) and L (no reorder, favourite or hide). Your playtime shows Blockfall 38 min, Brickburst 23 min and Chipforge 19 min, against Girderclimb 1.5 min, so the games you play most should sit close by.

**Needs:** Settings screen, Hall clicks

**Open decision:** Default order: your own catalog order (recommended) or Recently played? And may every player reorder and hide machines on their own copy (recommended), with Add, Edit and Remove staying admin-only?

### Hall music (M)

- The hall plays the built-in 'ghostarcade' chiptune softly (just the bass, arpeggio and hats) at the shared Music volume. If you assign a Chipforge song to the hall, such as the 'Ghost Arcade (launcher)' song you saved on 12 Oct, that plays instead.
- It fades out during the coin drop, so the coin chime stays clean.
- It is silent while a game runs and whenever the launcher window is not in front, and fades back in afterwards.
- Settings > Sound gains 'Hall music < Theme | My Chipforge song | Off >'.
- Later, if wanted: the machine page plays that game's own theme, so you hear a game before you play it.

**Why:** Fixes pain O. The launcher plays no music, and the planned hall audio was never built. A test build in the scratchpad confirmed the theme plays and your saved song loads, with no change to the shared music code.

**Needs:** Groundwork (knows when a game is running), Settings > Sound

**Open decision:** Music on by default, quietly (recommended), or off until you turn it on? Separately: change Chipforge so its song-assign list includes the hall?

### Quiet hall while you play (S)

While a game the launcher started is running and the launcher is in the background, the hall drops to 10 frames a second with its animation paused. It goes back to 60 the moment you return to it.

**Why:** Today the launcher redraws the whole hall 60 times a second behind your game, for nothing.

**Needs:** Groundwork

### Chipforge shown as a tool, not a broken game (S)

- Chipforge is recognised by its program name.
- Its PLAY button reads OPEN CHIPFORGE.
- Its card says what it is, for example 'Songs in your library: 7' and 'Hall song: built-in theme', instead of 'No runs recorded yet' and 'Nothing online yet'.
- It is left out of the Stats run counts and trophy totals, and gets no Daily button.

**Why:** Fixes pain N. You use Chipforge a lot (18 min 40 s of playtime), but the hall presents it as a game with no demo, no scores and 0 runs.

**Needs:** Hall clicks

**Open decision:** Keep Chipforge in the row like any other machine (recommended), or give it a fixed spot in the hall opened with its own key?

### Leaderboard download fix (S)

ghost-sync, the launcher's background helper, downloads at most 512 score rows from the server. The server keeps every past daily board forever, so over time the games late in the alphabet lose their world boards. The fix asks only for the regular boards and the last 14 days of dailies.

**Why:** Needed before the machine page shows full world boards, or those boards will slowly go empty.

**Needs:** Nothing (needed by the Machine page)

### Machine page (a page for each game) (L)

I, pad X, or a click on the card opens a page for the selected machine. Enter on the row still plays at once.
- Top: the name, the description and a strip with BEST, TIME, RUNS, LAST PLAYED and TROPHIES n/m.
- Left: the cabinet's attract demo drawn large. Under it, the buttons PLAY (or INSTALL / OPEN CHIPFORGE), DAILY CHALLENGE (faint with the reason when it is not available), NAME FOR THIS GAME, FAVOURITE and BACK. Admin also gets EDIT and REMOVE.
- Right, three tabs:
  - ABOUT: goal, controls and a tip, from the games' own how-to text;
  - SCORES: this machine's top 10 next to the world top 10 for each board, 'You hold #1 in the world', and today's daily;
  - TROPHIES: this game's 6 trophies, with dates.
- PLAY goes back to the row and drops the coin there, so starting a game feels the same.
- [<] and [>] flip to the next machine.
- H still opens the whole-arcade Trophies screen.

**Why:** Fixes pain Q: today there is one 'World best' number, and the full boards are only inside each game. It also gives Daily, Trophies and the per-game name a home the pad can reach (pain C and E). All the data already exists on disk.

**Needs:** Button kit, Hall clicks, Leaderboard download fix

**Open decision:** Open it with I / pad X / a click on the card, so Enter keeps meaning 'play now' (recommended)? Or make Enter open the page, which costs one extra press every time you play?

### Machines manager (the game list as buttons) (M)

Settings > Machines lists every machine with its status: Installed, Not installed or Installing.
- Admin gets [Add a machine], [Edit] and [Remove]. Remove asks first.
- The form shows all five fields at once. Click any field or use Tab; then [Save] or [Cancel]. Paths are checked, and '|' is refused.
- Renaming a machine warns that playtime and the per-game name are stored under the old name, and offers [Carry them over].
- Pointing a machine at a differently named program warns that its scores and trophies follow the program's file name.
- 'New machines in this version: X [Add them]' appears when Ghost Arcade ships a game your list lacks. Today new bundled games never appear on existing installs. Machines you dismiss do not come back.
- The old A, E and Delete keys keep working.

**Why:** Fixes pain D (only a file edit adds or removes games for anyone but you), G (instant Delete), J ('|' breaks the list), K (renaming loses data; your playtime file still holds an orphaned 'Gemdelve' entry) and L (new games never show up).

**Needs:** Settings screen, Text entry, Back rule

**Open decision:** When this ships, add the old 'Gemdelve' playtime (5 min 34 s) to Gemdive once (recommended), or leave it?

## Optional extras, pick any

### Daily Challenge desk (M)

A screen reached from the Arcade menu, the DAILY button on a machine page, or a chalkboard on the hall wall reading for example 'DAILY 2 of 7 today'.
- It shows today's date, your current and best streak, and 14 days of dots.
- Each daily game gets a row: played today, your best today, the world's best today and your rank.
- One game is starred 'Featured today', picked from the date so everyone sees the same one.
- Enter returns to the hall and starts that game's daily.

**Why:** A reason to come back each day, and the 'Daily Challenge tile' promised in IMPROVEMENT_PLAN.md. The data already exists in the run logs and the daily boards.

**Needs:** Arcade menu, Leaderboard download fix

**Open decision:** Keep D as 'start the daily now' (recommended), with the desk reached from the menu and the chalkboard? And do you want streaks and the featured pick, or just the list?

### Attract mode when idle (S)

After 60 s with no input on the hall:
- the card slides away and a large 'PRESS ANY BUTTON' appears (steady if Reduced flashing is on);
- every 6 s the row glides to the next installed machine and plays its demo.
Any key, button or mouse move wakes it and goes back to your machine. That press does nothing else, so it can never start a game by accident.

**Why:** This is how real arcade cabinets behave, and the 11 demos already exist. Today only the selected cabinet ever shows one.

**Needs:** Groundwork

**Open decision:** Start after 60 s, 2 min, or off by default?

### Chipforge as a Jukebox (M)

Chipforge's machine is drawn as a jukebox, with an arched top, glowing tubes and a screen that scrolls the notes of the song the hall is playing. Its machine page gets a SONGS tab: pick a song from your library as the hall music, or see which game plays which song.

**Why:** Gives Chipforge its own look instead of a fake cabinet, and makes it the natural place to choose the hall music.

**Needs:** Hall music, Machine page, Chipforge shown as a tool

### Player card and a first-run welcome (M)

- U opens your card: your name with [Rename], totals (time in the arcade, time playing, runs, favourite game, daily streak, online boards you top), and a trophy wall of all 66 trophies.
- An optional 'ghost colour' from 8 swatches tints the header ghost.
- First run: a welcome, then 'What should the arcade call you?' (can be skipped), then the share-scores question, then a card showing the controls.

**Why:** Something to look at between games, and a friendlier first screen than a privacy question.

**Needs:** Settings screen, Text entry

**Open decision:** Do you want the ghost colour (launcher only) at all?

### Updates: what's new and a copy button (S)

- The Updates screen lists up to 5 changes you don't have yet, by title.
- A [COPY UPDATE COMMAND] button puts 'cd <source> && ./install.sh --update' on the clipboard.

**Why:** Readable update news, which matters more once you share the arcade with others. Your own build is ahead of GitHub, so it rarely shows for you.

**Needs:** Today's screens rebuilt

**Open decision:** Do it now, or wait until other people use your arcade?

### Sharp fullscreen scaling (S)

A 'Scaling: Auto / Sharp / Smooth' row in Settings > Screen. Auto is today's behaviour. On your 1920x1080 screen, fullscreen today blurs the pixel text (a 1.6875x stretch). Sharp keeps the strokes solid. Before/after pictures: /tmp/claude-1000/-home-trg-Work/f3dd8f93-a9e7-4eec-88bb-fabcdce81bf2/scratchpad/launcher-plan/zoom_compare.png

**Why:** The launcher's identity is pixel text, and fullscreen on your own monitor makes it soft.

**Needs:** Settings screen

**Open decision:** This is the only feature that touches the shared screen code (winscale.c). It is off unless chosen. Try it in the launcher only (recommended), offer it to every game, or skip it?

### Motion and readability options (M)

- Short slides and fades when panels and screens open. Input still acts instantly.
- A 'Reduced motion' switch: cuts instead of slides, still spotlights and dust, no marquee stutter.
- 'Text size: Large', which lifts 8 px text to 16 px.
- 'Cabinet demos: On/Off'.

**Why:** Smoother transitions were planned in IMPROVEMENT_PLAN.md. 8 px text is hard to read from the couch.

**Needs:** Settings screen

**Open decision:** Is Large text worth it, given the hall card then shows only one line of description?

### Button sounds (S)

- Moving the highlight plays a soft click, slightly varied in pitch.
- Pressing plays a short confirm sound; Back plays a back sound.
- Each volume block plays at its own pitch, so you hear the level.
- A disabled button gives a low blip. Mouse hover stays silent.

**Why:** Feedback that makes the buttons feel like real arcade buttons.

**Needs:** Button kit, Settings > Sound

### Data section in Settings (S)

- Shows where each file lives.
- [Open data folder] and [Open settings folder].
- [Reset playtime...], [Forget per-game names...] and [Reset arcade settings...]. Each asks first and keeps a .bak copy.
- Scores and trophies are never deleted from here.

**Why:** Makes the hidden files findable without a terminal (pain D).

**Needs:** Settings screen, Back rule

### Clickable settings for the games (later, game by game) (L)

Move the button kit into the shared ghost-common code as new files. Each game can then switch its Settings screen to mouse-clickable rows and tabs, one game at a time. A game changes only when it switches.

**Why:** The games' Settings are keyboard-only lists today. This would give them the same buttons as the launcher.

**Needs:** Button kit proven in the launcher

**Open decision:** Later: which games, if any?

## Rejected, and why

### One-click updater (L)

A button that runs ./install.sh --update for you.

Rejected:
- Your source folder is your live working copy, with 27 changed files right now, so the update's 'git pull' would refuse or tangle work in progress.
- A full build takes minutes and needs the compilers.
- A launcher that replaces itself while running needs a restart flow.
The copy-command button above is the safe version.

### Tile grid view (M)

A grid of game tiles instead of the cabinet row.

Rejected: it would replace the hall look you like, and 12 machines don't need it.

### Search and filter (S)

Typing to find a game.

Rejected: with 12 machines it isn't needed, and typing on a pad is painful. Favourites and sorting cover it.

### Coin economy and unlockables (L)

Coins earned by playing and spent on unlocks.

Rejected for now: the unlocks would have to be built into all 11 games. Worth another look after the game rebuilds.

### Picture avatars (M)

A player picture.

Rejected: there is no art pipeline; everything is drawn from shapes and pixel text. The ghost colour is the cheap version.

### Ambient hall sounds (M)

Cabinet hum and distant machines.

Rejected for now: these need new sound loops and would compete with the hall music. Revisit after hall music.

### Editing the server address in the app (S)

Changing the leaderboard url, key or update source in Settings.

Rejected on purpose: it is part of the leaderboard security model. These stay file-only, and Settings shows them as read-only.

### Resizable window or a 16:9 relayout (L)

Letting the window be resized, or redrawing the hall at 960x540.

Rejected:
- Hyprland would probably tile a resizable launcher instead of letting it float.
- 960x540 means re-laying out the whole hall you like.
Sharp scaling fixes the blur for much less.

### News board in the hall (S)

A wall board made from GitHub commit titles.

Rejected: commit titles are written for developers, so it would be noise in the room.

## Phases

Each phase ends with a working launcher.

### Phase 0 - Groundwork

Groundwork: one input reader for keyboard, pad and mouse; one frame drawn per loop; the Alt+Enter fix; hold-to-repeat; shared settings re-read on focus and after a game; launcher.ini and the online.conf line writer; new tests that run without a window. Before starting: commit or set aside the work currently in progress in the repo.

**You get:** A launcher that looks exactly the same. Alt+Enter no longer starts a game, holding an arrow scrolls the row, and F11 no longer undoes a setting you changed in a game.

**Games affected:** No. Launcher files and its tests only.

### Phase 1 - Buttons and a way out

Button kit; the one rule for Back, Leave and 'are you sure?'; the Arcade menu; today's screens rebuilt with buttons (online Yes/No, trophy arrows, Stats that scrolls, Updates with 'Checking...', 'NEW VERSION' pill).

**You get:** The mouse works on every screen. Esc, pad B or Menu opens the Arcade menu, so the pad reaches every screen and the keyboard can quit. Delete and leaving ask first. Saves say 'Saved' or 'Couldn't save'.

**Games affected:** No.

### Phase 2 - Settings

Proper text entry; the Settings screen with Player, Sound, Screen, Online and Controls sections.

**You get:** The Settings screen you asked for, every option a button, switch or volume bar: fullscreen, reduced flashing, music volume, hall sounds, update checks, Check now, Sync now, per-game names.

**Games affected:** No game code changes. The four shared settings (music volume, screen shake, reduced flashing, fullscreen) are the same ones the games' Settings already write. A change made in the launcher shows up in every game, which is the point.

### Phase 3 - Hall controls and the pad

A hall you can click and drive from the pad (clickable cabinets, wheel, header chips, control strip, full pad map, hints that switch between keys and pad buttons, card fixes); the pad name board.

**You get:** Everything on the hall works by mouse and by Xbox pad, including typing your name. End of the core rework.

**Games affected:** No.

### Phase 4 - Your hall (recommended)

Your row (start on last played, favourites, order, hide); hall music; quiet hall while you play; Chipforge shown as a tool.

**You get:** The launcher remembers where you were, plays your hall tune, goes quiet and cheap while you play, and Chipforge stops looking broken.

**Games affected:** No. The music code is linked into the launcher unchanged. Only if you say yes: one small Chipforge change so it can assign a song to the hall.

### Phase 5 - Machine pages (recommended)

Leaderboard download fix; the machine page (About, Scores with world boards, Trophies); the Machines manager (admin form, rename carry-over, new bundled machines).

**You get:** A page per game with full local and world score boards, and the game list managed with buttons instead of a text file.

**Games affected:** No. Only ghost-sync, the launcher's background helper, asks the server for fewer old daily boards.

### Phase 6 - Extras, pick any (optional)

Any of: Daily Challenge desk, attract mode, Chipforge Jukebox, player card and welcome, Updates 'what's new', Sharp scaling, motion and readability options, button sounds, Data section, clickable game settings. Each is its own small release.

**You get:** Whatever you pick, one at a time.

**Games affected:** Only Sharp scaling (one shared screen file, off unless chosen) and clickable game settings (game by game) touch shared code. Everything else stays in the launcher.

## Decisions for the owner

- 1. Settings layout and how you open it. Options: (a) sections listed down the left, console style; (b) tabs across the top. Either way it opens from the Arcade menu, with Tab, or by clicking 'Playing as'. Recommendation: (a). There is room for all seven sections, and the pad uses LB/RB to switch section either way.
- 2. What Esc and pad B do on the hall, and whether leaving asks first. Options: (a) open the Arcade menu, with 'Leave the arcade' inside, so quitting is two presses; (b) ask 'Leave the arcade?' straight away; (c) nothing, as today. Then: ask before leaving always, only when a game or install is still running, or never. Recommendation: (a). Ask only when something is still running, and keep a click on the X quitting at once.
- 3. Clicking the lit machine on the hall. Options: (a) it plays straight away, like Enter; (b) it only selects, and PLAY, Enter or A is always needed to start. Recommendation: (a). A side machine needs one click to come to the middle and a second click to play, and the coin drop gives you a second to see it coming.
- 4. Pad buttons and how their hints look. Proposed map on the hall: A play, Y daily, X this game (Trophies until the machine page exists), View stats, Menu or B the Arcade menu, LB/RB jump 5 machines. Hint colours: (a) house colours, a grey disc with the letter; (b) real Xbox colours (green A, red B, blue X, yellow Y). Recommendation: the map as proposed, with (a). It keeps the arcade's one-light-colour rule. Tell me if any button feels wrong in your hand.
- 5. Hall music (Phase 4). Options: (a) on by default, quietly, at the shared music volume; (b) off until you turn it on. Then: (c) silent whenever the launcher is not the front window; (d) silent only while a game the launcher started is running. Recommendation: (a) with (c). Rule (c) means the music can never play under a game you started from the app menu. Your own 'Ghost Arcade (launcher)' song can replace the built-in tune.
- 6. Machine page and Chipforge (Phase 5). Machine page options: (a) open it with I, pad X or a click on the card, so Enter still plays at once; (b) Enter opens the page and you press PLAY there, one more press every time. Chipforge options: (c) stays in the row, shown honestly as a tool; (d) gets a fixed spot in the hall with its own key. Recommendation: (a) and (c). The jukebox look stays an optional extra.
- 7. Keeping the games untouched. Recommendation: nothing in Phases 0-5 changes any of the 11 games or Chipforge (only the four shared settings they already share). Two later choices are yours: (a) Sharp fullscreen scaling, the only feature that touches shared screen code, off unless chosen (recommended: try it in the launcher only); (b) giving the games the same clickable Settings, game by game (recommended: wait until the launcher's buttons have proven themselves).
- 8. How we check the look after each phase. Tests cannot see pixels, and I must not open windows on your desktop while you use it. Options: (a) install a small 'invisible screen' program (weston, an Arch package) so I can screenshot every screen without touching your desktop; (b) you run 'ghost-launcher --shots' after each phase, which opens a window for a few seconds and saves pictures; (c) you just play each phase and tell me what feels off. Recommendation: (a) plus (c). Pad feel, repeat speed and sound balance can only be judged by you.

## Risks

- The core rewrites the launcher's main loop (main.c, 516 lines). The coin drop, background install, first-run question and admin form could break along the way. Guard: tests for every screen and action. Old screens stay until their replacement works. You play through each phase before the next starts.
- Tests cannot see pixels. No invisible test screen is installed, and agents must not open windows on your desktop, so every phase needs your eyes (Decision 8).
- Mouse and keys can fight over the highlight. Rule: moving the mouse hands the highlight to what is under it, and the first key or pad press takes it back.
- Pad A counts as both Enter and Space in the shared pad code. The launcher's own pad reader must read the real buttons, or A fires twice. Typing x, c, p or r on the keyboard must never trigger pad-only actions.
- The shared prefs.ini is written by every game, and the last write wins. The launcher re-reads it before each save. Launcher-only settings go in launcher.ini, because any game's save rewrites prefs.ini with only its four lines.
- A sorted row adds a second numbering, 'place in the row' against 'place in games.txt'. This is the most likely source of bugs: Edit, Delete or Shift+U acting on the wrong machine. Each of these actions gets a test.
- Hall music could play over a game the launcher didn't start. Going silent whenever the launcher isn't the front window prevents that.
- World score boards will slowly empty because of the 512-row download limit, so that fix ships before the machine page shows them.
- Paste on Wayland can come back empty. It must fail quietly.
- Vertical room on the hall is tight: the control strip needs the card moved up 8 px.
- Scope: the core alone is four phases, and the recommended work adds two more. You can stop after any phase and still have a working launcher.
- Start from a clean point. The repo has 27 uncommitted changes today. Commit or set them aside before Phase 0, so each phase can be undone on its own. The README controls table, every on-screen hint and two stale doc passages (README.md:131-132, ROADMAP.md:103-108) need rewriting as part of the work.

## Mockups

```
MOCKUP 1 - THE HALL WITH ITS NEW BUTTONS (960x640; one character = 10x20 logical px)
+------------------------------------------------------------------------------------------------+
|                                                                                                |
|  (o) GHOST LAUNCHER                  [Playing as TRG]  [NEW VERSION]  [= MENU]  [X]            |
|                                                                                                |
|  [EXIT]                                     4 / 12                                             |
|                                                                                                |
|    G H O S T                         +==================+                                      |
|                                      |    BRICKBURST    |                                      |
|    A R C A D E                       |==================|                                      |
|                                      |                  |                                      |
|    INSERT COIN      +-------------+  |                  |   +-------------+                    |
|                     |  BLOCKFALL  |  |                  |   |   SKYRAID   |                    |
| +-------------+     |-------------|  |                  |   |-------------|     +------------+ |
| |   LANEHOP   |     |             |  |    ~ attract ~   |   |             |     |  GHOSTMAZE | |
| |-------------|     |             |  |                  |   |             |     |------------| |
| |             |     |    ~demo~   |  | glow + hand on   |   |    ~demo~   |     |            | |
| |    ~demo~   |     |             |  | hover; click the |   |             |     |   ~demo~   | |
| |             |     |             |  | lit one = PLAY   |   |             |     |            | |
| |-------------|     |-------------|  |==================|   |-------------|     |------------| |
| |             |     |             |  |                  |   |             |     |            | |
| |    o o (*)  |     |    o o (*)  |  |      o o (*)     |   |    o o (*)  |     |   o o (*)  | |
| |             |     |             |  |                  |   |             |     |            | |
| +-------------+     +-------------+  +==================+   +-------------+     +------------+ |
|________________________________________________________________________________________________|
|   +----------------------------------------------------------------------------------------+   |
|   | BRICKBURST                                             | Your best   3625  Arcade      |   |
|   | Paddle, ball, bombs and twelve walls                   | World best  3625  Ghost       |   |
|   | of bricks.                                             | Time played 22m 41s           |   |
|   |                                                        |                               |   |
|   | Ready to play.  Daily challenge today.                 |                               |   |
|   +----------------------------------------------------------------------------------------+   |
|................................................................................................|
| [(A) PLAY]  [(Y) DAILY]  [(X) THIS GAME]  [(View) STATS]  [(Menu) MENU]     [<]  4 / 12  [>]   |
+------------------------------------------------------------------------------------------------+
- The room is unchanged: neon sign, spotlights, carpet, attract demos, room tint, coin drop.
- Header chips (y 16-40):
  - [Playing as TRG] opens Settings > Player (red "Set your name" when empty).
  - [NEW VERSION] shows only when you are behind, and opens Updates.
  - [= MENU] opens the Arcade menu.
  - [X] closes (lights on hover, acts when you let go).
  [EXIT] on the wall is now a real button.
- Cabinets: point at a side machine = soft glow + hand pointer; click it = it slides to the middle;
  click the lit one = coin drop and play (Decision 3). Mouse wheel = one machine per notch.
- Card (moved up 8 px): one status line that never runs into the right column.
- Control strip (y 600-632) replaces the grey hint line. Every chip is a button. After a key press it reads:
  [Enter] PLAY  [D] DAILY  [I] THIS GAME  [L] STATS  [Esc] MENU      [<] 4 / 12 [>]
  DAILY is faint, with the reason, on machines that have no daily. THIS GAME reads TROPHIES until
  the machine page (Phase 5) exists.

MOCKUP 2 - SETTINGS, ONLINE SECTION
+------------------------------------------------------------------------------------------------+
|                                                                                                |
| [ < BACK ]   S E T T I N G S                             Saved      Playing as TRG    [=]  [X] |
|              ===============                                                                   |
|                                                                                                |
|   +---------------------+  ONLINE                                                              |
|   |   Player            |  Scores and update checks. Nothing is sent unless it is on.          |
|   |                     |  ----------------------------------------------------------------    |
|   |   Sound             | #  SHARE MY SCORES               [  OFF  ][## ON ##]          #      |
|   |                     |    Sends your name, game, mode, score, date, a random ID.            |
|   |   Screen            |                                                                      |
|   |                     |    CHECK FOR UPDATES              [  OFF  ][## ON ##]                |
|   |#> ONLINE ###########|    One anonymous request to GitHub, at most every 6 hours.           |
|   |                     |                                                                      |
|   |   Controls          |    [ CHECK NOW ]  Checked 2 h ago: you have the newest.              |
|   |                     |                                                                      |
|   |   Machines          |    [ UPDATE DETAILS > ]                                              |
|   |                     |                                                                      |
|   |   Data              |    [ SYNC SCORES NOW ]   Last sync: 10 minutes ago                   |
|   +---------------------+                                                                      |
|                              SERVER                          set in online.conf                |
|                              (read only, on purpose)                                           |
|                                                                                                |
|                                                                                                |
|                             Turning sharing on syncs straight away.                            |
|                                                                                                |
|                                                                                                |
|                                                                                                |
|                                                                                                |
|................................................................................................|
|                                                                                                |
| [Up/Down] Choose  [Left/Right] Change  [Enter] Press  [Tab] Section  [Esc] Back                |
|                                                                                                |
+------------------------------------------------------------------------------------------------+
- [< BACK] at (16,16); title 24 px; a "Saved" note for 1.5 s after a change; [=] menu chip and [X] as on the hall.
- Sections x 32-240 (16 px labels, 40-44 px rows). Rows x 264-920, 40 px apart. The focused row gets the
  panel colour plus a 4 px pink bar: a colour change, never a size change (house rule).
- On/Off switch = two cells, and the lit one is the current value. Click either cell, or use Left/Right/Enter.
- [CHECK NOW] reads "Checking..." until the answer arrives. The server address stays file-only on purpose.
- Other sections:
  - PLAYER: name field [Edit]; "Names for single games" with [Set]/[Clear] per machine.
  - SOUND: Music volume * as ten clickable blocks, Hall sounds, [Play coin sound].
  - SCREEN: Fullscreen *, Reduced flashing *, Screen shake *, Button hints < Auto | Keyboard | Xbox >.
  - CONTROLS: key, pad and mouse cards.
  * = shared with every game. MACHINES and DATA appear only once those phases are built.
- After a pad press, the bottom line reads: (D-pad) Choose  (A) Press  (LB)(RB) Section  (B) Back

MOCKUP 3 - THE ARCADE MENU OVER THE HALL (Esc, pad B or Menu, or the [= MENU] chip)
+------------------------------------------------------------------------------------------------+
|                              #                                                                 |
|  ARCADE MENU                 #       [Playing as TRG]  [NEW VERSION]  [= MENU]  [X]            |
|  ===========                 #                                                                 |
|                              #              4 / 12                                             |
|                              #                                                                 |
|    Back to the hall     Esc  #       +==================+                                      |
|                              #       |    BRICKBURST    |                                      |
|                              #       |==================|                                      |
| #> Settings             Tab ##       |                  |                                      |
|                              #       |                  |   +-------------+                    |
|                              #       |                  |   |   SKYRAID   |                    |
|    Stats                  L  #       |                  |   |-------------|     +------------+ |
|                              #       |    ~ attract ~   |   |             |     |  GHOSTMAZE | |
|                              #       |                  |   |             |     |------------| |
|    Trophies               H  #       |                  |   |    ~demo~   |     |            | |
|                              #       |                  |   |             |     |   ~demo~   | |
|                              #       |                  |   |             |     |            | |
|    Updates  (1 new)       V  #       |==================|   |-------------|     |------------| |
|                              #       |                  |   |             |     |            | |
|                              #       |      o o (*)     |   |    o o (*)  |     |   o o (*)  | |
|    Leave the arcade          #       |                  |   |             |     |            | |
|                              #       +==================+   +-------------+     +------------+ |
|                              #_________________________________________________________________|
|                              #-------------------------------------------------------------+   |
|  Your name and online        #                             | Your best   3625  Arcade      |   |
|  sharing are in Settings.    #                             | World best  3625  Ghost       |   |
|                              #                             | Time played 22m 41s           |   |
|                              #                             |                               |   |
|                              #                             |                               |   |
|                              #-------------------------------------------------------------+   |
|  (A) Open      (B) Close     #.................................................................|
|                              #         the hall keeps moving behind the panel, dimmed          |
+------------------------------------------------------------------------------------------------+
- Opens over the empty left wall (x 0-300). The hall dims and keeps moving behind it. Until Settings exists
  (Phase 2), the menu also lists "Your name" and "Online sharing".
- Each item shows its old letter key, which still works without opening the menu.
- "Leave the arcade" asks first only if a game or an install is still running.
- Esc, pad B, Menu or a click outside the panel closes it.

Copies of these mockups: /tmp/claude-1000/-home-trg-Work/f3dd8f93-a9e7-4eec-88bb-fabcdce81bf2/scratchpad/launcher-plan/merged/mockups.txt
A draft machine-page mockup (Phase 5): /tmp/claude-1000/-home-trg-Work/f3dd8f93-a9e7-4eec-88bb-fabcdce81bf2/scratchpad/launcher-plan/features-angle/machine_mock.txt
```
