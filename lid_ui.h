// ============================================================
// FILTRE - lid_ui.h
// The TERM512 lid: ST7789 2.8", 320x240, on the SD card's SPI bus.
// With the lid open it is the editor (pattern grid, settings list, browser,
// song) and the internal screen is the lens beside it. With the lid shut or
// absent the internal screen is the editor: the instrument is still whole.
// ============================================================
#pragma once
#include "config.h"

extern bool g_lidOk;
extern bool g_keysOverlay;   // true while the KEY MAP key is held: the lid shows the keyboard

// Call after M5Cardputer.begin() and BEFORE SD.begin(): bringing the panel
// up re-starts the shared Arduino SPI bus, which would drop a mounted card.
void lidInit();
void lidBootScreen();        // RGB bars + name + heap/arena (the bring-up check)
void lidUpdate(bool force);  // cheap; call from loop()
void lidToggle();
