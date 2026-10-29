#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <cstdint>
#include "ChipMemory.h"

enum class EngineLogLevel {
    Debug,
    Info,
    Warning,
    Error,
    Success
};

struct ChipEntry {
    QString family;  // e.g. "STM32F4 Series"
    QString name;    // name accepted by --chip, e.g. "STM32F407VG"
    QString descriptionFile;  // set for chips that come from a user-supplied YAML file
};

struct ProbeInfo {
    QString identifier;   // raw line from the engine
    QString displayName;  // e.g. "STLink V2"
    QString vendorId;
    QString productId;
    QString serialNumber;

    // Value accepted by probe-rs --probe: VID:PID or VID:PID:Serial.
    QString selector() const {
        QString s = vendorId + ":" + productId;
        if (!serialNumber.isEmpty()) s += ":" + serialNumber;
        return s;
    }
};

enum class DebugProtocol { Swd, Jtag };

// Everything a probe-rs command needs to know about the target and the way to reach it.
struct TargetSettings {
    QString chipName;
    uint32_t speedKhz = 4000;
    QString probeId;               // VID:PID[:serial], empty = let probe-rs pick
    QString chipDescriptionPath;   // YAML for chips probe-rs does not know
    DebugProtocol protocol = DebugProtocol::Swd;
    bool connectUnderReset = false;
};

class IProgrammerEngine : public QObject {
    Q_OBJECT

public:
    explicit IProgrammerEngine(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~IProgrammerEngine() = default;

    // Sets chip, speed and probe used by the following commands. Switching chip or
    // probe drops an existing connection.
    virtual void configureTarget(const TargetSettings& settings) = 0;
    // Memory layout of the configured chip (empty map when unknown). May run a short command.
    virtual ChipMemoryMap queryMemoryMap(const QString& chipName, const QString& chipDescriptionPath) = 0;
    virtual bool connectTarget(const QString& chipName, uint32_t speedKhz, const QString& probeId = QString()) = 0;
    virtual void disconnectTarget() = 0;
    virtual bool isConnected() const = 0;
    virtual bool isBusy() const = 0;
    // Checks quietly in the background whether the connected target still answers. If it does
    // not, the connection is closed and disconnected() is emitted.
    virtual void checkTargetConnection() {}

    virtual void eraseTarget(bool fullErase = true) = 0;
    // Removes the read-out protection (level 1). The chip erases its whole flash while it does so.
    // Finishes with operationFinished(false, ...) when no method is known for the chip.
    virtual void unlockTarget() = 0;
    // Sets the read-out protection to level 1 (never level 2). Reading the flash fails afterwards.
    virtual void lockTarget() = 0;
    // Asks what Lock and Unlock can do for a chip (no probe needed). The answer arrives through
    // capabilitiesReady(); engines that cannot tell never answer, and the window keeps the buttons on.
    virtual void requestCapabilities(const QString& chipName) = 0;
    virtual void programFile(const QString& filePath, uint32_t baseAddress, bool verify = true) = 0;
    virtual void verifyFile(const QString& filePath, uint32_t baseAddress) = 0;
    // Saves `byteCount` bytes starting at `address` to `outputFile` as raw binary.
    virtual void readMemory(uint64_t address, uint64_t byteCount, const QString& outputFile) = 0;
    virtual void resetTarget(bool halt = false) = 0;
    virtual void cancelCurrentOperation() = 0;

    // Asynchronous: the result arrives through chipListReady().
    virtual void requestChipList() = 0;
    // Asynchronous: the result arrives through probeListReady().
    virtual void requestProbeList() = 0;

signals:
    void connected(bool success, const QString& chipInfo);
    // After a connection: the read-out protection of the chip, 0 = off, 1 or 2. Not sent when
    // the level cannot be read for this chip.
    void protectionDetected(int level);
    void capabilitiesReady(const QString& chipName, bool canLock, bool canUnlock);
    void disconnected();
    void operationStarted(const QString& actionName);
    void progressChanged(int percentage, const QString& statusText);
    void logMessage(const QString& message, EngineLogLevel level);
    void operationFinished(bool success, const QString& message);
    void chipListReady(bool success, const QList<ChipEntry>& chips, const QString& error);
    void probeListReady(bool success, const QList<ProbeInfo>& probes, const QString& error);
};
