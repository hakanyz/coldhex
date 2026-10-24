#pragma once

#include "ProfileStore.h"
#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

struct NamedProfile {
    QString name;
    Profile profile;
};

struct ImportResult {
    QList<NamedProfile> profiles;
    QStringList skipped;  // one line for each profile left out of the file, with the reason
    QString error;        // set when the file as a whole cannot be used
    bool ok() const { return error.isEmpty(); }
};

// Profiles as a JSON file, to move them to another computer, share them or keep a copy.
// Reading is strict: whatever comes out of fromJson() can be stored and loaded safely.
class ProfileTransfer {
public:
    static constexpr int kFormatVersion = 1;
    static constexpr qint64 kMaxFileBytes = 1024 * 1024;
    static constexpr int kMaxProfiles = 200;

    // Firmware file paths are written only when `includePaths` is set. A path inside `baseDir`
    // (the folder of the exported file) is written relative to it.
    static QByteArray toJson(const QList<NamedProfile>& profiles, const QString& appVersion,
                             bool includePaths, const QString& baseDir);

    // Relative firmware paths are resolved against `baseDir`.
    static ImportResult fromJson(const QByteArray& data, const QString& baseDir);

    // `base` itself, or "base (2)", "base (3)"... when the name (any capitalisation) is taken.
    static QString uniqueName(const QString& base, const QStringList& taken);
};
