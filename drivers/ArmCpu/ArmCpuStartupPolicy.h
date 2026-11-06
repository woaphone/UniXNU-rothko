// SPDX-License-Identifier: BSD-2-Clause-Patent
#pragma once
#include <stdint.h>

namespace armcpu_startup {
constexpr unsigned Slots = 8;
// Secondaries install architectural IRQ handlers directly in their own cpu_data.
constexpr unsigned BootInterruptCpus = 1;
constexpr uint64_t AffinityMask = 0xff00ffffffULL;
constexpr unsigned SgiFrameOffset = 0x10000, SgiPendingOffset = 0x200;

inline void pendSgiZero(volatile uint32_t *sgiFrame) {
    sgiFrame[SgiPendingOffset / sizeof(uint32_t)] = 1U;
}

inline bool signalTargetValid(uint64_t affinity) {
    return (affinity & 0xff) < 16;
}

inline uint64_t sgiZero(uint64_t affinity) {
    return ((affinity & 0xff00000000ULL) << 16) |
           ((affinity & 0xff0000) << 16) |
           ((affinity & 0xff00) << 8) |
           (1ULL << (affinity & 0xff));
}

inline bool resetAffinityMatches(uint64_t expected, uint64_t observed) {
    return (expected & 0xffff) == (observed & 0xffff);
}
}
