#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <cstdint>

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
    virtual bool connectTarget(const QString& chipName, uint32_t speedKhz, const QString& probeId = QString()) = 0;
    virtual void disconnectTarget() = 0;
    virtual bool isConnected() const = 0;
    virtual bool isBusy() const = 0;

    virtual void eraseTarget(bool fullErase = true) = 0;
    virtual void programFile(const QString& filePath, uint32_t baseAddress, bool verify = true) = 0;
    virtual void verifyFile(const QString& filePath, uint32_t baseAddress) = 0;
    virtual void resetTarget(bool halt = false) = 0;
    virtual void cancelCurrentOperation() = 0;

    // Asynchronous: the result arrives through chipListReady().
    virtual void requestChipList() = 0;
    virtual QList<ProbeInfo> listAvailableProbes() = 0;

signals:
    void connected(bool success, const QString& chipInfo);
    void disconnected();
    void operationStarted(const QString& actionName);
    void progressChanged(int percentage, const QString& statusText);
    void logMessage(const QString& message, EngineLogLevel level);
    void operationFinished(bool success, const QString& message);
    void chipListReady(bool success, const QList<ChipEntry>& chips, const QString& error);
};
