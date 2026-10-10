// ============================================================
// FILTRE - bounce.cpp
// Offline render to WAV. The step clock lives in the render, so the
// bounce is the same audio the jack plays, only faster than real time.
// ============================================================
#include "storage.h"
#include "sequencer.h"
#include "audio_engine.h"
#include <SD.h>
#include <string.h>

#define BOUNCE_TAIL_SEC   1.5f
#define BOUNCE_MAX_SEC    900          // 15 minutes per file

static void wavHeader(File& f, uint32_t frames) {
    uint32_t dataBytes = frames * 2, rate = SAMPLE_RATE;
    uint8_t h[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',
                     16,0,0,0, 1,0, 1,0, 0,0,0,0, 0,0,0,0, 2,0, 16,0,
                     'd','a','t','a',0,0,0,0};
    uint32_t riff = 36 + dataBytes, brate = rate * 2;
    memcpy(h + 4,  &riff,  4);
    memcpy(h + 24, &rate,  4);
    memcpy(h + 28, &brate, 4);
    memcpy(h + 40, &dataBytes, 4);
    f.seek(0);
    f.write(h, 44);
}

static void silenceEngine() {
    for (int s = 0; s < NUM_SYNTHS; s++) g_synths[s].forEach([](SynthVoice& v){ v.active = false; v.ampEnv = 0; });
    for (int l = 0; l < NUM_DRUM_LANES; l++) { g_drumLanes[l].sv.active = false; g_drumLanes[l].smp.stop(); }
    engineStopSampleVoices();
    fxReset();
}

// total 16th steps of the chain from the loop start to its end (for progress)
static uint32_t songSteps() {
    uint32_t n = 0;
    for (int i = g_songLoopStart; i < SONG_LENGTH && g_song[i].pattern != SONG_EMPTY; i++)
        n += (uint32_t)g_song[i].repeats * patLen(g_patterns[g_song[i].pattern]);
    return n;
}

int storageBounce(void (*progress)(float, const char*)) {
    if (!SD.exists(DIR_BOUNCE)) SD.mkdir(DIR_BOUNCE);

    char path[80];
    int take = 0;
    for (take = 1; take < 100; take++) {
        snprintf(path, sizeof(path), "%s/T%02d_%ubpm_mix.wav", DIR_BOUNCE, take, (unsigned)g_bpm);
        if (!SD.exists(path)) break;
    }
    if (take >= 100) return -1;

    engineLock();

    // remember what the performer had, then build a one-shot chain
    bool     keepSong = g_songMode, keepRec = g_recEnabled;
    uint8_t  keepLoop = g_songLoopStart, keepStems = g_stemMask;
    SongSlot keepSlot0 = g_song[0], keepSlot1 = g_song[1];
    bool     madeChain = false;
    if (!keepSong || g_song[g_songLoopStart].pattern == SONG_EMPTY) {
        // no arrangement yet: four passes of the pattern on screen
        g_song[0] = { g_curPattern, 4, 0, TARGET_NONE };
        songClearSlot(1);
        g_songLoopStart = 0;
        madeChain = true;
    }
    g_recEnabled = false;
    g_songMode = true;
    g_songOneShot = true;

    static const struct { const char* name; uint8_t mask; } passes[] = {
        { "mix", STEM_ALL }, { "drums", STEM_DRUMS }, { "bass", STEM_BASS },
        { "synth", STEM_SYNTH }, { "decks", STEM_DECKS },
    };
    const int nPass = sizeof(passes) / sizeof(passes[0]);
    uint32_t total = (uint32_t)((float)songSteps() * SAMPLE_RATE * 15.0f / g_bpm)
                   + (uint32_t)(BOUNCE_TAIL_SEC * SAMPLE_RATE);
    static int16_t buf[AUDIO_BUF_LEN];
    bool ok = true;

    for (int ps = 0; ps < nPass && ok; ps++) {
        snprintf(path, sizeof(path), "%s/T%02d_%ubpm_%s.wav", DIR_BOUNCE, take,
                 (unsigned)g_bpm, passes[ps].name);
        SD.remove(path);
        File f = SD.open(path, FILE_WRITE);
        if (!f) { ok = false; break; }
        wavHeader(f, 0);

        silenceEngine();
        g_stemMask = passes[ps].mask;
        sequencerStart(true);

        uint32_t frames = 0, tail = (uint32_t)(BOUNCE_TAIL_SEC * SAMPLE_RATE);
        int32_t  peak = 0;
        bool started = false;
        while (frames < (uint32_t)BOUNCE_MAX_SEC * SAMPLE_RATE) {
            engineRender(buf, AUDIO_BUF_LEN, OUT_SCALE_BOUNCE);
            if (g_playing) started = true;
            if (started && !g_playing) {               // chain finished: let the tails ring out
                if (tail < AUDIO_BUF_LEN) break;
                tail -= AUDIO_BUF_LEN;
            }
            for (int i = 0; i < AUDIO_BUF_LEN; i++) { int32_t a = buf[i] < 0 ? -buf[i] : buf[i]; if (a > peak) peak = a; }
            if (f.write((uint8_t*)buf, sizeof(buf)) != sizeof(buf)) { ok = false; break; }
            frames += AUDIO_BUF_LEN;
            if (progress && (frames & 0x3FFF) == 0)
                progress(((float)ps + (float)frames / (float)(total ? total : 1)) / (float)nPass,
                         passes[ps].name);
        }
        wavHeader(f, frames);
        f.close();
        if (ok && peak < 8 && ps > 0) SD.remove(path);  // an empty stem is just clutter
    }

    // put everything back
    sequencerStop();
    engineRender(buf, AUDIO_BUF_LEN, 0.0f);            // let the stop land
    silenceEngine();
    g_stemMask = keepStems;
    g_songOneShot = false;
    g_songEnded = false;
    g_songMode = keepSong; g_recEnabled = keepRec;
    if (madeChain) { g_song[0] = keepSlot0; g_song[1] = keepSlot1; g_songLoopStart = keepLoop; }
    engineUnlock();
    return ok ? take : -1;
}
