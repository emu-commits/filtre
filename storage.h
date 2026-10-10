// ============================================================
// CardputerGroovebox - storage.h
// Project save/load to SD: /groovebox/projects/P1.gbx .. P8.gbx
// A project = patterns + song + synth params + drum lane config
// + sample references (samples reloaded by filename on load).
// ============================================================
#pragma once
#include "config.h"

#define NUM_PROJECT_SLOTS 8

extern uint8_t g_curProject;    // 0..7

bool storageSaveProject(uint8_t slot);
bool storageLoadProject(uint8_t slot);
bool storageProjectExists(uint8_t slot);

// Offline bounce of the song chain (or the current pattern x4 when the chain
// is empty) to /groovebox/bounce: full mix plus drums / bass / synth / decks
// stems, all starting on bar 1 and the same length. progress(0..1) may be null.
// Returns the take number written, or -1.
int  storageBounce(void (*progress)(float done, const char* what));
