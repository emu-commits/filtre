// ============================================================
// FILTRE - kbd_reader.cpp
//
// The library's TCA8418 reader hangs the keyboard for good while the rest of
// the firmware runs on. It waits for an edge on the INT pin, reads ONE event,
// clears the chip's key interrupt and then drops its own flag:
//   - a key event that lands between its INT_STAT read and `_isr_flag = false`
//     has its edge thrown away; the chip then holds INT low, so no edge ever
//     comes again and update() returns early forever;
//   - it clears only the key bit, so any other INT_STAT bit (GPI interrupts
//     are enabled on the unused pins) holds the line low with the same result;
//   - one event per update() lets the 10-event FIFO overflow when the loop is
//     busy drawing, losing releases (stuck keys).
//
// This reader goes by the LEVEL of INT (plus a timed poll, should the line
// itself fail), drains the FIFO, and clears every interrupt bit.
// ============================================================
#include <M5Cardputer.h>
#include <utility/Adafruit_TCA8418/Adafruit_TCA8418.h>
#include <utility/Adafruit_TCA8418/Adafruit_TCA8418_registers.h>
#include <algorithm>
#include <memory>
#include "kbd_reader.h"

#define KBD_INT_PIN      11
#define KBD_POLL_MS      50      // look anyway this often
#define KBD_MAX_EVENTS   16      // per update; the FIFO holds 10

#define INTSTAT_K    0x01
#define INTSTAT_GPI  0x02
#define INTSTAT_OVR  0x08
#define INTSTAT_ALL  0x1F

uint16_t g_kbdLostEdge = 0;
uint16_t g_kbdOverflow = 0;
uint16_t g_kbdGpiInt   = 0;

// only counts edges: it is how we recognise the case that hung the old reader
static volatile uint32_t s_edges = 0;
static void IRAM_ATTR kbdIsr() { s_edges = s_edges + 1; }

class FiltreKeyboardReader : public KeyboardReader {
public:
    void begin() override {
        _tca.reset(new Adafruit_TCA8418());
        if (!_tca->begin()) {
            Serial.println("KBD: TCA8418 init failed");
            _tca.reset();
            return;
        }
        _tca->matrix(7, 8);
        _tca->flush();
        pinMode(KBD_INT_PIN, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(KBD_INT_PIN), kbdIsr, CHANGE);
        _tca->enableInterrupts();
        _edgesSeen = s_edges;
        _lastPoll  = millis();
    }

    void update() override {
        if (!_tca) return;
        uint32_t nowMs = millis();
        bool low = (digitalRead(KBD_INT_PIN) == LOW);
        if (!low && !_carry && nowMs - _lastPoll < KBD_POLL_MS) return;
        _lastPoll = nowMs;

        // INT is asking, yet no edge arrived since we last cleared it: the old
        // reader would be dead from here on
        bool lostEdge = low && s_edges == _edgesSeen;

        // A press and its release are never applied in the same update, or a
        // tap shorter than one slow loop pass would not be seen at all: the
        // second event waits in _carry for the next pass.
        _nTouched = 0;
        bool drained = false;
        uint8_t got = 0;
        for (uint8_t i = 0; i < KBD_MAX_EVENTS; i++) {
            uint8_t ev = _carry ? _carry : _tca->getEvent();
            _carry = 0;
            if (ev == 0) { drained = true; break; }
            got++;
            if (!apply(ev)) { _carry = ev; break; }
        }

        if (drained) {
            uint8_t st = _tca->readRegister8(TCA8418_REG_INT_STAT) & INTSTAT_ALL;
            if (st & INTSTAT_GPI) {                   // only reading these lets the bit clear
                _tca->readRegister8(TCA8418_REG_GPIO_INT_STAT_1);
                _tca->readRegister8(TCA8418_REG_GPIO_INT_STAT_2);
                _tca->readRegister8(TCA8418_REG_GPIO_INT_STAT_3);
                g_kbdGpiInt++;
                Serial.printf("KBD: GPI interrupt cleared (INT_STAT %02X, #%u)\n", st, g_kbdGpiInt);
            }
            if (st & INTSTAT_OVR) {
                // events were lost and the chip cannot say which: start from
                // no keys down rather than keep one stuck
                _key_list.clear();
                g_kbdOverflow++;
                Serial.printf("KBD: FIFO overflow, key list reset (#%u)\n", g_kbdOverflow);
            }
            if (st) _tca->writeRegister8(TCA8418_REG_INT_STAT, st);
            _edgesSeen = s_edges;
            if (lostEdge && got) {
                g_kbdLostEdge++;
                Serial.printf("KBD: %u event(s) had no INT edge (INT_STAT %02X, #%u)\n", got, st, g_kbdLostEdge);
            }
        }
    }

private:
    std::unique_ptr<Adafruit_TCA8418> _tca;
    uint32_t  _lastPoll  = 0;
    uint32_t  _edgesSeen = 0;
    uint8_t   _carry     = 0;
    Point2D_t _touched[KBD_MAX_EVENTS];
    uint8_t   _nTouched  = 0;

    // false = this key already changed during this update; keep the event
    bool apply(uint8_t ev) {
        uint8_t code = ev & 0x7F;
        if (code < 1 || code > 80) return true;       // GPI event, not the matrix
        bool down = ev & 0x80;                        // the library reads bit 7 as "pressed"
        uint8_t r = (uint8_t)((code - 1) / 10), c = (uint8_t)((code - 1) % 10);
        Point2D_t p;                                  // same mapping as the original Cardputer
        p.x = r * 2 + (c > 3 ? 1 : 0);
        p.y = c % 4;

        for (uint8_t i = 0; i < _nTouched; i++) if (_touched[i] == p) return false;
        _touched[_nTouched++] = p;

        auto it = std::find(_key_list.begin(), _key_list.end(), p);
        if (down) { if (it == _key_list.end()) _key_list.push_back(p); }
        else      { if (it != _key_list.end()) _key_list.erase(it); }
        return true;
    }
};

void kbdReaderInstall() {
    if (M5.getBoard() != m5::board_t::board_M5CardputerADV) return;
    // the library's reader is about to be destroyed and its ISR points at it
    detachInterrupt(digitalPinToInterrupt(KBD_INT_PIN));
    M5Cardputer.Keyboard.begin(std::unique_ptr<KeyboardReader>(new FiltreKeyboardReader()));
}
