// ============================================================
// FILTRE - storage.cpp
// GBX v3 adds decks, FX, 32-step patterns, chop/lock lanes and song
// slots with repeats, mutes and filter targets. v4 appends the per-pattern
// mutes (a v3 file is a v4 file without that tail). v5 widens the song
// slot's mute to the 13 grid rows. v6 stores the patterns as the bars they
// really use (up to 64 patterns of up to 4 bars). v1..v5 still load.
// Arrangements (/groovebox/songs/*.arr) are text: see storageLoadArrangement.
// ============================================================
#include "storage.h"
#include "styles.h"
#include "sequencer.h"
#include "sampler.h"
#include "wavetable.h"
#include "audio_engine.h"
#include <stdlib.h>
#include <SD.h>
#include <string.h>
#include <stddef.h>

uint8_t g_curProject = 0;

#define GBX_MAGIC   0x31584247u   // "GBX1"
#define GBX_VERSION 6
#define OLD_BUILTIN_WT 8           // before v4: user wavetables started here
#define OLD_STEPS   16            // v1/v2 patterns were one bar
#define FILE_PATTERNS 8           // v1..v5: patterns inside the fixed ProjectFile; the rest are a tail after it
#define OLD_PATTERNS  32          // v4/v5: patterns in a file, tail included
#define V5_STEPS      32          // v3..v5 patterns were two bars

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
    SaveCellV1   cells[FILE_PATTERNS][NUM_SYNTHS][OLD_STEPS];
    uint8_t      drums[FILE_PATTERNS][OLD_STEPS];
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
    SaveCell     cells[FILE_PATTERNS][NUM_SYNTHS][OLD_STEPS];
    uint8_t      drums[FILE_PATTERNS][OLD_STEPS];
};

// ---------- v3 ----------
struct __attribute__((packed)) SavePattern {
    SaveCell cells[NUM_SYNTHS][V5_STEPS];
    uint8_t  drums[V5_STEPS];
    uint8_t  drumAcc[V5_STEPS];
    uint8_t  chop[NUM_DECKS][V5_STEPS];
    uint8_t  lock[V5_STEPS];
    uint32_t lockSet;
    uint8_t  length;
};
struct __attribute__((packed)) SaveDeck {
    char     name[LOOP_NAME_LEN];          // file in /groovebox/loops, empty = none
    uint8_t  mode, sliceMode, nSlices, fitBars, qual, startTenths, mute, reserved;   // mute: v3 only
    float    pitch, level;
    uint32_t in, out;
    uint32_t slice[MAX_SLICES + 1];
};
struct __attribute__((packed)) SaveFx {
    uint8_t  blend, cutoff, reso, type, poles, pump, bassPump, ghost, swing, key, bassMode, style;
    uint16_t release;
};
// the slot as v3 wrote it. Up to v4 `mute` was seven coarse bits laid over the
// pattern's mutes; from v5 it is the low byte of the slot's own 13 rows.
struct __attribute__((packed)) SaveSlot { uint8_t pattern, repeats, mute, target; };
#define OLD_SLOT_DRUMS 0x08
#define OLD_SLOT_DECKA 0x10
#define OLD_SLOT_DECKB 0x20
#define OLD_SLOT_KICK  0x40
// everything in front of the patterns: v3..v5 carry it at the top of ProjectFile, v6 writes it alone
#define PROJECT_HEAD \
    uint32_t magic; \
    uint16_t version; \
    uint16_t bpm; \
    uint8_t  songLoopStart; \
    SaveSlot song[SONG_LENGTH]; \
    uint8_t  voices[NUM_SYNTHS]; \
    SaveSynth    synths[NUM_SYNTHS]; \
    SaveDrumLane lanes[NUM_DRUM_LANES]; \
    SaveFx       fx; \
    SaveDeck     decks[NUM_DECKS];
struct __attribute__((packed)) ProjectHead { PROJECT_HEAD };
struct __attribute__((packed)) ProjectFile {
    PROJECT_HEAD
    SavePattern  patterns[FILE_PATTERNS];
    uint16_t     mutes[FILE_PATTERNS];     // v4: Pattern.mute
    uint8_t      slotMuteHi[SONG_LENGTH];  // v5: SongSlot.mute, high byte
};
#define V3_SIZE offsetof(ProjectFile, mutes)
#define V4_SIZE offsetof(ProjectFile, slotMuteHi)
// v4/v5: patterns FILE_PATTERNS..31 follow the struct, one {SavePattern, mute} at a time
struct __attribute__((packed)) TailPattern { SavePattern pat; uint16_t mute; };

// ---------- v6 ----------
// The ProjectFile up to (not including) `patterns`, then slotMuteHi, then:
//   uint8 count, uint8 barBytes
//   per pattern: SavePatHead, then one PatBar (as it sits in RAM) per bit set in `bars`
// Empty patterns cost 4 bytes and an empty project saves in about 2 KB.
#define V6_HEAD sizeof(ProjectHead)
static_assert(sizeof(ProjectHead) == offsetof(ProjectFile, patterns), "one head, two spellings");
struct __attribute__((packed)) SavePatHead { uint8_t length, bars; uint16_t mute; };
static_assert(sizeof(PatBar) == 226, "PatBar is written to the card as it is: keep its layout");

// a v3..v5 pattern into the pool (the pattern must be clear)
static void unpackPattern(const SavePattern& in, Pattern& o) {
    setPatternLength((uint8_t)(&o - g_patterns), in.length == V5_STEPS ? V5_STEPS : STEPS_PER_BAR);
    for (int st = 0; st < patLen(o); st++) {
        for (int s = 0; s < NUM_SYNTHS; s++) {
            const SaveCell& c = in.cells[s][st];
            SynthCell oc;
            for (int i = 0; i < MAX_POLY; i++) { oc.note[i] = c.note[i]; oc.oct[i] = c.oct[i]; }
            oc.accent = c.flags & 1; oc.slide = c.flags & 2;
            patSetCell(o, (uint8_t)s, (uint8_t)st, oc);
        }
        patSetDrums(o, (uint8_t)st, in.drums[st], in.drumAcc[st]);
        for (int d = 0; d < NUM_DECKS; d++) patSetChop(o, (uint8_t)d, (uint8_t)st, in.chop[d][st]);
        if (in.lockSet & (1u << st)) patSetLock(o, (uint8_t)st, in.lock[st]);
    }
}

static void slotPath(uint8_t slot, char* out, size_t n) {
    snprintf(out, n, "%s/P%u.gbx", DIR_PROJECTS, (unsigned)(slot + 1));
}

bool storageProjectExists(uint8_t slot) {
    char path[64]; slotPath(slot, path, sizeof(path));
    return SD.exists(path);
}

bool storageSaveProject(uint8_t slot) {
    ProjectHead* pf = (ProjectHead*)calloc(1, sizeof(ProjectHead));
    if (!pf) return false;
    uint8_t hi[SONG_LENGTH];

    pf->magic = GBX_MAGIC;
    pf->version = GBX_VERSION;
    pf->bpm = g_bpm;
    pf->songLoopStart = g_songLoopStart;
    for (int i = 0; i < SONG_LENGTH; i++) {
        const SongSlot& sl = g_song[i];
        pf->song[i] = { sl.pattern, sl.repeats, (uint8_t)(sl.mute & 0xFF), sl.target };
        hi[i] = (uint8_t)(sl.mute >> 8);
    }

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
        if (deckLoaded(d)) strncpy(o.name, k.name, LOOP_NAME_LEN - 1);
        o.mode = k.mode; o.sliceMode = k.sliceMode; o.nSlices = k.nSlices; o.fitBars = k.fitBars;
        o.qual = k.qual; o.startTenths = k.startTenths;
        o.pitch = k.pitch; o.level = k.level; o.in = k.in; o.out = k.out;
        memcpy(o.slice, k.slice, sizeof(o.slice));
    }

    char path[64]; slotPath(slot, path, sizeof(path));
    SD.remove(path);
    File f = SD.open(path, FILE_WRITE);
    bool ok = false;
    if (f) {
        ok = f.write((uint8_t*)pf, V6_HEAD) == V6_HEAD &&
             f.write(hi, SONG_LENGTH) == SONG_LENGTH;
        const uint8_t dims[2] = { NUM_PATTERNS, (uint8_t)sizeof(PatBar) };
        ok = ok && f.write(dims, 2) == 2;
        for (int p = 0; ok && p < NUM_PATTERNS; p++) {
            const Pattern& src = g_patterns[p];
            SavePatHead h = { patLen(src), 0, src.mute };
            for (int b = 0; b < patLen(src) / STEPS_PER_BAR; b++)
                if (src.bar[b] && memcmp(&g_bars[src.bar[b] - 1], &g_noBar, sizeof(PatBar))) h.bars |= (uint8_t)(1 << b);
            ok = f.write((uint8_t*)&h, sizeof(h)) == sizeof(h);
            for (int b = 0; ok && b < MAX_BARS; b++)
                if (h.bars & (1 << b))
                    ok = f.write((const uint8_t*)&g_bars[src.bar[b] - 1], sizeof(PatBar)) == sizeof(PatBar);
        }
        f.close();
    }
    free(pf);
    return ok;
}

// shared param/lane/song apply used by both format loaders
static void applySynth(int s, const SaveSynth& in, uint8_t voices, uint16_t version) {
    SynthTrack& t = g_synths[s];
    uint8_t wt = in.wtIndex;
    if (version < 4 && wt >= OLD_BUILTIN_WT) wt = (uint8_t)(wt + NUM_BUILTIN_WT - OLD_BUILTIN_WT);
    t.forEach([&](SynthVoice& v) {
        v.oscMode     = (OscMode)(in.oscMode < OSC_COUNT ? in.oscMode : OSC_SAW);
        v.wtIndex     = (wt < g_numWavetables) ? wt : 0;
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
    k.pitch = o.pitch; k.level = o.level;
    if (!o.name[0]) return;
    char name[LOOP_NAME_LEN];
    memcpy(name, o.name, LOOP_NAME_LEN); name[LOOP_NAME_LEN - 1] = 0;
    if (!strcmp(name, "FILTRE-DEMO")) {             // the factory loop lives in flash, not on the card
        int fs = samplerAddExternal(name, g_factoryLoop, g_factoryLoopFrames, SAMPLE_RATE, FMT_ULAW,
                                    d == 0 ? OWN_DECK_A : OWN_DECK_B);
        if (fs < 0 || !deckAdopt(d, fs)) return;
    } else
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
                : (version == 2) ? sizeof(ProjectFileV2)
                : (version == 3) ? V3_SIZE
                : (version == 4) ? V4_SIZE
                : (version == 5) ? sizeof(ProjectFile) : V6_HEAD;
    // v6 needs the head only; the older layouts carry 8 whole patterns in the struct
    uint8_t* raw = (uint8_t*)calloc(1, (version >= 3 && version <= 5) ? sizeof(ProjectFile) : need);   // v6: 1 KB, not 7.5
    if (!raw) { f.close(); return false; }
    size_t got = f.read(raw, need);
    uint8_t hi[SONG_LENGTH];                         // the slots' high mute bytes (v5 on)
    memset(hi, 0, sizeof(hi));
    if (got == need && version == 5) memcpy(hi, ((ProjectFile*)raw)->slotMuteHi, SONG_LENGTH);
    if (got == need && version >= 6)                 // v6 keeps them right after the head
        got = f.read(hi, SONG_LENGTH) == SONG_LENGTH ? need : 0;
    TailPattern* tail = nullptr;
    if (got == need && (version == 4 || version == 5)) tail = (TailPattern*)malloc(sizeof(TailPattern));
    if (got != need) { f.close(); free(raw); return false; }

    engineLock();                        // audio parked: nothing fires while the data is swapped
    for (int d = 0; d < NUM_DECKS; d++) deckUnload(d);
    samplerClearAll();
    for (int p = 0; p < NUM_PATTERNS; p++) { clearPattern((uint8_t)p); setPatternLength((uint8_t)p, STEPS_PER_BAR); }

    if (version >= 3) {
        ProjectFile* pf = (ProjectFile*)raw;
        g_bpm = pf->bpm;
        g_songLoopStart = pf->songLoopStart;
        for (int i = 0; i < SONG_LENGTH; i++) {
            const SaveSlot& in = pf->song[i];
            g_song[i] = { in.pattern, in.repeats, (uint16_t)(in.mute | (hi[i] << 8)), in.target, 0 };
            if (g_song[i].repeats < 1 || g_song[i].repeats > MAX_REPEATS) g_song[i].repeats = 1;
            if (g_song[i].pattern >= NUM_PATTERNS) g_song[i].pattern = SONG_EMPTY;
        }
        for (int s = 0; s < NUM_SYNTHS; s++) applySynth(s, pf->synths[s], pf->voices[s], version);
        for (int l = 0; l < NUM_DRUM_LANES; l++) applyLane(l, pf->lanes[l]);
        // v3 kept one mute per deck for the whole project
        uint16_t oldMute = 0;
        if (version == 3)
            for (int d = 0; d < NUM_DECKS; d++) if (pf->decks[d].mute) oldMute |= MUTE_DECK(d);
        const SaveFx& x = pf->fx;
        g_fx.blend = x.blend; g_fx.cutoff = x.cutoff; g_fx.reso = x.reso;
        g_fx.type = x.type < FLT_COUNT ? x.type : FLT_LP; g_fx.poles = x.poles ? 1 : 0;
        g_fx.pump = x.pump; g_fx.bassPump = x.bassPump; g_fx.ghost = x.ghost;
        g_fx.swing = x.swing; g_fx.key = x.key % 12; g_fx.bassMode = x.bassMode; g_fx.style = x.style;
        g_fx.release = x.release;
        if (version >= 6) {
            uint8_t dims[2] = { 0, 0 };
            bool fits = f.read(dims, 2) == 2 && dims[1] == sizeof(PatBar);
            for (int p = 0; fits && p < dims[0]; p++) {
                SavePatHead h;
                if (f.read((uint8_t*)&h, sizeof(h)) != sizeof(h)) break;
                Pattern scratch; memset((void*)&scratch, 0, sizeof(scratch));
                Pattern& o = (p < NUM_PATTERNS) ? g_patterns[p] : scratch;      // a file from a bigger build: skip the rest
                o.length = h.length; o.length = patLen(o);
                o.mute = (uint16_t)(h.mute & MUTE_MASK);
                for (int b = 0; fits && b < 8; b++) {
                    if (!(h.bars & (1 << b))) continue;
                    PatBar bar;
                    if (f.read((uint8_t*)&bar, sizeof(bar)) != sizeof(bar)) { fits = false; break; }
                    if (p >= NUM_PATTERNS || b >= patLen(o) / STEPS_PER_BAR) continue;
                    // through the pool's own writer: one lock keeps a bar it would otherwise not take
                    if (!patSetLock(o, (uint8_t)(b * STEPS_PER_BAR), 0)) continue;       // pool full: the bar is lost
                    g_bars[o.bar[b] - 1] = bar;
                }
            }
        } else {
        for (int p = 0; p < FILE_PATTERNS; p++) {
            Pattern& o = g_patterns[p];
            unpackPattern(pf->patterns[p], o);
            o.mute = (version == 3) ? oldMute : (uint16_t)(pf->mutes[p] & MUTE_MASK);
        }
        for (int p = FILE_PATTERNS; p < OLD_PATTERNS; p++) {     // the tail, if the file has one
            Pattern& o = g_patterns[p];
            if (version < 4 || !tail) continue;
            if (f.read((uint8_t*)tail, sizeof(TailPattern)) != sizeof(TailPattern)) { free(tail); tail = nullptr; continue; }
            unpackPattern(tail->pat, o);
            o.mute = (uint16_t)(tail->mute & MUTE_MASK);
        }
        }
        free(tail); tail = nullptr;
        for (int i = 0; i < SONG_LENGTH; i++) {
            SongSlot& sl = g_song[i];
            if (version >= 5) { sl.mute &= MUTE_MASK; continue; }
            // older files laid the slot's coarse mutes over the pattern's: the
            // slot now carries the sum, so the song sounds as it did
            uint16_t old = sl.mute, m = old & (MUTE_SYNTH(0) | MUTE_SYNTH(1) | MUTE_SYNTH(2));
            if (old & OLD_SLOT_DRUMS) m |= MUTE_ALL_LANES;
            if (old & OLD_SLOT_DECKA) m |= MUTE_DECK(0);
            if (old & OLD_SLOT_DECKB) m |= MUTE_DECK(1);
            if (old & OLD_SLOT_KICK)  m |= MUTE_LANE(0);
            if (sl.pattern != SONG_EMPTY) m |= g_patterns[sl.pattern].mute;
            sl.mute = m;
        }
        for (int d = 0; d < NUM_DECKS; d++) applyDeck(d, pf->decks[d]);
    } else if (version == 2) {
        ProjectFileV2* pf = (ProjectFileV2*)raw;
        legacyDefaults();
        g_bpm = pf->bpm;
        g_songLoopStart = pf->songLoopStart;
        applyOldSong(pf->song);
        for (int s = 0; s < NUM_SYNTHS; s++) applySynth(s, pf->synths[s], pf->voices[s], version);
        for (int l = 0; l < NUM_DRUM_LANES; l++) applyLane(l, pf->lanes[l]);
        for (int p = 0; p < FILE_PATTERNS; p++) {
            for (int s = 0; s < NUM_SYNTHS; s++)
                for (int st = 0; st < OLD_STEPS; st++) {
                    const SaveCell& c = pf->cells[p][s][st];
                    SynthCell o;
                    for (int i = 0; i < MAX_POLY; i++) { o.note[i] = c.note[i]; o.oct[i] = c.oct[i]; }
                    o.accent = c.flags & 1; o.slide = c.flags & 2;
                    patSetCell(g_patterns[p], (uint8_t)s, (uint8_t)st, o);
                }
            for (int st = 0; st < OLD_STEPS; st++) patSetDrums(g_patterns[p], (uint8_t)st, pf->drums[p][st], 0);
        }
    } else {              // v1: legacy mono cells, migrate on load
        ProjectFileV1* pf = (ProjectFileV1*)raw;
        legacyDefaults();
        g_bpm = pf->bpm;
        g_songLoopStart = pf->songLoopStart;
        applyOldSong(pf->song);
        for (int s = 0; s < NUM_SYNTHS; s++) applySynth(s, pf->synths[s], 1, version);
        for (int l = 0; l < NUM_DRUM_LANES; l++) applyLane(l, pf->lanes[l]);
        for (int p = 0; p < FILE_PATTERNS; p++) {
            for (int s = 0; s < NUM_SYNTHS; s++)
                for (int st = 0; st < OLD_STEPS; st++) {
                    const SaveCellV1& c = pf->cells[p][s][st];
                    SynthCell o; o.setMono(c.note, c.octave, c.flags & 1, c.flags & 2);
                    if (c.note == NOTE_EMPTY) o = SynthCell{};
                    patSetCell(g_patterns[p], (uint8_t)s, (uint8_t)st, o);
                }
            for (int st = 0; st < OLD_STEPS; st++) patSetDrums(g_patterns[p], (uint8_t)st, pf->drums[p][st], 0);
        }
    }
    free(raw);
    f.close();
    if (g_curStep >= patLen(g_patterns[g_curPattern])) g_curStep = 0;
    engineUnlock();
    return true;
}

// ---------- arrangements ----------
// One record per line. The device reads three kinds and skips everything else
// (the tool keeps its own notes in the same file):
//   BPM <n>                             tempo
//   P <pattern 1..64> <bars 1..4>       that pattern's length
//   S <pattern> <repeats> <mutes, hex>  the next song slot; bit 0 = T1 .. bit 12 = D8
int storageLoadArrangement(const char* filename) {
    char path[sizeof(DIR_SONGS) + LOOP_NAME_LEN + 2];
    snprintf(path, sizeof(path), "%s/%s", DIR_SONGS, filename);
    File f = SD.open(path, FILE_READ);
    if (!f) return -1;

    SongSlot slots[SONG_LENGTH];
    uint8_t  bars[NUM_PATTERNS];
    memset(bars, 0, sizeof(bars));
    int n = 0, bpm = 0, lineNo = 0;
    bool ok = true;
    char line[48];
    while (ok && f.available()) {
        size_t len = 0; uint8_t c = 0;
        while (f.read(&c, 1) == 1 && c != '\n')              // a longer line is cut: only tool lines are long
            if (c != '\r' && len < sizeof(line) - 1) line[len++] = (char)c;
        line[len] = 0;
        if (!len) continue;
        if (lineNo++ == 0) { ok = strncmp(line, "FILTRE-ARR", 10) == 0; continue; }
        unsigned a = 0, b = 0, m = 0; float t = 0;
        if (sscanf(line, "BPM %f", &t) == 1) bpm = (int)(t + 0.5f);
        else if (sscanf(line, "P %u %u", &a, &b) == 2) {
            if (a >= 1 && a <= NUM_PATTERNS && b >= 1 && b <= MAX_BARS) bars[a - 1] = (uint8_t)b;
        } else if (sscanf(line, "S %u %u %x", &a, &b, &m) == 3) {
            if (a < 1 || a > NUM_PATTERNS || b < 1) continue;
            if (n < SONG_LENGTH)
                slots[n++] = { (uint8_t)(a - 1), (uint8_t)(b > MAX_REPEATS ? MAX_REPEATS : b),
                               (uint16_t)(m & MUTE_MASK), TARGET_NONE, 0 };
        }
    }
    f.close();
    if (!ok || !n) return -1;

    engineLock();
    if (bpm >= 40 && bpm <= 300) g_bpm = (uint16_t)bpm;
    for (int p = 0; p < NUM_PATTERNS; p++)
        if (bars[p]) setPatternLength((uint8_t)p, (uint8_t)(bars[p] * STEPS_PER_BAR));   // growing repeats the bars it has
    for (int i = 0; i < SONG_LENGTH; i++) { if (i < n) g_song[i] = slots[i]; else songClearSlot(i); }
    g_songLoopStart = 0;
    if (g_curStep >= patLen(g_patterns[g_curPattern])) g_curStep = 0;
    engineUnlock();
    return n;
}
