// ============================================================
// FILTRE - audio_engine.cpp
// Render task derived from qwertyuu/Cardputer-Adv-Tracker (MIT),
// extended with 8 drum lanes + sample voices, then the deck bus,
// pump and the in-render step clock.
// ============================================================
#include "audio_engine.h"
#include "sequencer.h"
#include "sampler.h"
#include <math.h>
#ifdef ARDUINO
#include <M5Cardputer.h>
#endif

float g_scopeBuf[SCREEN_W];
volatile int g_scopeIdx = 0;

int16_t*          g_rsmpDst    = nullptr;
volatile uint32_t g_rsmpRemain = 0;
volatile uint32_t g_rsmpWr     = 0;

// identity below 0.75, smooth knee to 1.0 at 1.25
static inline float softClip(float x) {
    float a = x < 0 ? -x : x;
    if (a <= 0.75f) return x;
    float y;
    if (a >= 1.25f) y = 1.0f;
    else { float t = (a - 0.75f) * 2.0f; y = 0.75f + 0.5f * (t - 0.5f * t * t); }
    return x < 0 ? -y : y;
}

void engineRender(int16_t* buf, int n, float outScale) {
    seqDrainEvents();
    const uint8_t stems = g_stemMask;

    for (int i = 0; i < n; i++) {
        seqTick();
        g_cutNow = seqCutoffTick();

        // every voice renders every sample so stems stay phase-identical to the mix
        float bass  = g_synths[0].render();
        float synth = g_synths[1].render();
        float s3    = g_synths[2].render();
        const uint16_t mute = seqMuteMask();
        if (mute & MUTE_SYNTH(0)) bass = 0.0f;
        if (mute & MUTE_SYNTH(1)) synth = 0.0f;
        if (!(mute & MUTE_SYNTH(2))) synth += s3;

        float drums = 0.0f;
        for (int d = 0; d < NUM_DRUM_LANES; d++) {
            float v = g_drumLanes[d].render();
            if (!(mute & MUTE_LANE(d))) drums += v;
        }
        drums *= 0.6f;

        float decks = deckBusRender();

        // pump: depth 0 leaves the gain at exactly 1
        float duck = pumpRender();
        if (g_fx.pump) {
            float depth = (float)g_fx.pump * 0.01f;
            decks *= 1.0f - depth * duck;
            bass  *= 1.0f - depth * (float)g_fx.bassPump * 0.01f * duck;
        }

        float mix = g_previewVoice.render();
        if (stems & STEM_DRUMS) mix += drums;
        if (stems & STEM_BASS)  mix += bass;
        if (stems & STEM_SYNTH) mix += synth;
        if (stems & STEM_DECKS) mix += decks;

        mix = softClip(mix);
        buf[i] = (int16_t)(mix * outScale);

        if (g_rsmpRemain) {                       // resample tap
            g_rsmpDst[g_rsmpWr++] = buf[i];
            g_rsmpRemain--;
        }

        if ((i & 7) == 0 && g_scopeIdx < SCREEN_W)
            g_scopeBuf[g_scopeIdx++] = mix;
    }
}

#ifdef ARDUINO
static int16_t s_bufA[AUDIO_BUF_LEN];
static int16_t s_bufB[AUDIO_BUF_LEN];
static TaskHandle_t s_task = nullptr;
static volatile int  s_hold   = 0;
static volatile bool s_parked = false;

static void audioTask(void*) {
    int16_t* buffers[2] = { s_bufA, s_bufB };
    int cur = 0;

    while (true) {
        int16_t* buf = buffers[cur];

        if (s_hold) {
            memset(buf, 0, sizeof(s_bufA));
            s_parked = true;
        } else {
            s_parked = false;
            engineRender(buf, AUDIO_BUF_LEN, OUT_SCALE_LIVE);
        }

        // while the speaker is off (mic capture) just idle
        while (!M5Cardputer.Speaker.playRaw(buf, AUDIO_BUF_LEN, SAMPLE_RATE, false, 1, 0)) {
            if (s_hold) s_parked = true;
            vTaskDelay(1);
        }

        cur ^= 1;
    }
}

void audioEngineStart() {
    xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 1, &s_task, 0);
}

void engineLock() {
    if (s_hold++ == 0 && s_task) {
        s_parked = false;
        uint32_t t0 = millis();
        while (!s_parked && millis() - t0 < 200) delay(1);
    }
}

void engineUnlock() {
    if (s_hold > 0) s_hold--;
}
#else
void audioEngineStart() {}
void engineLock() {}
void engineUnlock() {}
#endif
