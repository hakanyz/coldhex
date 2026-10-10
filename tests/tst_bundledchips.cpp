#include "engine/BundledChips.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {

void writeFile(const QString& path, const QByteArray& content) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content);
}

QByteArray readFile(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

}  // namespace

class TestBundledChips : public QObject {
    Q_OBJECT

private slots:
    void copiesYamlFilesOnly() {
        QTemporaryDir src, dst;
        writeFile(src.filePath("a.yaml"), "name: A\n");
        writeFile(src.filePath("b.yml"), "name: B\n");
        writeFile(src.filePath("notes.txt"), "ignore me");

        const QStringList paths = BundledChips::extract(src.path(), dst.filePath("chips"));

        QCOMPARE(paths.size(), 2);
        QCOMPARE(readFile(dst.filePath("chips/a.yaml")), QByteArray("name: A\n"));
        QVERIFY(!QFile::exists(dst.filePath("chips/notes.txt")));
    }

    void replacesChangedFilesAndKeepsSameOnes() {
        QTemporaryDir src, dst;
        writeFile(src.filePath("a.yaml"), "name: new\n");
        QVERIFY(QDir().mkpath(dst.filePath("chips")));
        writeFile(dst.filePath("chips/a.yaml"), "name: old\n");

        BundledChips::extract(src.path(), dst.filePath("chips"));
        QCOMPARE(readFile(dst.filePath("chips/a.yaml")), QByteArray("name: new\n"));

        // Second run with identical content must still report the file.
        QCOMPARE(BundledChips::extract(src.path(), dst.filePath("chips")).size(), 1);
    }

    void missingSourceGivesNothing() {
        QTemporaryDir dst;
        QVERIFY(BundledChips::extract(dst.filePath("does-not-exist"), dst.filePath("chips")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestBundledChips)
#include "tst_bundledchips.moc"
