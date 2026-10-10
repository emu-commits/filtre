// ============================================================
// CardputerGroovebox - config.h
// Global constants, pins, shared enums
// Target: M5Stack Cardputer-ADV (ESP32-S3FN8)
// ============================================================
#pragma once
#include <stdint.h>

// ---------- Audio ----------
#define SAMPLE_RATE      22050
#define AUDIO_BUF_LEN    256
#define INV_SAMPLE_RATE  (1.0f / (float)SAMPLE_RATE)
#define TWO_PI_F         6.28318530718f

// ---------- Tracks / sequencer ----------
#define NUM_SYNTHS       3
#define NUM_DECKS        2     // sample decks A/B (CHOP or LOOP)
#define TRK_DRUMS        NUM_SYNTHS
#define TRK_DECK_A       (NUM_SYNTHS + 1)
#define TRK_DECK_B       (NUM_SYNTHS + 2)
#define NUM_TRACKS       (NUM_SYNTHS + 3)
#define MAX_SLICES       16
#define MAX_POLY         3     // voices per synth track (VOICES param 1..3)
#define NUM_DRUM_LANES   8
#define NUM_STEPS        32    // max steps per pattern (2 bars)
#define STEPS_PER_BAR    16
#define NUM_PATTERNS     8
#define SONG_LENGTH      64
#define SONG_EMPTY       0xFF

// ---------- Display ----------
#define SCREEN_W         240
#define SCREEN_H         135

// ---------- SD card (Cardputer-ADV pinout, verified against M5 docs) ----------
#define SD_SPI_CS_PIN    12
#define SD_SPI_MOSI_PIN  14
#define SD_SPI_CLK_PIN   40
#define SD_SPI_MISO_PIN  39

#define DIR_ROOT         "/groovebox"
#define DIR_SAMPLES      "/groovebox/samples"
#define DIR_WAVETABLES   "/groovebox/wavetables"
#define DIR_PROJECTS     "/groovebox/projects"
#define DIR_LOOPS        "/groovebox/loops"
#define DIR_LOOPS_LEN    16
#define DIR_BOUNCE       "/groovebox/bounce"

// ---------- Sample memory ----------
// One shared arena for everything that plays from RAM (deck A, deck B,
// drum one-shots, mic/resample captures). Sized at boot from the free heap.
#define ARENA_MAX_BYTES   (224 * 1024)
#define ARENA_MIN_BYTES   (48 * 1024)
#define ARENA_HEAP_RESERVE (56 * 1024)   // left for SD, lid, tasks, file buffers
#define MAX_SAMPLES       16
#define SAMPLE_NAME_LEN   32            // one-shot names (fixed by the GBX v1/v2 layout)
#define LOOP_NAME_LEN     64            // deck files: real-world sample names are long

// ---------- Wavetables ----------
#define WT_SIZE          256
#define NUM_BUILTIN_WT   8
#define MAX_USER_WT      8
#define NUM_WT_TOTAL     (NUM_BUILTIN_WT + MAX_USER_WT)
#define WT_NAME_LEN      12

// ---------- Shared enums ----------
enum OscMode : uint8_t { OSC_SAW = 0, OSC_SQR, OSC_TRI, OSC_SIN, OSC_WT, OSC_COUNT };

enum DrumEngine : uint8_t { ENG_808 = 0, ENG_909, ENG_SMPL, ENG_COUNT };

// type index within a synth drum engine
enum DrumType : uint8_t { DT_KICK = 0, DT_SNARE, DT_HAT_C, DT_HAT_O_OR_CLAP,
                          DT_CLAP9, DT_RIM, DT_COUNT };
#define DT_COUNT_808 4
// 808 set: KICK SNARE CHAT CLAP  |  909 set: KICK SNARE CHAT OHAT CLAP RIM

enum Page : uint8_t { PAGE_PATTERN = 0, PAGE_SOUND, PAGE_SAMPLE, PAGE_SONG, PAGE_HELP, PAGE_COUNT };

// note values 1..12 = C..B, 0 = empty
#define NOTE_EMPTY 0

// ---------- Colors ----------
// The internal UI draws into a 16-colour palette sprite (16 KB instead of
// 65 KB), so COL_* are palette indices. RGB565 values live in ui.cpp.
#define COL_BG       0
#define COL_GRID     1
#define COL_TEXT     2
#define COL_DIM      3
#define COL_SYNTH1   4    // cyan
#define COL_SYNTH2   5    // green
#define COL_SYNTH3   6    // yellow
#define COL_DRUMS    7    // magenta
#define COL_ACCENT   8    // orange
#define COL_REC      9    // red
#define COL_PLAYBG   10   // playhead cell background
#define COL_ACCBG    11   // accented cell background
#define COL_DECKA    12   // cream
#define COL_DECKB    13   // teal
#define COL_AMBER    14   // filter / cutoff locks
#define COL_HOT      15   // playhead red-orange
#define COL_PLAYHEAD COL_TEXT
#define COL_CURSOR   COL_TEXT
