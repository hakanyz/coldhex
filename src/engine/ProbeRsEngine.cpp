#include "ProbeRsEngine.h"
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>

ProbeRsEngine::ProbeRsEngine(QObject* parent)
    : IProgrammerEngine(parent),
      m_process(new QProcess(this)),
      m_chipListProcess(new QProcess(this)),
      m_progressRegex(QStringLiteral(R"((\d+)\s*%)"))
{
    connect(m_process, &QProcess::readyReadStandardOutput, this, &ProbeRsEngine::onProcessReadyReadStdOut);
    connect(m_process, &QProcess::readyReadStandardError, this, &ProbeRsEngine::onProcessReadyReadStdErr);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ProbeRsEngine::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &ProbeRsEngine::onProcessErrorOccurred);

    connect(m_chipListProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ProbeRsEngine::onChipListFinished);
    connect(m_chipListProcess, &QProcess::errorOccurred, this, &ProbeRsEngine::onChipListErrorOccurred);

    // The probe-rs installer puts the binary in ~/.cargo/bin, which is not always on PATH.
    if (QStandardPaths::findExecutable("probe-rs").isEmpty()) {
        const QString fallback = QStandardPaths::findExecutable("probe-rs", {QDir::homePath() + "/.cargo/bin"});
        if (!fallback.isEmpty()) m_executablePath = fallback;
    }
}

ProbeRsEngine::~ProbeRsEngine() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

void ProbeRsEngine::setExecutablePath(const QString& path) {
    m_executablePath = path;
}

QString ProbeRsEngine::executablePath() const {
    return m_executablePath;
}

bool ProbeRsEngine::isConnected() const {
    return m_connected;
}

bool ProbeRsEngine::isBusy() const {
    return m_state != EngineState::Idle;
}

void ProbeRsEngine::configureTarget(const QString& chipName, uint32_t speedKhz, const QString& probeId,
                                    const QString& chipDescriptionPath) {
    const bool targetChanged = (chipName != m_currentChip) || (probeId != m_currentProbeId)
                               || (chipDescriptionPath != m_currentChipDescriptionPath);

    m_currentChip = chipName;
    m_currentSpeedKhz = speedKhz;
    m_currentProbeId = probeId;
    m_currentChipDescriptionPath = chipDescriptionPath;

    if (targetChanged && m_connected && !isBusy()) {
        m_connected = false;
        emit disconnected();
    }
}

// Arguments every probe-rs command needs to find the right chip and probe.
QStringList ProbeRsEngine::targetArgs() const {
    // --non-interactive: probe-rs must never wait for a probe choice on a terminal we do not have.
    QStringList args = {"--chip", m_currentChip, "--non-interactive"};
    if (!m_currentChipDescriptionPath.isEmpty()) {
        args << "--chip-description-path" << m_currentChipDescriptionPath;
    }
    if (m_currentSpeedKhz > 0) {
        args << "--speed" << QString::number(m_currentSpeedKhz);
    }
    if (!m_currentProbeId.isEmpty()) {
        args << "--probe" << m_currentProbeId;
    }
    return args;
}

// probe-rs only honours --base-address together with --binary-format bin.
QStringList ProbeRsEngine::imageFormatArgs(const QString& filePath, uint32_t baseAddress) {
    const QString suffix = QFileInfo(filePath).suffix();
    if (suffix.compare("bin", Qt::CaseInsensitive) == 0) {
        return {"--binary-format", "bin", "--base-address", QString("0x%1").arg(baseAddress, 8, 16, QLatin1Char('0'))};
    }
    if (suffix.compare("hex", Qt::CaseInsensitive) == 0 || suffix.compare("ihex", Qt::CaseInsensitive) == 0) {
        return {"--binary-format", "hex"};
    }
    return {};
}

bool ProbeRsEngine::connectTarget(const QString& chipName, uint32_t speedKhz, const QString& probeId) {
    if (isBusy()) return false;

    // Keep the chip description chosen by configureTarget(); only chip, speed and probe may change here.
    configureTarget(chipName, speedKhz, probeId, m_currentChipDescriptionPath);

    QStringList args = QStringList{"info"} + targetArgs();
    runCommand(args, EngineState::Connecting, QString("Connecting to target: %1 (%2 kHz)").arg(chipName).arg(speedKhz));
    return true;
}

void ProbeRsEngine::disconnectTarget() {
    if (isBusy()) {
        cancelCurrentOperation();
    }
    m_connected = false;
    emit disconnected();
    emit logMessage("Target disconnected.", EngineLogLevel::Info);
}

void ProbeRsEngine::eraseTarget(bool fullErase) {
    if (isBusy()) return;
    Q_UNUSED(fullErase);

    runCommand(QStringList{"erase"} + targetArgs(), EngineState::Erasing, "Erasing target chip (Chip Erase)...");
}

void ProbeRsEngine::programFile(const QString& filePath, uint32_t baseAddress, bool verify) {
    if (isBusy()) return;

    QStringList args = QStringList{"download"} + targetArgs();
    if (verify) {
        args << "--verify";
    }
    args += imageFormatArgs(filePath, baseAddress);
    args << filePath;

    runCommand(args, EngineState::Programming, QString("Flashing: %1").arg(QFileInfo(filePath).fileName()));
}

void ProbeRsEngine::verifyFile(const QString& filePath, uint32_t baseAddress) {
    if (isBusy()) return;

    QStringList args = QStringList{"verify"} + targetArgs();
    args += imageFormatArgs(filePath, baseAddress);
    args << filePath;

    runCommand(args, EngineState::Verifying, QString("Verifying: %1").arg(QFileInfo(filePath).fileName()));
}

void ProbeRsEngine::resetTarget(bool halt) {
    if (isBusy()) return;

    QStringList args = QStringList{"reset"} + targetArgs();
    if (halt) {
        args << "--halt";
    }

    runCommand(args, EngineState::Resetting, "Resetting target...");
}

void ProbeRsEngine::cancelCurrentOperation() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_cancelRequested = true;
        m_process->kill();
    }
}

void ProbeRsEngine::requestChipList() {
    if (m_chipListProcess->state() != QProcess::NotRunning) return;
    m_chipListProcess->start(m_executablePath, {"chip", "list"});
}

QList<ChipEntry> ProbeRsEngine::parseChipList(const QString& output) {
    // Family lines start at column 0, "Variants:" is indented by 4, chip names by 8.
    QList<ChipEntry> chips;
    QString family;
    const QStringList lines = output.split('\n');
    for (const QString& rawLine : lines) {
        const QString line = QString(rawLine).remove('\r');
        if (line.trimmed().isEmpty()) continue;

        int indent = 0;
        while (indent < line.size() && line[indent] == ' ') ++indent;

        if (indent == 0) {
            if (!line.startsWith("Available chips")) family = line.trimmed();
        } else if (indent >= 8) {
            chips.append({family, line.trimmed()});
        }
    }
    return chips;
}

QList<ProbeInfo> ProbeRsEngine::parseProbeList(const QString& output) {
    // Example line: "[0]: STLink V2 -- 0483:3748: (ST-LINK)"; the serial number may follow the PID.
    static const QRegularExpression lineRegex(
        QStringLiteral(R"(^\[\d+\]:\s*(.+?)\s+--\s+([0-9A-Fa-f]{4}):([0-9A-Fa-f]{4})(?::([^\s:]*))?)"));

    QList<ProbeInfo> probes;
    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QRegularExpressionMatch m = lineRegex.match(line.trimmed());
        if (!m.hasMatch()) continue;

        ProbeInfo info;
        info.identifier = line.trimmed();
        info.displayName = m.captured(1).trimmed();
        info.vendorId = m.captured(2).toUpper();
        info.productId = m.captured(3).toUpper();
        info.serialNumber = m.captured(4);
        probes.append(info);
    }
    return probes;
}

QList<ProbeInfo> ProbeRsEngine::listAvailableProbes() {
    QProcess proc;
    proc.start(m_executablePath, {"list"});
    if (!proc.waitForStarted(3000)) {
        emit logMessage(QString("Cannot run '%1'. Install probe-rs and make sure it is in PATH.").arg(m_executablePath),
                        EngineLogLevel::Error);
        return {};
    }
    if (!proc.waitForFinished(5000)) {
        proc.kill();
        return {};
    }

    return parseProbeList(QString::fromUtf8(proc.readAllStandardOutput()));
}

void ProbeRsEngine::runCommand(const QStringList& args, EngineState targetState, const QString& actionDescription) {
    m_state = targetState;
    m_cancelRequested = false;
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_currentActionDescription = actionDescription;

    emit operationStarted(actionDescription);
    emit logMessage(QString("[CMD] %1 %2").arg(m_executablePath, args.join(' ')), EngineLogLevel::Debug);

    m_process->start(m_executablePath, args);
}

void ProbeRsEngine::drainOutput(QByteArray& buffer, const QByteArray& chunk, bool isError, bool flushRemainder) {
    buffer.append(chunk);

    // probe-rs rewrites progress lines using '\r', so both '\r' and '\n' end a line.
    int start = 0;
    for (int i = 0; i < buffer.size(); ++i) {
        if (buffer[i] == '\r' || buffer[i] == '\n') {
            parseOutputLine(QString::fromUtf8(buffer.mid(start, i - start)).trimmed(), isError);
            start = i + 1;
        }
    }
    buffer.remove(0, start);

    if (flushRemainder && !buffer.isEmpty()) {
        parseOutputLine(QString::fromUtf8(buffer).trimmed(), isError);
        buffer.clear();
    }
}

void ProbeRsEngine::onProcessReadyReadStdOut() {
    drainOutput(m_stdoutBuffer, m_process->readAllStandardOutput(), false);
}

void ProbeRsEngine::onProcessReadyReadStdErr() {
    drainOutput(m_stderrBuffer, m_process->readAllStandardError(), true);
}

void ProbeRsEngine::parseOutputLine(const QString& line, bool isError) {
    if (line.isEmpty()) return;

    auto match = m_progressRegex.match(line);
    if (match.hasMatch()) {
        int percent = match.captured(1).toInt();
        emit progressChanged(percent, line);
    }

    EngineLogLevel level = isError ? EngineLogLevel::Warning : EngineLogLevel::Info;
    if (line.contains("error", Qt::CaseInsensitive) || line.contains("failed", Qt::CaseInsensitive)) {
        level = EngineLogLevel::Error;
    } else if (line.contains("success", Qt::CaseInsensitive) || line.contains("Finished", Qt::CaseInsensitive)) {
        level = EngineLogLevel::Success;
    }

    emit logMessage(line, level);
}

void ProbeRsEngine::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    drainOutput(m_stdoutBuffer, m_process->readAllStandardOutput(), false, true);
    drainOutput(m_stderrBuffer, m_process->readAllStandardError(), true, true);

    if (m_cancelRequested) {
        m_cancelRequested = false;
        m_state = EngineState::Idle;
        emit logMessage("Operation cancelled by user.", EngineLogLevel::Warning);
        emit operationFinished(false, "Operation cancelled.");
        return;
    }

    bool success = (exitCode == 0 && exitStatus == QProcess::NormalExit);
    EngineState completedState = m_state;
    m_state = EngineState::Idle;

    if (completedState == EngineState::Connecting) {
        m_connected = success;
        emit connected(success, success ? m_currentChip : "Connection failed.");
    }

    QString msg = success ? QString("%1 completed successfully.").arg(m_currentActionDescription)
                          : QString("%1 failed (Exit Code: %2).").arg(m_currentActionDescription).arg(exitCode);

    emit progressChanged(success ? 100 : 0, msg);
    emit logMessage(msg, success ? EngineLogLevel::Success : EngineLogLevel::Error);
    emit operationFinished(success, msg);
}

void ProbeRsEngine::onProcessErrorOccurred(QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
        QString msg = QString("Failed to start '%1'. Please make sure probe-rs CLI is installed and available in PATH.").arg(m_executablePath);
        emit logMessage(msg, EngineLogLevel::Error);
        m_state = EngineState::Idle;
        emit operationFinished(false, msg);
    }
}

void ProbeRsEngine::onChipListFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    const QString output = QString::fromUtf8(m_chipListProcess->readAllStandardOutput());
    if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
        emit chipListReady(false, {}, QString("'probe-rs chip list' failed (exit code %1).").arg(exitCode));
        return;
    }

    const QList<ChipEntry> chips = parseChipList(output);
    if (chips.isEmpty()) {
        emit chipListReady(false, {}, "'probe-rs chip list' returned no chips.");
        return;
    }
    emit chipListReady(true, chips, QString());
}

void ProbeRsEngine::onChipListErrorOccurred(QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
        emit chipListReady(false, {},
                           QString("Cannot run '%1'. Install probe-rs and make sure it is in PATH.").arg(m_executablePath));
    }
}
