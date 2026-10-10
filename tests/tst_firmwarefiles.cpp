#include "core/FirmwareFiles.h"
#include <QtTest>

class TestFirmwareFiles : public QObject {
    Q_OBJECT

private slots:
    void recognisesFirmwareFiles() {
        QVERIFY(FirmwareFiles::isFirmwareFile("app.hex"));
        QVERIFY(FirmwareFiles::isFirmwareFile("C:/fw/App.BIN"));
        QVERIFY(FirmwareFiles::isFirmwareFile("app.elf"));
        QVERIFY(!FirmwareFiles::isFirmwareFile("notes.txt"));
        QVERIFY(!FirmwareFiles::isFirmwareFile("app"));
    }

    void recognisesBootloaderNames() {
        QVERIFY(FirmwareFiles::looksLikeBootloader("boot.hex"));
        QVERIFY(FirmwareFiles::looksLikeBootloader("C:/x/My_Bootloader.bin"));
        QVERIFY(FirmwareFiles::looksLikeBootloader("app_bl.elf"));
        QVERIFY(FirmwareFiles::looksLikeBootloader("bl_v2.hex"));
        QVERIFY(FirmwareFiles::looksLikeBootloader("bl.hex"));
        QVERIFY(!FirmwareFiles::looksLikeBootloader("application.hex"));
        QVERIFY(!FirmwareFiles::looksLikeBootloader("blink.hex"));
    }

    void singleFileGoesByName() {
        auto a = FirmwareFiles::assignDroppedFiles({"boot.hex"});
        QCOMPARE(a.size(), 1);
        QCOMPARE(a[0].slot, 1);

        a = FirmwareFiles::assignDroppedFiles({"main.hex"});
        QCOMPARE(a[0].slot, 2);
    }

    void twoPlainFilesFillBothSlotsInOrder() {
        const auto a = FirmwareFiles::assignDroppedFiles({"first.hex", "second.hex"});
        QCOMPARE(a.size(), 2);
        QCOMPARE(a[0].slot, 1);
        QCOMPARE(a[1].slot, 2);
    }

    void bootloaderNameWinsOverOrder() {
        const auto a = FirmwareFiles::assignDroppedFiles({"app.hex", "boot.hex"});
        QCOMPARE(a[0].slot, 2);
        QCOMPARE(a[1].slot, 1);
    }

    void skipsNonFirmwareAndLimitsToTwo() {
        bool extra = false;
        auto a = FirmwareFiles::assignDroppedFiles({"readme.txt", "a.hex"}, &extra);
        QCOMPARE(a.size(), 1);
        QVERIFY(!extra);

        a = FirmwareFiles::assignDroppedFiles({"a.hex", "b.hex", "c.hex"}, &extra);
        QCOMPARE(a.size(), 2);
        QVERIFY(extra);

        QVERIFY(FirmwareFiles::assignDroppedFiles({"x.txt"}).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestFirmwareFiles)
#include "tst_firmwarefiles.moc"
