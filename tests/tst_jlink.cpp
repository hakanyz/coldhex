#include "engine/JLinkCommands.h"
#include "engine/JLinkLocator.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace JLinkCommands;

class TestJLink : public QObject {
    Q_OBJECT

private:
    static void makeFile(const QString& path) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }

private slots:
    // --- which probes belong to this engine -------------------------------------------------
    void recognisesJLinkProbes() {
        QVERIFY(isJLinkProbe("1366:0101"));
        QVERIFY(isJLinkProbe("1366:0101:004294967295"));
        QVERIFY(isJLinkProbe("1366:1051:000123456789"));
        QVERIFY(!isJLinkProbe("0483:3748"));  // ST-Link
        QVERIFY(!isJLinkProbe("0d28:0204"));  // CMSIS-DAP
        QVERIFY(!isJLinkProbe(""));
    }

    void ignoresTheDefaultSerialNumber() {
        QCOMPARE(serialOf("1366:0101:004294967295"), QString());  // never programmed
        QCOMPARE(serialOf("1366:0101:4294967295"), QString());
        QCOMPARE(serialOf("1366:0101:-1"), QString());
        QCOMPARE(serialOf("1366:0101"), QString());
        QCOMPARE(serialOf("1366:0101:000123456789"), QString("123456789"));  // no leading zeros
    }

    // --- the command line and the scripts --------------------------------------------------
    void commandLineSelectsChipInterfaceAndSpeed() {
        TargetSettings t;
        t.chipName = "STM32F407VG";
        t.speedKhz = 4000;
        t.probeId = "1366:0101:004294967295";
        const QStringList args = arguments(t, "C:/tmp/command.jlink");
        QCOMPARE(args, (QStringList{"-NoGui", "1", "-device", "STM32F407VG", "-if", "SWD", "-speed", "4000",
                                    "-autoconnect", "1", "-ExitOnError", "1", "-CommanderScript",
                                    "C:/tmp/command.jlink"}));
    }

    void commandLineNamesTheProbeByItsSerialNumber() {
        TargetSettings t;
        t.chipName = "X";
        t.protocol = DebugProtocol::Jtag;
        t.probeId = "1366:0101:000123456789";
        const QStringList args = arguments(t, "s.jlink");
        QVERIFY(args.contains("JTAG"));
        const int at = args.indexOf("-SelectEmuBySN");
        QVERIFY(at >= 0);
        QCOMPARE(args.at(at + 1), QString("123456789"));
    }

    void scriptsUseForwardSlashesAndTheRightArguments() {
        QCOMPARE(connectScript(0x20000000), QString("mem32 0x20000000, 1\nexit\n"));
        // The end address of erase is the last byte, not the first one after the area.
        QCOMPARE(eraseScript(0x08000000, 0x08100000), QString("erase 0x08000000, 0x080fffff\nexit\n"));
        QCOMPARE(loadScript("C:\\fw\\app.hex", false, 0x08000000), QString("loadfile \"C:/fw/app.hex\"\nexit\n"));
        // A raw binary file needs the address it belongs at.
        QCOMPARE(loadScript("C:/fw/app.bin", true, 0x08008000),
                 QString("loadfile \"C:/fw/app.bin\", 0x08008000\nexit\n"));
        QCOMPARE(resetScript(), QString("r\ng\nexit\n"));
        QCOMPARE(logScript("C:\\t\\trace.log"), QString("log \"C:/t/trace.log\"\n"));
    }

    void theConnectScriptCanCheckTheLink() {
        QCOMPARE(connectScript(0x20000000, true),
                 QString("mem32 0x20000000, 1\nmem32 0xE00FF000, 8\nspeed 100\nmem32 0xE00FF000, 8\nexit\n"));
        QCOMPARE(kSafeSpeedKhz, 100);
    }

    void aShiftedReadIsFoundByTheLinkCheck() {
        // Real output of a J-Link V8 that was connected at 4000 kHz: the first read is shifted by a
        // word, the second one (at 100 kHz) is right. Neither run reports an error.
        const QString out =
            "J-Link>mem32 0x20000000, 1\n20000000 = 00000000\n"
            "J-Link>mem32 0xE00FF000, 8\n"
            "E00FF000 = 00000005 FFF0F003 FFF02003 FFF03003\n"
            "E00FF010 = FFF01003 FFF41003 FFF42003 00000000\n"
            "J-Link>speed 100\nSelecting 100 kHz as target interface speed\n"
            "J-Link>mem32 0xE00FF000, 8\n"
            "E00FF000 = FFF0F003 FFF02003 FFF03003 FFF01003\n"
            "E00FF010 = FFF41003 FFF42003 00000000 00000000\n";
        QCOMPARE(linkCheck(out), LinkCheck::Mismatch);
        QVERIFY(analyze(0, out).ok);  // not an error, so the connection still works
    }

    void aReliableLinkPassesTheLinkCheck() {
        const QString out =
            "J-Link>mem32 0xE00FF000, 8\n"
            "E00FF000 = FFF0F003 FFF02003 FFF03003 FFF01003\n"
            "E00FF010 = FFF41003 FFF42003 00000000 00000000\n"
            "J-Link>speed 100\n"
            "J-Link>mem32 0xE00FF000, 8\n"
            "E00FF000 = FFF0F003 FFF02003 FFF03003 FFF01003\n"
            "E00FF010 = FFF41003 FFF42003 00000000 00000000\n";
        QCOMPARE(linkCheck(out), LinkCheck::Consistent);
    }

    void theLinkCheckSaysNothingWithoutBothReads() {
        QCOMPARE(linkCheck(""), LinkCheck::NotDone);
        QCOMPARE(linkCheck("J-Link>mem32 0x20000000, 1\n20000000 = 00000000\n"), LinkCheck::NotDone);
        // Only the first read arrived (the run stopped).
        QCOMPARE(linkCheck("J-Link>mem32 0xE00FF000, 8\nE00FF000 = FFF0F003 FFF02003 FFF03003 FFF01003\n"
                           "E00FF010 = FFF41003 FFF42003 00000000 00000000\n"),
                 LinkCheck::NotDone);
    }

    void aReadIsSplitIntoPieces() {
        QCOMPARE(partCount(1048576, 65536), 16);
        QCOMPARE(partCount(65537, 65536), 2);
        QCOMPARE(partCount(100, 65536), 1);
        QCOMPARE(partCount(0, 65536), 0);

        const QStringList parts{"C:/t/p0.bin", "C:/t/p1.bin", "C:/t/p2.bin"};
        const QString script = readScript(0x08000000, 150000, 65536, parts);
        QVERIFY(script.contains("savebin \"C:/t/p0.bin\", 0x08000000, 0x00010000\n"));
        QVERIFY(script.contains("savebin \"C:/t/p1.bin\", 0x08010000, 0x00010000\n"));
        QVERIFY(script.contains("savebin \"C:/t/p2.bin\", 0x08020000, 0x000049f0\n"));  // 150000 - 131072 = 18928 bytes
        QVERIFY(script.endsWith("exit\n"));
    }

    // --- reading the output -----------------------------------------------------------------
    void aSuccessfulRunIsOk() {
        // Real output of a connect that worked (shortened).
        const QString out =
            "SEGGER J-Link Commander V9.14a (Compiled Jan 28 2026 17:17:36)\n"
            "J-Link Commander will now exit on Error\n\n"
            "J-Link>mem32 0x20000000, 1\n"
            "Connecting to J-Link via USB...O.K.\n"
            "Firmware: J-Link ARM V8 compiled Nov 28 2014 13:44:46\n"
            "VTref=3.338V\n"
            "Device \"STM32F407VG\" selected.\n"
            "Connecting to target via SWD\n"
            "Found SW-DP with ID 0x2BA01477\n"
            "Found Cortex-M4 r0p1, Little endian.\n"
            "T-bit of XPSR is 0 but should be 1. Changed to 1.\n"
            "20000000 = 00000000\n";
        const Outcome o = analyze(0, out);
        QVERIFY(o.ok);
        QVERIFY(o.error.isEmpty());
    }

    void aFailedConnectionIsFound() {
        const QString out =
            "J-Link>mem32 0x20000000, 1\n"
            "Connecting to J-Link via USB...O.K.\n"
            "VTref=0.000V\n"
            "Connecting to target via SWD\n"
            "InitTarget() start\n"
            "****** Error: Cannot connect to target.\n";
        const Outcome o = analyze(1, out);
        QVERIFY(!o.ok);
        QVERIFY(o.error.contains("Cannot connect"));
    }

    void anErrorLineBeatsAZeroExitCode() {
        QVERIFY(!analyze(0, "ERROR: Could not open file.\n").ok);
        QVERIFY(!analyze(0, "Verify failed at 0x08000010\n").ok);
        QVERIFY(!analyze(0, "Comparing data: mismatch at 0x08000020\n").ok);
    }

    void aBadExitCodeAloneIsAFailure() {
        const Outcome o = analyze(2, "all quiet\n");
        QVERIFY(!o.ok);
        QVERIFY(o.error.contains("exit code 2"));
    }

    void anEchoedCommandIsNotAnError() {
        // The word is inside a command the Commander only repeats.
        QVERIFY(analyze(0, "J-Link>ExitOnError 1 ... ERROR handling\nDone.\n").ok);
    }

    void anUnreliableLinkIsRecognisedFromTheRamCodeError() {
        // Real output of a write at a speed this probe cannot do: the exit code is 0 and it even
        // says "Erasing done.", but the line with the error is there.
        const QString out =
            "Erasing selected range...\n"
            "****** Error: Verification of RAMCode failed @ address 0x20000000.\n"
            "Write: 0xA801BE00 F0009900\n"
            "Read: 0x5E46BFF9 A801BE00\n"
            "Failed to prepare for programming.\n"
            "Failed to download RAMCode!\n"
            "Erasing done.\n";
        const Outcome o = analyze(0, out);
        QVERIFY(!o.ok);  // not fooled by the exit code and "done"
        QVERIFY(o.error.contains("RAMCode"));
        const QString a = advice(out);
        QVERIFY(a.contains("not reliable at this speed"));
        QVERIFY(a.contains("100 kHz"));
    }

    void adviceNamesTheUsualCauses() {
        QVERIFY(advice("Cannot connect to target.").contains("GND"));
        QVERIFY(advice("Unknown device 'FOO'").contains("does not know this chip"));
        QVERIFY(advice("No J-Link found on USB").contains("Close other programs"));
        QVERIFY(advice("everything fine").isEmpty());
    }

    // --- finding the SEGGER software --------------------------------------------------------
    void namesAreOrderedByTheNumbersInThem() {
        QVERIFY(JLinkLocator::naturalCompare("JLink_V99", "JLink_V914a") < 0);  // 99 < 914
        QVERIFY(JLinkLocator::naturalCompare("JLink_V914a", "JLink_V810") > 0);
        QVERIFY(JLinkLocator::naturalCompare("JLink_V810", "JLink_V0810") == 0);  // leading zeros do not count
        QVERIFY(JLinkLocator::naturalCompare("JLink_v810", "JLink_V810") == 0);   // nor does the case
        QVERIFY(JLinkLocator::naturalCompare("JLink", "JLink_V810") < 0);          // the shorter one is first
        QVERIFY(JLinkLocator::naturalCompare("JLink_V810b", "JLink_V810a") > 0);
        QVERIFY(JLinkLocator::naturalCompare("", "") == 0);
    }

    void findsTheNewestVersionFolder() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        makeFile(root.filePath("JLink_V810/JLink.exe"));
        makeFile(root.filePath("JLink_V914a/JLink.exe"));
        makeFile(root.filePath("JLink_V99/JLink.exe"));
        makeFile(root.filePath("Other/JLink.exe"));  // not a JLink* folder
        const QString found = JLinkLocator::find({root.path()}, "JLink.exe");
        QVERIFY2(found.contains("JLink_V914a"), qPrintable(found));  // 914 is newer than 99 and 810
    }

    void skipsFoldersWithoutTheProgram() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QDir().mkpath(root.filePath("JLink_V950"));  // newest, but empty
        makeFile(root.filePath("JLink_V810/JLink.exe"));
        QVERIFY(JLinkLocator::find({root.path()}, "JLink.exe").contains("JLink_V810"));
    }

    void findsNothingWhenThereIsNothing() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        qputenv("COLDHEX_JLINK", "");
        QVERIFY(JLinkLocator::find({root.path() + "/missing", root.path()}, "JLinkNoSuchProgram.exe").isEmpty());
    }

    void theEnvironmentVariableWins() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        makeFile(root.filePath("JLink_V810/JLink.exe"));
        makeFile(root.filePath("mine/MyJLink.exe"));
        qputenv("COLDHEX_JLINK", root.filePath("mine/MyJLink.exe").toUtf8());
        QCOMPARE(JLinkLocator::find({root.path()}, "JLink.exe"), root.filePath("mine/MyJLink.exe"));
        qputenv("COLDHEX_JLINK", "");
    }
};

QTEST_APPLESS_MAIN(TestJLink)
#include "tst_jlink.moc"
