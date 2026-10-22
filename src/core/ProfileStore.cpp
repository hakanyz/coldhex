#include "ProfileStore.h"
#include <QSettings>

namespace {
const QString kRoot = QStringLiteral("profiles");

QString keyFor(const QString& name, const char* field) {
    return kRoot + '/' + name + '/' + QLatin1String(field);
}

QString choiceToText(FirmwareChoice c) {
    switch (c) {
    case FirmwareChoice::File1: return QStringLiteral("file1");
    case FirmwareChoice::Both: return QStringLiteral("both");
    case FirmwareChoice::File2: break;
    }
    return QStringLiteral("file2");
}

FirmwareChoice choiceFromText(const QString& text) {
    if (text == "file1") return FirmwareChoice::File1;
    if (text == "both") return FirmwareChoice::Both;
    return FirmwareChoice::File2;
}
}

bool ProfileStore::isValidName(const QString& name) {
    const QString trimmed = name.trimmed();
    return !trimmed.isEmpty() && !trimmed.contains('/') && !trimmed.contains('\\');
}

QStringList ProfileStore::names(QSettings& settings) {
    settings.beginGroup(kRoot);
    QStringList result = settings.childGroups();
    settings.endGroup();
    result.sort(Qt::CaseInsensitive);
    return result;
}

bool ProfileStore::load(QSettings& settings, const QString& name, Profile& out) {
    if (!names(settings).contains(name)) return false;

    Profile p;
    p.manufacturer = settings.value(keyFor(name, "manufacturer")).toString();
    p.chip = settings.value(keyFor(name, "chip")).toString();
    p.interfaceName = settings.value(keyFor(name, "interface")).toString();
    p.speedKhz = settings.value(keyFor(name, "speedKhz"), p.speedKhz).toInt();
    p.slot1Path = settings.value(keyFor(name, "slot1Path")).toString();
    p.slot2Path = settings.value(keyFor(name, "slot2Path")).toString();
    p.slot1Address = settings.value(keyFor(name, "slot1Address")).toString();
    p.slot2Address = settings.value(keyFor(name, "slot2Address")).toString();
    p.firmware = choiceFromText(settings.value(keyFor(name, "firmware")).toString());
    p.eraseBeforeFlash = settings.value(keyFor(name, "eraseBeforeFlash"), p.eraseBeforeFlash).toBool();
    p.verifyAfterFlash = settings.value(keyFor(name, "verifyAfterFlash"), p.verifyAfterFlash).toBool();
    p.resetAfterFlash = settings.value(keyFor(name, "resetAfterFlash"), p.resetAfterFlash).toBool();
    p.connectUnderReset = settings.value(keyFor(name, "connectUnderReset"), p.connectUnderReset).toBool();
    out = p;
    return true;
}

void ProfileStore::save(QSettings& settings, const QString& name, const Profile& p) {
    settings.setValue(keyFor(name, "manufacturer"), p.manufacturer);
    settings.setValue(keyFor(name, "chip"), p.chip);
    settings.setValue(keyFor(name, "interface"), p.interfaceName);
    settings.setValue(keyFor(name, "speedKhz"), p.speedKhz);
    settings.setValue(keyFor(name, "slot1Path"), p.slot1Path);
    settings.setValue(keyFor(name, "slot2Path"), p.slot2Path);
    settings.setValue(keyFor(name, "slot1Address"), p.slot1Address);
    settings.setValue(keyFor(name, "slot2Address"), p.slot2Address);
    settings.setValue(keyFor(name, "firmware"), choiceToText(p.firmware));
    settings.setValue(keyFor(name, "eraseBeforeFlash"), p.eraseBeforeFlash);
    settings.setValue(keyFor(name, "verifyAfterFlash"), p.verifyAfterFlash);
    settings.setValue(keyFor(name, "resetAfterFlash"), p.resetAfterFlash);
    settings.setValue(keyFor(name, "connectUnderReset"), p.connectUnderReset);
    settings.sync();
}

void ProfileStore::remove(QSettings& settings, const QString& name) {
    settings.remove(kRoot + '/' + name);
    settings.sync();
}
