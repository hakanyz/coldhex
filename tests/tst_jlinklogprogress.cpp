#include "engine/JLinkLogProgress.h"
#include <QtTest>

// Lines as they are in a real J-Link log file (the prefix is the thread id and the time).
static QString L(const char* text) {
    return QString("T7318 002:633.184    -- ") + QLatin1String(text);
}

class TestJLinkLogProgress : public QObject {
    Q_OBJECT

private slots:
    void aLineWithoutTheMarkerChangesNothing() {
        JLinkLogProgress p;
        p.reset(true, 1000);
        QVERIFY(!p.feed("T7318 000:088.624 JLINK_IsConnected()").changed);
        QVERIFY(!p.feed("").changed);
        QVERIFY(!p.feed("T7318 000:900.054    -- --------------------------------------").changed);
    }

    void anEraseRunsThroughItsRanges() {
        // The real erase of the STM32F407's 1 MiB flash: 4x16 KB + 64 KB + 7x128 KB.
        JLinkLogProgress p;
        p.reset(true, 1024 * 1024);

        auto u = p.feed(L("Start of preparing flash programming"));
        QCOMPARE(u.percent, 2);
        QCOMPARE(u.text, QString("Preparing 2%"));
        QCOMPARE(p.feed(L("End of preparing flash programming")).percent, 10);

        QCOMPARE(p.feed(L("Start of erasing sectors")).percent, 10);
        // The first range has just started: nothing before it is done.
        u = p.feed(L("Erasing range 0x08000000 - 0x0800FFFF (  4 Sectors, 64 KB)"));
        QCOMPARE(u.text, QString("Erasing 10%"));
        // 64 KB of 1024 KB are done when the second range starts: 10 + 85 * 64 / 1024 = 15.
        u = p.feed(L("Erasing range 0x08010000 - 0x0801FFFF (  1 Sector, 64 KB)"));
        QCOMPARE(u.percent, 15);
        // Two ranges of 64 KB done, the third (128 KB) starts: 10 + 85 * 128 / 1024 = 20.
        u = p.feed(L("Erasing range 0x08020000 - 0x0803FFFF (  1 Sector, 128 KB)"));
        QCOMPARE(u.percent, 20);
        // The last range starts: 896 KB of 1024 KB are done.
        p.feed(L("Erasing range 0x08040000 - 0x0805FFFF (  1 Sector, 128 KB)"));
        p.feed(L("Erasing range 0x08060000 - 0x0807FFFF (  1 Sector, 128 KB)"));
        p.feed(L("Erasing range 0x08080000 - 0x0809FFFF (  1 Sector, 128 KB)"));
        p.feed(L("Erasing range 0x080A0000 - 0x080BFFFF (  1 Sector, 128 KB)"));
        p.feed(L("Erasing range 0x080C0000 - 0x080DFFFF (  1 Sector, 128 KB)"));
        u = p.feed(L("Erasing range 0x080E0000 - 0x080FFFFF (  1 Sector, 128 KB)"));
        QCOMPARE(u.percent, 10 + 85 * 896 / 1024);

        QCOMPARE(p.feed(L("End of erasing sectors")).percent, 95);
        QCOMPARE(p.feed(L("Start of restoring")).percent, 96);
        QCOMPARE(p.feed(L("End of restoring")).percent, 99);
    }

    void aProgramRunSpreadsTheWorkOverTheRangesItCompares() {
        // Real lines of a 300 KB write: three sectors of 128 KB.
        JLinkLogProgress p;
        p.reset(false, 0);
        p.feed(L("Start of preparing flash programming"));
        QCOMPARE(p.feed(L("End of preparing flash programming")).percent, 10);

        QCOMPARE(p.feed(L("Comparing range 0x8020000 - 0x803FFFF (1 Sector, 128 KB), using multi-block CRC calculation")).text,
                 QString("Comparing 12%"));
        p.feed(L("Comparing range 0x8040000 - 0x805FFFF (1 Sector, 128 KB), using multi-block CRC calculation"));
        p.feed(L("Comparing range 0x8060000 - 0x807FFFF (1 Sector, 128 KB), using multi-block CRC calculation"));
        QCOMPARE(p.feed(L("End of comparing flash")).percent, 15);

        // This run erases inside the programming: its erase markers move nothing.
        QCOMPARE(p.feed(L("Start of erasing sectors")).percent, 15);
        QCOMPARE(p.feed(L("End of erasing sectors")).percent, 15);

        QCOMPARE(p.feed(L("Start of flash programming")).percent, 15);
        auto u = p.feed(L("Programming range 0x08020000 - 0x0803FFFF (  1 Sector, 128 KB)"));
        QCOMPARE(u.text, QString("Programming 15%"));
        u = p.feed(L("Programming range 0x08040000 - 0x0805FFFF (  1 Sector, 128 KB)"));
        QCOMPARE(u.percent, 15 + 80 * 128 / 384);  // one of three sectors written
        u = p.feed(L("Programming range 0x08060000 - 0x0807FFFF (  1 Sector, 128 KB)"));
        QCOMPARE(u.percent, 15 + 80 * 256 / 384);
        QCOMPARE(p.feed(L("End of flash programming")).percent, 95);
        QCOMPARE(p.feed(L("End of restoring")).percent, 99);
    }

    void thePercentageNeverGoesBackOrReachesOneHundred() {
        JLinkLogProgress p;
        p.reset(true, 100);
        p.feed(L("End of preparing flash programming"));  // 10
        p.feed(L("Start of erasing sectors"));
        p.feed(L("Erasing range 0x0 - 0xFF (1 Sector, 1 KB)"));
        // The announced area was far too small: more is erased than the total says.
        auto u = p.feed(L("Erasing range 0x400 - 0x7FF (1 Sector, 64 KB)"));
        QVERIFY(u.percent <= 95);
        u = p.feed(L("Erasing range 0x800 - 0xBFF (1 Sector, 64 KB)"));
        QVERIFY(u.percent <= 95);
        // An earlier phase marker after a later one does not move the bar back.
        QVERIFY(p.feed(L("Start of preparing flash programming")).percent >= 10);
        QVERIFY(p.feed(L("End of restoring")).percent <= 99);
    }

    void withoutAKnownTotalTheBarStaysAtTheStartOfThePhase() {
        JLinkLogProgress p;
        p.reset(true, 0);  // the size of the area is not known
        p.feed(L("Start of erasing sectors"));
        auto u = p.feed(L("Erasing range 0x08000000 - 0x0800FFFF (  4 Sectors, 64 KB)"));
        u = p.feed(L("Erasing range 0x08010000 - 0x0801FFFF (  1 Sector, 64 KB)"));
        QCOMPARE(u.percent, 10);
    }

    void anUnchangedPercentIsNotReportedAgain() {
        JLinkLogProgress p;
        p.reset(false, 0);
        QVERIFY(p.feed(L("Start of preparing flash programming")).changed);
        QVERIFY(!p.feed(L("Start of preparing flash programming")).changed);
    }

    void resetForgetsEverything() {
        JLinkLogProgress p;
        p.reset(true, 1000);
        p.feed(L("End of restoring"));
        p.reset(true, 1000);
        QCOMPARE(p.feed(L("Start of preparing flash programming")).percent, 2);
    }
};

QTEST_APPLESS_MAIN(TestJLinkLogProgress)
#include "tst_jlinklogprogress.moc"
