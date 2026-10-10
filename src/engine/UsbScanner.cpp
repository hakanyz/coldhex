#include "UsbScanner.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>

bool UsbScanner::isKnownProbe(const QString& vid, const QString& pid) {
    return !probeFamily(vid, pid).isEmpty();
}

QString UsbScanner::probeFamily(const QString& vid, const QString& pid) {
    static const QSet<QString> stLinkPids = {"3744", "3748", "374B", "374D", "374E", "374F", "3752", "3753", "3754", "3757"};
    const QString v = vid.toUpper();
    const QString p = pid.toUpper();

    if (v == "0483" && stLinkPids.contains(p)) return "ST-Link";
    if (v == "1366") return "J-Link";
    if (v == "0D28" && p == "0204") return "CMSIS-DAP";
    return {};
}

QList<UsbDevice> UsbScanner::parseWindowsJson(const QByteArray& json) {
    QList<UsbDevice> devices;
    QSet<QString> seen;

    QJsonDocument doc = QJsonDocument::fromJson(json);
    QJsonArray items;
    if (doc.isArray()) {
        items = doc.array();
    } else if (doc.isObject()) {
        items.append(doc.object());  // PowerShell prints a single result as an object
    }

    static const QRegularExpression idRegex(R"(VID_([0-9A-Fa-f]{4})&PID_([0-9A-Fa-f]{4}))");
    for (const QJsonValue& value : items) {
        const QJsonObject obj = value.toObject();
        const QRegularExpressionMatch m = idRegex.match(obj.value("InstanceId").toString());
        if (!m.hasMatch()) continue;

        UsbDevice d;
        d.vid = m.captured(1).toUpper();
        d.pid = m.captured(2).toUpper();
        if (!isKnownProbe(d.vid, d.pid)) continue;

        // A device with several interfaces appears more than once; keep the first entry.
        if (seen.contains(d.id())) continue;
        seen.insert(d.id());

        d.name = obj.value("FriendlyName").toString();
        d.driver = obj.value("Service").toString();
        devices.append(d);
    }
    return devices;
}

QList<UsbDevice> UsbScanner::scan() {
#ifdef Q_OS_WIN
    QProcess ps;
    const QString script =
        "Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match '^USB\\\\VID_(0483|1366|0D28)&' } | "
        "Select-Object InstanceId,FriendlyName,Service | ConvertTo-Json -Compress";
    ps.start("powershell", {"-NoProfile", "-NonInteractive", "-Command", script});
    if (!ps.waitForFinished(8000)) {
        ps.kill();
        return {};
    }
    return parseWindowsJson(ps.readAllStandardOutput());
#else
    QList<UsbDevice> devices;
    QSet<QString> seen;
    const QDir usb("/sys/bus/usb/devices");
    const QStringList entries = usb.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
        auto readAttr = [&](const QString& name) {
            QFile f(usb.filePath(entry + "/" + name));
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
        };
        UsbDevice d;
        d.vid = readAttr("idVendor").toUpper();
        d.pid = readAttr("idProduct").toUpper();
        if (d.vid.isEmpty() || !isKnownProbe(d.vid, d.pid) || seen.contains(d.id())) continue;
        seen.insert(d.id());
        d.name = readAttr("product");
        devices.append(d);
    }
    return devices;
#endif
}
