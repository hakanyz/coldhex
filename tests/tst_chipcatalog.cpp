#include "core/ChipCatalog.h"
#include "engine/ProbeRsEngine.h"
#include <QtTest>

class TestChipCatalog : public QObject {
    Q_OBJECT

private:
    static QList<ChipEntry> sample() {
        return ProbeRsEngine::parseChipList(
            "Available chips:\r\n"
            "GD32F30x Series\r\n"
            "    Variants:\r\n"
            "        GD32F303CG\r\n"
            "        GD32F303RC\r\n"
            "STM32F4 Series\r\n"
            "    Variants:\r\n"
            "        STM32F401RE\r\n"
            "        STM32F407VG\r\n"
            "MSPM0C Series\r\n"
            "    Variants:\r\n"
            "        MSPM0C1104\r\n");
    }

private slots:
    void parsesFamiliesAndVariants() {
        const auto chips = sample();
        QCOMPARE(chips.size(), 5);
        QCOMPARE(chips[0].family, QString("GD32F30x Series"));
        QCOMPARE(chips[0].name, QString("GD32F303CG"));
        QCOMPARE(chips[3].name, QString("STM32F407VG"));
        QCOMPARE(chips[3].family, QString("STM32F4 Series"));
    }

    void parseIgnoresHeaderAndVariantsLines() {
        const auto chips = ProbeRsEngine::parseChipList("Available chips:\n    Variants:\n");
        QVERIFY(chips.isEmpty());
        QVERIFY(ProbeRsEngine::parseChipList("").isEmpty());
    }

    void listsOnlyManufacturersThatHaveChips() {
        const QStringList m = ChipCatalog::manufacturers(sample());
        QCOMPARE(m, (QStringList{"All chips", "STM32", "GD32", "TI"}));
        QVERIFY(!m.contains("APM32 / Geehy"));
    }

    void filtersChipsByManufacturer() {
        const auto chips = sample();
        QCOMPARE(ChipCatalog::chipNames(chips, "GD32"), (QStringList{"GD32F303CG", "GD32F303RC"}));
        QCOMPARE(ChipCatalog::chipNames(chips, "All chips").size(), 5);
        QVERIFY(ChipCatalog::chipNames(chips, "Unknown").isEmpty());
    }

    void findsChipCaseInsensitively() {
        const auto chips = sample();
        QString canonical;
        QVERIFY(ChipCatalog::find(chips, " stm32f407vg ", &canonical));
        QCOMPARE(canonical, QString("STM32F407VG"));
        QVERIFY(!ChipCatalog::find(chips, "STM32F407VGT6"));
        QVERIFY(!ChipCatalog::find(chips, ""));
    }

    void customChipsFormTheirOwnGroup() {
        QList<ChipEntry> chips = sample();
        chips.append({"Custom: APM32F1 Series", "APM32F103C8", "C:/chips/apm32f1.yaml"});

        const QStringList m = ChipCatalog::manufacturers(chips);
        QVERIFY(m.contains("APM32 / Geehy"));  // by name prefix
        QVERIFY(m.contains("Custom files"));
        QCOMPARE(ChipCatalog::chipNames(chips, "Custom files"), (QStringList{"APM32F103C8"}));
        QCOMPARE(ChipCatalog::chipNames(chips, "APM32 / Geehy"), (QStringList{"APM32F103C8"}));

        const ChipEntry* entry = ChipCatalog::findEntry(chips, "apm32f103c8");
        QVERIFY(entry);
        QCOMPARE(entry->descriptionFile, QString("C:/chips/apm32f1.yaml"));
        QVERIFY(ChipCatalog::findEntry(chips, "STM32F407VG")->descriptionFile.isEmpty());
        QVERIFY(!ChipCatalog::findEntry(chips, "nothing"));
    }

    void findsManufacturerOfChip() {
        QList<ChipEntry> chips = sample();
        chips.append({"Custom: APM32F1 Series", "APM32F103C8", "C:/chips/apm32f1.yaml"});

        QCOMPARE(ChipCatalog::manufacturerOf(chips, "STM32F407VG"), QString("STM32"));
        QCOMPARE(ChipCatalog::manufacturerOf(chips, "gd32f303cg"), QString("GD32"));
        QCOMPARE(ChipCatalog::manufacturerOf(chips, "APM32F103C8"), QString("APM32 / Geehy"));
        QVERIFY(ChipCatalog::manufacturerOf(chips, "Unknown").isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestChipCatalog)
#include "tst_chipcatalog.moc"
