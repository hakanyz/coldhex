#include "HexParser.h"
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <algorithm>
#include <map>
#include <vector>
#include <cstdint>

FileFormat HexParser::detectFormat(const QString& filePath) {
    QFileInfo info(filePath);
    QString ext = info.suffix().toLower();
    if (ext == "hex" || ext == "ihex") {
        return FileFormat::IntelHex;
    }
    if (ext == "bin") {
        return FileFormat::RawBinary;
    }
    if (ext == "elf" || ext == "out" || ext == "axf") {
        return FileFormat::Elf;
    }
    return FileFormat::AutoDetect;
}

HexParseResult HexParser::analyzeBinFile(const QString& filePath, uint32_t baseAddress) {
    HexParseResult result;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.errorMessage = "Failed to open binary file: " + filePath;
        return result;
    }
    result.data = file.readAll();
    result.success = true;
    result.baseAddress = baseAddress;
    result.size = static_cast<uint32_t>(result.data.size());
    return result;
}

HexParseResult HexParser::analyzeHexFile(const QString& filePath, bool extractData) {
    HexParseResult result;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorMessage = "Failed to open HEX file: " + filePath;
        return result;
    }

    QTextStream in(&file);
    uint32_t upperAddress = 0;
    uint32_t lowestAddress = 0xFFFFFFFF;
    uint32_t highestAddress = 0;
    uint32_t totalBytes = 0;
    bool foundData = false;
    int lineNum = 0;

    std::map<uint32_t, uint8_t> byteMap;

    while (!in.atEnd()) {
        lineNum++;
        QString line = in.readLine().trimmed();
        if (line.isEmpty()) continue;
        if (!line.startsWith(':')) {
            result.errorMessage = QString("Line %1: Invalid HEX line (does not start with ':')").arg(lineNum);
            return result;
        }

        if (line.length() < 11) {
            result.errorMessage = QString("Line %1: Line too short for HEX record").arg(lineNum);
            return result;
        }

        bool ok = false;
        uint8_t byteCount = line.mid(1, 2).toUInt(&ok, 16);
        if (!ok) {
            result.errorMessage = QString("Line %1: Invalid byte count field").arg(lineNum);
            return result;
        }

        if (line.length() != 11 + byteCount * 2) {
            result.errorMessage = QString("Line %1: Record length does not match its byte count").arg(lineNum);
            return result;
        }

        // All record bytes including the checksum must sum to zero (mod 256).
        uint8_t sum = 0;
        for (int i = 1; i + 1 < line.length(); i += 2) {
            uint8_t b = line.mid(i, 2).toUInt(&ok, 16);
            if (!ok) {
                result.errorMessage = QString("Line %1: Non-hexadecimal character in record").arg(lineNum);
                return result;
            }
            sum = static_cast<uint8_t>(sum + b);
        }
        if (sum != 0) {
            result.errorMessage = QString("Line %1: Checksum mismatch").arg(lineNum);
            return result;
        }

        uint16_t addressOffset = line.mid(3, 4).toUInt(&ok, 16);
        if (!ok) { result.errorMessage = QString("Line %1: Invalid address field").arg(lineNum); return result; }

        uint8_t recordType = line.mid(7, 2).toUInt(&ok, 16);
        if (!ok) { result.errorMessage = QString("Line %1: Invalid record type field").arg(lineNum); return result; }

        if (recordType == 0x00) { // Data Record
            uint32_t fullAddress = upperAddress + addressOffset;
            if (fullAddress < lowestAddress) lowestAddress = fullAddress;
            uint32_t endAddress = fullAddress + byteCount;
            if (endAddress > highestAddress) highestAddress = endAddress;
            totalBytes += byteCount;
            foundData = true;

            if (extractData) {
                for (uint8_t b = 0; b < byteCount; ++b) {
                    uint8_t val = line.mid(9 + (b * 2), 2).toUInt(nullptr, 16);
                    byteMap[fullAddress + b] = val;
                }
            }
        } else if (recordType == 0x01) { // End of File
            break;
        } else if (recordType == 0x02) { // Extended Segment Address
            uint16_t seg = line.mid(9, 4).toUInt(&ok, 16);
            if (ok) upperAddress = static_cast<uint32_t>(seg) << 4;
        } else if (recordType == 0x04) { // Extended Linear Address
            uint16_t highAddr = line.mid(9, 4).toUInt(&ok, 16);
            if (ok) upperAddress = static_cast<uint32_t>(highAddr) << 16;
        }
    }

    if (!foundData) {
        result.errorMessage = "No valid data records (type 00) found in HEX file.";
        return result;
    }

    result.success = true;
    result.baseAddress = lowestAddress;
    result.size = (highestAddress >= lowestAddress) ? (highestAddress - lowestAddress) : totalBytes;

    if (extractData && result.size > 0 && result.size <= 16 * 1024 * 1024) { // Up to 16MB
        result.data.resize(result.size);
        result.data.fill(static_cast<char>(0xFF)); // Fill with flash erased state (0xFF)
        for (const auto& pair : byteMap) {
            uint32_t offset = pair.first - lowestAddress;
            if (offset < result.size) {
                result.data[offset] = static_cast<char>(pair.second);
            }
        }
    }

    return result;
}

namespace {

uint32_t le32(const QByteArray& d, int offset) {
    const auto* p = reinterpret_cast<const uint8_t*>(d.constData()) + offset;
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

uint16_t le16(const QByteArray& d, int offset) {
    const auto* p = reinterpret_cast<const uint8_t*>(d.constData()) + offset;
    return uint16_t(p[0] | (p[1] << 8));
}

}  // namespace

HexParseResult HexParser::parseElf(const QByteArray& file, bool extractData) {
    HexParseResult result;

    if (file.size() < 52 || file.left(4) != QByteArray("\x7f" "ELF", 4)) {
        result.errorMessage = "Not an ELF file.";
        return result;
    }
    if (file[4] != 1 || file[5] != 1) {
        result.errorMessage = "Only 32-bit little-endian ELF files (Cortex-M) are supported.";
        return result;
    }

    const uint32_t phOffset = le32(file, 28);
    const uint16_t phSize = le16(file, 42);
    const uint16_t phCount = le16(file, 44);
    if (phCount == 0 || phSize < 32) {
        result.errorMessage = "The ELF file has no program headers.";
        return result;
    }

    struct Segment { uint32_t address; uint32_t offset; uint32_t size; };
    std::vector<Segment> segments;
    uint64_t lowest = UINT64_MAX;
    uint64_t highest = 0;

    for (uint16_t i = 0; i < phCount; ++i) {
        const uint64_t entry = uint64_t(phOffset) + uint64_t(i) * phSize;
        if (entry + 32 > uint64_t(file.size())) {
            result.errorMessage = "The ELF program header table is truncated.";
            return result;
        }
        const int e = static_cast<int>(entry);
        const uint32_t type = le32(file, e);
        const uint32_t offset = le32(file, e + 4);
        const uint32_t physical = le32(file, e + 12);  // load address: where it lives in flash
        const uint32_t fileSize = le32(file, e + 16);
        if (type != 1 || fileSize == 0) continue;       // PT_LOAD with content only

        if (uint64_t(offset) + fileSize > uint64_t(file.size())) {
            result.errorMessage = "An ELF segment lies outside the file.";
            return result;
        }
        segments.push_back({physical, offset, fileSize});
        lowest = std::min<uint64_t>(lowest, physical);
        highest = std::max<uint64_t>(highest, uint64_t(physical) + fileSize);
    }

    if (segments.empty()) {
        result.errorMessage = "The ELF file contains no loadable data.";
        return result;
    }

    result.success = true;
    result.baseAddress = static_cast<uint32_t>(lowest);
    result.size = static_cast<uint32_t>(highest - lowest);

    if (extractData && result.size <= 16 * 1024 * 1024) {
        result.data.fill(static_cast<char>(0xFF), static_cast<int>(result.size));
        for (const Segment& s : segments) {
            result.data.replace(static_cast<int>(s.address - lowest), static_cast<int>(s.size),
                                file.mid(static_cast<int>(s.offset), static_cast<int>(s.size)));
        }
    }
    return result;
}

HexParseResult HexParser::analyzeElfFile(const QString& filePath, bool extractData) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        HexParseResult result;
        result.errorMessage = "Failed to open ELF file: " + filePath;
        return result;
    }
    return parseElf(file.readAll(), extractData);
}
