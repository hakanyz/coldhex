#include "SlotValidator.h"

bool SlotValidator::parseAddress(const QString& text, uint32_t& out) {
    QString t = text.trimmed();
    if (t.startsWith("0x", Qt::CaseInsensitive)) {
        t = t.mid(2);
    }
    if (t.isEmpty()) return false;

    bool ok = false;
    const qulonglong value = t.toULongLong(&ok, 16);
    if (!ok || value > 0xFFFFFFFFull) return false;

    out = static_cast<uint32_t>(value);
    return true;
}

bool SlotValidator::overlaps(const AddressRange& a, const AddressRange& b) {
    if (a.size == 0 || b.size == 0) return false;

    // 64-bit math so ranges ending at the top of the address space do not wrap.
    const uint64_t aEnd = static_cast<uint64_t>(a.start) + a.size;
    const uint64_t bEnd = static_cast<uint64_t>(b.start) + b.size;
    return a.start < bEnd && b.start < aEnd;
}
