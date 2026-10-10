// ============================================================
// FILTRE - ui.h
// ============================================================
#pragma once
#include "config.h"

extern Page    g_curPage;
extern bool    g_needRedraw;
extern float   g_holdProg;       // 0..1 long-press progress / mic level meter
extern char    g_holdLabel[16];  // label shown next to the footer bar
extern uint8_t g_soundParam;     // selected row on SOUND page
extern uint8_t g_songCursor;     // selected slot on SONG page

// simple file browser (SAMPLE page)
#define BROWSER_MAX 64
extern char    g_fileList[BROWSER_MAX][SAMPLE_NAME_LEN];
extern uint8_t g_fileCount;
extern uint8_t g_fileSel;

extern uint8_t g_helpScroll;     // first HELP line shown
extern bool    g_sdOk;

// SOUND page rows when a deck is focused: the deck itself, then the shared
// filter / pump / pocket settings.
enum DeckRow : uint8_t {
    DR_MODE = 0, DR_SLICES, DR_PITCH, DR_LEVEL, DR_FIT,
    DR_BLEND, DR_CUTOFF, DR_RESO, DR_TYPE, DR_POLES,
    DR_PUMP, DR_RELEASE, DR_BASSPUMP, DR_GHOST,
    DR_SWING, DR_KEY, DR_BASSMODE, DR_STYLE, DR_PATLEN,
    DR_SLICEPOS, DR_IN, DR_OUT, DR_START, DR_QUAL, DR_SETBPM,
    DR_COUNT
};
const char* uiDeckRowName(uint8_t row);
void uiDeckRowValue(uint8_t deck, uint8_t row, char* out, size_t n);
bool uiBrowsingLoops();                   // SAMPLE page lists /loops (a deck is focused)

void uiInit();
void uiDraw();
void uiSplash();
void uiStatus(const char* msg);          // transient message in footer
void uiScanSampleDir();                  // refresh SAMPLE page file list (samples or loops)
void uiDrawNow();                        // paint immediately (before a blocking load)
