#include "core/SlotValidator.h"
#include <QtTest>

class TestSlotValidator : public QObject {
    Q_OBJECT

private slots:
    void parsesAddressForms() {
        uint32_t v = 0;
        QVERIFY(SlotValidator::parseAddress("0x08000000", v));
        QCOMPARE(v, 0x08000000u);
        QVERIFY(SlotValidator::parseAddress("0X8008000", v));
        QCOMPARE(v, 0x08008000u);
        QVERIFY(SlotValidator::parseAddress("  08004000 ", v));
        QCOMPARE(v, 0x08004000u);
        QVERIFY(SlotValidator::parseAddress("0xFFFFFFFF", v));
        QCOMPARE(v, 0xFFFFFFFFu);
    }

    void rejectsBadAddresses() {
        uint32_t v = 0;
        QVERIFY(!SlotValidator::parseAddress("", v));
        QVERIFY(!SlotValidator::parseAddress("0x", v));
        QVERIFY(!SlotValidator::parseAddress("0xZZ", v));
        QVERIFY(!SlotValidator::parseAddress("0x100000000", v));  // exceeds 32 bits
        QVERIFY(!SlotValidator::parseAddress("-1", v));
    }

    void detectsOverlap() {
        QVERIFY(SlotValidator::overlaps({0x08000000, 0x9000}, {0x08008000, 0x1000}));
        QVERIFY(SlotValidator::overlaps({0x08008000, 0x1000}, {0x08000000, 0x9000}));
        QVERIFY(SlotValidator::overlaps({0x1000, 0x1000}, {0x1000, 0x1000}));
    }

    void adjacentRangesDoNotOverlap() {
        QVERIFY(!SlotValidator::overlaps({0x08000000, 0x8000}, {0x08008000, 0x1000}));
        QVERIFY(!SlotValidator::overlaps({0x08008000, 0x1000}, {0x08000000, 0x8000}));
    }

    void emptyRangeNeverOverlaps() {
        QVERIFY(!SlotValidator::overlaps({0x08000000, 0}, {0x08000000, 0x1000}));
    }

    void rangeAtTopOfAddressSpaceDoesNotWrap() {
        QVERIFY(SlotValidator::overlaps({0xFFFFFF00, 0x100}, {0xFFFFFF80, 0x10}));
        QVERIFY(!SlotValidator::overlaps({0xFFFFFF00, 0x100}, {0x00000000, 0x100}));
    }
};

QTEST_GUILESS_MAIN(TestSlotValidator)
#include "tst_slotvalidator.moc"
