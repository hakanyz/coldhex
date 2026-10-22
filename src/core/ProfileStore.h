#pragma once

#include <QString>
#include <QStringList>

class QSettings;

// Which firmware file(s) Program and Verify write.
enum class FirmwareChoice {
    File1,
    File2,
    Both
};

// A named set of target and firmware settings, for switching between boards in one click.
// The probe is left out on purpose: it belongs to the computer, not to the board.
struct Profile {
    QString manufacturer;
    QString chip;
    QString interfaceName;
    int speedKhz = 1000;

    QString slot1Path;
    QString slot2Path;
    QString slot1Address;
    QString slot2Address;
    FirmwareChoice firmware = FirmwareChoice::File2;

    bool eraseBeforeFlash = true;
    bool verifyAfterFlash = true;
    bool resetAfterFlash = true;
    bool connectUnderReset = false;

    bool operator==(const Profile& o) const {
        return manufacturer == o.manufacturer && chip == o.chip && interfaceName == o.interfaceName
            && speedKhz == o.speedKhz && slot1Path == o.slot1Path && slot2Path == o.slot2Path
            && slot1Address == o.slot1Address && slot2Address == o.slot2Address
            && firmware == o.firmware && eraseBeforeFlash == o.eraseBeforeFlash
            && verifyAfterFlash == o.verifyAfterFlash && resetAfterFlash == o.resetAfterFlash
            && connectUnderReset == o.connectUnderReset;
    }
};

class ProfileStore {
public:
    // A name must not be empty and must not contain a slash or backslash (QSettings uses them
    // as separators).
    static bool isValidName(const QString& name);

    static QStringList names(QSettings& settings);
    static bool load(QSettings& settings, const QString& name, Profile& out);
    static void save(QSettings& settings, const QString& name, const Profile& profile);
    static void remove(QSettings& settings, const QString& name);
};
