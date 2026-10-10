#include "engine/SystemCheck.h"
#include <QtTest>

namespace {

UsbDevice usb(const QString& vid, const QString& pid, const QString& driver = QString()) {
    UsbDevice d;
    d.vid = vid;
    d.pid = pid;
    d.driver = driver;
    return d;
}

ProbeInfo engineProbe(const QString& vid, const QString& pid) {
    ProbeInfo p;
    p.vendorId = vid;
    p.productId = pid;
    return p;
}

bool hasStatus(const QList<CheckItem>& items, CheckItem::Status s) {
    for (const CheckItem& i : items) if (i.status == s) return true;
    return false;
}

QString allText(const QList<CheckItem>& items) {
    QString t;
    for (const CheckItem& i : items) t += i.title + "\n" + i.detail + "\n";
    return t;
}

}  // namespace

class TestSystemCheck : public QObject {
    Q_OBJECT

private slots:
    void everythingReady() {
        SystemCheck::Input in;
        in.probeRsPath = "C:/tools/probe-rs.exe";
        in.probeRsVersion = "probe-rs 0.32.0";
        in.usbProbes = {usb("0483", "3748")};
        in.engineProbes = {engineProbe("0483", "3748")};
        in.windows = true;

        const auto items = SystemCheck::evaluate(in);
        QVERIFY(!SystemCheck::needsAttention(items));
        QVERIFY(allText(items).contains("ST-Link (0483:3748) ready"));
    }

    void missingProbeRsIsAnError() {
        SystemCheck::Input in;
        in.usbProbes = {usb("0483", "3748")};

        const auto items = SystemCheck::evaluate(in);
        QVERIFY(hasStatus(items, CheckItem::Status::Error));
        QVERIFY(SystemCheck::needsAttention(items));
        QVERIFY(allText(items).contains("probe-rs not found"));
        // The probe is only reported as detected, not as broken.
        QVERIFY(!hasStatus(items, CheckItem::Status::Warning));
    }

    void noProbeIsOnlyInformation() {
        SystemCheck::Input in;
        in.probeRsPath = "probe-rs";

        const auto items = SystemCheck::evaluate(in);
        QVERIFY(!SystemCheck::needsAttention(items));
        QVERIFY(allText(items).contains("No debug probe detected"));
    }

    void seenByWindowsButNotByProbeRsMeansDriverProblem() {
        SystemCheck::Input in;
        in.probeRsPath = "probe-rs";
        in.windows = true;
        in.usbProbes = {usb("1366", "0101", "JLink"), usb("0483", "3748", "WinUSB")};
        in.engineProbes = {engineProbe("0483", "3748")};

        const auto items = SystemCheck::evaluate(in);
        QVERIFY(SystemCheck::needsAttention(items));
        const QString text = allText(items);
        QVERIFY(text.contains("J-Link (1366:0101) cannot be used"));
        QVERIFY(text.contains("Zadig"));
        QVERIFY(text.contains("Current driver: JLink"));
        QVERIFY(text.contains("ST-Link (0483:3748) ready"));
    }

    void stLinkAdviceNamesTheOfficialDriver() {
        SystemCheck::Input in;
        in.probeRsPath = "probe-rs";
        in.windows = true;
        in.usbProbes = {usb("0483", "374B")};

        QVERIFY(allText(SystemCheck::evaluate(in)).contains("STSW-LINK009"));
    }

    void linuxAdviceMentionsUdev() {
        SystemCheck::Input in;
        in.probeRsPath = "probe-rs";
        in.windows = false;
        in.usbProbes = {usb("0483", "3748")};

        QVERIFY(allText(SystemCheck::evaluate(in)).contains("udev"));
    }

    void matchesIdsCaseInsensitively() {
        SystemCheck::Input in;
        in.probeRsPath = "probe-rs";
        in.usbProbes = {usb("0D28", "0204")};
        in.engineProbes = {engineProbe("0d28", "0204")};

        QVERIFY(!SystemCheck::needsAttention(SystemCheck::evaluate(in)));
    }
};

QTEST_GUILESS_MAIN(TestSystemCheck)
#include "tst_systemcheck.moc"
