// ============================================================
// FILTRE - kbd_reader.h
// Replacement for the M5Cardputer library's TCA8418 reader.
// ============================================================
#pragma once
#include <stdint.h>

// Swap the library's reader for ours (Cardputer-ADV only). Call once, after
// M5Cardputer.begin().
void kbdReaderInstall();

// diagnostics, counted since boot
extern uint16_t g_kbdLostEdge;    // keys waiting with no interrupt edge to announce them
extern uint16_t g_kbdOverflow;    // the chip's 10-event FIFO overflowed
extern uint16_t g_kbdGpiInt;      // a non-key (GPI) interrupt held the line
