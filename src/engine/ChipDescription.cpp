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
