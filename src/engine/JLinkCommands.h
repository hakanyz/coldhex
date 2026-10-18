#pragma once

#include "IProgrammerEngine.h"
#include <QString>
#include <QStringList>
#include <cstdint>

// Everything needed to drive SEGGER's own J-Link Commander (JLink.exe / JLinkExe) without touching
// a process: the command line, the command script and the reading of its output. J-Link
// Commander uses the SEGGER USB driver, so a probe does not have to be switched to WinUSB
// (which would take it away from Keil and SEGGER's other tools).
namespace JLinkCommands {

// True for a probe selector (VID:PID[:serial]) of a SEGGER J-Link (vendor id 1366).
bool isJLinkProbe(const QString& selector);

// The serial number in a selector, or an empty string when there is none or it is the default
// value of probes that were never given a serial number (0xFFFFFFFF, shown as -1 or 4294967295).
QString serialOf(const QString& selector);

// Command line for one run of the Commander that executes the script file.
QStringList arguments(const TargetSettings& target, const QString& scriptFile);

// The speed at which even a poor J-Link (an old one, or with long wires) reads and writes correctly.
constexpr int kSafeSpeedKhz = 100;

// Command scripts. Paths are written with forward slashes and quoted.
// With `checkLink` the script also reads the same area twice, once at the chosen speed and once at
// kSafeSpeedKhz, so linkCheck() can tell whether the link is reliable at the chosen speed.
QString connectScript(uint64_t ramAddress, bool checkLink = false);
QString eraseScript(uint64_t start, uint64_t end);
QString loadScript(const QString& file, bool isBinary, uint64_t baseAddress);
QString resetScript();
// First line of a script that makes the Commander write a log file while it works.
QString logScript(const QString& logFile);
// Reads `parts` pieces of `chunkSize` bytes (the last one may be shorter) into partFiles[i], so the
// progress can be seen from the files that appear while the script runs.
QString readScript(uint64_t address, uint64_t byteCount, uint64_t chunkSize, const QStringList& partFiles);
// The same, but only for the parts in `which` (indexes into `partFiles`): used to try again for
// the parts a failed read did not finish. `sizes` holds the size of every part.
QString readPartsScript(uint64_t address, uint64_t chunkSize, const QStringList& partFiles,
                        const QList<qint64>& sizes, const QList<int>& which);
// True when the Commander ended because the chip could not be read at some address.
bool readFailedMidway(const QString& output);
// How many parts readScript() needs.
int partCount(uint64_t byteCount, uint64_t chunkSize);

// The result of one Commander run as far as it can be told from the exit code and the output.
struct Outcome {
    bool ok = true;
    QString error;  // the line that shows what went wrong
};
Outcome analyze(int exitCode, const QString& output);

// What the two reads of a connectScript(.., true) run say. An unreliable J-Link returns the data
// shifted by a word at higher speeds, with no error at all, so the two reads then differ.
enum class LinkCheck { NotDone, Consistent, Mismatch };
LinkCheck linkCheck(const QString& output);

// A short explanation with hints for the usual reasons of a failed run, or an empty string.
QString advice(const QString& output);

}  // namespace JLinkCommands
