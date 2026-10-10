#pragma once

#include <QString>
#include <QStringList>

// Finds the probe-rs executable. A copy shipped with ColdHex (next to the program, or in
// ../lib/coldhex for the Linux package) wins over one on PATH, so an installed package
// works without installing anything else.
class ProbeRsLocator {
public:
    // `appDir` is the folder of the ColdHex executable, `homeDir` the user's home folder.
    // Returns the full path of the first match, or an empty string.
    static QString find(const QString& appDir, const QString& homeDir);

    // The folders searched, in order, before PATH.
    static QStringList candidateDirs(const QString& appDir, const QString& homeDir);
};
