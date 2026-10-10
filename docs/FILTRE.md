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
2. Tap **TD** twice to focus **deck A**. **PG** to SAMPLE, pick a loop, **AUX** loads it.
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
- **Pump** is a gain envelope retriggered by lane 1, not a compressor. PUMP 0
  is bit-identical to no envelope. With **GHOST** on, lane-1 steps still pump
  while the kick is muted, which is the filtered-intro sound.
- **Swing** delays the odd 16ths of drums and synths only. Samples keep their
  own pocket.

## Keys

Everything Microgroove binds is unchanged. Additions:

| Key | Short | Hold 0.5 s |
|---|---|---|
| `e` / `u` | cutoff down / up (hold to sweep) | |
| `tab` / `p` | resonance down / up | |
| **TD** (`3`) | drums → deck A → deck B | mute whichever it is on |
| **SLD** (`,`) | slide at cursor | write bass in KEY |
| **AUX** (`.`) | SAMPLE: preview / load loop | mic record to the lane or deck |
| **enter** (`ok`) | | while held, the lid shows the key map for the current page and track |
| **SONG** (`n`) | song mode | resample: a pad (1.9 s), or one bar-exact pattern onto the focused deck |

Deck focused: slices 1–8 on `fn sft a s d f g h`, 9–16 on `q w r t y i o [`.
Hold **ACC** (`m`) for an accented hit.

SAMPLE page with a deck focused lists `/groovebox/loops`: **AUX** loads into the
focused deck, **P1** / **P2** load into deck A / B.

SONG page: **P1–P8** place a pattern, `e`/`u` set the slot's filter target
(below zero = none), `tab`/`p` set repeats 1–16, `fn sft a s d f g` toggle the
slot's mutes for T1 T2 T3 DRUMS A B KICK, hold **AUX** = bounce.

HELP page: **P1–P4** recall a style (FILTRE, NU-DISCO, HOMEWORK, BANGER). A
style never unloads audio; if you have edited the drums yourself it wants a hold.
**AUX** toggles the lid.

SOUND page with a deck focused lists the deck's rows (MODE, SLICES, PITCH,
LEVEL, FIT), then the shared rows in amber (BLEND, CUTOFF, RESO, TYPE, SLOPE,
PUMP, RELEASE, BASS PUMP, GHOST, SWING, KEY, BASSMODE, STYLE, LENGTH), then
SLICE POS, IN, OUT, START, QUALITY, BPM<LOOP.

## Samples and memory

The Cardputer-ADV has **no PSRAM**. One arena, sized at boot from the free heap
(roughly 150–220 KB), holds deck A, deck B and the drum one-shots. The SAMPLE
page and the lid always show `A / B / OS / free`.

- Put **WAV sections** in `/groovebox/loops`: 8/16/24-bit PCM or 32-bit float,
  mono or stereo, any sample rate. They are summed to mono, resampled to
  22.05 kHz, DC-removed and peak-normalised on load. MP3 is not supported.
- A sample is stored 16-bit if it fits, otherwise as 8-bit µ-law (`LO-FI`,
  half the memory, about the fidelity of an SP-1200). 220 KB is about
  5 s at 16-bit or 10 s lo-fi, shared between both decks.
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

Saves are GBX **v3**. Microgroove v1 and v2 projects load and keep their own
feel (straight 16ths, no decks); the next save writes v3. Deck audio is
referenced by file name, never embedded.

```
/groovebox/samples/      drum one-shots
/groovebox/loops/        deck material
/groovebox/projects/     P1.gbx – P8.gbx
/groovebox/wavetables/   single-cycle WAVs
/groovebox/bounce/       rendered takes
```

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
