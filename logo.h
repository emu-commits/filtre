// ============================================================
// FILTRE - logo.h
// The wordmark, drawn from rectangles so the splash and the page corners
// share one shape at any size and it costs no bitmap. Letters sit on a
// 20-unit-high grid with 4-unit strokes and lean to the right.
// ============================================================
#pragma once
#include <stdint.h>

#define LOGO_H      20                  // units
#define LOGO_W      94                  // units, before the lean

struct LogoRect { uint8_t x, y, w, h; int8_t skew; };   // skew: units the bottom edge moves right
// F I L T R E, x already advanced letter by letter (letters 14 wide, I 4, 4 between)
static const LogoRect k_logo[] = {
    {  0, 0,  4, 20, 0 }, {  0, 0, 14, 4, 0 }, {  0,  8, 11, 4, 0 },                          // F
    { 19, 0,  4, 20, 0 },                                                                    // I: a touch right, the F is open on that side
    { 28, 0,  4, 20, 0 }, { 28, 16, 13, 4, 0 },                                              // L: closer to the T, whose bar overhangs it
    { 43, 0, 15,  4, 0 }, { 48, 0,  4, 20, 0 },                                              // T
    { 62, 0,  4, 20, 0 }, { 62, 0, 12, 4, 0 }, { 62,  8, 12, 4, 0 }, { 72, 2, 4, 8, 0 },     // R: stem, bowl
    { 68, 12, 4,  8, 4 },                                                                    //    leg
    { 80, 0,  4, 20, 0 }, { 80, 0, 14, 4, 0 }, { 80,  8, 11, 4, 0 }, { 80, 16, 14, 4, 0 },   // E
};

// scale = pixels per unit * 16 (8 = the 10 px corner mark, 40 = 50 px tall)
static inline int logoHeight(int scale) { return LOGO_H * scale / 16; }
static inline int logoWidth(int scale)  { return LOGO_W * scale / 16 + logoHeight(scale) / 4; }

template <class G, class C>
static void logoDraw(G& g, int x, int y, int scale, C col) {
    const int H = logoHeight(scale);
    for (const LogoRect& r : k_logo) {
        int y0 = r.y * scale / 16, y1 = (r.y + r.h) * scale / 16;
        int x0 = r.x * scale / 16, w = (r.x + r.w) * scale / 16 - x0;
        for (int py = y0; py < y1; py++) {
            int lean = (H - 1 - py) / 4;                                   // the italic: 1 px every 4 rows
            int sk = r.skew ? r.skew * scale * (py - y0) / (16 * (y1 - y0)) : 0;
            g.drawFastHLine(x + x0 + lean + sk, y + py, w, col);
        }
    }
}

// the stripes under the mark: bars that thin out, each in its own colour
template <class G, class C>
static void logoStripes(G& g, int x, int y, int w, int n, const C* cols) {
    for (int i = 0; i < n; i++) {
        int h = n - i;
        g.fillRect(x, y, w, h, cols[i]);
        y += h + 1 + i;
    }
}
