#pragma once

#include <QString>
#include <QStringList>

// Finds SEGGER's J-Link Commander (JLink.exe on Windows, JLinkExe on Linux). ColdHex cannot ship
// it (SEGGER's licence does not allow that); it is there when the user has installed the J-Link
// software, as most Keil users have. Without it ColdHex still reaches a J-Link through probe-rs,
// which needs the WinUSB driver (Zadig).
class JLinkLocator {
public:
    // Looks in `roots` for folders named JLink* (the newest version wins) that contain
    // `exeName`, then on PATH. The environment variable COLDHEX_JLINK can name the file itself.
    // Returns the full path or an empty string.
    static QString find(const QStringList& roots, const QString& exeName);

    // The usual install folders and the name of the Commander on this system.
    static QStringList defaultRoots();
    static QString defaultExeName();

    // Orders names so that a run of digits counts by its value ("V99" before "V914a"); negative, zero
    // or positive like strcmp.
    static int naturalCompare(const QString& a, const QString& b);

    // find() with the defaults.
    static QString findDefault();
};
