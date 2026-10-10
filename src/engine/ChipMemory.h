#pragma once

#include <QList>
#include <cstdint>

struct MemoryRegion {
    uint64_t start = 0;
    uint64_t size = 0;
    bool nonVolatile = false;  // flash/NVM (true) or RAM (false)

    uint64_t end() const { return start + size; }
};

// The memory layout of a chip as described by probe-rs or a chip description file.
class ChipMemoryMap {
public:
    QList<MemoryRegion> regions;

    bool isValid() const { return !regions.isEmpty(); }

    // The biggest non-volatile region: the program flash.
    const MemoryRegion* mainFlash() const {
        const MemoryRegion* best = nullptr;
        for (const MemoryRegion& r : regions) {
            if (r.nonVolatile && (!best || r.size > best->size)) best = &r;
        }
        return best;
    }

    // The first RAM region. Reading RAM works even when flash read-out protection is on.
    const MemoryRegion* firstRam() const {
        for (const MemoryRegion& r : regions) {
            if (!r.nonVolatile) return &r;
        }
        return nullptr;
    }

    // True when [start, start+size) lies completely inside one non-volatile region.
    bool flashContains(uint64_t start, uint64_t size) const {
        for (const MemoryRegion& r : regions) {
            if (r.nonVolatile && start >= r.start && start + size <= r.end()) return true;
        }
        return false;
    }
};
