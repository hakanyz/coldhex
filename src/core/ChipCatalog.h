#pragma once

#include "engine/IProgrammerEngine.h"
#include <QList>
#include <QString>
#include <QStringList>

// Groups the chips reported by the engine by manufacturer, using name prefixes.
// Manufacturers without any chip in the engine's database are not offered.
class ChipCatalog {
public:
    static QString allChipsLabel();
    static QString customChipsLabel();

    // "All chips" first, then every known manufacturer that has at least one chip.
    static QStringList manufacturers(const QList<ChipEntry>& chips);

    // Chip names of one manufacturer, or all names for allChipsLabel().
    static QStringList chipNames(const QList<ChipEntry>& chips, const QString& manufacturer);

    // Case-insensitive exact lookup; returns the engine's spelling in `canonicalName`.
    static bool find(const QList<ChipEntry>& chips, const QString& name, QString* canonicalName = nullptr);

    // Same lookup, returning the entry itself (null when not found).
    static const ChipEntry* findEntry(const QList<ChipEntry>& chips, const QString& name);
};
