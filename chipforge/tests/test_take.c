/* Tests for the take (take.h): notes played along with a loop, caught onto
 * the pattern's grid. Quantising (ties forward, the next pass's step 0),
 * hold lengths that never wrap, a later note cutting an earlier one, drums
 * that only ever add hits, MERGE and REPLACE touching exactly what they
 * promise, and a full take refusing cleanly. Compiled by inclusion from
 * test_main.c. */
#include <stdio.h>
#include <string.h>
#include "song.h"
#include "take.h"

/* Static: a Take is 32 KB. */
static Take sTake;

static void take_EmptyPattern(SongPattern *p) {
    Song_ClearPattern(p);
}

/* A pattern with something in every track, so MERGE has cells to leave alone. */
static void take_BusyPattern(SongPattern *p) {
    for (int st = 0; st < SONG_STEPS; st++) {
        p->tone[TRACK_BASS][st] = (int8_t)(st % 8 == 0 ? 40 + st / 8 : (st % 8 == 1 ? NOTE_HOLD : NOTE_NONE));
        p->tone[TRACK_ARP][st] = (int8_t)(57 + st % 5);
        p->tone[TRACK_LEAD][st] = (int8_t)(st % 4 == 2 ? 72 : NOTE_NONE);
        p->drums[st] = (uint8_t)((st % 4 == 0 ? DRUM_KICK : 0) | (st % 2 ? DRUM_HAT : 0));
    }
}

static TakeCommit take_Pass(int pass, int grid) {
    TakeCommit c = {.grid = grid, .mode = TAKE_MERGE, .from = 64.0 * (pass - 1), .to = 64.0 * pass, .onlyTrack = -1};
    return c;
}

static void take_test_quantise(void) {
    CHECK(Take_Quantise(3.9, 1) == 4, "3.9 on 1/16 is 4");
    CHECK(Take_Quantise(4.49, 1) == 4, "4.49 on 1/16 is 4");
    CHECK(Take_Quantise(4.5, 1) == 5, "4.5 on 1/16 is 5 (ties go forward)");
    CHECK(Take_Quantise(4.9, 2) == 4, "4.9 on 1/8 is 4");
    CHECK(Take_Quantise(5.1, 2) == 6, "5.1 on 1/8 is 6");
    CHECK(Take_Quantise(5.0, 2) == 6, "5.0 on 1/8 is 6 (a tie goes forward)");
    CHECK(Take_Quantise(5.9, 4) == 4 && Take_Quantise(6.0, 4) == 8, "1/4: 5.9 is 4, 6.0 is 8");
    CHECK(Take_Quantise(63.6, 1) == 64, "63.6 is 64: the next pass's step 0");
    CHECK(Take_Quantise(-0.4, 1) == 0 && Take_Quantise(-0.6, 1) == -1, "the count-in rounds the same way");
    CHECK(Take_Quantise(-0.5, 1) == 0, "a tie in the count-in also goes forward");
    CHECK(Take_Quantise(4.6, 3) == 5, "an unknown grid counts as 1/16");
}

static void take_test_holds(void) {
    SongPattern p;
    take_EmptyPattern(&p);
    Take_Init(&sTake);
    int i = Take_NoteOn(&sTake, TRACK_LEAD, 60, 3.0);
    Take_NoteOff(&sTake, TRACK_LEAD, 60, 7.2);
    CHECK(i == 0, "the first press is id 0");
    TakeCommit c = take_Pass(1, 1);
    int cells = Take_Apply(&sTake, &c, &p);
    CHECK(p.tone[TRACK_LEAD][3] == 60, "held 3.0..7.2: the note on step 3");
    CHECK(p.tone[TRACK_LEAD][4] == NOTE_HOLD && p.tone[TRACK_LEAD][5] == NOTE_HOLD && p.tone[TRACK_LEAD][6] == NOTE_HOLD, "holds on 4..6");
    CHECK(p.tone[TRACK_LEAD][7] == NOTE_NONE, "step 7 untouched");
    CHECK(cells == 4, "four cells written");

    /* A release a moment early still keeps the step it nearly filled. */
    take_EmptyPattern(&p);
    Take_Init(&sTake);
    Take_NoteOn(&sTake, TRACK_BASS, 40, 8.0);
    Take_NoteOff(&sTake, TRACK_BASS, 40, 9.9);
    Take_Apply(&sTake, &c, &p);
    CHECK(p.tone[TRACK_BASS][8] == 40 && p.tone[TRACK_BASS][9] == NOTE_HOLD && p.tone[TRACK_BASS][10] == NOTE_NONE, "1.9 steps held is 2 steps");

    /* A tap shorter than a step is just the note. */
    take_EmptyPattern(&p);
    Take_Init(&sTake);
    Take_NoteOn(&sTake, TRACK_ARP, 64, 20.1);
    Take_NoteOff(&sTake, TRACK_ARP, 64, 20.3);
    Take_Apply(&sTake, &c, &p);
    CHECK(p.tone[TRACK_ARP][20] == 64 && p.tone[TRACK_ARP][21] == NOTE_NONE, "a tap is one step");

    /* Across the end of the pass: stops at 63, never wraps to step 0. */
    take_EmptyPattern(&p);
    Take_Init(&sTake);
    Take_NoteOn(&sTake, TRACK_LEAD, 67, 60.0);
    Take_NoteOff(&sTake, TRACK_LEAD, 67, 70.0);
    Take_Apply(&sTake, &c, &p);
    CHECK(p.tone[TRACK_LEAD][60] == 67 && p.tone[TRACK_LEAD][63] == NOTE_HOLD, "held across the end: holds to 63");
    CHECK(p.tone[TRACK_LEAD][0] == NOTE_NONE && p.tone[TRACK_LEAD][1] == NOTE_NONE, "and step 0 is left alone");

    /* Still held when the pass is written: held to the end of the pass. */
    take_EmptyPattern(&p);
    Take_Init(&sTake);
    Take_NoteOn(&sTake, TRACK_LEAD, 69, 50.0);
    Take_Apply(&sTake, &c, &p);
    bool toEnd = p.tone[TRACK_LEAD][50] == 69;
    for (int st = 51; st < SONG_STEPS; st++) toEnd = toEnd && p.tone[TRACK_LEAD][st] == NOTE_HOLD;
    CHECK(toEnd && p.tone[TRACK_LEAD][0] == NOTE_NONE, "a held note runs to step 63 and no further");

    /* A note caught a little early keeps the length it was played with. */
    take_EmptyPattern(&p);
    Take_Init(&sTake);
    Take_NoteOn(&sTake, TRACK_LEAD, 62, 3.6);
    Take_NoteOff(&sTake, TRACK_LEAD, 62, 6.6);
    Take_Apply(&sTake, &c, &p);
    CHECK(p.tone[TRACK_LEAD][4] == 62 && p.tone[TRACK_LEAD][5] == NOTE_HOLD && p.tone[TRACK_LEAD][6] == NOTE_HOLD &&
          p.tone[TRACK_LEAD][7] == NOTE_NONE, "3 steps played from 3.6: caught on 4, held 5..6");

    /* Notes are clamped into the song's range. */
    take_EmptyPattern(&p);
    Take_Init(&sTake);
    Take_NoteOn(&sTake, TRACK_LEAD, 120, 1.0);
    Take_NoteOn(&sTake, TRACK_BASS, 5, 2.0);
    Take_CloseAll(&sTake, 2.2);
    Take_Apply(&sTake, &c, &p);
    CHECK(p.tone[TRACK_LEAD][1] == NOTE_MAX && p.tone[TRACK_BASS][2] == NOTE_MIN, "notes clamp to NOTE_MIN..NOTE_MAX");
}

static void take_test_order(void) {
    SongPattern p;
    TakeCommit c = take_Pass(1, 1);

    /* Two notes on one step: the later wins. */
    take_EmptyPattern(&p);
    Take_Init(&sTake);
    Take_NoteOn(&sTake, TRACK_LEAD, 60, 10.1);
    Take_NoteOff(&sTake, TRACK_LEAD, 60, 14.0);
    Take_NoteOn(&sTake, TRACK_LEAD, 64, 10.3);
    Take_NoteOff(&sTake, TRACK_LEAD, 64, 11.2);
    Take_Apply(&sTake, &c, &p);
    CHECK(p.tone[TRACK_LEAD][10] == 64, "10.1 then 10.3: the later note wins the step");
    CHECK(p.tone[TRACK_LEAD][11] == NOTE_NONE && p.tone[TRACK_LEAD][12] == NOTE_NONE, "and the earlier note's holds are cut");
    CHECK(Take_NotesIn(&sTake, &c) == 1, "two presses on one step are one note");

    /* A later note cuts the earlier one's holds from its own step on. */
    take_EmptyPattern(&p);
    Take_Init(&sTake);
    Take_NoteOn(&sTake, TRACK_BASS, 40, 0.0);
    Take_NoteOn(&sTake, TRACK_BASS, 43, 4.0);  /* pressed while 40 is still held (legato) */
    Take_NoteOff(&sTake, TRACK_BASS, 43, 6.0);
    Take_NoteOff(&sTake, TRACK_BASS, 40, 12.0);
    Take_Apply(&sTake, &c, &p);
    CHECK(p.tone[TRACK_BASS][0] == 40 && p.tone[TRACK_BASS][3] == NOTE_HOLD, "the first note holds up to the second");
    CHECK(p.tone[TRACK_BASS][4] == 43 && p.tone[TRACK_BASS][5] == NOTE_HOLD, "the second note takes over");
    CHECK(p.tone[TRACK_BASS][6] == NOTE_NONE && p.tone[TRACK_BASS][11] == NOTE_NONE, "the first note's later holds are gone");

    /* 63.8 lands on step 0 of the next pass, not this one. */
    take_EmptyPattern(&p);
    Take_Init(&sTake);
    Take_NoteOn(&sTake, TRACK_LEAD, 72, 63.8);
    Take_NoteOff(&sTake, TRACK_LEAD, 72, 64.5);
    CHECK(Take_Apply(&sTake, &c, &p) == 0 && p.tone[TRACK_LEAD][63] == NOTE_NONE, "63.8 is not written into pass 1");
    TakeCommit c2 = take_Pass(2, 1);
    Take_Apply(&sTake, &c2, &p);
    CHECK(p.tone[TRACK_LEAD][0] == 72, "63.8 lands on step 0 of pass 2");

    /* Events of pass 3 land at their step mod 64. */
    take_EmptyPattern(&p);
    Take_Init(&sTake);
    Take_NoteOn(&sTake, TRACK_ARP, 55, 128.0 + 17.0);
    Take_CloseAll(&sTake, 128.0 + 17.5);
    TakeCommit c3 = take_Pass(3, 1);
    Take_Apply(&sTake, &c3, &p);
    CHECK(p.tone[TRACK_ARP][17] == 55, "pass 3 writes at step mod 64");

    /* Releases: only the latest open event with that note ends; a release
     * with nothing open is ignored. */
    Take_Init(&sTake);
    Take_NoteOn(&sTake, TRACK_LEAD, 60, 1.0);
    Take_NoteOff(&sTake, TRACK_LEAD, 60, 2.0);
    Take_NoteOn(&sTake, TRACK_LEAD, 60, 3.0);
    Take_NoteOff(&sTake, TRACK_LEAD, 60, 5.0);
    Take_NoteOff(&sTake, TRACK_LEAD, 60, 9.0);
    Take_NoteOff(&sTake, TRACK_ARP, 60, 9.0);
    CHECK(sTake.ev[0].off == 2.0 && sTake.ev[1].off == 5.0, "each release ends its own press; stray releases are ignored");
    Take_NoteOn(&sTake, TRACK_LEAD, 61, 7.0);
    Take_NoteOff(&sTake, TRACK_LEAD, 61, 6.9);
    CHECK(sTake.ev[2].off == 7.0, "a release before its press (the clock nudged back) is a zero-length note, not a held one");
}

static void take_test_merge_replace(void) {
    SongPattern base, p;
    take_BusyPattern(&base);

    /* MERGE: only the written cells change. */
    memcpy(&p, &base, sizeof(p));
    Take_Init(&sTake);
    Take_NoteOn(&sTake, TRACK_LEAD, 76, 9.0);
    Take_NoteOff(&sTake, TRACK_LEAD, 76, 11.0);
    TakeCommit c = take_Pass(1, 1);
    int cells = Take_Apply(&sTake, &c, &p);
    bool same = true;
    for (int st = 0; st < SONG_STEPS; st++)
        if (st != 9 && st != 10) same = same && p.tone[TRACK_LEAD][st] == base.tone[TRACK_LEAD][st];
    CHECK(cells == 2 && p.tone[TRACK_LEAD][9] == 76 && p.tone[TRACK_LEAD][10] == NOTE_HOLD, "MERGE writes the note and its hold");
    CHECK(same, "MERGE leaves every other lead cell as it was");
    CHECK(memcmp(p.tone[TRACK_BASS], base.tone[TRACK_BASS], sizeof(base.tone[0])) == 0 &&
          memcmp(p.tone[TRACK_ARP], base.tone[TRACK_ARP], sizeof(base.tone[0])) == 0 &&
          memcmp(p.drums, base.drums, sizeof(base.drums)) == 0, "MERGE leaves the other tracks byte-identical");

    /* REPLACE on one track: that track is cleared first, nothing else moves. */
    memcpy(&p, &base, sizeof(p));
    c.mode = TAKE_REPLACE;
    c.onlyTrack = TRACK_LEAD;
    Take_Apply(&sTake, &c, &p);
    bool cleared = true;
    for (int st = 0; st < SONG_STEPS; st++)
        if (st != 9 && st != 10) cleared = cleared && p.tone[TRACK_LEAD][st] == NOTE_NONE;
    CHECK(cleared && p.tone[TRACK_LEAD][9] == 76, "REPLACE clears the lead, then writes");
    CHECK(memcmp(p.tone[TRACK_BASS], base.tone[TRACK_BASS], sizeof(base.tone[0])) == 0 &&
          memcmp(p.tone[TRACK_ARP], base.tone[TRACK_ARP], sizeof(base.tone[0])) == 0 &&
          memcmp(p.drums, base.drums, sizeof(base.drums)) == 0, "REPLACE clears only its own track");

    /* REPLACE over every track clears only the tracks it has notes for. */
    memcpy(&p, &base, sizeof(p));
    c.onlyTrack = -1;
    Take_Apply(&sTake, &c, &p);
    CHECK(memcmp(p.tone[TRACK_BASS], base.tone[TRACK_BASS], sizeof(base.tone[0])) == 0 &&
          memcmp(p.drums, base.drums, sizeof(base.drums)) == 0 && p.tone[TRACK_LEAD][2] == NOTE_NONE,
          "REPLACE on all tracks clears just the played ones");

    /* onlyTrack keeps other tracks' events out. */
    memcpy(&p, &base, sizeof(p));
    Take_Init(&sTake);
    Take_NoteOn(&sTake, TRACK_BASS, 41, 4.0);
    Take_NoteOn(&sTake, TRACK_LEAD, 77, 4.0);
    Take_CloseAll(&sTake, 4.5);
    TakeCommit only = {.grid = 1, .mode = TAKE_MERGE, .from = 0, .to = 64, .onlyTrack = TRACK_LEAD};
    Take_Apply(&sTake, &only, &p);
    CHECK(p.tone[TRACK_LEAD][4] == 77 && p.tone[TRACK_BASS][4] == base.tone[TRACK_BASS][4], "onlyTrack writes that track alone");
    CHECK(Take_NotesIn(&sTake, &only) == 1, "and counts that track alone");

    /* Drums OR their bits and never toggle a hit off. */
    memcpy(&p, &base, sizeof(p));
    Take_Init(&sTake);
    Take_NoteOn(&sTake, TRACK_DRUMS, DRUM_KICK, 0.1);   /* step 0 already has a kick */
    Take_NoteOn(&sTake, TRACK_DRUMS, DRUM_SNARE, 4.2);  /* step 4: kick there, add a snare */
    Take_NoteOn(&sTake, TRACK_DRUMS, DRUM_HAT, 2.0);    /* step 2: nothing there */
    Take_NoteOn(&sTake, TRACK_DRUMS, DRUM_HAT, 2.1);    /* twice on one step */
    Take_CloseAll(&sTake, 3.0);
    c.mode = TAKE_MERGE;
    Take_Apply(&sTake, &c, &p);
    CHECK(p.drums[0] == base.drums[0], "a kick over a kick stays a kick (no toggle)");
    CHECK(p.drums[4] == (DRUM_KICK | DRUM_SNARE), "a snare is added beside the kick");
    CHECK(p.drums[2] == DRUM_HAT, "a hat on an empty step");
    CHECK(memcmp(p.tone, base.tone, sizeof(base.tone)) == 0, "drum hits touch no tone track");
    CHECK(Take_NotesIn(&sTake, &c) == 3, "three drum steps");

    /* Two passes over the same drums: hits only accumulate. */
    Take_Apply(&sTake, &c, &p);
    CHECK(p.drums[0] == base.drums[0] && p.drums[4] == (DRUM_KICK | DRUM_SNARE), "a second identical pass changes nothing");
}

static void take_test_forget_and_full(void) {
    Take_Init(&sTake);
    Take_NoteOn(&sTake, TRACK_LEAD, 60, -3.0);  /* count-in, caught before 0 */
    Take_NoteOn(&sTake, TRACK_LEAD, 61, 10.0);
    Take_NoteOn(&sTake, TRACK_LEAD, 62, 63.0);  /* on 1/8 this is caught at 64 */
    Take_NoteOn(&sTake, TRACK_LEAD, 63, 70.0);
    Take_Forget(&sTake, 2, 64);
    CHECK(sTake.count == 2 && sTake.ev[0].note == 62 && sTake.ev[1].note == 63, "Forget drops what was caught before the boundary, keeps order");

    Take_Init(&sTake);
    bool ok = true;
    for (int i = 0; i < TAKE_MAX_EVENTS; i++) ok = ok && Take_NoteOn(&sTake, TRACK_DRUMS, DRUM_HAT, (double)(i % 64) + 0.1) == i;
    CHECK(ok && sTake.count == TAKE_MAX_EVENTS, "1024 events are kept");
    CHECK(Take_NoteOn(&sTake, TRACK_DRUMS, DRUM_KICK, 5.0) == -1 && sTake.count == TAKE_MAX_EVENTS, "the 1025th is refused");
    SongPattern p;
    take_EmptyPattern(&p);
    TakeCommit c = take_Pass(1, 1);
    int cells = Take_Apply(&sTake, &c, &p);
    bool hats = true;
    for (int st = 0; st < SONG_STEPS; st++) hats = hats && p.drums[st] == DRUM_HAT;
    CHECK(hats && cells == TAKE_MAX_EVENTS, "a full take still writes every hit");
    CHECK(Take_NoteOn(&sTake, 7, 60, 1.0) == -1 && Take_NoteOn(&sTake, -1, 60, 1.0) == -1, "a track out of range is refused");

    /* A full take of tone notes: the later wins on every step, no overflow. */
    Take_Init(&sTake);
    for (int i = 0; i < TAKE_MAX_EVENTS; i++) Take_NoteOn(&sTake, TRACK_LEAD, 48 + i % 24, (double)i / 16.0);
    Take_CloseAll(&sTake, 64.0);
    take_EmptyPattern(&p);
    Take_Apply(&sTake, &c, &p);
    bool sane = true;
    for (int st = 0; st < SONG_STEPS; st++) sane = sane && p.tone[TRACK_LEAD][st] >= NOTE_MIN && p.tone[TRACK_LEAD][st] <= NOTE_MAX;
    CHECK(sane, "1024 dense lead notes: every step holds a real note (the later one)");
}

/* A pickup let go before the first downbeat has a negative release time: it
 * must read as released, not as a key still down (held to step 63). */
static void take_test_count_in_release(void) {
    SongPattern p;
    for (int grid = 1; grid <= 4; grid *= 2) {
        take_EmptyPattern(&p);
        Take_Init(&sTake);
        double on = -0.45 * grid, off = on + 0.35 * grid; /* a tap just before the downbeat */
        Take_NoteOn(&sTake, TRACK_LEAD, 72, on);
        Take_NoteOff(&sTake, TRACK_LEAD, 72, off);
        CHECK(!sTake.ev[0].held && sTake.ev[0].off == off, "a release in the count-in ends the note");
        TakeCommit c = take_Pass(1, grid);
        Take_Apply(&sTake, &c, &p);
        CHECK(p.tone[TRACK_LEAD][0] == 72 && p.tone[TRACK_LEAD][1] == NOTE_NONE && p.tone[TRACK_LEAD][63] == NOTE_NONE,
              "a pickup tapped and let go before the downbeat is one step, not held to 63");
    }
    /* Closing everything in the count-in (Tab, or F10 to stop) ends it too. */
    take_EmptyPattern(&p);
    Take_Init(&sTake);
    Take_NoteOn(&sTake, TRACK_BASS, 40, -0.9);
    Take_CloseAll(&sTake, -0.3);
    Take_NoteOff(&sTake, TRACK_BASS, 40, 30.0); /* the key comes up later: already ended */
    CHECK(!sTake.ev[0].held && sTake.ev[0].off == -0.3, "CloseAll in the count-in ends the note there, and a later release leaves it");
    TakeCommit c = take_Pass(1, 2);
    Take_Apply(&sTake, &c, &p);
    CHECK(p.tone[TRACK_BASS][0] == 40 && p.tone[TRACK_BASS][1] == NOTE_NONE, "so it is written as the step it was played");
}

/* Releases find their own press by id: two held keys can give one pitch. */
static void take_test_release_by_id(void) {
    SongPattern p;
    take_EmptyPattern(&p);
    Take_Init(&sTake);
    int q = Take_NoteOn(&sTake, TRACK_LEAD, 72, 0.0);  /* Q at octave 4 */
    int z = Take_NoteOn(&sTake, TRACK_LEAD, 72, 4.0);  /* Z at octave 5, Q still down */
    CHECK(q == 0 && z == 1, "presses get ids 0, 1, ...");
    Take_NoteOffId(&sTake, q, 4.5);                     /* Q up */
    Take_NoteOffId(&sTake, z, 12.0);                    /* Z up, 8 steps after it went down */
    CHECK(sTake.ev[0].off == 4.5 && sTake.ev[1].off == 12.0, "each release ends the press its key made, not the latest of that pitch");
    TakeCommit c = take_Pass(1, 1);
    Take_Apply(&sTake, &c, &p);
    bool zHeld = p.tone[TRACK_LEAD][4] == 72;
    for (int st = 5; st <= 11; st++) zHeld = zHeld && p.tone[TRACK_LEAD][st] == NOTE_HOLD;
    CHECK(zHeld && p.tone[TRACK_LEAD][12] == NOTE_NONE, "the second key's note holds for as long as it was down");

    /* Ids survive a written pass (indices do not), and a stale or refused id
     * ends nothing. */
    Take_Init(&sTake);
    int a = Take_NoteOn(&sTake, TRACK_LEAD, 60, 10.0);
    int b = Take_NoteOn(&sTake, TRACK_LEAD, 62, 70.0);
    Take_Forget(&sTake, 1, 64);                         /* pass 1 written: a is gone, b moves to index 0 */
    int d = Take_NoteOn(&sTake, TRACK_LEAD, 60, 71.0);
    CHECK(d != a && d != b, "an id is not handed out again after a pass is forgotten");
    Take_NoteOffId(&sTake, a, 72.0);                    /* a's key comes up: its press is gone */
    Take_NoteOffId(&sTake, -1, 72.0);                   /* a press the take refused */
    CHECK(sTake.ev[0].held && sTake.ev[1].held, "a forgotten or refused press ends nothing");
    Take_NoteOffId(&sTake, b, 73.0);
    Take_NoteOffId(&sTake, b, 80.0);                    /* a second release of the same press */
    CHECK(!sTake.ev[0].held && sTake.ev[0].off == 73.0 && sTake.ev[1].held, "the id finds its press at its new index, once");
}

/* An overdub ends where it was played: the pattern's own holds after a new
 * note (an older long note's tail) would otherwise sound the new note on. */
static void take_test_overdub_holds(void) {
    SongPattern p;
    take_EmptyPattern(&p);
    p.tone[TRACK_LEAD][0] = 60;
    for (int st = 1; st < 16; st++) p.tone[TRACK_LEAD][st] = NOTE_HOLD;
    p.tone[TRACK_LEAD][20] = 67;                        /* a later note, then a hold after a rest */
    p.tone[TRACK_LEAD][22] = NOTE_HOLD;
    p.tone[TRACK_BASS][5] = NOTE_HOLD;                  /* another track's holds stay */
    Take_Init(&sTake);
    Take_NoteOn(&sTake, TRACK_LEAD, 64, 4.0);
    Take_NoteOff(&sTake, TRACK_LEAD, 64, 4.25);         /* a quarter-step tap */
    TakeCommit c = take_Pass(1, 1);
    Take_Apply(&sTake, &c, &p);
    bool ended = true;
    for (int st = 5; st < 16; st++) ended = ended && p.tone[TRACK_LEAD][st] == NOTE_NONE;
    CHECK(p.tone[TRACK_LEAD][4] == 64 && ended, "a tap over an older long note is one step: that note's later holds go");
    CHECK(p.tone[TRACK_LEAD][0] == 60 && p.tone[TRACK_LEAD][3] == NOTE_HOLD, "the older note still sounds up to the tap");
    CHECK(p.tone[TRACK_LEAD][20] == 67 && p.tone[TRACK_LEAD][22] == NOTE_HOLD && p.tone[TRACK_BASS][5] == NOTE_HOLD,
          "only the run straight after the new note is ended");

    /* A note held over old holds keeps its own length and ends the rest. */
    take_EmptyPattern(&p);
    p.tone[TRACK_ARP][0] = 55;
    for (int st = 1; st < 32; st++) p.tone[TRACK_ARP][st] = NOTE_HOLD;
    Take_Init(&sTake);
    Take_NoteOn(&sTake, TRACK_ARP, 59, 8.0);
    Take_NoteOff(&sTake, TRACK_ARP, 59, 12.0);
    Take_Apply(&sTake, &c, &p);
    CHECK(p.tone[TRACK_ARP][8] == 59 && p.tone[TRACK_ARP][11] == NOTE_HOLD && p.tone[TRACK_ARP][12] == NOTE_NONE &&
          p.tone[TRACK_ARP][31] == NOTE_NONE, "held 4 steps over a 32-step note: 4 steps, and the old tail is gone");
}

void test_take_all(void) {
    take_test_quantise();
    take_test_holds();
    take_test_order();
    take_test_merge_replace();
    take_test_forget_and_full();
    take_test_count_in_release();
    take_test_release_by_id();
    take_test_overdub_holds();
}
