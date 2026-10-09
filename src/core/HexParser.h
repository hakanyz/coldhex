#pragma once

#include "FirmwareTarget.h"
#include <QString>
#include <QByteArray>
#include <cstdint>

struct HexParseResult {
    bool success = false;
    uint32_t baseAddress = 0;
    uint32_t size = 0;
    QByteArray data;
    QString errorMessage;
};

class HexParser {
public:
    static FileFormat detectFormat(const QString& filePath);
    static HexParseResult analyzeHexFile(const QString& filePath, bool extractData = true);
    static HexParseResult analyzeBinFile(const QString& filePath, uint32_t baseAddress = 0x08000000);
};
