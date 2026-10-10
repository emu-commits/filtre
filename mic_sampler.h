// ============================================================
// FILTRE - mic_sampler.h
// Live sampling from the ES8311/MEMS mic + engine resampling.
//   HOLD AUX (.) 0.5s        -> mic records while AUX stays held;
//                               release = commit to the focused
//                               drum lane or deck
//   HOLD SONG (n) 0.5s while -> resample the mix. Drum track: ~1.9 s,
//   playing                     then tap a pad. Deck focused: exactly
//                               one pattern from the next bar line,
//                               straight onto that deck as a loop.
// Captures go straight into the free tail of the sample arena (no
// separate scratch buffer) and are written to SD as WAV so projects
// reload them by name. RAM-only if there is no card.
// ============================================================
#pragma once
#include "config.h"

#define MIC_RATE          16000        // lane captures (stored native, voice retunes)
#define MIC_MAX_SEC       4
#define RSMP_PAD_FRAMES   42000        // ~1.9 s for a pad
#define TRIM_THRESHOLD    600          // |sample| gate for auto-trim
#define TRIM_PREROLL_MS   30

void micSamplerUpdate();               // pump capture; call every loop()

bool micRecStart(uint8_t track, uint8_t lane);   // pauses transport+speaker, starts capture
void micRecStop();                     // trim, commit, resume audio
bool micRecActive();

bool resampleArm(uint8_t track);       // false = no room
bool resamplePending();                // captured, waiting for a destination pad
void resampleCommit(uint8_t lane);

// set by the sequencer on the bar line when a bar-synced capture is armed
extern volatile uint32_t g_rsmpArmFrames;
