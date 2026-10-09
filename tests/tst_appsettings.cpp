#include "core/AppSettings.h"
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class TestAppSettings : public QObject {
    Q_OBJECT

private slots:
    void emptyStoreReportsNothingSaved() {
        QTemporaryDir dir;
        QSettings s(dir.filePath("empty.ini"), QSettings::IniFormat);
        QVERIFY(!AppSettings::load(s).hasSaved);
    }

    void roundTripsAllValues() {
        QTemporaryDir dir;
        const QString path = dir.filePath("settings.ini");

        UiSettings in;
        in.manufacturer = "GD32";
        in.chip = "GD32F303CG";
        in.probe = "J-Link";
        in.interfaceName = "SWD";
        in.speedKhz = 2000;
        in.slot1Path = "C:/fw/boot.hex";
        in.slot2Path = "C:/fw/app.bin";
        in.slot1Address = "0x08000000";
        in.slot2Address = "0x08008000";
        in.customChipFiles = QStringList{"C:/chips/APM32F1xx.yaml", "C:/chips/APM32F4.yaml"};
        in.slot1Enabled = false;
        in.slot2Enabled = true;
        in.geometry = QByteArray("\x01\x02\x03", 3);
        in.windowState = QByteArray("\x0A\x0B", 2);

        {
            QSettings s(path, QSettings::IniFormat);
            AppSettings::save(s, in);
        }

        QSettings s(path, QSettings::IniFormat);
        UiSettings out = AppSettings::load(s);
        QVERIFY(out.hasSaved);
        QCOMPARE(out.manufacturer, in.manufacturer);
        QCOMPARE(out.chip, in.chip);
        QCOMPARE(out.probe, in.probe);
        QCOMPARE(out.interfaceName, in.interfaceName);
        QCOMPARE(out.speedKhz, in.speedKhz);
        QCOMPARE(out.slot1Path, in.slot1Path);
        QCOMPARE(out.slot2Path, in.slot2Path);
        QCOMPARE(out.slot1Address, in.slot1Address);
        QCOMPARE(out.slot2Address, in.slot2Address);
        QCOMPARE(out.customChipFiles, in.customChipFiles);
        QCOMPARE(out.slot1Enabled, false);
        QCOMPARE(out.slot2Enabled, true);
        QCOMPARE(out.geometry, in.geometry);
        QCOMPARE(out.windowState, in.windowState);
    }

    void ignoresUnknownSchemaVersion() {
        QTemporaryDir dir;
        QSettings s(dir.filePath("future.ini"), QSettings::IniFormat);
        s.setValue("schemaVersion", 999);
        s.setValue("target/chip", "SomethingElse");
        QVERIFY(!AppSettings::load(s).hasSaved);
    }
};

QTEST_GUILESS_MAIN(TestAppSettings)
#include "tst_appsettings.moc"
