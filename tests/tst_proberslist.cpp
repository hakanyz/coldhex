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

    void buildsTargetArguments() {
        TargetSettings t;
        t.chipName = "STM32F407VG";
        t.speedKhz = 2000;
        t.probeId = "0483:3748";
        const QStringList args = ProbeRsEngine::targetArgsFor(t);

        QCOMPARE(args, (QStringList{"--chip", "STM32F407VG", "--non-interactive", "--protocol", "swd",
                                    "--speed", "2000", "--probe", "0483:3748"}));
    }

    void targetArgumentsReflectProtocolResetAndDescription() {
        TargetSettings t;
        t.chipName = "APM32F103CB";
        t.speedKhz = 0;
        t.protocol = DebugProtocol::Jtag;
        t.connectUnderReset = true;
        t.chipDescriptionPath = "C:/chips/apm32.yaml";
        const QStringList args = ProbeRsEngine::targetArgsFor(t);

        QVERIFY(args.contains("jtag"));
        QVERIFY(!args.contains("swd"));
        QVERIFY(args.contains("--connect-under-reset"));
        QVERIFY(!args.contains("--speed"));
        QVERIFY(!args.contains("--probe"));
        const int i = args.indexOf("--chip-description-path");
        QVERIFY(i >= 0);
        QCOMPARE(args.at(i + 1), QString("C:/chips/apm32.yaml"));
    }
};

QTEST_GUILESS_MAIN(TestProbeList)
#include "tst_proberslist.moc"
