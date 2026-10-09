#include "ChipCatalog.h"

namespace {

struct ManufacturerRule {
    const char* label;
    QStringList prefixes;
};

const QList<ManufacturerRule>& rules() {
    static const QList<ManufacturerRule> list = {
        {"STM32", {"STM32"}},
        {"GD32", {"GD32"}},
        {"APM32 / Geehy", {"APM32"}},
        {"Artery AT32", {"AT32"}},
        {"TI", {"MSP", "CC13", "CC23", "CC26", "TM4C"}},
        {"Nordic nRF", {"nRF"}},
        {"NXP", {"LPC", "MIMX"}},
    };
    return list;
}

bool matches(const ManufacturerRule& rule, const QString& chipName) {
    for (const QString& prefix : rule.prefixes) {
        if (chipName.startsWith(prefix, Qt::CaseInsensitive)) return true;
    }
    return false;
}

}  // namespace

QString ChipCatalog::allChipsLabel() {
    return QStringLiteral("All chips");
}

QString ChipCatalog::customChipsLabel() {
    return QStringLiteral("Custom files");
}

QStringList ChipCatalog::manufacturers(const QList<ChipEntry>& chips) {
    QStringList result{allChipsLabel()};
    for (const ManufacturerRule& rule : rules()) {
        for (const ChipEntry& chip : chips) {
            if (matches(rule, chip.name)) {
                result << QString::fromLatin1(rule.label);
                break;
            }
        }
    }
    for (const ChipEntry& chip : chips) {
        if (!chip.descriptionFile.isEmpty()) {
            result << customChipsLabel();
            break;
        }
    }
    return result;
}

QStringList ChipCatalog::chipNames(const QList<ChipEntry>& chips, const QString& manufacturer) {
    QStringList names;
    const ManufacturerRule* selected = nullptr;
    for (const ManufacturerRule& rule : rules()) {
        if (manufacturer == QLatin1String(rule.label)) selected = &rule;
    }

    for (const ChipEntry& chip : chips) {
        const bool custom = (manufacturer == customChipsLabel()) && !chip.descriptionFile.isEmpty();
        if (manufacturer == allChipsLabel() || custom || (selected && matches(*selected, chip.name))) {
            names << chip.name;
        }
    }
    return names;
}

const ChipEntry* ChipCatalog::findEntry(const QList<ChipEntry>& chips, const QString& name) {
    const QString wanted = name.trimmed();
    for (const ChipEntry& chip : chips) {
        if (chip.name.compare(wanted, Qt::CaseInsensitive) == 0) return &chip;
    }
    return nullptr;
}

bool ChipCatalog::find(const QList<ChipEntry>& chips, const QString& name, QString* canonicalName) {
    const ChipEntry* entry = findEntry(chips, name);
    if (!entry) return false;
    if (canonicalName) *canonicalName = entry->name;
    return true;
}
