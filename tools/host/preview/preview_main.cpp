// Renders every FILTRE page for both screens to PPM files (tools/host/build/shots).
#include <M5Cardputer.h>
#include "SD.h"
#include "config.h"
#include "ui.h"
#include "lid_ui.h"
#include "sequencer.h"
#include "sampler.h"
#include "styles.h"
#include "storage.h"
#include "wavetable.h"
#include "audio_engine.h"
#include <vector>

HostCardputer M5Cardputer;
HostEsp ESP;
bool g_sdOk = true;
uint32_t g_fakeMs = 100000;
lgfx::LGFX_Sprite* lidHostSprite();

static void dump(lgfx::LGFX_Sprite& s, const char* name) {
    char path[128]; snprintf(path, sizeof(path), "build/shots/%s.ppm", name);
    FILE* f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", (int)s.width(), (int)s.height());
    for (int y = 0; y < s.height(); y++)
        for (int x = 0; x < s.width(); x++) {
            uint16_t c = s.readPixel(x, y);       // RGB565
            uint8_t rgb[3] = { (uint8_t)((c >> 11) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31) };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}
static void shot(const char* name) {
    uiDraw(); lidUpdate(true);
    char n[64];
    snprintf(n, sizeof(n), "%s_int", name); dump(M5Cardputer.Display, n);
    snprintf(n, sizeof(n), "%s_lid", name); dump(*lidHostSprite(), n);
}
static void run(uint32_t n) { std::vector<int16_t> b(n); engineRender(b.data(), (int)n, 1.0f); }

int main() {
    M5Cardputer.Display.setColorDepth(16);
    M5Cardputer.Display.createSprite(SCREEN_W, SCREEN_H);
    uiInit(); lidInit();
    samplerInit(); wavetableInitBuiltins(); samplerEnsureDirs();
    sequencerInit(); deckInit(); filtreDefaults();

    lidBootScreen(); uiSplash();
    dump(M5Cardputer.Display, "00_boot_int"); dump(*lidHostSprite(), "00_boot_lid");

    // a worked project: demo song, deck A chopped with a recorded phrase, deck B looping, a filter sweep
    loadDemoPattern();
    deckLoad(1, "FILTRE-DEMO.wav");
    g_decks[1].mode = DECK_LOOP; g_decks[1].fitBars = 2;
    strncpy(g_decks[1].name, "A-very-long-disco-sample-name-1979.wav", LOOP_NAME_LEN - 1);
    g_decks[0].mode = DECK_CHOP; g_decks[0].lastSlice = 3;
    Pattern& p = g_patterns[0];
    const uint8_t phrase[16] = {1,0,0,3, 0,5,0,0, 2,0,16,0, 7,0,10,0};
    for (int i = 0; i < 16; i++) { p.chop[0][i] = phrase[i] | (i % 8 == 0 ? CHOP_ACC : 0); p.lock[i] = (uint8_t)(i * 16); p.lockSet |= 1u << i; }
    writeBass(0, BASS_OCTAVE);
    p.synth[2][0].setMono(10, 3, false, false); p.synth[2][0].add(1, 4); p.synth[2][0].add(5, 4);
    g_songMode = false; g_recEnabled = true; g_curTrack = TRK_DECK_A; g_curStep = 5;
    sequencerStart(true); run(30000);
    g_curPage = PAGE_PATTERN; shot("01_pattern_deck");

    setPatternLength(0, NUM_STEPS); g_curStep = 21; g_curTrack = TRK_DRUMS; g_curDrumLane = 2;
    uiStatus("PASTE P1>P3: You sure?"); shot("02_pattern_bar2_status");
    g_fakeMs += 5000;
    g_holdProg = 0.6f; strcpy(g_holdLabel, "WRITE BASS"); shot("03_pattern_hold"); g_holdProg = 0;

    g_curPage = PAGE_SETTINGS; g_curTrack = TRK_DECK_A; g_soundParam = DR_MODE; shot("04_sound_deck_top");
    g_soundParam = DR_CUTOFF; shot("05_sound_deck_cutoff");
    g_soundParam = DR_SETBPM; g_curTrack = TRK_DECK_B; g_fx.reso = 110; g_fx.poles = 0; shot("06_sound_deckB_end");
    g_fx.type = FLT_HP; g_soundParam = DR_TYPE; shot("07_sound_hp");
    g_fx.type = FLT_PHASER; g_fx.poles = 1; shot("07b_sound_phaser"); g_soundParam = DR_POLES; shot("07c_sound_phaser_stages");
    g_fx.type = FLT_LP; g_fx.poles = 1; g_fx.reso = 44;
    g_curTrack = 0; g_soundParam = 2; shot("08_sound_synth");
    g_curTrack = TRK_DRUMS; g_curDrumLane = 0; g_soundParam = 2; shot("09_sound_drum");
    g_soundParam = DRUM_PARAMS; shot("09b_sound_drum_bpm");          // first shared row: BPM
    g_curTrack = TRK_DECK_A; g_soundParam = DR_SLICEPOS; shot("09c_sound_deck_slicepos");

    g_curPage = PAGE_SAMPLE; g_curTrack = TRK_DECK_A;
    { std::vector<float> dummy; File f = SD.open("/groovebox/loops/Earth-Wind-and-Something-Else-Guitar-Section-LONG.wav", FILE_WRITE); f.write((const uint8_t*)"x", 1); f.close(); }
    uiScanSampleDir(); shot("10_sample_loops");
    g_curTrack = TRK_DRUMS; uiScanSampleDir(); g_fileSel = 9; samplerLoad(g_fileList[2]); shot("11_sample_drums");

    g_curPage = PAGE_SONG; g_songMode = true; g_songCursor = 2;
    g_song[4] = { 7, 16, MUTE_MASK, 0 }; g_song[5] = { 3, 8, MUTE_SYNTH(0) | MUTE_DECK(1) | MUTE_LANE(5), 254 };
    for (int i = 6; i < 40; i++) g_song[i] = { (uint8_t)(i % 8), (uint8_t)(1 + i % 5), (uint16_t)(i % 3 ? 0 : MUTE_LANE(0)), (uint8_t)(i % 4 ? i * 6 : TARGET_NONE) };
    shot("12_song"); g_songCursor = 4; shot("13_song_slot_full");
    { File f = SD.open("/groovebox/songs/Around-A-Very-Long-Reference-Track-Name.arr", FILE_WRITE); f.write((const uint8_t*)"x", 1); f.close(); }
    g_arrList = true; uiScanSongDir(); g_fileSel = 0; shot("13b_song_arrangements"); g_arrList = false;

    // a question on both screens, at its longest
    g_curPage = PAGE_PATTERN; g_curTrack = TRK_DECK_A;
    uiStatusFor("HOMEWORK: Replace drums?", 4000); shot("14_confirm_question");
    g_fakeMs += 5000;

    // lid shut: the internal screen is the editor again
    lidToggle();
    g_curPage = PAGE_PATTERN; shot("24_nolid_pattern");
    g_curPage = PAGE_SETTINGS; g_soundParam = DR_BPM; shot("25_nolid_settings_deck");
    g_curTrack = 1; g_soundParam = 12; shot("26_nolid_settings_synth");
    lidToggle();

    // empty machine: no loops loaded, pattern page
    sequencerStop(); run(64);
    deckUnload(0); deckUnload(1); g_songMode = false; g_recEnabled = false;
    // the key map, in each context
    g_keysOverlay = true;
    g_patterns[g_curPattern].mute = MUTE_SYNTH(1) | MUTE_LANE(2);
    g_curPage = PAGE_PATTERN; g_curTrack = TRK_DRUMS; shot("17_keys_drums");
    g_curTrack = TRK_DECK_A; shot("18_keys_deck");
    g_curTrack = 0; shot("19_keys_synth");
    g_curPage = PAGE_SONG; shot("20_keys_song");
    g_curPage = PAGE_SAMPLE; g_curTrack = TRK_DECK_A; uiScanSampleDir(); shot("21_keys_sample_loops");
    g_curTrack = TRK_DRUMS; uiScanSampleDir(); shot("22_keys_sample_drums");
    g_curPage = PAGE_SETTINGS; g_curTrack = 0; shot("23_keys_settings");
    g_keysOverlay = false;
    g_curPage = PAGE_PATTERN; g_curTrack = 0; g_curStep = 0; setPatternLength(0, STEPS_PER_BAR); shot("15_pattern_empty");
    g_curPage = PAGE_SETTINGS; g_curTrack = TRK_DECK_A; g_soundParam = 0; shot("16_sound_deck_empty");
    printf("shots written\n");
    return 0;
}
