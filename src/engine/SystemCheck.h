#pragma once

#include "IProgrammerEngine.h"
#include "UsbScanner.h"
#include <QList>
#include <QString>

struct CheckItem {
    enum class Status { Ok, Info, Warning, Error };

    Status status = Status::Info;
    QString title;
    QString detail;
};

// Compares what the operating system sees with what probe-rs can use and explains the
// difference. Nothing is installed or changed; the user follows the advice.
class SystemCheck {
public:
    struct Input {
        QString probeRsPath;        // empty when probe-rs was not found
        QString probeRsVersion;     // e.g. "probe-rs 0.32.0", may be empty
        QList<UsbDevice> usbProbes; // probes the OS sees
        QList<ProbeInfo> engineProbes; // probes probe-rs sees
        bool windows = false;
    };

    static QList<CheckItem> evaluate(const Input& input);

    // True when the user has something to fix (an error or warning, not just "no probe plugged in").
    static bool needsAttention(const QList<CheckItem>& items);

    // Gathers the real data from this machine (runs probe-rs and the USB scan).
    static Input gather(const QString& probeRsPath);
};
