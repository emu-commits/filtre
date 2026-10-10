// ============================================================
// FILTRE - mic_sampler.cpp
// ============================================================
#include "mic_sampler.h"
#include "sampler.h"
#include "sequencer.h"
#include "audio_engine.h"
#include "ui.h"
#include <M5Cardputer.h>
#include <SD.h>
#include <string.h>

volatile uint32_t g_rsmpArmFrames = 0;

static bool     s_recActive   = false;
static uint8_t  s_recTrack    = TRK_DRUMS, s_recLane = 0;
static uint32_t s_recRate     = MIC_RATE;
static int16_t* s_dst         = nullptr;
static uint32_t s_dstMax      = 0, s_wr = 0;
static bool     s_rsmpPending = false;
static uint32_t s_rsmpFrames  = 0;
static int8_t   s_rsmpDeck    = -1;     // >= 0: commit to this deck when done
static uint8_t  s_rsmpBars    = 1;
static uint8_t  s_micCount    = 0;      // MIC01.. name counter
static uint8_t  s_rsmpCount   = 0;

// ping-pong chunks queued into M5Unified's mic driver
#define CHUNK 256
static int16_t  s_chunk[2][CHUNK];
static int      s_curChunk = 0;
static float    s_level    = 0.0f;

// ---------- auto trim ----------
static void trimTail(const int16_t* d, uint32_t total, uint32_t rate,
                     uint32_t& start, uint32_t& len) {
    uint32_t pre = (rate * TRIM_PREROLL_MS) / 1000;
    uint32_t a = 0, b = total;
    while (a < total && (uint16_t)abs(d[a]) < TRIM_THRESHOLD) a++;
    while (b > a     && (uint16_t)abs(d[b - 1]) < TRIM_THRESHOLD) b--;
    if (a >= b) { start = 0; len = 0; return; }          // silence
    start = (a > pre) ? a - pre : 0;
    len   = b - start;
}

// ---------- WAV write ----------
static bool writeWav(const char* dir, const char* name, const int16_t* data,
                     uint32_t frames, uint32_t rate) {
    char path[96];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    SD.remove(path);
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    uint32_t dataBytes = frames * 2;
    uint8_t h[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',
                     16,0,0,0, 1,0, 1,0, 0,0,0,0, 0,0,0,0, 2,0, 16,0,
                     'd','a','t','a',0,0,0,0};
    uint32_t riff = 36 + dataBytes, brate = rate * 2;
    memcpy(h + 4,  &riff,  4);
    memcpy(h + 24, &rate,  4);
    memcpy(h + 28, &brate, 4);
    memcpy(h + 40, &dataBytes, 4);
    bool ok = f.write(h, 44) == 44 &&
              f.write((const uint8_t*)data, dataBytes) == dataBytes;
    f.close();
    return ok;
}

// The capture sits in the arena tail. Register it in place (no reload),
// keep a WAV copy on the card, and hand it to the lane or deck.
static bool commit(uint8_t track, uint8_t lane, const char* base, uint8_t& counter,
                   uint32_t start, uint32_t frames, uint32_t rate) {
    if (frames < 64) { g_tailHeld = false; uiStatus("TOO QUIET"); return false; }
    bool toDeck = trackIsDeck(track);
    char name[SAMPLE_NAME_LEN];
    snprintf(name, sizeof(name), "%s%02u.wav", base, (unsigned)(++counter));

    engineLock();
    int slot = samplerCommitScratch(name, start, frames, rate, OWN_ONESHOT);
    engineUnlock();
    if (slot < 0) { uiStatus("POOL FULL"); return false; }
    const int16_t* data = (const int16_t*)(g_arena + g_samples[slot].offset);
    writeWav(toDeck ? DIR_LOOPS : DIR_SAMPLES, name, data, frames, rate);   // no card: RAM only

    if (toDeck) {
        uint8_t d = trackDeck(track);
        if (!deckAdopt(d, slot)) return false;
        g_decks[d].fitBars = deckGuessBars(d, g_bpm, false);
        return true;
    }
    DrumLane& dl = g_drumLanes[lane];
    dl.engine = ENG_SMPL; dl.sampleSlot = (int8_t)slot; dl.smp.init();
    triggerLaneLive(lane);                              // instant audition
    return true;
}

// ---------- mic recording ----------
bool micRecStart(uint8_t track, uint8_t lane) {
    if (s_recActive || g_tailHeld) return false;
    s_recRate = trackIsDeck(track) ? SAMPLE_RATE : MIC_RATE;   // decks play at the engine rate
    s_dst = samplerScratch(&s_dstMax);
    if (s_dstMax > s_recRate * MIC_MAX_SEC) s_dstMax = s_recRate * MIC_MAX_SEC;
    if (!s_dst || s_dstMax < s_recRate / 4) { uiStatus("POOL FULL"); return false; }

    sequencerStop();
    g_tailHeld = true;
    engineLock();                       // audio task idles while the speaker is off
    M5Cardputer.Speaker.end();          // ES8311: avoid duplex contention
    M5Cardputer.Mic.begin();
    s_recTrack = track; s_recLane = lane; s_recActive = true;
    s_wr = 0; s_curChunk = 0; s_level = 0;
    M5Cardputer.Mic.record(s_chunk[0], CHUNK, s_recRate);
    M5Cardputer.Mic.record(s_chunk[1], CHUNK, s_recRate);
    uiStatus("SAMPLING...");
    return true;
}

void micSamplerUpdate() {
    if (s_recActive) {
        // a chunk finished when the driver moved to the queued one
        while (!M5Cardputer.Mic.isRecording() ||
               M5Cardputer.Mic.isRecording() == 1 /*one buf left*/) {
            int16_t* done = s_chunk[s_curChunk];
            uint32_t room = s_dstMax - s_wr;
            uint32_t n = (room < CHUNK) ? room : CHUNK;
            if (n) {
                memcpy(s_dst + s_wr, done, n * 2);
                s_wr += n;
                int16_t pk = 0;
                for (uint32_t i = 0; i < n; i++) { int16_t a = abs(done[i]); if (a > pk) pk = a; }
                s_level = s_level * 0.6f + (pk / 32768.0f) * 0.4f;
            }
            if (s_wr >= s_dstMax) { micRecStop(); return; }
            M5Cardputer.Mic.record(done, CHUNK, s_recRate);   // re-queue
            s_curChunk ^= 1;
            if (M5Cardputer.Mic.isRecording() >= 2) break;
        }
        // level meter via the hold-progress footer bar
        g_holdProg = s_level > 1.0f ? 1.0f : s_level;
        strncpy(g_holdLabel, "SAMPLING", sizeof(g_holdLabel));
        static uint32_t t = 0;
        if (millis() - t > 50) { t = millis(); g_needRedraw = true; }
    }
    if (s_rsmpFrames && g_rsmpRemain == 0 && g_rsmpArmFrames == 0) {   // resample finished
        uint32_t frames = s_rsmpFrames;
        s_rsmpFrames = 0;
        if (g_rsmpWr < frames) {                             // the arena moved under it
            s_rsmpDeck = -1; g_tailHeld = false;
            uiStatus("RESAMPLE CANCELLED");
        } else if (s_rsmpDeck >= 0) {
            uint8_t d = (uint8_t)s_rsmpDeck; s_rsmpDeck = -1;
            if (commit((uint8_t)(TRK_DECK_A + d), 0, "RSM", s_rsmpCount, 0, frames, SAMPLE_RATE)) {
                g_decks[d].mode = DECK_LOOP;                // a bar-exact bounce loops as is
                g_decks[d].fitBars = s_rsmpBars;
                uiStatus("RESAMPLED > DECK");
            }
        } else {
            s_rsmpPending = true;
            uiStatus("RSMP: TAP A PAD");
        }
        g_needRedraw = true;
    }
}

void micRecStop() {
    if (!s_recActive) return;
    s_recActive = false;
    M5Cardputer.Mic.end();
    M5Cardputer.Speaker.begin();
    engineUnlock();
    g_holdProg = 0; g_holdLabel[0] = 0;

    uint32_t start, len;
    trimTail(s_dst, s_wr, s_recRate, start, len);
    if (commit(s_recTrack, s_recLane, "MIC", s_micCount, start, len, s_recRate))
        uiStatus("SAMPLED!");
    g_needRedraw = true;
}

bool micRecActive() { return s_recActive; }

// ---------- resampling (engine mix -> arena tail, tap in the audio task) ----------
bool resampleArm(uint8_t track) {
    if (s_recActive || g_tailHeld) return false;
    uint32_t maxF; int16_t* dst = samplerScratch(&maxF);
    if (!dst) return false;
    g_rsmpDst = dst; g_rsmpWr = 0;

    if (trackIsDeck(track)) {
        // exactly one pass of the playing pattern, starting on the next bar line
        uint8_t  len    = patLen(g_patterns[g_playPattern]);
        uint32_t frames = (uint32_t)((float)SAMPLE_RATE * 15.0f / (float)g_bpm * (float)len);
        if (frames > maxF) return false;
        s_rsmpDeck = (int8_t)trackDeck(track);
        s_rsmpBars = (uint8_t)(len / STEPS_PER_BAR);
        s_rsmpFrames = frames;
        g_tailHeld = true;
        g_rsmpArmFrames = frames;           // the sequencer starts the tap on step 1
        uiStatus("RESAMPLE AT BAR...");
    } else {
        uint32_t frames = maxF < RSMP_PAD_FRAMES ? maxF : RSMP_PAD_FRAMES;
        if (frames < SAMPLE_RATE / 4) return false;
        s_rsmpDeck = -1;
        s_rsmpFrames = frames;
        g_tailHeld = true;
        g_rsmpRemain = frames;              // audio task decrements
        uiStatus("RESAMPLING...");
    }
    return true;
}

bool resamplePending() { return s_rsmpPending; }

void resampleCommit(uint8_t lane) {
    s_rsmpPending = false;
    if (!g_tailHeld) { uiStatus("RESAMPLE CANCELLED"); return; }
    uint32_t start, len;
    trimTail(g_rsmpDst, g_rsmpWr, SAMPLE_RATE, start, len);
    if (commit(TRK_DRUMS, lane, "RSM", s_rsmpCount, start, len, SAMPLE_RATE))
        uiStatus("RESAMPLED!");
    g_needRedraw = true;
}
