#include "UpdateInfo.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

namespace {
const QString kReleasePagePrefix = QStringLiteral("https://github.com/hakanyz/coldhex/");
const QString kDownloadPrefix = QStringLiteral("https://github.com/hakanyz/coldhex/releases/download/");

// "beta10" -> letters "beta", number 10, so that beta10 is newer than beta9.
int comparePreIdentifier(const QString& a, const QString& b) {
    static const QRegularExpression split(QStringLiteral("^(\\D*)(\\d*)$"));
    const QRegularExpressionMatch ma = split.match(a);
    const QRegularExpressionMatch mb = split.match(b);
    if (!ma.hasMatch() || !mb.hasMatch()) return QString::compare(a, b);
    const int letters = QString::compare(ma.captured(1), mb.captured(1));
    if (letters != 0) return letters;
    const qlonglong na = ma.captured(2).isEmpty() ? -1 : ma.captured(2).toLongLong();
    const qlonglong nb = mb.captured(2).isEmpty() ? -1 : mb.captured(2).toLongLong();
    return na < nb ? -1 : (na > nb ? 1 : 0);
}

int comparePre(const QString& a, const QString& b) {
    const QStringList pa = a.split('.');
    const QStringList pb = b.split('.');
    for (int i = 0; i < qMax(pa.size(), pb.size()); ++i) {
        if (i >= pa.size()) return -1;
        if (i >= pb.size()) return 1;
        const int c = comparePreIdentifier(pa[i], pb[i]);
        if (c != 0) return c;
    }
    return 0;
}

bool isAssetFor(const QString& name, Platform platform) {
    switch (platform) {
    case Platform::Windows: return name.endsWith(QLatin1String("-win64.exe"), Qt::CaseInsensitive);
    case Platform::Linux: return name.endsWith(QLatin1String(".deb"), Qt::CaseInsensitive);
    case Platform::Other: break;
    }
    return false;
}
}

VersionNumber parseVersion(const QString& text) {
    static const QRegularExpression pattern(QStringLiteral("^v?(\\d+)\\.(\\d+)(?:\\.(\\d+))?(?:-([0-9A-Za-z.\\-]+))?(?:\\+[0-9A-Za-z.\\-]+)?$"));
    VersionNumber v;
    const QRegularExpressionMatch m = pattern.match(text.trimmed());
    if (!m.hasMatch()) return v;
    bool ok = true;
    v.major = m.captured(1).toInt(&ok);
    bool ok2 = true;
    v.minor = m.captured(2).toInt(&ok2);
    v.patch = m.captured(3).isEmpty() ? 0 : m.captured(3).toInt();
    v.pre = m.captured(4);
    v.valid = ok && ok2;
    return v;
}

int compareVersions(const VersionNumber& a, const VersionNumber& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    if (a.pre.isEmpty() && b.pre.isEmpty()) return 0;
    if (a.pre.isEmpty()) return 1;   // a normal release is newer than its pre-releases
    if (b.pre.isEmpty()) return -1;
    return comparePre(a.pre, b.pre);
}

Platform currentPlatform() {
#if defined(Q_OS_WIN)
    return Platform::Windows;
#elif defined(Q_OS_LINUX)
    return Platform::Linux;
#else
    return Platform::Other;
#endif
}

ReleaseInfo UpdateInfo::parseRelease(const QByteArray& json, Platform platform, QString* error) {
    ReleaseInfo info;
    auto fail = [&](const QString& why) { if (error) *error = why; return info; };

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) return fail("GitHub sent an answer that is not readable.");
    const QJsonObject o = doc.object();

    if (o.contains("message") && !o.contains("tag_name")) {
        const QString message = o.value("message").toString();
        if (message.contains("rate limit", Qt::CaseInsensitive)) return fail("GitHub is limiting requests from this network. Try again later.");
        return fail(QString("GitHub answered: %1").arg(message));
    }
    if (o.value("draft").toBool() || o.value("prerelease").toBool()) return fail("The newest release is a draft or pre-release.");

    const QString tag = o.value("tag_name").toString();
    const VersionNumber version = parseVersion(tag);
    if (!version.valid) return fail("The newest release has no usable version number.");

    info.tag = tag;
    info.version = tag.startsWith('v') ? tag.mid(1) : tag;
    const QString page = o.value("html_url").toString();
    if (page.startsWith(kReleasePagePrefix)) info.pageUrl = page;
    info.notes = o.value("body").toString();

    const QJsonArray assets = o.value("assets").toArray();
    for (const QJsonValue& value : assets) {
        const QJsonObject asset = value.toObject();
        const QString name = asset.value("name").toString();
        const QString url = asset.value("browser_download_url").toString();
        if (!url.startsWith(kDownloadPrefix)) continue;  // only files of this project's releases
        if (info.assetName.isEmpty() && isAssetFor(name, platform)) {
            info.assetName = name;
            info.assetUrl = url;
            info.assetSize = static_cast<qint64>(asset.value("size").toDouble());
        } else if (name == QLatin1String("SHA256SUMS.txt")) {
            info.checksumUrl = url;
        }
    }

    info.valid = true;
    if (error) error->clear();
    return info;
}

bool UpdateInfo::isNewer(const QString& latest, const QString& current) {
    const VersionNumber l = parseVersion(latest);
    const VersionNumber c = parseVersion(current);
    return l.valid && c.valid && compareVersions(l, c) > 0;
}

bool UpdateInfo::isDue(const QDateTime& lastCheck, const QDateTime& now, int hours) {
    if (!lastCheck.isValid()) return true;
    if (lastCheck > now) return true;
    return lastCheck.secsTo(now) >= static_cast<qint64>(hours) * 3600;
}

UpdateSettings UpdateSettings::load(QSettings& settings) {
    UpdateSettings s;
    s.checkOnStartup = settings.value("updates/checkOnStartup", true).toBool();
    s.lastCheck = QDateTime::fromString(settings.value("updates/lastCheck").toString(), Qt::ISODate);
    s.skippedVersion = settings.value("updates/skippedVersion").toString();
    return s;
}

void UpdateSettings::save(QSettings& settings) const {
    settings.setValue("updates/checkOnStartup", checkOnStartup);
    settings.setValue("updates/lastCheck", lastCheck.isValid() ? lastCheck.toString(Qt::ISODate) : QString());
    settings.setValue("updates/skippedVersion", skippedVersion);
    settings.sync();
}
