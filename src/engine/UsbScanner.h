#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

struct UsbDevice {
    QString vid;      // upper-case hex, e.g. "0483"
    QString pid;      // upper-case hex, e.g. "3748"
    QString name;     // friendly name from the OS, may be empty
    QString driver;   // driver/service in use (Windows only), may be empty

    QString id() const { return vid + ":" + pid; }
};

// Looks for USB debug probes the operating system can see, regardless of whether probe-rs
// can use them. Comparing both views reveals missing drivers or permissions.
class UsbScanner {
public:
    // Known probe families: ST-Link, SEGGER J-Link, CMSIS-DAP (DAPLink).
    static bool isKnownProbe(const QString& vid, const QString& pid);
    static QString probeFamily(const QString& vid, const QString& pid);

    // Scans the machine (Windows: PowerShell; Linux: /sys/bus/usb).
    static QList<UsbDevice> scan();

    // Parses the JSON produced by the Windows scan command. Exposed for tests.
    static QList<UsbDevice> parseWindowsJson(const QByteArray& json);
};
