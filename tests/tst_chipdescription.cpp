#include "engine/ChipDescription.h"
#include <QTemporaryFile>
#include <QtTest>

namespace {

// Same layout target-gen produces (see probe-rs/targets/*.yaml): variants list first,
// then a top-level flash_algorithms list whose entries also start with "- name:".
const char* kSampleYaml =
    "name: APM32F1 Series\n"
    "generated_from_pack: true\n"
    "pack_file_release: 1.2.1\n"
    "variants:\n"
    "- name: APM32F103C8\n"
    "  cores:\n"
    "  - name: main\n"
    "    type: armv7m\n"
    "  memory_map:\n"
    "  - !Nvm\n"
    "    name: IROM1\n"
    "    range:\n"
    "      start: 0x8000000\n"
    "      end: 0x8010000\n"
    "  flash_algorithms:\n"
    "  - apm32f10x_128\n"
    "- name: APM32F103CB\n"
    "  cores:\n"
    "  - name: main\n"
    "    type: armv7m\n"
    "  flash_algorithms:\n"
    "  - apm32f10x_128\n"
    "flash_algorithms:\n"
    "- name: apm32f10x_128\n"
    "  description: APM32F10x 128kB Flash\n"
    "  default: true\n";

}  // namespace

class TestChipDescription : public QObject {
    Q_OBJECT

private slots:
    void readsFamilyName() {
        QCOMPARE(ChipDescription::familyName(kSampleYaml), QString("APM32F1 Series"));
    }

    void listsOnlyVariantNames() {
        QCOMPARE(ChipDescription::variantNames(kSampleYaml), (QStringList{"APM32F103C8", "APM32F103CB"}));
    }

    void handlesIndentedVariantList() {
        const QString yaml =
            "name: Test Series\n"
            "variants:\n"
            "  - name: CHIP_A\n"
            "    cores:\n"
            "    - name: main\n"
            "  - name: \"CHIP_B\"\n"
            "flash_algorithms:\n"
            "  - name: algo\n";
        QCOMPARE(ChipDescription::variantNames(yaml), (QStringList{"CHIP_A", "CHIP_B"}));
    }

    void handlesWindowsLineEndings() {
        QString yaml = QString(kSampleYaml).replace("\n", "\r\n");
        QCOMPARE(ChipDescription::variantNames(yaml), (QStringList{"APM32F103C8", "APM32F103CB"}));
    }

    void returnsNothingWithoutVariants() {
        QVERIFY(ChipDescription::variantNames("").isEmpty());
        QVERIFY(ChipDescription::variantNames("name: x\nflash_algorithms:\n- name: a\n").isEmpty());
    }

    void readsFromFile() {
        QTemporaryFile f;
        QVERIFY(f.open());
        f.write(kSampleYaml);
        f.flush();

        QString family;
        QString error;
        const QStringList names = ChipDescription::variantNamesFromFile(f.fileName(), &family, &error);
        QCOMPARE(names.size(), 2);
        QCOMPARE(family, QString("APM32F1 Series"));
        QVERIFY(error.isEmpty());
    }

    void reportsMissingAndInvalidFiles() {
        QString error;
        QVERIFY(ChipDescription::variantNamesFromFile("does/not/exist.yaml", nullptr, &error).isEmpty());
        QVERIFY(!error.isEmpty());

        QTemporaryFile f;
        QVERIFY(f.open());
        f.write("just: some\nother: yaml\n");
        f.flush();
        error.clear();
        QVERIFY(ChipDescription::variantNamesFromFile(f.fileName(), nullptr, &error).isEmpty());
        QVERIFY(error.contains("does not look like"));
    }
};

QTEST_GUILESS_MAIN(TestChipDescription)
#include "tst_chipdescription.moc"
