// ============================================================
// FILTRE - lid_ui.cpp
// Pins (TERM512 / AREA512 CapTFT, shared with the LoRa cap header):
//   SCLK 40  MOSI 14  MISO 39 (SD only)  DC 6  CS 5  RST 3  no backlight pin
// No framebuffer: static parts are painted once per page, moving parts
// (playheads, filter bar, step row) are patched in place.
// ============================================================
#include "lid_ui.h"

bool g_lidOk = false;

#ifdef FILTRE_NO_LID
void lidInit() {}
void lidBootScreen() {}
void lidUpdate(bool) {}
void lidToggle() {}
#else

#include <M5Cardputer.h>
#ifdef ARDUINO
#include <lgfx/v1/panel/Panel_ST7789.hpp>
#endif
#include "ui.h"
#include "keymap.h"
#include "sequencer.h"
#include "sampler.h"
#include "styles.h"
#include "storage.h"

#ifndef FILTRE_LID_ROTATION
#define FILTRE_LID_ROTATION 1          // image upside down? use 3
#endif
#ifndef FILTRE_LID_INVERT
#define FILTRE_LID_INVERT 0            // colours negative? use 1
#endif
#ifndef FILTRE_LID_HZ
#define FILTRE_LID_HZ 20000000         // lid wires are long; try 40 MHz once the bars are clean
#endif

#define LW 320
#define LH 240

#ifdef ARDUINO
class LidPanel : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI      _bus;
public:
    LidPanel() {
        auto b = _bus.config();
        b.spi_host   = SPI2_HOST;      // the host the SD card is on
        b.spi_mode   = 0;
        b.freq_write = FILTRE_LID_HZ;
        b.freq_read  = 8000000;
        b.spi_3wire  = false;          // MISO belongs to the SD card
        b.use_lock   = true;
        b.pin_sclk = 40; b.pin_mosi = 14; b.pin_miso = 39; b.pin_dc = 6;
        _bus.config(b);
        _panel.setBus(&_bus);

        auto p = _panel.config();
        p.pin_cs = 5; p.pin_rst = 3; p.pin_busy = -1;
        p.memory_width = 240; p.memory_height = 320;
        p.panel_width  = 240; p.panel_height  = 320;
        p.offset_x = 0; p.offset_y = 0; p.offset_rotation = 0;
        p.readable = false;
        p.invert = FILTRE_LID_INVERT;
        p.rgb_order = false;
        p.bus_shared = true;           // release the bus between draws: SD lives here too
        _panel.config(p);
        setPanel(&_panel);
    }
};

#else
// host preview (tools/host): the lid is a plain 320x240 sprite
class LidPanel : public lgfx::LGFX_Sprite {
public:
    bool init() { setColorDepth(16); return createSprite(LW, LH) != nullptr; }
    void setRotation(int) {}
};
#endif

static LidPanel lid;
#ifndef ARDUINO
lgfx::LGFX_Sprite* lidHostSprite() { return &lid; }
#endif
static bool     s_inited = false;

// palette
static uint16_t C_BG, C_WAVE, C_WAVEB, C_HEAD, C_FILT, C_TICK, C_TEXT, C_DIM, C_BOX, C_HOT;

void lidInit() {
#ifdef FILTRE_LID_FORCE_FAIL
    g_lidOk = false; return;
#endif
#ifdef ARDUINO
    // The lid's DC line is an input on the panel. If something on the header
    // DRIVES GPIO 6 (the LoRa cap's BUSY output lives there), this is not a
    // TERM512: stay off the pins rather than fight it.
    pinMode(6, INPUT_PULLUP);   delay(2); bool low  = digitalRead(6) == LOW;
    pinMode(6, INPUT_PULLDOWN); delay(2); bool high = digitalRead(6) == HIGH;
    pinMode(6, INPUT);
    if (low || high) { g_lidOk = false; return; }
#endif
    pinMode(SD_SPI_CS_PIN, OUTPUT); digitalWrite(SD_SPI_CS_PIN, HIGH);   // card quiet during panel init
    g_lidOk = lid.init();
    if (!g_lidOk) return;
    s_inited = true;
    lid.setRotation(FILTRE_LID_ROTATION);
    C_BG   = lid.color565(0x10, 0x0c, 0x0a);  C_WAVE = lid.color565(0xf2, 0xe6, 0xd0);
    C_WAVEB= lid.color565(0x30, 0xc8, 0xb0);  C_HEAD = lid.color565(0xff, 0x4d, 0x2e);
    C_FILT = lid.color565(0xff, 0xb0, 0x00);  C_TICK = lid.color565(0x7a, 0x6a, 0x58);
    C_TEXT = 0xFFFF;                          C_DIM  = lid.color565(0x90, 0x88, 0x80);
    C_BOX  = lid.color565(0x30, 0x28, 0x22);  C_HOT  = lid.color565(0xff, 0x20, 0x60);
}

void lidBootScreen() {
    if (!g_lidOk) return;
    lid.startWrite();
    lid.fillRect(0, 0, 107, LH, 0xF800);       // red | green | blue, full height
    lid.fillRect(107, 0, 106, LH, 0x07E0);
    lid.fillRect(213, 0, 107, LH, 0x001F);
    lid.fillRect(40, 80, 240, 80, 0x0000);
    lid.setTextColor(0xFFFF, 0x0000);
    lid.setTextSize(4); lid.setCursor(88, 90);  lid.print("FILTRE");
    lid.setTextSize(1); lid.setCursor(64, 136);
    lid.printf("heap %u  arena %u  rot %d", (unsigned)ESP.getFreeHeap(), (unsigned)g_arenaCap, FILTRE_LID_ROTATION);
    lid.setCursor(64, 148); lid.print("bars should read RED GREEN BLUE");
    lid.endWrite();
}

void lidToggle() {
    if (!s_inited) return;                     // never came up: stay off
    g_lidOk = !g_lidOk;
    if (!g_lidOk) lid.fillScreen(0);
}

// ---------- change detection ----------
static uint32_t fnv(uint32_t h, const void* d, size_t n) {
    const uint8_t* p = (const uint8_t*)d;
    while (n--) { h ^= *p++; h *= 16777619u; }
    return h;
}
#define HASH(h, v) h = fnv(h, &(v), sizeof(v))

static int      s_page = -1;
static uint32_t s_staticHash = 0, s_titleHash = 0;
static uint32_t s_lastMs = 0;
static int      s_headX[NUM_DECKS] = { -1, -1 };
static int      s_cutW = -1, s_resW = -1;
static int      s_stepShown = -1;

// ---------- shared pieces ----------
static void textAt(int x, int y, uint8_t size, uint16_t col, const char* s) {
    lid.setTextSize(size); lid.setTextColor(col, C_BG); lid.setCursor(x, y); lid.print(s);
}

static void memoryLine(int y) {
    char b[56];
    snprintf(b, sizeof(b), "A %uK  B %uK  OS %uK  free %uK  POOL %d%%   ",
             (unsigned)(samplerOwnerBytes(OWN_DECK_A) / 1024), (unsigned)(samplerOwnerBytes(OWN_DECK_B) / 1024),
             (unsigned)(samplerOwnerBytes(OWN_ONESHOT) / 1024), (unsigned)(samplerFreeBytes() / 1024),
             g_arenaCap ? (int)(g_arenaUsed * 100ull / g_arenaCap) : 0);
    textAt(2, y, 1, C_DIM, b);
}

static void filterBars(int y, bool force) {
    int cw = (int)(g_cutNow * 196.0f / 255.0f), rw = g_fx.reso * 48 / 127;
    if (force) {
        textAt(2, y + 2, 1, C_DIM, "CUTOFF");
        lid.drawRect(44, y, 200, 12, C_BOX);
        textAt(250, y + 2, 1, C_DIM, "Q");
        lid.drawRect(262, y, 52, 12, C_BOX);
        s_cutW = s_resW = -1;
    }
    if (cw != s_cutW) {
        lid.fillRect(46, y + 2, cw, 8, C_FILT);
        lid.fillRect(46 + cw, y + 2, 196 - cw, 8, C_BG);
        s_cutW = cw;
    }
    if (rw != s_resW) {
        lid.fillRect(264, y + 2, rw, 8, C_FILT);
        lid.fillRect(264 + rw, y + 2, 48 - rw, 8, C_BG);
        s_resW = rw;
    }
}

// ---------- PATTERN: the loop page ----------
#define WAVE_Y(d)  (22 + (d) * 78)
#define WAVE_H     62          // the label strip sits under it

static void waveColumn(uint8_t d, int x) {
    const Deck& k = g_decks[d];
    int y0 = WAVE_Y(d), mid = y0 + WAVE_H / 2;
    lid.drawFastVLine(x, y0, WAVE_H, C_BG);
    if (!deckLoaded(d)) return;
    int hi = mid - k.ovMax[x] * (WAVE_H / 2 - 2) / 128, lo = mid - k.ovMin[x] * (WAVE_H / 2 - 2) / 128;
    lid.drawFastVLine(x, hi, lo - hi + 1, d ? C_WAVEB : C_WAVE);
    uint32_t len = g_samples[k.slot].length;
    for (int s = 0; s < k.nSlices; s++)            // slice ticks along the top
        if ((int)((uint64_t)k.slice[s] * LW / len) == x)
            lid.drawFastVLine(x, y0, (s == k.lastSlice) ? WAVE_H : 9, (s == k.lastSlice) ? C_TEXT : C_TICK);
    if ((int)((uint64_t)k.in * LW / len) == x || (int)((uint64_t)k.out * LW / len) == x)
        lid.drawFastVLine(x, y0 + WAVE_H - 10, 10, C_FILT);     // IN / OUT brackets
}

static void stepCell(int i, bool playing) {
    const Pattern& p = g_patterns[g_playing ? g_playPattern : g_curPattern];
    uint8_t len = patLen(p);
    int st = (g_lastStep / STEPS_PER_BAR) * STEPS_PER_BAR + i;
    if (!g_playing) st = (g_curStep / STEPS_PER_BAR) * STEPS_PER_BAR + i;
    if (st >= len) st = i;
    int x = 2 + i * 20, y = 198;
    lid.fillRect(x, y, 18, 18, playing ? C_BOX : C_BG);
    lid.drawRect(x, y, 18, 18, (i % 4 == 0) ? C_TICK : C_BOX);
    uint8_t m = p.drums[st];
    if (m & 1)          lid.fillRect(x + 3, y + 11, 12, 4, C_HOT);               // kick
    if (m & 2)          lid.fillRect(x + 3, y + 7, 12, 3, C_TEXT);               // clap
    if (m & 0x0C)       lid.fillRect(x + 3, y + 3, 5, 3, C_DIM);                 // hats
    if (CHOP_SLICE(p.chop[0][st])) lid.fillRect(x + 10, y + 3, 3, 3, C_WAVE);
    if (CHOP_SLICE(p.chop[1][st])) lid.fillRect(x + 13, y + 3, 3, 3, C_WAVEB);
    if (p.lockSet & (1u << st))    lid.drawFastHLine(x + 1, y + 16, 16, C_FILT);
}

static void patternStatic() {
    lid.fillScreen(C_BG);
    for (uint8_t d = 0; d < NUM_DECKS; d++) {
        const Deck& k = g_decks[d];
        for (int x = 0; x < LW; x++) waveColumn(d, x);
        char b[56];
        if (!deckLoaded(d)) snprintf(b, sizeof(b), "DECK %c  empty", 'A' + d);
        else if (k.mode == DECK_CHOP) snprintf(b, sizeof(b), "%c CHOP %u  %.30s", 'A' + d, k.nSlices, k.name);
        else if (k.fitBars) snprintf(b, sizeof(b), "%c LOOP %u bar  %.26s", 'A' + d, k.fitBars, k.name);
        else snprintf(b, sizeof(b), "%c LOOP FIT?  %.28s", 'A' + d, k.name);
        lid.setTextSize(1);
        lid.setTextColor((g_curTrack == TRK_DECK_A + d) ? C_TEXT : C_DIM);
        lid.setCursor(4, WAVE_Y(d) + WAVE_H + 3); lid.print(b);
        s_headX[d] = -1;
    }
    filterBars(180, true);
    for (int i = 0; i < STEPS_PER_BAR; i++) stepCell(i, false);
    s_stepShown = -1;
    memoryLine(226);
}

static void patternTitle() {
    char b[40];
    snprintf(b, sizeof(b), "%3ubpm SW%u PUMP%u %s  ", g_bpm, g_fx.swing, g_fx.pump, g_keyNames[g_fx.key % 12]);
    textAt(2, 2, 2, C_TEXT, b);
    lid.setTextSize(1); lid.setTextColor(g_recEnabled ? C_HEAD : C_DIM, C_BG);
    lid.setCursor(292, 6); lid.print(g_playing ? (g_recEnabled ? "REC>" : " >  ") : (g_recEnabled ? "REC " : "    "));
}

static void patternDynamic() {
    for (uint8_t d = 0; d < NUM_DECKS; d++) {
        const Deck& k = g_decks[d];
        int x = -1;
        if (deckLoaded(d) && k.v[k.cur].active)
            x = (int)((uint64_t)k.playFrame * LW / g_samples[k.slot].length);
        if (x >= LW) x = LW - 1;
        if (x != s_headX[d]) {
            if (s_headX[d] >= 0) waveColumn(d, s_headX[d]);
            if (x >= 0) lid.drawFastVLine(x, WAVE_Y(d), WAVE_H, C_HEAD);
            s_headX[d] = x;
        }
    }
    filterBars(180, false);
    int st = g_playing ? (g_lastStep % STEPS_PER_BAR) : -1;
    if (st != s_stepShown) {
        if (s_stepShown >= 0) stepCell(s_stepShown, false);
        if (st >= 0) stepCell(st, true);
        s_stepShown = st;
    }
}

// ---------- SOUND: big parameter + the filter curve ----------
static void filterCurve() {
    const int x0 = 0, y0 = 104, w = LW, h = 76;
    lid.fillRect(x0, y0, w, h, C_BG);
    lid.drawRect(x0, y0, w, h, C_BOX);
    float fc = cutoffToHz(g_fx.cutoff);
    float q  = 0.6f * powf(20.0f, (float)g_fx.reso / 127.0f);
    int px = 0, py = 0;
    for (int i = 0; i < 64; i++) {
        float f = 40.0f * powf(11000.0f / 40.0f, (float)i / 63.0f);
        float r = f / fc, r2 = r * r;
        // 2-pole magnitude for the chosen type; the 24 dB slope is two stages (0.72, then q)
        auto mag = [&](float qq) {
            float den = sqrtf((1 - r2) * (1 - r2) + r2 / (qq * qq));
            float num = g_fx.type == FLT_LP ? 1.0f : g_fx.type == FLT_HP ? r2 : r / qq;
            return num / (den > 1e-6f ? den : 1e-6f);
        };
        float m = g_fx.poles ? mag(0.72f) * mag(q) : mag(q);
        float db = 20.0f * log10f(m > 1e-5f ? m : 1e-5f);
        int x = x0 + 2 + i * (w - 5) / 63;
        int y = y0 + 22 - (int)(db * 1.1f);            // 0 dB sits 22 px down, +20 dB at the top
        y = constrain(y, y0 + 1, y0 + h - 2);
        if (i) lid.drawLine(px, py, x, y, C_FILT);
        px = x; py = y;
    }
}

static void soundStatic() {
    lid.fillScreen(C_BG);
    char name[24] = "", val[28] = "", title[24];
    if (trackIsDeck(g_curTrack)) {
        uint8_t d = trackDeck(g_curTrack);
        snprintf(title, sizeof(title), "DECK %c", 'A' + d);
        snprintf(name, sizeof(name), "%s", uiDeckRowName(g_soundParam));
        uiDeckRowValue(d, g_soundParam, val, sizeof(val));
    } else {
        if (g_curTrack == TRK_DRUMS) snprintf(title, sizeof(title), "DRUM LANE %u", g_curDrumLane + 1);
        else                         snprintf(title, sizeof(title), "SYNTH %u", g_curTrack + 1);
        uiSoundRowText(g_soundParam, name, sizeof(name), val, sizeof(val));
    }
    textAt(2, 2, 2, C_DIM, title);
    textAt(2, 28, 4, C_TEXT, name);                    // parameter name, ~32 px tall
    textAt(2, 68, 3, C_FILT, val);
    filterCurve();
    filterBars(188, true);
    memoryLine(226);
}

// ---------- SAMPLE: the browser, bigger ----------
static void sampleStatic() {
    lid.fillScreen(C_BG);
    bool loops = uiBrowsingLoops();
    textAt(2, 2, 2, C_TEXT, loops ? "LOOPS" : "SAMPLES");
    int first = (g_fileSel > 4) ? g_fileSel - 4 : 0;
    for (int i = first, y = 26; i < first + 9 && i < g_fileCount; i++, y += 20) {
        char b[28]; snprintf(b, sizeof(b), "%.25s", g_fileList[i]);
        textAt(2, y, 2, i == g_fileSel ? C_TEXT : C_DIM, b);
    }
    if (!g_fileCount) textAt(2, 40, 1, C_DIM, loops ? "put .wav sections in /groovebox/loops" : "put .wav files in /groovebox/samples");
    // arena as one bar: deck A | deck B | one-shots
    lid.drawRect(0, 208, LW, 14, C_BOX);
    if (g_arenaCap) {
        int x = 2;
        const uint16_t cols[3] = { C_WAVE, C_WAVEB, C_HOT };
        const uint8_t own[3] = { OWN_DECK_A, OWN_DECK_B, OWN_ONESHOT };
        for (int i = 0; i < 3; i++) {
            int w = (int)((uint64_t)samplerOwnerBytes(own[i]) * (LW - 4) / g_arenaCap);
            lid.fillRect(x, 210, w, 10, cols[i]); x += w;
        }
    }
    memoryLine(226);
}

// ---------- SONG: the arrangement at a glance ----------
static void songStatic() {
    lid.fillScreen(C_BG);
    char b[40];
    snprintf(b, sizeof(b), "SONG %s  P%u", g_songMode ? "ON" : "off", g_curProject + 1);
    textAt(2, 2, 2, C_TEXT, b);
    for (int i = 0; i < SONG_LENGTH; i++) {
        const SongSlot& sl = g_song[i];
        int x = (i % 16) * 20, y = 24 + (i / 16) * 48;
        bool play = g_songMode && g_playing && i == g_songPos;
        lid.drawRect(x, y, 19, 46, i == g_songCursor ? C_TEXT : (play ? C_HEAD : C_BOX));
        if (i == g_songLoopStart) lid.drawFastVLine(x + 1, y + 1, 44, C_FILT);
        if (sl.pattern == SONG_EMPTY) continue;
        lid.setTextSize(2); lid.setTextColor(play ? C_HEAD : C_TEXT); lid.setCursor(x + 4, y + 3);
        lid.print(sl.pattern + 1);
        if (sl.repeats > 1) { lid.setTextSize(1); lid.setTextColor(C_DIM); lid.setCursor(x + 3, y + 20); lid.printf("x%u", sl.repeats); }
        if (sl.target != TARGET_NONE) {                 // cutoff column: shut -> open reads left to right
            int h = 1 + sl.target * 12 / 254;
            lid.fillRect(x + 3, y + 43 - h, 13, h, C_FILT);
        }
        for (int m = 0; m < SLOT_MUTE_BITS; m++)
            if (sl.mute & (1 << m)) lid.fillRect(x + 2 + m * 2, y + 29, 2, 2, C_HOT);
    }
    textAt(2, 219, 1, C_DIM, "e/u filter   tab/p repeats   hold AUX = bounce to SD");
    textAt(2, 230, 1, C_DIM, "mutes: fn sft a s d f g = T1 T2 T3 DRUMS A B KICK");
}

// ---------- HELP: the six steps, large ----------
static void helpStatic() {
    lid.fillScreen(C_BG);
    lid.setFont(&fonts::Font2); lid.setTextSize(1);
    for (int i = 0, y = 4; i < (int)HELP_STEP_LINES; i++, y += 18) {
        lid.setTextColor(g_helpSteps[i][0] != ' ' ? C_TEXT : C_DIM, C_BG);
        lid.setCursor(4, y); lid.print(g_helpSteps[i]);
    }
    lid.setFont(&fonts::Font0);
    char b[56];
    snprintf(b, sizeof(b), "lane 1 pumps the loop    heap %uK  arena %uK", (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(g_arenaCap / 1024));
    textAt(4, 228, 1, C_FILT, b);
}

// ---------- update ----------
static uint32_t staticHash() {
    uint32_t h = 2166136261u;
    HASH(h, g_curPage); HASH(h, g_curTrack);
    switch (g_curPage) {
        case PAGE_PATTERN: {
            for (int d = 0; d < NUM_DECKS; d++) {
                const Deck& k = g_decks[d];
                HASH(h, k.slot); HASH(h, k.mode); HASH(h, k.nSlices); HASH(h, k.fitBars);
                HASH(h, k.in); HASH(h, k.out); HASH(h, k.lastSlice);
                h = fnv(h, k.slice, sizeof(k.slice)); h = fnv(h, k.name, 8);
            }
            uint8_t pat = g_playing ? g_playPattern : g_curPattern, bar = (g_playing ? g_lastStep : g_curStep) / STEPS_PER_BAR;
            const Pattern& p = g_patterns[pat];
            HASH(h, pat); HASH(h, bar); HASH(h, p.lockSet);
            h = fnv(h, p.drums, NUM_STEPS); h = fnv(h, p.chop, sizeof(p.chop));
            HASH(h, g_arenaUsed);
            break;
        }
        case PAGE_SOUND:
            HASH(h, g_soundParam); HASH(h, g_curDrumLane); HASH(h, g_fx); HASH(h, g_bpm);
            if (!trackIsDeck(g_curTrack)) {      // synth / drum values are plain text: hash that
                char nm[24], vl[28]; uiSoundRowText(g_soundParam, nm, sizeof(nm), vl, sizeof(vl));
                h = fnv(h, vl, strlen(vl));
            }
            if (trackIsDeck(g_curTrack)) {
                const Deck& k = g_decks[trackDeck(g_curTrack)];
                HASH(h, k.mode); HASH(h, k.sliceMode); HASH(h, k.nSlices); HASH(h, k.fitBars); HASH(h, k.pitch);
                HASH(h, k.level); HASH(h, k.in); HASH(h, k.out); HASH(h, k.startTenths); HASH(h, k.qual);
                HASH(h, k.lastSlice); h = fnv(h, k.slice, sizeof(k.slice));
                uint8_t pl = patLen(g_patterns[g_curPattern]); HASH(h, pl);
            }
            break;
        case PAGE_SAMPLE:
            HASH(h, g_fileSel); HASH(h, g_fileCount); HASH(h, g_arenaUsed);
            { bool l = uiBrowsingLoops(); HASH(h, l); }
            break;
        case PAGE_SONG: {
            h = fnv(h, g_song, sizeof(g_song));
            HASH(h, g_songCursor); HASH(h, g_songLoopStart); HASH(h, g_songMode); HASH(h, g_curProject);
            uint8_t pos = g_playing ? g_songPos : 255; HASH(h, pos);
            break;
        }
        default: break;
    }
    return h;
}

void lidUpdate(bool force) {
    if (!g_lidOk) return;                       // shut or absent: draw nothing, spend nothing
    uint32_t now = millis();
    if (!force && now - s_lastMs < 60) return;
    s_lastMs = now;

    uint32_t h = staticHash();
    bool repaint = (h != s_staticHash) || (s_page != (int)g_curPage);
    lid.startWrite();
    if (repaint) {
        s_staticHash = h; s_page = g_curPage; s_titleHash = 0;
        switch (g_curPage) {
            case PAGE_PATTERN: patternStatic(); break;
            case PAGE_SOUND:   soundStatic();   break;
            case PAGE_SAMPLE:  sampleStatic();  break;
            case PAGE_SONG:    songStatic();    break;
            default:           helpStatic();    break;
        }
    }
    if (g_curPage == PAGE_PATTERN) {
        uint32_t t = 2166136261u;
        HASH(t, g_bpm); HASH(t, g_fx.swing); HASH(t, g_fx.pump); HASH(t, g_fx.key);
        bool pl = g_playing; HASH(t, pl); HASH(t, g_recEnabled);
        if (t != s_titleHash) { s_titleHash = t; patternTitle(); }
        patternDynamic();
    } else if (g_curPage == PAGE_SOUND) {
        filterBars(188, false);
    }
    lid.endWrite();
}
#endif
