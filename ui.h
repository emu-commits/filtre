// ============================================================
// FILTRE - ui.h
// ============================================================
#pragma once
#include "config.h"

extern Page    g_curPage;
extern bool    g_needRedraw;
extern float   g_holdProg;       // 0..1 long-press progress / mic level meter
extern char    g_holdLabel[16];  // label shown next to the footer bar
extern uint8_t g_soundParam;     // selected row on SETTINGS page
extern uint8_t g_songCursor;     // selected slot on SONG page

// simple file browser (SAMPLE page)
#define BROWSER_MAX 48
extern char    g_fileList[BROWSER_MAX][LOOP_NAME_LEN];
extern uint8_t g_fileCount;
extern uint8_t g_fileSel;

extern bool    g_sdOk;

// SETTINGS page. With a deck focused the rows are the deck itself, then the
// shared block (tempo, pocket, filter, pump, lid). Synth and drum tracks list
// their own rows, then the same shared block: every setting is reachable from
// any track, and none of them needs a key of its own.
enum DeckRow : uint8_t {
    DR_MODE = 0, DR_SLICES, DR_SLICEPOS, DR_PITCH, DR_LEVEL, DR_FIT,
    DR_IN, DR_OUT, DR_START, DR_QUAL, DR_SETBPM,
    DR_BPM, DR_SWING, DR_KEY, DR_BASSMODE, DR_STYLE, DR_PATLEN,
    DR_BLEND, DR_CUTOFF, DR_RESO, DR_TYPE, DR_POLES,
    DR_PUMP, DR_RELEASE, DR_BASSPUMP, DR_GHOST, DR_LID,
    DR_COUNT
};
#define DR_SHARED_FIRST DR_BPM
#define SYNTH_PARAMS 9
#define DRUM_PARAMS  7
const char* uiDeckRowName(uint8_t row);
void uiDeckRowValue(uint8_t deck, uint8_t row, char* out, size_t n);
void uiSoundRowText(uint8_t row, char* name, size_t nn, char* val, size_t vn);   // synth / drum rows
uint8_t uiRowCount();                     // rows on SETTINGS for the focused track
uint8_t uiRowDeckRow(uint8_t row);        // the DeckRow behind a row; DR_COUNT = a synth / drum row
void uiRowText(uint8_t row, char* name, size_t nn, char* val, size_t vn);
void uiTrackTitle(char* out, size_t n);   // "DECK A" / "DRUM LANE 3" / "SYNTH 2"
bool uiBrowsingLoops();                   // SAMPLE page lists /loops (a deck is focused)

void uiInit();
void uiDraw();
void uiSplash();
void uiStatus(const char* msg);          // transient message in footer
void uiStatusFor(const char* msg, uint32_t ms);
const char* uiStatusText();              // the message while it is up, else ""
void uiTick();                           // call from loop(): takes an expired message down
void uiScanSampleDir();                  // refresh SAMPLE page file list (samples or loops)
void uiDrawNow();                        // paint immediately (before a blocking load)
