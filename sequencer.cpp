// ============================================================
// FILTRE - sequencer.cpp
// ============================================================
#include "sequencer.h"
#include "audio_engine.h"
#include <Arduino.h>
#include <string.h>

Pattern    g_patterns[NUM_PATTERNS];
PatBar     g_bars[NUM_BARS];
const PatBar g_noBar = {};
volatile bool g_patFull = false;
SongSlot   g_song[SONG_LENGTH];
uint8_t    g_songLoopStart = 0;
uint8_t    g_songCursor = 0;       // the slot the SONG page edits; stopped, the one whose mutes apply
Page       g_curPage    = PAGE_PATTERN;   // the page on screen (the mute keys read it)
int8_t     g_songTag    = -1;             // SONG page: the slot tagged by CPY, -1 = none yet

SynthTrack g_synths[NUM_SYNTHS];
DrumLane   g_drumLanes[NUM_DRUM_LANES];

volatile bool    g_playing     = false;
volatile uint8_t g_playStep    = 0;
volatile uint8_t g_lastStep    = 0;
volatile uint8_t g_playPattern = 0;
volatile uint8_t g_songPos     = 0;
volatile uint8_t g_slotRep     = 0;
volatile uint16_t g_slotMute   = 0;
volatile bool    g_songEnded   = false;
bool       g_recEnabled  = false;
bool       g_songMode    = false;
bool       g_songOneShot = false;
uint16_t   g_bpm         = 122;
volatile bool g_cutHeld  = false;
uint8_t    g_stemMask    = STEM_ALL;

uint8_t    g_curPattern  = 0;
uint8_t    g_nextPattern = 0;
uint8_t    g_curTrack    = 0;
uint8_t    g_curStep     = 0;
uint8_t    g_curDrumLane = 0;
uint8_t    g_curOctave   = 4;

extern bool g_needRedraw;   // owned by ui.cpp
extern volatile uint32_t g_rsmpArmFrames;   // mic_sampler.cpp (0 on host)

uint32_t g_seqNow = 0, g_seqNextAt = 0;

// The player hears a step about this long after the engine fired it (DMA
// queue + codec). Live recording subtracts it before quantising, or every
// hit would land late. Tune on hardware.
#define REC_LATENCY_SAMPLES 900

// ---------- clock state (audio thread only) ----------
static float    s_frac      = 0.0f;     // fractional part of g_seqNextAt
static uint32_t s_lastAt    = 0, s_prevAt = 0;
static int32_t  s_bar       = -1;       // bars since start
static uint16_t s_slotStep  = 0;        // steps played in the current song slot
static float    s_cut       = 77.0f, s_cutInc = 0.0f;
static uint32_t s_cutLeft   = 0;
static float    s_slotFrom  = 77.0f;    // cutoff when the song slot began
static bool     s_touch     = false;    // manual override of locks/targets
static uint8_t  s_supDrums  = 0;        // lanes already played live for the coming step
static uint8_t  s_supSynth  = 0;
static uint8_t  s_supDeck   = 0;
static bool     s_loopRun[NUM_DECKS] = {false, false};
static bool     s_wrapPending = false;  // the pattern just played its last step

// ---------- core 1 -> audio thread event queue ----------
enum : uint8_t { EV_NOTE = 1, EV_DRUM, EV_SLICE, EV_START, EV_STOP, EV_PREVIEW, EV_AUDITION };
#define EVF_ACCENT 1
#define EVF_LEGATO 2
#define EVF_REC    4
struct SeqEvent { uint8_t type, a, b, c; };
#define EVQ_LEN 32
static SeqEvent s_evq[EVQ_LEN];
static volatile uint8_t s_evHead = 0, s_evTail = 0;

static void post(uint8_t type, uint8_t a, uint8_t b, uint8_t c) {
    uint8_t next = (uint8_t)((s_evHead + 1) % EVQ_LEN);
    if (next == s_evTail) return;                 // full: drop rather than block the keys
    s_evq[s_evHead] = { type, a, b, c };
    s_evHead = next;
}

// ---------- pattern memory ----------
// Bars are taken by the keys (core 1) and by live record (the audio task), so
// the pool's bookkeeping sits in a short critical section. Step data itself is
// plain bytes: a torn read costs one wrong step once, as it always did.
static uint8_t s_barUsed[NUM_BARS];
#ifdef ARDUINO
static portMUX_TYPE s_barMux = portMUX_INITIALIZER_UNLOCKED;
#define BAR_LOCK()   portENTER_CRITICAL(&s_barMux)
#define BAR_UNLOCK() portEXIT_CRITICAL(&s_barMux)
#else
#define BAR_LOCK()
#define BAR_UNLOCK()
#endif

// the bar a step lives in, taken from the pool if the pattern has none there yet
static PatBar* barForWrite(Pattern& p, uint8_t st) {
    uint8_t& h = p.bar[(st / STEPS_PER_BAR) % MAX_BARS];
    if (h) return &g_bars[h - 1];
    PatBar* got = nullptr;
    BAR_LOCK();
    if (h) got = &g_bars[h - 1];                  // the other thread took it meanwhile
    else for (int i = 0; i < NUM_BARS; i++)
        if (!s_barUsed[i]) {
            s_barUsed[i] = 1;
            memset(&g_bars[i], 0, sizeof(PatBar));
            h = (uint8_t)(i + 1);
            got = &g_bars[i];
            break;
        }
    BAR_UNLOCK();
    if (!got) g_patFull = true;
    return got;
}

static void barFree(Pattern& p, uint8_t b) {
    BAR_LOCK();
    uint8_t h = p.bar[b];
    p.bar[b] = 0;
    if (h) s_barUsed[h - 1] = 0;
    BAR_UNLOCK();
}

// a bar with nothing left in it goes back to the pool
static void barTrim(Pattern& p, uint8_t st) {
    uint8_t b = (uint8_t)((st / STEPS_PER_BAR) % MAX_BARS);
    if (p.bar[b] && memcmp(&g_bars[p.bar[b] - 1], &g_noBar, sizeof(PatBar)) == 0) barFree(p, b);
}

uint8_t patBarsFree() {
    uint8_t n = 0;
    for (int i = 0; i < NUM_BARS; i++) if (!s_barUsed[i]) n++;
    return n;
}

bool patAnyLock(const Pattern& p) {
    for (int b = 0; b < patLen(p) / STEPS_PER_BAR; b++)
        if (p.bar[b] && g_bars[p.bar[b] - 1].lockSet) return true;
    return false;
}

bool patSetCell(Pattern& p, uint8_t track, uint8_t st, const SynthCell& c) {
    uint32_t v = 0;
    if (!c.empty()) {
        for (int i = MAX_POLY - 1; i >= 0; i--) {
            uint8_t n = c.note[i] > 12 ? 0 : c.note[i], o = c.oct[i] > 7 ? 7 : c.oct[i];
            v = (v << 7) | (n ? (uint32_t)(n | (o << 4)) : 0);
        }
        v |= (c.accent ? 1u << 21 : 0) | (c.slide ? 1u << 22 : 0);
    }
    if (!v && !p.bar[(st / STEPS_PER_BAR) % MAX_BARS]) return true;
    PatBar* b = barForWrite(p, st);
    if (!b) return false;
    uint8_t* o = b->cell[track][st % STEPS_PER_BAR];
    o[0] = (uint8_t)v; o[1] = (uint8_t)(v >> 8); o[2] = (uint8_t)(v >> 16);
    return true;
}

bool patSetDrums(Pattern& p, uint8_t st, uint8_t hits, uint8_t acc) {
    acc &= hits;
    if (!hits && !p.bar[(st / STEPS_PER_BAR) % MAX_BARS]) return true;
    PatBar* b = barForWrite(p, st);
    if (!b) return false;
    b->drums[st % STEPS_PER_BAR] = hits; b->drumAcc[st % STEPS_PER_BAR] = acc;
    return true;
}

bool patSetChop(Pattern& p, uint8_t d, uint8_t st, uint8_t v) {
    if (!v && !p.bar[(st / STEPS_PER_BAR) % MAX_BARS]) return true;
    PatBar* b = barForWrite(p, st);
    if (!b) return false;
    b->chop[d][st % STEPS_PER_BAR] = v;
    return true;
}

bool patSetLock(Pattern& p, uint8_t st, uint8_t v) {
    PatBar* b = barForWrite(p, st);
    if (!b) return false;
    b->lock[st % STEPS_PER_BAR] = v;
    b->lockSet |= (uint16_t)(1u << (st % STEPS_PER_BAR));
    return true;
}

void patClearLock(Pattern& p, uint8_t st) {
    uint8_t h = p.bar[(st / STEPS_PER_BAR) % MAX_BARS];
    if (!h) return;
    g_bars[h - 1].lock[st % STEPS_PER_BAR] = 0;
    g_bars[h - 1].lockSet &= (uint16_t)~(1u << (st % STEPS_PER_BAR));
}

bool patEqual(const Pattern& a, const Pattern& b) {
    if (patLen(a) != patLen(b) || a.mute != b.mute) return false;
    for (int i = 0; i < patLen(a) / STEPS_PER_BAR; i++)
        if (memcmp(&patBar(a, (uint8_t)(i * STEPS_PER_BAR)), &patBar(b, (uint8_t)(i * STEPS_PER_BAR)), sizeof(PatBar))) return false;
    return true;
}

// ---------- init ----------
void clearPattern(uint8_t idx) {
    if (idx >= NUM_PATTERNS) return;
    Pattern& p = g_patterns[idx];
    for (int b = 0; b < MAX_BARS; b++) barFree(p, (uint8_t)b);
    p.length = patLen(p);
    p.mute = 0;
}

void songClearSlot(uint8_t i) {
    if (i >= SONG_LENGTH) return;
    g_song[i] = { SONG_EMPTY, 1, 0, TARGET_NONE, 0 };
}

void sequencerInit() {
    memset((void*)g_patterns, 0, sizeof(g_patterns));
    memset(s_barUsed, 0, sizeof(s_barUsed));
    for (int p = 0; p < NUM_PATTERNS; p++) g_patterns[p].length = STEPS_PER_BAR;
    for (int i = 0; i < SONG_LENGTH; i++) songClearSlot(i);

    for (int s = 0; s < NUM_SYNTHS; s++) g_synths[s].init();
    g_synths[0].forEach([](SynthVoice& v){ v.oscMode = OSC_SAW; v.fltCutoff = 0.30f;
                                           v.fltReso = 0.45f;   v.fltEnvAmt = 0.60f; });
    g_synths[1].forEach([](SynthVoice& v){ v.oscMode = OSC_SQR; v.fltCutoff = 0.45f;
                                           v.volume  = 0.6f; });
    g_synths[2].forEach([](SynthVoice& v){ v.oscMode = OSC_WT;  v.wtIndex = 0;
                                           v.fltCutoff = 0.55f; v.volume = 0.5f; });
    g_synths[2].setVoices(3);          // track 3 ships polyphonic (chords/pads)

    // lanes 0-3: 808 KICK SNARE CHAT CLAP | lanes 4-7: 909 KICK SNARE CHAT OHAT
    // (styles.cpp replaces this kit with the FILTRE 909 set at power-on)
    g_drumLanes[0].init(ENG_808, DT_KICK);
    g_drumLanes[1].init(ENG_808, DT_SNARE);
    g_drumLanes[2].init(ENG_808, DT_HAT_C);
    g_drumLanes[3].init(ENG_808, DT_HAT_O_OR_CLAP);
    g_drumLanes[4].init(ENG_909, DT_KICK);
    g_drumLanes[5].init(ENG_909, DT_SNARE);
    g_drumLanes[6].init(ENG_909, DT_HAT_C);
    g_drumLanes[7].init(ENG_909, DT_HAT_O_OR_CLAP);
    // 909 hats choke each other by default
    g_drumLanes[6].chokeGroup = 1;
    g_drumLanes[7].chokeGroup = 1;
}

// ---------- mutes ----------
// Song mode off: the pattern that is sounding carries the mutes (stopped, the
// one on screen). Song mode on: the slot does, and the pattern's are ignored.
// Nothing is ever copied between the two, so each comes back as it was left.
static int muteSlot() {
    uint8_t i = (g_songMode && g_playing) ? (uint8_t)g_songPos : g_songCursor;
    return (i < SONG_LENGTH && g_song[i].pattern != SONG_EMPTY) ? i : -1;
}

// What you hear: song mode (the N key) decides, whatever page you are on.
uint16_t seqMuteMask() {
    if (g_songMode) {
        if (g_playing) return g_slotMute;
        int i = muteSlot();
        return i >= 0 ? g_song[i].mute : 0;
    }
    return g_patterns[g_playing ? g_playPattern : g_curPattern].mute;
}

// What the mute keys edit and the screens show. The SONG page always works on
// song slots (the playing one in song mode, else the one under the cursor);
// every other page follows song mode.
static bool editsSlot() { return g_curPage == PAGE_SONG || g_songMode; }

uint16_t seqEditMask() {
    if (!editsSlot()) return seqMuteMask();
    int i = muteSlot();
    return i >= 0 ? g_song[i].mute : 0;
}

int seqToggleMute(uint8_t row, uint8_t* pat, bool* nowMuted) {
    uint16_t bit = (uint16_t)(1u << row);
    if (editsSlot()) {
        int i = muteSlot();
        if (i < 0) return -1;
        g_song[i].mute ^= bit;
        if (g_songMode && g_playing) g_slotMute = g_song[i].mute;
        *nowMuted = g_song[i].mute & bit;
        return i + 1;
    }
    *pat = g_playing ? (uint8_t)g_playPattern : g_curPattern;
    g_patterns[*pat].mute ^= bit;
    *nowMuted = g_patterns[*pat].mute & bit;
    return 0;
}
bool seqDeckMuted(uint8_t d)  { return seqMuteMask() & MUTE_DECK(d); }

void engineStopSampleVoices() {
    for (int l = 0; l < NUM_DRUM_LANES; l++) g_drumLanes[l].smp.stop();
    g_previewVoice.stop();
    deckStopAll();
    s_loopRun[0] = s_loopRun[1] = false;
}

// ---------- triggering ----------
static void triggerLane(uint8_t lane, bool accent) {
    if (lane >= NUM_DRUM_LANES) return;
    uint8_t grp = g_drumLanes[lane].chokeGroup;
    if (grp != 0) {
        for (int i = 0; i < NUM_DRUM_LANES; i++)
            if (i != lane && g_drumLanes[i].chokeGroup == grp)
                g_drumLanes[i].choke();
    }
    g_drumLanes[lane].trigger(accent);
    if (lane == 0) pumpTrigger();                 // lane 1 pumps the loop
}

static inline float stepSamples() { return (float)SAMPLE_RATE * 15.0f / (float)g_bpm; }   // one 16th
static inline float barFrames()   { return stepSamples() * STEPS_PER_BAR; }

// ---------- song ----------
static void slotEnter() {
    const SongSlot& sl = g_song[g_songPos];
    g_slotMute = sl.mute;
    g_slotRep  = 0;
    s_slotStep = 0;
    s_slotFrom = s_cut;
    if (sl.pattern != SONG_EMPTY) g_playPattern = sl.pattern;
}

// next non-empty slot; returns true when the chain wrapped back to the loop start
static bool songAdvance() {
    uint8_t start = g_songPos;
    bool wrapped = false;
    for (int i = 0; i < SONG_LENGTH; i++) {
        uint8_t next = (uint8_t)(g_songPos + 1);
        if (next >= SONG_LENGTH || g_song[next].pattern == SONG_EMPTY) {
            // the chain ends at the first gap: go back to the loop start
            next = g_songLoopStart; wrapped = true;
        }
        g_songPos = next;
        if (g_song[g_songPos].pattern != SONG_EMPTY) break;
        if (g_songPos == start) break;            // nothing to play
    }
    slotEnter();
    return wrapped;
}

// ---------- the step ----------
static void cutoffSegment(Pattern& p, uint8_t step, uint8_t len, float dur) {
    float base = (float)g_fx.cutoff;
    float a, b;
    const SongSlot& sl = g_song[g_songPos];
    if (s_touch) {                                // hands on the filter win
        if (g_recEnabled && g_cutHeld) patSetLock(p, step, g_fx.cutoff);
        if (!g_cutHeld && (g_recEnabled || step == len - 1)) s_touch = false;
        s_cutLeft = 0;
        return;
    }
    if (g_songMode && sl.target != TARGET_NONE) {  // song target replaces the lock lane
        float total = (float)(sl.repeats ? sl.repeats : 1) * (float)len;
        float to    = (float)sl.target;
        a = s_slotFrom + (to - s_slotFrom) * ((float)s_slotStep / total);
        b = s_slotFrom + (to - s_slotFrom) * ((float)(s_slotStep + 1) / total);
        if (b > 255.0f) b = 255.0f; if (b < 0.0f) b = 0.0f;
    } else if (patAnyLock(p)) {
        uint8_t nx = (uint8_t)((step + 1) % len);
        a = patLocked(p, step) ? (float)patLock(p, step) : base;
        b = patLocked(p, nx)   ? (float)patLock(p, nx)   : base;
    } else {
        a = b = base;
        dur = 64.0f;
        a = s_cut;                                // glide back to the knob
    }
    s_cut     = a;
    s_cutLeft = (uint32_t)dur;
    if (s_cutLeft < 1) s_cutLeft = 1;
    s_cutInc  = (b - a) / (float)s_cutLeft;
}

float seqCutoffTick() {
    if (!g_playing || s_touch) {
        // hand on the knob: a 20 ms slew turns key repeats into a smooth sweep
        s_cut += ((float)g_fx.cutoff - s_cut) * 0.0022f; s_cutLeft = 0;
    } else if (s_cutLeft) {
        s_cut += s_cutInc; s_cutLeft--;
    }
    return s_cut;
}

void seqFireStep() {
    // Decide what plays next on the bar line itself, not a 16th early: a
    // pattern tapped during the last step still lands, and a one-shot song
    // ends after its last step has had its full length.
    if (s_wrapPending) {
        s_wrapPending = false;
        if (g_songMode) {
            uint8_t reps = g_song[g_songPos].repeats ? g_song[g_songPos].repeats : 1;
            if (++g_slotRep >= reps) {
                if (songAdvance() && g_songOneShot) {
                    g_songEnded = true; g_playing = false;
                    for (int d = 0; d < NUM_DECKS; d++) { deckStop(d); s_loopRun[d] = false; }
                    return;
                }
            }
        } else {
            g_playPattern = g_nextPattern;        // whatever was queued last
        }
    }

    uint8_t step = g_playStep;
    Pattern& p = g_patterns[g_playPattern];
    uint8_t len = patLen(p);
    if (step >= len) step = 0;

    // --- clock: swing delays the odd 16ths, even ones stay on the grid ---
    float sw  = (float)g_fx.swing * 0.01f;
    float dur = 2.0f * stepSamples() * ((step & 1) ? (1.0f - sw) : sw);
    s_prevAt = s_lastAt; s_lastAt = g_seqNow;
    s_frac += dur;
    uint32_t whole = (uint32_t)s_frac;
    g_seqNextAt += whole; s_frac -= (float)whole;

    if ((step % STEPS_PER_BAR) == 0) s_bar++;
    if (step == 0 && g_rsmpArmFrames) {           // bar-synced resample starts exactly here
        g_rsmpRemain = g_rsmpArmFrames; g_rsmpArmFrames = 0;
    }

    // --- synths ---
    const uint16_t mute = seqMuteMask();
    for (int s = 0; s < NUM_SYNTHS; s++) {
        if ((mute & MUTE_SYNTH(s)) || (s_supSynth & (1 << s))) continue;
        const SynthCell c = patCell(p, (uint8_t)s, step);
        bool mono = (g_synths[s].voices <= 1);
        for (int i = 0; i < (mono ? 1 : MAX_POLY); i++)
            if (c.note[i] != NOTE_EMPTY)
                g_synths[s].noteOn(noteToFreq(c.note[i], c.oct[i]),
                                   c.accent, mono && c.slide);
    }

    // --- drums; lane 1 also drives the pump, muted or not when GHOST is on ---
    const uint8_t accs = patDrumAcc(p, step);
    uint8_t mask = patDrums(p, step) & (uint8_t)~s_supDrums;
    if (mask & 1) {
        if (!(mute & MUTE_LANE(0))) triggerLane(0, accs & 1);
        else if (g_fx.ghost) pumpTrigger();
    }
    for (int l = 1; l < NUM_DRUM_LANES; l++)
        if ((mask & (1 << l)) && !(mute & MUTE_LANE(l))) triggerLane(l, accs & (1 << l));

    // --- decks ---
    for (int d = 0; d < NUM_DECKS; d++) {
        Deck& k = g_decks[d];
        if (!deckLoaded(d)) { s_loopRun[d] = false; continue; }
        if (k.mode == DECK_LOOP) {
            if ((step % STEPS_PER_BAR) == 0) {
                bool sync = k.fitBars ? (s_bar % k.fitBars) == 0 : !s_loopRun[d];
                bool dead = !k.v[k.cur].active || !k.v[k.cur].loop;
                if (sync || dead) { deckLoopSync(d, barFrames()); s_loopRun[d] = true; }
            }
        } else {
            s_loopRun[d] = false;
            uint8_t c = patChop(p, (uint8_t)d, step);
            if (CHOP_SLICE(c) && !(s_supDeck & (1 << d)))
                deckNoteOn(d, (uint8_t)(CHOP_SLICE(c) - 1), c & CHOP_ACC, c & CHOP_REV);
        }
    }
    s_supDrums = s_supSynth = s_supDeck = 0;

    cutoffSegment(p, step, len, dur);

    // --- advance ---
    g_lastStep = step;
    s_slotStep++;
    step++;
    if (step >= len) { step = 0; s_wrapPending = true; }
    g_playStep = step;
    g_needRedraw = true;
}

// ---------- live record: which step did the player mean? ----------
// Returns the step and whether it is the coming one (not fired yet).
static uint8_t quantizedStep(bool* upcoming) {
    uint8_t len = patLen(g_patterns[g_playPattern]);
    uint32_t t = (g_seqNow > REC_LATENCY_SAMPLES) ? g_seqNow - REC_LATENCY_SAMPLES : 0;
    uint8_t last = g_lastStep;
    uint8_t prev = (uint8_t)((last + len - 1) % len);
    uint32_t dPrev = (t > s_prevAt) ? t - s_prevAt : s_prevAt - t;
    uint32_t dLast = (t > s_lastAt) ? t - s_lastAt : s_lastAt - t;
    uint32_t dNext = (g_seqNextAt > t) ? g_seqNextAt - t : t - g_seqNextAt;
    *upcoming = false;
    if (dNext < dLast && dNext < dPrev) { *upcoming = true; return g_playStep; }
    if (dPrev < dLast && s_lastAt != s_prevAt) return prev;
    return last;
}

static void startTransport(bool fromTop) {
    g_songEnded = false;
    g_playStep = 0;
    if (g_songMode) {
        if (fromTop) g_songPos = g_songLoopStart;
        slotEnter();
    } else {
        g_playPattern = g_curPattern;
        g_slotMute = 0;
    }
    g_seqNow = 0; g_seqNextAt = 0; s_frac = 0.0f;
    s_lastAt = s_prevAt = 0;
    s_bar = -1;
    s_touch = false;
    s_supDrums = s_supSynth = s_supDeck = 0;
    s_loopRun[0] = s_loopRun[1] = false;
    s_wrapPending = false;
    g_playing = true;
}

static void stopTransport() {
    g_playing = false;
    g_playStep = 0;
    g_lastStep = 0;
    for (int d = 0; d < NUM_DECKS; d++) { deckStop(d); s_loopRun[d] = false; }
}

void seqDrainEvents() {
    while (s_evTail != s_evHead) {
        SeqEvent e = s_evq[s_evTail];
        s_evTail = (uint8_t)((s_evTail + 1) % EVQ_LEN);
        bool rec = (e.c & EVF_REC) && g_playing;
        bool upcoming = false;
        uint8_t st = rec ? quantizedStep(&upcoming) : 0;
        Pattern& p = g_patterns[g_playPattern];

        switch (e.type) {
            case EV_NOTE: {
                uint8_t track = e.a, note = e.b & 0x0F, oct = e.b >> 4;
                if (track >= NUM_SYNTHS) break;
                bool poly = (g_synths[track].voices > 1);
                bool legato = e.c & EVF_LEGATO, accent = e.c & EVF_ACCENT;
                g_synths[track].noteOn(noteToFreq(note, oct), accent, !poly && legato);
                if (rec) {
                    SynthCell c = patCell(p, track, st);
                    if (poly && legato && !c.empty()) { c.add(note, oct); if (accent) c.accent = true; }
                    else c.setMono(note, oct, accent, !poly && legato);
                    patSetCell(p, track, st, c);
                    if (upcoming) s_supSynth |= (1 << track);
                }
                break;
            }
            case EV_AUDITION: {
                uint8_t note = e.b & 0x0F, oct = e.b >> 4;
                if (e.a < NUM_SYNTHS) g_synths[e.a].noteOn(noteToFreq(note, oct), e.c & EVF_ACCENT, false);
                break;
            }
            case EV_DRUM:
                triggerLane(e.a, e.c & EVF_ACCENT);
                if (rec && e.a < NUM_DRUM_LANES) {
                    uint8_t bit = (uint8_t)(1 << e.a), acc = patDrumAcc(p, st);
                    patSetDrums(p, st, patDrums(p, st) | bit, (e.c & EVF_ACCENT) ? acc | bit : acc & (uint8_t)~bit);
                    if (upcoming) s_supDrums |= (1 << e.a);
                }
                break;
            case EV_SLICE:
                if (e.a >= NUM_DECKS) break;
                deckNoteOn(e.a, e.b, e.c & EVF_ACCENT, false);
                if (rec) {
                    patSetChop(p, e.a, st, (uint8_t)((e.b + 1) | ((e.c & EVF_ACCENT) ? CHOP_ACC : 0)));
                    if (upcoming) s_supDeck |= (1 << e.a);
                }
                break;
            case EV_START:   startTransport(e.a); break;
            case EV_STOP:    stopTransport(); break;
            case EV_PREVIEW: g_previewVoice.trigger(e.a, 1.0f, 0.9f); break;
        }
        if (rec) g_needRedraw = true;
    }
}

// ---------- transport (core 1) ----------
void sequencerStart(bool fromTop) { g_nextPattern = g_curPattern; post(EV_START, fromTop, 0, 0); }
void sequencerStop()              { post(EV_STOP, 0, 0, 0); }

// ---------- live input (core 1) ----------
void liveSynthNote(uint8_t track, uint8_t note, uint8_t octave, bool accent, bool legato) {
    if (track >= NUM_SYNTHS) return;
    uint8_t flags = (accent ? EVF_ACCENT : 0) | (legato ? EVF_LEGATO : 0);
    if (g_recEnabled && !g_playing) {
        // step-write: keys held together stack a chord on the same step
        bool poly = (g_synths[track].voices > 1);
        Pattern& p = g_patterns[g_curPattern];
        uint8_t len = patLen(p);
        bool stacked = false;
        if (poly && legato) {
            uint8_t prev = (uint8_t)((g_curStep + len - 1) % len);
            SynthCell c = patCell(p, track, prev);
            if (!c.empty()) { c.add(note, octave); if (accent) c.accent = true; patSetCell(p, track, prev, c); stacked = true; }
        }
        if (!stacked) {
            SynthCell c; c.setMono(note, octave, accent, false);
            patSetCell(p, track, g_curStep, c);
            g_curStep = (uint8_t)((g_curStep + 1) % len);
        }
        g_needRedraw = true;
    } else if (g_recEnabled) {
        flags |= EVF_REC;
    }
    post(EV_NOTE, track, (uint8_t)(note | (octave << 4)), flags);
}

void liveDrumHit(uint8_t lane, bool accent) {
    if (lane >= NUM_DRUM_LANES) return;
    uint8_t flags = accent ? EVF_ACCENT : 0;
    if (g_recEnabled && !g_playing) {
        Pattern& p = g_patterns[g_curPattern];
        uint8_t bit = (uint8_t)(1 << lane), acc = patDrumAcc(p, g_curStep);
        patSetDrums(p, g_curStep, patDrums(p, g_curStep) | bit, accent ? acc | bit : acc & (uint8_t)~bit);
        g_curStep = (uint8_t)((g_curStep + 1) % patLen(p));
        g_needRedraw = true;
    } else if (g_recEnabled) {
        flags |= EVF_REC;
    }
    post(EV_DRUM, lane, 0, flags);
}

void liveDeckHit(uint8_t deck, uint8_t slice, bool accent) {
    if (deck >= NUM_DECKS || slice >= g_decks[deck].nSlices) return;
    uint8_t flags = accent ? EVF_ACCENT : 0;
    if (g_recEnabled && !g_playing) {
        Pattern& p = g_patterns[g_curPattern];
        patSetChop(p, deck, g_curStep, (uint8_t)((slice + 1) | (accent ? CHOP_ACC : 0)));
        g_curStep = (uint8_t)((g_curStep + 1) % patLen(p));
        g_needRedraw = true;
    } else if (g_recEnabled) {
        flags |= EVF_REC;
    }
    post(EV_SLICE, deck, slice, flags);
}

void auditionSynth(uint8_t track, uint8_t note, uint8_t octave, bool accent) {
    post(EV_AUDITION, track, (uint8_t)(note | (octave << 4)), accent ? EVF_ACCENT : 0);
}
void auditionSample(int slot)        { if (slot >= 0) post(EV_PREVIEW, (uint8_t)slot, 0, 0); }
void triggerLaneLive(uint8_t lane)   { post(EV_DRUM, lane, 0, 0); }

void touchCutoff() {
    s_touch = true;
    if (g_recEnabled && g_playing)
        patSetLock(g_patterns[g_playPattern], g_lastStep, g_fx.cutoff);

}

// ---------- pattern helpers ----------
void pastePattern(uint8_t src, uint8_t dst) {
    if (src >= NUM_PATTERNS || dst >= NUM_PATTERNS || src == dst) return;
    const Pattern& a = g_patterns[src];
    Pattern& b = g_patterns[dst];
    for (int i = 0; i < MAX_BARS; i++) {
        if (!a.bar[i]) { barFree(b, (uint8_t)i); continue; }
        PatBar* to = barForWrite(b, (uint8_t)(i * STEPS_PER_BAR));
        if (to) memcpy(to, &g_bars[a.bar[i] - 1], sizeof(PatBar));     // pool full: that bar stays as it was
    }
    b.length = patLen(a);
    b.mute = a.mute;
    if (dst == g_curPattern && g_curStep >= patLen(g_patterns[dst])) g_curStep = 0;
}

void seqSelectPattern(uint8_t idx) {
    idx %= NUM_PATTERNS;
    g_nextPattern = idx;
    if (g_playing && !g_songMode) return;         // the bar line decides (seqFireStep)
    g_curPattern = idx;
    if (g_curStep >= patLen(g_patterns[idx])) g_curStep = 0;
}

bool seqFollowPattern() {
    if (!g_playing || g_songMode) return false;
    uint8_t now = g_playPattern;
    if (now == g_curPattern || now != g_nextPattern) return false;
    g_curPattern = now;
    if (g_curStep >= patLen(g_patterns[now])) g_curStep = 0;
    g_needRedraw = true;
    return true;
}

void setPatternLength(uint8_t idx, uint8_t len) {
    if (idx >= NUM_PATTERNS) return;
    Pattern& p = g_patterns[idx];
    int had = patLen(p) / STEPS_PER_BAR;
    int want = constrain((int)len / STEPS_PER_BAR, 1, MAX_BARS);
    // growing: the new bars repeat the ones already there, so nothing drops out
    for (int b = had; b < want; b++) {
        uint8_t from = p.bar[b % had];
        if (!from) { barFree(p, (uint8_t)b); continue; }
        PatBar* to = barForWrite(p, (uint8_t)(b * STEPS_PER_BAR));
        if (to) memcpy(to, &g_bars[from - 1], sizeof(PatBar));
    }
    p.length = (uint8_t)(want * STEPS_PER_BAR);
    for (int b = want; b < MAX_BARS; b++) barFree(p, (uint8_t)b);       // shrinking gives the memory back
    if (idx == g_curPattern && g_curStep >= patLen(p)) g_curStep = 0;
}

// ---------- step editing ----------
void toggleDrumAtCursor(uint8_t lane) {
    Pattern& p = g_patterns[g_curPattern];
    uint8_t hits = patDrums(p, g_curStep) ^ (uint8_t)(1 << lane);
    patSetDrums(p, g_curStep, hits, patDrumAcc(p, g_curStep));
    if (!hits) barTrim(p, g_curStep);
    g_needRedraw = true;
}

void writeSynthAtCursor(uint8_t note, uint8_t octave, bool accent) {
    if (g_curTrack >= NUM_SYNTHS) return;
    Pattern& p = g_patterns[g_curPattern];
    SynthCell c; c.setMono(note, octave, accent, false);
    patSetCell(p, g_curTrack, g_curStep, c);
    g_curStep = (uint8_t)((g_curStep + 1) % patLen(p));
    g_needRedraw = true;
}

static void clearCell(Pattern& p, uint8_t st) {
    if (g_curTrack < NUM_SYNTHS)      patSetCell(p, g_curTrack, st, SynthCell{});
    else if (g_curTrack == TRK_DRUMS) patSetDrums(p, st, 0, 0);
    else                              { patSetChop(p, trackDeck(g_curTrack), st, 0); patClearLock(p, st); }
    barTrim(p, st);
}

void clearCellAtCursor() {
    clearCell(g_patterns[g_curPattern], g_curStep);
    g_needRedraw = true;
}

void clearStepAtPlayhead() {
    clearCell(g_patterns[g_playPattern], g_lastStep);
    g_needRedraw = true;
}
