# FILTRE

A pocket French-touch sampler for the **M5Stack Cardputer-ADV** with the
**TERM512** lid (ST7789 2.8″, 320×240). A fork of
[Microgroove](https://github.com/matoslav/MicroGroove) by lebiro.studio (MIT).

You load a few seconds of a record, the keys play its transients, you tap a
new loop out of them, and a resonant filter and a pumping 909 kick do the rest.
The whole arrangement is built on the device and bounced to the SD card as a
mix plus stems; vocals go on afterwards in a DAW.

## Six steps

1. **PLAY.** FILTRE drums are already loaded (909, 122 BPM, swing 62).
2. Hold `3` to focus **deck A**. **PG** to SAMPLE, pick a loop, **AUX** loads it.
3. The keys play its chops. **REC** + **PLAY** and tap them in: that is your loop.
4. **e / u** sweep the filter, on any page. With REC on, the sweep is kept in the pattern.
5. Hold **SLD** to write a bass line in KEY. Play the piano on T1 to change it.
6. **SONG**: place patterns, **e / u** set each slot's filter. Hold **AUX** on SONG to bounce to SD.

Key-by-key workflows are in [USER-GUIDE.md](USER-GUIDE.md).

No card? Hold **LOAD + SAVE** for the built-in demo: an original CC0 loop (played
straight from flash, so it costs no sample memory) and a
four-slot song that opens the filter.

## The signal path

```
deck A ─┐
        ├─ BLEND ─ filter (LP/HP/BP, 12 or 24 dB) ─ pump ─┐
deck B ─┘                                                 │
T1 303 bass ─────────────────────── pump (BASS PUMP %) ───┼─ soft clip ─ out
T2, T3 synths ────────────────────────────────────────────┤
8 drum lanes (lane 1 triggers the pump) ──────────────────┘
```

- **Deck** = one sample in memory. **CHOP** mode slices it at its transients
  (or into 4/8/16) onto 16 keys and each pattern stores which slice plays on
  which step. **LOOP** mode plays the region bar-locked; **FIT** stretches it
  by resampling to exactly 1/2/4/8 bars, or **BPM<LOOP** sets the tempo from it.
- **Filter** is one shared filter on both decks. The header bar on every page
  shows where the cutoff really is, including recorded motion.
- **PHASER** is a fourth filter TYPE: 4 or 8 first-order all-pass stages
  (SLOPE) mixed with the dry bus. CUTOFF places the sweep (120 Hz to 5.4 kHz;
  keys, recorded locks and song targets all move it), a triangle LFO of one
  cycle per 4 bars rocks it ±0.9 octave, RESO is the feedback.
- **Wavetables**: 10 built in. STRINGS and SUPERSAW are read by each voice as
  3 / 5 detuned copies (`g_wtUnison`), since one cycle cannot hold a detune.
- **Pump** is a gain envelope retriggered by lane 1, not a compressor. PUMP 0
  is bit-identical to no envelope. With **GHOST** on, lane-1 steps still pump
  while the kick is muted, which is the filtered-intro sound.
- **Swing** delays the odd 16ths of drums and synths only. Samples keep their
  own pocket.

## Keys

The single source is `keymap.h`. Where FILTRE differs from Microgroove:

| Key | Short | Hold 0.5 s |
|---|---|---|
| `e` / `u` | cutoff down / up (hold to sweep) | |
| `tab` / `p` | resonance down / up | |
| top row `` ` `` … `=` | mute that grid row: T1 T2 T3, deck A B, drum lanes 1–8 | select that row |
| `[` / `]` | pattern − / + (queued to the end of the playing pattern; press again to move the queue) | |
| **CPY** (`'`) | tag this pattern as the copy source | asks "PASTE: You sure?"; `'` again overwrites this pattern with the tagged one |
| **CLR** (`c`) | clear cell / song slot | asks "CLEAR: Are you sure?"; `c` again clears the pattern |
| `,` `.` `;` `/` | arrows left, down, up, right, as printed on the keys | |
| **SLD** (`x`) | slide at cursor | write bass in KEY |
| **AUX** (`b`) | SAMPLE: preview / load loop | mic record to the lane or deck |
| **REC** (`v`) | live record on / off | |
| `opt` / `alt` | octave down / up; SONG page: project slot | (no hold function) |
| **LOAD** (`\`) / **SAVE** (`del`) | which slot | asks "Are you sure?"; the same key again does it |
| **enter** (`ok`) | | while held, the lid shows the key map for the current page and track |
| **SONG** (`n`) | song mode | resample: a pad (1.9 s), or one bar-exact pattern onto the focused deck |

Tempo has no key: it is the BPM row on SETTINGS. The piano stops at `l` / `o`:
`;` is the up arrow and `[` is pattern −. `m` steps to the next bar of the pattern on the PATTERN page.

Mutes are a 13-bit mask, one bit per grid row, and there are two sets: one in
each pattern (`Pattern.mute`) and one in each song slot (`SongSlot.mute`).
Song mode picks the set that is in force and that the top row edits
(`seqMuteMask` / `seqToggleMute`): off = the playing pattern's, on = the
playing slot's (stopped: the pattern on screen / the slot under the SONG
cursor). The slot's mask replaces the pattern's, it is not added to it, and
neither is copied to the other except when a slot is first created.
The SONG page is the exception to "song mode picks": there the top row and the
mute display use `seqEditMask` / `seqToggleMute`, which always take the slot
(playing slot in song mode, else the one under the cursor), while what is heard
(`seqMuteMask`) still follows `g_songMode`. `n`, `z`, `x`, `v` do nothing on
SONG; `'` there tags a slot (`g_songTag`) and a hold pastes the whole
`SongSlot` over the cursor slot.

There is no HELP page. Holding `ok` draws the keyboard on the lid from
`keymap.h`'s own lookups, so the on-device help cannot go stale.

Deck focused: slices 1–8 on `fn sft a s d f g h`, 9–15 on `q w r t y i o`, 16 on `j`.
Hold **ACC** (`z`) for an accented hit.

SAMPLE page with a deck focused lists `/groovebox/loops`: **AUX** loads into the
focused deck. With a drum lane focused it lists `/groovebox/samples`: **AUX**
previews, pads 1–8 put the sample on that lane.

SONG page: `[` / `]` set the slot's pattern (an empty slot starts as the slot before it), `e`/`u` set the slot's filter target
(below zero = none), `tab`/`p` set repeats 1–16, the top row mutes the slot
when song mode is on, `m` lists `/groovebox/songs` (hold `\` loads the
highlighted arrangement), hold **AUX** = bounce.

SETTINGS page lists the focused track's own rows, then one shared block in
amber that is the same for every track: BPM, SWING, KEY, BASSMODE, STYLE,
LENGTH, BLEND, CUTOFF, RESO, TYPE, SLOPE, PUMP, RELEASE, BASS PUMP, GHOST, LID.
A deck's own rows are MODE, SLICES, SLICE POS, PITCH, LEVEL, FIT, IN, OUT,
START, QUALITY, BPM<LOOP. STYLE never unloads audio; if you have edited the
drums yourself it asks first and the same arrow again confirms.

## Two screens

With the lid open the lid is the editor and the internal screen is the lens:

| Page | Lid (320x240) | Internal (240x135) |
|---|---|---|
| PATTERN | the grid: T1-T3, deck A/B chops, cutoff locks, 8 drum lanes | both deck waveforms with slices, IN/OUT and playheads |
| SETTINGS | the scrolling list | the selected row in large type, plus the waveform, the filter curve or the scope |
| SAMPLE | the file list and the arena bar | the same list, with what is loaded where |
| SONG | all 64 slots | the same, with the slot under the cursor in words |

Messages, questions and the hold bar are on the bottom line of both. With the
lid off or absent (`-nolid` build, or the LID row) the internal screen draws
the grid and the list itself.

The lid has no framebuffer and is never cleared while a page is up: each grid
cell, list row and song slot keeps a hash of what it last drew and is redrawn
only when that changes. (Repainting the whole panel on every pad hit was a
visible blink and kept the main loop away from the keyboard for the length of
the repaint.)

## Samples and memory

The Cardputer-ADV has **no PSRAM**. One arena, sized at boot from the free heap
(211 KB measured on a Cardputer-ADV with the lid, 256 KB at most; the boot
screen shows the real figure), holds deck A, deck B and the drum one-shots. The SAMPLE
page and the lid always show `A / B / OS / free`.

**Pattern memory** is a second, fixed pool: 96 bars of 226 bytes (21.7 KB)
shared by all 64 patterns. A pattern is 1 to 4 bars long and only names its
bars; a bar is taken from the pool when the first note, hit, chop or cutoff
lock is written into it and goes back when it is emptied, the pattern is
cleared or shortened. An empty pattern costs 8 bytes. A synth step is 3 bytes
(three notes of 4 + 3 bits, accent, slide). The lid's bottom line shows
`BARS n` left; a write with none left is refused with `PATTERN MEMORY FULL`.
The 32 fixed 2-bar patterns this replaces took 30 KB.

Where the rest of the RAM went: the wavetable loader's 8 KB decode buffer and
the eight user wavetable slots (4 KB) are now heap, taken only while or when
needed; a project save needs 1 KB instead of 7.5 KB; the heap left to the rest
of the firmware when the arena is sized is 48 KB instead of 56 KB.

- Put **WAV sections** in `/groovebox/loops`: 8/16/24-bit PCM or 32-bit float,
  mono or stereo, any sample rate. They are summed to mono, resampled to
  22.05 kHz, DC-removed and peak-normalised on load. MP3 is not supported.
- A sample is stored 16-bit if it fits, otherwise as 8-bit µ-law (`LO-FI`,
  half the memory, about the fidelity of an SP-1200). 211 KB is about
  4.8 s at 16-bit or 9.6 s lo-fi, shared between both decks.
- A file longer than the free memory is **cut**, and says so (`CUT TO 6.2s`).
  START moves the window into the file for the next load.
- File names up to 63 characters.

## Song and bounce

A song slot is a pattern, a repeat count, a mute mask and an optional filter
target. With a target set the cutoff glides from wherever it was to the target
across the whole slot (all repeats), and the pattern's own filter locks are
ignored for that slot. The chain ends at the first empty slot and returns to
the loop start.

**Bounce** renders the chain once, offline, to `/groovebox/bounce/`:

```
T01_122bpm_mix.wav   T01_122bpm_drums.wav   T01_122bpm_bass.wav
T01_122bpm_synth.wav T01_122bpm_decks.wav
```

All files start on bar 1 and are the same length (song + 1.5 s tail), 22.05 kHz
mono 16-bit, so they drop into a DAW at the tempo in the name. Empty stems are
not written. With no song chain, it bounces four passes of the current pattern.

## Project files

Saves are GBX **v6**: the settings block of v5, then each of the 64 patterns
as its length, mutes and only the bars that hold something (an empty project
is about 2 KB; v5 was always 28 KB). v3, v4 and v5 still load (v3/v4 slots get
the pattern's mutes plus their old coarse slot mutes, so the song sounds as it
did), as do Microgroove v1 and v2 projects, which keep their own feel
(straight 16ths, no decks). The next save writes v6, which firmware older
than this cannot open. Deck audio is referenced by file name, never embedded.

```
/groovebox/samples/      drum one-shots
/groovebox/loops/        deck material
/groovebox/projects/     P1.gbx – P8.gbx
/groovebox/wavetables/   single-cycle WAVs
/groovebox/bounce/       rendered takes
/groovebox/songs/        arrangements (.arr) from the arranger
```

## Arrangements

`docs/arranger` is a static page (published at
<https://emu-commits.github.io/filtre/arranger/>) that cuts a reference track
into measures and lets you mark each with a pattern number and the muted
rows. It saves a text file; `storageLoadArrangement` reads four kinds of
line from it and skips the rest, which are the tool's own:

```
FILTRE-ARR 1
BPM 124                  tempo, rounded (the tool keeps the exact one in TEMPO)
FILTER LP                the filter's type: LP, HP, BP or PHASER (left out = as it is)
P 1 2 main loop          pattern 1 is 2 bars long, 1..4 (the name is for the tool)
S 1 4 1FE0 254           next slot: pattern 1, 4 times, mutes in hex (bit 0 = T1 .. bit 12 = D8),
                         then the slot's filter target 0..254 (left out = no target)
```

Repeats are worked out when the file is saved: a run of measures with the
same pattern and mutes becomes one slot, its length divided by the pattern's
length (runs longer than 16 repeats are split). Loading replaces the chain,
sets those pattern lengths, the tempo and the filter type if the file names
one, and leaves notes, sounds and pattern mutes alone. Tests: `node tools/arranger-test/arr.test.mjs`,
`beat.test.mjs`, and `ui.test.mjs <track>` (headless Chromium).

## Build and flash

```
pio run -e cardputer-adv                 # with the lid
pio run -e cardputer-adv-nolid           # internal screen only
pio run -e cardputer-adv -t upload
```

Copy `factory-sd/groovebox` to the root of a FAT32 card.

### The lid

Pins: SCLK 40, MOSI 14, MISO 39 (SD only), DC 6, CS 5, RST 3, no backlight pin.
It shares the SD card's SPI host, so the lid is started **before** the card is
mounted (starting it re-begins that bus). The internal LCD is on the other
host and is never released.

First boot shows red / green / blue bars on the lid. If they are wrong:

| Symptom | Build flag |
|---|---|
| Image upside down | `-DFILTRE_LID_ROTATION=3` |
| Colours negative | `-DFILTRE_LID_INVERT=1` |
| Bars clean, want faster redraws | `-DFILTRE_LID_HZ=40000000` |

The lid cannot be detected (it has no read line). If something on the header
drives GPIO 6, as the LoRa cap's BUSY pin does, the lid code stays off the pins.

## Tests without the hardware

```
make -C tools/host test       # the real engine, natively: 61 checks
make -C tools/host preview    # both screens of every page as PNG, tools/host/build/shots
```

`tools/make_factory_loop.py` regenerates the factory loop (synthesised, CC0).

## Not verified on hardware yet

Lid colours and rotation, the record-latency trim (`REC_LATENCY_SAMPLES` in
`sequencer.cpp`), real arena size, CPU headroom of the audio task, and the feel
of the filter-key sweep rate.
