#include "core/ProfileTransfer.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

namespace {
Profile sample() {
    Profile p;
    p.manufacturer = "APM32 / Geehy";
    p.chip = "APM32F407VG";
    p.interfaceName = "SWD";
    p.speedKhz = 4000;
    p.slot1Path = "C:/work/fw/boot.hex";
    p.slot2Path = "C:/work/fw/app.hex";
    p.slot1Address = "0x08000000";
    p.slot2Address = "0x08008000";
    p.firmware = FirmwareChoice::Both;
    p.eraseBeforeFlash = false;
    p.verifyAfterFlash = true;
    p.resetAfterFlash = false;
    p.connectUnderReset = true;
    return p;
}

// A file with one profile, with `field` set to `value` (a null value removes the field).
QByteArray fileWith(const QString& field, const QJsonValue& value) {
    QJsonObject profile{{"name", "x"}, {"chip", "STM32F407VG"}};
    if (!field.isEmpty()) {
        if (value.isNull()) profile.remove(field);
        else profile[field] = value;
    }
    QJsonObject root{{"format", "coldhex-profiles"}, {"version", 1}, {"profiles", QJsonArray{profile}}};
    return QJsonDocument(root).toJson();
}
}

class TestProfileTransfer : public QObject {
    Q_OBJECT

private slots:
    void roundTripsEveryField() {
        const QByteArray json = ProfileTransfer::toJson({{"board", sample()}}, "1.0.0", true, QString());
        const ImportResult r = ProfileTransfer::fromJson(json, QString());
        QVERIFY(r.ok());
        QVERIFY(r.skipped.isEmpty());
        QCOMPARE(r.profiles.size(), 1);
        QCOMPARE(r.profiles[0].name, QString("board"));
        QVERIFY(r.profiles[0].profile == sample());
    }

    void keepsSeveralProfilesInOrder() {
        Profile a = sample(); a.chip = "A";
        Profile b = sample(); b.chip = "B";
        const ImportResult r = ProfileTransfer::fromJson(ProfileTransfer::toJson({{"one", a}, {"two", b}}, "1.0.0", false, QString()), QString());
        QVERIFY(r.ok());
        QCOMPARE(r.profiles.size(), 2);
        QCOMPARE(r.profiles[0].name, QString("one"));
        QCOMPARE(r.profiles[1].profile.chip, QString("B"));
    }

    void leavesOutFilePathsUnlessAsked() {
        const ImportResult r = ProfileTransfer::fromJson(ProfileTransfer::toJson({{"board", sample()}}, "1.0.0", false, QString()), QString());
        QVERIFY(r.ok());
        QVERIFY(r.profiles[0].profile.slot1Path.isEmpty());
        QVERIFY(r.profiles[0].profile.slot2Path.isEmpty());
        QCOMPARE(r.profiles[0].profile.slot1Address, QString("0x08000000"));
    }

    void writesPathsBesideTheFileRelativeAndReadsThemBack() {
        QTemporaryDir project;     // the folder the profile file is exported to
        QTemporaryDir elsewhere;   // a firmware file somewhere else
        QVERIFY(project.isValid() && elsewhere.isValid());
        Profile p = sample();
        p.slot1Path = QDir(project.path()).filePath("out/boot.hex");
        p.slot2Path = QDir(elsewhere.path()).filePath("app.hex");

        const QByteArray json = ProfileTransfer::toJson({{"board", p}}, "1.0.0", true, project.path());
        QVERIFY(json.contains("\"out/boot.hex\""));
        QVERIFY(json.contains(QDir::fromNativeSeparators(p.slot2Path).toUtf8()));

        // Read on another computer, where the folder is somewhere else.
        QTemporaryDir shared;
        const ImportResult r = ProfileTransfer::fromJson(json, shared.path());
        QVERIFY(r.ok());
        QCOMPARE(r.profiles[0].profile.slot1Path, QDir::cleanPath(shared.path() + "/out/boot.hex"));
        QCOMPARE(r.profiles[0].profile.slot2Path, QDir::fromNativeSeparators(p.slot2Path));
    }
    void normalisesAddresses() {
        QJsonObject p{{"name", "x"}, {"chip", "C"}, {"slot1Address", "8000000"}, {"slot2Address", "0X0800abcd"}};
        QJsonObject root{{"format", "coldhex-profiles"}, {"version", 1}, {"profiles", QJsonArray{p}}};
        const ImportResult r = ProfileTransfer::fromJson(QJsonDocument(root).toJson(), QString());
        QVERIFY(r.ok());
        QCOMPARE(r.profiles[0].profile.slot1Address, QString("0x08000000"));
        QCOMPARE(r.profiles[0].profile.slot2Address, QString("0x0800ABCD"));
    }

    void missingOptionalFieldsGetTheDefaults() {
        const ImportResult r = ProfileTransfer::fromJson(fileWith(QString(), QJsonValue()), QString());
        QVERIFY(r.ok());
        const Profile& p = r.profiles[0].profile;
        QCOMPARE(p.interfaceName, QString("SWD"));
        QCOMPARE(p.speedKhz, Profile().speedKhz);
        QVERIFY(p.firmware == FirmwareChoice::File2);
        QVERIFY(p.eraseBeforeFlash && p.verifyAfterFlash && p.resetAfterFlash);
        QVERIFY(!p.connectUnderReset);
    }

    void ignoresUnknownFields() {
        const ImportResult r = ProfileTransfer::fromJson(fileWith("somethingNew", "hello"), QString());
        QVERIFY(r.ok());
        QCOMPARE(r.profiles.size(), 1);
    }

    void refusesWhatIsNotAProfileFile() {
        QVERIFY(!ProfileTransfer::fromJson("not json at all", QString()).ok());
        QVERIFY(!ProfileTransfer::fromJson("[1,2,3]", QString()).ok());
        QVERIFY(!ProfileTransfer::fromJson("{\"format\":\"something-else\",\"version\":1,\"profiles\":[]}", QString()).ok());
        QVERIFY(!ProfileTransfer::fromJson("{\"format\":\"coldhex-profiles\",\"version\":1,\"profiles\":[]}", QString()).ok());
        QVERIFY(!ProfileTransfer::fromJson("{\"format\":\"coldhex-profiles\",\"version\":1}", QString()).ok());
        QVERIFY(!ProfileTransfer::fromJson("{\"format\":\"coldhex-profiles\",\"profiles\":[{}]}", QString()).ok());
    }

    void refusesAFileFromANewerFormat() {
        const ImportResult r = ProfileTransfer::fromJson("{\"format\":\"coldhex-profiles\",\"version\":99,\"profiles\":[{\"name\":\"x\",\"chip\":\"c\"}]}", QString());
        QVERIFY(!r.ok());
        QVERIFY(r.error.contains("newer"));
    }

    void refusesAHugeFile() {
        QVERIFY(!ProfileTransfer::fromJson(QByteArray(ProfileTransfer::kMaxFileBytes + 1, ' '), QString()).ok());
    }

    void skipsABadProfileButKeepsTheGoodOnes() {
        QJsonArray list{
            QJsonObject{{"name", "good"}, {"chip", "C1"}},
            QJsonObject{{"name", "a/b"}, {"chip", "C2"}},          // name not allowed
            QJsonObject{{"name", "nochip"}},                       // no chip
            QJsonObject{{"name", "badaddr"}, {"chip", "C3"}, {"slot1Address", "xyz"}},
            QJsonObject{{"name", "good"}, {"chip", "C4"}},         // same name again
            QJsonObject{{"name", "GOOD"}, {"chip", "C5"}},         // same name, other capitals
            QJsonObject{{"name", "fine"}, {"chip", "C6"}},
            QJsonValue(5),                                         // not a profile
        };
        QJsonObject root{{"format", "coldhex-profiles"}, {"version", 1}, {"profiles", list}};
        const ImportResult r = ProfileTransfer::fromJson(QJsonDocument(root).toJson(), QString());
        QVERIFY(r.ok());
        QCOMPARE(r.profiles.size(), 2);
        QCOMPARE(r.profiles[0].name, QString("good"));
        QCOMPARE(r.profiles[0].profile.chip, QString("C1"));
        QCOMPARE(r.profiles[1].name, QString("fine"));
        QCOMPARE(r.skipped.size(), 6);
    }

    void refusesWrongTypesAndOutOfRangeValues_data() {
        QTest::addColumn<QString>("field");
        QTest::addColumn<QJsonValue>("value");
        QTest::newRow("speed as text") << "speedKhz" << QJsonValue("fast");
        QTest::newRow("speed zero") << "speedKhz" << QJsonValue(0);
        QTest::newRow("speed negative") << "speedKhz" << QJsonValue(-5);
        QTest::newRow("speed huge") << "speedKhz" << QJsonValue(5000000);
        QTest::newRow("speed fraction") << "speedKhz" << QJsonValue(1.5);
        QTest::newRow("interface") << "interface" << QJsonValue("I2C");
        QTest::newRow("firmware") << "firmware" << QJsonValue("everything");
        QTest::newRow("flag as text") << "eraseBeforeFlash" << QJsonValue("yes");
        QTest::newRow("path as number") << "slot1Path" << QJsonValue(7);
        QTest::newRow("path with control character") << "slot1Path" << QJsonValue(QString("a\nb"));
        QTest::newRow("path too long") << "slot2Path" << QJsonValue(QString(2000, 'a'));
        QTest::newRow("chip too long") << "chip" << QJsonValue(QString(500, 'c'));
    }

    void refusesWrongTypesAndOutOfRangeValues() {
        QFETCH(QString, field);
        QFETCH(QJsonValue, value);
        QVERIFY(!ProfileTransfer::fromJson(fileWith(field, value), QString()).ok());
    }

    void acceptsJtagInAnyCapitalisation() {
        const ImportResult r = ProfileTransfer::fromJson(fileWith("interface", "jtag"), QString());
        QVERIFY(r.ok());
        QCOMPARE(r.profiles[0].profile.interfaceName, QString("JTAG"));
    }

    void makesNamesUnique() {
        QCOMPARE(ProfileTransfer::uniqueName("board", {}), QString("board"));
        QCOMPARE(ProfileTransfer::uniqueName("board", {"other"}), QString("board"));
        QCOMPARE(ProfileTransfer::uniqueName("board", {"Board"}), QString("board (2)"));
        QCOMPARE(ProfileTransfer::uniqueName("board", {"board", "board (2)", "BOARD (3)"}), QString("board (4)"));
    }
};

QTEST_GUILESS_MAIN(TestProfileTransfer)
#include "tst_profiletransfer.moc"
