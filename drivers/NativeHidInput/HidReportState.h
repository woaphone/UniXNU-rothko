// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <stdint.h>

namespace hid_report {
constexpr unsigned Fingers = 10, ReportBytes = 62;
inline uint8_t keyBit(uint16_t usage) {
    switch (usage) {
    case 0x40: return 0;
    case 0x30: return 1;
    case 0xe9: return 2;
    case 0xea: return 3;
    default: return 0xff;
    }
}
struct Contact { int id, x, y; };
struct Slot { int id = -1; unsigned x = 0, y = 0; };

inline unsigned coordinate(int value, int maximum, bool reverse = false) {
    if (maximum < 2) return 0;
    if (value < 0) value = 0;
    if (value >= maximum) value = maximum - 1;
    if (reverse) value = maximum - 1 - value;
    return (unsigned)((int64_t)value * 32767 / (maximum - 1));
}

struct TouchState {
    Slot slots[Fingers];
    static void finger(uint8_t *report, unsigned slot, const Slot &s, bool down) {
        uint8_t *p = report + 1 + slot * 6;
        // Bit 0 is Tip Switch; bit 1 is Confidence (HID digitizer usage 0x47).
        p[0] = down ? 3 : 2; p[1] = (uint8_t)s.id;
        p[2] = (uint8_t)s.x; p[3] = (uint8_t)(s.x >> 8);
        p[4] = (uint8_t)s.y; p[5] = (uint8_t)(s.y >> 8);
        ++report[61];
    }
    // Keep a contact in its logical collection. Reusing a slot needs lift then down,
    // including replacement of all ten contacts in one hardware frame.
    unsigned update(const Contact *input, unsigned count, int maxX, int maxY,
                    uint8_t (&reports)[2][ReportBytes], bool rotate180 = false) {
        for (unsigned r = 0; r < 2; ++r) {
            for (unsigned b = 0; b < ReportBytes; ++b) reports[r][b] = 0;
            reports[r][0] = 3;
        }
        if (count > Fingers) count = Fingers;
        bool used[Fingers] = {}, lifted = false;
        for (unsigned s = 0; s < Fingers; ++s) {
            if (slots[s].id < 0) continue;
            unsigned i = 0;
            while (i < count && (used[i] || input[i].id != slots[s].id)) ++i;
            if (i < count) {
                used[i] = true;
                slots[s].x = coordinate(input[i].x, maxX, rotate180);
                slots[s].y = coordinate(input[i].y, maxY, rotate180);
                finger(reports[0], s, slots[s], true);
            } else {
                finger(reports[0], s, slots[s], false);
                slots[s].id = -1;
                lifted = true;
            }
        }
        bool added = false;
        for (unsigned i = 0; i < count; ++i) {
            if (used[i] || input[i].id < 0 || input[i].id > 255) continue;
            bool duplicate = false;
            for (unsigned s = 0; s < Fingers; ++s)
                if (slots[s].id == input[i].id) duplicate = true;
            if (duplicate) continue;
            for (unsigned s = 0; s < Fingers; ++s) {
                if (slots[s].id >= 0) continue;
                slots[s].id = input[i].id;
                slots[s].x = coordinate(input[i].x, maxX, rotate180);
                slots[s].y = coordinate(input[i].y, maxY, rotate180);
                added = true;
                break;
            }
        }
        const unsigned final = lifted && added ? 1 : 0;
        if (final || !lifted) {
            for (unsigned b = 1; b < ReportBytes; ++b) reports[final][b] = 0;
            for (unsigned s = 0; s < Fingers; ++s)
                if (slots[s].id >= 0) finger(reports[final], s, slots[s], true);
        }
        if (!reports[0][61]) return 0;
        return final + 1;
    }
};

// Call under the device's IOLock. A bounded overflow always resynchronizes the
// final physical state, rather than dropping a release and leaving a key down.
struct KeyQueue {
    static constexpr unsigned Depth = 64;
    uint8_t states[Depth] = {}, desired = 0;
    unsigned head = 0, tail = 0, overflows = 0;
    bool resync = false;
    bool push(unsigned bit, bool down) {
        if (bit > 3) return false;
        const uint8_t nextState = down ? desired | (1u << bit) : desired & ~(1u << bit);
        if (nextState == desired) return true;
        desired = nextState;
        const unsigned next = (head + 1) % Depth;
        if (resync || next == tail) { resync = true; ++overflows; return false; }
        states[head] = desired;
        head = next;
        return true;
    }
    bool pop(uint8_t &state) {
        if (tail != head) {
            state = states[tail];
            tail = (tail + 1) % Depth;
            return true;
        }
        if (!resync) return false;
        state = desired;
        resync = false;
        return true;
    }
};
} // namespace hid_report
