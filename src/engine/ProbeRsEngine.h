#pragma once

#include "FlashProgressParser.h"
#include "IProgrammerEngine.h"
#include <QProcess>
#include <QRegularExpression>

enum class EngineState {
    Idle,
    Connecting,
    Erasing,
    Unlocking,
    Locking,
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
    void unlockTarget() override;
    void lockTarget() override;
    void requestCapabilities(const QString& chipName) override;
    void programFile(const QString& filePath, uint32_t baseAddress, bool verify = true) override;
    void verifyFile(const QString& filePath, uint32_t baseAddress) override;
    void readMemory(uint64_t address, uint64_t byteCount, const QString& outputFile) override;
    void resetTarget(bool halt = false) override;
    void cancelCurrentOperation() override;

    void requestChipList() override;
    void requestProbeList() override;

    // The arguments that select chip, probe, protocol and speed for every probe-rs command.
    static QStringList targetArgsFor(const TargetSettings& target);

    // True while the probe a connection was made with is still in the list of probes. An empty
    // `connectedSelector` means "whichever probe probe-rs picked", so any probe will do.
    static bool probeStillListed(const QString& connectedSelector, const QStringList& listedSelectors);

    // Parses the output of "probe-rs list".
    static QList<ProbeInfo> parseProbeList(const QString& output);

    // Parses the output of "probe-rs chip info <name>".
    static ChipMemoryMap parseChipInfo(const QString& output);

    // Parses the output of "probe-rs chip list".
    static QList<ChipEntry> parseChipList(const QString& output);

    // The arguments of the coldhex-flash helper that select chip, probe, protocol and speed.
    static QStringList helperTargetArgsFor(const TargetSettings& target);

    // "hex", "bin" or "elf" for the file name, or an empty string for a type the helper cannot write.
    static QString helperFormat(const QString& filePath);

    // "--format hex" (and "--base-address 0x..." for a bin file), or an empty list for a type the
    // helper cannot handle.
    static QStringList helperFormatArgs(const QString& filePath, uint32_t baseAddress);

    void setExecutablePath(const QString& path);
    QString executablePath() const;

    // The coldhex-flash helper reports real progress while erasing and programming. Without it
    // (empty path) the same work is done by the probe-rs command line tool, without a percentage.
    void setHelperPath(const QString& path);
    QString helperPath() const;

private slots:
    void onProcessReadyReadStdOut();
    void onProcessReadyReadStdErr();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessErrorOccurred(QProcess::ProcessError error);
    void onChipListFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onChipListErrorOccurred(QProcess::ProcessError error);
    void onProbeListFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProbeListErrorOccurred(QProcess::ProcessError error);
    void onProtectionFinished();
    void onCapabilitiesFinished();

private:
    void runCommand(const QStringList& args, EngineState targetState, const QString& actionDescription,
                    bool useHelper = false);
    void parseOutputLine(const QString& line, bool isError);
    QStringList targetArgs() const;
    static QStringList imageFormatArgs(const QString& filePath, uint32_t baseAddress);
    void drainOutput(QByteArray& buffer, const QByteArray& chunk, bool isError, bool flushRemainder = false);

    QString m_executablePath = "probe-rs";
    QString m_helperPath;
    QProcess* m_process = nullptr;
    QProcess* m_chipListProcess = nullptr;
    QProcess* m_probeListProcess = nullptr;
    QProcess* m_protectionProcess = nullptr;  // reads the protection level once after a connect
    QProcess* m_capabilitiesProcess = nullptr;
    QString m_capabilitiesChip;
    QByteArray m_protectionOutput;
    EngineState m_state = EngineState::Idle;
    bool m_connected = false;
    bool m_cancelRequested = false;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    QStringList m_outputLines;  // output of the running command, used to explain failures

    TargetSettings m_target;
    QString m_currentActionDescription;

    QRegularExpression m_progressRegex;

    // State of a running coldhex-flash command.
    bool m_helperRunning = false;
    bool m_helperResultSeen = false;
    bool m_helperResultOk = false;
    FlashProgressParser m_helperParser;
};
