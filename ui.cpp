// ============================================================
// FILTRE - ui.cpp  (from Microgroove)
// 240x135, sprite double-buffered in 16 colours. 4 pages.
// With the lid open the lid is the editor (pattern grid, settings list) and
// this screen is the lens beside it (deck waveforms, the focused setting).
// With no lid, this screen is the editor.
// Footer doubles as long-press progress bar / mic level meter.
// ============================================================
#include <M5Cardputer.h>
#include <SD.h>
#include "config.h"
#include "ui.h"
#include "lid_ui.h"
#include "keymap.h"
#include "sequencer.h"
#include "sampler.h"
#include "storage.h"
#include "styles.h"
#include "wavetable.h"
#include "audio_engine.h"
#include "logo.h"
#include <math.h>

bool    g_needRedraw = true;
uint8_t g_soundParam = 0;
float   g_holdProg   = 0.0f;
char    g_holdLabel[16] = "";

char    g_fileList[BROWSER_MAX][LOOP_NAME_LEN];
uint8_t g_fileCount = 0;
uint8_t g_fileSel   = 0;
bool    g_arrList   = false;

static M5Canvas canvas(&M5Cardputer.Display);
static char     s_status[28] = "";
static uint32_t s_statusUntil = 0;
static bool     s_listIsLoops = false;

// palette: index = COL_* in config.h
static const uint8_t k_palette[16][3] = {
    {0x00,0x00,0x00}, {0x2C,0x2C,0x2C}, {0xFF,0xFF,0xFF}, {0x7B,0x7D,0x7B},   // bg grid text dim
    {0x00,0xFF,0xFF}, {0x00,0xFF,0x00}, {0xFF,0xFF,0x00}, {0xFF,0x00,0xFF},   // synth1-3 drums
    {0xFF,0xA5,0x00}, {0xFF,0x00,0x00}, {0x39,0x3D,0x39}, {0x63,0x30,0x00},   // accent rec playbg accbg
    {0xF2,0xE6,0xD0}, {0x30,0xC8,0xB0}, {0xFF,0xB0,0x00}, {0xFF,0x4D,0x2E},   // deckA deckB amber hot
};

static const char* noteNames[] = {"--","C-","C#","D-","D#","E-","F-","F#","G-","G#","A-","A#","B-"};
static const uint8_t trackCols[NUM_TRACKS] = { COL_SYNTH1, COL_SYNTH2, COL_SYNTH3, COL_DRUMS, COL_DECKA, COL_DECKB };
static const char* engNames[]  = {"808","909","SMP"};
static const char* type808[]   = {"KICK","SNARE","CHAT","CLAP"};
static const char* type909[]   = {"KICK","SNARE","CHAT","OHAT","CLAP","RIM"};
static const char* oscNames[]  = {"SAW","SQR","TRI","SIN","WT"};
static const char  sliceChar[] = "123456789ABCDEFG";

void uiInit() {
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    canvas.setColorDepth(4);               // 16 KB instead of 65 KB: the arena needs the RAM
    canvas.createSprite(SCREEN_W, SCREEN_H);
    canvas.createPalette();
    for (int i = 0; i < 16; i++)
        canvas.setPaletteColor(i, k_palette[i][0], k_palette[i][1], k_palette[i][2]);
    canvas.setTextFont(1);
    canvas.setTextSize(1);
}

void uiStatusFor(const char* msg, uint32_t ms) {
    strncpy(s_status, msg, sizeof(s_status) - 1);
    s_status[sizeof(s_status) - 1] = 0;
    s_statusUntil = millis() + ms;
    g_needRedraw = true;
}
void uiStatus(const char* msg) { uiStatusFor(msg, 1500); }

const char* uiStatusText() { return (s_statusUntil && millis() < s_statusUntil) ? s_status : ""; }

void uiTick() {
    if (s_statusUntil && millis() >= s_statusUntil) { s_statusUntil = 0; g_needRedraw = true; }
}

bool uiBrowsingLoops() { return s_listIsLoops; }

static void scanDir(const char* path, const char* ext, const char* EXT);
void uiScanSampleDir() {
    s_listIsLoops = trackIsDeck(g_curTrack);
    scanDir(s_listIsLoops ? DIR_LOOPS : DIR_SAMPLES, ".wav", ".WAV");
}
// the SAMPLE page rescans when it is entered, so the list can be borrowed
void uiScanSongDir() { scanDir(DIR_SONGS, ".arr", ".ARR"); }

static void scanDir(const char* path, const char* ext, const char* EXT) {
    g_fileCount = 0;
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) return;
    File f = dir.openNextFile();
    while (f && g_fileCount < BROWSER_MAX) {
        String nm = f.name();
        int slash = nm.lastIndexOf('/');            // some core versions return full path
        if (slash >= 0) nm = nm.substring(slash + 1);
        // a name that does not fit could not be opened again, so it is not listed
        if (!f.isDirectory() && !nm.startsWith(".") && (nm.endsWith(ext) || nm.endsWith(EXT)) &&
            strlen(nm.c_str()) < LOOP_NAME_LEN) {
            strncpy(g_fileList[g_fileCount], nm.c_str(), LOOP_NAME_LEN - 1);
            g_fileList[g_fileCount][LOOP_NAME_LEN - 1] = 0;
            g_fileCount++;
        }
        f.close();
        f = dir.openNextFile();
    }
    dir.close();
    if (g_fileSel >= g_fileCount) g_fileSel = 0;
}

// print at most n characters (long file names must not run into the next column)
static void printClipped(const char* s, int n) {
    char b[44];
    if (n > (int)sizeof(b) - 1) n = sizeof(b) - 1;
    strncpy(b, s, n); b[n] = 0;
    if ((int)strlen(s) > n && n > 1) b[n - 1] = '~';
    canvas.print(b);
}

// ---------- deck / FX rows (shared with the lid) ----------
const char* uiDeckRowName(uint8_t row) {
    static const char* names[DR_COUNT] = {
        "MODE", "SLICES", "SLICE POS", "PITCH", "LEVEL", "FIT",
        "IN", "OUT", "START", "QUALITY", "BPM<LOOP",
        "BPM", "SWING", "KEY", "BASSMODE", "STYLE", "LENGTH",
        "BLEND", "CUTOFF", "RESO", "TYPE", "SLOPE",
        "PUMP", "RELEASE", "BASS PUMP", "GHOST", "LID",
    };
    return row < DR_COUNT ? names[row] : "";
}

void uiDeckRowValue(uint8_t di, uint8_t row, char* o, size_t n) {
    const Deck& k = g_decks[di];
    bool has = deckLoaded(di);
    float len = has ? (float)g_samples[k.slot].length : 1.0f;
    static const char* sl[] = {"AUTO","4","8","16"};
    static const char* ty[] = {"LOW PASS","HIGH PASS","BAND PASS","PHASER"};
    static const char* ql[] = {"AUTO","16 BIT","LO-FI"};
    switch (row) {
        case DR_MODE:     snprintf(o, n, "%s", k.mode == DECK_LOOP ? "LOOP" : "CHOP"); break;
        case DR_SLICES:   snprintf(o, n, "%s (%u)", sl[k.sliceMode], k.nSlices); break;
        case DR_PITCH:    snprintf(o, n, "%+.1f st", k.pitch); break;
        case DR_LEVEL:    snprintf(o, n, "%d%%", (int)(k.level * 100 + 0.5f)); break;
        case DR_FIT:      if (k.fitBars) snprintf(o, n, "%u BAR", k.fitBars); else snprintf(o, n, "OFF"); break;
        case DR_BLEND:    snprintf(o, n, g_fx.blend < 64 ? "A %d" : g_fx.blend > 64 ? "B %d" : "A+B", abs((int)g_fx.blend - 64)); break;
        case DR_CUTOFF:   snprintf(o, n, "%d%% %dHz", g_fx.cutoff * 100 / 255,
                                   (int)(g_fx.type == FLT_PHASER ? phaserHz(g_fx.cutoff) : cutoffToHz(g_fx.cutoff))); break;
        case DR_RESO:     snprintf(o, n, "%d%%", g_fx.reso * 100 / 127); break;
        case DR_TYPE:     snprintf(o, n, "%s", ty[g_fx.type % FLT_COUNT]); break;
        case DR_POLES:    if (g_fx.type == FLT_PHASER) snprintf(o, n, "%d STAGE", PHASER_STAGES(g_fx.poles));
                          else snprintf(o, n, "%s", g_fx.poles ? "24 dB" : "12 dB"); break;
        case DR_PUMP:     snprintf(o, n, "%u%%", g_fx.pump); break;
        case DR_RELEASE:  snprintf(o, n, "%u ms", g_fx.release); break;
        case DR_BASSPUMP: snprintf(o, n, "%u%%", g_fx.bassPump); break;
        case DR_GHOST:    snprintf(o, n, "%s", g_fx.ghost ? "ON" : "OFF"); break;
        case DR_BPM:      snprintf(o, n, "%u", g_bpm); break;
        case DR_LID:      snprintf(o, n, "%s", g_lidOk ? "ON" : "OFF"); break;
        case DR_SWING:    snprintf(o, n, "%u%%", g_fx.swing); break;
        case DR_KEY:      snprintf(o, n, "%s", g_keyNames[g_fx.key % 12]); break;
        case DR_BASSMODE: snprintf(o, n, "%s", g_bassModeNames[g_fx.bassMode % BASS_COUNT]); break;
        case DR_STYLE:    snprintf(o, n, "%s", g_styleNames[g_fx.style % STYLE_COUNT]); break;
        case DR_PATLEN:   { int bars = patLen(g_patterns[g_curPattern]) / STEPS_PER_BAR;
                            snprintf(o, n, "%d BAR%s", bars, bars > 1 ? "S" : ""); break; }
        case DR_SLICEPOS: if (has) snprintf(o, n, "#%u %.3fs", k.lastSlice + 1, (float)k.slice[k.lastSlice] / SAMPLE_RATE);
                          else snprintf(o, n, "-"); break;
        case DR_IN:       snprintf(o, n, "%d%%", has ? (int)(k.in * 100.0f / len) : 0); break;
        case DR_OUT:      snprintf(o, n, "%d%%", has ? (int)(k.out * 100.0f / len + 0.5f) : 0); break;
        case DR_START:    snprintf(o, n, "%.1f s", k.startTenths * 0.1f); break;
        case DR_QUAL:     snprintf(o, n, "%s", ql[k.qual % QUAL_COUNT]); break;
        case DR_SETBPM:   if (has) snprintf(o, n, "> %d", (int)(deckBpmForBars(di, k.fitBars ? k.fitBars : deckGuessBars(di, g_bpm, true)) + 0.5f));
                          else snprintf(o, n, "-"); break;
        default: o[0] = 0;
    }
}

// synth and drum rows as text, for the lid's big readout
void uiSoundRowText(uint8_t row, char* name, size_t nn, char* val, size_t vn) {
    name[0] = val[0] = 0;
    if (g_curTrack == TRK_DRUMS) {
        static const char* names[] = {"LANE","ENGINE","TYPE","VOLUME","TUNE","DECAY","CHOKE"};
        if (row >= 7) return;
        const DrumLane& d = g_drumLanes[g_curDrumLane];
        snprintf(name, nn, "%s", names[row]);
        switch (row) {
            case 0: snprintf(val, vn, "%d", g_curDrumLane + 1); break;
            case 1: snprintf(val, vn, "%s", engNames[d.engine]); break;
            case 2: if (d.engine == ENG_SMPL) snprintf(val, vn, "%.17s", samplerValid(d.sampleSlot) ? g_samples[d.sampleSlot].name : "(none)");
                    else snprintf(val, vn, "%s", d.engine == ENG_909 ? type909[d.type % DT_COUNT] : type808[d.type % DT_COUNT_808]);
                    break;
            case 3: snprintf(val, vn, "%d%%", (int)(d.volume * 100 + 0.5f)); break;
            case 4: snprintf(val, vn, "%+.1f st", d.tune); break;
            case 5: snprintf(val, vn, "%.2fx", d.decay); break;
            case 6: if (d.chokeGroup) snprintf(val, vn, "GRP %u", d.chokeGroup); else snprintf(val, vn, "OFF"); break;
        }
    } else if (g_curTrack < NUM_SYNTHS) {
        static const char* names[] = {"OSC","WTABLE","CUTOFF","RESO","ENV AMT","FLT DEC","AMP DEC","VOLUME","VOICES"};
        if (row >= 9) return;
        const SynthTrack& t = g_synths[g_curTrack];
        const SynthVoice& v = t.v[0];
        snprintf(name, nn, "%s", names[row]);
        switch (row) {
            case 0: snprintf(val, vn, "%s", oscNames[v.oscMode]); break;
            case 1: snprintf(val, vn, "%s", g_wtNames[v.wtIndex]); break;
            case 2: snprintf(val, vn, "%d%%", (int)(v.fltCutoff * 100 + 0.5f)); break;
            case 3: snprintf(val, vn, "%d%%", (int)(v.fltReso * 100 + 0.5f)); break;
            case 4: snprintf(val, vn, "%d%%", (int)(v.fltEnvAmt * 100 + 0.5f)); break;
            case 5: snprintf(val, vn, "%d%%", (int)((v.filtDecRate - 0.995f) / 0.00495f * 100 + 0.5f)); break;
            case 6: snprintf(val, vn, "%d%%", (int)((v.ampDecRate - 0.999f) / 0.00099f * 100 + 0.5f)); break;
            case 7: snprintf(val, vn, "%d%%", (int)(v.volume * 100 + 0.5f)); break;
            case 8: snprintf(val, vn, "%u %s", t.voices, t.voices > 1 ? "POLY" : "MONO"); break;
        }
    }
}

// ---------- SETTINGS rows: the track's own, then the shared block ----------
static uint8_t localRows() {
    if (trackIsDeck(g_curTrack)) return DR_SHARED_FIRST;
    return (g_curTrack == TRK_DRUMS) ? DRUM_PARAMS : SYNTH_PARAMS;
}
uint8_t uiRowCount() { return (uint8_t)(localRows() + (DR_COUNT - DR_SHARED_FIRST)); }

uint8_t uiRowDeckRow(uint8_t row) {
    if (trackIsDeck(g_curTrack)) return row < DR_COUNT ? row : DR_COUNT;
    uint8_t loc = localRows();
    if (row < loc) return DR_COUNT;
    row = (uint8_t)(DR_SHARED_FIRST + row - loc);
    return row < DR_COUNT ? row : DR_COUNT;
}

void uiRowText(uint8_t row, char* name, size_t nn, char* val, size_t vn) {
    uint8_t dr = uiRowDeckRow(row);
    if (dr < DR_COUNT) {
        snprintf(name, nn, "%s", uiDeckRowName(dr));
        uiDeckRowValue(trackIsDeck(g_curTrack) ? trackDeck(g_curTrack) : 0, dr, val, vn);
    } else uiSoundRowText(row, name, nn, val, vn);
}

void uiTrackTitle(char* o, size_t n) {
    if (trackIsDeck(g_curTrack))       snprintf(o, n, "DECK %c", 'A' + trackDeck(g_curTrack));
    else if (g_curTrack == TRK_DRUMS)  snprintf(o, n, "DRUM LANE %u", g_curDrumLane + 1);
    else                               snprintf(o, n, "SYNTH %u", g_curTrack + 1);
}

// ---------- header / footer ----------
static void drawHeader() {
    canvas.fillRect(0, 0, SCREEN_W, 11, COL_GRID);
    canvas.setTextColor(COL_TEXT);
    canvas.setCursor(2, 2);
    static const char* pageNames[] = {"PATTERN","SETTINGS","SAMPLE","SONG"};
    canvas.print(pageNames[g_curPage]);

    canvas.setCursor(54, 2);
    canvas.printf("%3u", g_bpm);

    canvas.setCursor(78, 2);
    uint8_t len = patLen(g_patterns[g_curPattern]);
    if (g_playing && !g_songMode && g_nextPattern != g_curPattern)   // a switch is queued
                          canvas.printf("PT%u>%u", g_curPattern + 1, g_nextPattern + 1);
    else if (len > STEPS_PER_BAR) canvas.printf("PT%u.%u", g_curPattern + 1, g_curStep / STEPS_PER_BAR + 1);
    else                  canvas.printf("PT%u", g_curPattern + 1);
    if (g_songMode) { canvas.setTextColor(COL_ACCENT); canvas.setCursor(112, 2); canvas.print("SNG"); }

    canvas.setTextColor(COL_DIM);
    canvas.setCursor(134, 2);
    canvas.printf("P%u O%u", g_curProject + 1, g_curOctave);

    // the filter is always in sight: where the cutoff really is right now
    canvas.drawRect(166, 2, 34, 7, COL_DIM);
    int w = (int)(g_cutNow * 32.0f / 255.0f);
    canvas.fillRect(167, 3, constrain(w, 0, 32), 5, COL_AMBER);

    // transport
    if (g_playing) { canvas.setTextColor(COL_SYNTH2); canvas.setCursor(210, 2); canvas.print(">"); }
    if (g_recEnabled) canvas.fillCircle(228, 5, 3, COL_REC);
}

static void drawFooter(const char* hint) {
    // long-press progress / mic level meter takes over the footer
    if (g_holdProg > 0.001f) {
        int w = (int)((SCREEN_W - 4) * (g_holdProg > 1.0f ? 1.0f : g_holdProg));
        canvas.drawRect(1, SCREEN_H - 10, SCREEN_W - 2, 9, COL_GRID);
        canvas.fillRect(2, SCREEN_H - 9, w, 7, COL_ACCENT);
        if (g_holdLabel[0]) {
            canvas.setTextColor(COL_TEXT);
            canvas.setCursor(4, SCREEN_H - 9);
            canvas.print(g_holdLabel);
        }
        return;
    }
    canvas.setTextColor(COL_DIM);
    canvas.setCursor(2, SCREEN_H - 9);
    if (uiStatusText()[0]) {
        canvas.setTextColor(COL_ACCENT);
        canvas.print(s_status);
    } else {
        canvas.print(hint);
    }
}

// "A:CH8 B:L2 C30 P70 M68%"  decks, cutoff, pump, arena use
static const char* statusLine() {
    static char b[44];
    char dk[2][8];
    for (int d = 0; d < NUM_DECKS; d++) {
        const Deck& k = g_decks[d];
        if (!deckLoaded(d))            snprintf(dk[d], 8, "--");
        else if (k.mode == DECK_CHOP)  snprintf(dk[d], 8, "CH%u", k.nSlices);
        else if (k.fitBars)            snprintf(dk[d], 8, "L%ubar", k.fitBars);
        else                           snprintf(dk[d], 8, "FIT?");
    }
    snprintf(b, sizeof(b), "A:%s B:%s C%d P%u M%d%%", dk[0], dk[1],
             g_fx.cutoff * 100 / 255, g_fx.pump,
             g_arenaCap ? (int)(g_arenaUsed * 100ull / g_arenaCap) : 0);
    return b;
}

// ---------- PATTERN page, no lid: the grid ----------
static void drawPatternPage() {
    const int gx = 14, gw = 14;          // grid origin x, cell width
    const Pattern& p = g_patterns[g_curPattern];
    uint8_t len  = patLen(p);
    uint8_t base = (uint8_t)((g_curStep / STEPS_PER_BAR) * STEPS_PER_BAR);   // bar on screen
    bool here = g_playing && g_playPattern == g_curPattern;
    const uint16_t mu = seqEditMask();
    int y = 13;

    // 3 synth rows (11 px pitch)
    for (int t = 0; t < NUM_SYNTHS; t++) {
        canvas.setTextColor((mu & MUTE_SYNTH(t)) ? COL_GRID : trackCols[t]);
        canvas.setCursor(2, y + 1);
        canvas.printf("%d", t + 1);

        for (int i = 0; i < STEPS_PER_BAR; i++) {
            int st = base + i, x = gx + i * gw;
            const SynthCell c = patCell(p, (uint8_t)t, (uint8_t)st);
            uint8_t bg = COL_BG;
            if (here && st == g_lastStep) bg = COL_PLAYBG;
            if (c.accent && !c.empty()) bg = COL_ACCBG;
            canvas.fillRect(x, y, gw - 1, 10, bg);
            canvas.drawRect(x, y, gw - 1, 10, (i % 4 == 0) ? COL_DIM : COL_GRID);

            if (!c.empty()) {
                canvas.setTextColor((mu & MUTE_SYNTH(t)) ? COL_DIM : trackCols[t]);
                canvas.setCursor(x + 1, y + 1);
                canvas.print(noteNames[c.note[0]]);
                // chord: one dot per extra note, top-right of the cell
                for (int nn = 1; nn < MAX_POLY; nn++)
                    if (c.note[nn] != NOTE_EMPTY)
                        canvas.fillRect(x + gw - 3, y + 1 + (nn - 1) * 3, 2, 2,
                                        (mu & MUTE_SYNTH(t)) ? COL_DIM : trackCols[t]);
                if (c.slide) canvas.drawFastHLine(x + 1, y + 8, gw - 3, COL_ACCENT);
            }
            if (t == g_curTrack && st == g_curStep)
                canvas.drawRect(x - 1, y - 1, gw + 1, 12, COL_CURSOR);
        }
        y += 11;
    }

    // 2 deck rows: the chop track (or a running-loop bar)
    y += 1;
    for (int d = 0; d < NUM_DECKS; d++) {
        const Deck& k = g_decks[d];
        uint8_t col = trackCols[TRK_DECK_A + d];
        bool sel = (g_curTrack == TRK_DECK_A + d);
        const bool dmute = mu & MUTE_DECK(d);
        canvas.setTextColor(dmute ? COL_GRID : (sel ? COL_TEXT : col));
        canvas.setCursor(2, y);
        canvas.print(d ? "B" : "A");

        if (deckLoaded(d) && k.mode == DECK_LOOP) {
            canvas.drawRect(gx, y, STEPS_PER_BAR * gw - 1, 8, sel ? COL_DIM : COL_GRID);
            canvas.setTextColor(dmute ? COL_DIM : col);
            canvas.setCursor(gx + 3, y);
            if (k.fitBars) canvas.printf("LOOP %u BAR", k.fitBars); else canvas.print("LOOP  FIT?");
            canvas.setCursor(gx + 76, y);
            printClipped(k.name, 22);
            if (here && k.out > k.in) {                    // where the loop is
                int px = gx + 1 + (int)((uint64_t)(k.playFrame > k.in ? k.playFrame - k.in : 0)
                                        * (STEPS_PER_BAR * gw - 3) / (k.out - k.in));
                canvas.drawFastVLine(constrain(px, gx + 1, gx + STEPS_PER_BAR * gw - 3), y + 1, 6, COL_HOT);
            }
        } else {
            for (int i = 0; i < STEPS_PER_BAR; i++) {
                int st = base + i, x = gx + i * gw;
                uint8_t c = patChop(p, (uint8_t)d, (uint8_t)st);
                uint8_t bg = (here && st == g_lastStep) ? COL_PLAYBG : COL_BG;
                if (CHOP_SLICE(c) && (c & CHOP_ACC)) bg = COL_ACCBG;
                canvas.fillRect(x, y, gw - 1, 8, bg);
                if (CHOP_SLICE(c)) {
                    canvas.setTextColor(dmute ? COL_DIM : col);
                    canvas.setCursor(x + 4, y);
                    canvas.print(sliceChar[CHOP_SLICE(c) - 1]);
                } else {
                    canvas.drawPixel(x + 6, y + 4, (i % 4 == 0) ? COL_DIM : COL_GRID);
                }
            }
        }
        if (sel) canvas.drawRect(gx + (g_curStep - base) * gw - 1, y - 1, gw + 1, 10, COL_CURSOR);
        y += 9;
    }

    // cutoff lock lane: amber where a sweep is recorded, taller = more open
    for (int i = 0; i < STEPS_PER_BAR; i++) {
        int st = base + i;
        if (patLocked(p, (uint8_t)st)) {
            int h = 1 + patLock(p, (uint8_t)st) / 86;
            canvas.fillRect(gx + i * gw + 1, y + 3 - h, gw - 3, h, COL_AMBER);
        }
    }
    y += 4;

    // 8 drum lanes, lane 1 on top, lane 8 at the bottom (the mute keys' order)
    for (int row = 0; row < NUM_DRUM_LANES; row++) {
        int l = row;                                    // visual row = lane
        bool sel = (g_curTrack == TRK_DRUMS && g_curDrumLane == l);
        const bool lmute = mu & MUTE_LANE(l);
        // 7 px rows: number every other lane, and always the selected one
        if (sel || (l % 2 == 0 && !(g_curTrack == TRK_DRUMS && abs((int)g_curDrumLane - l) == 1))) {
            canvas.setTextColor(lmute ? COL_GRID : (sel ? COL_TEXT : COL_DRUMS));
            canvas.setCursor(2, y - 1);
            canvas.printf("%d", l + 1);
        }

        for (int i = 0; i < STEPS_PER_BAR; i++) {
            int st = base + i, x = gx + i * gw;
            bool hit = patDrums(p, (uint8_t)st) & (1 << l);
            bool acc = patDrumAcc(p, (uint8_t)st) & (1 << l);
            uint8_t col = hit ? (lmute ? COL_DIM : (acc ? COL_ACCENT : COL_DRUMS)) : COL_GRID;
            if (here && st == g_lastStep && hit) col = COL_TEXT;
            canvas.fillRect(x + 3, y + 1, gw - 7, 4, col);
            if (sel && st == g_curStep)
                canvas.drawRect(x + 1, y - 1, gw - 3, 8, COL_CURSOR);
        }
        y += 7;
    }
}

// ---------- pieces shared by the SETTINGS and deck views ----------
// one deck's waveform: slice marks, the last pad hit, IN/OUT, the playhead
static void drawWave(uint8_t di, int wx, int wy, int ww, int wh) {
    const Deck& k = g_decks[di];
    canvas.drawRect(wx, wy, ww, wh, COL_GRID);
    if (!deckLoaded(di)) return;
    uint32_t len = g_samples[k.slot].length;
    if (!len) return;
    const int iw = ww - 2, x0 = wx + 1, mid = wy + wh / 2, amp = wh / 2 - 2;
    uint8_t col = seqDeckMuted(di) ? COL_DIM : trackCols[TRK_DECK_A + di];
    for (int x = 0; x < iw; x++) {
        int o = x * OV_W / iw;
        int hi = mid - k.ovMax[o] * amp / 128, lo = mid - k.ovMin[o] * amp / 128;
        canvas.drawFastVLine(x0 + x, hi, lo - hi + 1, col);
    }
    for (int s = 0; s < k.nSlices; s++) {
        int x = x0 + (int)((uint64_t)k.slice[s] * (iw - 1) / len);
        bool hot = (s == k.lastSlice);
        canvas.drawFastVLine(x, wy + 1, hot ? wh - 2 : 5, hot ? COL_TEXT : COL_DIM);
    }
    canvas.drawFastVLine(x0 + (int)((uint64_t)k.in  * (iw - 1) / len), wy + wh - 8, 7, COL_AMBER);
    canvas.drawFastVLine(x0 + (int)((uint64_t)k.out * (iw - 1) / len), wy + wh - 8, 7, COL_AMBER);
    if (k.v[k.cur].active) {
        int px = x0 + (int)((uint64_t)k.playFrame * (iw - 1) / len);
        canvas.drawFastVLine(constrain(px, x0, x0 + iw - 1), wy + 1, wh - 2, COL_HOT);
    }
}

static void drawScope(int wx, int wy, int ww, int wh) {
    canvas.drawRect(wx, wy, ww, wh, COL_GRID);
    const int mid = wy + wh / 2, step = (ww > 120) ? 1 : 2;
    int prevY = mid;
    for (int i = 0; i < ww - 2 && i * step < g_scopeIdx; i++) {
        int yy = constrain(mid - (int)(g_scopeBuf[i * step] * (wh / 2 - 2)), wy + 1, wy + wh - 2);
        if (i) canvas.drawLine(wx + i, prevY, wx + 1 + i, yy, COL_SYNTH2);
        prevY = yy;
    }
    g_scopeIdx = 0;
}

// the deck filter's response: 2-pole magnitude, twice for the 24 dB slope
static void drawFilterCurve(int x0, int y0, int w, int h) {
    canvas.drawRect(x0, y0, w, h, COL_GRID);
    float fc = cutoffToHz(g_fx.cutoff);
    float q  = 0.6f * powf(20.0f, (float)g_fx.reso / 127.0f);
    int px = 0, py = 0;
    if (g_fx.type == FLT_PHASER) {                    // the notches, at the centre of the sweep
        const float pc = phaserHz(g_fx.cutoff), n = (float)PHASER_STAGES(g_fx.poles);
        for (int x = 2; x < w - 2; x++) {
            float f = 40.0f * powf(11000.0f / 40.0f, (float)(x - 2) / (float)(w - 5));
            float m = fabsf(cosf(n * atanf(f / pc)));
            float db = 20.0f * log10f(m > 0.02f ? m : 0.02f);
            int y = constrain(y0 + h * 3 / 10 - (int)(db * (float)h / 50.0f), y0 + 1, y0 + h - 2);
            if (x > 2) canvas.drawLine(px, py, x0 + x, y, COL_AMBER);
            px = x0 + x; py = y;
        }
        return;
    }
    for (int i = 0; i < 60; i++) {
        float f = 40.0f * powf(11000.0f / 40.0f, (float)i / 59.0f);
        float r = f / fc, r2 = r * r;
        auto mag = [&](float qq) {
            float den = sqrtf((1 - r2) * (1 - r2) + r2 / (qq * qq));
            float num = g_fx.type == FLT_LP ? 1.0f : g_fx.type == FLT_HP ? r2 : r / qq;
            return num / (den > 1e-6f ? den : 1e-6f);
        };
        float m = g_fx.poles ? mag(0.72f) * mag(q) : mag(q);
        float db = 20.0f * log10f(m > 1e-5f ? m : 1e-5f);
        int x = x0 + 2 + i * (w - 5) / 59;
        int y = constrain(y0 + h * 3 / 10 - (int)(db * (float)h / 70.0f), y0 + 1, y0 + h - 2);
        if (i) canvas.drawLine(px, py, x, y, COL_AMBER);
        px = x; py = y;
    }
}

// where the cutoff really is, and what the pump is doing
static void drawCutPump(int x, int y, int w, bool stacked) {
    int bw = stacked ? w - 22 : w / 2 - 28;
    canvas.setTextColor(COL_DIM);
    canvas.setCursor(x, y); canvas.print("CUT");
    canvas.drawRect(x + 22, y, bw, 7, COL_GRID);
    canvas.fillRect(x + 23, y + 1, (int)(g_cutNow * (bw - 2) / 255.0f), 5, COL_AMBER);
    int px = stacked ? x : x + w / 2 + 4, py = stacked ? y + 11 : y;
    canvas.setCursor(px, py); canvas.print("PMP");
    canvas.drawRect(px + 22, py, bw, 7, COL_GRID);
    canvas.fillRect(px + 23, py + 1, (int)(g_pumpNow * g_fx.pump * 0.01f * (bw - 2)), 5, COL_HOT);
}

// ---------- SETTINGS page, no lid: the scrolling list ----------
static void drawSettingsPage() {
    char title[24], name[24], val[28];
    bool deck = trackIsDeck(g_curTrack);
    uiTrackTitle(title, sizeof(title));
    canvas.setTextColor(trackCols[g_curTrack]);
    canvas.setCursor(2, 14);
    canvas.print(title);
    if (deck) {
        canvas.print(" ");
        if (deckLoaded(trackDeck(g_curTrack))) printClipped(g_decks[trackDeck(g_curTrack)].name, 17);
        else { canvas.setTextColor(COL_DIM); canvas.print("empty - PG: SAMPLE"); }
    }

    int rows = uiRowCount(), sel = (g_soundParam < rows) ? g_soundParam : 0;
    int first = constrain(sel - 4, 0, max(0, rows - 9));
    int y = 26;
    for (int r = first; r < first + 9 && r < rows; r++) {
        uint8_t dr = uiRowDeckRow((uint8_t)r);
        bool shared = (dr >= DR_SHARED_FIRST && dr < DR_COUNT);     // tempo / pocket / filter / pump
        uiRowText((uint8_t)r, name, sizeof(name), val, sizeof(val));
        canvas.setTextColor(r == sel ? COL_TEXT : (shared ? COL_AMBER : COL_DIM));
        canvas.setCursor(2, y);
        canvas.printf("%s%-9s", r == sel ? ">" : " ", name);
        canvas.setTextColor(r == sel ? COL_TEXT : COL_DIM);
        printClipped(val, 13);
        y += 11;
    }

    const int wx = 150, wy = 24, ww = 88, wh = 44;
    if (deck) {
        uint8_t di = trackDeck(g_curTrack);
        drawWave(di, wx, wy, ww, wh);
        if (deckLoaded(di)) {
            const SampleInfo& si = g_samples[g_decks[di].slot];
            canvas.setTextColor(COL_DIM);
            canvas.setCursor(wx, wy + wh + 3);
            canvas.printf("%.2fs %s", (float)si.length / SAMPLE_RATE, si.fmt == FMT_ULAW ? "LO-FI" : "16BIT");
        }
    } else drawScope(wx, wy, ww, wh);
    drawCutPump(wx, 84, ww, true);
    canvas.setTextColor(COL_DIM);
    canvas.setCursor(wx, 106); canvas.printf("free %uK", (unsigned)(samplerFreeBytes() / 1024));
}

// ---------- SETTINGS page, lid open: the focused row, large ----------
// The list itself is on the lid. This is what the row is doing.
static void drawFocusPage() {
    char title[24], name[24], val[28];
    uint8_t rows = uiRowCount(), row = (g_soundParam < rows) ? g_soundParam : 0;
    uiTrackTitle(title, sizeof(title));
    uiRowText(row, name, sizeof(name), val, sizeof(val));
    canvas.setTextColor(trackCols[g_curTrack]);
    canvas.setCursor(2, 14); canvas.print(title);
    canvas.setTextColor(COL_DIM);
    canvas.setCursor(204, 14); canvas.printf("%2u/%u", row + 1, rows);
    canvas.setTextSize(2);
    canvas.setTextColor(COL_TEXT);  canvas.setCursor(2, 25); canvas.print(name);
    canvas.setTextColor(COL_AMBER); canvas.setCursor(2, 43); printClipped(val, 19);
    canvas.setTextSize(1);

    uint8_t dr = uiRowDeckRow(row);
    const int by = 62, bh = 48;
    if (dr >= DR_BLEND && dr <= DR_POLES)   drawFilterCurve(0, by, SCREEN_W, bh);
    else if (trackIsDeck(g_curTrack))       drawWave(trackDeck(g_curTrack), 0, by, SCREEN_W, bh);
    else                                    drawScope(0, by, SCREEN_W, bh);
    drawCutPump(2, 114, SCREEN_W - 4, false);
}

// ---------- PATTERN page, lid open: both decks, full width ----------
// The grid is on the lid. Pads you play light up here.
static void drawDecksPage() {
    for (int d = 0; d < NUM_DECKS; d++) {
        const Deck& k = g_decks[d];
        int y0 = 13 + d * 56;
        drawWave((uint8_t)d, 0, y0, SCREEN_W, 45);
        char b[56];
        if (!deckLoaded(d))            snprintf(b, sizeof(b), "DECK %c empty - PG: SAMPLE loads one", 'A' + d);
        else if (k.mode == DECK_CHOP)  snprintf(b, sizeof(b), "%c CHOP %u  %.40s", 'A' + d, k.nSlices, k.name);
        else if (k.fitBars)            snprintf(b, sizeof(b), "%c LOOP %u bar  %.36s", 'A' + d, k.fitBars, k.name);
        else                           snprintf(b, sizeof(b), "%c LOOP FIT?  %.38s", 'A' + d, k.name);
        bool sel = (g_curTrack == TRK_DECK_A + d);
        canvas.setTextColor(seqDeckMuted(d) ? COL_GRID : (sel ? COL_TEXT : trackCols[TRK_DECK_A + d]));
        canvas.setCursor(2, y0 + 47);
        printClipped(b, 39);
    }
}

// ---------- SAMPLE page ----------
static void drawSamplePage() {
    canvas.setTextColor(COL_TEXT);
    canvas.setCursor(2, 14);
    if (s_listIsLoops) {
        canvas.setTextColor(trackCols[g_curTrack]);
        canvas.printf("LOOPS (%u) > DECK %c", g_fileCount, 'A' + trackDeck(g_curTrack));
    } else {
        canvas.printf("SD SAMPLES (%u)", g_fileCount);
    }

    // arena: deck A | deck B | one-shots, in their colours
    const int mx = 150, mw = 88;
    canvas.drawRect(mx, 13, mw, 8, COL_GRID);
    if (g_arenaCap) {
        int x = mx + 1;
        const uint8_t cols[3] = { COL_DECKA, COL_DECKB, COL_DRUMS };
        const uint8_t own[3]  = { OWN_DECK_A, OWN_DECK_B, OWN_ONESHOT };
        for (int i = 0; i < 3; i++) {
            int w = (int)((uint64_t)samplerOwnerBytes(own[i]) * (mw - 2) / g_arenaCap);
            canvas.fillRect(x, 14, w, 6, cols[i]);
            x += w;
        }
    }

    int y = 25;
    if (g_fileCount == 0) {
        canvas.setTextColor(COL_DIM);
        canvas.setCursor(2, 40);
        canvas.print(g_sdOk ? "Put .wav files in" : "NO SD CARD");
        canvas.setCursor(2, 52);
        canvas.print(s_listIsLoops ? DIR_LOOPS : DIR_SAMPLES);
        canvas.setCursor(2, 70);
        canvas.print(s_listIsLoops ? "or hold AUX to mic-sample the deck" : "or hold AUX to mic-sample");
    } else {
        // scrolling list, 7 rows
        int first = (g_fileSel > 3) ? g_fileSel - 3 : 0;
        if (first + 7 > g_fileCount) first = max(0, (int)g_fileCount - 7);
        for (int i = first; i < first + 7 && i < g_fileCount; i++) {
            bool sel = (i == g_fileSel);
            canvas.setTextColor(sel ? COL_TEXT : COL_DIM);
            canvas.setCursor(2, y);
            canvas.print(sel ? ">" : " ");
            printClipped(g_fileList[i], 31);
            if (s_listIsLoops) {
                for (int d = 0; d < NUM_DECKS; d++)
                    if (deckLoaded(d) && strncmp(g_decks[d].name, g_fileList[i], LOOP_NAME_LEN) == 0) {
                        canvas.setTextColor(trackCols[TRK_DECK_A + d]);
                        canvas.setCursor(216 + d * 12, y); canvas.print(d ? "B" : "A");
                    }
            } else if (samplerFindByName(g_fileList[i]) >= 0) {
                canvas.setTextColor(COL_SYNTH2); canvas.setCursor(216, y); canvas.print("RAM");
            }
            y += 12;
        }
    }
    // memory in numbers, always
    canvas.setTextColor(COL_DIM);
    canvas.setCursor(2, 113);
    canvas.printf("A %uK  B %uK  OS %uK  free %uK",
                  (unsigned)(samplerOwnerBytes(OWN_DECK_A) / 1024), (unsigned)(samplerOwnerBytes(OWN_DECK_B) / 1024),
                  (unsigned)(samplerOwnerBytes(OWN_ONESHOT) / 1024), (unsigned)(samplerFreeBytes() / 1024));
}

// ---------- SONG page ----------
static void drawSongPage() {
    canvas.setTextColor(COL_TEXT);
    canvas.setCursor(2, 14);
    canvas.print(g_songMode ? "SONG" : "SONG(PT)");
    canvas.setTextColor(COL_DIM);
    canvas.setCursor(98, 14);
    canvas.printf("PROJECT P%u %s", g_curProject + 1,
                  storageProjectExists(g_curProject) ? "*" : "");
    logoDraw(canvas, SCREEN_W - logoWidth(8) - 2, 13, 8, COL_ACCENT);

    if (g_arrList) {                              // arrangements on the card
        canvas.setCursor(2, 28); canvas.setTextColor(COL_AMBER); canvas.print("LOAD ARRANGEMENT");
        if (!g_fileCount) {
            canvas.setTextColor(COL_DIM);
            canvas.setCursor(2, 44); canvas.print("no .arr files in");
            canvas.setCursor(2, 56); canvas.print(DIR_SONGS);
        }
        int first = (g_fileSel > 3) ? g_fileSel - 3 : 0;
        for (int i = 0; i < 7 && first + i < g_fileCount; i++) {
            bool on = (first + i == g_fileSel);
            canvas.setTextColor(on ? COL_TEXT : COL_DIM);
            canvas.setCursor(2, 40 + i * 10);
            canvas.print(on ? ">" : " ");
            printClipped(g_fileList[first + i], 38);
        }
        return;
    }

    const int gx = 8, gy = 25, cw = 14, ch = 21;
    for (int i = 0; i < SONG_LENGTH; i++) {
        int x = gx + (i % 16) * cw;
        int y = gy + (i / 16) * ch;
        const SongSlot& sl = g_song[i];
        bool isCursor = (i == g_songCursor);
        bool isPlay   = (g_songMode && g_playing && i == g_songPos);

        canvas.drawRect(x, y, cw - 1, ch - 2, isPlay ? COL_TEXT : COL_GRID);
        if (i == g_songLoopStart)
            canvas.drawFastVLine(x, y, ch - 2, COL_ACCENT);

        if (sl.pattern != SONG_EMPTY) {
            // pattern number, then how many times it plays (1-9, A-G = 10-16) in grey
            canvas.setTextColor(isPlay ? COL_TEXT : COL_SYNTH1);
            canvas.setCursor(x + 1, y + 2);
            canvas.printf("%u", sl.pattern + 1);
            const bool two = sl.pattern >= 9;             // two digits leave no room beside them
            if (sl.repeats > 1) {
                canvas.setTextColor(COL_DIM);
                canvas.setCursor(two ? x + 1 : x + 7, two ? y + 10 : y + 2);
                canvas.print(sliceChar[(sl.repeats - 1) & 15]);
            }
            if (i == g_songTag) canvas.fillRect(x + cw - 5, y + 1, 3, 3, COL_ACCENT);
            // filter target: amber column, taller = more open
            if (sl.target != TARGET_NONE) {
                int h = 1 + sl.target * 5 / 254;
                canvas.fillRect(x + 1, y + ch - 3 - h, cw - 3, h, COL_AMBER);
            }
            if (sl.mute) canvas.drawFastHLine(x + 1, (sl.pattern >= 9 && sl.repeats > 1) ? y + 9 : y + 11, cw - 3, COL_REC);
        }
        if (isCursor) canvas.drawRect(x - 1, y - 1, cw + 1, ch, COL_CURSOR);
    }

    // the slot under the cursor, in words
    const SongSlot& sl = g_song[g_songCursor];
    canvas.setCursor(2, 112);
    if (sl.pattern == SONG_EMPTY) {
        canvas.setTextColor(COL_DIM);
        canvas.printf("slot %u empty", g_songCursor + 1);
    } else {
        canvas.setTextColor(COL_TEXT);
        canvas.printf("%u:P%u x%u ", g_songCursor + 1, sl.pattern + 1, sl.repeats);
        canvas.setTextColor(COL_AMBER);
        if (sl.target == TARGET_NONE) canvas.print("cut -- "); else canvas.printf("cut %d%% ", sl.target * 100 / 254);
        // muted rows in grid order: synths 1-3, decks A B, drum lanes 1-8
        canvas.setTextColor(COL_REC);
        if (sl.mute) canvas.print("mute ");
        for (int b = 0; b < NUM_SYNTHS + NUM_DECKS + NUM_DRUM_LANES; b++)
            if (sl.mute & (1u << b)) canvas.print("123AB12345678"[b]);
    }
}

// ---------- draw dispatch ----------
void uiDraw() {
    canvas.fillSprite(COL_BG);
    drawHeader();
    switch (g_curPage) {
        case PAGE_PATTERN:
            if (g_lidOk) drawDecksPage(); else drawPatternPage();
            drawFooter(statusLine()); break;
        case PAGE_SETTINGS:
            if (g_lidOk) drawFocusPage(); else drawSettingsPage();
            drawFooter("; . row   , / change   z = fine"); break;
        case PAGE_SAMPLE:  drawSamplePage();
            drawFooter(s_listIsLoops ? "b:load here   hold 3/4: deck A/B"
                                     : "b:preview   fn Aa a s d f g h: lane"); break;
        default:           drawSongPage();
            drawFooter(g_arrList ? "; . pick   hold \\:load   m:back"
                                 : "[ ]:pattern e/u:cut tab/p:x m:arr"); break;
    }
    canvas.pushSprite(0, 0);
}

void uiDrawNow() {
    uiDraw();
    lidUpdate(true);
    g_needRedraw = false;
}

// letters set wide apart, the way a front panel is engraved
static void printSpaced(int x, int y, const char* t, int pitch) {
    for (; *t; t++, x += pitch) { canvas.setCursor(x, y); canvas.print(*t); }
}

void uiSplash() {
    canvas.fillSprite(COL_BG);
    const int sc = 36;                                   // 2.25 px per logo unit: 45 px tall
    const int lx = (SCREEN_W - logoWidth(sc)) / 2;
    logoDraw(canvas, lx, 14, sc, COL_ACCENT);
    static const uint8_t stripes[4] = { COL_ACCENT, COL_HOT, COL_REC, COL_ACCBG };
    logoStripes(canvas, lx, 66, logoWidth(sc), 4, stripes);

    canvas.setTextSize(1);
    canvas.setTextColor(COL_TEXT);
    printSpaced(lx + 1, 88, "FRENCH TOUCH SAMPLER", 9);
    canvas.setTextColor(COL_DIM);
    canvas.setCursor(lx + 1, 101);
    canvas.printf("%u PATTERNS / %u BARS / %uK SAMPLES", NUM_PATTERNS, MAX_BARS, (unsigned)(g_arenaCap / 1024));
    canvas.setCursor(lx + 1, 111);
    canvas.printf("built on Microgroove    heap %uK", (unsigned)(ESP.getFreeHeap() / 1024));
    canvas.setTextColor(COL_ACCENT);
    printSpaced(lx + 1, 124, "PRESS ANY KEY", 9);
    canvas.pushSprite(0, 0);
}
