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

// ---------- pattern memory ----------
// A pattern owns no steps of its own: it names up to MAX_BARS bars in one
// shared pool, and a bar is taken only when something is written into it. An
// empty pattern costs 8 bytes, so there can be many of them, and long ones,
// without the RAM a fixed array would pin (the sample arena gets the rest).
// A synth step is 3 bytes: three notes of 4 + 3 bits, accent, slide.
#define CELL_BYTES 3
struct PatBar {
    uint8_t  cell[NUM_SYNTHS][STEPS_PER_BAR][CELL_BYTES];
    uint8_t  drums[STEPS_PER_BAR];             // bitmask, bit n = lane n
    uint8_t  drumAcc[STEPS_PER_BAR];           // accent bitmask
    uint8_t  chop[NUM_DECKS][STEPS_PER_BAR];   // deck A/B slice steps
    uint8_t  lock[STEPS_PER_BAR];              // per-step cutoff lock, 0..255
    uint16_t lockSet;                          // bit n = step n has a lock
};
extern PatBar       g_bars[NUM_BARS];
extern const PatBar g_noBar;                   // what an untaken bar reads as: silence

struct Pattern {
    uint8_t   bar[MAX_BARS];                // pool index + 1, 0 = nothing written there yet
    uint8_t   length;                       // steps: 16, 32, 48 or 64 (anything else reads as 16)
    uint8_t   spare;
    uint16_t  mute;                         // MUTE_*: one bit per grid row
    Pattern& operator=(const Pattern&) = delete;   // would share the bars: use pastePattern
};
// One bit per grid row: T1 T2 T3, deck A B, drum lanes 1..8. A pattern has a
// set and so has every song slot; song mode decides which one is in force.
#define MUTE_SYNTH(s)  (1u << (s))
#define MUTE_DECK(d)   (1u << (NUM_SYNTHS + (d)))
#define MUTE_LANE(l)   (1u << (NUM_SYNTHS + NUM_DECKS + (l)))
#define MUTE_ALL_LANES (0xFFu << (NUM_SYNTHS + NUM_DECKS))
#define MUTE_MASK      ((1u << (NUM_SYNTHS + NUM_DECKS + NUM_DRUM_LANES)) - 1)
static inline uint8_t patLen(const Pattern& p) {
    return (p.length && p.length <= NUM_STEPS && p.length % STEPS_PER_BAR == 0) ? p.length : STEPS_PER_BAR;
}

// ---- reading a step: never takes memory ----
static inline const PatBar& patBar(const Pattern& p, uint8_t st) {
    uint8_t h = p.bar[(st / STEPS_PER_BAR) % MAX_BARS];
    return h ? g_bars[h - 1] : g_noBar;
}
static inline SynthCell patCell(const Pattern& p, uint8_t track, uint8_t st) {
    const uint8_t* b = patBar(p, st).cell[track][st % STEPS_PER_BAR];
    uint32_t v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16);
    SynthCell c;
    for (int i = 0; i < MAX_POLY; i++) { c.note[i] = v & 15; c.oct[i] = (v >> 4) & 7; v >>= 7; }
    c.accent = v & 1; c.slide = v & 2;
    return c;
}
static inline uint8_t patDrums(const Pattern& p, uint8_t st)   { return patBar(p, st).drums[st % STEPS_PER_BAR]; }
static inline uint8_t patDrumAcc(const Pattern& p, uint8_t st) { return patBar(p, st).drumAcc[st % STEPS_PER_BAR]; }
static inline uint8_t patChop(const Pattern& p, uint8_t d, uint8_t st) { return patBar(p, st).chop[d][st % STEPS_PER_BAR]; }
static inline bool    patLocked(const Pattern& p, uint8_t st)  { return patBar(p, st).lockSet & (1u << (st % STEPS_PER_BAR)); }
static inline uint8_t patLock(const Pattern& p, uint8_t st)    { return patBar(p, st).lock[st % STEPS_PER_BAR]; }
bool patAnyLock(const Pattern& p);          // any cutoff lock inside the pattern's length

// ---- writing a step: takes a bar the first time something lands in it ----
// These return false when the pool is full (g_patFull is raised for the UI);
// writing "nothing" into an untaken bar takes no memory and always succeeds.
bool patSetCell(Pattern& p, uint8_t track, uint8_t st, const SynthCell& c);
bool patSetDrums(Pattern& p, uint8_t st, uint8_t hits, uint8_t acc);
bool patSetChop(Pattern& p, uint8_t d, uint8_t st, uint8_t v);
bool patSetLock(Pattern& p, uint8_t st, uint8_t v);
void patClearLock(Pattern& p, uint8_t st);
bool patEqual(const Pattern& a, const Pattern& b);   // same length, mutes and steps
uint8_t patBarsFree();                      // bars left in the pool
extern volatile bool g_patFull;             // a write found the pool full: the loop reports it once

// song slot: a pattern, how many times it plays, who is silent, where the filter ends up
#define TARGET_NONE     255   // so targets run 0..254
#define MAX_REPEATS     16

struct SongSlot {
    uint8_t pattern;      // SONG_EMPTY = empty
    uint8_t repeats;      // 1..MAX_REPEATS
    uint16_t mute;        // MUTE_*: in song mode these replace the pattern's
    uint8_t target;       // cutoff reached at the end of the slot, TARGET_NONE = leave it
    uint8_t spare;        // keeps the struct free of padding (it is hashed and compared whole)
};

// ---------- Shared state (defined in sequencer.cpp) ----------
extern Pattern    g_patterns[NUM_PATTERNS];
extern SongSlot   g_song[SONG_LENGTH];
extern uint8_t    g_songLoopStart;
extern uint8_t    g_songCursor;
extern Page       g_curPage;            // page on screen (defined here so the host tests link)
extern int8_t     g_songTag;            // SONG page: slot tagged by CPY, -1 = none

extern SynthTrack g_synths[NUM_SYNTHS];
extern DrumLane   g_drumLanes[NUM_DRUM_LANES];

// transport (written by the audio thread)
extern volatile bool    g_playing;
extern volatile uint8_t g_playStep;       // next step to fire
extern volatile uint8_t g_lastStep;       // step sounding now
extern volatile uint8_t g_playPattern;    // pattern currently sounding
extern volatile uint8_t g_songPos;
extern volatile uint8_t g_slotRep;        // repeat counter inside the song slot
extern volatile uint16_t g_slotMute;      // mute mask of the playing slot (song mode)
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
extern uint8_t    g_nextPattern;    // pattern the transport takes when this one ends
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
uint16_t seqEditMask();             // the set the mute keys edit and the screens show (the SONG page: the slot's)
uint16_t seqMuteMask();             // MUTE_* in force now: the song slot's in song mode, else the pattern's
// Flip one row where seqMuteMask() reads it. Returns the slot + 1 it changed,
// 0 for a pattern (*pat says which), -1 when song mode has no slot to change.
int   seqToggleMute(uint8_t row, uint8_t* pat, bool* nowMuted);
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
void pastePattern(uint8_t src, uint8_t dst);
void seqSelectPattern(uint8_t idx); // at once when stopped (or in song mode), else queued to the pattern's end
bool seqFollowPattern();            // core 1, every pass: the edit view moves with a queued switch
void triggerLaneLive(uint8_t lane); // audition a lane (choke-aware, no record)
void setPatternLength(uint8_t idx, uint8_t len);   // 16..64 steps; growing repeats the bars it has, shrinking frees the rest
void songClearSlot(uint8_t i);

// step editing helpers
void toggleDrumAtCursor(uint8_t lane);
void writeSynthAtCursor(uint8_t note, uint8_t octave, bool accent);
void clearCellAtCursor();
void clearStepAtPlayhead();         // held-delete erase while recording

bool loadDemoPattern();             // styles.cpp: the FILTRE miniature
