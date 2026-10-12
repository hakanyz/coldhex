#include "JLinkLocator.h"
#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <algorithm>

QStringList JLinkLocator::defaultRoots() {
#ifdef Q_OS_WIN
    return {"C:/Program Files/SEGGER", "C:/Program Files (x86)/SEGGER"};
#else
    return {"/opt/SEGGER"};
#endif
}

QString JLinkLocator::defaultExeName() {
#ifdef Q_OS_WIN
    return "JLink.exe";
#else
    return "JLinkExe";
#endif
}

QString JLinkLocator::find(const QStringList& roots, const QString& exeName) {
    const QString fromEnvironment = qEnvironmentVariable("COLDHEX_JLINK");
    if (!fromEnvironment.isEmpty() && QFileInfo::exists(fromEnvironment)) return fromEnvironment;

    // Folders such as JLink_V914a or JLink_V810: the highest version comes first.
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);

    for (const QString& root : roots) {
        QStringList folders = QDir(root).entryList({"JLink*"}, QDir::Dirs | QDir::NoDotAndDotDot);
        std::sort(folders.begin(), folders.end(),
                  [&collator](const QString& a, const QString& b) { return collator.compare(a, b) > 0; });
        for (const QString& folder : folders) {
            const QString candidate = QDir(root).filePath(folder + "/" + exeName);
            if (QFileInfo(candidate).isFile()) return candidate;
        }
    }

    return QStandardPaths::findExecutable(exeName);
}

QString JLinkLocator::findDefault() {
    return find(defaultRoots(), defaultExeName());
}
