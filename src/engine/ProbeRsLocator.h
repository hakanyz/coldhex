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

    // Finds the coldhex-flash helper that reports real erase and program progress. It is only
    // looked for next to the program (or in the same folders as probe-rs), never on PATH. The
    // environment variable COLDHEX_FLASH_HELPER can name a file for a development build.
    // Returns an empty string when there is none; ColdHex then uses probe-rs alone.
    static QString findHelper(const QString& appDir);
};
