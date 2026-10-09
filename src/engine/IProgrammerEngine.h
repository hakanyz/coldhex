#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <cstdint>

enum class EngineLogLevel {
    Debug,
    Info,
    Warning,
    Error,
    Success
};

struct ProbeInfo {
    QString identifier;
    QString vendorId;
    QString productId;
    QString serialNumber;
};

class IProgrammerEngine : public QObject {
    Q_OBJECT

public:
    explicit IProgrammerEngine(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~IProgrammerEngine() = default;

    virtual bool connectTarget(const QString& chipName, uint32_t speedKhz, const QString& probeId = QString()) = 0;
    virtual void disconnectTarget() = 0;
    virtual bool isConnected() const = 0;
    virtual bool isBusy() const = 0;

    virtual void eraseTarget(bool fullErase = true) = 0;
    virtual void programFile(const QString& filePath, uint32_t baseAddress, bool verify = true) = 0;
    virtual void resetTarget(bool halt = false) = 0;
    virtual void cancelCurrentOperation() = 0;

    virtual QStringList listSupportedChips(const QString& filter = QString()) = 0;
    virtual QList<ProbeInfo> listAvailableProbes() = 0;

signals:
    void connected(bool success, const QString& chipInfo);
    void disconnected();
    void operationStarted(const QString& actionName);
    void progressChanged(int percentage, const QString& statusText);
    void logMessage(const QString& message, EngineLogLevel level);
    void operationFinished(bool success, const QString& message);
};
