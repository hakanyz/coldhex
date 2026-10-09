#pragma once

#include <QString>
#include <QStringList>

// Reads the chip names out of a probe-rs target description (the YAML produced by
// target-gen from a CMSIS-Pack). Only the family name and the variant names are
// needed here; probe-rs itself loads the file when --chip-description-path is given.
class ChipDescription {
public:
    static QString familyName(const QString& yamlText);
    static QStringList variantNames(const QString& yamlText);

    // Reads the file and returns its variant names; `error` is set on failure.
    static QStringList variantNamesFromFile(const QString& path, QString* family, QString* error);
};
