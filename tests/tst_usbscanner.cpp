#include "engine/ProbeRsLocator.h"
#include "engine/UsbScanner.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TestUsbScanner : public QObject {
    Q_OBJECT

private slots:
    void recognisesProbeFamilies() {
        QCOMPARE(UsbScanner::probeFamily("0483", "3748"), QString("ST-Link"));
        QCOMPARE(UsbScanner::probeFamily("0483", "374b"), QString("ST-Link"));
        QCOMPARE(UsbScanner::probeFamily("1366", "0101"), QString("J-Link"));
        QCOMPARE(UsbScanner::probeFamily("0d28", "0204"), QString("CMSIS-DAP"));
        QVERIFY(!UsbScanner::isKnownProbe("0483", "5740"));   // an ST virtual COM port, not a probe
        QVERIFY(!UsbScanner::isKnownProbe("046D", "C52F"));
    }

    void parsesWindowsJsonArray() {
        const QByteArray json =
            R"([{"InstanceId":"USB\VID_0483&PID_3748\6&104FED9E&0&2","FriendlyName":"STM32 STLink","Service":"WinUSB"},)"
            R"({"InstanceId":"USB\VID_0483&PID_3748&MI_00\7&1","FriendlyName":"ST interface","Service":"usbccgp"},)"
            R"({"InstanceId":"USB\VID_1366&PID_0101\000123456789","FriendlyName":"J-Link","Service":"JLink"}])";

        const auto devices = UsbScanner::parseWindowsJson(json);
        QCOMPARE(devices.size(), 2);  // the duplicate ST-Link interface entry is dropped
        QCOMPARE(devices[0].id(), QString("0483:3748"));
        QCOMPARE(devices[0].driver, QString("WinUSB"));
        QCOMPARE(devices[1].id(), QString("1366:0101"));
    }

    void parsesSingleObjectAndEmptyOutput() {
        const QByteArray one = R"({"InstanceId":"USB\VID_1366&PID_0101\1","FriendlyName":"J-Link","Service":null})";
        QCOMPARE(UsbScanner::parseWindowsJson(one).size(), 1);
        QVERIFY(UsbScanner::parseWindowsJson("").isEmpty());
        QVERIFY(UsbScanner::parseWindowsJson("not json").isEmpty());
    }

    void locatorPrefersBundledCopy() {
        QTemporaryDir app;
        const QString exeName = QStringLiteral("probe-rs") +
#ifdef Q_OS_WIN
            ".exe";
#else
            "";
#endif
        QFile f(app.filePath(exeName));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.close();
#ifndef Q_OS_WIN
        f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
#endif

        const QString found = ProbeRsLocator::find(app.path(), "/nonexistent-home");
        QCOMPARE(QDir::cleanPath(found), QDir::cleanPath(app.filePath(exeName)));
    }

    void locatorSearchesLibFolderOfLinuxPackage() {
        const QStringList dirs = ProbeRsLocator::candidateDirs("/opt/probedesk/bin", "/home/u");
        QVERIFY(dirs.contains("/opt/probedesk/lib/probedesk"));
    }
};

QTEST_GUILESS_MAIN(TestUsbScanner)
#include "tst_usbscanner.moc"
