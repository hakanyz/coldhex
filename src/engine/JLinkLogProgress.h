#pragma once

#include <QString>
#include <cstdint>

// Turns the lines of the J-Link log file into a progress percentage.
//
// J-Link Commander writes nothing live to a pipe, but its log file (the `log` command) is written
// as it goes. While it erases or programs it logs which sector range it starts, for example
//   -- Erasing range 0x08000000 - 0x0800FFFF (  4 Sectors, 64 KB)
//   -- Programming range 0x08020000 - 0x0803FFFF (  1 Sector, 128 KB)
// A range is announced when it starts, so the bytes of the ranges before it are done.
// The percentage is spread over the phases: prepare, compare, erase, program, restore.
class JLinkLogProgress {
public:
    struct Update {
        bool changed = false;
        int percent = 0;
        QString text;  // for example "Erasing 40%"
    };

    // `eraseOnly`: the run is an erase command, whose erase ranges make up most of the work, and
    // `eraseBytes` is the size of the area it erases. For a program run both are not needed: the
    // size of the work is the sum of the ranges the Commander compares before it writes.
    void reset(bool eraseOnly, uint64_t eraseBytes);

    // Handles one line of the log file.
    Update feed(const QString& line);

private:
    Update set(int percent, const QString& activity);
    static int mix(int from, int to, uint64_t done, uint64_t total);

    bool m_eraseOnly = false;
    uint64_t m_eraseBytes = 0;
    uint64_t m_compareBytes = 0;  // everything the Commander compares: the size of the work
    uint64_t m_eraseDone = 0;
    uint64_t m_programDone = 0;
    uint64_t m_currentRange = 0;
    int m_percent = -1;
    QString m_activity;
};
