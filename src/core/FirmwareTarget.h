#pragma once

#include <QString>
#include <cstdint>

enum class FileFormat {
    AutoDetect,
    IntelHex,
    RawBinary,
    Elf
};

struct FirmwareSlot {
    bool enabled = false;
    QString label;              // e.g. "File 1"
    QString filePath;
    uint32_t startAddress = 0x08000000;
    FileFormat format = FileFormat::AutoDetect;
    uint32_t detectedSize = 0;
    bool isValid = false;
};

struct TargetConfig {
    QString chipName;
    uint32_t speedKhz = 4000;
    QString probeId;
    bool autoSpeedEnabled = true;
    bool eraseBeforeFlash = true;
    bool verifyAfterFlash = true;
    bool resetAfterFlash = true;
};
