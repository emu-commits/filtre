# FILTRE user guide

Key by key. Key names are what is printed on the Cardputer: `Aa` is shift,
`ok` is enter, `space` is the space bar.

**Tap** means press and release. **Hold** means keep it down about half a
second; a bar fills in the footer and names what the hold will do. Let go
before the bar fills and you get the tap instead.

## The keyboard at a glance

```
 `   1   2   3 | 4   5   6   7   8   9   0   -  | =     del
 T1  T2  T3  TD| P1  P2  P3  P4  P5  P6  P7  P8 | LOAD  SAVE

tab  q   w   e   r   t   y   u   i   o   p   [   ]   \
RES-         CUT-            CUT+            RES+
     └── black keys / chops 9-16 ──┘

 fn  Aa  a   s   d   f   g   h   j   k   l   ;   '   ok
 └──── pads 1-8 / chops 1-8 ────┘   (white keys continue)

ctrl opt alt  z   x   c   v   b   n    m    ,    .    /    space
PAGE BPM- BPM+ CLR ◄   ▼   ▲   ►  SONG ACC  SLD  AUX  REC  PLAY
```

| Key | Tap | Hold |
|---|---|---|
| `space` | play / stop | play from the top |
| `ctrl` | next page | jump to PATTERN page |
| `/` | record on / off | |
| `x` `c` `v` `b` | left, down, up, right | repeats |
| `z` | clear the step under the cursor | clear the whole pattern |
| `m` | (nothing alone) accent while held | |
| `` ` `` `1` `2` | select synth track T1, T2, T3 | mute that track |
| `3` | drums → deck A → deck B → drums | mute whichever it is on |
| `4` … `-` | select pattern 1 … 8 | copy current pattern there |
| `opt` / `alt` | tempo −1 / +1 | octave down / up |
| `e` / `u` | filter cutoff down / up | sweeps while held |
| `tab` / `p` | resonance down / up | sweeps while held |
| `,` | slide on the note under the cursor | write a bass line |
| `.` | (depends on page) | record from the mic |
| `n` | song mode on / off | resample (while playing) |
| `=` | show which project slot | load project |
| `del` | show which project slot | save project |
| `=` + `del` together | load the built-in demo | |

The pages, in the order `ctrl` steps through them:
**PATTERN → SOUND → SAMPLE → SONG → HELP**.

The footer of the screen shows what just happened ("REC ON", "DECK A: 2 BAR").

---

## 0. Hear the demo first

1. Hold `=` and `del` together until the footer says **DEMO: PLAY**.
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

1. Tap `3` until the footer says **DECK A**.
2. Tap `ctrl` until you are on the **SAMPLE** page. It lists `/groovebox/loops`.
3. `c` / `v` move down / up the list.
4. Tap `.` to load the highlighted file into deck A.
   (Or tap `4` to load into deck A and `5` to load into deck B, whichever deck
   is selected.)
5. The footer says **DECK A: 2 BAR** (it guessed the length at the current
   tempo), **DECK A: FIT?** (it could not guess), or **CUT TO 6.2s**.

## 3. Match the tempo

Either set the tempo to the sample, or stretch the sample to the tempo.

- **Tempo from the sample:** `ctrl` to **SOUND** (deck still selected),
  `c` down to the last row **BPM<LOOP**, tap `b`. Footer: **BPM FROM LOOP**.
- **Tempo by hand:** tap `opt` / `alt` for −1 / +1 BPM, on any page.
- **Stretch the sample:** on SOUND go to **FIT** and use `x` / `b` to pick
  1, 2, 4 or 8 bars. This changes the pitch along with the speed.

## 4. Choose how the deck plays: LOOP or CHOP

On the **SOUND** page with the deck selected, the top row is **MODE**. Tap `b`
to switch.

- **LOOP**: the sample plays by itself, locked to the bar, whenever the
  sequencer runs. Nothing to program. Fastest way to a filter-house loop.
- **CHOP**: the sample is cut at its transients and each piece sits on a key.
  Nothing plays until you play or program it.

On the SOUND page, `c` / `v` pick a row and `x` / `b` change it. Hold `m` while
pressing `x` / `b` for fine steps.

## 5. Play the chops and build a loop (CHOP mode)

Select the deck (`3`) and go to the **PATTERN** page (hold `ctrl`).

```
chops  1   2   3   4   5   6   7   8        9  10  11  12  13  14  15  16
keys   fn  Aa  a   s   d   f   g   h        q   w   r   t   y   i   o   [
```

Hold `m` while hitting a key for an accented (louder) hit.

**Record it live**

1. Tap `/` so the footer says **REC ON**.
2. Tap `space`. The drums play; tap the chop keys in time.
3. Hits are snapped to the nearest 16th and loop round straight away.
4. Wrong hit: hold `z` while it plays past, and it is erased at the playhead.
5. Tap `/` to stop recording. The loop keeps playing.

**Or write it step by step**

1. Stop the sequencer (`space`). Tap `/` for **REC ON**.
2. Move the cursor with `x` / `b` to the step you want.
3. Tap a chop key: it is written at the cursor and the cursor moves on a step.
4. `z` clears the step under the cursor.

**Change the chops** (SOUND page, deck selected)

- **SLICES**: AUTO (transients), 4, 8 or 16 equal pieces.
- **SLICE POS**: play a chop, then `x` / `b` nudge where it starts.
- **IN** / **OUT**: trim the start and end of the whole sample.
- **PITCH**: ±12 semitones. **LEVEL**: deck volume.

**One or two bars:** SOUND page, **LENGTH** row, tap `b`. On the PATTERN page
moving the cursor past step 16 shows the second bar.

## 6. The filter

Works on every page except SONG. It filters both decks, not the drums or bass.

| Key | Does |
|---|---|
| `e` | cutoff down. Tap = small step, hold = sweep |
| `u` | cutoff up |
| `tab` | resonance down |
| `p` | resonance up |

The bar in the header shows where the cutoff is.

**Record a sweep into the pattern:** tap `/` (REC ON), tap `space`, then hold
`e` or `u`. The movement is stored on the steps you swept across and repeats
every time the pattern plays. To remove it, select the deck, put the cursor on
a step and tap `z` (this also clears a chop on that step).

**Filter type and slope:** SOUND page, deck selected, rows **TYPE** (LP / HP /
BP) and **SLOPE** (12 / 24 dB).

## 7. Drums

Tap `3` until the footer says **DRUMS**, PATTERN page.

```
lane   1     2     3     4     5       6    7      8
key    fn    Aa    a     s     d       f    g      h
sound  kick  clap  hat   open  shaker  rim  snare  808 kick
```

Record exactly as with chops: `/` then `space` and play, or stopped with REC on
to write at the cursor. Hold `m` for accents. Tapping `z` on the drum track
clears **every** lane on that step.

A FILTRE drum pattern is already on pattern 1. Other ready-made grooves:
`ctrl` to the **HELP** page, then tap `4` FILTRE, `5` NU-DISCO, `6` HOMEWORK,
`7` BANGER. If you have changed the drums yourself it asks you to hold the key.

**Change a drum sound:** SOUND page with drums selected. Tap a pad to pick the
lane, `c` / `v` for the row, `x` / `b` to change it (engine, sound, volume,
tune, decay, choke).

**Use your own one-shots:** put short WAVs in `/groovebox/samples/`. With drums
selected go to the SAMPLE page, highlight a file, tap `.` to hear it, tap
`4` … `-` to put it on lane 1 … 8.

## 8. The pump (sidechain)

Lane 1, the kick, ducks the decks every time it hits. SOUND page, deck selected:

- **PUMP**: depth, 0–100. 0 is off.
- **RELEASE**: how long it takes to come back.
- **BASS PUMP**: how much the bass ducks as well.
- **GHOST**: when on, the kick steps still pump even while the kick is muted.
  That is the filtered-intro sound: no kick, but the loop is already breathing.

## 9. Bass

**Have it written for you:** select a deck, SOUND page, set **KEY** (the key of
your sample) and **BASSMODE**. Go back to PATTERN and hold `,`. A bass line in
that key is written onto T1 of the current pattern.

**Play it yourself:** tap `` ` `` to select T1.

```
white  fn  Aa  a   s   d   f   g   h   j   k   l   ;   '   ok
note   C   D   E   F   G   A   B   C   D   E   F   G   A   B

black  q   w   r   t   y   i   o   [   ]   \
note   C#  D#  F#  G#  A#  C#  D#  F#  G#  A#
```

Hold `opt` / `alt` for octave down / up. Record with `/` exactly as above.
Tap `,` on a note to make it slide into the next one. `1` and `2` select the
other two synth tracks (T2 is a chord stab).

**Synth sound:** SOUND page with T1, T2 or T3 selected; `c` / `v` rows,
`x` / `b` change, any piano key auditions it.

## 10. Make variations

Hold `5` to copy the current pattern to pattern 2 and switch to it. Change
something (drop the kick, add hats, a different chop order). Repeat with `6`,
`7` and so on. Tap `4` … `-` to switch between patterns; while playing the
change happens on the next bar line.

Mute while you work: hold `` ` `` / `1` / `2` for a synth track, hold `3` for
the drums or the selected deck.

## 11. Arrange the song

`ctrl` to the **SONG** page. Each slot is one pattern, played a number of
times, with its own mutes and a filter target.

| Key | Does |
|---|---|
| `x` `b` | move one slot |
| `c` `v` | move one row (16 slots) |
| `4` … `-` | put pattern 1 … 8 in this slot and move on |
| `p` / `tab` | repeats +1 / −1 (1 to 16) |
| `u` / `e` | filter target up / down. The cutoff glides to this value across the slot. Below zero = no target |
| `fn` `Aa` `a` `s` `d` `f` `g` | mute T1, T2, T3, drums, deck A, deck B, kick only |
| `z` | empty this slot |
| `.` | make this slot the loop-back point |

The song ends at the first empty slot and jumps back to the loop-back point.

Tap `n` for **SONG MODE**, then hold `space` to play from the top. Tap `n`
again to go back to looping one pattern.

A classic shape, using one pattern and its copies:

| Slot | Pattern | Repeats | Mutes | Filter target |
|---|---|---|---|---|
| 1 | P1 | 8 | kick (`g`), bass (`fn`) | low (about 50) |
| 2 | P1 | 8 | bass | rising (about 140) |
| 3 | P2 | 16 | none | open (254) |
| 4 | P1 | 8 | kick | low again |

## 12. Save and load

Projects live in eight slots, P1–P8.

- **Pick the slot:** on the SONG page hold `opt` (previous) or `alt` (next).
- **Save:** hold `del`. Footer: **SAVED**.
- **Load:** hold `=`. Footer: **LOADED**.
- Tapping `=` or `del` just tells you which slot is current.

Samples are saved by file name, so keep the WAVs on the card.

## 13. Bounce to the card

On the **SONG** page hold `.`. A progress bar runs and the footer says
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

**Record from the microphone.** PATTERN or SOUND page. Select a drum lane or a
deck, hold `.`, make the sound, let go. It lands on that lane or deck. The
sequencer stops while recording.

**Resample.** While playing, hold `n`.
- Drums selected: about two seconds of the mix are captured; then tap a pad
  (`fn` … `h`) to choose the lane it goes on.
- Deck selected: exactly one pass of the pattern is captured from the next bar
  line and loaded onto that deck. This is how you filter a loop, print it, and
  filter it again.

**Swing.** SOUND page, deck selected, **SWING** (50 = straight, 62 is the
FILTRE default). It moves drums and synths only.

**Blend.** SOUND page, **BLEND** crossfades deck A against deck B.

**Long file, wrong part.** SOUND page, **START** moves where loading begins, in
seconds. **QUALITY** forces 16-bit or lo-fi. Both apply the next time you load.

**Lid.** On the HELP page tap `.` to turn the lid display off or on.

**Memory check.** HELP page, `c` to the bottom: it shows free heap and the
sample arena size.

## When something does not work

| Footer says | Meaning |
|---|---|
| PG > SAMPLE: LOAD A LOOP | you hit a chop key but the deck is empty |
| LOAD FAILED | not a WAV, or no room left. Unload by loading something shorter |
| CUT TO 6.2s | the file was longer than free memory; you have the first 6.2 s from START |
| DECK A: FIT? | length is not a whole number of bars at this tempo; use BPM<LOOP or FIT |
| NO ROOM / BUSY | resample or mic has no memory left |
| PLACE A PATTERN FIRST | SONG page: the slot is empty, put a pattern in it first |
| NO SD CARD | bounce and save need the card |
| DEMO: NO LOOP | the demo song loaded but the loop could not; tell me |
