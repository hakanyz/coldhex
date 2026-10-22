#include "core/ProfileStore.h"
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

class TestProfileStore : public QObject {
    Q_OBJECT

private slots:
    void emptyStoreHasNoProfiles() {
        QTemporaryDir dir;
        QSettings s(dir.filePath("p.ini"), QSettings::IniFormat);
        QVERIFY(ProfileStore::names(s).isEmpty());
        Profile p;
        QVERIFY(!ProfileStore::load(s, "missing", p));
    }

    void roundTripsAllValues() {
        QTemporaryDir dir;
        QSettings s(dir.filePath("p.ini"), QSettings::IniFormat);

        Profile in;
        in.manufacturer = "APM32";
        in.chip = "APM32F103CB";
        in.interfaceName = "SWD";
        in.speedKhz = 2000;
        in.slot1Path = "C:/fw/boot.hex";
        in.slot2Path = "C:/fw/app.bin";
        in.slot1Address = "0x08000000";
        in.slot2Address = "0x08008000";
        in.firmware = FirmwareChoice::Both;
        in.eraseBeforeFlash = false;
        in.verifyAfterFlash = false;
        in.resetAfterFlash = false;
        in.connectUnderReset = true;
        ProfileStore::save(s, "APM32 board", in);

        Profile out;
        QVERIFY(ProfileStore::load(s, "APM32 board", out));
        QCOMPARE(out.manufacturer, in.manufacturer);
        QCOMPARE(out.chip, in.chip);
        QCOMPARE(out.interfaceName, in.interfaceName);
        QCOMPARE(out.speedKhz, in.speedKhz);
        QCOMPARE(out.slot1Path, in.slot1Path);
        QCOMPARE(out.slot2Path, in.slot2Path);
        QCOMPARE(out.slot1Address, in.slot1Address);
        QCOMPARE(out.slot2Address, in.slot2Address);
        QVERIFY(out.firmware == FirmwareChoice::Both);
        QCOMPARE(out.eraseBeforeFlash, false);
        QCOMPARE(out.verifyAfterFlash, false);
        QCOMPARE(out.resetAfterFlash, false);
        QCOMPARE(out.connectUnderReset, true);
    }

    void everyFirmwareChoiceSurvives() {
        QTemporaryDir dir;
        QSettings s(dir.filePath("p.ini"), QSettings::IniFormat);
        for (FirmwareChoice c : {FirmwareChoice::File1, FirmwareChoice::File2, FirmwareChoice::Both}) {
            Profile in;
            in.firmware = c;
            ProfileStore::save(s, "x", in);
            Profile out;
            QVERIFY(ProfileStore::load(s, "x", out));
            QVERIFY(out.firmware == c);
        }
    }

    void namesAreSortedAndProfilesAreIndependent() {
        QTemporaryDir dir;
        QSettings s(dir.filePath("p.ini"), QSettings::IniFormat);
        Profile a;
        a.chip = "A";
        Profile b;
        b.chip = "B";
        ProfileStore::save(s, "stm32 board", b);
        ProfileStore::save(s, "APM32 board", a);

        QCOMPARE(ProfileStore::names(s), (QStringList{"APM32 board", "stm32 board"}));

        Profile out;
        QVERIFY(ProfileStore::load(s, "stm32 board", out));
        QCOMPARE(out.chip, QString("B"));
    }

    void savingAgainReplacesTheProfile() {
        QTemporaryDir dir;
        QSettings s(dir.filePath("p.ini"), QSettings::IniFormat);
        Profile p;
        p.chip = "old";
        ProfileStore::save(s, "x", p);
        p.chip = "new";
        ProfileStore::save(s, "x", p);

        QCOMPARE(ProfileStore::names(s).size(), 1);
        Profile out;
        QVERIFY(ProfileStore::load(s, "x", out));
        QCOMPARE(out.chip, QString("new"));
    }

    void removeDeletesOnlyThatProfile() {
        QTemporaryDir dir;
        QSettings s(dir.filePath("p.ini"), QSettings::IniFormat);
        ProfileStore::save(s, "a", Profile());
        ProfileStore::save(s, "b", Profile());
        ProfileStore::remove(s, "a");
        QCOMPARE(ProfileStore::names(s), (QStringList{"b"}));
    }

    void validatesNames() {
        QVERIFY(ProfileStore::isValidName("APM32 board"));
        QVERIFY(!ProfileStore::isValidName(""));
        QVERIFY(!ProfileStore::isValidName("   "));
        QVERIFY(!ProfileStore::isValidName("a/b"));
        QVERIFY(!ProfileStore::isValidName("a\\b"));
    }
};

QTEST_GUILESS_MAIN(TestProfileStore)
#include "tst_profilestore.moc"
