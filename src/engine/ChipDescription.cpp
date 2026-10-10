#include "ChipDescription.h"
#include <QFile>

namespace {

int indentOf(const QString& line) {
    int n = 0;
    while (n < line.size() && line[n] == ' ') ++n;
    return n;
}

QString valueOf(const QString& trimmedLine, const QString& key) {
    QString v = trimmedLine.mid(key.size()).trimmed();
    if (v.size() >= 2 && ((v.startsWith('"') && v.endsWith('"')) || (v.startsWith('\'') && v.endsWith('\'')))) {
        v = v.mid(1, v.size() - 2);
    }
    return v;
}

}  // namespace

QString ChipDescription::familyName(const QString& yamlText) {
    const QStringList lines = yamlText.split('\n');
    for (const QString& raw : lines) {
        const QString line = QString(raw).remove('\r');
        if (line.startsWith("name:")) return valueOf(line.trimmed(), "name:");
        if (line.startsWith("variants:")) break;
    }
    return {};
}

QStringList ChipDescription::variantNames(const QString& yamlText) {
    // Inside the top-level "variants:" list every entry starts with "- name: <chip>" at one
    // fixed indentation. Deeper "- name:" lines (cores, memory regions) and the top-level
    // keys that follow the list (flash_algorithms: ...) must not be picked up.
    QStringList names;
    bool inVariants = false;
    int entryIndent = -1;

    const QStringList lines = yamlText.split('\n');
    for (const QString& raw : lines) {
        const QString line = QString(raw).remove('\r');
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#')) continue;

        const int indent = indentOf(line);

        if (!inVariants) {
            if (indent == 0 && trimmed.startsWith("variants:")) inVariants = true;
            continue;
        }

        if (indent == 0 && !trimmed.startsWith('-')) {
            break;  // next top-level key ends the variants list
        }

        if (trimmed.startsWith("- name:")) {
            if (entryIndent < 0) entryIndent = indent;
            if (indent == entryIndent) {
                const QString name = valueOf(trimmed, "- name:");
                if (!name.isEmpty()) names << name;
            }
        }
    }
    return names;
}

QStringList ChipDescription::variantNamesFromFile(const QString& path, QString* family, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = "Cannot open " + path;
        return {};
    }
    const QString text = QString::fromUtf8(file.readAll());
    const QStringList names = variantNames(text);
    if (names.isEmpty()) {
        if (error) *error = path + " does not look like a probe-rs chip description (no variants found).";
        return {};
    }
    if (family) *family = familyName(text);
    return names;
}

ChipMemoryMap ChipDescription::memoryMap(const QString& yamlText, const QString& variant) {
    ChipMemoryMap map;

    // Find the variant's block: from its "- name: X" line to the next entry at the same indent.
    const QStringList lines = yamlText.split('\n');
    bool inVariants = false;
    bool inBlock = false;
    int entryIndent = -1;

    enum class Kind { None, Nvm, Ram } kind = Kind::None;
    bool inRange = false;
    MemoryRegion current;
    bool haveStart = false;

    auto flush = [&]() {
        if (kind != Kind::None && haveStart && current.size > 0) map.regions.append(current);
        kind = Kind::None;
        inRange = false;
        haveStart = false;
        current = MemoryRegion();
    };

    for (const QString& raw : lines) {
        const QString line = QString(raw).remove('\r');
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#')) continue;
        const int indent = indentOf(line);

        if (!inVariants) {
            if (indent == 0 && trimmed.startsWith("variants:")) inVariants = true;
            continue;
        }
        if (indent == 0 && !trimmed.startsWith('-')) break;  // end of the variants list

        if (trimmed.startsWith("- name:")) {
            if (entryIndent < 0) entryIndent = indent;
            if (indent == entryIndent) {
                flush();
                const QString name = valueOf(trimmed, "- name:");
                if (inBlock) break;  // the next variant starts
                inBlock = (name.compare(variant, Qt::CaseInsensitive) == 0);
                continue;
            }
        }
        if (!inBlock) continue;

        if (trimmed.startsWith("- !Nvm")) { flush(); kind = Kind::Nvm; }
        else if (trimmed.startsWith("- !Ram")) { flush(); kind = Kind::Ram; }
        else if (trimmed.startsWith("- !")) { flush(); }
        else if (kind != Kind::None) {
            if (trimmed.startsWith("range:")) {
                inRange = true;
            } else if (inRange && trimmed.startsWith("start:")) {
                current.start = valueOf(trimmed, "start:").toULongLong(nullptr, 0);
                current.nonVolatile = (kind == Kind::Nvm);
                haveStart = true;
            } else if (inRange && trimmed.startsWith("end:")) {
                const uint64_t end = valueOf(trimmed, "end:").toULongLong(nullptr, 0);
                current.size = end > current.start ? end - current.start : 0;
                inRange = false;
            }
        }
    }
    flush();
    return map;
}

ChipMemoryMap ChipDescription::memoryMapFromFile(const QString& path, const QString& variant) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return memoryMap(QString::fromUtf8(file.readAll()), variant);
}
