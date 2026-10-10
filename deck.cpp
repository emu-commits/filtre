// ============================================================
// FILTRE - deck.cpp
// ============================================================
#include "deck.h"
#include "audio_engine.h"
#include <SD.h>
#include <string.h>
#include <math.h>

Deck     g_decks[NUM_DECKS];
FxParams g_fx;
volatile float g_cutNow  = 77.0f;
volatile float g_pumpNow = 0.0f;

bool seqDeckMuted(uint8_t d);            // sequencer.cpp: deck mute or song-slot mute

void fxDefaults() {
    g_fx.blend = 64;  g_fx.cutoff = 77;  g_fx.reso = 44;  g_fx.type = FLT_LP;  g_fx.poles = 1;
    g_fx.pump = 70;   g_fx.release = 180; g_fx.bassPump = 50; g_fx.ghost = 1;
    g_fx.swing = 62;  g_fx.key = 9;      g_fx.bassMode = 0;  g_fx.style = 0;
    g_cutNow = g_fx.cutoff;
}

static void deckClear(Deck& k) {
    memset(&k, 0, sizeof(k));
    k.slot = -1; k.mode = DECK_CHOP; k.sliceMode = SLICE_AUTO;
    k.level = 0.9f; k.qual = QUAL_AUTO;
}

void deckInit() {
    for (int d = 0; d < NUM_DECKS; d++) deckClear(g_decks[d]);
    fxReset();
}

// ---------- analysis (core 1, read-only on the sample) ----------
static inline int32_t rawAt(const Deck& k, uint32_t i) {
    const SampleInfo& s = g_samples[k.slot];
    return (int32_t)smpRead(g_arena + s.offset, s.fmt, i);
}

static void buildOverview(Deck& k) {
    uint32_t len = g_samples[k.slot].length;
    for (int x = 0; x < OV_W; x++) {
        uint32_t a = (uint32_t)((uint64_t)len * x / OV_W);
        uint32_t b = (uint32_t)((uint64_t)len * (x + 1) / OV_W);
        if (b <= a) b = a + 1;
        uint32_t stride = (b - a) / 48 + 1;            // a few dozen probes per column
        int32_t lo = 0, hi = 0;
        for (uint32_t i = a; i < b && i < len; i += stride) {
            int32_t v = rawAt(k, i);
            if (v < lo) lo = v; if (v > hi) hi = v;
        }
        k.ovMin[x] = (int8_t)(lo >> 8);
        k.ovMax[x] = (int8_t)(hi >> 8);
    }
}

// nearest rising zero crossing at or before i (bounded search)
static uint32_t snapZero(const Deck& k, uint32_t i, uint32_t lo) {
    uint32_t stop = (i > lo + 220) ? i - 220 : lo;
    for (uint32_t j = i; j > stop; j--)
        if (rawAt(k, j - 1) <= 0 && rawAt(k, j) > 0) return j;
    return i;
}

#define ENV_MAX 1024
static uint16_t s_env[ENV_MAX];

static void sliceAuto(Deck& k) {
    uint32_t len = k.out - k.in;
    uint32_t hop = len / ENV_MAX + 1;
    if (hop < 128) hop = 128;
    uint32_t n = len / hop;
    if (n > ENV_MAX) n = ENV_MAX;

    uint32_t emax = 1;
    for (uint32_t h = 0; h < n; h++) {
        uint32_t a = k.in + h * hop, acc = 0;
        for (uint32_t i = 0; i < hop; i += 2) { int32_t v = rawAt(k, a + i); acc += (uint32_t)(v < 0 ? -v : v); }
        acc /= (hop / 2);
        s_env[h] = (uint16_t)(acc > 65535 ? 65535 : acc);
        if (acc > emax) emax = acc;
    }

    // onset strength = rise over the louder of the two previous hops
    uint32_t minDist = (uint32_t)(SAMPLE_RATE * 0.075f) / hop + 1;   // 75 ms apart
    uint32_t thr     = emax / 10;
    uint32_t picks[MAX_SLICES];
    uint8_t  np = 0;
    picks[np++] = 0;                                   // slice 1 always starts the region
    while (np < MAX_SLICES) {
        uint32_t best = 0, bestH = 0;
        for (uint32_t h = 2; h < n; h++) {
            uint32_t prev = s_env[h - 1] > s_env[h - 2] ? s_env[h - 1] : s_env[h - 2];
            if (s_env[h] <= prev) continue;
            uint32_t rise = s_env[h] - prev;
            if (rise <= best || rise < thr) continue;
            bool near = false;
            for (uint8_t p = 0; p < np; p++) {
                uint32_t dist = h > picks[p] ? h - picks[p] : picks[p] - h;
                if (dist < minDist) { near = true; break; }
            }
            if (!near) { best = rise; bestH = h; }
        }
        if (!best) break;
        picks[np++] = bestH;
    }
    // sort by time
    for (uint8_t i = 1; i < np; i++)
        for (uint8_t j = i; j > 0 && picks[j] < picks[j - 1]; j--) { uint32_t t = picks[j]; picks[j] = picks[j - 1]; picks[j - 1] = t; }

    k.nSlices = np;
    for (uint8_t i = 0; i < np; i++) {
        if (picks[i] == 0) { k.slice[i] = k.in; continue; }
        // refine: first sample in the previous hop that reaches 25% of this hop's peak
        uint32_t a = k.in + (picks[i] - 1) * hop, b = a + 2 * hop;
        if (b > k.out) b = k.out;
        int32_t pk = 1;
        for (uint32_t j = a; j < b; j++) { int32_t v = rawAt(k, j); if (v < 0) v = -v; if (v > pk) pk = v; }
        uint32_t on = a;
        for (uint32_t j = a; j < b; j++) { int32_t v = rawAt(k, j); if (v < 0) v = -v; if (v * 4 >= pk) { on = j; break; } }
        on = (on > k.in + 40) ? on - 40 : k.in;        // ~2 ms pre-roll keeps the attack
        k.slice[i] = snapZero(k, on, k.in);
    }
    k.slice[np] = k.out;
}

void deckReslice(uint8_t d) {
    if (!deckLoaded(d)) return;
    Deck& k = g_decks[d];
    if (k.sliceMode == SLICE_AUTO) {
        sliceAuto(k);
        if (k.nSlices >= 2) return;
    }
    uint8_t n = (k.sliceMode == SLICE_4) ? 4 : (k.sliceMode == SLICE_8) ? 8 : 16;
    k.nSlices = n;
    for (uint8_t i = 0; i <= n; i++)
        k.slice[i] = k.in + (uint32_t)((uint64_t)(k.out - k.in) * i / n);
}

void deckNudgeSlice(uint8_t d, uint8_t s, int32_t frames) {
    if (!deckLoaded(d)) return;
    Deck& k = g_decks[d];
    if (s == 0 || s >= k.nSlices) return;              // slice 1 is pinned to IN
    int64_t p = (int64_t)k.slice[s] + frames;
    int64_t lo = (int64_t)k.slice[s - 1] + 64, hi = (int64_t)k.slice[s + 1] - 64;
    if (p < lo) p = lo; if (p > hi) p = hi;
    if (lo <= hi) k.slice[s] = (uint32_t)p;
}

void deckSetRegion(uint8_t d, uint32_t in, uint32_t out) {
    if (!deckLoaded(d)) return;
    Deck& k = g_decks[d];
    uint32_t len = g_samples[k.slot].length;
    if (out > len - 1) out = len - 1;
    if (in + 512 > out) in = (out > 512) ? out - 512 : 0;
    k.in = in; k.out = out;
    deckReslice(d);
}

float deckBpmForBars(uint8_t d, uint8_t bars) {
    if (!deckLoaded(d) || !bars) return 0;
    Deck& k = g_decks[d];
    return (float)SAMPLE_RATE * 240.0f * bars / (float)(k.out - k.in);
}

uint8_t deckGuessBars(uint8_t d, uint16_t bpm, bool force) {
    if (!deckLoaded(d)) return 0;
    Deck& k = g_decks[d];
    float bars = (float)(k.out - k.in) * bpm / ((float)SAMPLE_RATE * 240.0f);
    uint8_t best = 1; float bestErr = 1e9f;
    for (uint8_t b = 1; b <= 8; b *= 2) {
        float err = fabsf(logf(bars / (float)b));
        if (err < bestErr) { bestErr = err; best = b; }
    }
    return (force || bestErr < 0.04f) ? best : 0;      // within 4%
}

static void adopt(uint8_t d, int slot, const char* name) {
    Deck& k = g_decks[d];
    strncpy(k.name, name, LOOP_NAME_LEN - 1);
    k.name[LOOP_NAME_LEN - 1] = 0;
    k.in = 0; k.out = g_samples[slot].length - 1;
    k.slot = (int8_t)slot;
    g_samples[slot].owner = (d == 0) ? OWN_DECK_A : OWN_DECK_B;
    k.lastSlice = 0;
    buildOverview(k);
    deckReslice(d);
}

void deckUnload(uint8_t d) {
    if (d >= NUM_DECKS) return;
    Deck& k = g_decks[d];
    int slot = k.slot;
    engineLock();
    k.v[0].active = k.v[1].active = false;
    k.slot = -1; k.nSlices = 0; k.name[0] = 0; k.cut = false; k.fitBars = 0;
    engineUnlock();
    if (slot >= 0) samplerFree(slot);
}

bool deckLoad(uint8_t d, const char* filename) {
    if (d >= NUM_DECKS) return false;
    Deck& k = g_decks[d];
    deckUnload(d);                                     // free first: the new take may need the room
    char path[DIR_LOOPS_LEN + LOOP_NAME_LEN + 2];
    snprintf(path, sizeof(path), "%s/%s", DIR_LOOPS, filename);
    bool cut = false;
    int slot = samplerLoadDeck(path, filename, d == 0 ? OWN_DECK_A : OWN_DECK_B,
                               k.qual, k.startTenths * 0.1f, &cut);
    if (slot < 0) return false;
    engineLock();
    adopt(d, slot, filename);
    k.cut = cut;
    engineUnlock();
    return true;
}

bool deckAdopt(uint8_t d, int slot) {
    if (d >= NUM_DECKS || !samplerValid(slot)) return false;
    if (g_decks[d].slot != slot) deckUnload(d);
    if (!samplerValid(slot)) return false;
    engineLock();
    adopt(d, slot, g_samples[slot].name);
    engineUnlock();
    return true;
}

// ---------- playback (audio thread) ----------
static void voiceStart(Deck& k, float start, float end, float inc, float vel, bool loop) {
    const SampleInfo& s = g_samples[k.slot];
    k.v[k.cur].fadeOut = true;                          // cut the previous hit, click-free
    k.cur ^= 1;
    ChopVoice& v = k.v[k.cur];
    v.data  = g_arena + s.offset;
    v.fmt   = s.fmt;
    v.start = start; v.end = end;
    v.inc   = inc;
    v.pos   = (inc >= 0) ? start : end - 1.0f;
    v.vel   = vel;
    v.gain  = 0.0f;
    v.loop  = loop;
    v.fadeOut = false;
    v.active  = true;
}

void deckNoteOn(uint8_t d, uint8_t slice, bool accent, bool reverse) {
    if (!deckLoaded(d)) return;
    Deck& k = g_decks[d];
    if (slice >= k.nSlices) return;
    k.lastSlice = slice;
    float inc = powf(2.0f, k.pitch / 12.0f);
    voiceStart(k, (float)k.slice[slice], (float)k.slice[slice + 1],
               reverse ? -inc : inc, accent ? 1.0f : 0.8f, false);
}

void deckLoopSync(uint8_t d, float barFrames) {
    if (!deckLoaded(d)) return;
    Deck& k = g_decks[d];
    float len = (float)(k.out - k.in);
    float inc = k.fitBars ? len / (barFrames * (float)k.fitBars) : powf(2.0f, k.pitch / 12.0f);
    voiceStart(k, (float)k.in, (float)k.out, inc, 0.9f, true);
}

void deckStop(uint8_t d) {
    if (d >= NUM_DECKS) return;
    g_decks[d].v[0].fadeOut = g_decks[d].v[1].fadeOut = true;
}

void deckStopAll() {
    for (int d = 0; d < NUM_DECKS; d++)
        g_decks[d].v[0].active = g_decks[d].v[1].active = false;
}

static inline float voiceRender(ChopVoice& v) {
    if (v.fadeOut) {
        v.gain -= DECK_FADE_OUT;
        if (v.gain <= 0.0f) { v.active = false; return 0.0f; }
    } else if (v.gain < 1.0f) {
        v.gain += DECK_FADE_IN;
        if (v.gain > 1.0f) v.gain = 1.0f;
    }
    uint32_t i = (uint32_t)v.pos;
    float frac = v.pos - (float)i;
    float a = smpRead(v.data, v.fmt, i), b = smpRead(v.data, v.fmt, i + 1);
    float out = (a + (b - a) * frac) * (1.0f / 32768.0f) * v.gain * v.vel;
    v.pos += v.inc;
    return out;
}

static inline float deckRender(Deck& k) {
    float out = 0.0f;
    for (int n = 0; n < 2; n++) {
        ChopVoice& v = k.v[n];
        if (!v.active) continue;
        out += voiceRender(v);
        if (!v.active || v.fadeOut) continue;
        // close to the edge of its slice: fade, or wrap the loop on the other voice
        float room = (v.inc >= 0) ? v.end - v.pos : v.pos - v.start;
        float need = 66.0f * (v.inc >= 0 ? v.inc : -v.inc) + 2.0f;
        if (room <= need) {
            if (v.loop) voiceStart(k, v.start, v.end, v.inc, v.vel, true);
            else        v.fadeOut = true;
        }
    }
    ChopVoice& c = k.v[k.cur];
    if (c.active) k.playFrame = (uint32_t)c.pos;
    return out * k.level;
}

// ---------- filter (TPT state-variable, stable up to Nyquist) ----------
struct Svf {
    float ic1, ic2;
    inline float run(float x, float a1, float a2, float a3, float kq, uint8_t type) {
        float v3 = x - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        if (type == FLT_LP) return v2;
        if (type == FLT_BP) return v1 * kq;
        return x - kq * v1 - v2;
    }
};

struct SvfCoef { float a1, a2, a3, k; };

static Svf      s_f1, s_f2;
static SvfCoef  s_c1, s_c2;
static float    s_comp  = 1.0f;
static float    s_cutSm = 77.0f;
static uint32_t s_ctl   = 0;

float cutoffToHz(float c) {
    return 80.0f * powf(2.0f, c * (7.1f / 255.0f));     // 80 Hz .. ~11 kHz
}

static inline void setCoef(SvfCoef& c, float g, float k) {
    c.k  = k;
    c.a1 = 1.0f / (1.0f + g * (g + k));
    c.a2 = g * c.a1;
    c.a3 = g * c.a2;
}

static void updateCoefs() {
    s_cutSm += (g_cutNow - s_cutSm) * 0.12f;            // ~6 ms glide: no zipper
    float fc = cutoffToHz(s_cutSm);
    if (fc > SAMPLE_RATE * 0.49f) fc = SAMPLE_RATE * 0.49f;
    float g = tanf(3.14159265f * fc * INV_SAMPLE_RATE);
    float q = 0.6f * powf(20.0f, (float)g_fx.reso * (1.0f / 127.0f));   // 0.6 .. 12
    if (g_fx.poles) { setCoef(s_c1, g, 1.0f / 0.72f); setCoef(s_c2, g, 1.0f / q); }  // resonance on stage 2
    else            { setCoef(s_c1, g, 1.0f / q); }
    s_comp = (q > 2.0f) ? sqrtf(2.0f / q) : 1.0f;       // keep big resonance out of the clipper
}

float deckBusRender() {
    // muted decks still advance, so they come back in time
    float a = deckRender(g_decks[0]); if (seqDeckMuted(0)) a = 0.0f;
    float b = deckRender(g_decks[1]); if (seqDeckMuted(1)) b = 0.0f;

    uint8_t bl = g_fx.blend;
    float ga = (bl <= 64) ? 1.0f : (float)(127 - bl) * (1.0f / 63.0f);
    float gb = (bl >= 64) ? 1.0f : (float)bl * (1.0f / 64.0f);
    float x = a * ga + b * gb;

    if ((s_ctl++ & 15) == 0) updateCoefs();
    float y = s_f1.run(x, s_c1.a1, s_c1.a2, s_c1.a3, s_c1.k, g_fx.type);
    if (g_fx.poles) y = s_f2.run(y, s_c2.a1, s_c2.a2, s_c2.a3, s_c2.k, g_fx.type);
    // a hot resonance must not blow up the state on the no-FPU-exception path
    if (s_f1.ic1 > 8.0f) s_f1.ic1 = 8.0f; else if (s_f1.ic1 < -8.0f) s_f1.ic1 = -8.0f;
    if (s_f2.ic1 > 8.0f) s_f2.ic1 = 8.0f; else if (s_f2.ic1 < -8.0f) s_f2.ic1 = -8.0f;
    return y * s_comp;
}

// ---------- pump: a gain envelope retriggered by lane 1, not a compressor ----------
#define PUMP_ATTACK 110                     // 5 ms
static float    s_duck   = 0.0f;
static int      s_atk    = 0;
static float    s_relCoef = 0.999f;
static uint16_t s_relFor  = 0;

void pumpTrigger() { s_atk = PUMP_ATTACK; }

float pumpRender() {
    if (s_relFor != g_fx.release) {         // reach ~5% after `release` ms
        s_relFor  = g_fx.release;
        s_relCoef = expf(-2.9957f / ((float)g_fx.release * 0.001f * (float)SAMPLE_RATE));
    }
    if (s_atk > 0) { s_duck += (1.0f - s_duck) / (float)s_atk; s_atk--; }
    else           { s_duck *= s_relCoef; if (s_duck < 1e-5f) s_duck = 0.0f; }
    g_pumpNow = s_duck;
    return s_duck;
}

void fxReset() {
    s_f1.ic1 = s_f1.ic2 = s_f2.ic1 = s_f2.ic2 = 0.0f;
    s_duck = 0.0f; s_atk = 0; s_relFor = 0;
    s_cutSm = g_cutNow;
    s_ctl = 0;
}
