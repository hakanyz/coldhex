#include "MockEngine.h"
#include "core/FlashCoordinator.h"
#include "ui/OperationStatus.h"
#include <QLabel>
#include <QProgressBar>
#include <QStatusBar>
#include <QtTest>

class TestOperationStatus : public QObject {
    Q_OBJECT

private slots:
    void idleShowsNothing() {
        MockEngine engine;
        FlashCoordinator coord(&engine);
        QStatusBar bar;
        OperationStatus status(&bar, &engine, &coord);

        QVERIFY(!status.isRunning());
        QVERIFY(!status.textLabel()->isVisibleTo(&bar));
        QVERIFY(!status.progressBar()->isVisibleTo(&bar));
        QVERIFY(!status.elapsedLabel()->isVisibleTo(&bar));
    }

    void singleCommandRunsBusyUntilAPercentageArrives() {
        MockEngine engine;
        FlashCoordinator coord(&engine);
        QStatusBar bar;
        OperationStatus status(&bar, &engine, &coord);

        emit engine.operationStarted("Erasing target chip (Chip Erase)...");
        QVERIFY(status.isRunning());
        QCOMPARE(status.textLabel()->text(), QString("Erasing target chip (Chip Erase)"));
        QVERIFY(status.progressBar()->isVisibleTo(&bar));
        QCOMPARE(status.progressBar()->maximum(), 0);  // busy animation
        QCOMPARE(status.elapsedLabel()->text(), QString("00:00"));

        emit engine.progressChanged(40, "40%");
        QCOMPARE(status.progressBar()->maximum(), 100);
        QCOMPARE(status.progressBar()->value(), 40);
    }

    void successFillsTheBarAndSaysDone() {
        MockEngine engine;
        FlashCoordinator coord(&engine);
        QStatusBar bar;
        OperationStatus status(&bar, &engine, &coord);

        emit engine.operationStarted("Resetting target...");
        emit engine.operationFinished(true, "ok");
        QVERIFY(!status.isRunning());
        QCOMPARE(status.textLabel()->text(), QString("Resetting target - done"));
        QCOMPARE(status.progressBar()->value(), 100);
    }

    void failureHidesTheBarAndSaysFailed() {
        MockEngine engine;
        FlashCoordinator coord(&engine);
        QStatusBar bar;
        OperationStatus status(&bar, &engine, &coord);

        emit engine.operationStarted("Connecting to target");
        emit engine.operationFinished(false, "mock failure");
        QCOMPARE(status.textLabel()->text(), QString("Connecting to target - failed"));
        QVERIFY(!status.progressBar()->isVisibleTo(&bar));
    }

    void sequenceReportsStepsAndIgnoresTheCommandsInsideIt() {
        MockEngine engine;
        FlashCoordinator coord(&engine);
        QStatusBar bar;
        OperationStatus status(&bar, &engine, &coord);

        emit coord.sequenceStarted();
        QVERIFY(status.isRunning());

        emit coord.stepChanged("Erasing chip (Chip Erase)...", 2, 4);
        QCOMPARE(status.textLabel()->text(), QString("[2/4] Erasing chip (Chip Erase)"));
        QCOMPARE(status.progressBar()->value(), 25);  // one finished step out of four

        // The commands of the steps start and finish on their own; the sequence goes on.
        emit engine.operationStarted("Erasing target chip (Chip Erase)...");
        emit engine.operationFinished(true, "ok");
        QVERIFY(status.isRunning());
        QCOMPARE(status.textLabel()->text(), QString("[2/4] Erasing chip (Chip Erase)"));

        emit coord.totalProgressChanged(60);
        QCOMPARE(status.progressBar()->value(), 60);

        emit coord.sequenceFinished(true, "All flashing and verification steps completed successfully!");
        QVERIFY(!status.isRunning());
        QCOMPARE(status.textLabel()->text(), QString("All steps - done"));
        QCOMPARE(status.progressBar()->value(), 100);
    }

    void aFailedSequenceNamesTheStep() {
        MockEngine engine;
        FlashCoordinator coord(&engine);
        QStatusBar bar;
        OperationStatus status(&bar, &engine, &coord);

        emit coord.sequenceStarted();
        emit coord.stepChanged("Flashing Slot 2: app", 3, 4);
        emit coord.sequenceFinished(false, "failed");
        QCOMPARE(status.textLabel()->text(), QString("[3/4] Flashing Slot 2: app - failed"));
        QVERIFY(!status.progressBar()->isVisibleTo(&bar));
    }

    void aNewCommandAfterASequenceIsReportedAgain() {
        MockEngine engine;
        FlashCoordinator coord(&engine);
        QStatusBar bar;
        OperationStatus status(&bar, &engine, &coord);

        emit coord.sequenceStarted();
        emit coord.sequenceFinished(true, "done");
        emit engine.operationStarted("Reading flash");
        QVERIFY(status.isRunning());
        QCOMPARE(status.textLabel()->text(), QString("Reading flash"));
    }
};

QTEST_MAIN(TestOperationStatus)
#include "tst_operationstatus.moc"
