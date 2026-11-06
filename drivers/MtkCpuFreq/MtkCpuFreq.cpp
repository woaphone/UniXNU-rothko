// SPDX-License-Identifier: BSD-2-Clause-Patent
// ABI: MediaTek mediatek-cpufreq-hw_main.c. Firmware owns PLL/voltage sequencing.
#include "MtkCpuFreq.h"
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <libkern/c++/OSSharedPtr.h>

namespace {
constexpr unsigned kLutCount = 32;
constexpr unsigned kEnable = 0x84, kRequest = 0x88, kState = 0x8c;
uint32_t read(uintptr_t base, unsigned offset) {
    return *reinterpret_cast<volatile uint32_t *>(base + offset);
}
void write(uintptr_t base, unsigned offset, uint32_t value) {
    *reinterpret_cast<volatile uint32_t *>(base + offset) = value;
    __asm__ volatile("dsb sy" ::: "memory");
}
OSSharedPtr<IOMemoryMap> map(uint64_t address, uint32_t size) {
    IODeviceMemory *memory = IODeviceMemory::withRange(address, size);
    if (!memory) return nullptr;
    OSSharedPtr<IOMemoryMap> mapping(memory->map(kIOMapAnywhere | kIOMapInhibitCache), OSNoRetain);
    memory->release();
    return mapping;
}
}
void mtkCpuFreqStart(const MtkCpuFreqDomain *domains, unsigned count,
                     uint64_t scalingModeAddress, uint64_t fdvfsSupportAddress,
                     uint64_t fdvfsRequestAddress) {
    auto modeMap = map(scalingModeAddress, 4);
    if (!modeMap) return;
    auto supportMap = map(fdvfsSupportAddress, 4);
    auto requestMap = map(fdvfsRequestAddress, 32);
    if (!supportMap || !requestMap) return;
    const bool fdvfs = read(supportMap->getVirtualAddress(), 0) == 0xabcd0001;
    const bool frequencyRequest = read(modeMap->getVirtualAddress(), 0) != 0;
    for (unsigned domain = 0; domain < count; ++domain) {
        const auto &resource = domains[domain];
        auto mmio = map(resource.address, resource.size);
        if (!mmio) continue;
        uintptr_t base = mmio->getVirtualAddress();
        uint32_t before = read(base, kRequest), state = read(base, kState);
        unsigned selected = 0, entries = 0;
        uint32_t selectedKhz = 0, previousKhz = 0;
        for (unsigned i = 0; i < kLutCount; ++i) {
            uint32_t khz = (read(base, i * 4) & 0xfff) * 1000;
            if (!khz || khz == previousKhz) break;
            ++entries;
            if (khz <= resource.targetKhz && khz > selectedKhz) {
                selected = i;
                selectedKhz = khz;
            }
            previousKhz = khz;
        }
        // An absent firmware LUT is not a license to program a guessed PLL.
        if (!selectedKhz) {
            kprintf("MTK-CPUFREQ17: domain=%u LUT unavailable entries=%u request=%u state=%x\n",
                    domain, entries, before, state);
            continue;
        }
        write(base, kEnable, 1);
        if (fdvfs) {
            // MTK FDVFS: each logical CPU votes in units of 26 MHz, rounded up.
            const uint32_t vote = (selectedKhz + 25999) / 26000;
            for (unsigned cpu = 0; cpu < 8; ++cpu) {
                if (resource.cpuMask & (1u << cpu)) {
                    uint32_t oldVote = read(requestMap->getVirtualAddress(), cpu * 4);
                    write(requestMap->getVirtualAddress(), cpu * 4, vote);
                    kprintf("MTK-CPUFREQ17: cpu=%u FDVFS vote=%u->%u units=26000khz\n",
                            cpu, oldVote, read(requestMap->getVirtualAddress(), cpu * 4));
                }
            }
        } else {
            write(base, kRequest, frequencyRequest ? selectedKhz : selected);
        }
        kprintf("MTK-CPUFREQ17: domain=%u prior=%u mode=%s target-khz=%u index=%u entries=%u request=%u state=%x->%x\n",
                domain, before, fdvfs ? "FDVFS" : frequencyRequest ? "khz" : "index", selectedKhz,
                selected, entries, read(base, kRequest), state, read(base, kState));
    }
}
