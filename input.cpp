// ============================================================
// FILTRE - input.cpp  (from Microgroove)
// Snapshot diffing -> immediate / short / long press dispatch.
// All bindings live in keymap.h.
// ============================================================
#include <M5Cardputer.h>
#include "config.h"
#include "keymap.h"
#include "sequencer.h"
#include "sampler.h"
#include "storage.h"
#include "wavetable.h"
#include "styles.h"
#include "ui.h"
#include "lid_ui.h"
#include "mic_sampler.h"

void inputInit();
void inputUpdate();

#define LONG_PRESS_MS 450
#define HINT_AFTER_MS 140    // progress bar appears after this
#define RPT_DELAY_MS  350
#define RPT_RATE_MS    90

// ---------- snapshot of all 56 keys as synthetic codes ----------
struct KeySnap {
    uint8_t codes[16];
    uint8_t n = 0;
    void add(uint8_t kc) { if (n < sizeof(codes)) codes[n++] = kc; }
    bool has(uint8_t kc) const {
        for (uint8_t i = 0; i < n; i++) if (codes[i] == kc) return true;
        return false;
    }
};
static KeySnap s_prev;
uint8_t g_recPadKc = KC_NONE;          // key that ends mic capture on release

// pending short/long holds
struct Hold { uint8_t kc; uint8_t act; uint32_t t0; bool fired; };
static Hold    s_holds[8];
static uint8_t s_nHolds = 0;

// auto-repeat state (one repeating key at a time is plenty)
static uint8_t  s_rptAct  = ACT_NONE;
static uint8_t  s_rptKc   = KC_NONE;
static uint32_t s_rptNext = 0;
static uint16_t s_rptCount = 0;

// filter keys: integrated over time so a sweep is smooth whatever the loop rate
static uint32_t s_fltLastMs = 0, s_cutSince = 0;
static float    s_cutAcc = 0.0f, s_resAcc = 0.0f;

// ---------- helpers ----------
static bool accentHeld(const KeySnap& s) { return s.has('m'); }

static uint8_t heldPianoCount(const KeySnap& s) {
    uint8_t c = 0;
    for (uint8_t i = 0; i < s.n; i++) if (pianoSemi(s.codes[i]) >= 0) c++;
    return c;
}

static void semiToNote(int8_t semi, uint8_t& note, uint8_t& oct) {
    note = (uint8_t)(semi % 12) + 1;
    oct  = (uint8_t)constrain((int)g_curOctave + semi / 12, 1, 7);
}

static uint8_t curLen() { return patLen(g_patterns[g_curPattern]); }

static void setCutoff(int v) {
    g_fx.cutoff = (uint8_t)constrain(v, 0, 255);
    touchCutoff();
}

// ---------- SOUND page parameter model ----------
#define SYNTH_PARAMS 9
#define DRUM_PARAMS  7

static void adjustSynthParam(SynthTrack& t, uint8_t row, int dir, bool fine) {
    float step = fine ? 0.01f : 0.05f;
    if (row == 8) {                                        // VOICES 1..MAX_POLY
        t.setVoices((uint8_t)constrain((int)t.voices + dir, 1, MAX_POLY));
        return;
    }
    t.forEach([&](SynthVoice& v) {
        switch (row) {
            case 0: v.oscMode = (OscMode)(((int)v.oscMode + dir + OSC_COUNT) % OSC_COUNT); break;
            case 1: if (g_numWavetables)
                        v.wtIndex = (uint8_t)(((int)v.wtIndex + dir + g_numWavetables) % g_numWavetables);
                    break;
            case 2: v.fltCutoff  = constrain(v.fltCutoff  + dir * step, 0.0f, 1.0f); break;
            case 3: v.fltReso    = constrain(v.fltReso    + dir * step, 0.0f, 1.0f); break;
            case 4: v.fltEnvAmt  = constrain(v.fltEnvAmt  + dir * step, 0.0f, 1.0f); break;
            case 5: v.filtDecRate = constrain(v.filtDecRate + dir * (fine ? 0.00002f : 0.0001f),
                                              0.9950f, 0.99995f); break;
            case 6: v.ampDecRate  = constrain(v.ampDecRate  + dir * (fine ? 0.00001f : 0.00005f),
                                              0.9990f, 0.99999f); break;
            case 7: v.volume     = constrain(v.volume     + dir * step, 0.0f, 1.0f); break;
        }
    });
}

static int firstSample(int from, int dir) {
    for (int i = 1; i <= MAX_SAMPLES; i++) {
        int s = (from + dir * i + MAX_SAMPLES * 2) % MAX_SAMPLES;
        if (g_samples[s].used && g_samples[s].owner == OWN_ONESHOT) return s;
    }
    return -1;
}

static void adjustDrumParam(uint8_t lane, uint8_t row, int dir, bool fine) {
    DrumLane& d = g_drumLanes[lane];
    switch (row) {
        case 0: g_curDrumLane = (uint8_t)((lane + dir + NUM_DRUM_LANES) % NUM_DRUM_LANES); break;
        case 1: d.engine = (DrumEngine)(((int)d.engine + dir + ENG_COUNT) % ENG_COUNT);
                if (d.engine == ENG_808 && d.type >= DT_COUNT_808) d.type = DT_KICK;
                break;
        case 2:
            if (d.engine == ENG_SMPL) {
                int s = firstSample(d.sampleSlot < 0 ? 0 : d.sampleSlot, dir);
                if (s >= 0) d.sampleSlot = (int8_t)s;
            } else {
                int cnt = (d.engine == ENG_909) ? DT_COUNT : DT_COUNT_808;
                d.type = (uint8_t)(((int)d.type + dir + cnt) % cnt);
            }
            break;
        case 3: d.volume = constrain(d.volume + dir * (fine ? 0.01f : 0.05f), 0.0f, 1.0f); break;
        case 4: d.tune   = constrain(d.tune   + dir * (fine ? 0.1f  : 1.0f), -12.0f, 12.0f); break;
        case 5: d.decay  = constrain(d.decay  + dir * (fine ? 0.02f : 0.1f), 0.4f, 2.5f); break;
        case 6: d.chokeGroup = (uint8_t)(((int)d.chokeGroup + dir + 4) % 4); break;
    }
}

// styles never unload loop audio; drums the player wrote need a hold (HELP page)
static void applyStyle(uint8_t style, bool force) {
    if (!force && styleNeedsConfirm(style, g_curPattern)) {
        uiStatus("HELP pg: HOLD P1-P4");
        return;
    }
    styleApply(style, g_curPattern);
    uiStatus(g_styleNames[style]);
}

static void adjustDeckParam(uint8_t di, uint8_t row, int dir, bool fine) {
    Deck& k = g_decks[di];
    uint32_t len = deckLoaded(di) ? g_samples[k.slot].length : 0;
    switch (row) {
        case DR_MODE:
            k.mode = (uint8_t)((k.mode + 1) % DECK_MODE_COUNT);
            if (k.mode == DECK_LOOP && !k.fitBars) k.fitBars = deckGuessBars(di, g_bpm, false);
            break;
        case DR_SLICES:
            k.sliceMode = (uint8_t)((k.sliceMode + dir + SLICE_COUNT) % SLICE_COUNT);
            deckReslice(di); break;
        case DR_PITCH: k.pitch = constrain(k.pitch + dir * (fine ? 0.1f : 1.0f), -12.0f, 12.0f); break;
        case DR_LEVEL: k.level = constrain(k.level + dir * (fine ? 0.01f : 0.05f), 0.0f, 1.0f); break;
        case DR_FIT: {
            static const uint8_t opts[] = { 0, 1, 2, 4, 8 };
            int i = 0; for (int j = 0; j < 5; j++) if (opts[j] == k.fitBars) i = j;
            k.fitBars = opts[(i + dir + 5) % 5]; break;
        }
        case DR_BLEND:    g_fx.blend = (uint8_t)constrain((int)g_fx.blend + dir * (fine ? 1 : 8), 0, 127); break;
        case DR_CUTOFF:   setCutoff((int)g_fx.cutoff + dir * (fine ? 1 : 8)); break;
        case DR_RESO:     g_fx.reso = (uint8_t)constrain((int)g_fx.reso + dir * (fine ? 1 : 6), 0, 127); break;
        case DR_TYPE:     g_fx.type = (uint8_t)((g_fx.type + dir + FLT_COUNT) % FLT_COUNT); break;
        case DR_POLES:    g_fx.poles ^= 1; break;
        case DR_PUMP:     g_fx.pump = (uint8_t)constrain((int)g_fx.pump + dir * (fine ? 1 : 5), 0, 100); break;
        case DR_RELEASE:  g_fx.release = (uint16_t)constrain((int)g_fx.release + dir * (fine ? 5 : 20), 40, 400); break;
        case DR_BASSPUMP: g_fx.bassPump = (uint8_t)constrain((int)g_fx.bassPump + dir * (fine ? 1 : 5), 0, 100); break;
        case DR_GHOST:    g_fx.ghost ^= 1; break;
        case DR_SWING:    g_fx.swing = (uint8_t)constrain((int)g_fx.swing + dir, 50, 72); break;
        case DR_KEY:      g_fx.key = (uint8_t)((g_fx.key + dir + 12) % 12); break;
        case DR_BASSMODE: g_fx.bassMode = (uint8_t)((g_fx.bassMode + dir + BASS_COUNT) % BASS_COUNT); break;
        case DR_STYLE:    applyStyle((uint8_t)((g_fx.style + dir + STYLE_COUNT) % STYLE_COUNT), false); break;
        case DR_PATLEN:
            setPatternLength(g_curPattern, curLen() == NUM_STEPS ? STEPS_PER_BAR : NUM_STEPS);
            uiStatus(curLen() == NUM_STEPS ? "2 BARS" : "1 BAR"); break;
        case DR_SLICEPOS: deckNudgeSlice(di, k.lastSlice, dir * (fine ? 22 : 220)); break;
        case DR_IN:
            if (len) deckSetRegion(di, (uint32_t)constrain((int32_t)k.in + dir * (int32_t)(len / (fine ? 2048 : 64)),
                                                           0, (int32_t)k.out - 512), k.out);
            break;
        case DR_OUT:
            if (len) deckSetRegion(di, k.in, (uint32_t)constrain((int32_t)k.out + dir * (int32_t)(len / (fine ? 2048 : 64)),
                                                                 (int32_t)k.in + 512, (int32_t)len - 1));
            break;
        case DR_START:
            k.startTenths = (uint8_t)constrain((int)k.startTenths + dir * (fine ? 1 : 10), 0, 250);
            uiStatus("APPLIES ON NEXT LOAD"); break;
        case DR_QUAL: k.qual = (uint8_t)((k.qual + dir + QUAL_COUNT) % QUAL_COUNT);
            uiStatus("APPLIES ON NEXT LOAD"); break;
        case DR_SETBPM: {
            uint8_t bars = k.fitBars ? k.fitBars : deckGuessBars(di, g_bpm, true);
            float bpm = deckBpmForBars(di, bars);
            if (bpm >= 40 && bpm <= 300) { g_bpm = (uint16_t)(bpm + 0.5f); k.fitBars = bars; uiStatus("BPM FROM LOOP"); }
            break;
        }
    }
}

// PATTERN rows top to bottom: T1 T2 T3, deck A, deck B, drum lanes 8..1
#define GRID_ROWS (NUM_SYNTHS + NUM_DECKS + NUM_DRUM_LANES)
static int gridRow() {
    if (g_curTrack < NUM_SYNTHS) return g_curTrack;
    if (trackIsDeck(g_curTrack)) return NUM_SYNTHS + trackDeck(g_curTrack);
    return NUM_SYNTHS + NUM_DECKS + (NUM_DRUM_LANES - 1 - g_curDrumLane);
}
static void setGridRow(int r) {
    r = constrain(r, 0, GRID_ROWS - 1);
    if (r < NUM_SYNTHS) g_curTrack = (uint8_t)r;
    else if (r < NUM_SYNTHS + NUM_DECKS) g_curTrack = (uint8_t)(TRK_DECK_A + r - NUM_SYNTHS);
    else { g_curTrack = TRK_DRUMS; g_curDrumLane = (uint8_t)(NUM_DRUM_LANES - 1 - (r - NUM_SYNTHS - NUM_DECKS)); }
}

// ---------- arrows, per page ----------
static void arrow(uint8_t act, const KeySnap& now) {
    int dx = (act == ACT_LEFT) ? -1 : (act == ACT_RIGHT) ? 1 : 0;
    int dy = (act == ACT_UP)   ? -1 : (act == ACT_DOWN)  ? 1 : 0;

    switch (g_curPage) {
        case PAGE_PATTERN:
            if (dy) setGridRow(gridRow() + dy);
            if (dx) g_curStep = (uint8_t)((g_curStep + curLen() + dx) % curLen());   // crossing 16 flips the bar
            break;

        case PAGE_SOUND: {
            bool fine = accentHeld(now);
            if (trackIsDeck(g_curTrack)) {
                if (dy) g_soundParam = (uint8_t)((g_soundParam + DR_COUNT + dy) % DR_COUNT);
                if (dx) adjustDeckParam(trackDeck(g_curTrack), g_soundParam, dx, fine);
                break;
            }
            uint8_t rows = (g_curTrack == TRK_DRUMS) ? DRUM_PARAMS : SYNTH_PARAMS;
            if (g_soundParam >= rows) g_soundParam = 0;
            if (dy) g_soundParam = (uint8_t)((g_soundParam + rows + dy) % rows);
            if (dx) {
                if (g_curTrack == TRK_DRUMS) adjustDrumParam(g_curDrumLane, g_soundParam, dx, fine);
                else adjustSynthParam(g_synths[g_curTrack], g_soundParam, dx, fine);
            }
            break;
        }
        case PAGE_SAMPLE:
            if (dy < 0 && g_fileSel > 0) g_fileSel--;
            if (dy > 0 && g_fileSel + 1 < g_fileCount) g_fileSel++;
            break;

        case PAGE_SONG:
            if (dy) g_songCursor = (uint8_t)((g_songCursor + SONG_LENGTH + dy * 16) % SONG_LENGTH);
            if (dx) g_songCursor = (uint8_t)((g_songCursor + SONG_LENGTH + dx) % SONG_LENGTH);
            break;

        case PAGE_HELP: {
            int total = (int)(HELP_STEP_LINES + HELP_KEY_LINES) + 4;
            g_helpScroll = (uint8_t)constrain((int)g_helpScroll + dy * 5, 0, total - 5);
            break;
        }
        default: break;
    }
    g_needRedraw = true;
}

// ---------- filter keys ----------
// PATTERN/SOUND/SAMPLE/HELP: the live filter. SONG: the slot under the cursor.
static void filterKeyDown(uint8_t act) {
    if (g_curPage == PAGE_SONG) {
        SongSlot& sl = g_song[g_songCursor];
        if (sl.pattern == SONG_EMPTY) { uiStatus("PLACE A PATTERN FIRST"); return; }
        if (act == ACT_CUT_DN || act == ACT_CUT_UP) {
            int dir = (act == ACT_CUT_UP) ? 1 : -1;
            if (sl.target == TARGET_NONE) sl.target = g_fx.cutoff > 254 ? 254 : g_fx.cutoff;
            else if (sl.target == 0 && dir < 0) sl.target = TARGET_NONE;      // below shut = no target
            else sl.target = (uint8_t)constrain((int)sl.target + dir * 8, 0, 254);
        } else {
            sl.repeats = (uint8_t)constrain((int)sl.repeats + (act == ACT_RES_UP ? 1 : -1), 1, MAX_REPEATS);
        }
        g_needRedraw = true;
        return;
    }
    if (act == ACT_CUT_DN || act == ACT_CUT_UP) {
        setCutoff((int)g_fx.cutoff + (act == ACT_CUT_UP ? 2 : -2));   // a tap is a small, exact move
        s_cutSince = millis(); s_cutAcc = 0;
        g_cutHeld = true;
    } else {
        g_fx.reso = (uint8_t)constrain((int)g_fx.reso + (act == ACT_RES_UP ? 1 : -1), 0, 127);
        s_resAcc = 0;
    }
    g_needRedraw = true;
}

static void filterKeysHeld(uint32_t nowMs) {
    uint32_t dt = nowMs - s_fltLastMs;
    s_fltLastMs = nowMs;
    if (dt > 100) dt = 100;
    bool dn = s_prev.has('e'), up = s_prev.has('u');
    if (g_curPage == PAGE_SONG) { g_cutHeld = false; return; }
    if (dn != up) {
        // ~1.4 s across the whole range once it gets going, slower for the first 300 ms
        float rate = (nowMs - s_cutSince > 300) ? 0.19f : 0.07f;
        s_cutAcc += (float)dt * rate;
        int n = (int)s_cutAcc;
        if (n) { s_cutAcc -= n; setCutoff((int)g_fx.cutoff + (up ? n : -n)); g_needRedraw = true; }
        g_cutHeld = true;
    } else {
        g_cutHeld = false;
    }
    bool rd = s_prev.has(KC_TAB), ru = s_prev.has('p');
    if (rd != ru) {
        s_resAcc += (float)dt * 0.045f;
        int n = (int)s_resAcc;
        if (n) { s_resAcc -= n; g_fx.reso = (uint8_t)constrain((int)g_fx.reso + (ru ? n : -n), 0, 127); g_needRedraw = true; }
    }
}

// ---------- immediate actions (key-down) ----------
static void doImmediate(uint8_t act, const KeySnap& now) {
    switch (act) {
        case ACT_LEFT: case ACT_RIGHT: case ACT_UP: case ACT_DOWN:
            arrow(act, now); break;

        case ACT_CUT_DN: case ACT_CUT_UP: case ACT_RES_DN: case ACT_RES_UP:
            filterKeyDown(act); break;

        case ACT_REC:
            g_recEnabled = !g_recEnabled;
            uiStatus(g_recEnabled ? "REC ON" : "REC OFF");
            g_needRedraw = true; break;

        case ACT_ACCENT: break;   // pure modifier
        default: break;
    }
}

// load the highlighted loop into a deck (blocking: paint the status first)
static void loadLoop(uint8_t d) {
    if (!g_fileCount) return;
    uiStatus("LOADING..."); uiDrawNow();
    if (deckLoad(d, g_fileList[g_fileSel])) {
        Deck& k = g_decks[d];
        k.fitBars = deckGuessBars(d, g_bpm, false);
        char m[24];
        if (k.cut)            snprintf(m, sizeof(m), "CUT TO %.1fs", (float)g_samples[k.slot].length / SAMPLE_RATE);
        else if (k.fitBars)   snprintf(m, sizeof(m), "DECK %c: %u BAR", 'A' + d, k.fitBars);
        else                  snprintf(m, sizeof(m), "DECK %c: FIT?", 'A' + d);
        uiStatus(m);
        g_curTrack = (uint8_t)(TRK_DECK_A + d);
    } else {
        uiStatus("LOAD FAILED");
    }
    g_needRedraw = true;
}

// ---------- short actions (release < 450ms) ----------
static void doShort(uint8_t act) {
    // track select
    if (act >= ACT_TRACK1 && act <= ACT_TRACK3) {
        g_curTrack = (uint8_t)(act - ACT_TRACK1);
        if (g_curPage == PAGE_SAMPLE) uiScanSampleDir();
        g_needRedraw = true; return;
    }
    if (act == ACT_TRACKD) {                              // drums -> deck A -> deck B -> drums
        if (g_curTrack == TRK_DRUMS)        g_curTrack = TRK_DECK_A;
        else if (g_curTrack == TRK_DECK_A)  g_curTrack = TRK_DECK_B;
        else                                g_curTrack = TRK_DRUMS;
        uiStatus(g_curTrack == TRK_DRUMS ? "DRUMS" : g_curTrack == TRK_DECK_A ? "DECK A" : "DECK B");
        if (g_curPage == PAGE_SAMPLE) uiScanSampleDir();
        g_soundParam = 0;
        g_needRedraw = true; return;
    }
    // pattern keys: context
    if (act >= ACT_PAT1 && act <= ACT_PAT8) {
        uint8_t k = (uint8_t)(act - ACT_PAT1);
        if (g_curPage == PAGE_SAMPLE) {
            if (uiBrowsingLoops()) {
                if (k < NUM_DECKS) loadLoop(k); else uiStatus("P1 = DECK A  P2 = DECK B");
            } else if (g_fileCount) {
                int slot = samplerLoad(g_fileList[g_fileSel]);
                if (strlen(g_fileList[g_fileSel]) >= SAMPLE_NAME_LEN) uiStatus("NAME TOO LONG (31)");
                else if (slot >= 0) {
                    DrumLane& d = g_drumLanes[k];
                    d.engine = ENG_SMPL; d.sampleSlot = (int8_t)slot; d.smp.init();
                    uiStatus("ASSIGNED");
                } else uiStatus("LOAD FAILED");
                g_needRedraw = true;
            }
        } else if (g_curPage == PAGE_SONG) {
            SongSlot& sl = g_song[g_songCursor];
            if (sl.pattern == SONG_EMPTY) songClearSlot(g_songCursor);
            sl.pattern = k;
            g_songCursor = (g_songCursor + 1) % SONG_LENGTH;
            g_needRedraw = true;
        } else if (g_curPage == PAGE_HELP && k < STYLE_COUNT) {
            applyStyle(k, false);
            g_needRedraw = true;
        } else {
            g_curPattern = k;
            if (g_curStep >= curLen()) g_curStep = 0;
            uiStatus("PATTERN"); g_needRedraw = true;
        }
        return;
    }

    switch (act) {
        case ACT_LOAD: {
            char m[24];
            snprintf(m, sizeof(m), "P%u%s hold=LOAD", g_curProject + 1,
                     storageProjectExists(g_curProject) ? "*" : " empty");
            uiStatus(m); break;
        }
        case ACT_SAVE: {
            char m[24];
            snprintf(m, sizeof(m), "P%u hold=SAVE", g_curProject + 1);
            uiStatus(m); break;
        }
        case ACT_BPM_DN:
            g_bpm = (uint16_t)constrain((int)g_bpm - 1, 40, 300);
            g_needRedraw = true; break;
        case ACT_BPM_UP:
            g_bpm = (uint16_t)constrain((int)g_bpm + 1, 40, 300);
            g_needRedraw = true; break;

        case ACT_PAGE:
            g_curPage = (Page)(((int)g_curPage + 1) % PAGE_COUNT);
            if (g_curPage == PAGE_SAMPLE) uiScanSampleDir();
            if (g_curPage == PAGE_SOUND) g_soundParam = 0;
            g_needRedraw = true; break;

        case ACT_PLAY:
            if (g_playing) sequencerStop(); else sequencerStart(false);
            g_needRedraw = true; break;

        case ACT_CLR:
            if (g_curPage == PAGE_SONG) songClearSlot(g_songCursor);
            else clearCellAtCursor();
            g_needRedraw = true; break;

        case ACT_SONG:
            g_songMode = !g_songMode;
            uiStatus(g_songMode ? "SONG MODE" : "PATTERN MODE");
            g_needRedraw = true; break;

        case ACT_SLIDE:
            if (g_curTrack < NUM_SYNTHS) {
                SynthCell& c = g_patterns[g_curPattern].synth[g_curTrack][g_curStep];
                if (!c.empty()) {
                    c.slide = !c.slide;
                    if (c.slide && g_synths[g_curTrack].voices > 1)
                        uiStatus("SLIDE = MONO ONLY");
                    g_needRedraw = true;
                }
            } else {
                uiStatus("hold = WRITE BASS");
            }
            break;

        case ACT_AUX:
            if (g_curPage == PAGE_SAMPLE && g_fileCount) {
                if (uiBrowsingLoops()) { loadLoop(trackDeck(g_curTrack)); break; }
                int slot = samplerLoad(g_fileList[g_fileSel]);
                if (slot >= 0) auditionSample(slot);
                else uiStatus("LOAD FAILED");
                g_needRedraw = true;
            } else if (g_curPage == PAGE_SONG) {
                g_songLoopStart = g_songCursor;
                uiStatus("LOOP SET"); g_needRedraw = true;
            } else if (g_curPage == PAGE_HELP) {
                lidToggle();
                uiStatus(g_lidOk ? "LID ON" : "LID OFF"); g_needRedraw = true;
            } else {
                uiStatus("hold = MIC SAMPLE");
            }
            break;
        default: break;
    }
}

static void bounceProgress(float done, const char* what) {
    g_holdProg = done < 0.02f ? 0.02f : done;
    snprintf(g_holdLabel, sizeof(g_holdLabel), "BOUNCE %s", what);
    uiDrawNow();
}

// ---------- long actions (held 450ms) ----------
static void doLong(uint8_t act) {
    if (act >= ACT_TRACK1 && act <= ACT_TRACK3) {
        uint8_t t = (uint8_t)(act - ACT_TRACK1);
        g_synthMute[t] = !g_synthMute[t];
        uiStatus(g_synthMute[t] ? "MUTED" : "UNMUTED");
        g_needRedraw = true; return;
    }
    if (act == ACT_TRACKD) {                              // mutes whatever TD is on
        if (trackIsDeck(g_curTrack)) {
            Deck& k = g_decks[trackDeck(g_curTrack)];
            k.mute = !k.mute;
            uiStatus(k.mute ? "DECK MUTED" : "DECK ON");
        } else {
            g_drumMute = !g_drumMute;
            uiStatus(g_drumMute ? "DRUMS MUTED" : "DRUMS ON");
        }
        g_needRedraw = true; return;
    }
    if (act >= ACT_PAT1 && act <= ACT_PAT8) {
        uint8_t k = (uint8_t)(act - ACT_PAT1);
        if (g_curPage == PAGE_HELP) {                     // hold = replace the player's own drums too
            if (k < STYLE_COUNT) { applyStyle(k, true); g_needRedraw = true; }
            return;
        }
        if (g_curPage == PAGE_SAMPLE || g_curPage == PAGE_SONG) return;
        clonePatternTo(k);
        g_curPattern = k;
        char m[16]; snprintf(m, sizeof(m), "CLONED>%u", k + 1);
        uiStatus(m); g_needRedraw = true; return;
    }
    switch (act) {
        case ACT_LOAD:
            uiStatus("LOADING..."); uiDrawNow();
            uiStatus(storageLoadProject(g_curProject) ? "LOADED" : "LOAD FAILED");
            g_needRedraw = true; break;
        case ACT_SAVE:
            uiStatus(storageSaveProject(g_curProject) ? "SAVED" : "SAVE FAILED");
            break;
        case ACT_PAGE:
            g_curPage = PAGE_PATTERN; g_needRedraw = true; break;
        case ACT_PLAY:
            sequencerStart(true); g_needRedraw = true; break;
        case ACT_CLR:
            clearPattern(g_curPattern);
            uiStatus("PATTERN CLEARED"); g_needRedraw = true; break;
        case ACT_BPM_DN:
            if (g_curPage == PAGE_SONG) {
                if (g_curProject > 0) g_curProject--;
                uiStatus("PROJECT");
            } else if (g_curOctave > 1) g_curOctave--;
            g_needRedraw = true; break;
        case ACT_BPM_UP:
            if (g_curPage == PAGE_SONG) {
                if (g_curProject < NUM_PROJECT_SLOTS - 1) g_curProject++;
                uiStatus("PROJECT");
            } else if (g_curOctave < 7) g_curOctave++;
            g_needRedraw = true; break;

        case ACT_SONG:                        // long SONG = resample the mix
            if (!g_playing) uiStatus("PLAY, THEN HOLD");
            else if (!resampleArm(g_curTrack)) uiStatus("NO ROOM / BUSY");
            break;

        case ACT_SLIDE:                       // long SLD = write the bass line
            writeBass(g_curPattern, g_fx.bassMode);
            { char m[24]; snprintf(m, sizeof(m), "BASS %s in %s", g_bassModeNames[g_fx.bassMode], g_keyNames[g_fx.key]);
              uiStatus(m); }
            g_needRedraw = true; break;

        case ACT_AUX:
            if (g_curPage == PAGE_SONG) {     // long AUX on SONG = bounce mix + stems
                if (!g_sdOk) { uiStatus("NO SD CARD"); break; }
                int take = storageBounce(bounceProgress);
                g_holdProg = 0; g_holdLabel[0] = 0;
                char m[24];
                if (take > 0) snprintf(m, sizeof(m), "BOUNCED T%02d", take); else snprintf(m, sizeof(m), "BOUNCE FAILED");
                uiStatus(m); g_needRedraw = true;
            } else if (micRecStart(g_curTrack, g_curDrumLane)) {
                g_recPadKc = (uint8_t)'.';
            } else {
                uiStatus("MIC BUSY / FULL");
            }
            break;
        default: break;
    }
}

// label next to the hold bar; a few holds mean something else on some pages
static const char* longName(uint8_t act) {
    if (act == ACT_AUX && g_curPage == PAGE_SONG) return "BOUNCE";
    if (g_curPage == PAGE_HELP && act >= ACT_PAT1 && act <= ACT_PAT4) return "STYLE";
    if ((g_curPage == PAGE_SAMPLE || g_curPage == PAGE_SONG) && act >= ACT_PAT1 && act <= ACT_PAT8) return "";
    return actLongName(act);
}

// ---------- piano / pads (key-down, latency-critical path) ----------
static void doPiano(uint8_t kc, const KeySnap& now) {
    bool accent = accentHeld(now);

    if (g_curPage == PAGE_SONG) {                         // slot mutes
        int8_t bit = padSlotMute(kc);
        SongSlot& sl = g_song[g_songCursor];
        if (bit >= 0 && sl.pattern != SONG_EMPTY) { sl.mute ^= (uint8_t)(1 << bit); g_needRedraw = true; }
        return;
    }
    if (g_curPage != PAGE_PATTERN && g_curPage != PAGE_SOUND) return;

    if (g_curTrack == TRK_DRUMS) {
        int8_t lane = padLane(kc);
        if (lane < 0) return;
        g_curDrumLane = (uint8_t)lane;
        if (resamplePending()) { resampleCommit(lane); return; }
        if (g_curPage == PAGE_SOUND) triggerLaneLive(lane);
        else                         liveDrumHit(lane, accent);
        g_needRedraw = true;
        return;
    }

    if (trackIsDeck(g_curTrack)) {
        uint8_t d = trackDeck(g_curTrack);
        int8_t sl = padSlice(kc);
        if (sl < 0) return;
        if (!deckLoaded(d)) { uiStatus("PG > SAMPLE: LOAD A LOOP"); return; }
        if (sl >= g_decks[d].nSlices) return;
        g_decks[d].lastSlice = (uint8_t)sl;
        bool keepRec = g_recEnabled;
        if (g_curPage == PAGE_SOUND) g_recEnabled = false;   // SOUND page only auditions
        liveDeckHit(d, (uint8_t)sl, accent);
        g_recEnabled = keepRec;
        g_needRedraw = true;
        return;
    }

    int8_t semi = pianoSemi(kc);
    if (semi < 0) return;
    uint8_t note, oct; semiToNote(semi, note, oct);

    if (g_curPage == PAGE_SOUND) {
        auditionSynth(g_curTrack, note, oct, accent);
    } else {
        bool legato = heldPianoCount(s_prev) >= 1;
        liveSynthNote(g_curTrack, note, oct, accent, legato);
    }
}

// ---------- main entry ----------
void inputInit() {
    s_prev = KeySnap();
    s_nHolds = 0; s_rptAct = ACT_NONE;
    g_holdProg = 0; g_holdLabel[0] = 0;
    s_fltLastMs = millis();
}

void inputUpdate() {
    M5Cardputer.update();
    uint32_t nowMs = millis();

    // -- CLR held while live-recording = erase at playhead --
    if (g_playing && g_recEnabled && s_prev.has('z')) clearStepAtPlayhead();

    filterKeysHeld(nowMs);

    // -- auto-repeat --
    if (s_rptAct != ACT_NONE && s_prev.has(s_rptKc) && nowMs >= s_rptNext) {
        s_rptCount++;
        doImmediate(s_rptAct, s_prev);
        s_rptNext = nowMs + RPT_RATE_MS;
    }

    // -- long-press firing + progress bar (runs even without key changes) --
    float prog = 0.0f; const char* lbl = "";
    for (uint8_t i = 0; i < s_nHolds; i++) {
        Hold& h = s_holds[i];
        uint32_t held = nowMs - h.t0;
        // CLR in erase-mode never fires short/long
        if (h.act == ACT_CLR && g_playing && g_recEnabled) { h.fired = true; continue; }
        if (!h.fired && held >= LONG_PRESS_MS) { h.fired = true; doLong(h.act); }
        if (!h.fired && held >= HINT_AFTER_MS) {
            float p = (float)(held - HINT_AFTER_MS) / (LONG_PRESS_MS - HINT_AFTER_MS);
            const char* nm = longName(h.act);
            if (p > prog && nm[0]) { prog = p; lbl = nm; }
        }
    }
    // chord: LOAD + SAVE held simultaneously = DEMO
    {
        int iLoad = -1, iSave = -1;
        for (uint8_t i = 0; i < s_nHolds; i++) {
            if (s_holds[i].act == ACT_LOAD && !s_holds[i].fired) iLoad = i;
            if (s_holds[i].act == ACT_SAVE && !s_holds[i].fired) iSave = i;
        }
        if (iLoad >= 0 && iSave >= 0) {
            s_holds[iLoad].fired = true;
            s_holds[iSave].fired = true;
            uiStatus(loadDemoPattern() ? "DEMO: PLAY" : "DEMO: NO LOOP");
            g_needRedraw = true;
            prog = 0; lbl = "";
        }
    }

    // during mic capture the footer bar is the level meter (mic_sampler owns it)
    if (!micRecActive() && prog != g_holdProg) {
        static uint32_t lastAnim = 0;
        g_holdProg = prog;
        strncpy(g_holdLabel, lbl, sizeof(g_holdLabel) - 1);
        g_holdLabel[sizeof(g_holdLabel) - 1] = 0;
        if (nowMs - lastAnim > 40) { lastAnim = nowMs; g_needRedraw = true; }
    }

    if (!M5Cardputer.Keyboard.isChange()) return;

    // -- build snapshot --
    Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();
    KeySnap now;
    if (st.fn)    now.add(KC_FN);
    if (st.shift) now.add(KC_SHIFT);
    if (st.ctrl)  now.add(KC_CTRL);
    if (st.opt)   now.add(KC_OPT);
    if (st.alt)   now.add(KC_ALT);
    if (st.tab)   now.add(KC_TAB);
    if (st.del)   now.add(KC_DEL);
    if (st.enter) now.add(KC_ENTER);
    if (st.space) now.add(KC_SPACE);
    for (auto k : st.word) now.add((uint8_t)normalizeKey(k));

    // -- newly pressed --
    for (uint8_t i = 0; i < now.n; i++) {
        uint8_t kc = now.codes[i];
        if (s_prev.has(kc)) continue;

        // piano/pads take priority over any action bound to the same key
        if (pianoSemi(kc) >= 0 &&
            (g_curPage == PAGE_PATTERN || g_curPage == PAGE_SOUND || g_curPage == PAGE_SONG)) {
            doPiano(kc, now);
            continue;
        }

        uint8_t act = keyAction(kc);
        if (act == ACT_NONE) continue;

        if (actImmediate(act)) {
            doImmediate(act, now);
            if (actRepeats(act)) {
                s_rptAct = act; s_rptKc = kc;
                s_rptNext = nowMs + RPT_DELAY_MS; s_rptCount = 0;
            }
        } else if (s_nHolds < 8) {
            s_holds[s_nHolds++] = { kc, act, nowMs, false };
        }
    }

    // -- mic capture ends when AUX is released --
    if (g_recPadKc != KC_NONE && !now.has(g_recPadKc)) {
        g_recPadKc = KC_NONE;
        micRecStop();
    }

    // -- released --
    for (uint8_t i = 0; i < s_nHolds; ) {
        if (!now.has(s_holds[i].kc)) {
            if (!s_holds[i].fired) doShort(s_holds[i].act);
            s_holds[i] = s_holds[--s_nHolds];
        } else i++;
    }
    if (s_rptAct != ACT_NONE && !now.has(s_rptKc)) { s_rptAct = ACT_NONE; s_rptCount = 0; }

    s_prev = now;
}
