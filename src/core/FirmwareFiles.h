#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Decisions about firmware files that do not depend on the user interface.
class FirmwareFiles {
public:
    // True for files ColdHex can flash: Intel HEX, raw binary and ELF (by file extension).
    static bool isFirmwareFile(const QString& path);

    // Names that usually belong to a bootloader: boot.hex, my_bootloader.bin, app_bl.elf ...
    static bool looksLikeBootloader(const QString& path);

    struct SlotAssignment {
        QString file;
        int slot = 2;  // 1 = bootloader, 2 = application
    };

    // Distributes dropped files over the two slots. Bootloader-like names go to slot 1, all
    // others to slot 2. Two files without such a name fill slot 1 and slot 2 in the order given.
    // Files that are not firmware are skipped; at most two files are used.
    static QList<SlotAssignment> assignDroppedFiles(const QStringList& files, bool* ignoredExtra = nullptr);
};
