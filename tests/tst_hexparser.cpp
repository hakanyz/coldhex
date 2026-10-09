#include "core/HexParser.h"
#include <QTemporaryFile>
#include <QtTest>

class TestHexParser : public QObject {
    Q_OBJECT

private:
    static void writeTemp(QTemporaryFile& f, const QByteArray& content) {
        QVERIFY(f.open());
        f.write(content);
        f.flush();
    }

private slots:
    void detectsFormatFromExtension() {
        QCOMPARE(HexParser::detectFormat("a.hex"), FileFormat::IntelHex);
        QCOMPARE(HexParser::detectFormat("a.BIN"), FileFormat::RawBinary);
        QCOMPARE(HexParser::detectFormat("a.elf"), FileFormat::Elf);
        QCOMPARE(HexParser::detectFormat("a.txt"), FileFormat::AutoDetect);
    }

    void parsesSimpleHex() {
        QTemporaryFile f;
        writeTemp(f, ":0400000001020304F2\n:00000001FF\n");
        auto r = HexParser::analyzeHexFile(f.fileName());
        QVERIFY(r.success);
        QCOMPARE(r.baseAddress, 0u);
        QCOMPARE(r.size, 4u);
        QCOMPARE(r.data, QByteArray("\x01\x02\x03\x04", 4));
    }

    void appliesExtendedLinearAddress() {
        QTemporaryFile f;
        writeTemp(f, ":020000040800F2\n:0200100000FFEF\n:00000001FF\n");
        auto r = HexParser::analyzeHexFile(f.fileName());
        QVERIFY(r.success);
        QCOMPARE(r.baseAddress, 0x08000010u);
        QCOMPARE(r.size, 2u);
    }

    void fillsGapsWithErasedState() {
        QTemporaryFile f;
        writeTemp(f, ":01000000AA55\n:01000300BB41\n:00000001FF\n");
        auto r = HexParser::analyzeHexFile(f.fileName());
        QVERIFY(r.success);
        QCOMPARE(r.size, 4u);
        QCOMPARE(r.data, QByteArray("\xAA\xFF\xFF\xBB", 4));
    }

    void sizeOnlyModeSkipsData() {
        QTemporaryFile f;
        writeTemp(f, ":0400000001020304F2\n:00000001FF\n");
        auto r = HexParser::analyzeHexFile(f.fileName(), false);
        QVERIFY(r.success);
        QCOMPARE(r.size, 4u);
        QVERIFY(r.data.isEmpty());
    }

    void rejectsLineWithoutColon() {
        QTemporaryFile f;
        writeTemp(f, "garbage\n");
        auto r = HexParser::analyzeHexFile(f.fileName());
        QVERIFY(!r.success);
        QVERIFY(!r.errorMessage.isEmpty());
    }

    void rejectsChecksumMismatch() {
        QTemporaryFile f;
        writeTemp(f, ":0400000001020304F3\n:00000001FF\n");  // correct checksum is F2
        auto r = HexParser::analyzeHexFile(f.fileName());
        QVERIFY(!r.success);
        QVERIFY(r.errorMessage.contains("Checksum"));
        QVERIFY(r.errorMessage.contains("Line 1"));
    }

    void rejectsRecordLengthMismatch() {
        QTemporaryFile f;
        writeTemp(f, ":0400000001020304F2\n:0400000001020F2\n:00000001FF\n");
        auto r = HexParser::analyzeHexFile(f.fileName());
        QVERIFY(!r.success);
        QVERIFY(r.errorMessage.contains("Line 2"));
    }

    void rejectsNonHexCharacters() {
        QTemporaryFile f;
        writeTemp(f, ":0100000ZAA55\n:00000001FF\n");
        QVERIFY(!HexParser::analyzeHexFile(f.fileName()).success);
    }

    void acceptsCrLfLineEndings() {
        QTemporaryFile f;
        writeTemp(f, ":0400000001020304F2\r\n:00000001FF\r\n");
        QVERIFY(HexParser::analyzeHexFile(f.fileName()).success);
    }

    void rejectsHexWithoutDataRecords() {
        QTemporaryFile f;
        writeTemp(f, ":00000001FF\n");
        QVERIFY(!HexParser::analyzeHexFile(f.fileName()).success);
    }

    void failsOnMissingFile() {
        QVERIFY(!HexParser::analyzeHexFile("does/not/exist.hex").success);
        QVERIFY(!HexParser::analyzeBinFile("does/not/exist.bin").success);
    }

    void readsBinaryWithGivenBase() {
        QTemporaryFile f;
        writeTemp(f, QByteArray("\x10\x20\x30", 3));
        auto r = HexParser::analyzeBinFile(f.fileName(), 0x08004000);
        QVERIFY(r.success);
        QCOMPARE(r.baseAddress, 0x08004000u);
        QCOMPARE(r.size, 3u);
    }
};

QTEST_GUILESS_MAIN(TestHexParser)
#include "tst_hexparser.moc"
