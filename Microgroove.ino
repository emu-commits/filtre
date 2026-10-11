// ============================================================
// FILTRE - a pocket French-touch sampler for the M5Stack Cardputer-ADV
// with the TERM512 lid. A fork of Microgroove by lebiro.studio.
//   - 2 sample decks, each CHOP (transients on the keys, sequenced)
//     or LOOP (bar-locked), blended into one resonant filter
//   - kick-driven pump, swung 909 drums, 303 bass writer
//   - 8 patterns of 1 or 2 bars, per-step filter locks
//   - song chain with repeats, mutes and filter targets
//   - offline bounce of mix + stems to microSD
//   - Microgroove's 3 synth tracks, 808/909 lanes, mic + resampling
//   - GBX v3 projects (loads Microgroove v1/v2 transparently)
//
// Portions of the synth voice, 808 drums, and audio task are derived
// from qwertyuu/Cardputer-Adv-Tracker (MIT License) - see LICENSE.
// ============================================================
#include <M5Cardputer.h>
#include <SPI.h>
#include <SD.h>

#include "config.h"
#include "sequencer.h"
#include "sampler.h"
#include "wavetable.h"
#include "storage.h"
#include "audio_engine.h"
#include "mic_sampler.h"
#include "ui.h"
#include "lid_ui.h"
#include "styles.h"
#include "kbd_reader.h"

void inputInit();
void inputUpdate();

bool g_sdOk = false;

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    kbdReaderInstall();              // the library's reader can hang: see kbd_reader.cpp

    uiInit();

    // Speaker / codec
    auto spk = M5Cardputer.Speaker.config();
    spk.sample_rate   = SAMPLE_RATE;
    spk.task_priority = 3;
    spk.dma_buf_count = 4;
    spk.dma_buf_len   = AUDIO_BUF_LEN;
    M5Cardputer.Speaker.config(spk);
    M5Cardputer.Speaker.begin();
    M5Cardputer.Speaker.setVolume(200);

    // Lid first, then the card: the lid shares the SD's SPI bus and starting
    // it re-begins that bus. The internal LCD is on the other host throughout.
    delay(100);
    lidInit();

    // SD (Cardputer-ADV pinout)
    SPI.begin(SD_SPI_CLK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
    g_sdOk = SD.begin(SD_SPI_CS_PIN, SPI, 25000000);

    // Modules. The arena is sized last-but-one so it takes what is really free.
    wavetableInitBuiltins();
    if (g_sdOk) {
        samplerEnsureDirs();
        wavetableLoadUserFromSD();
    }
    sequencerInit();
    deckInit();
    samplerInit();
    filtreDefaults();                // FILTRE kit + drums on pattern 1, 122 BPM

    Serial.printf("FILTRE boot: heap %u, arena %u, sd %d, lid %d\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)g_arenaCap, g_sdOk, g_lidOk);
    lidBootScreen();

    uiSplash();
    while (true) {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) break;
        if (M5Cardputer.BtnA.wasPressed()) break;
        delay(30);
    }

    inputInit();
    audioEngineStart();              // render task on core 0

    if (!g_sdOk) uiStatus("NO SD: =+del = DEMO");
    g_needRedraw = true;
}

void loop() {
    // a pass this long means core 1 was blocked, which also looks like dead keys
    static uint32_t lastPass = 0;
    uint32_t passMs = millis();
    if (lastPass && passMs - lastPass > 400)
        Serial.printf("LOOP: stalled %u ms (page %d, heap %u)\n",
                      (unsigned)(passMs - lastPass), (int)g_curPage, (unsigned)ESP.getFreeHeap());
    lastPass = passMs;

    inputUpdate();
    micSamplerUpdate();
    uiTick();

    if (g_needRedraw) {
        g_needRedraw = false;
        uiDraw();
    }

    // moving parts: scope, playheads (a chop played by hand has one too), the live filter bar
    bool sounding = g_playing;
    for (int d = 0; d < NUM_DECKS; d++) sounding |= g_decks[d].v[g_decks[d].cur].active;
    if (sounding) {
        static uint32_t last = 0;
        uint32_t every = (g_curPage == PAGE_SETTINGS || (g_curPage == PAGE_PATTERN && g_lidOk)) ? 60 : 120;
        if (millis() - last > every) { last = millis(); uiDraw(); }
    }
    lidUpdate(false);

    delay(2);
}
