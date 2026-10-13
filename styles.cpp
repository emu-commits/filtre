// ============================================================
// FILTRE - styles.cpp
// ============================================================
#include "styles.h"
#include "sequencer.h"
#include "sampler.h"
#include "audio_engine.h"
#include <string.h>

const char* const g_styleNames[STYLE_COUNT]   = { "FILTRE", "NU-DISCO", "HOMEWORK", "BANGER" };
const char* const g_bassModeNames[BASS_COUNT] = { "OCTAVE", "OFFBEAT", "FIFTH" };
const char* const g_keyNames[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

// lane order used by every style
enum { L_KICK = 0, L_CLAP, L_CHAT, L_OHAT, L_SHAKER, L_RIM, L_SNARE, L_KICK8 };

void styleKit() {
    g_drumLanes[L_KICK  ].init(ENG_909, DT_KICK);
    g_drumLanes[L_CLAP  ].init(ENG_909, DT_CLAP9);
    g_drumLanes[L_CHAT  ].init(ENG_909, DT_HAT_C);
    g_drumLanes[L_OHAT  ].init(ENG_909, DT_HAT_O_OR_CLAP);
    g_drumLanes[L_SHAKER].init(ENG_909, DT_HAT_C);
    g_drumLanes[L_RIM   ].init(ENG_909, DT_RIM);
    g_drumLanes[L_SNARE ].init(ENG_909, DT_SNARE);
    g_drumLanes[L_KICK8 ].init(ENG_808, DT_KICK);
    g_drumLanes[L_CLAP ].volume = 0.70f;
    g_drumLanes[L_CHAT ].volume = 0.42f;
    g_drumLanes[L_OHAT ].volume = 0.40f; g_drumLanes[L_OHAT].decay = 0.7f;
    g_drumLanes[L_CHAT ].chokeGroup = 1; g_drumLanes[L_OHAT].chokeGroup = 1;   // closed cuts open
    g_drumLanes[L_SHAKER].volume = 0.26f; g_drumLanes[L_SHAKER].decay = 0.55f; g_drumLanes[L_SHAKER].tune = 5.0f;
    g_drumLanes[L_RIM  ].volume = 0.50f;
    g_drumLanes[L_SNARE].volume = 0.55f;
    g_drumLanes[L_KICK8].volume = 0.70f;
}

struct StyleDef {
    uint16_t bpm; uint8_t swing, pump; uint16_t release;
    uint8_t  cutoff, reso;
    uint16_t steps[6];     // kick, clap, chat, ohat, shaker, rim: bit n = step n
    uint16_t accent[6];
    int8_t   bass;         // BassMode, or -1 = leave T1 alone
};

// 16th-note bit helpers: b(1) = step 1
#define ST(n) (1u << ((n) - 1))
#define FOUR   (ST(1) | ST(5) | ST(9) | ST(13))
#define BACK   (ST(5) | ST(13))
#define EIGHTH (ST(1) | ST(3) | ST(5) | ST(7) | ST(9) | ST(11) | ST(13) | ST(15))
#define OFF8   (ST(3) | ST(7) | ST(11) | ST(15))
#define OFF16  (ST(2) | ST(4) | ST(6) | ST(8) | ST(10) | ST(12) | ST(14) | ST(16))
#define GHOST  (ST(4) | ST(8) | ST(12) | ST(16))        // the swung 16ths

static const StyleDef k_styles[STYLE_COUNT] = {
    // FILTRE: four on the floor, clap on 2 and 4, 8th hats with swung ghosts, open hat on the offbeat
    { 122, 62, 70, 180,  77, 44,
      { FOUR, BACK, EIGHTH | GHOST, ST(7) | ST(15), 0, 0 },
      { 0,    0,    OFF8,           0,              0, 0 }, -1 },
    // NU-DISCO: softer, shaker carries the swing, one open hat
    { 118, 68, 40, 180,  96, 36,
      { FOUR, BACK, EIGHTH, ST(15), OFF16, 0 },
      { 0,    0,    OFF8,   0,      GHOST, 0 }, -1 },
    // HOMEWORK: FILTRE drums, more resonance, octave-pump bass
    { 126, 54, 80, 180,  77, 70,
      { FOUR, BACK, EIGHTH | GHOST, ST(7) | ST(15), 0, 0 },
      { 0,    0,    OFF8,           0,              0, 0 }, BASS_OCTAVE },
    // BANGER: loud clap, 16th hats, short release
    { 128, 58, 65, 120,  90, 50,
      { FOUR, BACK, EIGHTH | OFF16, ST(7) | ST(15), 0, ST(11) | ST(16) },
      { FOUR, BACK, OFF8,           0,              0, 0 }, -1 },
};

// one step of a style's drums: hits and accents as lane bits
static void styleStep(const StyleDef& s, int st, uint8_t* m, uint8_t* a) {
    *m = *a = 0;
    for (int l = 0; l < 6; l++) {
        if (s.steps[l]  & (1u << (st % STEPS_PER_BAR))) *m |= (1 << l);
        if (s.accent[l] & (1u << (st % STEPS_PER_BAR))) *a |= (1 << l);
    }
}

// A style may freely replace drums that another style wrote. Hand-edited
// drums, or a T1 line about to be replaced by the style's bass, need a hold.
bool styleNeedsConfirm(uint8_t style, uint8_t pattern) {
    if (style >= STYLE_COUNT) return false;
    const Pattern& p = g_patterns[pattern % NUM_PATTERNS];
    const uint8_t len = patLen(p);
    bool empty = true, fromStyle = false;
    for (int st = 0; st < len; st++) if (patDrums(p, st)) empty = false;
    if (!empty) {
        for (int k = 0; k < STYLE_COUNT && !fromStyle; k++) {
            fromStyle = true;
            for (int st = 0; st < len && fromStyle; st++) {
                uint8_t m, a; styleStep(k_styles[k], st, &m, &a);
                fromStyle = patDrums(p, st) == m && patDrumAcc(p, st) == (a & m);
            }
        }
        if (!fromStyle) return true;
    }
    if (k_styles[style].bass >= 0)
        for (int st = 0; st < len; st++) if (!patCell(p, 0, st).empty()) return true;
    return false;
}

void writeBass(uint8_t pattern, uint8_t mode) {
    Pattern& p = g_patterns[pattern % NUM_PATTERNS];
    uint8_t root = (uint8_t)(g_fx.key % 12);            // 0 = C
    uint8_t oct  = (root <= 4) ? 2 : 1;                 // keep the root between F1 and E2
    uint8_t n1 = (uint8_t)(root + 1);
    uint8_t fifth = (uint8_t)((root + 7) % 12 + 1), fifthOct = (uint8_t)(oct + (root + 7) / 12);
    uint8_t len = patLen(p);

    for (int st = 0; st < len; st++) patSetCell(p, 0, (uint8_t)st, SynthCell{});
    auto put = [&](int st, uint8_t n, uint8_t o, bool acc, bool slide) {
        SynthCell c; c.setMono(n, o, acc, slide);
        patSetCell(p, 0, (uint8_t)st, c);
    };
    for (int bar = 0; bar < len; bar += STEPS_PER_BAR) {
        switch (mode) {
            case BASS_OFFBEAT:
                put(bar + 2, n1, oct, true, false);  put(bar + 6, n1, oct, false, false);
                put(bar + 10, n1, oct, true, false); put(bar + 14, n1, oct, false, false);
                break;
            case BASS_FIFTH:
                put(bar + 0, n1, oct, true, false);  put(bar + 4, n1, oct, false, false);
                put(bar + 8, fifth, fifthOct, false, true);
                put(bar + 12, n1, oct, false, false);
                break;
            default:   // OCTAVE: root, octave slid into, root
                put(bar + 0, n1, oct, true, false);  put(bar + 2, n1, (uint8_t)(oct + 1), false, true);
                put(bar + 4, n1, oct, false, false);
                put(bar + 8, n1, oct, true, false);  put(bar + 10, n1, (uint8_t)(oct + 1), false, true);
                put(bar + 12, n1, oct, false, false);
                break;
        }
    }
    g_synths[0].setVoices(1);                           // slides need the mono 303
}

static void writeDrums(Pattern& p, const StyleDef& s) {
    uint8_t len = patLen(p);
    for (int st = 0; st < len; st++) {
        uint8_t m, a; styleStep(s, st, &m, &a);
        patSetDrums(p, (uint8_t)st, m, a);
    }
}

void styleApply(uint8_t style, uint8_t pattern) {
    if (style >= STYLE_COUNT) return;
    const StyleDef& s = k_styles[style];
    Pattern& p = g_patterns[pattern % NUM_PATTERNS];
    writeDrums(p, s);
    g_bpm = s.bpm;
    g_fx.swing = s.swing; g_fx.pump = s.pump; g_fx.release = s.release;
    g_fx.type = FLT_LP;   g_fx.cutoff = s.cutoff; g_fx.reso = s.reso;
    g_fx.style = style;
    if (s.bass >= 0) { g_fx.bassMode = (uint8_t)s.bass; writeBass(pattern, g_fx.bassMode); }
}

void filtreDefaults() {
    fxDefaults();
    styleKit();
    // T1: mono 303 bass, saw, low and short
    g_synths[0].setVoices(1);
    g_synths[0].forEach([](SynthVoice& v){ v.oscMode = OSC_SAW; v.fltCutoff = 0.22f; v.fltReso = 0.40f;
                                           v.fltEnvAmt = 0.45f; v.ampDecRate = 0.99975f; v.volume = 0.70f; });
    // T2: 3-voice stab, short
    g_synths[1].setVoices(3);
    g_synths[1].forEach([](SynthVoice& v){ v.oscMode = OSC_SQR; v.fltCutoff = 0.45f; v.fltReso = 0.25f;
                                           v.ampDecRate = 0.99950f; v.volume = 0.5f; });
    styleApply(STYLE_FILTRE, 0);
}

// The FILTRE miniature: closed filter -> bass in -> wide open -> back down.
bool loadDemoPattern() {
    engineLock();
    for (int p = 0; p < 4; p++) { clearPattern(p); setPatternLength(p, STEPS_PER_BAR); }
    filtreDefaults();
    for (int p = 1; p < 4; p++) pastePattern(0, p);

    // factory loop on deck A, bar-locked
    if (!deckLoaded(0)) {
        int slot = samplerAddExternal("FILTRE-DEMO", g_factoryLoop, g_factoryLoopFrames,
                                      SAMPLE_RATE, FMT_ULAW, OWN_DECK_A);
        if (slot >= 0 && deckAdopt(0, slot)) {
            g_decks[0].mode = DECK_LOOP;
            g_decks[0].fitBars = deckGuessBars(0, g_bpm, true);
        }
    }

    writeBass(1, BASS_OCTAVE);
    writeBass(2, BASS_OCTAVE);
    // pattern 3 opens up: 16th hats
    for (int st = 0; st < STEPS_PER_BAR; st++)
        patSetDrums(g_patterns[2], (uint8_t)st, patDrums(g_patterns[2], st) | (1 << L_CHAT), patDrumAcc(g_patterns[2], st));

    for (int i = 0; i < SONG_LENGTH; i++) songClearSlot(i);
    g_song[0] = { 0, 2, MUTE_LANE(0), 50 };      // filter nearly shut, no kick (it still pumps)
    g_song[1] = { 1, 2, 0, 140 };                // kick + bass, opening
    g_song[2] = { 2, 2, 0, 254 };                // wide open
    g_song[3] = { 3, 1, MUTE_SYNTH(0), 51 };      // back down, loop to slot 2
    g_songLoopStart = 1;
    g_fx.cutoff = 36;
    g_curPattern = g_nextPattern = 0;
    g_songMode = true;
    engineUnlock();
    return deckLoaded(0);
}
