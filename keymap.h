// ============================================================
// FILTRE - keymap.h  (from Microgroove's v6 keymap)
// THE single source of truth for key assignments. The KEY MAP overlay
// (hold enter, lid_ui.cpp) labels the keyboard from the lookups below,
// so it cannot drift from the bindings. There is no other help page.
//
//                       SHORT (tap)              LONG (hold 450 ms)
// ` 1 2                 select track 1/2/3       mute track
// 3   TD                drums -> deck A -> B     mute drums / that deck
// 4 5 6 7 8 9 0 -       select pattern 1..8      clone current -> there
//                       (SAMPLE pg: assign lane, or load deck A/B)
//                       (SONG pg: place)
// =                     LOAD info                "LOAD: Are you sure?"
// del                   SAVE info                "SAVE: Are you sure?"
//                       (press the same key again to do it; anything else
//                        cancels.  LOAD+SAVE held together = demo)
// ctl                   next page                jump to PATTERN page
// opt / alt             octave - / +             (immediate; SONG pg: project - / +)
// z                     clear cell / song slot   clear whole pattern
//                       (held while REC+playing = erase at playhead)
// , . ; /               arrows LEFT DOWN UP RIGHT, as printed on the keys
//                       (immediate, auto-repeat)
// x   SLD               toggle slide at cursor   WRITE BASS in KEY
// c   AUX               SAMPLE pg: preview/load  MIC RECORD (lane or deck)
//                       SONG pg: set loop start  SONG pg: BOUNCE to SD
// v   REC               toggle live record       (none)
// n   SONG              toggle song mode         RESAMPLE the mix (playing)
// m   ACC               held = accent modifier   (none - pure modifier)
// spc                   play / stop              play from top
//
// Tempo is not on the keyboard: it is the BPM row of the SETTINGS page.
//
// THE FILTER (the four keys the piano leaves dead), any page:
//   e / u               cutoff down / up   (hold REC-armed = record the sweep)
//   tab / p             resonance down / up
//   SONG pg: e/u = slot filter target, tab/p = slot repeats
//
// PIANO (PATTERN + SETTINGS pages, synth track selected)
//   white: fn sft a s d f g h j k l           black: q w r t y i o [
// PADS (drum track): lanes 1..8 on fn sft a s d f g h
// CHOPS (deck selected): slices 1..8 on fn sft a s d f g h, 9..16 on q w r t y i o [
// SONG pg: fn sft a s d f g = slot mutes T1 T2 T3 DRUMS A B KICK
//
// enter (ok)            held = KEY MAP on the lid: this keyboard, labelled for
//                       the page and track you are on. Every other key still works.
// ============================================================
#pragma once
#include "config.h"

// ---- shift-symbol normalization (US layer used by M5Cardputer lib) ----
static inline char normalizeKey(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    switch (c) {
        case '!': return '1'; case '@': return '2'; case '#': return '3';
        case '$': return '4'; case '%': return '5'; case '^': return '6';
        case '&': return '7'; case '*': return '8'; case '(': return '9';
        case ')': return '0'; case '_': return '-'; case '+': return '=';
        case '{': return '['; case '}': return ']'; case ':': return ';';
        case '"': return '\''; case '<': return ','; case '>': return '.';
        case '?': return '/'; case '~': return '`'; case '|': return '\\';
    }
    return c;
}

// ---- synthetic key codes for the non-printing keys ----
#define KC_NONE   0
#define KC_FN     200
#define KC_SHIFT  201
#define KC_CTRL   202
#define KC_OPT    203
#define KC_ALT    204
#define KC_TAB    205
#define KC_DEL    206
#define KC_ENTER  207
#define KC_SPACE  208

// ---- actions ----
enum : uint8_t {
    ACT_NONE = 0,
    // immediate (fire on key-down)
    ACT_LEFT, ACT_RIGHT, ACT_UP, ACT_DOWN,
    ACT_ACCENT, ACT_REC, ACT_KEYS,
    ACT_OCT_DN, ACT_OCT_UP,
    // continuous while held (input.cpp integrates them over time)
    ACT_CUT_DN, ACT_CUT_UP, ACT_RES_DN, ACT_RES_UP,
    // short/long (fire on release / after 450 ms) - keep ranges contiguous
    ACT_TRACK1, ACT_TRACK2, ACT_TRACK3, ACT_TRACKD,
    ACT_PAT1, ACT_PAT2, ACT_PAT3, ACT_PAT4,
    ACT_PAT5, ACT_PAT6, ACT_PAT7, ACT_PAT8,
    ACT_LOAD, ACT_SAVE,
    ACT_PAGE, ACT_PLAY, ACT_CLR,
    ACT_SONG, ACT_AUX, ACT_SLIDE,
};

// ---- key -> action (returns ACT_NONE for piano-only / dead keys) ----
static inline uint8_t keyAction(uint8_t kc) {
    switch (kc) {
        case '`': return ACT_TRACK1;  case '1': return ACT_TRACK2;
        case '2': return ACT_TRACK3;  case '3': return ACT_TRACKD;
        case '4': return ACT_PAT1;    case '5': return ACT_PAT2;
        case '6': return ACT_PAT3;    case '7': return ACT_PAT4;
        case '8': return ACT_PAT5;    case '9': return ACT_PAT6;
        case '0': return ACT_PAT7;    case '-': return ACT_PAT8;
        case '=':      return ACT_LOAD;
        case KC_DEL:   return ACT_SAVE;
        case KC_CTRL:  return ACT_PAGE;
        case KC_OPT:   return ACT_OCT_DN;
        case KC_ALT:   return ACT_OCT_UP;
        case 'z':      return ACT_CLR;
        // the arrows are where the Cardputer prints them
        case ',': return ACT_LEFT;  case '.': return ACT_DOWN;
        case ';': return ACT_UP;    case '/': return ACT_RIGHT;
        case 'x': return ACT_SLIDE; case 'c': return ACT_AUX;
        case 'v': return ACT_REC;
        case 'n': return ACT_SONG;  case 'm': return ACT_ACCENT;
        case KC_SPACE: return ACT_PLAY;
        case KC_ENTER: return ACT_KEYS;
        case 'e':    return ACT_CUT_DN;  case 'u': return ACT_CUT_UP;
        case KC_TAB: return ACT_RES_DN;  case 'p': return ACT_RES_UP;
    }
    return ACT_NONE;
}

// immediate actions fire on key-down; the rest are short/long holds
static inline bool actImmediate(uint8_t act) {
    switch (act) {
        case ACT_LEFT: case ACT_RIGHT: case ACT_UP: case ACT_DOWN:
        case ACT_ACCENT: case ACT_REC: case ACT_KEYS:
        case ACT_OCT_DN: case ACT_OCT_UP:
        case ACT_CUT_DN: case ACT_CUT_UP: case ACT_RES_DN: case ACT_RES_UP:
            return true;
        default: return false;
    }
}

static inline bool actRepeats(uint8_t act) {
    switch (act) {
        case ACT_LEFT: case ACT_RIGHT: case ACT_UP: case ACT_DOWN:
            return true;
        default: return false;
    }
}

// label shown next to the hold-progress bar
static inline const char* actLongName(uint8_t act) {
    if (act >= ACT_TRACK1 && act <= ACT_TRACK3) return "MUTE";
    if (act == ACT_TRACKD)                      return "MUTE";
    if (act >= ACT_PAT1 && act <= ACT_PAT8)     return "CLONE";
    switch (act) {
        case ACT_LOAD:   return "LOAD";
        case ACT_SAVE:   return "SAVE";
        case ACT_PAGE:   return "PATTERN PG";
        case ACT_PLAY:   return "FROM TOP";
        case ACT_CLR:    return "CLEAR PAT";
        case ACT_SONG:   return "RESAMPLE";
        case ACT_AUX:    return "MIC SAMPLE";
        case ACT_SLIDE:  return "WRITE BASS";
    }
    return "";
}

// ---- piano: semitones from C at the current octave, or -1 ----
// White keys on the home row, sharps on the row above them
// (shifted one key right so each sharp sits between its whites).
// The piano stops at l / [ : the ; key is the UP arrow.
static inline int8_t pianoSemi(uint8_t kc) {
    switch (kc) {
        // white keys
        case KC_FN:    return 0;   // C
        case KC_SHIFT: return 2;   // D
        case 'a':      return 4;   // E
        case 's':      return 5;   // F
        case 'd':      return 7;   // G
        case 'f':      return 9;   // A
        case 'g':      return 11;  // B
        case 'h':      return 12;  // C'
        case 'j':      return 14;  // D'
        case 'k':      return 16;  // E'
        case 'l':      return 17;  // F'
        // black keys (tab, e, u, p are the E-F / B-C gaps: FILTRE puts the filter there)
        case 'q':      return 1;   // C#
        case 'w':      return 3;   // D#
        case 'r':      return 6;   // F#
        case 't':      return 8;   // G#
        case 'y':      return 10;  // A#
        case 'i':      return 13;  // C#'
        case 'o':      return 15;  // D#'
        case '[':      return 18;  // F#'
    }
    return -1;
}

// ---- pads: drum lanes 1..8 on the white-key row, or -1 ----
static inline int8_t padLane(uint8_t kc) {
    switch (kc) {
        case KC_FN:    return 0;  case KC_SHIFT: return 1;
        case 'a':      return 2;  case 's':      return 3;
        case 'd':      return 4;  case 'f':      return 5;
        case 'g':      return 6;  case 'h':      return 7;
    }
    return -1;
}

// ---- chops: slices 1..16 when a deck is selected, or -1 ----
static inline int8_t padSlice(uint8_t kc) {
    int8_t lane = padLane(kc);
    if (lane >= 0) return lane;
    switch (kc) {
        case 'q': return 8;   case 'w': return 9;   case 'r': return 10;  case 't': return 11;
        case 'y': return 12;  case 'i': return 13;  case 'o': return 14;  case '[': return 15;
    }
    return -1;
}

// ---- SONG page: slot mute toggles on the first seven pads (bit index) ----
static inline int8_t padSlotMute(uint8_t kc) {
    int8_t lane = padLane(kc);
    return (lane >= 0 && lane < 7) ? lane : -1;
}
