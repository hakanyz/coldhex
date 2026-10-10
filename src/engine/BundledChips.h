#pragma once

#include <QString>
#include <QStringList>

// probe-rs reads chip descriptions from real files, so YAML files that are built into the
// executable as Qt resources are written out to a folder before they can be used.
class BundledChips {
public:
    // Copies every *.yaml in `sourceDir` (":/chips" in the application) to `targetDir`,
    // replacing files whose content differs. Returns the paths of the files in `targetDir`.
    static QStringList extract(const QString& sourceDir, const QString& targetDir);
};
