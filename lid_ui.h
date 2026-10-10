// ============================================================
// FILTRE - lid_ui.h
// The TERM512 lid: ST7789 2.8", 320x240, on the SD card's SPI bus.
// It is a lens on the internal page (waveforms, filter, song), never
// a second app: with the lid shut or absent the instrument is whole.
// ============================================================
#pragma once
#include "config.h"

extern bool g_lidOk;

// Call after M5Cardputer.begin() and BEFORE SD.begin(): bringing the panel
// up re-starts the shared Arduino SPI bus, which would drop a mounted card.
void lidInit();
void lidBootScreen();        // RGB bars + name + heap/arena (the bring-up check)
void lidUpdate(bool force);  // cheap; call from loop()
void lidToggle();
