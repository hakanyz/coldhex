#include "engine/ProbeRsEngine.h"
#include <QtTest>

class TestProbeList : public QObject {
    Q_OBJECT

private slots:
    void parsesProbeWithoutSerial() {
        const QString out = "The following debug probes were found:\n[0]: STLink V2 -- 0483:3748: (ST-LINK)\n";
        const auto probes = ProbeRsEngine::parseProbeList(out);
        QCOMPARE(probes.size(), 1);
        QCOMPARE(probes[0].displayName, QString("STLink V2"));
        QCOMPARE(probes[0].vendorId, QString("0483"));
        QCOMPARE(probes[0].productId, QString("3748"));
        QVERIFY(probes[0].serialNumber.isEmpty());
        QCOMPARE(probes[0].selector(), QString("0483:3748"));
    }

    void parsesProbeWithSerial() {
        const QString out = "The following debug probes were found:\n[0]: J-Link -- 1366:0101:000123456789 (J-Link)\n";
        const auto probes = ProbeRsEngine::parseProbeList(out);
        QCOMPARE(probes.size(), 1);
        QCOMPARE(probes[0].selector(), QString("1366:0101:000123456789"));
    }

    void parsesMultipleProbes() {
        const QString out =
            "The following debug probes were found:\r\n"
            "[0]: STLink V2 -- 0483:3748: (ST-LINK)\r\n"
            "[1]: CMSIS-DAP -- 0d28:0204:ABCDEF (CMSIS-DAP)\r\n";
        const auto probes = ProbeRsEngine::parseProbeList(out);
        QCOMPARE(probes.size(), 2);
        QCOMPARE(probes[1].selector(), QString("0D28:0204:ABCDEF"));
    }

    void ignoresHeaderAndEmptyOutput() {
        QVERIFY(ProbeRsEngine::parseProbeList("").isEmpty());
        QVERIFY(ProbeRsEngine::parseProbeList("No debug probes were found.\n").isEmpty());
        QVERIFY(ProbeRsEngine::parseProbeList("The following debug probes were found:\n").isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestProbeList)
#include "tst_proberslist.moc"
