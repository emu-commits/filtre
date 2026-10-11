// FILTRE engine tests - runs the real firmware engine natively.
#include "SD.h"
#include "config.h"
#include "sampler.h"
#include "deck.h"
#include "sequencer.h"
#include "audio_engine.h"
#include "styles.h"
#include "storage.h"
#include "wavetable.h"
#include <vector>
#include <string>

static int s_fail = 0, s_pass = 0;
#define CHECK(cond, ...) do { if (cond) s_pass++; else { s_fail++; printf("  FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define SECTION(name) printf("== %s\n", name)

static void writeWav(const char* sdPath, const std::vector<float>& mono, uint32_t rate, int channels, int bits) {
    File f = SD.open(sdPath, FILE_WRITE);
    uint32_t bps = bits / 8, dataBytes = (uint32_t)mono.size() * channels * bps;
    uint8_t h[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'d','a','t','a',0,0,0,0};
    uint32_t riff = 36 + dataBytes, brate = rate * channels * bps; uint16_t ch = channels, ba = channels * bps, bi = bits;
    memcpy(h + 4, &riff, 4); memcpy(h + 22, &ch, 2); memcpy(h + 24, &rate, 4); memcpy(h + 28, &brate, 4);
    memcpy(h + 32, &ba, 2); memcpy(h + 34, &bi, 2); memcpy(h + 40, &dataBytes, 4);
    f.write(h, 44);
    for (float v : mono) {
        int32_t s = (int32_t)(v * 8388607.0f);
        for (int c = 0; c < channels; c++) {
            if (bits == 16) { int16_t x = (int16_t)(s >> 8); f.write((uint8_t*)&x, 2); }
            else { uint8_t b[3] = { (uint8_t)s, (uint8_t)(s >> 8), (uint8_t)(s >> 16) }; f.write(b, 3); }
        }
    }
    f.close();
}

// one call per sample so tests can watch the clock
static std::vector<int16_t> render(uint32_t n, float scale = OUT_SCALE_BOUNCE) {
    std::vector<int16_t> out(n);
    for (uint32_t i = 0; i < n; i++) engineRender(&out[i], 1, scale);
    return out;
}
static double rms(const std::vector<int16_t>& v, size_t a, size_t b) {
    double s = 0; for (size_t i = a; i < b && i < v.size(); i++) s += (double)v[i] * v[i];
    return sqrt(s / (double)(b > a ? b - a : 1));
}
static void quiet() {
    sequencerStop(); render(64);
    for (int s = 0; s < NUM_SYNTHS; s++) g_synths[s].forEach([](SynthVoice& v){ v.active = false; });
    for (int l = 0; l < NUM_DRUM_LANES; l++) { g_drumLanes[l].sv.active = false; g_drumLanes[l].smp.stop(); }
    engineStopSampleVoices(); fxReset();
}
static void freshProject() {
    quiet();
    for (int d = 0; d < NUM_DECKS; d++) deckUnload(d);
    samplerClearAll();
    sequencerInit(); deckInit(); filtreDefaults();
    g_songMode = false; g_recEnabled = false; g_curPattern = 0; g_stemMask = STEM_ALL;
    g_nextPattern = 0;
}
static float stepLen() { return SAMPLE_RATE * 15.0f / g_bpm; }

// a test loop: 8 tone bursts, one per 8th note, exactly one bar at `bpm`
static std::vector<float> burstLoop(uint32_t rate, float bpm, std::vector<uint32_t>* onsets = nullptr, int bars = 1) {
    uint32_t n = (uint32_t)(rate * 240.0f / bpm * bars);
    std::vector<float> v(n, 0.0f);
    for (int k = 0; k < 8 * bars; k++) {
        uint32_t a = (uint32_t)((uint64_t)n * k / (8 * bars));
        if (onsets) onsets->push_back(a);
        float f = 220.0f * (1 + (k % 4));
        for (uint32_t i = 0; i < rate / 12 && a + i < n; i++)
            v[a + i] = 0.8f * sinf(6.2831853f * f * i / rate) * expf(-(float)i / (rate * 0.03f));
    }
    return v;
}

int main() {
    samplerInit(); wavetableInitBuiltins(); samplerEnsureDirs();

    SECTION("mu-law");
    {
        double worst = 0;
        std::vector<float> one(1);
        for (int v = -32000; v <= 32000; v += 37) {
            // encode through the public path: a 1-frame raw add would be overkill; use the table
            int best = 0; int bd = 1 << 30;
            for (int c = 0; c < 256; c++) { int d = abs(g_ulawTable[c] - v); if (d < bd) { bd = d; best = c; } }
            (void)best; double rel = (double)bd / (abs(v) + 200.0);
            if (rel > worst) worst = rel;
        }
        CHECK(worst < 0.04, "mu-law table error %.3f", worst);
        CHECK(g_ulawTable[0xFF] == 0 && g_ulawTable[0x7F] == 0, "silence codes");
    }

    SECTION("deck load: 44.1k stereo 16-bit and 48k mono 24-bit");
    {
        freshProject();
        std::vector<uint32_t> on;
        writeWav("/groovebox/loops/b441.wav", burstLoop(44100, 122, &on), 44100, 2, 16);
        writeWav("/groovebox/loops/b48.wav", burstLoop(48000, 122), 48000, 1, 24);
        uint32_t expect = (uint32_t)(SAMPLE_RATE * 240.0f / 122);
        CHECK(deckLoad(0, "b441.wav"), "load 44.1k");
        CHECK(abs((int)g_samples[g_decks[0].slot].length - (int)expect) < 4, "frames %u vs %u", g_samples[g_decks[0].slot].length, expect);
        CHECK(g_samples[g_decks[0].slot].fmt == FMT_PCM16, "short loop stays 16-bit");
        CHECK(deckLoad(1, "b48.wav"), "load 48k/24");
        CHECK(abs((int)g_samples[g_decks[1].slot].length - (int)expect) < 4, "48k frames %u", g_samples[g_decks[1].slot].length);
        int32_t pk = 0; for (uint32_t i = 0; i < expect - 2; i++) { int32_t v = abs((int)smpRead(g_arena + g_samples[g_decks[1].slot].offset, FMT_PCM16, i)); if (v > pk) pk = v; }
        CHECK(pk > 27000 && pk < 30500, "normalised peak %d", pk);

        SECTION("transient slicing");
        Deck& k = g_decks[0];
        CHECK(k.nSlices == 8, "auto slices %u (want 8)", k.nSlices);
        int worst = 0;
        for (int i = 0; i < k.nSlices && i < 8; i++) {
            int want = (int)(on[i] / 2);   // 44.1k -> 22.05k
            int d = abs((int)k.slice[i] - want); if (d > worst) worst = d;
        }
        CHECK(worst < 70, "slice starts within 3 ms (worst %d frames)", worst);
        k.sliceMode = SLICE_16; deckReslice(0);
        CHECK(k.nSlices == 16 && k.slice[16] == k.out, "grid 16");
        k.sliceMode = SLICE_AUTO; deckReslice(0);

        SECTION("arena free + compaction");
        uint32_t usedBoth = g_arenaUsed, bBytes = samplerBytes(g_decks[1].slot);
        int16_t probe = ((int16_t*)(g_arena + g_samples[g_decks[1].slot].offset))[1234];
        deckUnload(0);
        CHECK(g_arenaUsed == usedBoth - (usedBoth - bBytes), "arena shrank to deck B only (%u)", g_arenaUsed);
        CHECK(g_samples[g_decks[1].slot].offset == 0, "deck B moved to the front");
        CHECK(((int16_t*)(g_arena + g_samples[g_decks[1].slot].offset))[1234] == probe, "data intact after move");
        CHECK(samplerOwnerBytes(OWN_DECK_B) == bBytes && samplerOwnerBytes(OWN_DECK_A) == 0, "owner metering");

        SECTION("long file: falls back to mu-law, then cuts (never silently)");
        std::vector<float> lng((size_t)44100 * 7, 0.0f);
        for (size_t i = 0; i < lng.size(); i++) lng[i] = 0.5f * sinf(i * 0.05f);
        writeWav("/groovebox/loops/long.wav", lng, 44100, 1, 16);
        CHECK(deckLoad(0, "long.wav"), "load 7 s");
        CHECK(g_samples[g_decks[0].slot].fmt == FMT_ULAW, "7 s goes mu-law");
        std::vector<float> huge((size_t)44100 * 30, 0.1f);
        writeWav("/groovebox/loops/huge.wav", huge, 44100, 1, 16);
        CHECK(deckLoad(0, "huge.wav") && g_decks[0].cut, "30 s is cut and flagged");
        CHECK(g_arenaUsed <= g_arenaCap, "arena not overrun (%u/%u)", g_arenaUsed, g_arenaCap);
    }

    SECTION("clock: swing moves odd 16ths only");
    {
        freshProject();
        g_bpm = 122; g_fx.swing = 62;
        sequencerStart(true);
        std::vector<uint32_t> at; uint8_t last = 255; int16_t s;
        for (uint32_t i = 0; i < (uint32_t)(stepLen() * 33); i++) {
            engineRender(&s, 1, 1.0f);
            if (g_lastStep != last || (at.empty() && g_playing)) { if (g_lastStep != last) at.push_back(i); last = g_lastStep; }
        }
        float P = stepLen(); bool evenOk = true, oddOk = true;
        for (size_t k = 0; k + 1 < at.size() && k < 32; k++) {
            float grid = (k / 2) * 2 * P;
            float want = (k & 1) ? grid + 2 * P * 0.62f : grid;
            if (fabsf((float)at[k] - at[0] - want) > 1.5f) { if (k & 1) oddOk = false; else evenOk = false; }
        }
        CHECK(at.size() >= 32, "32 steps fired (%zu)", at.size());
        CHECK(evenOk, "even 16ths on the grid"); CHECK(oddOk, "odd 16ths late by swing");
        quiet();
    }

    SECTION("pump");
    {
        freshProject();
        std::vector<float> flat((size_t)44100 * 240 / 122, 0.0f);
        for (size_t i = 0; i < flat.size(); i++) flat[i] = 0.5f * sinf(i * 0.2f);
        writeWav("/groovebox/loops/flat.wav", flat, 44100, 1, 16);
        deckLoad(0, "flat.wav"); g_decks[0].mode = DECK_LOOP; g_decks[0].fitBars = 1;
        g_fx.cutoff = 255; g_fx.reso = 0; g_fx.swing = 50; g_stemMask = STEM_DECKS;
        uint32_t bar = (uint32_t)(stepLen() * 16);

        g_fx.pump = 0; quiet(); render(8000); sequencerStart(true); auto dry = render(bar * 2); quiet();
        memset(g_patterns[0].drums, 0, NUM_STEPS);            // no kick at all
        sequencerStart(true); auto none = render(bar * 2); quiet();
        CHECK(dry == none, "PUMP 0 is bit-identical to no kick");

        filtreDefaults(); g_fx.cutoff = 255; g_fx.reso = 0; g_fx.swing = 50; g_fx.pump = 70; g_fx.release = 180;
        sequencerStart(true); auto wet = render(bar * 2); quiet();
        uint32_t beat = bar / 4, k2 = bar + beat;              // a kick in bar 2
        double atKick = rms(wet, k2 + 150, k2 + 700), before8th = rms(wet, k2 + beat / 2 - 600, k2 + beat / 2);
        double ref = rms(dry, k2 + 150, k2 + 700);
        CHECK(atKick < ref * 0.45, "loop ducks at the kick (%.0f vs %.0f)", atKick, ref);
        CHECK(before8th > ref * 0.85, "recovered before the next 8th (%.0f vs %.0f)", before8th, ref);

        SECTION("ghost pump: kick muted by the song slot still pumps");
        g_songMode = true; g_song[0] = { 0, 2, SLOT_MUTE_KICK, TARGET_NONE }; g_songLoopStart = 0;
        sequencerStart(true); auto ghost = render(bar * 2); quiet();
        CHECK(rms(ghost, k2 + 150, k2 + 700) < ref * 0.45, "ghost pump works");
        g_fx.ghost = 0; sequencerStart(true); auto noGhost = render(bar * 2); quiet();
        CHECK(rms(noGhost, k2 + 150, k2 + 700) > ref * 0.9, "GHOST off: no pump without the kick");
        g_songMode = false; songClearSlot(0); g_stemMask = STEM_ALL;
    }

    SECTION("filter opens fully and stays stable");
    {
        freshProject();
        std::vector<float> nz((size_t)44100 * 240 / 122); uint32_t seed = 7;
        for (auto& v : nz) { seed = seed * 1664525u + 1013904223u; v = ((seed >> 8) / 8388608.0f - 1.0f) * 0.5f; }
        writeWav("/groovebox/loops/noise.wav", nz, 44100, 1, 16);
        deckLoad(0, "noise.wav"); g_decks[0].mode = DECK_LOOP; g_decks[0].fitBars = 1;
        g_fx.pump = 0; g_fx.reso = 20; g_stemMask = STEM_DECKS; memset(g_patterns[0].drums, 0, NUM_STEPS);
        uint32_t bar = (uint32_t)(stepLen() * 16);
        double lvl[3]; uint8_t cuts[3] = { 255, 128, 20 };
        for (int i = 0; i < 3; i++) { g_fx.cutoff = cuts[i]; render(8000); fxReset(); sequencerStart(true); auto o = render(bar); quiet(); lvl[i] = rms(o, 2000, bar); }
        g_fx.type = FLT_HP; g_fx.cutoff = 0; render(8000); fxReset(); sequencerStart(true); auto hp = render(bar); quiet();
        g_fx.type = FLT_LP;
        CHECK(lvl[0] > rms(hp, 2000, bar) * 0.85, "LP wide open passes the full band (%.0f vs %.0f)", lvl[0], rms(hp, 2000, bar));
        CHECK(lvl[1] < lvl[0] * 0.75 && lvl[2] < lvl[0] * 0.25, "closing the filter darkens (%.0f %.0f %.0f)", lvl[0], lvl[1], lvl[2]);
        g_fx.reso = 127; int32_t pk = 0;
        for (int c = 0; c <= 255; c += 15) { g_fx.cutoff = c; sequencerStart(true); auto o = render(bar / 4); quiet(); for (auto v : o) if (abs(v) > pk) pk = abs(v); }
        CHECK(pk <= 26000, "full resonance sweep never exceeds the clip ceiling (%d)", pk);
        g_stemMask = STEM_ALL;
    }

    SECTION("chop sequencing + live record");
    {
        freshProject();
        deckLoad(0, "b441.wav"); g_decks[0].mode = DECK_CHOP;
        g_fx.pump = 0; g_fx.cutoff = 255; g_fx.swing = 50; g_stemMask = STEM_DECKS;
        Pattern& p = g_patterns[0]; memset(p.drums, 0, NUM_STEPS);
        p.chop[0][0] = 1; p.chop[0][4] = 3; p.chop[0][10] = 5;
        sequencerStart(true); auto o = render((uint32_t)(stepLen() * 16)); quiet();
        float P = stepLen();
        CHECK(rms(o, 0, 600) > 2000 && rms(o, (size_t)(4 * P), (size_t)(4 * P) + 600) > 2000 && rms(o, (size_t)(10 * P), (size_t)(10 * P) + 600) > 2000, "slices sound on their steps");
        CHECK(rms(o, (size_t)(2.5f * P), (size_t)(3.5f * P)) < 200, "silence between chops");

        memset(p.chop, 0, sizeof(p.chop));
        g_recEnabled = true; sequencerStart(true);
        render((uint32_t)(6 * P) + 900 + 30);                 // player hits just after step 7 sounds
        liveDeckHit(0, 2, false);
        render((uint32_t)(3 * P));
        liveDrumHit(2, true);                                  // ~step 10
        render((uint32_t)(8 * P)); quiet(); g_recEnabled = false;
        CHECK(p.chop[0][6] == 3, "pad recorded on step 7 (latency compensated)");
        int hat = -1; for (int i = 0; i < 16; i++) if ((p.drums[i] & 4) && (p.drumAcc[i] & 4)) hat = i;
        CHECK(hat == 9, "accented drum hit recorded on step 10 (got %d)", hat + 1);
        g_stemMask = STEM_ALL;
    }

    SECTION("cutoff locks: recorded sweep repeats every bar, no zipper");
    {
        freshProject();
        Pattern& p = g_patterns[0];
        for (int i = 0; i < 16; i++) { p.lock[i] = (uint8_t)(i * 16); p.lockSet |= 1u << i; }
        g_fx.swing = 50; sequencerStart(true);
        uint32_t bar = (uint32_t)(stepLen() * 16); std::vector<float> cut(bar * 2); int16_t s;
        for (uint32_t i = 0; i < bar * 2; i++) { engineRender(&s, 1, 1.0f); cut[i] = g_cutNow; }
        quiet();
        float maxJump = 0, diff = 0;
        for (uint32_t i = 1; i < bar - 1500; i++) maxJump = fmaxf(maxJump, fabsf(cut[i] - cut[i - 1]));
        for (uint32_t i = 100; i < bar - 100; i += 97) diff = fmaxf(diff, fabsf(cut[i] - cut[i + bar]));
        CHECK(maxJump < 0.2f, "sweep is continuous (max step %.3f)", maxJump);
        CHECK(diff < 1.5f, "bar 2 repeats bar 1 (%.2f)", diff);
        CHECK(fabsf(cut[(uint32_t)(stepLen() * 8)] - 128) < 3, "mid-bar value %.1f", cut[(uint32_t)(stepLen() * 8)]);
        g_curTrack = TRK_DECK_A; clearPattern(0);
        CHECK(g_patterns[0].lockSet == 0, "CLR pattern clears its locks");
    }

    SECTION("song: repeats, targets, mutes, one-shot end");
    {
        freshProject(); loadDemoPattern();
        CHECK(deckLoaded(0) && g_decks[0].fitBars == 2, "factory loop on deck A, 2 bars (fit %u)", g_decks[0].fitBars);
        g_songOneShot = true; g_songLoopStart = 0; sequencerStart(true);
        uint32_t bar = (uint32_t)(stepLen() * 16), n = 0; int16_t s; float atEnd[4] = {0}; uint8_t lastPos = 0;
        std::vector<int16_t> bassDuringSlot0;
        while (n < bar * 12) {
            engineRender(&s, 1, OUT_SCALE_BOUNCE);
            if (n && !g_playing) break;
            if (g_songPos != lastPos) lastPos = g_songPos;
            atEnd[g_songPos] = g_cutNow; n++;
        }
        CHECK(abs((int)n - (int)(bar * 7)) < 8, "2+2+2+1 bars then stop (%u vs %u)", n, bar * 7);
        CHECK(fabsf(atEnd[0] - 50) < 3 && fabsf(atEnd[1] - 140) < 3 && fabsf(atEnd[2] - 254) < 3 && fabsf(atEnd[3] - 51) < 3,
              "targets reached: %.0f %.0f %.0f %.0f", atEnd[0], atEnd[1], atEnd[2], atEnd[3]);
        g_songOneShot = false; quiet();
    }

    SECTION("demo loop plays from flash: no arena needed");
    {
        freshProject();
        uint32_t cap = g_arenaCap; g_arenaCap = g_arenaUsed;          // a device whose arena came up full/tiny
        CHECK(loadDemoPattern() && deckLoaded(0), "demo loads with 0 bytes of arena free");
        CHECK(g_arenaUsed == g_arenaCap && samplerOwnerBytes(OWN_DECK_A) == 0, "and takes none (%u used)", g_arenaUsed);
        g_arenaCap = cap;
        sequencerStart(true); int16_t b[256]; int32_t peak = 0;
        g_fx.cutoff = 254; g_songMode = false;
        for (int i = 0; i < 200; i++) { engineRender(b, 256, OUT_SCALE_BOUNCE); for (int j = 0; j < 256; j++) if (abs(b[j]) > peak) peak = abs(b[j]); }
        CHECK(peak > 2000, "it is audible (peak %d)", (int)peak);
        quiet(); deckUnload(0);
        CHECK(!deckLoaded(0) && g_arenaUsed == 0, "unloading it leaves the arena untouched (%u)", g_arenaUsed);
    }

    SECTION("project v3 round trip + upstream v1 file");
    {
        Pattern keep = g_patterns[1]; FxParams fx = g_fx; SongSlot sl = g_song[2];
        setPatternLength(1, NUM_STEPS); g_patterns[1].chop[0][20] = 4 | CHOP_ACC; g_patterns[1].lock[31] = 200; g_patterns[1].lockSet |= 1u << 31;
        g_patterns[1].mute = MUTE_SYNTH(1) | MUTE_DECK(1) | MUTE_LANE(3);
        g_patterns[5].mute = MUTE_LANE(0);
        keep = g_patterns[1];
        deckUnload(0); deckLoad(0, "FILTRE-DEMO.wav"); g_decks[0].mode = DECK_CHOP; deckNudgeSlice(0, 1, 30);
        Deck dk = g_decks[0]; g_fx.key = 4; g_fx.reso = 99; fx = g_fx; uint16_t bpm = g_bpm = 124;
        CHECK(storageSaveProject(2), "save P3");
        freshProject();
        CHECK(storageLoadProject(2), "load P3");
        CHECK(memcmp(&keep, &g_patterns[1], sizeof(Pattern)) == 0, "pattern (32 steps, chops, locks) identical");
        CHECK(memcmp(&fx, &g_fx, sizeof(FxParams)) == 0 && g_bpm == bpm, "fx + bpm identical");
        CHECK(g_patterns[1].mute == (MUTE_SYNTH(1) | MUTE_DECK(1) | MUTE_LANE(3)) && g_patterns[5].mute == MUTE_LANE(0) && g_patterns[0].mute == 0,
              "mutes come back per pattern (%03x %03x)", g_patterns[1].mute, g_patterns[5].mute);
        CHECK(memcmp(&sl, &g_song[2], sizeof(SongSlot)) == 0, "song slot identical");
        CHECK(deckLoaded(0) && g_decks[0].nSlices == dk.nSlices && memcmp(dk.slice, g_decks[0].slice, sizeof(dk.slice)) == 0 && g_decks[0].mode == DECK_CHOP, "deck + edited slices restored");
        CHECK(g_drumLanes[1].type == DT_CLAP9 && g_drumLanes[1].engine == ENG_909, "909 clap lane survives");

        CHECK(storageLoadProject(0), "upstream factory P1.gbx (v1) loads");
        CHECK(g_fx.swing == 50 && !deckLoaded(0), "legacy project: straight, no decks");
        bool any = false; for (int st = 0; st < 16; st++) if (g_patterns[0].drums[st]) any = true;
        CHECK(any && patLen(g_patterns[0]) == 16 && g_song[0].repeats == 1 && g_song[0].target == TARGET_NONE, "v1 patterns + song migrated");
        sequencerStart(true); auto o = render(40000); quiet();
        CHECK(rms(o, 0, 40000) > 500, "v1 project makes sound");
    }

    SECTION("mutes live in the pattern; a pattern change waits for the bar");
    {
        freshProject();
        g_fx.pump = 0; g_fx.swing = 50; g_stemMask = STEM_DRUMS;
        uint32_t bar = (uint32_t)(stepLen() * 16);
        pastePattern(0, 1); pastePattern(0, 2);
        CHECK(memcmp(g_patterns[0].drums, g_patterns[2].drums, NUM_STEPS) == 0, "paste copies the pattern");
        g_patterns[1].mute = MUTE_ALL_LANES;                   // pattern 2: every drum lane off
        g_patterns[2].mute = MUTE_ALL_LANES & ~MUTE_LANE(0);   // pattern 3: kick only

        sequencerStart(true); auto b1 = render(bar / 2);
        seqSelectPattern(1); seqSelectPattern(2); seqSelectPattern(1);          // P+ P+ P-
        CHECK(g_curPattern == 0 && g_nextPattern == 1, "queued, the editor has not moved (cur %u next %u)", g_curPattern, g_nextPattern);
        auto b1b = render(bar / 2 - 200);
        CHECK(g_playPattern == 0 && !seqFollowPattern(), "still on pattern 1 just before the bar line");
        CHECK(rms(b1b, 0, b1b.size()) > 400, "pattern 1 keeps sounding to its end (%.0f)", rms(b1b, 0, b1b.size()));
        render(400);
        CHECK(g_playPattern == 1, "switched on the bar line (%u)", g_playPattern);
        CHECK(seqFollowPattern() && g_curPattern == 1, "the editor follows");
        auto b2 = render(bar - 1200);
        CHECK(rms(b2, 3000, b2.size()) < 30, "pattern 2 carries its own mutes: drums silent (%.0f)", rms(b2, 3000, b2.size()));
        seqSelectPattern(2); render(1400);
        CHECK(g_playPattern == 2, "next queued pattern lands a bar later");
        auto b3 = render(bar - 2000);
        double all = rms(b1, 0, b1.size()), kick = rms(b3, 0, b3.size());
        CHECK(kick > 300 && kick < all * 0.97, "pattern 3: the kick alone (%.0f vs all %.0f)", kick, all);
        g_patterns[2].mute = 0; auto b3b = render(bar / 2);
        seqFollowPattern();
        quiet();
        seqSelectPattern(5);
        CHECK(g_curPattern == 5 && g_nextPattern == 5, "stopped: a pattern change is immediate");
        g_stemMask = STEM_ALL;
    }

    SECTION("phaser: stable, level-matched, and it moves");
    {
        freshProject();
        deckLoad(0, "noise.wav"); g_decks[0].mode = DECK_LOOP; g_decks[0].fitBars = 1;
        g_fx.pump = 0; g_fx.reso = 0; g_stemMask = STEM_DECKS; memset(g_patterns[0].drums, 0, NUM_STEPS);
        uint32_t bar = (uint32_t)(stepLen() * 16);
        g_fx.type = FLT_LP; g_fx.cutoff = 255; fxReset(); sequencerStart(true); auto dry = render(bar); quiet();
        g_fx.type = FLT_PHASER; g_fx.cutoff = 128; fxReset(); sequencerStart(true); auto ph = render(bar * 4); quiet();
        double d = rms(dry, 2000, bar), w = rms(ph, 2000, bar * 4);
        CHECK(w > d * 0.6 && w < d * 1.3, "phased loop about as loud as the dry one (%.0f vs %.0f)", w, d);
        // a notch passes through 400 Hz as the sweep goes: a narrow probe there rises and falls over the 4 bars
        double lo = 1e9, hi = 0;
        const double rr = 0.985, cw = 2 * rr * cos(6.2831853 * 400.0 / SAMPLE_RATE);
        for (int k = 0; k < 16; k++) {
            double acc = 0, y1 = 0, y2 = 0; size_t a = (size_t)bar * k / 4, n = bar / 4;
            for (size_t i = a; i < a + n; i++) { double y = ph[i] + cw * y1 - rr * rr * y2; y2 = y1; y1 = y; if (i > a + 800) acc += y * y; }
            double r = sqrt(acc / n); if (r < lo) lo = r; if (r > hi) hi = r;
        }
        CHECK(hi > lo * 1.8, "the sweep carries a notch through 400 Hz (%.0f..%.0f)", lo, hi);
        g_fx.reso = 127; int32_t pk = 0; bool finite = true;
        for (int p8 = 0; p8 < 2; p8++) for (int c = 0; c <= 255; c += 15) {
            g_fx.poles = p8; g_fx.cutoff = c; sequencerStart(true); auto o = render(bar / 4); quiet();
            for (auto v : o) if (abs(v) > pk) pk = abs(v);
        }
        CHECK(finite && pk <= 26000, "full feedback, 4 and 8 stages: never past the clip ceiling (%d)", pk);
        g_fx.type = FLT_LP; g_fx.poles = 1; g_stemMask = STEM_ALL;
    }

    SECTION("wavetables: STRINGS and SUPERSAW");
    {
        CHECK(g_numWavetables == NUM_BUILTIN_WT && !strcmp(g_wtNames[8], "STRINGS") && !strcmp(g_wtNames[9], "SUPERSAW"),
              "%u built-in tables, last two %s %s", g_numWavetables, g_wtNames[8], g_wtNames[9]);
        double ratio[3]; float peak = 0; const uint8_t tabs[3] = { 1, 8, 9 };
        for (int t = 0; t < 3; t++) {
            SynthVoice v; v.init(); v.oscMode = OSC_WT; v.wtIndex = tabs[t];
            v.fltCutoff = 0.8f; v.fltEnvAmt = 0; v.fltReso = 0; v.ampDecRate = 1.0f; v.filtDecRate = 1.0f;
            v.noteOn(220.0f, false, false);
            double lo = 1e9, hi = 0;
            for (int win = 0; win < 40; win++) {               // 2 s in 50 ms windows
                double acc = 0;
                for (int i = 0; i < SAMPLE_RATE / 20; i++) { float o = v.render(); acc += o * o; if (fabsf(o) > peak) peak = fabsf(o); }
                double r = sqrt(acc / (SAMPLE_RATE / 20)); if (r < lo) lo = r; if (r > hi) hi = r;
            }
            ratio[t] = hi / lo;
        }
        CHECK(ratio[0] < 1.02, "a plain table holds still (%.3f)", ratio[0]);
        CHECK(ratio[1] > 1.1 && ratio[2] > 1.2, "strings and supersaw beat against themselves (%.2f %.2f)", ratio[1], ratio[2]);
        CHECK(peak < 1.6f, "unison stays in range (peak %.2f)", peak);
    }

    SECTION("bounce: mix + stems, bar aligned, equal length");
    {
        freshProject(); loadDemoPattern(); g_songLoopStart = 0;
        int take = storageBounce(nullptr);
        CHECK(take == 1, "bounce take %d", take);
        const char* names[] = { "mix", "drums", "bass", "decks" };
        uint32_t sizes[4] = {0};
        for (int i = 0; i < 4; i++) {
            char path[80]; snprintf(path, sizeof(path), "%s/T01_122bpm_%s.wav", DIR_BOUNCE, names[i]);
            File f = SD.open(path); if (f) { sizes[i] = f.size(); f.close(); }
        }
        uint32_t want = (uint32_t)(stepLen() * 16 * 7 + 1.5f * SAMPLE_RATE) * 2;
        CHECK(sizes[0] > 0 && sizes[0] == sizes[1] && sizes[1] == sizes[2] && sizes[2] == sizes[3], "all files same length (%u %u %u %u)", sizes[0], sizes[1], sizes[2], sizes[3]);
        CHECK(abs((int)sizes[0] - 44 - (int)want) < 2048, "7 bars + tail (%u vs %u)", sizes[0], want);
        CHECK(!SD.exists("/groovebox/bounce/T01_122bpm_synth.wav"), "silent synth stem removed");
        CHECK(!g_songOneShot && g_stemMask == STEM_ALL && g_songMode, "state restored after bounce");
    }

    printf("\n%d passed, %d failed\n", s_pass, s_fail);
    return s_fail ? 1 : 0;
}
