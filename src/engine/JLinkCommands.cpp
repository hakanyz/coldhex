#include "JLinkCommands.h"
#include <QList>
#include <QRegularExpression>
#include <algorithm>

namespace JLinkCommands {

namespace {

QString quoted(const QString& path) {
    QString p = path;
    p.replace('\\', '/');
    return "\"" + p + "\"";
}

QString hex(uint64_t value) {
    return QString("0x%1").arg(value, 8, 16, QLatin1Char('0'));
}

}  // namespace

bool isJLinkProbe(const QString& selector) {
    return selector.startsWith("1366:", Qt::CaseInsensitive);
}

QString serialOf(const QString& selector) {
    const QStringList parts = selector.split(':');
    if (parts.size() < 3) return {};
    // probe-rs shows the serial number with leading zeros (004294967295); the Commander wants
    // the plain number.
    QString serial = parts.at(2).trimmed();
    while (serial.size() > 1 && serial.startsWith('0')) serial.remove(0, 1);
    if (serial.isEmpty() || serial == "0" || serial == "-1" || serial == "4294967295") return {};
    return serial;
}

QStringList arguments(const TargetSettings& target, const QString& scriptFile) {
    QStringList args = {"-NoGui", "1",
                        "-device", target.chipName,
                        "-if", target.protocol == DebugProtocol::Jtag ? "JTAG" : "SWD"};
    if (target.speedKhz > 0) args << "-speed" << QString::number(target.speedKhz);
    const QString serial = serialOf(target.probeId);
    if (!serial.isEmpty()) args << "-SelectEmuBySN" << serial;
    args << "-autoconnect" << "1" << "-ExitOnError" << "1" << "-CommanderScript" << scriptFile;
    return args;
}

namespace {

// The ROM table of the debug logic: every Cortex-M has it, it always reads, and its words differ,
// so a shifted read shows.
const char* const kRomTable = "0xE00FF000";

}  // namespace

QString connectScript(uint64_t ramAddress, bool checkLink) {
    // Reading one word of RAM connects to the chip and works even when flash read-out is locked.
    QString script = QString("mem32 %1, 1\n").arg(hex(ramAddress));
    if (checkLink) {
        script += QString("mem32 %1, 8\nspeed %2\nmem32 %1, 8\n").arg(kRomTable).arg(kSafeSpeedKhz);
    }
    return script + "exit\n";
}

LinkCheck linkCheck(const QString& output) {
    // The words of each "mem32 0xE00FF000, 8" in the output, in order.
    QList<QStringList> reads;
    static const QRegularExpression echo(R"(^J-Link>\s*mem32\s+0xE00FF000)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression data(R"(^[0-9A-Fa-f]{8}\s*=\s*((?:[0-9A-Fa-f]{8}\s*)+)$)");

    for (const QString& raw : output.split('\n')) {
        const QString line = raw.trimmed();
        if (echo.match(line).hasMatch()) {
            reads.append(QStringList());
            continue;
        }
        const QRegularExpressionMatch m = data.match(line);
        if (m.hasMatch() && !reads.isEmpty()) {
            reads.last() += m.captured(1).simplified().toUpper().split(' ');
        }
    }
    if (reads.size() != 2 || reads.at(0).size() != 8 || reads.at(1).size() != 8) return LinkCheck::NotDone;
    return reads.at(0) == reads.at(1) ? LinkCheck::Consistent : LinkCheck::Mismatch;
}

QString eraseScript(uint64_t start, uint64_t end) {
    return QString("erase %1, %2\nexit\n").arg(hex(start), hex(end - 1));
}

QString loadScript(const QString& file, bool isBinary, uint64_t baseAddress) {
    // The address is only used (and only allowed) for raw binary files.
    const QString address = isBinary ? ", " + hex(baseAddress) : QString();
    return QString("loadfile %1%2\nexit\n").arg(quoted(file), address);
}


QString logScript(const QString& logFile) {
    return QString("log %1\n").arg(quoted(logFile));
}

QString resetScript() {
    return "r\ng\nexit\n";
}

int partCount(uint64_t byteCount, uint64_t chunkSize) {
    if (byteCount == 0 || chunkSize == 0) return 0;
    return static_cast<int>((byteCount + chunkSize - 1) / chunkSize);
}

QString readScript(uint64_t address, uint64_t byteCount, uint64_t chunkSize, const QStringList& partFiles) {
    QString script;
    uint64_t done = 0;
    for (const QString& part : partFiles) {
        if (done >= byteCount) break;
        const uint64_t length = std::min(chunkSize, byteCount - done);
        script += QString("savebin %1, %2, %3\n").arg(quoted(part), hex(address + done), hex(length));
        done += length;
    }
    return script + "exit\n";
}

Outcome analyze(int exitCode, const QString& output) {
    // Wording taken from real J-Link Commander output.
    static const char* const failures[] = {
        "ERROR", "Cannot connect to target", "Could not connect", "Could not find", "Could not open",
        "FAILED", "No J-Link found", "Unknown device", "Cannot read", "Failed to"};

    // Said in any case of letters ("Verify failed", "mismatch at ...").
    static const char* const looseFailures[] = {"failed", "mismatch"};

    Outcome outcome;
    const QStringList lines = output.split('\n');
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith("J-Link>")) continue;  // an echoed command
        bool failed = false;
        for (const char* needle : failures) {
            if (line.contains(QLatin1String(needle), Qt::CaseSensitive)) failed = true;
        }
        for (const char* needle : looseFailures) {
            if (line.contains(QLatin1String(needle), Qt::CaseInsensitive)) failed = true;
        }
        if (failed) {
            outcome.ok = false;
            outcome.error = line;
            return outcome;
        }
    }
    if (exitCode != 0) {
        outcome.ok = false;
        outcome.error = QString("J-Link Commander ended with exit code %1.").arg(exitCode);
    }
    return outcome;
}

QString advice(const QString& output) {
    auto has = [&output](const char* needle) { return output.contains(QLatin1String(needle), Qt::CaseInsensitive); };

    if (has("Unknown device") || (has("Device \"") && has("not found")) || has("is unknown")) {
        return "The J-Link software does not know this chip.\n"
               "- Pick a chip that is in SEGGER's device list (STM32 devices are).\n"
               "- Or use WinUSB (Zadig) for this probe so that probe-rs, which knows more chips, can be used.";
    }
    if (has("RAMCode failed") || has("Failed to download RAMCode") || has("Failed to prepare for programming")) {
        return "Writing to the chip's RAM failed, so the connection is not reliable at this speed.\n"
               "- Lower the speed (an older or simple J-Link may need 100 kHz; try 400, 100 or lower).\n"
               "- Use short wires and connect GND next to SWDIO and SWCLK.\n"
               "- Reading can then give wrong data too: read again at a lower speed to be sure.";
    }
    if (has("No J-Link found") || has("Connecting to J-Link via USB...FAILED") || has("Could not open")) {
        return "The J-Link cannot be opened.\n"
               "- Close other programs that use it (Keil, J-Link Commander, other debuggers).\n"
               "- Re-plug the probe, and check that SEGGER's driver is installed.";
    }
    if (has("Cannot connect to target") || has("Could not connect") || has("VTref=0.000")) {
        return "The probe works, but the target does not answer.\n"
               "- GND of the probe and the board must be connected, and the board needs power (VTref).\n"
               "- SWDIO and SWCLK must go to the matching pins.\n"
               "- Lower the speed, and check the chip choice and the selected interface (SWD or JTAG).";
    }
    return {};
}

}  // namespace JLinkCommands
