#include "engine/ProbeInspector.h"
#include <QtTest>

using namespace ProbeInspect;

class TestProbeInspector : public QObject {
    Q_OBJECT

private slots:
    void readsJLinkBanner() {
        // Real start-up text of J-Link Commander V9.14a.
        const QString out =
            "SEGGER J-Link Commander V9.14a (Compiled Jan 28 2026 17:17:36)\r\n"
            "DLL version V9.14a, compiled Jan 28 2026 17:16:39\r\n"
            "\r\n"
            "Connecting to J-Link via USB...O.K.\r\n"
            "Firmware: J-Link V9 compiled May  7 2021 16:26:12\r\n"
            "Hardware version: V9.70\r\n"
            "J-Link uptime (since boot): N/A (Not supported by this model)\r\n"
            "S/N: 59768885\r\n"
            "License(s): RDI, GDB, FlashDL, FlashBP, JFlash\r\n"
            "VTref=3.270V\r\n";
        ProbeDetails d;
        QVERIFY(parseJLinkBanner(out, &d));
        QCOMPARE(d.name, QString("J-Link V9"));
        QCOMPARE(d.firmware, QString("May  7 2021 16:26:12"));
        QCOMPARE(d.hardware, QString("V9.70"));
        QCOMPARE(d.serial, QString("59768885"));
        QVERIFY(d.hasVoltage());
        QVERIFY(qAbs(d.voltage - 3.27) < 0.001);
    }

    void jlinkWithoutVoltageLine() {
        ProbeDetails d;
        QVERIFY(parseJLinkBanner("Firmware: J-Link V10 compiled Jan  1 2024 10:00:00\r\n", &d));
        QVERIFY(!d.hasVoltage());
    }

    void jlinkFailureIsNotABanner() {
        ProbeDetails d;
        QVERIFY(!parseJLinkBanner("Connecting to J-Link via USB...FAILED: Cannot connect to J-Link.\r\n", &d));
    }

    void readsHelperAnswerWithVoltage() {
        const QString out =
            "{\"event\":\"probe\",\"name\":\"ST-Link V2\",\"voltage\":3.2689}\n"
            "{\"event\":\"result\",\"ok\":true}\n";
        ProbeDetails d;
        QVERIFY(parseHelperInfo(out, &d));
        QCOMPARE(d.name, QString("ST-Link V2"));
        QVERIFY(qAbs(d.voltage - 3.2689) < 0.0001);
    }

    void helperNullVoltageMeansUnknown() {
        ProbeDetails d;
        QVERIFY(parseHelperInfo("{\"event\":\"probe\",\"name\":\"CMSIS-DAP\",\"voltage\":null}\n", &d));
        QVERIFY(!d.hasVoltage());
    }

    void helperErrorIsNotAnAnswer() {
        ProbeDetails d;
        QVERIFY(!parseHelperInfo("{\"event\":\"result\",\"ok\":false,\"error\":\"no debug probe found\"}\n", &d));
    }

    void describesWhatIsKnown() {
        ProbeDetails d;
        d.name = "ST-Link V2";
        d.voltage = 3.2689;
        QCOMPARE(describe(d), QString("ST-Link V2 | target voltage 3.27 V"));
    }

    void describesMissingAndLowVoltage() {
        ProbeDetails d;
        d.name = "CMSIS-DAP";
        QVERIFY(describe(d).contains("not reported"));
        d.voltage = 0.0;
        QVERIFY(describe(d).contains("no target power"));
    }
};

QTEST_APPLESS_MAIN(TestProbeInspector)
#include "tst_probeinspector.moc"
