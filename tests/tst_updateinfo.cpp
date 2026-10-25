#include "core/UpdateInfo.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

namespace {
const QString kBase = "https://github.com/hakanyz/coldhex/releases/download/v1.0.1/";

QJsonObject asset(const QString& name, int size, const QString& url = QString()) {
    return QJsonObject{{"name", name}, {"size", size}, {"browser_download_url", url.isEmpty() ? kBase + name : url}};
}

QByteArray release(const QString& tag = "v1.0.1", bool prerelease = false, bool draft = false) {
    QJsonObject o{
        {"tag_name", tag},
        {"draft", draft},
        {"prerelease", prerelease},
        {"html_url", "https://github.com/hakanyz/coldhex/releases/tag/" + tag},
        {"body", "Fixes things."},
        {"assets", QJsonArray{asset("coldhex-1.0.1-Linux.tar.gz", 100), asset("coldhex-1.0.1-win64.zip", 200),
                              asset("coldhex-1.0.1-win64.exe", 300), asset("coldhex_1.0.1_amd64.deb", 400),
                              asset("SHA256SUMS.txt", 5)}},
    };
    return QJsonDocument(o).toJson();
}

VersionNumber v(const char* text) { return parseVersion(text); }

QDateTime utc(const char* iso) { return QDateTime::fromString(QString(iso), Qt::ISODate); }
}
class TestUpdateInfo : public QObject {
    Q_OBJECT

private slots:
    void readsVersionNumbers() {
        QVERIFY(v("1.0.0").valid);
        QVERIFY(v("v1.2.3").valid);
        QCOMPARE(v("v1.2.3").minor, 2);
        QCOMPARE(v("1.2").patch, 0);
        QCOMPARE(v("1.0.0-beta4").pre, QString("beta4"));
        QVERIFY(v("1.0.0+build5").valid);
        QVERIFY(v("1.0.0+build5").pre.isEmpty());
        QVERIFY(!v("").valid);
        QVERIFY(!v("latest").valid);
        QVERIFY(!v("1").valid);
        QVERIFY(!v("1.x.0").valid);
        QVERIFY(!v("1.0.0.0").valid);
    }

    void ordersVersions() {
        const char* ascending[] = {"0.2.0-beta4", "0.2.0", "0.9.9", "1.0.0-beta1", "1.0.0-beta2",
                                   "1.0.0-beta10", "1.0.0-rc1", "1.0.0", "1.0.1", "1.1.0", "2.0.0"};
        const int n = sizeof(ascending) / sizeof(ascending[0]);
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                const int c = compareVersions(v(ascending[i]), v(ascending[j]));
                QVERIFY2((i < j) == (c < 0) && (i == j) == (c == 0) && (i > j) == (c > 0),
                         qPrintable(QString("%1 vs %2 gave %3").arg(ascending[i], ascending[j]).arg(c)));
            }
        }
        QCOMPARE(compareVersions(v("v1.0.0"), v("1.0.0")), 0);
    }

    void decidesWhatIsNewer() {
        QVERIFY(UpdateInfo::isNewer("v1.0.1", "1.0.0"));
        QVERIFY(UpdateInfo::isNewer("v1.0.0", "0.2.0-beta4"));
        QVERIFY(UpdateInfo::isNewer("v0.2.0", "0.2.0-beta4"));
        QVERIFY(!UpdateInfo::isNewer("v1.0.0", "1.0.0"));
        QVERIFY(!UpdateInfo::isNewer("v1.0.0", "1.0.1"));
        QVERIFY(!UpdateInfo::isNewer("v0.2.0-beta4", "0.2.0"));
        QVERIFY(!UpdateInfo::isNewer("garbage", "1.0.0"));
        QVERIFY(!UpdateInfo::isNewer("v2.0.0", "garbage"));
    }

    void picksThePackageOfThePlatform() {
        const ReleaseInfo win = UpdateInfo::parseRelease(release(), Platform::Windows);
        QVERIFY(win.valid);
        QCOMPARE(win.version, QString("1.0.1"));
        QCOMPARE(win.tag, QString("v1.0.1"));
        QCOMPARE(win.assetName, QString("coldhex-1.0.1-win64.exe"));
        QCOMPARE(win.assetSize, qint64(300));
        QVERIFY(win.checksumUrl.endsWith("SHA256SUMS.txt"));
        QVERIFY(win.pageUrl.endsWith("/tag/v1.0.1"));
        QCOMPARE(win.notes, QString("Fixes things."));

        QCOMPARE(UpdateInfo::parseRelease(release(), Platform::Linux).assetName, QString("coldhex_1.0.1_amd64.deb"));

        const ReleaseInfo other = UpdateInfo::parseRelease(release(), Platform::Other);
        QVERIFY(other.valid);
        QVERIFY(other.assetName.isEmpty());
    }

    void ignoresFilesFromAnotherPlace() {
        const QJsonObject o{{"tag_name", "v1.0.1"}, {"html_url", "https://evil.example/page"},
                            {"assets", QJsonArray{asset("coldhex-1.0.1-win64.exe", 1, "https://evil.example/coldhex-1.0.1-win64.exe")}}};
        const ReleaseInfo r = UpdateInfo::parseRelease(QJsonDocument(o).toJson(), Platform::Windows);
        QVERIFY(r.valid);
        QVERIFY(r.assetUrl.isEmpty());
        QVERIFY(r.pageUrl.isEmpty());
    }
    void doesNotOfferDraftsAndPreReleases() {
        QString why;
        QVERIFY(!UpdateInfo::parseRelease(release("v1.0.1", true), Platform::Windows, &why).valid);
        QVERIFY(!why.isEmpty());
        QVERIFY(!UpdateInfo::parseRelease(release("v1.0.1", false, true), Platform::Windows).valid);
    }

    void explainsAnswersItCannotUse() {
        QString why;
        QVERIFY(!UpdateInfo::parseRelease("not json", Platform::Windows, &why).valid);
        QVERIFY(!why.isEmpty());
        QVERIFY(!UpdateInfo::parseRelease("{\"tag_name\":\"latest\"}", Platform::Windows, &why).valid);
        QVERIFY(!UpdateInfo::parseRelease("{}", Platform::Windows, &why).valid);
        QVERIFY(!UpdateInfo::parseRelease("{\"message\":\"Not Found\"}", Platform::Windows, &why).valid);
        QVERIFY(why.contains("Not Found"));
        QVERIFY(!UpdateInfo::parseRelease("{\"message\":\"API rate limit exceeded for 1.2.3.4\"}", Platform::Windows, &why).valid);
        QVERIFY(why.contains("limiting"));
    }

    void readsChecksumFiles() {
        const QString good = QString(64, 'a');
        const QString other = QString(64, 'B');
        const QByteArray sums = QString("%1  coldhex-1.0.4-win64.exe\n%2 *coldhex_1.0.4_amd64.deb\n\ngarbage line\n%3  coldhex-1.0.4-win64.exe.sig\n")
                                    .arg(good, other, QString(64, 'c')).toUtf8();
        QCOMPARE(UpdateInfo::expectedChecksum(sums, "coldhex-1.0.4-win64.exe"), good);
        QCOMPARE(UpdateInfo::expectedChecksum(sums, "coldhex_1.0.4_amd64.deb"), other.toLower());   // star mode, lowercased
        QCOMPARE(UpdateInfo::expectedChecksum(sums, "coldhex-1.0.4-win64.zip"), QString());         // not listed
        QCOMPARE(UpdateInfo::expectedChecksum(sums, "win64.exe"), QString());                       // no partial names
        QCOMPARE(UpdateInfo::expectedChecksum("deadbeef  coldhex-1.0.4-win64.exe\n", "coldhex-1.0.4-win64.exe"), QString());  // too short
        QCOMPARE(UpdateInfo::expectedChecksum(QByteArray(), "x"), QString());
        // Windows line endings
        QCOMPARE(UpdateInfo::expectedChecksum((good + "  a.exe\r\n").toUtf8(), "a.exe"), good);
    }

    void decidesWhenTheProgramCanReplaceItself() {
        const ReleaseInfo r = UpdateInfo::parseRelease(release(), Platform::Windows);
        QVERIFY(r.valid && !r.assetUrl.isEmpty() && !r.checksumUrl.isEmpty());
        QVERIFY(UpdateInfo::canInstall(r, Platform::Windows, true));
        QVERIFY(!UpdateInfo::canInstall(r, Platform::Windows, false));   // zip copy or a build of a developer
        QVERIFY(!UpdateInfo::canInstall(r, Platform::Linux, true));
        QVERIFY(!UpdateInfo::canInstall(ReleaseInfo(), Platform::Windows, true));

        ReleaseInfo noSums = r;
        noSums.checksumUrl.clear();
        QVERIFY(!UpdateInfo::canInstall(noSums, Platform::Windows, true));   // nothing to check the download against

        ReleaseInfo noSize = r;
        noSize.assetSize = 0;
        QVERIFY(!UpdateInfo::canInstall(noSize, Platform::Windows, true));

        ReleaseInfo huge = r;
        huge.assetSize = UpdateInfo::kMaxInstallerBytes + 1;
        QVERIFY(!UpdateInfo::canInstall(huge, Platform::Windows, true));
    }

    void recognisesAnInstalledCopy() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath("bin"));
        const QString bin = QDir(dir.path()).filePath("bin");
        QVERIFY(!UpdateInfo::isInstalledByInstaller(bin));
        QFile uninstaller(QDir(dir.path()).filePath("unins000.exe"));
        QVERIFY(uninstaller.open(QIODevice::WriteOnly));
        uninstaller.close();
        QVERIFY(UpdateInfo::isInstalledByInstaller(bin));
    }

    void startsTheInstallerSilentlyAndReopensColdHex() {
        const QStringList args = UpdateInfo::installerArguments();
        QVERIFY(args.contains("/SILENT"));
        QVERIFY(args.contains("/NORESTART"));
        QVERIFY(args.contains("/CLOSEAPPLICATIONS"));
        QVERIFY(args.contains("/RELAUNCH"));
        QVERIFY(!args.contains("/VERYSILENT"));
    }

    void keepsTheUpdateSettings() {
        QTemporaryDir dir;
        const QString path = dir.filePath("u.ini");
        {
            QSettings s(path, QSettings::IniFormat);
            const UpdateSettings fresh = UpdateSettings::load(s);
            QVERIFY(fresh.checkOnStartup);
            QVERIFY(fresh.skippedVersion.isEmpty());
            QVERIFY(fresh.etag.isEmpty());
            QVERIFY(fresh.cachedRelease.isEmpty());

            UpdateSettings u;
            u.checkOnStartup = false;
            u.skippedVersion = "1.0.1";
            u.etag = "W/\"abc123\"";
            u.cachedRelease = "{\"tag_name\":\"v1.0.1\"}";
            u.save(s);
        }
        QSettings s(path, QSettings::IniFormat);
        const UpdateSettings u = UpdateSettings::load(s);
        QVERIFY(!u.checkOnStartup);
        QCOMPARE(u.skippedVersion, QString("1.0.1"));
        QCOMPARE(u.etag, QString("W/\"abc123\""));
        QCOMPARE(u.cachedRelease, QString("{\"tag_name\":\"v1.0.1\"}"));
    }};

QTEST_GUILESS_MAIN(TestUpdateInfo)
#include "tst_updateinfo.moc"
