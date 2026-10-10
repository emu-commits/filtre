// ============================================================
// FILTRE - sequencer.h
// Patterns, transport, step triggering, song chain, live record.
// The step clock runs inside the audio render (sample-accurate, so
// swing is exact and screen redraws cannot move a hit). Core 1 talks
// to it through a small event queue.
// ============================================================
#pragma once
#include "config.h"
#include "synth_voice.h"
#include "drum_voice.h"
#include "deck.h"

// Up to MAX_POLY notes per step. Slot 0 is "the" note for mono
// tracks; slots 1..2 are chord notes on poly tracks.
struct SynthCell {
    uint8_t note[MAX_POLY];   // 0 = empty, 1..12 = C..B
    uint8_t oct [MAX_POLY];   // 1..7
    bool    accent;
    bool    slide;            // played back only when track VOICES == 1

    inline bool empty() const { return note[0] == NOTE_EMPTY; }
    inline void clear(uint8_t defOct) {
        for (int i = 0; i < MAX_POLY; i++) { note[i] = NOTE_EMPTY; oct[i] = defOct; }
        accent = false; slide = false;
    }
    inline void setMono(uint8_t n, uint8_t o, bool a, bool s) {
        clear(o); note[0] = n; oct[0] = o; accent = a; slide = s;
    }
    // add a chord note; returns slot used (replaces last slot when full)
    inline uint8_t add(uint8_t n, uint8_t o) {
        for (int i = 0; i < MAX_POLY; i++)                 // same note: re-write
            if (note[i] == n && oct[i] == o) return i;
        for (int i = 0; i < MAX_POLY; i++)
            if (note[i] == NOTE_EMPTY) { note[i] = n; oct[i] = o; return i; }
        note[MAX_POLY - 1] = n; oct[MAX_POLY - 1] = o;
        return MAX_POLY - 1;
    }
};

// A synth track = 1..MAX_POLY identical voices sharing parameters.
// voices == 1 -> exact legacy mono/303 behavior incl. slide+legato.
struct SynthTrack {
    SynthVoice v[MAX_POLY];
    uint8_t    voices;        // 1..MAX_POLY
    uint8_t    rr;            // round-robin cursor

    void init() { for (int i = 0; i < MAX_POLY; i++) v[i].init(); voices = 1; rr = 0; }

    // apply a parameter edit to every voice (params are per-track)
    template <typename F> void forEach(F f) { for (int i = 0; i < MAX_POLY; i++) f(v[i]); }

    void setVoices(uint8_t n) {
        if (n < 1) n = 1; if (n > MAX_POLY) n = MAX_POLY;
        voices = n;
        for (int i = n; i < MAX_POLY; i++) v[i].active = false;  // kill spares
        if (n > 1) rr %= n;
    }

    // mono path keeps slide semantics; poly allocates a voice
    void noteOn(float freq, bool acc, bool slide) {
        if (voices <= 1) { v[0].noteOn(freq, acc, slide); return; }
        int pick = -1;
        for (int i = 0; i < voices; i++)                   // free voice first
            if (!v[i].active) { pick = i; break; }
        if (pick < 0) {                                    // steal quietest
            float amp = 1e9f; 
            for (int i = 0; i < voices; i++)
                if (v[i].ampEnv < amp) { amp = v[i].ampEnv; pick = i; }
        }
        rr = (uint8_t)((pick + 1) % voices);
        v[pick].noteOn(freq, acc, false);                  // no slide in poly
    }

    inline float render() {
        if (voices <= 1) return v[0].render();
        float s = 0;
        for (int i = 0; i < voices; i++) s += v[i].render();
        return s * (voices > 2 ? 0.62f : 0.75f);           // headroom vs. clip
    }
};

// chop step: 0 = empty, else slice+1 in the low 5 bits
#define CHOP_SLICE(c)  ((c) & 0x1F)
#define CHOP_ACC       0x20
#define CHOP_REV       0x40

struct Pattern {
    SynthCell synth[NUM_SYNTHS][NUM_STEPS];
    uint8_t   drums[NUM_STEPS];             // bitmask, bit n = lane n
    uint8_t   drumAcc[NUM_STEPS];           // accent bitmask
    uint8_t   chop[NUM_DECKS][NUM_STEPS];   // deck A/B slice steps
    uint8_t   lock[NUM_STEPS];              // per-step cutoff lock, 0..255
    uint32_t  lockSet;                      // bit n = step n has a lock
    uint8_t   length;                       // 16 or 32 (0 reads as 16)
};
static inline uint8_t patLen(const Pattern& p) { return p.length == NUM_STEPS ? NUM_STEPS : STEPS_PER_BAR; }

// song slot: a pattern, how many times it plays, who is silent, where the filter ends up
#define SLOT_MUTE_T1    0x01
#define SLOT_MUTE_T2    0x02
#define SLOT_MUTE_T3    0x04
#define SLOT_MUTE_DRUMS 0x08
#define SLOT_MUTE_DECKA 0x10
#define SLOT_MUTE_DECKB 0x20
#define SLOT_MUTE_KICK  0x40
#define SLOT_MUTE_BITS  7
#define TARGET_NONE     255   // so targets run 0..254
#define MAX_REPEATS     16

struct SongSlot {
    uint8_t pattern;      // SONG_EMPTY = empty
    uint8_t repeats;      // 1..MAX_REPEATS
    uint8_t mute;         // SLOT_MUTE_*
    uint8_t target;       // cutoff reached at the end of the slot, TARGET_NONE = leave it
};

// ---------- Shared state (defined in sequencer.cpp) ----------
extern Pattern    g_patterns[NUM_PATTERNS];
extern SongSlot   g_song[SONG_LENGTH];
extern uint8_t    g_songLoopStart;

extern SynthTrack g_synths[NUM_SYNTHS];
extern DrumLane   g_drumLanes[NUM_DRUM_LANES];
extern bool       g_synthMute[NUM_SYNTHS];
extern bool       g_drumMute;

// transport (written by the audio thread)
extern volatile bool    g_playing;
extern volatile uint8_t g_playStep;       // next step to fire
extern volatile uint8_t g_lastStep;       // step sounding now
extern volatile uint8_t g_playPattern;    // pattern currently sounding
extern volatile uint8_t g_songPos;
extern volatile uint8_t g_slotRep;        // repeat counter inside the song slot
extern volatile uint8_t g_slotMute;       // mute mask of the playing slot (song mode)
extern volatile bool    g_songEnded;      // one-shot song reached its end (bounce)
extern bool       g_recEnabled;
extern bool       g_songMode;
extern bool       g_songOneShot;          // stop instead of looping (bounce)
extern uint16_t   g_bpm;
extern volatile bool g_cutHeld;           // a cutoff key is down
extern uint8_t    g_stemMask;             // which groups reach the mix (bounce)
#define STEM_DRUMS 1
#define STEM_BASS  2
#define STEM_SYNTH 4
#define STEM_DECKS 8
#define STEM_ALL   15

// edit state
extern uint8_t    g_curPattern;     // pattern being edited/viewed
extern uint8_t    g_curTrack;       // 0..2 synth, 3 = drums, 4/5 = deck A/B
extern uint8_t    g_curStep;
extern uint8_t    g_curDrumLane;    // selected lane 0..7
extern uint8_t    g_curOctave;

static inline bool trackIsDeck(uint8_t t) { return t == TRK_DECK_A || t == TRK_DECK_B; }
static inline uint8_t trackDeck(uint8_t t) { return (uint8_t)(t - TRK_DECK_A); }

void sequencerInit();
void sequencerStart(bool fromTop);
void sequencerStop();

// ---------- audio thread ----------
void seqDrainEvents();              // once per buffer
void seqFireStep();                 // internal: called by seqTick
extern uint32_t g_seqNow, g_seqNextAt;
static inline void seqTick() {      // once per sample
    if (g_playing) { if (g_seqNow == g_seqNextAt) seqFireStep(); g_seqNow++; }
}
float seqCutoffTick();              // once per sample: effective cutoff 0..255
bool  seqSynthMuted(uint8_t s);
bool  seqDrumsMuted();
bool  seqDeckMuted(uint8_t d);
void  engineStopSampleVoices();     // arena is about to move

// Live input -> sound + optional record
void liveSynthNote(uint8_t track, uint8_t note, uint8_t octave, bool accent, bool legato);
void liveDrumHit(uint8_t lane, bool accent);
void liveDeckHit(uint8_t deck, uint8_t slice, bool accent);
void auditionSynth(uint8_t track, uint8_t note, uint8_t octave, bool accent);
void auditionSample(int slot);
void touchCutoff();                 // the base cutoff was just moved by hand

// pattern + lane helpers
void clearPattern(uint8_t idx);
void clonePatternTo(uint8_t dst);   // copy current pattern into slot dst
void triggerLaneLive(uint8_t lane); // audition a lane (choke-aware, no record)
void setPatternLength(uint8_t idx, uint8_t len);   // 16 <-> 32 (duplicates bar 1 when growing)
void songClearSlot(uint8_t i);

// step editing helpers
void toggleDrumAtCursor(uint8_t lane);
void writeSynthAtCursor(uint8_t note, uint8_t octave, bool accent);
void clearCellAtCursor();
void clearStepAtPlayhead();         // held-delete erase while recording

void loadDemoPattern();             // styles.cpp: the FILTRE miniature
