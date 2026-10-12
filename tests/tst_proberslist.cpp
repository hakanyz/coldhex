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

    void helperArgumentsSelectChipProbeProtocolAndSpeed() {
        TargetSettings t;
        t.chipName = "STM32F407VG";
        t.speedKhz = 15000;
        t.probeId = "0483:3748";
        const QStringList args = ProbeRsEngine::helperTargetArgsFor(t);
        QCOMPARE(args, (QStringList{"--chip", "STM32F407VG", "--protocol", "swd", "--speed", "15000",
                                    "--probe", "0483:3748"}));
        QVERIFY(!args.contains("--non-interactive"));  // an option of the probe-rs tool only

        t.protocol = DebugProtocol::Jtag;
        t.connectUnderReset = true;
        t.chipDescriptionPath = "C:/chips/apm.yaml";
        const QStringList more = ProbeRsEngine::helperTargetArgsFor(t);
        QVERIFY(more.contains("jtag"));
        QVERIFY(more.contains("--connect-under-reset"));
        QVERIFY(more.contains("--chip-description-path"));
    }

    void helperFormatFollowsTheFileExtension() {
        QCOMPARE(ProbeRsEngine::helperFormat("app.hex"), QString("hex"));
        QCOMPARE(ProbeRsEngine::helperFormat("APP.IHEX"), QString("hex"));
        QCOMPARE(ProbeRsEngine::helperFormat("app.bin"), QString("bin"));
        QCOMPARE(ProbeRsEngine::helperFormat("app.elf"), QString("elf"));
        QCOMPARE(ProbeRsEngine::helperFormat("app.axf"), QString("elf"));
        QVERIFY(ProbeRsEngine::helperFormat("app.txt").isEmpty());  // probe-rs decides for these
    }

    void aConnectionNoticesWhenItsProbeIsGone() {
        const QStringList both{"0483:3748", "1366:0101:004294967295"};
        QVERIFY(ProbeRsEngine::probeStillListed("0483:3748", both));
        QVERIFY(ProbeRsEngine::probeStillListed("1366:0101:004294967295", both));
        QVERIFY(!ProbeRsEngine::probeStillListed("0483:3748", {"1366:0101:004294967295"}));
        QVERIFY(!ProbeRsEngine::probeStillListed("0483:3748", {}));
    }

    void aConnectionWithoutAChosenProbeNeedsAnyProbe() {
        QVERIFY(ProbeRsEngine::probeStillListed("", {"0483:3748"}));
        QVERIFY(!ProbeRsEngine::probeStillListed("", {}));
    }

    void helperFormatArgumentsAddTheBaseAddressOnlyForBinFiles() {
        QCOMPARE(ProbeRsEngine::helperFormatArgs("app.hex", 0x08008000), (QStringList{"--format", "hex"}));
        QCOMPARE(ProbeRsEngine::helperFormatArgs("app.bin", 0x08008000),
                 (QStringList{"--format", "bin", "--base-address", "0x08008000"}));
        QVERIFY(ProbeRsEngine::helperFormatArgs("app.txt", 0).isEmpty());
    }

    void parsesChipInfoMemoryRegions() {
        const QString out =
            "STM32F407VG\n"
            "Cores (1):\n"
            "    - main (Armv7em)\n"
            "NVM: 0x08000000..0x08100000 (1.0 MiB)\n"
            "RAM: 0x10000000..0x10010000 (64.0 KiB)\n"
            "NVM: 0x1fff7800..0x1fff7a10 (528 B)\n"
            "RAM: 0x20000000..0x2001c000 (112.0 KiB)\n";
        const ChipMemoryMap map = ProbeRsEngine::parseChipInfo(out);

        QCOMPARE(map.regions.size(), 4);
        QVERIFY(map.mainFlash());
        QCOMPARE(map.mainFlash()->start, uint64_t(0x08000000));
        QCOMPARE(map.mainFlash()->size, uint64_t(0x100000));
        QCOMPARE(map.firstRam()->start, uint64_t(0x10000000));
        QVERIFY(map.flashContains(0x08008000, 0x4000));
        QVERIFY(!map.flashContains(0x080FFF00, 0x200));   // runs past the end of the flash
        QVERIFY(!map.flashContains(0x20000000, 16));      // RAM is not flash
        QVERIFY(ProbeRsEngine::parseChipInfo("Error: unknown chip\n").regions.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestProbeList)
#include "tst_proberslist.moc"
