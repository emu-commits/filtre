// ============================================================
// FILTRE - sampler.h
// One shared sample arena (deck A, deck B, drum one-shots, mic and
// resample captures) + WAV decoding + the one-shot playback voice.
// Samples are stored either as 16-bit PCM or as 8-bit mu-law, which
// halves the memory for long deck material.
// ============================================================
#pragma once
#include "config.h"
#include <FS.h>

enum SampleFmt   : uint8_t { FMT_PCM16 = 0, FMT_ULAW = 1 };
enum SampleOwner : uint8_t { OWN_ONESHOT = 0, OWN_DECK_A, OWN_DECK_B, OWN_COUNT };
enum LoadQual    : uint8_t { QUAL_AUTO = 0, QUAL_16BIT, QUAL_LOFI, QUAL_COUNT };

struct SampleInfo {
    char     name[SAMPLE_NAME_LEN];  // filename (basename)
    uint32_t offset;                 // start in arena, BYTES (always even)
    uint32_t length;                 // frames
    uint32_t rate;                   // native sample rate
    uint8_t  fmt;                    // SampleFmt
    uint8_t  owner;                  // SampleOwner (metering only)
    bool     used;
    const uint8_t* ext;              // set: data lives in flash, not in the arena (0 bytes of it)
};

extern uint8_t*   g_arena;           // allocated once at boot
extern uint32_t   g_arenaUsed;       // bytes
extern uint32_t   g_arenaCap;        // bytes
extern SampleInfo g_samples[MAX_SAMPLES];
extern int16_t    g_ulawTable[256];

static inline float smpRead(const uint8_t* d, uint8_t fmt, uint32_t i) {
    return fmt == FMT_PCM16 ? (float)((const int16_t*)d)[i] : (float)g_ulawTable[d[i]];
}
static inline bool samplerValid(int slot) {
    return slot >= 0 && slot < MAX_SAMPLES && g_samples[slot].used;
}
static inline const uint8_t* smpData(const SampleInfo& s) {
    return s.ext ? s.ext : g_arena + s.offset;
}

bool samplerInit();                       // allocate arena (no SD access)
void samplerEnsureDirs();                 // create /groovebox tree on the card
void samplerClearAll();                   // wipe arena + registry
int  samplerLoad(const char* filename);   // one-shot from DIR_SAMPLES; slot or -1
int  samplerFindByName(const char* filename);
void samplerFree(int slot);               // release + compact (parks the engine)

// Deck material: decoded to mono at the engine rate, DC removed, peak
// normalised. If the file is longer than the free arena it is loaded from
// startSec and cut to what fits; *cut reports that. Returns slot or -1.
int  samplerLoadDeck(const char* path, const char* name, uint8_t owner,
                     uint8_t qual, float startSec, bool* cut);

uint32_t samplerBytes(int slot);
uint32_t samplerOwnerBytes(uint8_t owner);
static inline uint32_t samplerFreeBytes() { return g_arenaCap - g_arenaUsed; }

// Register ready-made sample data that stays where it is (the factory loop
// in flash): costs no arena, so it works however small the arena came up.
int  samplerAddExternal(const char* name, const uint8_t* data, uint32_t frames,
                        uint32_t rate, uint8_t fmt, uint8_t owner);
// Copy ready-made sample data into the arena.
int  samplerAddRaw(const char* name, const uint8_t* data, uint32_t frames,
                   uint32_t rate, uint8_t fmt, uint8_t owner);

// Recording straight into the free tail of the arena (mic / resample).
// While g_tailHeld is set a capture lives there unregistered: loads are
// refused, and a free (which would move the tail) cancels the capture.
extern volatile bool g_tailHeld;
int16_t* samplerScratch(uint32_t* maxFrames);
int      samplerCommitScratch(const char* name, uint32_t start, uint32_t frames,
                              uint32_t rate, uint8_t owner);

// ---------- WAV reading ----------
// PCM 8/16/24-bit and 32-bit float, mono or stereo (summed), any rate.
struct WavReader {
    File*    f;
    uint16_t channels, bits;
    bool     isFloat;
    uint32_t rate, frames, dataPos, bytesPerFrame, pos;

    bool     open(File& file);
    void     seekFrame(uint32_t frame);
    uint32_t read(int16_t* dst, uint32_t maxFrames);   // mono frames
};

// Decode an open WAV file to mono int16. Returns false on unsupported format.
// Used by both the sampler and the wavetable loader.
bool wavDecodeToMono16(File& f, int16_t* dst, uint32_t maxFrames,
                       uint32_t& outFrames, uint32_t& outRate);

// ---------- Playback voice ----------
struct SampleVoice {
    const uint8_t* data;
    uint32_t len;
    float    pos;
    float    inc;      // rate/engine-rate * pitch
    float    gain;
    uint8_t  fmt;
    bool     active;

    void init() { data = nullptr; len = 0; pos = 0; inc = 1; gain = 1; fmt = FMT_PCM16; active = false; }

    void trigger(int slot, float pitch, float volume) {
        if (!samplerValid(slot)) { active = false; return; }
        data   = smpData(g_samples[slot]);
        fmt    = g_samples[slot].fmt;
        len    = g_samples[slot].length;
        pos    = 0;
        inc    = ((float)g_samples[slot].rate * INV_SAMPLE_RATE) * pitch;
        gain   = volume;
        active = true;
    }

    void stop() { active = false; }

    inline float render() {
        if (!active) return 0.0f;
        uint32_t i = (uint32_t)pos;
        if (i + 1 >= len) { active = false; return 0.0f; }
        float frac = pos - (float)i;
        float a = smpRead(data, fmt, i), b = smpRead(data, fmt, i + 1);
        pos += inc;
        return (a + (b - a) * frac) * (1.0f / 32768.0f) * gain;
    }
};

// One-shot preview player (SAMPLE page), mixed by the audio engine.
extern SampleVoice g_previewVoice;
