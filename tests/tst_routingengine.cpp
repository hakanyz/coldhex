#include "MockEngine.h"
#include "engine/RoutingEngine.h"
#include <QSignalSpy>
#include <QtTest>

class TestRoutingEngine : public QObject {
    Q_OBJECT

private:
    static TargetSettings settingsFor(const QString& probe) {
        TargetSettings t;
        t.chipName = "STM32F407VG";
        t.probeId = probe;
        return t;
    }

private slots:
    void anStLinkGoesToProbeRs() {
        MockEngine probeRs, jlink;
        RoutingEngine router(&probeRs, &jlink);
        router.configureTarget(settingsFor("0483:3748"));
        router.eraseTarget();
        QCOMPARE(probeRs.commands, (QStringList{"erase"}));
        QVERIFY(jlink.commands.isEmpty());
        QVERIFY(!router.usesJLink());
    }

    void aJLinkGoesToTheJLinkEngine() {
        MockEngine probeRs, jlink;
        RoutingEngine router(&probeRs, &jlink);
        router.configureTarget(settingsFor("1366:0101:004294967295"));
        router.eraseTarget();
        QVERIFY(probeRs.commands.isEmpty());
        QCOMPARE(jlink.commands, (QStringList{"erase"}));
        QVERIFY(router.usesJLink());
        QCOMPARE(jlink.configured.size(), 1);
        QVERIFY(probeRs.configured.isEmpty());
    }

    void withoutTheJLinkSoftwareAJLinkUsesProbeRs() {
        MockEngine probeRs;
        RoutingEngine router(&probeRs, nullptr);
        router.configureTarget(settingsFor("1366:0101"));
        router.eraseTarget();
        QCOMPARE(probeRs.commands, (QStringList{"erase"}));
        QVERIFY(!router.usesJLink());
    }

    void connectingChoosesTheEngineByTheProbeToo() {
        // The flash sequence connects without configureTarget() first.
        MockEngine probeRs, jlink;
        RoutingEngine router(&probeRs, &jlink);
        router.connectTarget("STM32F407VG", 4000, "1366:0101");
        QCOMPARE(jlink.commands, (QStringList{"connect@4000"}));
        QVERIFY(probeRs.commands.isEmpty());
    }

    void everyKindOfCommandIsPassedOn() {
        MockEngine probeRs, jlink;
        RoutingEngine router(&probeRs, &jlink);
        router.configureTarget(settingsFor("1366:0101"));
        router.programFile("app.hex", 0x08000000, true);
        router.verifyFile("app.hex", 0x08000000);
        router.readMemory(0x08000000, 16, "out.bin");
        router.resetTarget();
        router.checkTargetConnection();
        QCOMPARE(jlink.commands.size(), 5);
        QVERIFY(jlink.commands.at(0).startsWith("program:app.hex"));
        QVERIFY(jlink.commands.at(1).startsWith("verify:app.hex"));
        QVERIFY(jlink.commands.at(2).startsWith("read:"));
        QCOMPARE(jlink.commands.at(3), QString("reset"));
        QCOMPARE(jlink.commands.at(4), QString("checkTarget"));
    }

    void switchingTheProbeClosesTheOldConnection() {
        MockEngine probeRs, jlink;
        RoutingEngine router(&probeRs, &jlink);
        QSignalSpy disconnected(&probeRs, &IProgrammerEngine::disconnected);

        router.connectTarget("STM32F407VG", 4000, "0483:3748");
        probeRs.finish(true);
        QVERIFY(router.isConnected());

        router.configureTarget(settingsFor("1366:0101"));  // another probe, another engine
        QVERIFY(!probeRs.isConnected());
        QVERIFY(router.usesJLink());
        QVERIFY(!router.isConnected());  // the J-Link engine is not connected
    }

    void theStateAndTheSignalsAreThoseOfTheActiveEngine() {
        MockEngine probeRs, jlink;
        RoutingEngine router(&probeRs, &jlink);
        QSignalSpy finished(&router, &IProgrammerEngine::operationFinished);
        QSignalSpy progress(&router, &IProgrammerEngine::progressChanged);

        router.configureTarget(settingsFor("1366:0101"));
        router.eraseTarget();
        QVERIFY(router.isBusy());
        emit jlink.progressChanged(40, "Erasing 40%");
        jlink.finish(true);

        QVERIFY(!router.isBusy());
        QCOMPARE(progress.size(), 1);
        QCOMPARE(progress.at(0).at(0).toInt(), 40);
        QCOMPARE(finished.size(), 1);
        QVERIFY(finished.at(0).at(0).toBool());
    }

    void theListsAlwaysComeFromProbeRs() {
        MockEngine probeRs, jlink;
        probeRs.chips << ChipEntry{"STM32F4 Series", "STM32F407VG", QString()};
        RoutingEngine router(&probeRs, &jlink);
        QSignalSpy chips(&router, &IProgrammerEngine::chipListReady);
        router.configureTarget(settingsFor("1366:0101"));  // the J-Link engine is active
        router.requestChipList();
        QCOMPARE(chips.size(), 1);
        QVERIFY(chips.at(0).at(0).toBool());
    }

    void chipDataComesFromProbeRs() {
        MockEngine probeRs, jlink;
        MemoryRegion flash;
        flash.start = 0x08000000;
        flash.size = 0x100000;
        flash.nonVolatile = true;
        probeRs.memory.regions << flash;
        RoutingEngine router(&probeRs, &jlink);
        router.configureTarget(settingsFor("1366:0101"));
        QVERIFY(router.queryMemoryMap("X", QString()).isValid());
    }
};

QTEST_GUILESS_MAIN(TestRoutingEngine)
#include "tst_routingengine.moc"
