#include "ProbeRsEngine.h"
#include "ProbeRsLocator.h"
#include "ChipDescription.h"
#include "ConnectionAdvice.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>

ProbeRsEngine::ProbeRsEngine(QObject* parent)
    : IProgrammerEngine(parent),
      m_process(new QProcess(this)),
      m_chipListProcess(new QProcess(this)),
      m_probeListProcess(new QProcess(this)),
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
    connect(m_probeListProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &ProbeRsEngine::onProbeListFinished);
    connect(m_probeListProcess, &QProcess::errorOccurred, this, &ProbeRsEngine::onProbeListErrorOccurred);

    // Prefer a probe-rs shipped with ColdHex, then PATH, then the installer's default folder.
    const QString found = ProbeRsLocator::find(QCoreApplication::applicationDirPath(), QDir::homePath());
    if (!found.isEmpty()) m_executablePath = found;
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

void ProbeRsEngine::configureTarget(const TargetSettings& settings) {
    const bool targetChanged = settings.chipName != m_target.chipName
                               || settings.probeId != m_target.probeId
                               || settings.chipDescriptionPath != m_target.chipDescriptionPath
                               || settings.protocol != m_target.protocol
                               || settings.connectUnderReset != m_target.connectUnderReset;

    m_target = settings;

    if (targetChanged && m_connected && !isBusy()) {
        m_connected = false;
        emit disconnected();
    }
}

// Arguments every probe-rs command needs to find the right chip and probe.
QStringList ProbeRsEngine::targetArgs() const {
    return targetArgsFor(m_target);
}

QStringList ProbeRsEngine::targetArgsFor(const TargetSettings& target) {
    // --non-interactive: probe-rs must never wait for a probe choice on a terminal we do not have.
    QStringList args = {"--chip", target.chipName, "--non-interactive"};
    if (!target.chipDescriptionPath.isEmpty()) {
        args << "--chip-description-path" << target.chipDescriptionPath;
    }
    args << "--protocol" << (target.protocol == DebugProtocol::Jtag ? "jtag" : "swd");
    if (target.speedKhz > 0) {
        args << "--speed" << QString::number(target.speedKhz);
    }
    if (!target.probeId.isEmpty()) {
        args << "--probe" << target.probeId;
    }
    if (target.connectUnderReset) {
        args << "--connect-under-reset";
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
    if (suffix.compare("elf", Qt::CaseInsensitive) == 0 || suffix.compare("axf", Qt::CaseInsensitive) == 0) {
        return {"--binary-format", "elf"};
    }
    return {};
}

bool ProbeRsEngine::connectTarget(const QString& chipName, uint32_t speedKhz, const QString& probeId) {
    if (isBusy()) return false;

    // Protocol, chip description and reset behaviour stay as set by configureTarget().
    TargetSettings settings = m_target;
    settings.chipName = chipName;
    settings.speedKhz = speedKhz;
    settings.probeId = probeId;
    configureTarget(settings);

    // "probe-rs info" ignores --chip, so it would not notice a wrongly selected chip. Reading one
    // word of RAM attaches with the selected chip and also works when flash read-out is locked.
    QStringList args;
    const ChipMemoryMap memory = queryMemoryMap(chipName, m_target.chipDescriptionPath);
    if (const MemoryRegion* ram = memory.firstRam()) {
        args = QStringList{"read"} + targetArgs()
               + QStringList{"b32", QString("0x%1").arg(ram->start, 8, 16, QLatin1Char('0')), "1"};
    } else {
        args = QStringList{"info"} + targetArgs();
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

void ProbeRsEngine::requestProbeList() {
    if (m_probeListProcess->state() != QProcess::NotRunning) return;
    m_probeListProcess->start(m_executablePath, {"list"});
}

void ProbeRsEngine::onProbeListFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    const QString output = QString::fromUtf8(m_probeListProcess->readAllStandardOutput());
    if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
        emit probeListReady(false, {}, QString("'probe-rs list' failed (exit code %1).").arg(exitCode));
        return;
    }
    emit probeListReady(true, parseProbeList(output), QString());
}

void ProbeRsEngine::onProbeListErrorOccurred(QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
        emit probeListReady(false, {},
                            QString("Cannot run '%1'. Install probe-rs and make sure it is in PATH.").arg(m_executablePath));
    }
}

void ProbeRsEngine::runCommand(const QStringList& args, EngineState targetState, const QString& actionDescription) {
    m_state = targetState;
    m_cancelRequested = false;
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_outputLines.clear();
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

    if (m_outputLines.size() < 200) m_outputLines << line;

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

    // For a failed command explain the probable cause in plain words.
    QString advice;
    if (!success) {
        advice = ConnectionAdvice::explain(m_outputLines.join('\n'));
        if (advice.isEmpty() && completedState == EngineState::Connecting) advice = ConnectionAdvice::generic();
        if (!advice.isEmpty()) emit logMessage(advice, EngineLogLevel::Warning);
    }

    if (completedState == EngineState::Connecting) {
        m_connected = success;
        emit connected(success, success ? m_target.chipName : advice);
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

ChipMemoryMap ProbeRsEngine::parseChipInfo(const QString& output) {
    // Lines look like "NVM: 0x08000000..0x08100000 (1.0 MiB)" or "RAM: ...".
    static const QRegularExpression regionRegex(R"(^\s*(NVM|RAM):\s*(0x[0-9A-Fa-f]+)\.\.(0x[0-9A-Fa-f]+))");

    ChipMemoryMap map;
    const QStringList lines = output.split('\n');
    for (const QString& line : lines) {
        const QRegularExpressionMatch m = regionRegex.match(line);
        if (!m.hasMatch()) continue;

        MemoryRegion r;
        r.nonVolatile = (m.captured(1) == "NVM");
        r.start = m.captured(2).toULongLong(nullptr, 16);
        const uint64_t end = m.captured(3).toULongLong(nullptr, 16);
        r.size = end > r.start ? end - r.start : 0;
        if (r.size > 0) map.regions.append(r);
    }
    return map;
}

ChipMemoryMap ProbeRsEngine::queryMemoryMap(const QString& chipName, const QString& chipDescriptionPath) {
    if (chipName.isEmpty()) return {};

    if (!chipDescriptionPath.isEmpty()) {
        return ChipDescription::memoryMapFromFile(chipDescriptionPath, chipName);
    }

    QProcess proc;
    proc.start(m_executablePath, {"chip", "info", chipName});
    if (!proc.waitForFinished(5000)) {
        proc.kill();
        return {};
    }
    if (proc.exitCode() != 0) return {};
    return parseChipInfo(QString::fromUtf8(proc.readAllStandardOutput()));
}

void ProbeRsEngine::readMemory(uint64_t address, uint64_t byteCount, const QString& outputFile) {
    if (isBusy() || byteCount == 0) return;

    // 32-bit words are much faster than bytes; fall back to bytes for odd sizes or addresses.
    const bool wordAligned = (address % 4 == 0) && (byteCount % 4 == 0);
    const QString width = wordAligned ? "b32" : "b8";
    const uint64_t count = wordAligned ? byteCount / 4 : byteCount;

    QStringList args = QStringList{"read"} + targetArgs();
    args << "--output" << outputFile << "--format" << "binary"
         << width << QString("0x%1").arg(address, 8, 16, QLatin1Char('0')) << QString::number(count);

    runCommand(args, EngineState::Reading,
               QString("Reading %1 bytes from 0x%2").arg(byteCount).arg(address, 8, 16, QLatin1Char('0')));
}
