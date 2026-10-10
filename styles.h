// ============================================================
// FILTRE - styles.h
// Compiled-in styles (drums + pocket + filter settings), the bass
// writer, the 909 kit and the no-card demo.
// ============================================================
#pragma once
#include "config.h"

enum Style    : uint8_t { STYLE_FILTRE = 0, STYLE_NUDISCO, STYLE_HOMEWORK, STYLE_BANGER, STYLE_COUNT };
enum BassMode : uint8_t { BASS_OCTAVE = 0, BASS_OFFBEAT, BASS_FIFTH, BASS_COUNT };

extern const char* const g_styleNames[STYLE_COUNT];
extern const char* const g_bassModeNames[BASS_COUNT];
extern const char* const g_keyNames[12];

void styleKit();                                   // FILTRE 909 kit on the eight lanes
void styleApply(uint8_t style, uint8_t pattern);   // drums, BPM, swing, pump, filter (+ bass)
bool styleNeedsConfirm(uint8_t style, uint8_t pattern);   // it would overwrite the player's own notes
void writeBass(uint8_t pattern, uint8_t mode);     // 303 line on T1 in the current KEY
void filtreDefaults();                             // power-on state: kit + FILTRE style on P1
void loadDemoPattern();                            // no-card demo: factory loop + 4-pattern song

// factory_loop.cpp - original audio, CC0, mu-law @ 22.05 kHz
extern const uint8_t  g_factoryLoop[];
extern const uint32_t g_factoryLoopFrames;
