// writes a v5 project (and the v4 cut of it) with the firmware as committed at 5e62861
#include "SD.h"
#include "config.h"
#include "sequencer.h"
#include "sampler.h"
#include "styles.h"
#include "storage.h"
#include "wavetable.h"
#include "audio_engine.h"
#include <vector>
#include <string>
bool g_needRedraw = false;
int main() {
    samplerInit(); wavetableInitBuiltins(); samplerEnsureDirs();
    sequencerInit(); deckInit(); filtreDefaults();
    g_patterns[2].mute = MUTE_SYNTH(1);
    for (int i = 0; i < SONG_LENGTH; i++) g_song[i] = { 2, 3, 0x1F00, 77 };
    g_song[1].mute = MUTE_LANE(7) | MUTE_DECK(0) | MUTE_SYNTH(2);
    // pattern 2 (index 1): two bars, a chord, a slide, chops, a lock at the very end
    setPatternLength(1, 32);
    g_patterns[1].synth[2][20].setMono(10, 3, true, false); g_patterns[1].synth[2][20].add(1, 4); g_patterns[1].synth[2][20].add(5, 7);
    g_patterns[1].synth[0][3].setMono(12, 2, false, true);
    g_patterns[1].chop[0][20] = 4 | CHOP_ACC; g_patterns[1].chop[1][31] = 16 | CHOP_REV;
    g_patterns[1].lock[31] = 200; g_patterns[1].lockSet |= 1u << 31;
    g_patterns[1].drums[17] = 0x81; g_patterns[1].drumAcc[17] = 0x80;
    g_patterns[1].mute = MUTE_DECK(1);
    // pattern 21 (index 20) lives in the file's tail
    g_patterns[20].drums[5] = 0x24; g_patterns[20].drumAcc[5] = 0x04; g_patterns[20].mute = MUTE_LANE(3);
    g_patterns[31].synth[1][15].setMono(7, 5, false, false);
    g_bpm = 131; g_fx.key = 4; g_fx.reso = 99;
    if (!storageSaveProject(5)) return 1;
    std::string path = std::string(getenv("FILTRE_SD")) + DIR_PROJECTS + "/P6.gbx";
    std::vector<uint8_t> raw; { FILE* fp = fopen(path.c_str(), "rb"); int c; while ((c = fgetc(fp)) != EOF) raw.push_back((uint8_t)c); fclose(fp); }
    { FILE* fp = fopen("v5.gbx", "wb"); fwrite(raw.data(), 1, raw.size(), fp); fclose(fp); }
    size_t at = 0; int run = 0;
    for (size_t i = 0; i < raw.size(); i++) { run = (raw[i] == 0x1F) ? run + 1 : 0; if (run == 2) { at = i - 1; break; } }
    if (!at) return 2;
    raw.erase(raw.begin() + at, raw.begin() + at + SONG_LENGTH);
    raw[4] = 4; raw[5] = 0;
    raw[9 + 0 * 4 + 2] = 0x40 | 0x10;      // slot 1: old KICK + DECK A
    raw[9 + 1 * 4 + 2] = 0x08;             // slot 2: old DRUMS
    { FILE* fp = fopen("v4.gbx", "wb"); fwrite(raw.data(), 1, raw.size(), fp); fclose(fp); }
    printf("v5 %zu bytes\n", raw.size() + SONG_LENGTH);
    return 0;
}
