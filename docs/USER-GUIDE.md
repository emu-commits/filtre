# FILTRE user guide

Key by key. Key names are what is printed on the Cardputer: `Aa` is shift,
`ok` is enter, `space` is the space bar.

**Tap** means press and release. **Hold** means keep it down about half a
second; a bar fills in the footer and names what the hold will do. Let go
before the bar fills and you get the tap instead.

**Two screens.** With the lid open, the big lid screen is where you edit: the
pattern grid, the settings list, the file browser, the song. The small screen
shows what goes with it: both deck waveforms on PATTERN, the selected setting
in large type on SETTINGS. Messages and questions appear at the bottom of
both. With no lid the small screen is the editor.

## The keyboard at a glance

```
 `   1   2   3   4   5   6   7   8   9   0   -   =     del
 T1  T2  T3  A   B   D1  D2  D3  D4  D5  D6  D7  D8    SAVE
 └── tap = mute that row, hold = select it ──────┘

tab  q   w   e   r   t   y   u   i   o   p   [    ]    \
RES-         CUT-            CUT+            RES+ PAT- PAT+ LOAD
     └── black keys / chops 9-15 ──┘

 fn  Aa  a   s   d   f   g   h   j   k   l   ;   '     ok
 └──── pads 1-8 / chops 1-8 ────┘ (16)           ▲  COPY  KEY MAP

ctrl opt alt  z   x   c   v   b   n    m    ,    .    /    space
PAGE OCT- OCT+ ACC SLD CLR REC AUX SONG      ◄    ▼    ►    PLAY
```

The top row is the pattern grid turned sideways: its 13 keys are the 13 rows
(synths T1 T2 T3, decks A B, drum lanes 1 to 8).

| Key | Tap | Hold |
|---|---|---|
| `space` | play / stop | play from the top |
| `ctrl` | next page | jump to PATTERN page |
| `v` | record on / off | |
| `,` `.` `;` `/` | left, down, up, right: the arrows printed on the keys | repeats |
| `c` | clear the step under the cursor | asks **CLEAR P1: Are you sure?** Press `c` again to clear the whole pattern |
| `z` | (nothing alone) accent while held | |
| `` ` `` `1` `2` | mute / unmute synth track T1, T2, T3 | select that track |
| `3` `4` | mute / unmute deck A, deck B | select that deck |
| `5` … `=` | mute / unmute drum lane 1 … 8 | select that lane |
| `[` / `]` | previous / next pattern. While playing, the change waits for the end of the pattern | |
| `'` | copy: remember this pattern | asks **PASTE P1>P3: You sure?** Press `'` again to overwrite this pattern with the copied one |
| `opt` / `alt` | octave down / up (SONG page: project slot) | |
| `e` / `u` | filter cutoff down / up | sweeps while held |
| `tab` / `p` | resonance down / up | sweeps while held |
| `x` | slide on the note under the cursor | write a bass line |
| `b` | (depends on page) | record from the mic |
| `n` | song mode on / off. It also picks which mutes the top row edits: the pattern's (off) or the song slot's (on) | resample (while playing) |
| `m` | PATTERN page: the next bar of a pattern longer than one bar. SONG page: the arrangements on the card | |
| `\` | show which project slot | asks **LOAD: Are you sure?** Press `\` again to load |
| `del` | show which project slot | asks **SAVE: Are you sure?** Press `del` again to save |
| `\` + `del` together | load the built-in demo | |
| `ok` | (nothing) | while held, the lid shows the key map |

**Forgot a key? Hold `ok`.** The lid screen turns into a picture of the
keyboard with every key labelled for the page and track you are on, and a
colour-matched legend underneath. Let go and the lid goes back to where it
was. Everything else keeps working while you hold it, so you can hold `ok`,
find the key, and press it. The music does not pause. This is the only help
on the device, and it is always current: it is drawn from the same table the
keys are read from.

**Tempo has no key.** It is the **BPM** row on the SETTINGS page (section 3).

The pages, in the order `ctrl` steps through them:
**PATTERN → SETTINGS → SAMPLE → SONG**.

The footer of the screen shows what just happened ("REC ON", "DECK A: 2 BAR").

---

## 0. Hear the demo first

1. Hold `\` and `del` together until the footer says **DEMO: PLAY**.
2. Tap `space`.

You hear a four-slot song: the loop with the filter nearly shut and no kick,
then kick and bass with the filter opening, then wide open with 16th hats, then
back down. Deck A shows **FILTRE-DEMO**. Tap `space` to stop.

The demo replaces patterns 1–4 and the song chain. It does not touch the card.

## 1. Put your own sample on the card

Cut the section you want on a computer first and export it as **WAV**
(any bit depth, mono or stereo, any sample rate). Aim for **1, 2 or 4 bars**,
cut exactly on the bar lines. Copy it to `/groovebox/loops/` on the SD card.

Memory is small. Both decks together hold about 5 seconds at full quality or
10 seconds lo-fi. A longer file is cut to what fits and the footer says so.

## 2. Load it onto a deck

1. Hold `3` until the footer says **DECK A** (`4` is deck B).
2. Tap `ctrl` until you are on the **SAMPLE** page. It lists `/groovebox/loops`.
3. `.` / `;` move down / up the list.
4. Tap `b` to load the highlighted file into the selected deck.
5. The footer says **DECK A: 2 BAR** (it guessed the length at the current
   tempo), **DECK A: FIT?** (it could not guess), or **CUT TO 6.2s**.

## 3. Match the tempo

Either set the tempo to the sample, or stretch the sample to the tempo.

- **Tempo from the sample:** `ctrl` to **SETTINGS** (deck still selected),
  `.` down to the row **BPM<LOOP**, tap `/`. Footer: **BPM FROM LOOP**.
- **Tempo by hand:** SETTINGS page, **BPM** row (the first amber row, on every
  track), `,` / `/` for −1 / +1. Hold the key and it speeds up.
- **Stretch the sample:** on SETTINGS go to **FIT** and use `,` / `/` to pick
  1, 2, 4 or 8 bars. This changes the pitch along with the speed.

## 4. Choose how the deck plays: LOOP or CHOP

On the **SETTINGS** page with the deck selected, the top row is **MODE**. Tap `/`
to switch.

- **LOOP**: the sample plays by itself, locked to the bar, whenever the
  sequencer runs. Nothing to program. Fastest way to a filter-house loop.
- **CHOP**: the sample is cut at its transients and each piece sits on a key.
  Nothing plays until you play or program it.

On the SETTINGS page, `.` / `;` pick a row and `,` / `/` change it. Hold `z`
while pressing `,` / `/` for fine steps. Grey rows belong to the selected track;
amber rows (tempo, swing, key, filter, pump, lid) are the same whichever track
is selected.

## 5. Play the chops and build a loop (CHOP mode)

Select the deck (hold `3` for A, `4` for B) and go to the **PATTERN** page (hold `ctrl`).

```
chops  1   2   3   4   5   6   7   8        9  10  11  12  13  14  15  16
keys   fn  Aa  a   s   d   f   g   h        q   w   r   t   y   i   o   j
```

Hold `z` while hitting a key for an accented (louder) hit.

**Record it live**

1. Tap `v` so the footer says **REC ON**.
2. Tap `space`. The drums play; tap the chop keys in time.
3. Hits are snapped to the nearest 16th and loop round straight away.
4. Wrong hit: hold `c` while it plays past, and it is erased at the playhead.
5. Tap `v` to stop recording. The loop keeps playing.

**Or write it step by step**

1. Stop the sequencer (`space`). Tap `v` for **REC ON**.
2. Move the cursor with `,` / `/` to the step you want.
3. Tap a chop key: it is written at the cursor and the cursor moves on a step.
4. `c` clears the step under the cursor.

**Change the chops** (SETTINGS page, deck selected; the small screen shows the
waveform with the slice marks while you do)

- **SLICES**: AUTO (transients), 4, 8 or 16 equal pieces.
- **SLICE POS**: play a chop, then `,` / `/` nudge where it starts.
- **IN** / **OUT**: trim the start and end of the whole sample.
- **PITCH**: ±12 semitones. **LEVEL**: deck volume.

**One to four bars:** SETTINGS page, **LENGTH** row: `/` adds a bar, `,` takes
the last one away (what was in it is gone). New bars start as copies of the
ones the pattern has. On the PATTERN page moving the cursor past step 16 shows
the next bar, and `m` steps through the bars, keeping the same column; the
title reads `PT5.3` for pattern 5, bar 3.

**Pattern memory:** there are 64 patterns and room for 96 bars between them.
Only bars with something in them count, so empty patterns are free. The
bottom line of the lid shows `BARS` left; when it reaches 0 the footer says
`PATTERN MEMORY FULL` until you clear or shorten a pattern.

## 6. The filter

Works on every page except SONG. It filters both decks, not the drums or bass.

| Key | Does |
|---|---|
| `e` | cutoff down. Tap = small step, hold = sweep |
| `u` | cutoff up |
| `tab` | resonance down |
| `p` | resonance up |

The bar in the header shows where the cutoff is.

**Record a sweep into the pattern:** tap `v` (REC ON), tap `space`, then hold
`e` or `u`. The movement is stored on the steps you swept across and repeats
every time the pattern plays. To remove it, select the deck, put the cursor on
a step and tap `c` (this also clears a chop on that step).

**Filter type and slope:** SETTINGS page, rows **TYPE** (LP / HP /
BP / PHASER) and **SLOPE** (12 / 24 dB).

**PHASER** is the slow whole-loop phaser of French house. It rocks by itself,
once every four bars. `e` / `u` move where the sweep sits (and can be recorded
like any filter move), `tab` / `p` set the feedback: low is a soft swirl, high
is the sharp, vocal jet sound. **SLOPE** picks **4 STAGE** (the classic pedal)
or **8 STAGE** (denser).

## 7. Drums

Hold any of `5` … `=` to select that drum lane (the footer says **LANE 1**),
PATTERN page.

```
lane   1     2     3     4     5       6    7      8
key    fn    Aa    a     s     d       f    g      h
sound  kick  clap  hat   open  shaker  rim  snare  808 kick
```

Record exactly as with chops: `v` then `space` and play, or stopped with REC on
to write at the cursor. Hold `z` for accents. Tapping `c` on the drum track
clears **every** lane on that step.

A FILTRE drum pattern is already on pattern 1. Other ready-made grooves:
SETTINGS page, **STYLE** row, `,` / `/` steps through FILTRE, NU-DISCO,
HOMEWORK, BANGER. If you have changed the drums yourself it asks
**Replace drums?** Press the same arrow again for yes.

**Change a drum sound:** SETTINGS page with drums selected. Tap a pad to pick
the lane, `.` / `;` for the row, `,` / `/` to change it (engine, sound, volume,
tune, decay, choke).

**Use your own one-shots:** put short WAVs in `/groovebox/samples/`. With drums
selected go to the SAMPLE page, highlight a file, tap `b` to hear it, tap
a pad (`fn` `Aa` `a` `s` `d` `f` `g` `h`) to put it on lane 1 … 8.

## 8. The pump (sidechain)

Lane 1, the kick, ducks the decks every time it hits. SETTINGS page:

- **PUMP**: depth, 0–100. 0 is off.
- **RELEASE**: how long it takes to come back.
- **BASS PUMP**: how much the bass ducks as well.
- **GHOST**: when on, the kick steps still pump even while the kick is muted.
  That is the filtered-intro sound: no kick, but the loop is already breathing.

## 9. Bass

**Have it written for you:** SETTINGS page, set **KEY** (the key of
your sample) and **BASSMODE**. Go back to PATTERN and hold `x`. A bass line in
that key is written onto T1 of the current pattern.

**Play it yourself:** tap `` ` `` to select T1.

```
white  fn  Aa  a   s   d   f   g   h   j   k   l
note   C   D   E   F   G   A   B   C   D   E   F

black  q   w   r   t   y   i   o
note   C#  D#  F#  G#  A#  C#  D#
```

Tap `opt` / `alt` for octave down / up. Record with `v` exactly as above.
Tap `x` on a note to make it slide into the next one. Hold `1` or `2` to select the
other two synth tracks (T2 is a chord stab).

**Synth sound:** SETTINGS page with T1, T2 or T3 selected; `.` / `;` rows,
`,` / `/` change, any piano key auditions it. With **OSC** on **WT** the
**WTABLE** row picks the table; **STRINGS** (bright, bowed) and **SUPERSAW** are
several detuned copies at once, so they shimmer on a held chord.

## 10. Make variations

Tap `'` on the pattern you like (**P1 COPIED**), tap `]` to go to the next
pattern, then hold `'`: **PASTE P1>P2: You sure?** Press `'` again. Change
something (drop the kick, add hats, a different chop order) and repeat.

`[` / `]` step through the eight patterns. While the sequencer is playing the
change waits for the end of the pattern that is sounding, and the title shows
it as **PT1>3**. Press again before then to change your mind: three taps of
`]` queue the pattern three further on.

**Mutes.** Tap a key of the top row to silence that row: `` ` `` `1` `2` the
synths, `3` `4` the decks, `5` … `=` the drum lanes (`5` is the kick). Tap
again to bring it back. Hold `ok` to see which rows are muted (red).

Mutes are a state, not something you record: REC does not matter, and a mute
covers the whole pattern. There are two sets, and **song mode (`n`) picks the
one you hear and edit** on every page except SONG:

- **Song mode off:** the mutes belong to the pattern and are saved with it, so
  pattern 2 can be pattern 1 without the kick: copy, paste, tap `5`.
- **Song mode on:** the mutes belong to the song slot that is playing (stopped:
  the slot under the cursor on the SONG page), for all its repeats. The
  pattern's own mutes are ignored.

Neither set is ever copied onto the other: switch back and forth and each is
as you left it. The one exception is a slot you have just created, which
starts with its pattern's mutes. **On the SONG page the mutes always belong
to the slot** (the playing one, else the one under the cursor), whatever song
mode says; leaving the page does not change song mode. The footer says where a
mute landed:
**P3 LANE 1 MUTED** or **S12 LANE 1 MUTED**.

## 11. Arrange the song

`ctrl` to the **SONG** page. Each slot is one pattern, played a number of
times, with its own mutes and a filter target. The 13 small marks in a slot
are the 13 rows, left to right; a red one is muted.

| Key | Does |
|---|---|
| `,` `/` | move one slot |
| `.` `;` | move one row (16 slots) |
| `]` / `[` | next / previous pattern in this slot. An empty slot starts as a copy of the slot before it |
| `p` / `tab` | repeats +1 / −1 (1 to 16) |
| `u` / `e` | filter target up / down. The cutoff glides to this value across the slot. Below zero = no target |
| top row `` ` `` … `=` | mute that row in this slot (always, on this page). Same 13 keys as everywhere else |
| `'` | tag this slot. Hold `'` on another slot to paste the tagged one over it: pattern, repeats, mutes and filter target. A small mark shows the tagged slot |
| `m` | list the arrangements on the card (see below) |
| `c` | empty this slot |
| `b` | make this slot the loop-back point |

The song ends at the first empty slot and jumps back to the loop-back point.

On any other page tap `n` for **SONG MODE**, then hold `space` to play from the
top. Tap `n` again to go back to looping one pattern. The SONG page has no `n`,
`z`, `x` or `v` of its own. It shows **SONG(PT)** when song mode is off, which
means Play there still plays the one pattern.

A classic shape, using one pattern and its copies:

| Slot | Pattern | Repeats | Mutes | Filter target |
|---|---|---|---|---|
| 1 | P1 | 8 | kick (`5`), bass (`` ` ``) | low (about 50) |
| 2 | P1 | 8 | bass | rising (about 140) |
| 3 | P1 | 16 | none | open (254) |
| 4 | P1 | 8 | kick | low again |

One pattern is enough for that: the slots carry the mutes.

### Start from a reference track

The **arranger** is a web page that maps a record bar by bar in FILTRE's own
terms: <https://emu-commits.github.io/filtre/arranger/>

1. Open the track there (MP3 or WAV; it stays on your computer). It finds the
   tempo and the bar lines and shows every measure.
2. Give each measure a pattern number and mute the parts that are silent in
   it. Say per pattern how long it is, 1 to 4 bars. The **Filter** slider
   (keys `e` `u`, as on the SONG page) sets where the cutoff has got to by the
   end of a section; leave it at *Not driven* to keep the filter in your hands.
   **Filter type** (low pass, high pass, band pass, phaser) is one choice for
   the whole project; *As on FILTRE* leaves the type the device has.
3. **Save .arr** and copy the file to `/groovebox/songs/` on the card.
4. On FILTRE: SONG page, tap `m`. Pick the file with `;` and `.`, hold `\`,
   and answer **LOAD …: You sure?** with `\` again.

That replaces the song chain, sets the length of the patterns the file names
and the tempo (and the filter type, if you chose one), and turns song mode on. It does not touch your notes, sounds,
decks or the patterns' own mutes, and nothing is written to the card until you
save the project. From there the song is yours to change like any other.

## 12. Save and load

Projects live in eight slots, P1–P8.

- **Pick the slot:** on the SONG page tap `opt` (previous) or `alt` (next).
- **Save:** hold `del`. Both screens ask **SAVE P1: Are you sure?** Press `del`
  again: **SAVED**.
- **Load:** hold `\`. **LOAD P1: Are you sure?** Press `\` again: **LOADED**.
- Any other key, or four seconds, answers no (**CANCELLED**).
- Tapping `\` or `del` just tells you which slot is current.

Samples are saved by file name, so keep the WAVs on the card.

## 13. Bounce to the card

On the **SONG** page hold `b`. A progress bar runs and the footer says
**BOUNCED T01**. On the card, in `/groovebox/bounce/`:

```
T01_122bpm_mix.wav     the whole thing
T01_122bpm_drums.wav
T01_122bpm_bass.wav
T01_122bpm_synth.wav
T01_122bpm_decks.wav   the filtered, pumped sample
```

All start on bar 1 and are the same length, so drop them into a DAW at the
tempo in the file name and put the acapella on top. Stems that would be silent
are not written. With an empty song it bounces four passes of the current
pattern.

## Extras

**Record from the microphone.** PATTERN or SETTINGS page. Select a drum lane or a
deck, hold `b`, make the sound, let go. It lands on that lane or deck. The
sequencer stops while recording.

**Resample.** While playing, hold `n`.
- Drums selected: about two seconds of the mix are captured; then tap a pad
  (`fn` … `h`) to choose the lane it goes on.
- Deck selected: exactly one pass of the pattern is captured from the next bar
  line and loaded onto that deck. This is how you filter a loop, print it, and
  filter it again.

**Swing.** SETTINGS page, **SWING** (50 = straight, 62 is the
FILTRE default). It moves drums and synths only.

**Blend.** SETTINGS page, **BLEND** crossfades deck A against deck B.

**Long file, wrong part.** SETTINGS page, deck selected, **START** moves where loading begins, in
seconds. **QUALITY** forces 16-bit or lo-fi. Both apply the next time you load.

**Lid.** SETTINGS page, last row **LID**: off or on. With it off the small
screen becomes the editor.

**Memory check.** The bottom line of the lid shows what each deck and the
one-shots are using, how much sample memory is free, and how many bars of
pattern memory are left (`BARS`).

## When something does not work

| Footer says | Meaning |
|---|---|
| PG > SAMPLE: LOAD A LOOP | you hit a chop key but the deck is empty |
| LOAD FAILED | not a WAV, or no room left. Unload by loading something shorter |
| CUT TO 6.2s | the file was longer than free memory; you have the first 6.2 s from START |
| PATTERN MEMORY FULL | all 96 bars are in use; the note was not written. Clear or shorten a pattern |
| DECK A: FIT? | length is not a whole number of bars at this tempo; use BPM<LOOP or FIT |
| NO ROOM / BUSY | resample or mic has no memory left |
| PLACE A PATTERN FIRST | SONG page: the slot is empty, put a pattern in it first |
| NO SD CARD | bounce and save need the card |
| P1 IS EMPTY | you held LOAD on a slot nothing was saved to |
| CANCELLED | a LOAD / SAVE / style question was up and you pressed something else |
| DEMO: NO LOOP | the demo song loaded but the loop could not; tell me |
