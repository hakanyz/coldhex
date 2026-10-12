#include "JLinkLocator.h"
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

// Orders names so that a run of digits counts by its value: "V99" is before "V914a". QCollator can
// do this too, but only where the platform supports its numeric mode; this works everywhere.
int JLinkLocator::naturalCompare(const QString& a, const QString& b) {
    int i = 0;
    int j = 0;
    while (i < a.size() && j < b.size()) {
        if (a.at(i).isDigit() && b.at(j).isDigit()) {
            int startA = i;
            int startB = j;
            while (i < a.size() && a.at(i).isDigit()) ++i;
            while (j < b.size() && b.at(j).isDigit()) ++j;
            QString numberA = a.mid(startA, i - startA);
            QString numberB = b.mid(startB, j - startB);
            while (numberA.size() > 1 && numberA.startsWith('0')) numberA.remove(0, 1);
            while (numberB.size() > 1 && numberB.startsWith('0')) numberB.remove(0, 1);
            if (numberA.size() != numberB.size()) return numberA.size() < numberB.size() ? -1 : 1;
            const int same = numberA.compare(numberB);
            if (same != 0) return same < 0 ? -1 : 1;
        } else {
            const QChar charA = a.at(i).toLower();
            const QChar charB = b.at(j).toLower();
            if (charA != charB) return charA < charB ? -1 : 1;
            ++i;
            ++j;
        }
    }
    // One name is the start of the other: the shorter one is first.
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

QString JLinkLocator::find(const QStringList& roots, const QString& exeName) {
    const QString fromEnvironment = qEnvironmentVariable("COLDHEX_JLINK");
    if (!fromEnvironment.isEmpty() && QFileInfo::exists(fromEnvironment)) return fromEnvironment;

    // Folders such as JLink_V914a or JLink_V810: the highest version comes first.
    for (const QString& root : roots) {
        QStringList folders = QDir(root).entryList({"JLink*"}, QDir::Dirs | QDir::NoDotAndDotDot);
        std::sort(folders.begin(), folders.end(),
                  [](const QString& a, const QString& b) { return naturalCompare(a, b) > 0; });
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
