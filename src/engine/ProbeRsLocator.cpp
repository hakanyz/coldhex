#include "ProbeRsLocator.h"
#include <QDir>
#include <QStandardPaths>

QStringList ProbeRsLocator::candidateDirs(const QString& appDir, const QString& homeDir) {
    QStringList dirs;
    dirs << appDir
         << QDir::cleanPath(appDir + "/probe-rs")
         << QDir::cleanPath(appDir + "/../lib/coldhex");
    return dirs;
}

QString ProbeRsLocator::find(const QString& appDir, const QString& homeDir) {
    // 1. Bundled with the program
    const QString bundled = QStandardPaths::findExecutable("probe-rs", candidateDirs(appDir, homeDir));
    if (!bundled.isEmpty()) return bundled;

    // 2. On PATH
    const QString onPath = QStandardPaths::findExecutable("probe-rs");
    if (!onPath.isEmpty()) return onPath;

    // 3. Default location of the probe-rs installer
    return QStandardPaths::findExecutable("probe-rs", {homeDir + "/.cargo/bin"});
}
