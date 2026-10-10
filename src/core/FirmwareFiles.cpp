#include "FirmwareFiles.h"
#include "HexParser.h"
#include <QFileInfo>

bool FirmwareFiles::isFirmwareFile(const QString& path) {
    const FileFormat format = HexParser::detectFormat(path);
    return format == FileFormat::IntelHex || format == FileFormat::RawBinary || format == FileFormat::Elf;
}

bool FirmwareFiles::looksLikeBootloader(const QString& path) {
    const QString name = QFileInfo(path).completeBaseName().toLower();
    return name.contains("boot") || name.endsWith("_bl") || name.startsWith("bl_") || name == "bl";
}

QList<FirmwareFiles::SlotAssignment> FirmwareFiles::assignDroppedFiles(const QStringList& files, bool* ignoredExtra) {
    QStringList firmware;
    for (const QString& file : files) {
        if (isFirmwareFile(file)) firmware << file;
    }
    if (ignoredExtra) *ignoredExtra = firmware.size() > 2;

    const bool twoPlainFiles = firmware.size() == 2 && !looksLikeBootloader(firmware[0])
                               && !looksLikeBootloader(firmware[1]);

    QList<SlotAssignment> result;
    for (int i = 0; i < firmware.size() && i < 2; ++i) {
        SlotAssignment a;
        a.file = firmware[i];
        const bool toSlot1 = twoPlainFiles ? (i == 0) : looksLikeBootloader(firmware[i]);
        a.slot = toSlot1 ? 1 : 2;
        result.append(a);
    }
    return result;
}
