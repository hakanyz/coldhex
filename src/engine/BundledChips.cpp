#include "BundledChips.h"
#include <QDir>
#include <QFile>

QStringList BundledChips::extract(const QString& sourceDir, const QString& targetDir) {
    QStringList paths;

    const QDir source(sourceDir);
    if (!source.exists()) return paths;
    if (!QDir().mkpath(targetDir)) return paths;

    const QStringList names = source.entryList({"*.yaml", "*.yml"}, QDir::Files, QDir::Name);
    for (const QString& name : names) {
        QFile in(source.filePath(name));
        if (!in.open(QIODevice::ReadOnly)) continue;
        const QByteArray content = in.readAll();

        const QString targetPath = QDir(targetDir).filePath(name);
        QFile existing(targetPath);
        if (existing.open(QIODevice::ReadOnly)) {
            const bool same = (existing.readAll() == content);
            existing.close();
            if (same) {
                paths << targetPath;
                continue;
            }
        }

        QFile out(targetPath);
        if (out.open(QIODevice::WriteOnly | QIODevice::Truncate) && out.write(content) == content.size()) {
            paths << targetPath;
        }
    }
    return paths;
}
