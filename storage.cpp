// ============================================================
// FILTRE - storage.cpp
// GBX v3 adds decks, FX, 32-step patterns, chop/lock lanes and song
// slots with repeats, mutes and filter targets. v1/v2 still load.
// ============================================================
#include "storage.h"
#include "sequencer.h"
#include "sampler.h"
#include "wavetable.h"
#include "audio_engine.h"
#include <stdlib.h>
#include <SD.h>
#include <string.h>

uint8_t g_curProject = 0;

#define GBX_MAGIC   0x31584247u   // "GBX1"
#define GBX_VERSION 3
#define OLD_STEPS   16            // v1/v2 patterns were one bar

// ---- serialized structures (POD, packed layout kept stable) ----
struct __attribute__((packed)) SaveSynth {
    uint8_t oscMode, wtIndex;
    float   cutoff, reso, envAmt, ampDec, filtDec, volume;
};

struct __attribute__((packed)) SaveDrumLane {
    uint8_t engine, type, chokeGroup;
    float   volume, tune, decay;
    char    sampleName[SAMPLE_NAME_LEN];   // empty = none
};

// ---------- v1 (legacy, mono cells) ----------
struct __attribute__((packed)) SaveCellV1 {
    uint8_t note, octave, flags;   // bit0 accent, bit1 slide
};
struct __attribute__((packed)) ProjectFileV1 {
    uint32_t magic;
    uint16_t version;
    uint16_t bpm;
    uint8_t  songLoopStart;
    uint8_t  song[SONG_LENGTH];
    SaveSynth    synths[NUM_SYNTHS];
    SaveDrumLane lanes[NUM_DRUM_LANES];
    SaveCellV1   cells[NUM_PATTERNS][NUM_SYNTHS][OLD_STEPS];
    uint8_t      drums[NUM_PATTERNS][OLD_STEPS];
};

// ---------- v2 (poly cells + voices) ----------
struct __attribute__((packed)) SaveCell {
    uint8_t note[MAX_POLY];
    uint8_t oct [MAX_POLY];
    uint8_t flags;                 // bit0 accent, bit1 slide
};
struct __attribute__((packed)) ProjectFileV2 {
    uint32_t magic;
    uint16_t version;
    uint16_t bpm;
    uint8_t  songLoopStart;
    uint8_t  song[SONG_LENGTH];
    uint8_t  voices[NUM_SYNTHS];   // 1..MAX_POLY per synth track
    SaveSynth    synths[NUM_SYNTHS];
    SaveDrumLane lanes[NUM_DRUM_LANES];
    SaveCell     cells[NUM_PATTERNS][NUM_SYNTHS][OLD_STEPS];
    uint8_t      drums[NUM_PATTERNS][OLD_STEPS];
};

// ---------- v3 ----------
struct __attribute__((packed)) SavePattern {
    SaveCell cells[NUM_SYNTHS][NUM_STEPS];
    uint8_t  drums[NUM_STEPS];
    uint8_t  drumAcc[NUM_STEPS];
    uint8_t  chop[NUM_DECKS][NUM_STEPS];
    uint8_t  lock[NUM_STEPS];
    uint32_t lockSet;
    uint8_t  length;
};
struct __attribute__((packed)) SaveDeck {
    char     name[SAMPLE_NAME_LEN];        // file in /groovebox/loops, empty = none
    uint8_t  mode, sliceMode, nSlices, fitBars, qual, startTenths, mute, reserved;
    float    pitch, level;
    uint32_t in, out;
    uint32_t slice[MAX_SLICES + 1];
};
struct __attribute__((packed)) SaveFx {
    uint8_t  blend, cutoff, reso, type, poles, pump, bassPump, ghost, swing, key, bassMode, style;
    uint16_t release;
};
struct __attribute__((packed)) ProjectFile {
    uint32_t magic;
    uint16_t version;
    uint16_t bpm;
    uint8_t  songLoopStart;
    SongSlot song[SONG_LENGTH];
    uint8_t  voices[NUM_SYNTHS];
    SaveSynth    synths[NUM_SYNTHS];
    SaveDrumLane lanes[NUM_DRUM_LANES];
    SaveFx       fx;
    SaveDeck     decks[NUM_DECKS];
    SavePattern  patterns[NUM_PATTERNS];
};

static void slotPath(uint8_t slot, char* out, size_t n) {
    snprintf(out, n, "%s/P%u.gbx", DIR_PROJECTS, (unsigned)(slot + 1));
}

bool storageProjectExists(uint8_t slot) {
    char path[64]; slotPath(slot, path, sizeof(path));
    return SD.exists(path);
}

bool storageSaveProject(uint8_t slot) {
    ProjectFile* pf = (ProjectFile*)calloc(1, sizeof(ProjectFile));   // too big for stack or .bss
    if (!pf) return false;

    pf->magic = GBX_MAGIC;
    pf->version = GBX_VERSION;
    pf->bpm = g_bpm;
    pf->songLoopStart = g_songLoopStart;
    memcpy(pf->song, g_song, sizeof(pf->song));

    for (int s = 0; s < NUM_SYNTHS; s++) {
        SynthVoice& v = g_synths[s].v[0];
        pf->voices[s] = g_synths[s].voices;
        pf->synths[s] = { (uint8_t)v.oscMode, v.wtIndex,
                          v.fltCutoff, v.fltReso, v.fltEnvAmt,
                          v.ampDecRate, v.filtDecRate, v.volume };
    }
    for (int l = 0; l < NUM_DRUM_LANES; l++) {
        DrumLane& d = g_drumLanes[l];
        SaveDrumLane& o = pf->lanes[l];
        o.engine = d.engine; o.type = d.type; o.chokeGroup = d.chokeGroup;
        o.volume = d.volume; o.tune = d.tune; o.decay = d.decay;
        if (d.engine == ENG_SMPL && samplerValid(d.sampleSlot))
            strncpy(o.sampleName, g_samples[d.sampleSlot].name, SAMPLE_NAME_LEN - 1);
    }

    pf->fx = { g_fx.blend, g_fx.cutoff, g_fx.reso, g_fx.type, g_fx.poles, g_fx.pump, g_fx.bassPump,
               g_fx.ghost, g_fx.swing, g_fx.key, g_fx.bassMode, g_fx.style, g_fx.release };
    for (int d = 0; d < NUM_DECKS; d++) {
        const Deck& k = g_decks[d];
        SaveDeck& o = pf->decks[d];
        if (deckLoaded(d)) strncpy(o.name, k.name, SAMPLE_NAME_LEN - 1);
        o.mode = k.mode; o.sliceMode = k.sliceMode; o.nSlices = k.nSlices; o.fitBars = k.fitBars;
        o.qual = k.qual; o.startTenths = k.startTenths; o.mute = k.mute;
        o.pitch = k.pitch; o.level = k.level; o.in = k.in; o.out = k.out;
        memcpy(o.slice, k.slice, sizeof(o.slice));
    }

    for (int p = 0; p < NUM_PATTERNS; p++) {
        const Pattern& src = g_patterns[p];
        SavePattern& o = pf->patterns[p];
        for (int s = 0; s < NUM_SYNTHS; s++)
            for (int st = 0; st < NUM_STEPS; st++) {
                const SynthCell& c = src.synth[s][st];
                SaveCell& oc = o.cells[s][st];
                for (int i = 0; i < MAX_POLY; i++) { oc.note[i] = c.note[i]; oc.oct[i] = c.oct[i]; }
                oc.flags = (uint8_t)((c.accent ? 1 : 0) | (c.slide ? 2 : 0));
            }
        memcpy(o.drums, src.drums, NUM_STEPS);
        memcpy(o.drumAcc, src.drumAcc, NUM_STEPS);
        memcpy(o.chop, src.chop, sizeof(o.chop));
        memcpy(o.lock, src.lock, NUM_STEPS);
        o.lockSet = src.lockSet;
        o.length  = patLen(src);
    }

    char path[64]; slotPath(slot, path, sizeof(path));
    SD.remove(path);
    File f = SD.open(path, FILE_WRITE);
    size_t written = 0;
    if (f) { written = f.write((uint8_t*)pf, sizeof(ProjectFile)); f.close(); }
    free(pf);
    return written == sizeof(ProjectFile);
}

// shared param/lane/song apply used by both format loaders
static void applySynth(int s, const SaveSynth& in, uint8_t voices) {
    SynthTrack& t = g_synths[s];
    t.forEach([&](SynthVoice& v) {
        v.oscMode     = (OscMode)(in.oscMode < OSC_COUNT ? in.oscMode : OSC_SAW);
        v.wtIndex     = (in.wtIndex < g_numWavetables) ? in.wtIndex : 0;
        v.fltCutoff   = in.cutoff;
        v.fltReso     = in.reso;
        v.fltEnvAmt   = in.envAmt;
        v.ampDecRate  = in.ampDec;
        v.filtDecRate = in.filtDec;
        v.volume      = in.volume;
        v.active      = false;
    });
    t.setVoices(voices);
}

static void applyLane(int l, const SaveDrumLane& o) {
    DrumLane& d = g_drumLanes[l];
    d.engine = (DrumEngine)(o.engine < ENG_COUNT ? o.engine : ENG_808);
    d.type   = (o.type < DT_COUNT) ? o.type : DT_KICK;
    if (d.engine != ENG_909 && d.type >= DT_COUNT_808) d.type = DT_KICK;
    d.chokeGroup = o.chokeGroup;
    d.volume = o.volume; d.tune = o.tune; d.decay = o.decay;
    d.sampleSlot = -1;
    d.sv.init(); d.smp.init();
    if (d.engine == ENG_SMPL && o.sampleName[0])
        d.sampleSlot = (int8_t)samplerLoad(o.sampleName);   // -1 if missing
}

// v1/v2 projects keep their own feel: straight 16ths, no decks.
static void legacyDefaults() {
    for (int d = 0; d < NUM_DECKS; d++) deckUnload(d);
    fxDefaults();
    g_fx.swing = 50;
    for (int p = 0; p < NUM_PATTERNS; p++) {
        memset(&g_patterns[p], 0, sizeof(Pattern));
        g_patterns[p].length = STEPS_PER_BAR;
    }
}

static void applyOldSong(const uint8_t* song) {
    for (int i = 0; i < SONG_LENGTH; i++) { songClearSlot(i); g_song[i].pattern = song[i]; }
}

static void applyDeck(int d, const SaveDeck& o) {
    deckUnload(d);
    Deck& k = g_decks[d];
    k.mode = o.mode < DECK_MODE_COUNT ? o.mode : DECK_CHOP;
    k.sliceMode = o.sliceMode < SLICE_COUNT ? o.sliceMode : SLICE_AUTO;
    k.qual = o.qual < QUAL_COUNT ? o.qual : QUAL_AUTO;
    k.startTenths = o.startTenths;
    k.mute = o.mute; k.pitch = o.pitch; k.level = o.level;
    if (!o.name[0]) return;
    char name[SAMPLE_NAME_LEN];
    memcpy(name, o.name, SAMPLE_NAME_LEN); name[SAMPLE_NAME_LEN - 1] = 0;
    if (!deckLoad(d, name)) return;                 // file gone: deck stays empty
    // the saved chop only makes sense on the same audio
    uint32_t len = g_samples[k.slot].length;
    if (o.out < len && o.in < o.out && o.nSlices >= 1 && o.nSlices <= MAX_SLICES) {
        bool sane = true;
        for (int i = 0; i < o.nSlices; i++) if (o.slice[i] >= o.slice[i + 1] || o.slice[i + 1] > o.out) sane = false;
        if (sane) {
            k.in = o.in; k.out = o.out; k.nSlices = o.nSlices;
            memcpy(k.slice, o.slice, sizeof(k.slice));
        }
    }
    k.fitBars = o.fitBars <= 8 ? o.fitBars : 0;
}

bool storageLoadProject(uint8_t slot) {
    char path[64]; slotPath(slot, path, sizeof(path));
    File f = SD.open(path, FILE_READ);
    if (!f) return false;

    // header peek: magic + version decide the format
    uint8_t head[8];
    if (f.read(head, 8) != 8) { f.close(); return false; }
    uint32_t magic;  memcpy(&magic, head, 4);
    uint16_t version; memcpy(&version, head + 4, 2);
    if (magic != GBX_MAGIC || version < 1 || version > GBX_VERSION) { f.close(); return false; }
    f.seek(0);

    size_t need = (version == 1) ? sizeof(ProjectFileV1)
                : (version == 2) ? sizeof(ProjectFileV2) : sizeof(ProjectFile);
    uint8_t* raw = (uint8_t*)malloc(need);
    if (!raw) { f.close(); return false; }
    size_t got = f.read(raw, need);
    f.close();
    if (got != need) { free(raw); return false; }

    engineLock();                        // audio parked: nothing fires while the data is swapped
    for (int d = 0; d < NUM_DECKS; d++) deckUnload(d);
    samplerClearAll();

    if (version == 3) {
        ProjectFile* pf = (ProjectFile*)raw;
        g_bpm = pf->bpm;
        g_songLoopStart = pf->songLoopStart;
        memcpy(g_song, pf->song, sizeof(pf->song));
        for (int i = 0; i < SONG_LENGTH; i++) {
            if (g_song[i].repeats < 1 || g_song[i].repeats > MAX_REPEATS) g_song[i].repeats = 1;
            if (g_song[i].pattern >= NUM_PATTERNS) g_song[i].pattern = SONG_EMPTY;
        }
        for (int s = 0; s < NUM_SYNTHS; s++) applySynth(s, pf->synths[s], pf->voices[s]);
        for (int l = 0; l < NUM_DRUM_LANES; l++) applyLane(l, pf->lanes[l]);
        const SaveFx& x = pf->fx;
        g_fx.blend = x.blend; g_fx.cutoff = x.cutoff; g_fx.reso = x.reso;
        g_fx.type = x.type < FLT_COUNT ? x.type : FLT_LP; g_fx.poles = x.poles ? 1 : 0;
        g_fx.pump = x.pump; g_fx.bassPump = x.bassPump; g_fx.ghost = x.ghost;
        g_fx.swing = x.swing; g_fx.key = x.key % 12; g_fx.bassMode = x.bassMode; g_fx.style = x.style;
        g_fx.release = x.release;
        for (int p = 0; p < NUM_PATTERNS; p++) {
            const SavePattern& in = pf->patterns[p];
            Pattern& o = g_patterns[p];
            for (int s = 0; s < NUM_SYNTHS; s++)
                for (int st = 0; st < NUM_STEPS; st++) {
                    const SaveCell& c = in.cells[s][st];
                    SynthCell& oc = o.synth[s][st];
                    for (int i = 0; i < MAX_POLY; i++) { oc.note[i] = c.note[i]; oc.oct[i] = c.oct[i]; }
                    oc.accent = c.flags & 1; oc.slide = c.flags & 2;
                }
            memcpy(o.drums, in.drums, NUM_STEPS);
            memcpy(o.drumAcc, in.drumAcc, NUM_STEPS);
            memcpy(o.chop, in.chop, sizeof(o.chop));
            memcpy(o.lock, in.lock, NUM_STEPS);
            o.lockSet = in.lockSet;
            o.length  = (in.length == NUM_STEPS) ? NUM_STEPS : STEPS_PER_BAR;
        }
        for (int d = 0; d < NUM_DECKS; d++) applyDeck(d, pf->decks[d]);
    } else if (version == 2) {
        ProjectFileV2* pf = (ProjectFileV2*)raw;
        legacyDefaults();
        g_bpm = pf->bpm;
        g_songLoopStart = pf->songLoopStart;
        applyOldSong(pf->song);
        for (int s = 0; s < NUM_SYNTHS; s++) applySynth(s, pf->synths[s], pf->voices[s]);
        for (int l = 0; l < NUM_DRUM_LANES; l++) applyLane(l, pf->lanes[l]);
        for (int p = 0; p < NUM_PATTERNS; p++) {
            for (int s = 0; s < NUM_SYNTHS; s++)
                for (int st = 0; st < OLD_STEPS; st++) {
                    const SaveCell& c = pf->cells[p][s][st];
                    SynthCell& o = g_patterns[p].synth[s][st];
                    for (int i = 0; i < MAX_POLY; i++) { o.note[i] = c.note[i]; o.oct[i] = c.oct[i]; }
                    o.accent = c.flags & 1; o.slide = c.flags & 2;
                }
            memcpy(g_patterns[p].drums, pf->drums[p], OLD_STEPS);
        }
    } else {              // v1: legacy mono cells, migrate on load
        ProjectFileV1* pf = (ProjectFileV1*)raw;
        legacyDefaults();
        g_bpm = pf->bpm;
        g_songLoopStart = pf->songLoopStart;
        applyOldSong(pf->song);
        for (int s = 0; s < NUM_SYNTHS; s++) applySynth(s, pf->synths[s], 1);
        for (int l = 0; l < NUM_DRUM_LANES; l++) applyLane(l, pf->lanes[l]);
        for (int p = 0; p < NUM_PATTERNS; p++) {
            for (int s = 0; s < NUM_SYNTHS; s++)
                for (int st = 0; st < OLD_STEPS; st++) {
                    const SaveCellV1& c = pf->cells[p][s][st];
                    g_patterns[p].synth[s][st].setMono(c.note, c.octave,
                                                       c.flags & 1, c.flags & 2);
                }
            memcpy(g_patterns[p].drums, pf->drums[p], OLD_STEPS);
        }
    }
    free(raw);
    if (g_curStep >= patLen(g_patterns[g_curPattern])) g_curStep = 0;
    engineUnlock();
    return true;
}
