#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

class QSettings;

// Values the main window restores on startup. `hasSaved` is false when nothing
// has been stored yet, in which case the UI keeps its built-in defaults.
struct UiSettings {
    bool hasSaved = false;

    QString manufacturer;
    QString chip;
    QString probe;
    QString interfaceName;
    int speedKhz = 1000;

    QString slot1Path;
    QString slot2Path;
    QString slot1Address;
    QString slot2Address;
    QStringList customChipFiles;
    bool slot1Enabled = true;
    bool slot2Enabled = true;

    QByteArray geometry;
    QByteArray windowState;
};

class AppSettings {
public:
    static UiSettings load(QSettings& settings);
    static void save(QSettings& settings, const UiSettings& values);
};
