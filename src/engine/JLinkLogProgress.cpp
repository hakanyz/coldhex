#include "JLinkLogProgress.h"
#include <QRegularExpression>
#include <algorithm>

namespace {

// "( 4 Sectors, 64 KB)" -> 65536. Returns 0 when the line has no size.
uint64_t rangeBytes(const QString& payload) {
    static const QRegularExpression size(R"(\(\s*\d+\s+Sectors?,\s*(\d+)\s*(KB|MB|Bytes)\s*\))");
    const QRegularExpressionMatch m = size.match(payload);
    if (!m.hasMatch()) return 0;
    const uint64_t number = m.captured(1).toULongLong();
    const QString unit = m.captured(2);
    if (unit == "MB") return number * 1024 * 1024;
    if (unit == "KB") return number * 1024;
    return number;
}

}  // namespace

void JLinkLogProgress::reset(bool eraseOnly, uint64_t eraseBytes) {
    *this = JLinkLogProgress();
    m_eraseOnly = eraseOnly;
    m_eraseBytes = eraseBytes;
}

int JLinkLogProgress::mix(int from, int to, uint64_t done, uint64_t total) {
    if (total == 0) return from;
    return from + static_cast<int>((to - from) * std::min(done, total) / total);
}

JLinkLogProgress::Update JLinkLogProgress::set(int percent, const QString& activity) {
    percent = std::min(99, std::max(percent, m_percent));  // never goes back, never reaches 100
    Update update;
    update.percent = percent;
    update.text = QString("%1 %2%").arg(activity).arg(percent);
    update.changed = percent != m_percent || activity != m_activity;
    m_percent = percent;
    m_activity = activity;
    return update;
}

JLinkLogProgress::Update JLinkLogProgress::feed(const QString& line) {
    static const QRegularExpression marker(R"(--\s+(\S.*)$)");
    const QRegularExpressionMatch m = marker.match(line);
    if (!m.hasMatch()) return {};
    const QString what = m.captured(1).trimmed();

    // Where the erase and the program phase lie on the scale of 100.
    const int eraseFrom = m_eraseOnly ? 10 : 15;
    const int eraseTo = m_eraseOnly ? 95 : 15;
    const int programFrom = 15;
    const int programTo = 95;

    if (what.startsWith("Start of preparing flash programming")) return set(2, "Preparing");
    if (what.startsWith("End of preparing flash programming")) return set(10, "Preparing");

    if (what.startsWith("Comparing range")) {
        m_compareBytes += rangeBytes(what);
        return set(12, "Comparing");
    }
    if (what.startsWith("End of comparing flash")) return set(15, "Comparing");

    if (what.startsWith("Start of erasing sectors")) {
        m_eraseDone = 0;
        m_currentRange = 0;
        return set(eraseFrom, "Erasing");
    }
    if (what.startsWith("Erasing range")) {
        m_eraseDone += m_currentRange;  // the range before this one is finished
        m_currentRange = rangeBytes(what);
        const uint64_t total = m_eraseOnly ? m_eraseBytes : m_compareBytes;
        return set(mix(eraseFrom, eraseTo, m_eraseDone, total), "Erasing");
    }
    if (what.startsWith("End of erasing sectors")) return set(eraseTo, "Erasing");

    if (what.startsWith("Start of flash programming")) {
        m_programDone = 0;
        m_currentRange = 0;
        return set(programFrom, "Programming");
    }
    if (what.startsWith("Programming range")) {
        m_programDone += m_currentRange;
        m_currentRange = rangeBytes(what);
        return set(mix(programFrom, programTo, m_programDone, m_compareBytes), "Programming");
    }
    if (what.startsWith("End of flash programming")) return set(programTo, "Programming");

    if (what.startsWith("Start of restoring")) return set(96, "Finishing");
    if (what.startsWith("End of restoring")) return set(99, "Finishing");

    return {};
}
