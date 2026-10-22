#include "core/FileWatcher.h"
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
void writeFile(const QString& path, const QByteArray& data) {
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Truncate), "cannot write test file");
    f.write(data);
}
}

class TestFileWatcher : public QObject {
    Q_OBJECT

private slots:
    void reportsChangedContent() {
        QTemporaryDir dir;
        const QString path = dir.filePath("app.hex");
        writeFile(path, "one");

        FileWatcher watcher(nullptr, 50);
        watcher.setFile(path);
        QSignalSpy spy(&watcher, &FileWatcher::changed);

        writeFile(path, "two");
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.count(), 1);
    }

    void ignoresRewriteWithSameContent() {
        QTemporaryDir dir;
        const QString path = dir.filePath("app.hex");
        writeFile(path, "same");

        FileWatcher watcher(nullptr, 50);
        watcher.setFile(path);
        QSignalSpy spy(&watcher, &FileWatcher::changed);

        writeFile(path, "same");
        QVERIFY(!spy.wait(600));
    }

    void followsFileThatIsDeletedAndRecreated() {
        QTemporaryDir dir;
        const QString path = dir.filePath("app.hex");
        writeFile(path, "one");

        FileWatcher watcher(nullptr, 50);
        watcher.setFile(path);
        QSignalSpy spy(&watcher, &FileWatcher::changed);

        QFile::remove(path);
        QTest::qWait(150);
        writeFile(path, "two");
        QVERIFY(spy.wait(3000));

        // The new file is watched too.
        writeFile(path, "three");
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.count(), 2);
    }

    void stopsWhenPathIsEmpty() {
        QTemporaryDir dir;
        const QString path = dir.filePath("app.hex");
        writeFile(path, "one");

        FileWatcher watcher(nullptr, 50);
        watcher.setFile(path);
        watcher.setFile(QString());
        QSignalSpy spy(&watcher, &FileWatcher::changed);

        writeFile(path, "two");
        QVERIFY(!spy.wait(600));
        QVERIFY(watcher.path().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestFileWatcher)
#include "tst_filewatcher.moc"
