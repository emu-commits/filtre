// ============================================================
// FILTRE - ui.cpp  (from Microgroove)
// 240x135, sprite double-buffered in 16 colours. 5 pages.
// Drum grid drawn TR-style: kick (lane 1) at the bottom.
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

Page    g_curPage    = PAGE_PATTERN;
bool    g_needRedraw = true;
uint8_t g_soundParam = 0;
uint8_t g_songCursor = 0;
uint8_t g_helpScroll = 0;
float   g_holdProg   = 0.0f;
char    g_holdLabel[16] = "";

char    g_fileList[BROWSER_MAX][SAMPLE_NAME_LEN];
uint8_t g_fileCount = 0;
uint8_t g_fileSel   = 0;

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

void uiStatus(const char* msg) {
    strncpy(s_status, msg, sizeof(s_status) - 1);
    s_status[sizeof(s_status) - 1] = 0;
    s_statusUntil = millis() + 1500;
    g_needRedraw = true;
}

bool uiBrowsingLoops() { return s_listIsLoops; }

void uiScanSampleDir() {
    g_fileCount = 0;
    s_listIsLoops = trackIsDeck(g_curTrack);
    File dir = SD.open(s_listIsLoops ? DIR_LOOPS : DIR_SAMPLES);
    if (!dir || !dir.isDirectory()) return;
    File f = dir.openNextFile();
    while (f && g_fileCount < BROWSER_MAX) {
        String nm = f.name();
        int slash = nm.lastIndexOf('/');            // some core versions return full path
        if (slash >= 0) nm = nm.substring(slash + 1);
        if (!f.isDirectory() && !nm.startsWith(".") && (nm.endsWith(".wav") || nm.endsWith(".WAV"))) {
            strncpy(g_fileList[g_fileCount], nm.c_str(), SAMPLE_NAME_LEN - 1);
            g_fileList[g_fileCount][SAMPLE_NAME_LEN - 1] = 0;
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
        "MODE", "SLICES", "PITCH", "LEVEL", "FIT",
        "BLEND", "CUTOFF", "RESO", "TYPE", "SLOPE",
        "PUMP", "RELEASE", "BASS PUMP", "GHOST",
        "SWING", "KEY", "BASSMODE", "STYLE", "LENGTH",
        "SLICE POS", "IN", "OUT", "START", "QUALITY", "BPM<LOOP",
    };
    return row < DR_COUNT ? names[row] : "";
}

void uiDeckRowValue(uint8_t di, uint8_t row, char* o, size_t n) {
    const Deck& k = g_decks[di];
    bool has = deckLoaded(di);
    float len = has ? (float)g_samples[k.slot].length : 1.0f;
    static const char* sl[] = {"AUTO","4","8","16"};
    static const char* ty[] = {"LOW PASS","HIGH PASS","BAND PASS"};
    static const char* ql[] = {"AUTO","16 BIT","LO-FI"};
    switch (row) {
        case DR_MODE:     snprintf(o, n, "%s", k.mode == DECK_LOOP ? "LOOP" : "CHOP"); break;
        case DR_SLICES:   snprintf(o, n, "%s (%u)", sl[k.sliceMode], k.nSlices); break;
        case DR_PITCH:    snprintf(o, n, "%+.1f st", k.pitch); break;
        case DR_LEVEL:    snprintf(o, n, "%d%%", (int)(k.level * 100 + 0.5f)); break;
        case DR_FIT:      if (k.fitBars) snprintf(o, n, "%u BAR", k.fitBars); else snprintf(o, n, "OFF"); break;
        case DR_BLEND:    snprintf(o, n, g_fx.blend < 64 ? "A %d" : g_fx.blend > 64 ? "B %d" : "A+B", abs((int)g_fx.blend - 64)); break;
        case DR_CUTOFF:   snprintf(o, n, "%d%% %dHz", g_fx.cutoff * 100 / 255, (int)cutoffToHz(g_fx.cutoff)); break;
        case DR_RESO:     snprintf(o, n, "%d%%", g_fx.reso * 100 / 127); break;
        case DR_TYPE:     snprintf(o, n, "%s", ty[g_fx.type % FLT_COUNT]); break;
        case DR_POLES:    snprintf(o, n, "%s", g_fx.poles ? "24 dB" : "12 dB"); break;
        case DR_PUMP:     snprintf(o, n, "%u%%", g_fx.pump); break;
        case DR_RELEASE:  snprintf(o, n, "%u ms", g_fx.release); break;
        case DR_BASSPUMP: snprintf(o, n, "%u%%", g_fx.bassPump); break;
        case DR_GHOST:    snprintf(o, n, "%s", g_fx.ghost ? "ON" : "OFF"); break;
        case DR_SWING:    snprintf(o, n, "%u%%", g_fx.swing); break;
        case DR_KEY:      snprintf(o, n, "%s", g_keyNames[g_fx.key % 12]); break;
        case DR_BASSMODE: snprintf(o, n, "%s", g_bassModeNames[g_fx.bassMode % BASS_COUNT]); break;
        case DR_STYLE:    snprintf(o, n, "%s", g_styleNames[g_fx.style % STYLE_COUNT]); break;
        case DR_PATLEN:   snprintf(o, n, "%s", patLen(g_patterns[g_curPattern]) == NUM_STEPS ? "2 BARS" : "1 BAR"); break;
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

// ---------- header / footer ----------
static void drawHeader() {
    canvas.fillRect(0, 0, SCREEN_W, 11, COL_GRID);
    canvas.setTextColor(COL_TEXT);
    canvas.setCursor(2, 2);
    static const char* pageNames[] = {"PATTERN","SOUND","SAMPLE","SONG","HELP"};
    canvas.print(pageNames[g_curPage]);

    canvas.setCursor(54, 2);
    canvas.printf("%3u", g_bpm);

    canvas.setCursor(78, 2);
    uint8_t len = patLen(g_patterns[g_curPattern]);
    if (len == NUM_STEPS) canvas.printf("PT%u.%u", g_curPattern + 1, g_curStep / STEPS_PER_BAR + 1);
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
    if (millis() < s_statusUntil) {
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

// ---------- PATTERN page ----------
static void drawPatternPage() {
    const int gx = 14, gw = 14;          // grid origin x, cell width
    const Pattern& p = g_patterns[g_curPattern];
    uint8_t len  = patLen(p);
    uint8_t base = (uint8_t)((g_curStep / STEPS_PER_BAR) * STEPS_PER_BAR);   // bar on screen
    bool here = g_playing && g_playPattern == g_curPattern;
    int y = 13;

    // 3 synth rows (11 px pitch)
    for (int t = 0; t < NUM_SYNTHS; t++) {
        canvas.setTextColor(g_synthMute[t] ? COL_GRID : trackCols[t]);
        canvas.setCursor(2, y + 1);
        canvas.printf("%d", t + 1);

        for (int i = 0; i < STEPS_PER_BAR; i++) {
            int st = base + i, x = gx + i * gw;
            const SynthCell& c = p.synth[t][st];
            uint8_t bg = COL_BG;
            if (here && st == g_lastStep) bg = COL_PLAYBG;
            if (c.accent && !c.empty()) bg = COL_ACCBG;
            canvas.fillRect(x, y, gw - 1, 10, bg);
            canvas.drawRect(x, y, gw - 1, 10, (i % 4 == 0) ? COL_DIM : COL_GRID);

            if (!c.empty()) {
                canvas.setTextColor(g_synthMute[t] ? COL_DIM : trackCols[t]);
                canvas.setCursor(x + 1, y + 1);
                canvas.print(noteNames[c.note[0]]);
                // chord: one dot per extra note, top-right of the cell
                for (int nn = 1; nn < MAX_POLY; nn++)
                    if (c.note[nn] != NOTE_EMPTY)
                        canvas.fillRect(x + gw - 3, y + 1 + (nn - 1) * 3, 2, 2,
                                        g_synthMute[t] ? COL_DIM : trackCols[t]);
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
        canvas.setTextColor(k.mute ? COL_GRID : (sel ? COL_TEXT : col));
        canvas.setCursor(2, y);
        canvas.print(d ? "B" : "A");

        if (deckLoaded(d) && k.mode == DECK_LOOP) {
            canvas.drawRect(gx, y, STEPS_PER_BAR * gw - 1, 8, sel ? COL_DIM : COL_GRID);
            canvas.setTextColor(k.mute ? COL_DIM : col);
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
                uint8_t c = p.chop[d][st];
                uint8_t bg = (here && st == g_lastStep) ? COL_PLAYBG : COL_BG;
                if (CHOP_SLICE(c) && (c & CHOP_ACC)) bg = COL_ACCBG;
                canvas.fillRect(x, y, gw - 1, 8, bg);
                if (CHOP_SLICE(c)) {
                    canvas.setTextColor(k.mute ? COL_DIM : col);
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
        if (p.lockSet & (1u << st)) {
            int h = 1 + p.lock[st] / 86;
            canvas.fillRect(gx + i * gw + 1, y + 3 - h, gw - 3, h, COL_AMBER);
        }
    }
    y += 4;

    // 8 drum lanes, TR-style: lane 8 on top, kick (lane 1) at the bottom
    for (int row = 0; row < NUM_DRUM_LANES; row++) {
        int l = NUM_DRUM_LANES - 1 - row;             // visual row -> lane
        bool sel = (g_curTrack == TRK_DRUMS && g_curDrumLane == l);
        canvas.setTextColor(g_drumMute ? COL_GRID : (sel ? COL_TEXT : COL_DRUMS));
        canvas.setCursor(2, y - 1);
        canvas.printf("%d", l + 1);

        for (int i = 0; i < STEPS_PER_BAR; i++) {
            int st = base + i, x = gx + i * gw;
            bool hit = p.drums[st] & (1 << l);
            bool acc = p.drumAcc[st] & (1 << l);
            uint8_t col = hit ? (g_drumMute ? COL_DIM : (acc ? COL_ACCENT : COL_DRUMS)) : COL_GRID;
            if (here && st == g_lastStep && hit) col = COL_TEXT;
            canvas.fillRect(x + 3, y + 1, gw - 7, 4, col);
            if (sel && st == g_curStep)
                canvas.drawRect(x + 1, y - 1, gw - 3, 8, COL_CURSOR);
        }
        y += 7;
    }
}

// ---------- SOUND page ----------
static void drawBar(int x, int y, float v01) {
    canvas.drawRect(x, y, 62, 7, COL_GRID);
    canvas.fillRect(x + 1, y + 1, (int)(60 * constrain(v01, 0.0f, 1.0f)), 5, COL_SYNTH1);
}

static void drawDeckSound(uint8_t di) {
    const Deck& k = g_decks[di];
    uint8_t col = trackCols[TRK_DECK_A + di];
    canvas.setTextColor(col);
    canvas.setCursor(2, 14);
    canvas.printf("DECK %c ", 'A' + di);
    if (deckLoaded(di)) printClipped(k.name, 17);
    else { canvas.setTextColor(COL_DIM); canvas.print("empty - PG: SAMPLE"); }

    // rows (scrolling window of 9)
    int first = (int)g_soundParam - 4;
    if (first > DR_COUNT - 9) first = DR_COUNT - 9;
    if (first < 0) first = 0;
    int y = 26;
    char val[24];
    for (int r = first; r < first + 9 && r < DR_COUNT; r++) {
        bool sel = (g_soundParam == r);
        bool shared = (r >= DR_BLEND && r <= DR_PATLEN);     // filter / pump / pocket: not per deck
        canvas.setTextColor(sel ? COL_TEXT : (shared ? COL_AMBER : COL_DIM));
        canvas.setCursor(2, y);
        canvas.printf("%s%-9s", sel ? ">" : " ", uiDeckRowName(r));
        uiDeckRowValue(di, r, val, sizeof(val));
        canvas.setTextColor(sel ? COL_TEXT : COL_DIM);
        printClipped(val, 13);
        y += 11;
    }

    // right: waveform with slice marks and the playhead
    const int wx = 150, wy = 24, ww = 88, wh = 44;
    canvas.drawRect(wx, wy, ww, wh, COL_GRID);
    if (deckLoaded(di)) {
        uint32_t len = g_samples[k.slot].length;
        for (int x = 0; x < ww - 2; x++) {
            int o = x * OV_W / (ww - 2);
            int lo = k.ovMin[o], hi = k.ovMax[o];
            int y0 = wy + wh / 2 - hi * (wh / 2 - 2) / 128, y1 = wy + wh / 2 - lo * (wh / 2 - 2) / 128;
            canvas.drawFastVLine(wx + 1 + x, y0, y1 - y0 + 1, col);
        }
        for (int s = 0; s < k.nSlices; s++) {
            int x = wx + 1 + (int)((uint64_t)k.slice[s] * (ww - 2) / len);
            canvas.drawFastVLine(x, wy + 1, (s == k.lastSlice) ? wh - 2 : 5, (s == k.lastSlice) ? COL_TEXT : COL_DIM);
        }
        int px = wx + 1 + (int)((uint64_t)k.playFrame * (ww - 2) / len);
        if (k.v[k.cur].active) canvas.drawFastVLine(constrain(px, wx + 1, wx + ww - 2), wy + 1, wh - 2, COL_HOT);
        canvas.setTextColor(COL_DIM);
        canvas.setCursor(wx, wy + wh + 3);
        canvas.printf("%.2fs %s", (float)len / SAMPLE_RATE, g_samples[k.slot].fmt == FMT_ULAW ? "LO-FI" : "16BIT");
    }
    // real cutoff + pump activity
    canvas.setTextColor(COL_DIM);
    canvas.setCursor(wx, 84);  canvas.print("CUT");
    canvas.drawRect(wx + 22, 84, 66, 7, COL_GRID);
    canvas.fillRect(wx + 23, 85, (int)(g_cutNow * 64.0f / 255.0f), 5, COL_AMBER);
    canvas.setCursor(wx, 95);  canvas.print("PMP");
    canvas.drawRect(wx + 22, 95, 66, 7, COL_GRID);
    canvas.fillRect(wx + 23, 96, (int)(g_pumpNow * g_fx.pump * 0.64f), 5, COL_HOT);
    canvas.setCursor(wx, 106); canvas.printf("free %uK", (unsigned)(samplerFreeBytes() / 1024));
}

static void drawSoundPage() {
    if (trackIsDeck(g_curTrack)) { drawDeckSound(trackDeck(g_curTrack)); return; }

    bool onDrums = (g_curTrack == TRK_DRUMS);
    canvas.setTextColor(trackCols[g_curTrack]);
    canvas.setCursor(2, 14);
    if (onDrums) {
        DrumLane& d = g_drumLanes[g_curDrumLane];
        canvas.printf("DRUM LANE %d  [%s]", g_curDrumLane + 1, engNames[d.engine]);
    } else {
        canvas.printf("SYNTH %d", g_curTrack + 1);
    }

    // scope
    canvas.drawRect(150, 24, 88, 44, COL_GRID);
    int prevY = 46;
    for (int i = 0; i < 86 && i * 2 < g_scopeIdx; i++) {
        int yy = 46 - (int)(g_scopeBuf[i * 2] * 20.0f);
        yy = constrain(yy, 25, 66);
        canvas.drawLine(151 + i - 1, prevY, 151 + i, yy, COL_SYNTH2);
        prevY = yy;
    }
    g_scopeIdx = 0;

    int y = 26;
    char val[24];

    if (!onDrums) {
        SynthTrack& trk = g_synths[g_curTrack];
        SynthVoice& v = trk.v[0];
        const char* names[] = {"OSC","WTABLE","CUTOFF","RESO","ENV AMT","FLT DEC","AMP DEC","VOLUME","VOICES"};
        for (int r = 0; r < 9; r++) {
            bool sel = (g_soundParam == r);
            canvas.setTextColor(sel ? COL_TEXT : COL_DIM);
            canvas.setCursor(2, y);
            canvas.printf("%s%-8s", sel ? ">" : " ", names[r]);
            switch (r) {
                case 0: canvas.print(oscNames[v.oscMode]); break;
                case 1: canvas.print(g_wtNames[v.wtIndex]);
                        if (v.oscMode != OSC_WT) { canvas.setTextColor(COL_GRID); canvas.print(" (off)"); }
                        break;
                case 2: drawBar(64, y, v.fltCutoff);  break;
                case 3: drawBar(64, y, v.fltReso);    break;
                case 4: drawBar(64, y, v.fltEnvAmt);  break;
                case 5: drawBar(64, y, (v.filtDecRate - 0.995f) / 0.00495f); break;
                case 6: drawBar(64, y, (v.ampDecRate  - 0.999f) / 0.00099f); break;
                case 7: drawBar(64, y, v.volume);     break;
                case 8: canvas.printf("%u %s", trk.voices,
                                      trk.voices > 1 ? "POLY" : "MONO(303)");
                        break;
            }
            y += 11;    // 9 rows must clear the footer bar
        }
    } else {
        DrumLane& d = g_drumLanes[g_curDrumLane];
        const char* names[] = {"LANE","ENGINE","TYPE","VOLUME","TUNE","DECAY","CHOKE"};
        for (int r = 0; r < 7; r++) {
            bool sel = (g_soundParam == r);
            canvas.setTextColor(sel ? COL_TEXT : COL_DIM);
            canvas.setCursor(2, y);
            canvas.printf("%s%-8s", sel ? ">" : " ", names[r]);
            switch (r) {
                case 0: canvas.printf("%d%s", g_curDrumLane + 1, g_curDrumLane == 0 ? " (pumps)" : ""); break;
                case 1: canvas.print(engNames[d.engine]); break;
                case 2:
                    if (d.engine == ENG_SMPL) {
                        if (samplerValid(d.sampleSlot)) printClipped(g_samples[d.sampleSlot].name, 14);
                        else { canvas.setTextColor(COL_GRID); canvas.print("(no sample)"); }
                    } else {
                        canvas.print(d.engine == ENG_909 ? type909[d.type % DT_COUNT] : type808[d.type % DT_COUNT_808]);
                    }
                    break;
                case 3: drawBar(64, y, d.volume); break;
                case 4: snprintf(val, sizeof(val), "%+.1f st", d.tune); canvas.print(val); break;
                case 5: snprintf(val, sizeof(val), "%.2fx", d.decay);   canvas.print(val); break;
                case 6: if (d.chokeGroup) canvas.printf("GRP %u", d.chokeGroup);
                        else canvas.print("OFF");
                        break;
            }
            y += 12;
        }
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
                    if (deckLoaded(d) && strncmp(g_decks[d].name, g_fileList[i], SAMPLE_NAME_LEN) == 0) {
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
    canvas.printf("SONG %s", g_songMode ? "[ON]" : "[off]");
    canvas.setTextColor(COL_DIM);
    canvas.setCursor(120, 14);
    canvas.printf("PROJECT P%u %s", g_curProject + 1,
                  storageProjectExists(g_curProject) ? "*" : "");

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
            canvas.setTextColor(isPlay ? COL_TEXT : COL_SYNTH1);
            canvas.setCursor(x + 4, y + 2);
            canvas.printf("%u", sl.pattern + 1);
            // repeats: one tick each (a full row = 12 or more)
            int reps = sl.repeats > 12 ? 12 : sl.repeats;
            if (sl.repeats > 1) canvas.drawFastHLine(x + 1, y + 11, reps - 1 ? reps : 1, COL_DIM);
            if (sl.repeats > 1) for (int r = 0; r < reps; r += 2) canvas.drawPixel(x + 1 + r, y + 11, COL_TEXT);
            // filter target: amber column, taller = more open
            if (sl.target != TARGET_NONE) {
                int h = 1 + sl.target * 5 / 254;
                canvas.fillRect(x + 1, y + ch - 3 - h, cw - 3, h, COL_AMBER);
            }
            if (sl.mute) canvas.drawFastHLine(x + 1, y + 13, cw - 3, COL_REC);
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
        static const char* mn[SLOT_MUTE_BITS] = {"1","2","3","D","A","B","K"};
        canvas.setTextColor(COL_REC);
        if (sl.mute) canvas.print("mute ");
        for (int b = 0; b < SLOT_MUTE_BITS; b++) if (sl.mute & (1 << b)) canvas.print(mn[b]);
    }
}

// ---------- HELP page ----------
static void drawHelpPage() {
    const int steps = HELP_STEP_LINES, keys = HELP_KEY_LINES;
    int y = 14;
    for (int i = g_helpScroll; i < g_helpScroll + 10 && i < steps + keys + 4; i++) {
        canvas.setCursor(2, y);
        if (i < steps) {
            canvas.setTextColor(g_helpSteps[i][0] != ' ' ? COL_TEXT : COL_DIM);
            canvas.print(g_helpSteps[i]);
        } else if (i < steps + keys) {
            canvas.setTextColor(COL_DIM);
            canvas.print(g_keyHelp[i - steps]);
        } else {
            canvas.setTextColor(COL_AMBER);
            switch (i - steps - keys) {
                case 0: canvas.printf("STYLES 4:%s 5:%s", g_styleNames[0], g_styleNames[1]); break;
                case 1: canvas.printf("       6:%s 7:%s", g_styleNames[2], g_styleNames[3]); break;
                case 2: canvas.printf("heap %uK  arena %uK  SD %s",
                                      (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(g_arenaCap / 1024),
                                      g_sdOk ? "OK" : "--"); break;
                case 3: canvas.printf("LID %s (AUX toggles)%s", g_lidOk ? "OK" : "OFF",
                                      ESP.getFreeHeap() < 24 * 1024 ? " LOW RAM" : ""); break;
            }
        }
        y += 11;
    }
}

// ---------- draw dispatch ----------
void uiDraw() {
    canvas.fillSprite(COL_BG);
    drawHeader();
    switch (g_curPage) {
        case PAGE_PATTERN: drawPatternPage();
            drawFooter(statusLine()); break;
        case PAGE_SOUND:   drawSoundPage();
            drawFooter(trackIsDeck(g_curTrack) ? "v c row  x b adjust  keys=chops"
                                               : "v c row  x b adjust  m=fine"); break;
        case PAGE_SAMPLE:  drawSamplePage();
            drawFooter(s_listIsLoops ? ".:load  4:deckA 5:deckB  3:drums"
                                     : ".:preview 4..-:lane  3:decks"); break;
        case PAGE_SONG:    drawSongPage();
            drawFooter("4..-:set e/u:cut tab/p:x z:clr"); break;
        default:           drawHelpPage();
            drawFooter("FILTRE  v c scroll  4-7 style"); break;
    }
    canvas.pushSprite(0, 0);
}

void uiDrawNow() {
    uiDraw();
    lidUpdate(true);
    g_needRedraw = false;
}

void uiSplash() {
    canvas.fillSprite(COL_BG);
    canvas.setTextSize(3);
    canvas.setTextColor(COL_AMBER);  canvas.setCursor(15, 12); canvas.print("FILTRE");
    canvas.setTextSize(1);
    canvas.setTextColor(COL_DIM);
    canvas.setCursor(15, 44);  canvas.print("French-touch sampler");
    canvas.setCursor(15, 60);  canvas.print("2 decks: chop or loop / filter");
    canvas.setCursor(15, 72);  canvas.print("pump / 909 / 303 / song bounce");
    canvas.setCursor(15, 88);
    canvas.printf("heap %uK arena %uK lid %s", (unsigned)(ESP.getFreeHeap() / 1024),
                  (unsigned)(g_arenaCap / 1024), g_lidOk ? "OK" : "OFF");
    canvas.setCursor(15, 100); canvas.print("built on Microgroove (lebiro.studio)");
    canvas.setTextColor(COL_ACCENT);
    canvas.setCursor(15, 118); canvas.print("Press any key...");
    canvas.pushSprite(0, 0);
}
