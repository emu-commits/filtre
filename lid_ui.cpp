// ============================================================
// FILTRE - lid_ui.cpp
// Pins (TERM512 / AREA512 CapTFT, shared with the LoRa cap header):
//   SCLK 40  MOSI 14  MISO 39 (SD only)  DC 6  CS 5  RST 3  no backlight pin
// The lid is the editor: the pattern grid, the settings list, the browser,
// the song. No framebuffer, and no repaint either: every piece on a page
// remembers what it last drew and is redrawn only when that changes.
// ============================================================
#include "lid_ui.h"

bool g_lidOk = false;
bool g_keysOverlay = false;

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
static uint16_t C_BG, C_WAVE, C_WAVEB, C_HEAD, C_FILT, C_TICK, C_TEXT, C_DIM, C_BOX, C_HOT, C_ACC, C_SYN[3];

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
    C_ACC  = lid.color565(0x63, 0x30, 0x00);
    C_SYN[0] = lid.color565(0x00, 0xff, 0xff); C_SYN[1] = lid.color565(0x00, 0xff, 0x00); C_SYN[2] = lid.color565(0xff, 0xff, 0x00);
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

// ---------- change detection ----------
// A full-screen clear takes ~60 ms on this bus and the loop cannot read keys
// meanwhile. The panel is cleared once, on entering a page. After that each
// piece (grid cell, list row, song slot) is compared with what it last drew.
static uint32_t fnv(uint32_t h, const void* d, size_t n) {
    const uint8_t* p = (const uint8_t*)d;
    while (n--) { h ^= *p++; h *= 16777619u; }
    return h;
}
#define FNV0 2166136261u
#define HASH(h, v) h = fnv(h, &(v), sizeof(v))
static uint32_t hashStr(uint32_t h, const char* s) { return fnv(h, s, strlen(s)); }

#define CI_CELL(r, c) ((r) * 16 + (c))  // PATTERN: 14 rows x 16 steps; other pages use 0.. as rows / slots
#define CI_LABEL(r)   (224 + (r))
#define CI_TITLE      238
#define CI_FOOT       239
#define CACHE_N       240
static uint32_t s_cache[CACHE_N];       // 0 = never drawn
static bool stale(int i, uint32_t h) {
    h |= 1;
    if (s_cache[i] == h) return false;
    s_cache[i] = h;
    return true;
}

static int      s_page = -1;
#define PAGE_KEYS (-2)                  // s_page while the key map is up
#define PAGE_ARR  (-3)                  // s_page while SONG shows the arrangement list
static uint32_t s_keysHash = 0;
static uint32_t s_lastMs = 0;
static int      s_footMode = -1;

void lidToggle() {
    if (!s_inited) return;                     // never came up: stay off
    g_lidOk = !g_lidOk;
    s_page = -1;
    if (!g_lidOk) lid.fillScreen(0);
}

// ---------- shared pieces ----------
static void textAt(int x, int y, uint8_t size, uint16_t col, const char* s, uint16_t bg) {
    lid.setTextSize(size); lid.setTextColor(col, bg); lid.setCursor(x, y); lid.print(s);
}
static void textAt(int x, int y, uint8_t size, uint16_t col, const char* s) { textAt(x, y, size, col, s, C_BG); }

// bottom strip, every page: hold bar > message / question > memory
#define FOOT_Y 220
static void footer() {
    const char* st = uiStatusText();
    int mode = (g_holdProg > 0.001f) ? 2 : st[0] ? 1 : 0;
    char b[64];
    uint32_t h = FNV0; HASH(h, mode);
    int w = 0;
    if (mode == 2) {
        w = (int)(124.0f * (g_holdProg > 1.0f ? 1.0f : g_holdProg));
        h = hashStr(h, g_holdLabel); HASH(h, w);
    } else if (mode == 1) {
        h = hashStr(h, st);
    } else {
        snprintf(b, sizeof(b), "A %uK  B %uK  OS %uK  free %uK  POOL %d%%   ",
                 (unsigned)(samplerOwnerBytes(OWN_DECK_A) / 1024), (unsigned)(samplerOwnerBytes(OWN_DECK_B) / 1024),
                 (unsigned)(samplerOwnerBytes(OWN_ONESHOT) / 1024), (unsigned)(samplerFreeBytes() / 1024),
                 g_arenaCap ? (int)(g_arenaUsed * 100ull / g_arenaCap) : 0);
        h = hashStr(h, b);
    }
    if (!stale(CI_FOOT, h)) return;
    if (mode != s_footMode) { lid.fillRect(0, FOOT_Y, LW, LH - FOOT_Y, C_BG); s_footMode = mode; }
    if (mode == 2) {
        snprintf(b, sizeof(b), "%-15.15s", g_holdLabel);
        textAt(2, FOOT_Y + 2, 2, C_TEXT, b);
        lid.drawRect(188, FOOT_Y + 2, 128, 15, C_BOX);
        lid.fillRect(190, FOOT_Y + 4, w, 11, C_FILT);
        lid.fillRect(190 + w, FOOT_Y + 4, 124 - w, 11, C_BG);
    } else if (mode == 1) {
        snprintf(b, sizeof(b), "%-26.26s", st);
        textAt(2, FOOT_Y + 2, 2, C_FILT, b);
    } else {
        textAt(2, FOOT_Y + 8, 1, C_DIM, b);
    }
}

// ---------- PATTERN: the grid ----------
// Rows top to bottom: T1 T2 T3, deck A, deck B, the cutoff-lock lane, drum
// lanes 8..1 (kick at the bottom). 16 steps = the bar the cursor is in.
#define GX 16
#define CW 19
#define R_DECK 3
#define R_LOCK 5
#define R_DRUM 6
#define GRID_N 14
static int rowY(int r) { return r < R_DECK ? 20 + r * 16 : r < R_LOCK ? 68 + (r - R_DECK) * 14 : r == R_LOCK ? 96 : 104 + (r - R_DRUM) * 14; }
static int rowH(int r) { return r < R_DECK ? 16 : r == R_LOCK ? 8 : 14; }
static const char* const k_noteNames[] = {"--","C-","C#","D-","D#","E-","F-","F#","G-","G#","A-","A#","B-"};
static const char  k_sliceChar[] = "123456789ABCDEFG";

static void gridCell(int r, int c, const Pattern& p, uint16_t mu, int st, bool play, bool cur) {
    const int x = GX + c * CW, y = rowY(r), w = CW - 1, h = rowH(r) - 1;
    const bool beat = (c % 4 == 0);
    uint32_t hs = FNV0; HASH(hs, play); HASH(hs, cur);
    lid.setTextSize(1);

    if (r < R_DECK) {                                   // synth note
        const SynthCell& sc = p.synth[r][st];
        bool mute = mu & MUTE_SYNTH(r);
        hs = fnv(hs, &sc, sizeof(sc)); HASH(hs, mute);
        if (!stale(CI_CELL(r, c), hs)) return;
        lid.fillRect(x, y, w, h, (sc.accent && !sc.empty()) ? C_ACC : play ? C_BOX : C_BG);
        lid.drawRect(x, y, w, h, cur ? C_TEXT : beat ? C_TICK : C_BOX);
        if (sc.empty()) return;
        uint16_t col = mute ? C_DIM : C_SYN[r];
        lid.setTextColor(col); lid.setCursor(x + 2, y + 4); lid.print(k_noteNames[sc.note[0] % 13]);
        for (int n = 1; n < MAX_POLY; n++)              // chord: one dot per extra note
            if (sc.note[n] != NOTE_EMPTY) lid.fillRect(x + w - 4, y + 2 + (n - 1) * 4, 2, 3, col);
        if (sc.slide) lid.drawFastHLine(x + 2, y + h - 3, w - 4, C_FILT);

    } else if (r < R_LOCK) {                            // chop
        int d = r - R_DECK;
        uint8_t v = p.chop[d][st], mode = g_decks[d].mode;
        bool mute = mu & MUTE_DECK(d);
        HASH(hs, v); HASH(hs, mute); HASH(hs, mode);
        if (!stale(CI_CELL(r, c), hs)) return;
        lid.fillRect(x, y, w, h, (CHOP_SLICE(v) && (v & CHOP_ACC)) ? C_ACC : play ? C_BOX : C_BG);
        if (CHOP_SLICE(v)) {
            lid.setTextColor(mute ? C_DIM : d ? C_WAVEB : C_WAVE);
            lid.setCursor(x + 6, y + 3); lid.print(k_sliceChar[(CHOP_SLICE(v) - 1) & 15]);
        } else lid.fillRect(x + 8, y + 6, 2, 2, beat ? C_TICK : C_BOX);
        if (cur) lid.drawRect(x, y, w, h, C_TEXT);

    } else if (r == R_LOCK) {                           // recorded cutoff: taller = more open
        uint8_t v = (p.lockSet & (1u << st)) ? (uint8_t)(1 + p.lock[st] * 6 / 255) : 0;
        HASH(hs, v);
        if (!stale(CI_CELL(r, c), hs)) return;
        lid.fillRect(x, y, w, h, play ? C_BOX : C_BG);
        if (v) lid.fillRect(x + 1, y + h - v, w - 2, v, C_FILT);

    } else {                                            // drum lane
        int lane = GRID_N - 1 - r;
        bool hit = p.drums[st] & (1 << lane), acc = p.drumAcc[st] & (1 << lane), mute = mu & MUTE_LANE(lane);
        HASH(hs, hit); HASH(hs, acc); HASH(hs, mute);
        if (!stale(CI_CELL(r, c), hs)) return;
        lid.fillRect(x, y, w, h, play ? C_BOX : C_BG);
        if (hit) lid.fillRect(x + 3, y + 3, w - 6, h - 6, mute ? C_DIM : play ? C_TEXT : acc ? C_FILT : C_HOT);
        else     lid.fillRect(x + 8, y + 6, 2, 2, beat ? C_TICK : C_BOX);
        if (cur) lid.drawRect(x, y, w, h, C_TEXT);
    }
}

// a deck in LOOP mode has no steps to edit: the row says what is running
static void loopRow(int r, int d, bool sel, int curCol, bool mute) {
    const Deck& k = g_decks[d];
    uint32_t hs = FNV0 ^ 0xA5;
    HASH(hs, k.fitBars); HASH(hs, mute); HASH(hs, sel); HASH(hs, curCol);
    hs = fnv(hs, k.name, sizeof(k.name));
    if (!stale(CI_CELL(r, 0), hs)) return;
    for (int c = 1; c < 16; c++) s_cache[CI_CELL(r, c)] = 0;      // step cells come back when it is CHOP again
    const int y = rowY(r), w = 16 * CW - 1, h = rowH(r) - 1;
    char b[52];
    if (k.fitBars) snprintf(b, sizeof(b), "LOOP %u BAR  %.34s", k.fitBars, k.name);
    else           snprintf(b, sizeof(b), "LOOP FIT?  %.36s", k.name);
    lid.fillRect(GX, y, w, h, C_BG);
    lid.drawRect(GX, y, w, h, C_BOX);
    lid.setTextSize(1); lid.setTextColor(mute ? C_DIM : d ? C_WAVEB : C_WAVE);
    lid.setCursor(GX + 4, y + 3); lid.print(b);
    if (sel) lid.drawRect(GX + curCol * CW, y, CW - 1, h, C_TEXT);
}

static void patternDraw() {
    const Pattern& p = g_patterns[g_curPattern];
    const uint8_t len = patLen(p), last = g_lastStep;
    const int base = (g_curStep / STEPS_PER_BAR) * STEPS_PER_BAR;
    const bool playing = g_playing, here = playing && g_playPattern == g_curPattern;
    const int playCol = (here && last >= base && last < base + STEPS_PER_BAR) ? last - base : -1;
    const int curCol = g_curStep - base;
    const uint16_t mu = seqMuteMask();          // the pattern's, or in song mode the slot's

    char b[40], t[8];
    const uint8_t next = g_nextPattern;
    if (playing && !g_songMode && next != g_curPattern)      // a switch is queued for the pattern's end
                          snprintf(t, sizeof(t), "PT%u>%u", g_curPattern + 1, next + 1);
    else if (len == NUM_STEPS) snprintf(t, sizeof(t), "PT%u.%u", g_curPattern + 1, base / STEPS_PER_BAR + 1);
    else                  snprintf(t, sizeof(t), "PT%u", g_curPattern + 1);
    snprintf(b, sizeof(b), "%-6s%3ubpm %-2s %s", t, g_bpm, g_keyNames[g_fx.key % 12], g_songMode ? "SNG" : "   ");
    uint32_t h = hashStr(FNV0, b); HASH(h, playing); HASH(h, g_recEnabled);
    if (stale(CI_TITLE, h)) {
        textAt(2, 2, 2, C_TEXT, b);
        textAt(268, 2, 2, g_recEnabled ? C_HEAD : C_WAVEB,
               playing ? (g_recEnabled ? "REC>" : "  > ") : (g_recEnabled ? "REC " : "    "));
    }

    for (int r = 0; r < GRID_N; r++) {
        char lab[2] = { 0, 0 };
        uint16_t lc = C_DIM;
        bool sel = false;
        if (r < R_DECK)      { sel = (g_curTrack == r); lab[0] = (char)('1' + r);
                               lc = (mu & MUTE_SYNTH(r)) ? C_BOX : sel ? C_TEXT : C_SYN[r]; }
        else if (r < R_LOCK) { int d = r - R_DECK; sel = (g_curTrack == TRK_DECK_A + d); lab[0] = (char)('A' + d);
                               lc = (mu & MUTE_DECK(d)) ? C_BOX : sel ? C_TEXT : d ? C_WAVEB : C_WAVE; }
        else if (r == R_LOCK){ lab[0] = 'f'; lc = C_TICK; }
        else                 { int lane = GRID_N - 1 - r; sel = (g_curTrack == TRK_DRUMS && g_curDrumLane == lane);
                               lab[0] = (char)('1' + lane); lc = (mu & MUTE_LANE(lane)) ? C_BOX : sel ? C_TEXT : C_HOT; }
        h = FNV0; HASH(h, lab[0]); HASH(h, lc);
        if (stale(CI_LABEL(r), h)) textAt(4, rowY(r) + (rowH(r) - 8) / 2, 1, lc, lab);

        if (r >= R_DECK && r < R_LOCK && deckLoaded(r - R_DECK) && g_decks[r - R_DECK].mode == DECK_LOOP) {
            loopRow(r, r - R_DECK, sel, curCol, mu & MUTE_DECK(r - R_DECK));
            continue;
        }
        for (int c = 0; c < STEPS_PER_BAR; c++) {
            int st = base + c;
            if (st >= len) st = c;
            gridCell(r, c, p, mu, st, c == playCol, sel && c == curCol);
        }
    }
}

// ---------- SETTINGS: the list ----------
#define LIST_Y 24
#define LIST_PITCH 19
static void listRow(int i, const char* left, uint16_t lc, const char* right, uint16_t rc, uint16_t bg) {
    int y = LIST_Y + i * LIST_PITCH;
    lid.fillRect(0, y, LW, 1, bg);
    lid.fillRect(0, y + 17, LW, 2, bg);
    lid.fillRect(0, y + 1, 2, 16, bg);
    textAt(2, y + 1, 2, lc, left, bg);
    if (right) textAt(2 + 12 * (int)strlen(left), y + 1, 2, rc, right, bg);
    int end = 2 + 12 * (int)(strlen(left) + (right ? strlen(right) : 0));
    if (end < LW) lid.fillRect(end, y + 1, LW - end, 16, bg);
}

static void settingsDraw() {
    char name[24], val[28], t[24], l[16], v[20], b[40];
    int rows = uiRowCount(), sel = (g_soundParam < rows) ? g_soundParam : 0;
    int first = constrain(sel - 4, 0, max(0, rows - 10));
    uiTrackTitle(t, sizeof(t));
    snprintf(b, sizeof(b), "%-13.13s %3ubpm %2d/%-2d", t, g_bpm, sel + 1, rows);
    if (stale(CI_TITLE, hashStr(FNV0, b))) textAt(2, 2, 2, C_TEXT, b);

    for (int i = 0; i < 10; i++) {
        int r = first + i;
        uint32_t h = FNV0; HASH(h, r);
        if (r >= rows) { if (stale(i, h ^ 0x5A)) lid.fillRect(0, LIST_Y + i * LIST_PITCH, LW, LIST_PITCH, C_BG); continue; }
        uint8_t dr = uiRowDeckRow((uint8_t)r);
        bool shared = (dr >= DR_SHARED_FIRST && dr < DR_COUNT), on = (r == sel);
        uiRowText((uint8_t)r, name, sizeof(name), val, sizeof(val));
        h = hashStr(hashStr(h, name), val); HASH(h, on);
        if (!stale(i, h)) continue;
        snprintf(l, sizeof(l), "%c%-9.9s ", on ? '>' : ' ', name);
        snprintf(v, sizeof(v), "%-15.15s", val);
        listRow(i, l, on ? C_TEXT : shared ? C_FILT : C_DIM, v, on ? C_TEXT : C_DIM, on ? C_BOX : C_BG);
    }
}

// ---------- SAMPLE: the browser ----------
static void sampleDraw() {
    char b[40];
    bool loops = uiBrowsingLoops();
    if (loops) snprintf(b, sizeof(b), "LOOPS > DECK %c            ", 'A' + trackDeck(g_curTrack));
    else       snprintf(b, sizeof(b), "SAMPLES > DRUM LANES      ");
    if (stale(CI_TITLE, hashStr(FNV0, b))) textAt(2, 2, 2, C_TEXT, b);

    int first = (g_fileSel > 4) ? g_fileSel - 4 : 0;
    for (int i = 0; i < 9; i++) {
        int idx = first + i;
        const char* s = "";
        if (idx < g_fileCount)        s = g_fileList[idx];
        else if (!g_fileCount && !i)  s = "no .wav files in";
        else if (!g_fileCount && i == 1) s = loops ? DIR_LOOPS : DIR_SAMPLES;
        bool on = (idx < g_fileCount && idx == g_fileSel);
        uint32_t h = hashStr(FNV0, s); HASH(h, on);
        if (!stale(i, h)) continue;
        snprintf(b, sizeof(b), "%c%-25.25s", on ? '>' : ' ', s);
        listRow(i, b, on ? C_TEXT : C_DIM, nullptr, 0, on ? C_BOX : C_BG);
    }

    // arena as one bar: deck A | deck B | one-shots
    const uint8_t own[3] = { OWN_DECK_A, OWN_DECK_B, OWN_ONESHOT };
    int w[3];
    uint32_t h = FNV0;
    for (int i = 0; i < 3; i++) {
        w[i] = g_arenaCap ? (int)((uint64_t)samplerOwnerBytes(own[i]) * (LW - 4) / g_arenaCap) : 0;
        HASH(h, w[i]);
    }
    if (stale(20, h)) {
        const uint16_t cols[3] = { C_WAVE, C_WAVEB, C_HOT };
        lid.drawRect(0, 202, LW, 12, C_BOX);
        int x = 2;
        for (int i = 0; i < 3; i++) { lid.fillRect(x, 204, w[i], 8, cols[i]); x += w[i]; }
        if (x < LW - 2) lid.fillRect(x, 204, LW - 2 - x, 8, C_BG);
    }
}

// ---------- SONG: the arrangement at a glance ----------
static void songDraw() {
    char b[40], t[12];
    if (g_arrList) {                            // the arrangements on the card
        if (stale(CI_TITLE, hashStr(FNV0, "arr"))) textAt(2, 2, 2, C_TEXT, "LOAD ARRANGEMENT          ");
        int first = (g_fileSel > 4) ? g_fileSel - 4 : 0;
        for (int i = 0; i < 10; i++) {
            int idx = first + i;
            const char* s = "";
            if (idx < g_fileCount)           s = g_fileList[idx];
            else if (!g_fileCount && !i)     s = "no .arr files in";
            else if (!g_fileCount && i == 1) s = DIR_SONGS;
            bool on = (idx < g_fileCount && idx == g_fileSel);
            uint32_t h = hashStr(FNV0, s); HASH(h, on);
            if (!stale(i, h)) continue;
            snprintf(b, sizeof(b), "%c%-25.25s", on ? '>' : ' ', s);
            listRow(i, b, on ? C_TEXT : C_DIM, nullptr, 0, on ? C_BOX : C_BG);
        }
        return;
    }
    const SongSlot& cs = g_song[g_songCursor];
    if (cs.pattern == SONG_EMPTY) snprintf(t, sizeof(t), "%u:--", g_songCursor + 1);
    else                          snprintf(t, sizeof(t), "%u:P%u x%u", g_songCursor + 1, cs.pattern + 1, cs.repeats);
    snprintf(b, sizeof(b), "SONG %-3s P%u  %-14s", g_songMode ? "ON" : "off", g_curProject + 1, t);
    if (stale(CI_TITLE, hashStr(FNV0, b))) textAt(2, 2, 2, C_TEXT, b);

    const bool playing = g_songMode && g_playing;
    const uint8_t pos = g_songPos;
    for (int i = 0; i < SONG_LENGTH; i++) {
        const SongSlot& sl = g_song[i];
        bool play = playing && i == pos, cur = (i == g_songCursor), loop = (i == g_songLoopStart);
        uint32_t h = fnv(FNV0, &sl, sizeof(sl)); HASH(h, play); HASH(h, cur); HASH(h, loop);
        if (!stale(i, h)) continue;
        int x = (i % 16) * 20, y = 24 + (i / 16) * 48;
        lid.fillRect(x, y, 19, 46, C_BG);
        lid.drawRect(x, y, 19, 46, cur ? C_TEXT : (play ? C_HEAD : C_BOX));
        if (loop) lid.drawFastVLine(x + 1, y + 1, 44, C_FILT);
        if (sl.pattern == SONG_EMPTY) continue;
        lid.setTextSize(2); lid.setTextColor(play ? C_HEAD : C_TEXT); lid.setCursor(x + 4, y + 3);
        lid.print(sl.pattern + 1);
        if (sl.repeats > 1) { lid.setTextSize(1); lid.setTextColor(C_DIM); lid.setCursor(x + 3, y + 20); lid.printf("x%u", sl.repeats); }
        if (sl.target != TARGET_NONE) {                 // cutoff column: shut -> open reads left to right
            int hh = 1 + sl.target * 12 / 254;
            lid.fillRect(x + 3, y + 43 - hh, 13, hh, C_FILT);
        }
        for (int m = 0; m < NUM_SYNTHS + NUM_DECKS + NUM_DRUM_LANES; m++)     // the 13 rows, left to right
            if (sl.mute & (1u << m)) lid.fillRect(x + 3 + m, y + 28, 1, 4, C_HOT);
    }
}

// ---------- KEY MAP: the keyboard, labelled for where you are ----------
// Labels come from keymap.h's own lookups, so they cannot drift from the
// bindings. All text is const; the overlay owns no memory.
static const uint8_t k_keyRows[4][14] = {
    { '`','1','2','3','4','5','6','7','8','9','0','-','=',KC_DEL },
    { KC_TAB,'q','w','e','r','t','y','u','i','o','p','[',']','\\' },
    { KC_FN,KC_SHIFT,'a','s','d','f','g','h','j','k','l',';','\'',KC_ENTER },
    { KC_CTRL,KC_OPT,KC_ALT,'z','x','c','v','b','n','m',',','.','/',KC_SPACE },
};

static void keyLegend(uint8_t kc, char* o) {
    const char* s = nullptr;
    switch (kc) {
        case KC_DEL: s = "del"; break;  case KC_TAB: s = "tab"; break;  case KC_FN: s = "fn"; break;
        case KC_SHIFT: s = "Aa"; break; case KC_ENTER: s = "ok"; break; case KC_CTRL: s = "ctl"; break;
        case KC_OPT: s = "opt"; break;  case KC_ALT: s = "alt"; break;  case KC_SPACE: s = "spc"; break;
    }
    if (s) strcpy(o, s); else { o[0] = (char)kc; o[1] = 0; }
}

// what the key does right now (<= 3 chars) and its group colour; "" = nothing here
static uint16_t keyFunction(uint8_t kc, char* o) {
    o[0] = 0;
    bool play = (g_curPage == PAGE_PATTERN || g_curPage == PAGE_SETTINGS);
    bool song = (g_curPage == PAGE_SONG);
    if (g_curPage == PAGE_SAMPLE && padLane(kc) >= 0 && !uiBrowsingLoops()) {   // pad = that drum lane
        snprintf(o, 4, "L%d", padLane(kc) + 1);
        return C_HOT;
    }
    if (pianoSemi(kc) >= 0 && play) {                                   // same priority as input.cpp
        if (g_curTrack == TRK_DRUMS) {
            if (padLane(kc) >= 0) snprintf(o, 4, "%d", padLane(kc) + 1);
        } else if (trackIsDeck(g_curTrack)) {
            if (padSlice(kc) >= 0) snprintf(o, 4, "%d", padSlice(kc) + 1);
        } else {
            strcpy(o, g_keyNames[pianoSemi(kc) % 12]);
        }
        return C_HOT;
    }
    uint8_t a = keyAction(kc);
    if (a >= ACT_MUTE_FIRST && a <= ACT_MUTE_LAST) {                    // red = muted (pattern, or slot in song mode)
        int i = a - ACT_MUTE_FIRST;
        if (i < NUM_SYNTHS)                  snprintf(o, 4, "T%d", i + 1);
        else if (i < NUM_SYNTHS + NUM_DECKS) snprintf(o, 4, "%c", 'A' + i - NUM_SYNTHS);
        else                                 snprintf(o, 4, "D%d", i - NUM_SYNTHS - NUM_DECKS + 1);
        return (seqMuteMask() & (1u << i)) ? C_HEAD : C_TEXT;
    }
    const char* s = ""; uint16_t c = C_WAVE;
    switch (a) {
        case ACT_PAT_DN: s = "P-";  c = C_WAVEB; break;
        case ACT_PAT_UP: s = "P+";  c = C_WAVEB; break;
        case ACT_COPY:   s = "CPY"; c = C_WAVEB; break;
        case ACT_LOAD:   s = "LD";  c = C_DIM;  break;
        case ACT_ARR:    if (song) { s = "ARR"; c = C_DIM; } break;
        case ACT_SAVE:   s = "SV";  c = C_DIM;  break;
        case ACT_PAGE:   s = "PG";  c = C_DIM;  break;
        case ACT_OCT_DN: s = song ? "PJ-" : "OC-"; c = C_DIM;  break;
        case ACT_OCT_UP: s = song ? "PJ+" : "OC+"; c = C_DIM;  break;
        case ACT_CUT_DN: s = "CT-"; c = C_FILT; break;
        case ACT_CUT_UP: s = "CT+"; c = C_FILT; break;
        case ACT_RES_DN: s = song ? "RP-" : "RS-"; c = C_FILT; break;
        case ACT_RES_UP: s = song ? "RP+" : "RS+"; c = C_FILT; break;
        case ACT_CLR:    s = "CLR"; break;
        case ACT_LEFT:   s = "<";   break;
        case ACT_DOWN:   s = "v";   break;
        case ACT_UP:     s = "^";   break;
        case ACT_RIGHT:  s = ">";   break;
        case ACT_REC:    s = "REC"; break;
        case ACT_PLAY:   s = "PLY"; break;
        case ACT_ACCENT: s = "ACC"; c = C_HOT;  break;
        case ACT_SONG:   s = "SNG"; c = C_HEAD; break;
        case ACT_SLIDE:  s = "SLD"; c = C_HEAD; break;
        case ACT_AUX:    s = "AUX"; c = C_HEAD; break;
        case ACT_KEYS:   s = "MAP"; c = C_TEXT; break;
    }
    strcpy(o, s);
    return c;
}

static void keysStatic() {
    lid.fillScreen(C_BG);
    static const char* const pages[] = { "PATTERN", "SETTINGS", "SAMPLE", "SONG" };
    char b[64], trk[12];
    if (g_curTrack == TRK_DRUMS)        strcpy(trk, "DRUMS");
    else if (trackIsDeck(g_curTrack))   snprintf(trk, sizeof(trk), "DECK %c", 'A' + trackDeck(g_curTrack));
    else                                snprintf(trk, sizeof(trk), "SYNTH T%d", g_curTrack + 1);
    snprintf(b, sizeof(b), "KEY MAP  %s page  %s", pages[g_curPage % PAGE_COUNT], trk);
    textAt(4, 2, 1, C_TEXT, b);
    textAt(LW - 6 * 14 - 2, 2, 1, C_DIM, "release = back");

    lid.setTextSize(1);
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 14; c++) {
            uint8_t kc = k_keyRows[r][c];
            char leg[4], fn[8];
            keyLegend(kc, leg);
            uint16_t col = keyFunction(kc, fn);
            int x = 6 + c * 22, y = 14 + r * 27;
            if (kc == KC_ENTER) lid.fillRect(x, y, 21, 25, C_BOX);
            lid.drawRect(x, y, 21, 25, fn[0] ? col : C_BOX);
            lid.setTextColor(fn[0] ? C_DIM : C_TICK);
            lid.setCursor(x + (22 - 6 * (int)strlen(leg)) / 2, y + 3);  lid.print(leg);
            lid.setTextColor(col);
            lid.setCursor(x + (22 - 6 * (int)strlen(fn)) / 2, y + 14);  lid.print(fn);
        }

    // the legend: one line per colour, in words
    const char* ln[8]; uint16_t lc[8];
    char pads[48];
    lc[0] = C_TEXT;  ln[0] = g_songMode ? "Tap: mute in this SONG SLOT. Hold: select"
                                        : "Tap: mute in this PATTERN.  Hold: select";
    lc[1] = C_WAVEB;
    switch (g_curPage) {
        case PAGE_SONG:   ln[1] = "P- P+ the pattern in this slot"; break;
        default:          ln[1] = "P- P+ pattern (playing: at its end).  CPY"; break;
    }
    lc[2] = C_FILT;
    ln[2] = (g_curPage == PAGE_SONG) ? "CT slot filter target   RP slot repeats"
                                     : "CT cutoff   RS resonance.  Hold: sweep";
    lc[3] = C_HOT;
    if (g_curPage == PAGE_SONG)        ln[3] = "SNG on: top row mutes the slot you hear";
    else if (g_curPage == PAGE_SAMPLE) ln[3] = uiBrowsingLoops() ? "Pads, notes play on PATTERN and SETTINGS"
                                                                 : "L1-L8: put this sample on a drum lane";
    else if (g_curTrack == TRK_DRUMS)  ln[3] = "1-8 drum lanes, 1 = kick.  ACC: accent";
    else if (trackIsDeck(g_curTrack)) {
        snprintf(pads, sizeof(pads), "1-16 chops of deck %c.  ACC: accent", 'A' + trackDeck(g_curTrack));
        ln[3] = pads;
    } else                             ln[3] = "Notes.  ACC: accent.  OC: octave";
    lc[4] = C_WAVE;
    ln[4] = (g_curPage == PAGE_SETTINGS) ? "^ v pick a row, < > change it. BPM: a row"
                                         : "< v ^ > cursor.  CLR cell.  Hold: PLY top";
    lc[5] = C_DIM;
    ln[5] = (g_curPage == PAGE_SONG) ? "PJ project for LD SV.  ARR: load a song"
                                     : "PG next page (hold: PATTERN). OC octave";
    lc[6] = C_DIM;   ln[6] = "Hold LD SV CLR CPY: again = yes. LD+SV demo";
    lc[7] = C_HEAD;
    switch (g_curPage) {
        case PAGE_SAMPLE: ln[7] = uiBrowsingLoops() ? "AUX load into the deck.  SNG song mode"
                                                    : "AUX hear the sample.  SNG song mode"; break;
        case PAGE_SONG:   ln[7] = "AUX loop start (hold: bounce).  SNG song"; break;
        default:          ln[7] = "Hold: SNG resample, SLD bass, AUX mic"; break;
    }
    lid.setFont(&fonts::Font2); lid.setTextWrap(false);
    for (int i = 0; i < 8; i++) {
        lid.fillRect(4, 125 + i * 14 + 4, 5, 8, lc[i]);
        lid.setTextColor(lc[i] == C_DIM ? C_TEXT : lc[i]); lid.setCursor(14, 125 + i * 14); lid.print(ln[i]);
    }
    lid.setFont(&fonts::Font0);
}

// ---------- update ----------
void lidUpdate(bool force) {
    if (!g_lidOk) return;                       // shut or absent: draw nothing, spend nothing
    uint32_t now = millis();
    bool edge = g_keysOverlay != (s_page == PAGE_KEYS);     // key map going up or down: no wait
    if (!force && !edge && now - s_lastMs < 60) return;
    s_lastMs = now;

    lid.startWrite();
    if (g_keysOverlay) {
        uint32_t k = FNV0;
        HASH(k, g_curPage); HASH(k, g_curTrack); HASH(k, g_curPattern);
        { uint16_t mu = seqMuteMask(); HASH(k, mu); HASH(k, g_songMode); }
        { bool l = uiBrowsingLoops(); HASH(k, l); }
        if (s_page != PAGE_KEYS || k != s_keysHash) { s_keysHash = k; s_page = PAGE_KEYS; keysStatic(); }
        lid.endWrite();
        return;
    }
    const int page = (g_curPage == PAGE_SONG && g_arrList) ? PAGE_ARR : (int)g_curPage;
    if (s_page != page) {                       // a new page: the one time the panel is cleared
        s_page = page;
        lid.fillScreen(C_BG);
        memset(s_cache, 0, sizeof(s_cache));
        s_footMode = -1;
    }
    switch (g_curPage) {
        case PAGE_PATTERN:  patternDraw();  break;
        case PAGE_SETTINGS: settingsDraw(); break;
        case PAGE_SAMPLE:   sampleDraw();   break;
        default:            songDraw();     break;
    }
    footer();
    lid.endWrite();
}
#endif
