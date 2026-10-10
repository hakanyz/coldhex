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

    // --- ELF -----------------------------------------------------------------------------

    static void put32(QByteArray& b, int offset, uint32_t v) {
        for (int i = 0; i < 4; ++i) b[offset + i] = char((v >> (8 * i)) & 0xFF);
    }
    static void put16(QByteArray& b, int offset, uint16_t v) {
        b[offset] = char(v & 0xFF);
        b[offset + 1] = char(v >> 8);
    }

    // Builds a small 32-bit little-endian ELF with the given loadable segments.
    static QByteArray makeElf(const QList<QPair<uint32_t, QByteArray>>& segments, uint32_t type = 1) {
        const int phCount = segments.size();
        QByteArray elf(52 + 32 * phCount, '\0');
        const char ident[] = {0x7f, 'E', 'L', 'F', 1, 1, 1, 0};
        for (int i = 0; i < 8; ++i) elf[i] = ident[i];
        put16(elf, 16, 2);                 // ET_EXEC
        put16(elf, 18, 0x28);              // EM_ARM
        put32(elf, 28, 52);                // program header offset
        put16(elf, 40, 52);
        put16(elf, 42, 32);
        put16(elf, 44, uint16_t(phCount));

        for (int i = 0; i < phCount; ++i) {
            const int e = 52 + 32 * i;
            const uint32_t fileOffset = uint32_t(elf.size());
            elf.append(segments[i].second);
            put32(elf, e, type);                               // p_type
            put32(elf, e + 4, fileOffset);                     // p_offset
            put32(elf, e + 8, 0x20000000);                     // p_vaddr (runtime address, ignored)
            put32(elf, e + 12, segments[i].first);             // p_paddr (load address)
            put32(elf, e + 16, uint32_t(segments[i].second.size()));
            put32(elf, e + 20, uint32_t(segments[i].second.size()));
        }
        return elf;
    }

private slots:
    void parsesElfLoadSegments() {
        const QByteArray elf = makeElf({{0x08000000, QByteArray("\x01\x02\x03\x04", 4)},
                                        {0x08000008, QByteArray("\xAA\xBB", 2)}});
        const auto r = HexParser::parseElf(elf);
        QVERIFY(r.success);
        QCOMPARE(r.baseAddress, 0x08000000u);
        QCOMPARE(r.size, 10u);
        QCOMPARE(r.data, QByteArray("\x01\x02\x03\x04\xFF\xFF\xFF\xFF\xAA\xBB", 10));
        QVERIFY(HexParser::parseElf(elf, false).data.isEmpty());
    }

    void readsElfFromFile() {
        QTemporaryFile f;
        writeTemp(f, makeElf({{0x08004000, QByteArray("\x10\x20", 2)}}));
        const auto r = HexParser::analyzeElfFile(f.fileName());
        QVERIFY(r.success);
        QCOMPARE(r.baseAddress, 0x08004000u);
        QCOMPARE(r.size, 2u);
    }

    void rejectsNonElfAndUnsupportedElf() {
        QVERIFY(!HexParser::parseElf("not an elf file at all, just text........................................").success);
        QVERIFY(!HexParser::parseElf(QByteArray()).success);

        QByteArray elf64 = makeElf({{0x08000000, QByteArray("\x01", 1)}});
        elf64[4] = 2;  // ELFCLASS64
        const auto r = HexParser::parseElf(elf64);
        QVERIFY(!r.success);
        QVERIFY(r.errorMessage.contains("32-bit"));
    }

    void rejectsElfWithoutLoadableData() {
        const QByteArray elf = makeElf({{0x08000000, QByteArray("\x01", 1)}}, 4 /* PT_NOTE */);
        QVERIFY(!HexParser::parseElf(elf).success);
    }

    void rejectsTruncatedElf() {
        QByteArray elf = makeElf({{0x08000000, QByteArray(16, 'x')}});
        elf.truncate(60);  // cuts into the program header and the data
        QVERIFY(!HexParser::parseElf(elf).success);
    }
};

QTEST_GUILESS_MAIN(TestHexParser)
#include "tst_hexparser.moc"
