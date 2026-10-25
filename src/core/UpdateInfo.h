#pragma once

#include <QByteArray>
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

    // The SHA-256 of `fileName` (lowercase hex) from the text of a SHA256SUMS.txt file, empty when
    // the file is not listed there or the line is malformed.
    static QString expectedChecksum(const QByteArray& sumsFile, const QString& fileName);

    // The installer replaces the running program only when it was put there by the Windows
    // installer (which leaves its uninstaller next to the program folder) and the release carries
    // the package and its checksum.
    static bool isInstalledByInstaller(const QString& appDirPath);
    static bool canInstall(const ReleaseInfo& release, Platform platform, bool installedByInstaller);

    // What the installer is started with: silent, closing and reopening ColdHex.
    static QStringList installerArguments();

    static constexpr qint64 kMaxInstallerBytes = 300LL * 1024 * 1024;
};

struct UpdateSettings {
    bool checkOnStartup = true;
    QString skippedVersion;
    // The last answer of GitHub and its tag, so that the next check can ask "has it changed?".
    // An answer of "not changed" does not count against GitHub's limit on requests.
    QString etag;
    QString cachedRelease;

    static UpdateSettings load(QSettings& settings);
    void save(QSettings& settings) const;
};
