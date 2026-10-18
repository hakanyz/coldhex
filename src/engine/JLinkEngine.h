#pragma once

#include "IProgrammerEngine.h"
#include "JLinkLogProgress.h"
#include <QProcess>
#include <QStringList>
#include <QTemporaryDir>

class QTimer;

// Drives SEGGER's J-Link Commander (see JLinkCommands.h). Every command is one run of the
// Commander with a small script. This engine exists so that a J-Link can keep SEGGER's USB
// driver (Keil needs it) instead of being switched to WinUSB for probe-rs.
// The chip data (memory map) and the probe and chip lists come from another engine, probe-rs.
class JLinkEngine : public IProgrammerEngine {
    Q_OBJECT

public:
    JLinkEngine(const QString& commanderPath, IProgrammerEngine* chipData, QObject* parent = nullptr);
    ~JLinkEngine() override;

    void configureTarget(const TargetSettings& settings) override;
    ChipMemoryMap queryMemoryMap(const QString& chipName, const QString& chipDescriptionPath) override;
    bool connectTarget(const QString& chipName, uint32_t speedKhz, const QString& probeId = QString()) override;
    void disconnectTarget() override;
    bool isConnected() const override { return m_connected; }
    bool isBusy() const override { return m_state != State::Idle; }

    void eraseTarget(bool fullErase = true) override;
    void unlockTarget() override;
    void lockTarget() override;
    void requestCapabilities(const QString& chipName) override;
    void programFile(const QString& filePath, uint32_t baseAddress, bool verify = true) override;
    void verifyFile(const QString& filePath, uint32_t baseAddress) override;
    void readMemory(uint64_t address, uint64_t byteCount, const QString& outputFile) override;
    void resetTarget(bool halt = false) override;
    void cancelCurrentOperation() override;

    QString commanderPath() const { return m_commander; }

    // The chip and probe lists come from probe-rs.
    void requestChipList() override {}
    void requestProbeList() override {}

private slots:
    void onFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onErrorOccurred(QProcess::ProcessError error);
    void onProgressTick();

private:
    enum class State { Idle, Connecting, Erasing, Programming, Verifying, Reading, Resetting };

    // Writes the script, starts the Commander and reports the start.
    bool run(const QString& script, State state, const QString& action);
    bool joinReadParts(QString* error);
    void clearReadJob();
    void startReadingParts(uint64_t address, uint64_t byteCount, State state, const QString& action);
    bool compareVerifyParts(QString* error);
    void followLog();

    QString m_commander;
    IProgrammerEngine* m_chipData;
    QProcess* m_process;
    QTimer* m_progressTimer;
    QTemporaryDir m_scratch;  // scripts and the pieces of a read

    TargetSettings m_target;
    State m_state = State::Idle;
    bool m_connected = false;
    bool m_cancelRequested = false;
    QString m_action;

    // The running read: the pieces the Commander writes and where they are joined.
    QString m_readOutput;
    QStringList m_readParts;
    QList<qint64> m_readPartSizes;
    uint64_t m_readBase = 0;  // address of the first part
    int m_readRetries = 0;    // how often the unfinished parts were tried again

    // The running verify: what the chip has to contain, compared after the pieces are read.
    QByteArray m_verifyImage;
    uint64_t m_verifyBase = 0;

    // The running erase or program: the log file the Commander writes while it works.
    JLinkLogProgress m_logProgress;
    QString m_logFile;
    qint64 m_logPos = 0;
    QByteArray m_logPending;
};
