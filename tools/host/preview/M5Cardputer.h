// Host shim: lets ui.cpp and lid_ui.cpp draw into real M5GFX sprites so both
// screens can be rendered to image files without the hardware.
#pragma once
#include "Arduino.h"
#include <lgfx/v1/LGFXBase.hpp>
#include <lgfx/v1/LGFX_Sprite.hpp>
#include <lgfx/v1/lgfx_fonts.hpp>
#include <algorithm>
using std::max;
using std::min;

typedef lgfx::LGFX_Sprite M5Canvas;
#define TFT_BLACK 0x0000
#define OUTPUT 1
#define HIGH 1
static inline void pinMode(int, int) {}
static inline void digitalWrite(int, int) {}

struct HostDisplay : public lgfx::LGFX_Sprite { void setRotation(int) {} };
struct HostCardputer { HostDisplay Display; };
extern HostCardputer M5Cardputer;
struct HostEsp { uint32_t getFreeHeap() { return 61 * 1024; } };
extern HostEsp ESP;
