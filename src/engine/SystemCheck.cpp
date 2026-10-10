#include "SystemCheck.h"
#include "ProbeRsEngine.h"
#include <QProcess>

namespace {

QString driverAdvice(const QString& family, bool windows) {
    if (!windows) {
        return "Install the probe-rs udev rules (the ProbeDesk .deb does this), then unplug and re-plug the probe "
               "and log out and in again. Your user must be allowed to access USB debug probes.";
    }
    if (family == "ST-Link") {
        return "Install the official ST-LINK USB driver from st.com (search for \"STSW-LINK009\"), then re-plug the probe.";
    }
    if (family == "J-Link") {
        return "probe-rs needs the WinUSB driver for J-Link. Install it for your J-Link with Zadig (zadig.akeo.ie): "
               "choose the J-Link, select WinUSB and click Install. After that SEGGER's own tools no longer find "
               "this probe until the SEGGER driver is restored in Device Manager.";
    }
    return "Re-plug the probe. CMSIS-DAP probes need no driver on Windows; if it is still missing, try another USB port or cable.";
}

bool engineSees(const QList<ProbeInfo>& probes, const UsbDevice& device) {
    for (const ProbeInfo& p : probes) {
        if (p.vendorId.compare(device.vid, Qt::CaseInsensitive) == 0
            && p.productId.compare(device.pid, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

}  // namespace

QList<CheckItem> SystemCheck::evaluate(const Input& in) {
    QList<CheckItem> items;
    const bool haveProbeRs = !in.probeRsPath.isEmpty();

    if (haveProbeRs) {
        items.append({CheckItem::Status::Ok, "probe-rs found",
                      (in.probeRsVersion.isEmpty() ? QString() : in.probeRsVersion + "\n") + in.probeRsPath});
    } else {
        items.append({CheckItem::Status::Error, "probe-rs not found",
                      "ProbeDesk uses probe-rs to talk to the probe. Install probe-rs (probe.rs/docs/getting-started/installation) "
                      "so that it is on PATH, or place probe-rs next to the ProbeDesk program."});
    }

    if (in.usbProbes.isEmpty()) {
        items.append({CheckItem::Status::Info, "No debug probe detected",
                      "Connect your ST-Link, J-Link or CMSIS-DAP probe with a USB cable."});
        return items;
    }

    for (const UsbDevice& device : in.usbProbes) {
        const QString family = UsbScanner::probeFamily(device.vid, device.pid);
        const QString label = QString("%1 (%2)").arg(family, device.id());

        if (!haveProbeRs) {
            items.append({CheckItem::Status::Info, label + " detected",
                          "The system sees this probe. It can be used once probe-rs is available."});
        } else if (engineSees(in.engineProbes, device)) {
            items.append({CheckItem::Status::Ok, label + " ready", "probe-rs can use this probe."});
        } else {
            QString detail = "The system sees this probe, but probe-rs cannot use it.";
            if (!device.driver.isEmpty()) detail += "\nCurrent driver: " + device.driver;
            detail += "\n" + driverAdvice(family, in.windows);
            items.append({CheckItem::Status::Warning, label + " cannot be used", detail});
        }
    }
    return items;
}

bool SystemCheck::needsAttention(const QList<CheckItem>& items) {
    for (const CheckItem& item : items) {
        if (item.status == CheckItem::Status::Error || item.status == CheckItem::Status::Warning) return true;
    }
    return false;
}

SystemCheck::Input SystemCheck::gather(const QString& probeRsPath) {
    Input in;
#ifdef Q_OS_WIN
    in.windows = true;
#endif
    in.probeRsPath = probeRsPath;

    if (!probeRsPath.isEmpty()) {
        QProcess version;
        version.start(probeRsPath, {"--version"});
        if (version.waitForFinished(4000)) {
            in.probeRsVersion = QString::fromUtf8(version.readAllStandardOutput()).trimmed();
        }

        QProcess list;
        list.start(probeRsPath, {"list"});
        if (list.waitForFinished(5000)) {
            in.engineProbes = ProbeRsEngine::parseProbeList(QString::fromUtf8(list.readAllStandardOutput()));
        }
    }

    in.usbProbes = UsbScanner::scan();
    return in;
}
