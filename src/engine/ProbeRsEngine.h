#pragma once

#include "IProgrammerEngine.h"
#include <QProcess>
#include <QRegularExpression>

enum class EngineState {
    Idle,
    Connecting,
    Erasing,
    Programming,
    Verifying,
    Resetting
};

class ProbeRsEngine : public IProgrammerEngine {
    Q_OBJECT

public:
    explicit ProbeRsEngine(QObject* parent = nullptr);
    ~ProbeRsEngine() override;

    bool connectTarget(const QString& chipName, uint32_t speedKhz, const QString& probeId = QString()) override;
    void disconnectTarget() override;
    bool isConnected() const override;
    bool isBusy() const override;

    void eraseTarget(bool fullErase = true) override;
    void programFile(const QString& filePath, uint32_t baseAddress, bool verify = true) override;
    void resetTarget(bool halt = false) override;
    void cancelCurrentOperation() override;

    QStringList listSupportedChips(const QString& filter = QString()) override;
    QList<ProbeInfo> listAvailableProbes() override;

    void setExecutablePath(const QString& path);
    QString executablePath() const;

private slots:
    void onProcessReadyReadStdOut();
    void onProcessReadyReadStdErr();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessErrorOccurred(QProcess::ProcessError error);

private:
    void runCommand(const QStringList& args, EngineState targetState, const QString& actionDescription);
    void parseOutputLine(const QString& line, bool isError);
    void drainOutput(QByteArray& buffer, const QByteArray& chunk, bool isError, bool flushRemainder = false);

    QString m_executablePath = "probe-rs";
    QProcess* m_process = nullptr;
    EngineState m_state = EngineState::Idle;
    bool m_connected = false;
    bool m_cancelRequested = false;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;

    QString m_currentChip;
    uint32_t m_currentSpeedKhz = 4000;
    QString m_currentProbeId;
    QString m_currentActionDescription;

    QRegularExpression m_progressRegex;
};
