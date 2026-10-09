#include "ProbeRsEngine.h"
#include <QDebug>
#include <QFileInfo>

ProbeRsEngine::ProbeRsEngine(QObject* parent)
    : IProgrammerEngine(parent),
      m_process(new QProcess(this)),
      m_progressRegex(QStringLiteral(R"((\d+)\s*%)"))
{
    connect(m_process, &QProcess::readyReadStandardOutput, this, &ProbeRsEngine::onProcessReadyReadStdOut);
    connect(m_process, &QProcess::readyReadStandardError, this, &ProbeRsEngine::onProcessReadyReadStdErr);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ProbeRsEngine::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &ProbeRsEngine::onProcessErrorOccurred);
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

bool ProbeRsEngine::connectTarget(const QString& chipName, uint32_t speedKhz, const QString& probeId) {
    if (isBusy()) return false;

    m_currentChip = chipName;
    m_currentSpeedKhz = speedKhz;
    m_currentProbeId = probeId;

    QStringList args = {"info", "--chip", chipName};
    if (speedKhz > 0) {
        args << "--speed" << QString::number(speedKhz);
    }
    if (!probeId.isEmpty()) {
        args << "--probe" << probeId;
    }

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

    QStringList args = {"erase", "--chip", m_currentChip};
    if (m_currentSpeedKhz > 0) {
        args << "--speed" << QString::number(m_currentSpeedKhz);
    }
    if (!m_currentProbeId.isEmpty()) {
        args << "--probe" << m_currentProbeId;
    }

    runCommand(args, EngineState::Erasing, "Erasing target chip (Chip Erase)...");
}

void ProbeRsEngine::programFile(const QString& filePath, uint32_t baseAddress, bool verify) {
    if (isBusy()) return;

    QStringList args = {"download", "--chip", m_currentChip};
    if (m_currentSpeedKhz > 0) {
        args << "--speed" << QString::number(m_currentSpeedKhz);
    }
    if (!m_currentProbeId.isEmpty()) {
        args << "--probe" << m_currentProbeId;
    }
    if (verify) {
        args << "--verify";
    }

    QFileInfo fi(filePath);
    if (fi.suffix().compare("bin", Qt::CaseInsensitive) == 0 && baseAddress != 0) {
        args << "--base-address" << QString("0x%1").arg(baseAddress, 8, 16, QLatin1Char('0'));
    }

    args << filePath;

    runCommand(args, EngineState::Programming, QString("Flashing: %1").arg(fi.fileName()));
}

void ProbeRsEngine::resetTarget(bool halt) {
    if (isBusy()) return;

    QStringList args = {"reset", "--chip", m_currentChip};
    if (m_currentSpeedKhz > 0) {
        args << "--speed" << QString::number(m_currentSpeedKhz);
    }
    if (!m_currentProbeId.isEmpty()) {
        args << "--probe" << m_currentProbeId;
    }
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

QStringList ProbeRsEngine::listSupportedChips(const QString& filter) {
    QProcess proc;
    QStringList args = {"chip", "list"};
    proc.start(m_executablePath, args);
    if (!proc.waitForFinished(5000)) {
        return {};
    }

    QString output = QString::fromUtf8(proc.readAllStandardOutput());
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    QStringList results;

    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (filter.isEmpty() || trimmed.contains(filter, Qt::CaseInsensitive)) {
            results << trimmed;
        }
    }
    return results;
}

QList<ProbeInfo> ProbeRsEngine::listAvailableProbes() {
    QProcess proc;
    QStringList args = {"list"};
    proc.start(m_executablePath, args);
    if (!proc.waitForFinished(5000)) {
        return {};
    }

    QString output = QString::fromUtf8(proc.readAllStandardOutput());
    QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    QList<ProbeInfo> probes;

    for (const QString& line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith("Probes:") || trimmed.startsWith("No probes found")) {
            continue;
        }
        ProbeInfo info;
        info.identifier = trimmed;
        probes.append(info);
    }
    return probes;
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
