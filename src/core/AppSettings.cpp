#include "AppSettings.h"
#include <QSettings>

namespace {
constexpr int kSchemaVersion = 1;
}

UiSettings AppSettings::load(QSettings& settings) {
    UiSettings v;
    if (settings.value("schemaVersion", 0).toInt() != kSchemaVersion) {
        return v;
    }

    v.hasSaved = true;
    v.manufacturer = settings.value("target/manufacturer").toString();
    v.chip = settings.value("target/chip").toString();
    v.probe = settings.value("target/probe").toString();
    v.interfaceName = settings.value("target/interface").toString();
    v.speedKhz = settings.value("target/speedKhz", v.speedKhz).toInt();

    v.slot1Path = settings.value("firmware/slot1Path").toString();
    v.slot2Path = settings.value("firmware/slot2Path").toString();
    v.slot1Address = settings.value("firmware/slot1Address").toString();
    v.slot2Address = settings.value("firmware/slot2Address").toString();
    v.customChipFiles = settings.value("target/customChipFiles").toStringList();
    v.slot1Enabled = settings.value("firmware/slot1Enabled", v.slot1Enabled).toBool();
    v.slot2Enabled = settings.value("firmware/slot2Enabled", v.slot2Enabled).toBool();

    v.geometry = settings.value("window/geometry").toByteArray();
    v.windowState = settings.value("window/state").toByteArray();
    return v;
}

void AppSettings::save(QSettings& settings, const UiSettings& v) {
    settings.setValue("schemaVersion", kSchemaVersion);

    settings.setValue("target/manufacturer", v.manufacturer);
    settings.setValue("target/chip", v.chip);
    settings.setValue("target/probe", v.probe);
    settings.setValue("target/interface", v.interfaceName);
    settings.setValue("target/speedKhz", v.speedKhz);

    settings.setValue("firmware/slot1Path", v.slot1Path);
    settings.setValue("firmware/slot2Path", v.slot2Path);
    settings.setValue("firmware/slot1Address", v.slot1Address);
    settings.setValue("firmware/slot2Address", v.slot2Address);
    settings.setValue("target/customChipFiles", v.customChipFiles);
    settings.setValue("firmware/slot1Enabled", v.slot1Enabled);
    settings.setValue("firmware/slot2Enabled", v.slot2Enabled);

    settings.setValue("window/geometry", v.geometry);
    settings.setValue("window/state", v.windowState);
    settings.sync();
}
