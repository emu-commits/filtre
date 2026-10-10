// ============================================================
// FILTRE - deck.h
// Two sample decks (A/B) sharing the arena. Each deck is either
//   CHOP - sliced at its transients onto 16 keys, sequenced per step
//   LOOP - free-running, re-synced to the bar
// Both decks are blended, then run through one resonant filter and
// the kick-driven pump. This is the French-touch signal path.
// ============================================================
#pragma once
#include "config.h"
#include "sampler.h"

enum DeckMode   : uint8_t { DECK_CHOP = 0, DECK_LOOP, DECK_MODE_COUNT };
enum SliceMode  : uint8_t { SLICE_AUTO = 0, SLICE_4, SLICE_8, SLICE_16, SLICE_COUNT };
enum FilterType : uint8_t { FLT_LP = 0, FLT_HP, FLT_BP, FLT_COUNT };

#define OV_W            320     // overview columns (lid width)
#define DECK_FADE_IN    (1.0f / 24.0f)
#define DECK_FADE_OUT   (1.0f / 64.0f)

struct ChopVoice {
    const uint8_t* data;
    float   pos, inc;          // inc < 0 = reversed
    float   start, end;        // frames; playback stays inside [start, end)
    float   gain, vel;
    uint8_t fmt;
    bool    active, fadeOut, loop;
};

struct Deck {
    // ----- settings (written by core 1) -----
    int8_t   slot;                       // arena slot, -1 = empty
    char     name[SAMPLE_NAME_LEN];
    uint8_t  mode;                       // DeckMode
    uint8_t  sliceMode;                  // SliceMode
    uint8_t  nSlices;
    uint8_t  fitBars;                    // LOOP: 0 = native speed, else 1/2/4/8 bars
    uint8_t  qual;                       // LoadQual used for the next load
    uint8_t  startTenths;                // load offset into a long file, 0.1 s units
    bool     mute;
    bool     cut;                        // file was longer than memory
    float    pitch;                      // semitones, -12..+12
    float    level;                      // 0..1
    uint32_t in, out;                    // region, frames
    uint32_t slice[MAX_SLICES + 1];      // slice starts; slice[nSlices] = out
    uint8_t  lastSlice;                  // last pad hit (edit target)

    // ----- playback (audio thread) -----
    ChopVoice v[2];                      // two voices so a retrigger crossfades
    uint8_t   cur;
    volatile uint32_t playFrame;         // for the playhead display

    // ----- waveform overview (UI) -----
    int8_t   ovMin[OV_W], ovMax[OV_W];
};

struct FxParams {
    uint8_t  blend;      // 0 = A only, 64 = both, 127 = B only
    uint8_t  cutoff;     // 0..255 base cutoff (what the e/u keys move)
    uint8_t  reso;       // 0..127
    uint8_t  type;       // FilterType
    uint8_t  poles;      // 0 = 12 dB/oct, 1 = 24 dB/oct
    uint8_t  pump;       // 0..100 depth
    uint16_t release;    // 40..400 ms
    uint8_t  bassPump;   // 0..100, percent of pump applied to T1
    uint8_t  ghost;      // 1 = lane-1 steps pump even while the kick is muted
    uint8_t  swing;      // 50..72 percent
    uint8_t  key;        // 0..11 root, C = 0
    uint8_t  bassMode;   // BassMode (styles.h)
    uint8_t  style;      // last style recalled
};

extern Deck     g_decks[NUM_DECKS];
extern FxParams g_fx;
extern volatile float g_cutNow;          // effective cutoff 0..255 (after locks/targets)
extern volatile float g_pumpNow;         // current duck amount 0..1 (display)

void fxDefaults();
void deckInit();

// ----- core 1: loading / editing -----
bool deckLoad(uint8_t d, const char* filename);   // from DIR_LOOPS
bool deckAdopt(uint8_t d, int slot);               // take over an arena sample (mic/resample/factory)
void deckUnload(uint8_t d);
void deckReslice(uint8_t d);
void deckNudgeSlice(uint8_t d, uint8_t slice, int32_t frames);
void deckSetRegion(uint8_t d, uint32_t in, uint32_t out);
uint8_t deckGuessBars(uint8_t d, uint16_t bpm, bool force);   // 0 = no clean fit
float   deckBpmForBars(uint8_t d, uint8_t bars);
static inline bool deckLoaded(uint8_t d) { return d < NUM_DECKS && samplerValid(g_decks[d].slot); }

// ----- audio thread -----
void  deckNoteOn(uint8_t d, uint8_t slice, bool accent, bool reverse);
void  deckLoopSync(uint8_t d, float barFrames);   // (re)start the loop region on the bar
void  deckStop(uint8_t d);                         // short fade
void  deckStopAll();                               // hard stop (arena is about to move)
float deckBusRender();                             // A+B blended and filtered, before the pump
void  pumpTrigger();
float pumpRender();                                // advances the envelope; returns duck 0..1
void  fxReset();                                   // clear filter + pump state
float cutoffToHz(float c);                         // 0..255 -> Hz
