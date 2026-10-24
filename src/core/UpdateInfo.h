#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QSettings>
#include <QString>

// A version such as "1.0.0" or "1.1.0-beta2" (a leading "v" is accepted).
struct VersionNumber {
    bool valid = false;
    int major = 0;
    int minor = 0;
    int patch = 0;
    QString pre;  // the part after the dash; empty for a normal release
};

VersionNumber parseVersion(const QString& text);

// <0 when a is older than b, 0 when equal, >0 when a is newer. 1.0.0-beta2 is older than 1.0.0 and
// newer than 1.0.0-beta1; the number inside "beta10" counts as a number, so it is newer than "beta9".
int compareVersions(const VersionNumber& a, const VersionNumber& b);

enum class Platform {
    Windows,
    Linux,
    Other
};

Platform currentPlatform();

// What the update check needs to know about the newest release.
struct ReleaseInfo {
    bool valid = false;
    QString tag;           // "v1.0.1"
    QString version;       // "1.0.1"
    QString pageUrl;       // the release page
    QString notes;         // the description written on the release page
    QString assetName;     // the package for this computer, empty when there is none
    QString assetUrl;
    qint64 assetSize = 0;
    QString checksumUrl;   // a SHA256SUMS.txt file of the release, empty when there is none
};

class UpdateInfo {
public:
    // Reads the answer of GitHub's "latest release" request. Drafts and pre-releases are not
    // offered. `error` tells why the answer cannot be used (empty when valid).
    static ReleaseInfo parseRelease(const QByteArray& json, Platform platform, QString* error = nullptr);

    static bool isNewer(const QString& latest, const QString& current);

    // The check runs at most once a day (an unreadable or future time counts as due).
    static bool isDue(const QDateTime& lastCheck, const QDateTime& now, int hours = 24);
};

struct UpdateSettings {
    bool checkOnStartup = true;
    QDateTime lastCheck;
    QString skippedVersion;

    static UpdateSettings load(QSettings& settings);
    void save(QSettings& settings) const;
};
