// ============================================================
// FILTRE - audio_engine.h
// ============================================================
#pragma once
#include "config.h"

extern float g_scopeBuf[SCREEN_W];
extern volatile int g_scopeIdx;

// resample tap: the render copies its output here while g_rsmpRemain > 0
extern int16_t*          g_rsmpDst;
extern volatile uint32_t g_rsmpRemain;
extern volatile uint32_t g_rsmpWr;

#define OUT_SCALE_LIVE    12000.0f   // upstream speaker level
#define OUT_SCALE_BOUNCE  26000.0f   // files get a healthier level

void audioEngineStart();   // creates the render task on core 0

// Render n mono samples: drains the event queue, runs the step clock, mixes.
// Called by the audio task, and directly by the offline bounce and host tests.
void engineRender(int16_t* buf, int n, float outScale);

// Park the audio task (it plays silence) while core 1 moves sample memory or
// swaps project data. Nestable.
void engineLock();
void engineUnlock();
void engineStopSampleVoices();   // sequencer.cpp: drop every voice that points into the arena
