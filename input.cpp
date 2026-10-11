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
#include "kbd_reader.h"

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

// a question on screen ("LOAD: Are you sure?"): the same key again says yes,
// any other key or a few seconds says no
enum : uint8_t { CONF_NONE = 0, CONF_LOAD, CONF_SAVE, CONF_STYLE, CONF_CLEAR, CONF_PASTE };
#define CONFIRM_MS 4000
static uint8_t  s_confKind = CONF_NONE, s_confAct = ACT_NONE, s_confArg = 0;
static uint32_t s_confUntil = 0;

static int8_t   s_copySrc = -1;        // pattern tagged by CPY, -1 = none yet

static void confirmAsk(uint8_t kind, uint8_t act, uint8_t arg, const char* question) {
    s_confKind = kind; s_confAct = act; s_confArg = arg;
    s_confUntil = millis() + CONFIRM_MS;
    uiStatusFor(question, CONFIRM_MS);
}

// filter keys: integrated over time so a sweep is smooth whatever the loop rate
static uint32_t s_fltLastMs = 0, s_cutSince = 0;
static float    s_cutAcc = 0.0f, s_resAcc = 0.0f;

// ---------- helpers ----------
static bool accentHeld(const KeySnap& s) { return s.has(KEY_ACCENT); }

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

// ---------- SETTINGS page parameter model (row layout: ui.h) ----------

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

// styles never unload loop audio; drums the player wrote are only replaced on a yes
static void applyStyle(uint8_t style, uint8_t act) {
    if (styleNeedsConfirm(style, g_curPattern)) {
        char m[28]; snprintf(m, sizeof(m), "%.9s: Replace drums?", g_styleNames[style]);
        confirmAsk(CONF_STYLE, act, style, m);
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
        case DR_STYLE:    applyStyle((uint8_t)((g_fx.style + dir + STYLE_COUNT) % STYLE_COUNT),
                                     dir > 0 ? ACT_RIGHT : ACT_LEFT); break;
        case DR_BPM:      g_bpm = (uint16_t)constrain((int)g_bpm + dir * (fine ? 1 : s_rptCount > 8 ? 4 : 1), 40, 300); break;
        case DR_LID:
        {
            bool was = g_lidOk;
            lidToggle();
            if (!was && !g_lidOk) uiStatus("NO LID FOUND");
            break;
        }
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

        case PAGE_SETTINGS: {
            bool fine = accentHeld(now);
            uint8_t rows = uiRowCount();
            if (g_soundParam >= rows) g_soundParam = 0;
            if (dy) g_soundParam = (uint8_t)((g_soundParam + rows + dy) % rows);
            if (dx) {
                uint8_t dr = uiRowDeckRow(g_soundParam);      // a deck row or one of the shared block
                if (dr < DR_COUNT) adjustDeckParam(trackIsDeck(g_curTrack) ? trackDeck(g_curTrack) : 0, dr, dx, fine);
                else if (g_curTrack == TRK_DRUMS) adjustDrumParam(g_curDrumLane, g_soundParam, dx, fine);
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

        default: break;
    }
    g_needRedraw = true;
}

// ---------- filter keys ----------
// PATTERN/SETTINGS/SAMPLE: the live filter. SONG: the slot under the cursor.
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

        case ACT_OCT_DN: case ACT_OCT_UP: {
            int dir = (act == ACT_OCT_UP) ? 1 : -1;
            char m[16];
            if (g_curPage == PAGE_SONG) {                 // the project slot LOAD / SAVE use
                g_curProject = (uint8_t)constrain((int)g_curProject + dir, 0, NUM_PROJECT_SLOTS - 1);
                snprintf(m, sizeof(m), "PROJECT P%u", g_curProject + 1);
            } else {
                g_curOctave = (uint8_t)constrain((int)g_curOctave + dir, 1, 7);
                snprintf(m, sizeof(m), "OCTAVE %u", g_curOctave);
            }
            uiStatus(m); break;
        }

        case ACT_PAT_DN: case ACT_PAT_UP: {
            int dir = (act == ACT_PAT_UP) ? 1 : -1;
            char m[24];
            if (g_curPage == PAGE_SONG) {                 // the pattern in the slot under the cursor
                SongSlot& sl = g_song[g_songCursor];
                if (sl.pattern == SONG_EMPTY) {           // a new slot starts as the one before it
                    uint8_t from = (g_songCursor && g_song[g_songCursor - 1].pattern != SONG_EMPTY)
                                   ? g_song[g_songCursor - 1].pattern : g_curPattern;
                    songClearSlot(g_songCursor);
                    sl.pattern = from;
                } else {
                    sl.pattern = (uint8_t)((sl.pattern + dir + NUM_PATTERNS) % NUM_PATTERNS);
                }
                g_needRedraw = true; break;
            }
            bool queue = g_playing && !g_songMode;        // lands when the playing pattern ends
            uint8_t to = (uint8_t)(((queue ? g_nextPattern : g_curPattern) + dir + NUM_PATTERNS) % NUM_PATTERNS);
            seqSelectPattern(to);
            if (!queue)                    snprintf(m, sizeof(m), "PATTERN %u", to + 1);
            else if (to == g_playPattern)  snprintf(m, sizeof(m), "STAY ON P%u", to + 1);
            else                           snprintf(m, sizeof(m), "NEXT: P%u", to + 1);
            uiStatus(m); g_needRedraw = true; break;
        }

        case ACT_ACCENT: break;   // pure modifier
        case ACT_KEYS:
            if (!g_lidOk) uiStatus("KEY MAP NEEDS THE LID");
            break;
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

// the 13 mute keys / grid rows by name
static void muteRowName(uint8_t i, char* o, size_t n) {
    if (i < NUM_SYNTHS)                  snprintf(o, n, "T%u", i + 1);
    else if (i < NUM_SYNTHS + NUM_DECKS) snprintf(o, n, "DECK %c", 'A' + i - NUM_SYNTHS);
    else                                 snprintf(o, n, "LANE %u", i - NUM_SYNTHS - NUM_DECKS + 1);
}

// ---------- short actions (release < 450ms) ----------
static void doShort(uint8_t act) {
    // the top row: mute that grid row. Mutes live in the pattern, and the one
    // you hear is the one that changes (in song mode that may not be on screen)
    if (act >= ACT_MUTE_FIRST && act <= ACT_MUTE_LAST) {
        uint8_t i = (uint8_t)(act - ACT_MUTE_FIRST);
        uint8_t pat = g_playing ? (uint8_t)g_playPattern : g_curPattern;
        uint16_t& mu = g_patterns[pat].mute;
        mu ^= (uint16_t)(1u << i);
        char nm[10], m[24];
        muteRowName(i, nm, sizeof(nm));
        if (pat != g_curPattern) snprintf(m, sizeof(m), "P%u %s %s", pat + 1, nm, (mu & (1u << i)) ? "MUTED" : "ON");
        else                     snprintf(m, sizeof(m), "%s %s", nm, (mu & (1u << i)) ? "MUTED" : "ON");
        uiStatus(m); g_needRedraw = true; return;
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
        case ACT_COPY: {
            char m[24];
            s_copySrc = (int8_t)g_curPattern;
            snprintf(m, sizeof(m), "P%u COPIED hold=PASTE", g_curPattern + 1);
            uiStatus(m); break;
        }
        case ACT_PAGE:
            g_curPage = (Page)(((int)g_curPage + 1) % PAGE_COUNT);
            if (g_curPage == PAGE_SAMPLE) uiScanSampleDir();
            if (g_curPage == PAGE_SETTINGS) g_soundParam = 0;
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
            g_nextPattern = g_curPattern;         // nothing stays queued across the switch
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
    if (act >= ACT_MUTE_FIRST && act <= ACT_MUTE_LAST) {     // hold = work on that row
        uint8_t i = (uint8_t)(act - ACT_MUTE_FIRST);
        if (i < NUM_SYNTHS)                  g_curTrack = i;
        else if (i < NUM_SYNTHS + NUM_DECKS) g_curTrack = (uint8_t)(TRK_DECK_A + i - NUM_SYNTHS);
        else { g_curTrack = TRK_DRUMS; g_curDrumLane = (uint8_t)(i - NUM_SYNTHS - NUM_DECKS); }
        if (g_soundParam >= uiRowCount()) g_soundParam = 0;
        if (g_curPage == PAGE_SAMPLE) uiScanSampleDir();
        char nm[10]; muteRowName(i, nm, sizeof(nm));
        uiStatus(nm); g_needRedraw = true; return;
    }
    switch (act) {
        case ACT_LOAD: case ACT_SAVE: {       // the hold only asks; the same key again does it
            char m[28];
            if (act == ACT_LOAD && !storageProjectExists(g_curProject)) {
                snprintf(m, sizeof(m), "P%u IS EMPTY", g_curProject + 1);
                uiStatus(m); break;
            }
            snprintf(m, sizeof(m), "%s P%u: Are you sure?", act == ACT_LOAD ? "LOAD" : "SAVE", g_curProject + 1);
            confirmAsk(act == ACT_LOAD ? CONF_LOAD : CONF_SAVE, act, 0, m);
            break;
        }
        case ACT_PAGE:
            g_curPage = PAGE_PATTERN; g_needRedraw = true; break;
        case ACT_PLAY:
            sequencerStart(true); g_needRedraw = true; break;
        case ACT_CLR: {                       // asks, like LOAD / SAVE
            char m[28];
            snprintf(m, sizeof(m), "CLEAR P%u: Are you sure?", g_curPattern + 1);
            confirmAsk(CONF_CLEAR, act, g_curPattern, m);
            break;
        }
        case ACT_COPY: {                      // paste the tagged pattern over this one
            char m[28];
            if (s_copySrc < 0)                        { uiStatus("TAP CPY ON A PATTERN FIRST"); break; }
            if ((uint8_t)s_copySrc == g_curPattern)   { snprintf(m, sizeof(m), "P%u IS THE COPY ITSELF", g_curPattern + 1);
                                                        uiStatus(m); break; }
            snprintf(m, sizeof(m), "PASTE P%u>P%u: You sure?", s_copySrc + 1, g_curPattern + 1);
            confirmAsk(CONF_PASTE, act, (uint8_t)(s_copySrc | (g_curPattern << 4)), m);
            break;
        }
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
                g_recPadKc = (uint8_t)KEY_AUX;
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
    return actLongName(act);
}

// the answer to confirmAsk()
static void confirmRun(uint8_t kind, uint8_t arg) {
    switch (kind) {
        case CONF_LOAD:
            uiStatus("LOADING..."); uiDrawNow();
            uiStatus(storageLoadProject(g_curProject) ? "LOADED" : "LOAD FAILED");
            break;
        case CONF_SAVE:
            uiStatus("SAVING..."); uiDrawNow();
            uiStatus(storageSaveProject(g_curProject) ? "SAVED" : "SAVE FAILED");
            break;
        case CONF_STYLE:
            styleApply(arg, g_curPattern);
            uiStatus(g_styleNames[arg]);
            break;
        case CONF_CLEAR:
            clearPattern(arg);
            uiStatus("PATTERN CLEARED");
            break;
        case CONF_PASTE: {
            char m[16];
            pastePattern(arg & 0x0F, arg >> 4);
            snprintf(m, sizeof(m), "PASTED > P%u", (arg >> 4) + 1);
            uiStatus(m);
            break;
        }
    }
    g_needRedraw = true;
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
    if (g_curPage == PAGE_SAMPLE) {                       // pad = put this sample on that lane
        int8_t lane = padLane(kc);
        if (lane < 0 || uiBrowsingLoops() || !g_fileCount) return;
        int slot = samplerLoad(g_fileList[g_fileSel]);
        if (strlen(g_fileList[g_fileSel]) >= SAMPLE_NAME_LEN) uiStatus("NAME TOO LONG (31)");
        else if (slot >= 0) {
            char m[20];
            DrumLane& d = g_drumLanes[lane];
            d.engine = ENG_SMPL; d.sampleSlot = (int8_t)slot; d.smp.init();
            snprintf(m, sizeof(m), "ON LANE %d", lane + 1);
            uiStatus(m);
        } else uiStatus("LOAD FAILED");
        g_needRedraw = true;
        return;
    }
    if (g_curPage != PAGE_PATTERN && g_curPage != PAGE_SETTINGS) return;

    if (g_curTrack == TRK_DRUMS) {
        int8_t lane = padLane(kc);
        if (lane < 0) return;
        g_curDrumLane = (uint8_t)lane;
        if (resamplePending()) { resampleCommit(lane); return; }
        if (g_curPage == PAGE_SETTINGS) triggerLaneLive(lane);
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
        if (g_curPage == PAGE_SETTINGS) g_recEnabled = false;   // SETTINGS page only auditions
        liveDeckHit(d, (uint8_t)sl, accent);
        g_recEnabled = keepRec;
        g_needRedraw = true;
        return;
    }

    int8_t semi = pianoSemi(kc);
    if (semi < 0) return;
    uint8_t note, oct; semiToNote(semi, note, oct);

    if (g_curPage == PAGE_SETTINGS) {
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
    s_confKind = CONF_NONE;
    g_holdProg = 0; g_holdLabel[0] = 0;
    s_fltLastMs = millis();
}

void inputUpdate() {
    M5Cardputer.update();
    uint32_t nowMs = millis();

    // the keyboard chip had to be rescued: say so, the old reader hung here
    {
        static uint16_t seen = 0;
        uint16_t n = (uint16_t)(g_kbdLostEdge + g_kbdOverflow + g_kbdGpiInt);
        if (n != seen) {
            seen = n;
            char m[28];
            snprintf(m, sizeof(m), "KBD FIX E%u O%u G%u", g_kbdLostEdge, g_kbdOverflow, g_kbdGpiInt);
            uiStatus(m);
        }
    }

    // -- CLR held while live-recording = erase at playhead --
    if (g_playing && g_recEnabled && s_prev.has(KEY_CLR)) clearStepAtPlayhead();

    seqFollowPattern();             // a queued pattern just came in: the editor goes with it

    filterKeysHeld(nowMs);
    if (s_confKind && (int32_t)(nowMs - s_confUntil) >= 0) s_confKind = CONF_NONE;   // nobody answered

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

    // -- build snapshot straight from the key matrix --
    // Not keysState(): it never reports the del key as `del`, and it drops
    // every other key while fn is held (fn is pad 1 here, so chords matter).
    KeySnap now;
    for (const auto& pos : M5Cardputer.Keyboard.keyList()) {
        uint8_t v = (uint8_t)M5Cardputer.Keyboard.getKeyValue(pos).value_first;   // unshifted legend
        switch (v) {
            case KEY_FN:         now.add(KC_FN);    break;
            case KEY_LEFT_SHIFT: now.add(KC_SHIFT); break;
            case KEY_LEFT_CTRL:  now.add(KC_CTRL);  break;
            case KEY_OPT:        now.add(KC_OPT);   break;
            case KEY_LEFT_ALT:   now.add(KC_ALT);   break;
            case KEY_TAB:        now.add(KC_TAB);   break;
            case KEY_BACKSPACE:  now.add(KC_DEL);   break;
            case KEY_ENTER:      now.add(KC_ENTER); break;
            case ' ':            now.add(KC_SPACE); break;
            default:             now.add((uint8_t)normalizeKey((char)v)); break;
        }
    }
    g_keysOverlay = now.has(KC_ENTER);         // the lid shows the key map while it is down
    // changed? (compare the sets: the library's isChange() only counts keys)
    bool same = (now.n == s_prev.n);
    for (uint8_t i = 0; same && i < now.n; i++) if (!s_prev.has(now.codes[i])) same = false;
    if (same) return;

    // -- newly pressed --
    for (uint8_t i = 0; i < now.n; i++) {
        uint8_t kc = now.codes[i];
        if (s_prev.has(kc)) continue;

        // a question is up: this key answers it (accent and the key map are not answers)
        if (s_confKind && kc != KEY_ACCENT && kc != KC_ENTER) {
            uint8_t kind = s_confKind;
            s_confKind = CONF_NONE;
            if (keyAction(kc) == s_confAct) { confirmRun(kind, s_confArg); continue; }
            uiStatus("CANCELLED");
        }

        // piano/pads take priority over any action bound to the same key
        if ((pianoSemi(kc) >= 0 &&
             (g_curPage == PAGE_PATTERN || g_curPage == PAGE_SETTINGS || g_curPage == PAGE_SONG)) ||
            (g_curPage == PAGE_SAMPLE && padLane(kc) >= 0 && !uiBrowsingLoops())) {
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
