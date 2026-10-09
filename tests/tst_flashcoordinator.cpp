#include "MockEngine.h"
#include "core/FlashCoordinator.h"
#include <QSignalSpy>
#include <QtTest>

class TestFlashCoordinator : public QObject {
    Q_OBJECT

private:
    static FirmwareSlot makeSlot(const QString& path, uint32_t addr) {
        FirmwareSlot s;
        s.enabled = true;
        s.label = path;
        s.filePath = path;
        s.startAddress = addr;
        return s;
    }

    static TargetConfig makeConfig(uint32_t speed = 4000, bool autoSpeed = true) {
        TargetConfig c;
        c.chipName = "TestChip";
        c.speedKhz = speed;
        c.autoSpeedEnabled = autoSpeed;
        return c;
    }

    static void setup(FlashCoordinator& coord, const TargetConfig& cfg) {
        coord.setTargetConfig(cfg);
        coord.setSlot1(makeSlot("boot.hex", 0x08000000));
        coord.setSlot2(makeSlot("app.hex", 0x08008000));
    }

private slots:
    void fullSequenceRunsInOrder() {
        MockEngine engine;
        FlashCoordinator coord(&engine);
        setup(coord, makeConfig());
        QSignalSpy finished(&coord, &FlashCoordinator::sequenceFinished);

        coord.startFlashSequence();
        for (int i = 0; i < 5; ++i) engine.finish(true);

        QCOMPARE(engine.commands, (QStringList{
            "connect@4000", "erase",
            "program:boot.hex@0x8000000:verify",
            "program:app.hex@0x8008000:verify",
            "reset"}));
        QCOMPARE(finished.count(), 1);
        QVERIFY(finished.first().at(0).toBool());
    }

    void fallbackResumesFromFailedStep() {
        MockEngine engine;
        FlashCoordinator coord(&engine);
        setup(coord, makeConfig(4000));
        QSignalSpy fallback(&coord, &FlashCoordinator::autoSpeedFallbackTriggered);
        QSignalSpy finished(&coord, &FlashCoordinator::sequenceFinished);

        coord.startFlashSequence();
        engine.finish(true);   // connect
        engine.finish(true);   // erase
        engine.finish(false);  // slot 1 fails -> speed drops, reconnect, retry slot 1
        QCOMPARE(fallback.count(), 1);
        QCOMPARE(fallback.first().at(0).toUInt(), 2000u);

        for (int i = 0; i < 4; ++i) engine.finish(true);  // connect, slot1, slot2, reset

        QCOMPARE(engine.commands, (QStringList{
            "connect@4000", "erase",
            "program:boot.hex@0x8000000:verify",
            "connect@2000",
            "program:boot.hex@0x8000000:verify",
            "program:app.hex@0x8008000:verify",
            "reset"}));
        QCOMPARE(engine.commands.count("erase"), 1);
        QCOMPARE(finished.count(), 1);
        QVERIFY(finished.first().at(0).toBool());
    }

    void connectFailureRetriesConnectAtLowerSpeed() {
        MockEngine engine;
        FlashCoordinator coord(&engine);
        setup(coord, makeConfig(4000));

        coord.startFlashSequence();
        engine.finish(false);  // connect@4000 fails
        QCOMPARE(engine.commands.last(), QString("connect@2000"));
    }

    void failureWithoutAutoSpeedStopsSequence() {
        MockEngine engine;
        FlashCoordinator coord(&engine);
        setup(coord, makeConfig(4000, false));
        QSignalSpy fallback(&coord, &FlashCoordinator::autoSpeedFallbackTriggered);
        QSignalSpy finished(&coord, &FlashCoordinator::sequenceFinished);

        coord.startFlashSequence();
        engine.finish(true);   // connect
        engine.finish(false);  // erase fails

        QCOMPARE(fallback.count(), 0);
        QCOMPARE(finished.count(), 1);
        QVERIFY(!finished.first().at(0).toBool());
    }

    void failureAtLowestSpeedStopsSequence() {
        MockEngine engine;
        FlashCoordinator coord(&engine);
        setup(coord, makeConfig(100));
        QSignalSpy finished(&coord, &FlashCoordinator::sequenceFinished);

        coord.startFlashSequence();
        engine.finish(false);

        QCOMPARE(finished.count(), 1);
        QVERIFY(!finished.first().at(0).toBool());
    }

    void speedFallsThroughWholeTable() {
        MockEngine engine;
        FlashCoordinator coord(&engine);
        setup(coord, makeConfig(10000));
        QSignalSpy finished(&coord, &FlashCoordinator::sequenceFinished);

        coord.startFlashSequence();
        for (int i = 0; i < 10 && finished.isEmpty(); ++i) engine.finish(false);

        QCOMPARE(engine.commands, (QStringList{
            "connect@10000", "connect@4000", "connect@2000",
            "connect@1000", "connect@500", "connect@100"}));
        QCOMPARE(finished.count(), 1);
    }

    void cancelEndsSequenceOnceWithoutFallback() {
        MockEngine engine;
        FlashCoordinator coord(&engine);
        setup(coord, makeConfig());
        QSignalSpy fallback(&coord, &FlashCoordinator::autoSpeedFallbackTriggered);
        QSignalSpy finished(&coord, &FlashCoordinator::sequenceFinished);

        coord.startFlashSequence();
        coord.cancelSequence();
        engine.finish(false);  // late "cancelled" notification from the engine

        QCOMPARE(fallback.count(), 0);
        QCOMPARE(finished.count(), 1);
        QVERIFY(!finished.first().at(0).toBool());
    }

    void rejectsEmptyChipName() {
        MockEngine engine;
        FlashCoordinator coord(&engine);
        TargetConfig cfg = makeConfig();
        cfg.chipName.clear();
        setup(coord, cfg);
        QSignalSpy finished(&coord, &FlashCoordinator::sequenceFinished);

        coord.startFlashSequence();

        QCOMPARE(finished.count(), 1);
        QVERIFY(!finished.first().at(0).toBool());
        QVERIFY(engine.commands.isEmpty());
    }

    void rejectsWhenNoSlotEnabled() {
        MockEngine engine;
        FlashCoordinator coord(&engine);
        coord.setTargetConfig(makeConfig());
        QSignalSpy finished(&coord, &FlashCoordinator::sequenceFinished);

        coord.startFlashSequence();

        QCOMPARE(finished.count(), 1);
        QVERIFY(!finished.first().at(0).toBool());
    }
};

QTEST_GUILESS_MAIN(TestFlashCoordinator)
#include "tst_flashcoordinator.moc"
