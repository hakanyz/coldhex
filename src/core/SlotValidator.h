#pragma once

#include <QString>
#include <cstdint>

struct AddressRange {
    uint32_t start = 0;
    uint32_t size = 0;
};

class SlotValidator {
public:
    // Accepts "0x08000000", "08000000" or "8000000" (hexadecimal, optional 0x prefix).
    static bool parseAddress(const QString& text, uint32_t& out);

    // True when the two ranges share at least one byte. Empty ranges never overlap.
    static bool overlaps(const AddressRange& a, const AddressRange& b);
};
