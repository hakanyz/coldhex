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
    Reading,
    Resetting
};

class ProbeRsEngine : public IProgrammerEngine {
    Q_OBJECT

public:
    explicit ProbeRsEngine(QObject* parent = nullptr);
    ~ProbeRsEngine() override;

    void configureTarget(const TargetSettings& settings) override;
    ChipMemoryMap queryMemoryMap(const QString& chipName, const QString& chipDescriptionPath) override;
    bool connectTarget(const QString& chipName, uint32_t speedKhz, const QString& probeId = QString()) override;
    void disconnectTarget() override;
    bool isConnected() const override;
    bool isBusy() const override;

    void eraseTarget(bool fullErase = true) override;
    void programFile(const QString& filePath, uint32_t baseAddress, bool verify = true) override;
    void verifyFile(const QString& filePath, uint32_t baseAddress) override;
    void readMemory(uint64_t address, uint64_t byteCount, const QString& outputFile) override;
    void resetTarget(bool halt = false) override;
    void cancelCurrentOperation() override;

    void requestChipList() override;
    void requestProbeList() override;

    // The arguments that select chip, probe, protocol and speed for every probe-rs command.
    static QStringList targetArgsFor(const TargetSettings& target);

    // Parses the output of "probe-rs list".
    static QList<ProbeInfo> parseProbeList(const QString& output);

    // Parses the output of "probe-rs chip info <name>".
    static ChipMemoryMap parseChipInfo(const QString& output);

    // Parses the output of "probe-rs chip list".
    static QList<ChipEntry> parseChipList(const QString& output);

    void setExecutablePath(const QString& path);
    QString executablePath() const;

private slots:
    void onProcessReadyReadStdOut();
    void onProcessReadyReadStdErr();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessErrorOccurred(QProcess::ProcessError error);
    void onChipListFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onChipListErrorOccurred(QProcess::ProcessError error);
    void onProbeListFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProbeListErrorOccurred(QProcess::ProcessError error);

private:
    void runCommand(const QStringList& args, EngineState targetState, const QString& actionDescription);
    void parseOutputLine(const QString& line, bool isError);
    QStringList targetArgs() const;
    static QStringList imageFormatArgs(const QString& filePath, uint32_t baseAddress);
    void drainOutput(QByteArray& buffer, const QByteArray& chunk, bool isError, bool flushRemainder = false);

    QString m_executablePath = "probe-rs";
    QProcess* m_process = nullptr;
    QProcess* m_chipListProcess = nullptr;
    QProcess* m_probeListProcess = nullptr;
    EngineState m_state = EngineState::Idle;
    bool m_connected = false;
    bool m_cancelRequested = false;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;

    TargetSettings m_target;
    QString m_currentActionDescription;

    QRegularExpression m_progressRegex;
};
