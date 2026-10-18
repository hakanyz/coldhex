#include "JLinkEngine.h"
#include "JLinkCommands.h"
#include "core/HexParser.h"
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTimer>

namespace {

// A read is done in pieces of this size, one Commander command each; a finished piece is a
// file on disk, which is the only live progress the Commander gives.
constexpr uint64_t kReadChunk = 64 * 1024;

// Lines of the Commander output worth showing in the console after a successful run.
bool isInteresting(const QString& line) {
    static const QRegularExpression keys(
        R"(^(Found |VTref|Firmware:|Hardware version|Erasing|Erase|Programming|Verifying|Comparing|Downloading|Script processing))");
    return keys.match(line).hasMatch();
}

}  // namespace

JLinkEngine::JLinkEngine(const QString& commanderPath, IProgrammerEngine* chipData, QObject* parent)
    : IProgrammerEngine(parent),
      m_commander(commanderPath),
      m_chipData(chipData),
      m_process(new QProcess(this)),
      m_progressTimer(new QTimer(this))
{
    m_process->setProcessChannelMode(QProcess::MergedChannels);  // the Commander writes to both
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &JLinkEngine::onFinished);
    connect(m_process, &QProcess::errorOccurred, this, &JLinkEngine::onErrorOccurred);

    m_progressTimer->setInterval(200);
    connect(m_progressTimer, &QTimer::timeout, this, &JLinkEngine::onProgressTick);
}

JLinkEngine::~JLinkEngine() {
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

void JLinkEngine::configureTarget(const TargetSettings& settings) {
    const bool targetChanged = settings.chipName != m_target.chipName
                               || settings.probeId != m_target.probeId
                               || settings.protocol != m_target.protocol
                               || settings.connectUnderReset != m_target.connectUnderReset;
    m_target = settings;

    if (targetChanged && m_connected && !isBusy()) {
        m_connected = false;
        emit disconnected();
    }
}

ChipMemoryMap JLinkEngine::queryMemoryMap(const QString& chipName, const QString& chipDescriptionPath) {
    return m_chipData->queryMemoryMap(chipName, chipDescriptionPath);
}

bool JLinkEngine::run(const QString& script, State state, const QString& action) {
    if (!m_scratch.isValid()) {
        emit logMessage("Cannot create a temporary folder for the J-Link script.", EngineLogLevel::Error);
        QTimer::singleShot(0, this, [this]() { emit operationFinished(false, "No temporary folder."); });
        return false;
    }
    // Erasing and programming are followed through the log file the Commander writes while it
    // works: its output on the pipe only comes when it has finished.
    QString text = script;
    const bool followsLog = (state == State::Erasing || state == State::Programming);
    if (followsLog) {
        m_logFile = m_scratch.filePath("trace.log");
        QFile::remove(m_logFile);
        m_logPos = 0;
        m_logPending.clear();
        text = JLinkCommands::logScript(m_logFile) + script;
    }

    const QString scriptFile = m_scratch.filePath("command.jlink");
    QFile file(scriptFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        emit logMessage("Cannot write the J-Link script file.", EngineLogLevel::Error);
        QTimer::singleShot(0, this, [this]() { emit operationFinished(false, "Cannot write the script."); });
        return false;
    }
    file.write(text.toUtf8());
    file.close();

    m_state = state;
    m_cancelRequested = false;
    m_action = action;

    const QStringList args = JLinkCommands::arguments(m_target, scriptFile);
    emit operationStarted(action);
    emit logMessage(QString("[CMD] %1 %2").arg(m_commander, args.join(' ')), EngineLogLevel::Debug);
    m_process->start(m_commander, args);

    // Reading and verifying watch the pieces on disk, erasing and programming the log file.
    if (followsLog || state == State::Reading || state == State::Verifying) m_progressTimer->start();
    return true;
}

bool JLinkEngine::connectTarget(const QString& chipName, uint32_t speedKhz, const QString& probeId) {
    if (isBusy()) return false;

    TargetSettings settings = m_target;
    settings.chipName = chipName;
    settings.speedKhz = speedKhz;
    settings.probeId = probeId;
    configureTarget(settings);

    uint64_t ram = 0x20000000;
    if (const MemoryRegion* region = m_chipData->queryMemoryMap(chipName, m_target.chipDescriptionPath).firstRam()) {
        ram = region->start;
    }
    if (m_target.connectUnderReset) {
        emit logMessage("Connect under reset is not supported with the J-Link engine yet and is ignored.",
                        EngineLogLevel::Warning);
    }
    // Above the safe speed the connection also checks that reads are correct at that speed.
    const bool checkLink = m_target.speedKhz > static_cast<uint32_t>(JLinkCommands::kSafeSpeedKhz);
    return run(JLinkCommands::connectScript(ram, checkLink), State::Connecting,
               QString("Connecting to target: %1 (%2 kHz)").arg(chipName).arg(speedKhz));
}

void JLinkEngine::disconnectTarget() {
    if (isBusy()) cancelCurrentOperation();
    m_connected = false;
    emit disconnected();
    emit logMessage("Target disconnected.", EngineLogLevel::Info);
}

void JLinkEngine::eraseTarget(bool) {
    if (isBusy()) return;

    const MemoryRegion* flash = m_chipData->queryMemoryMap(m_target.chipName, m_target.chipDescriptionPath).mainFlash();
    if (!flash) {
        emit logMessage("The flash size of this chip is unknown, so it cannot be erased.", EngineLogLevel::Error);
        QTimer::singleShot(0, this, [this]() { emit operationFinished(false, "Flash size unknown."); });
        return;
    }
    m_logProgress.reset(true, flash->size);
    run(JLinkCommands::eraseScript(flash->start, flash->end()), State::Erasing, "Erasing target chip (Chip Erase)...");
}

void JLinkEngine::unlockTarget() {
    if (isBusy()) return;

    // The unlock recipes run through the probe-rs helper. SEGGER's Commander is not wired to
    // them yet; until then a J-Link needs the WinUSB driver (Zadig) to use the probe-rs engine.
    emit logMessage("Unlock is not available through the SEGGER J-Link engine yet.", EngineLogLevel::Error);
    QTimer::singleShot(0, this, [this]() { emit operationFinished(false, "Unlock not supported by this engine."); });
}

// The SEGGER engine has no Lock or Unlock: it stays silent, and the window decides.
void JLinkEngine::requestCapabilities(const QString&) {}

void JLinkEngine::lockTarget() {
    if (isBusy()) return;

    emit logMessage("Lock is not available through the SEGGER J-Link engine yet.", EngineLogLevel::Error);
    QTimer::singleShot(0, this, [this]() { emit operationFinished(false, "Lock not supported by this engine."); });
}

void JLinkEngine::programFile(const QString& filePath, uint32_t baseAddress, bool) {
    if (isBusy()) return;

    // The Commander always verifies what it has written, so the verify flag changes nothing.
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    static const QStringList known = {"bin", "hex", "ihex", "elf", "axf", "out", "srec", "mot"};
    if (!known.contains(suffix)) {
        emit logMessage("The J-Link engine cannot write a ." + suffix + " file.", EngineLogLevel::Error);
        QTimer::singleShot(0, this, [this]() { emit operationFinished(false, "File type not supported."); });
        return;
    }
    m_logProgress.reset(false, 0);
    run(JLinkCommands::loadScript(filePath, suffix == "bin", baseAddress), State::Programming,
        QString("Flashing: %1").arg(QFileInfo(filePath).fileName()));
}

void JLinkEngine::verifyFile(const QString& filePath, uint32_t baseAddress) {
    if (isBusy()) return;

    // The Commander can only compare a raw binary file, so other formats are converted first.
    // Gaps between the segments of an image count as erased bytes (0xFF).
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    HexParseResult image;
    if (suffix == "bin") {
        image = HexParser::analyzeBinFile(filePath, baseAddress);
    } else if (suffix == "hex" || suffix == "ihex") {
        image = HexParser::analyzeHexFile(filePath);
    } else if (suffix == "elf" || suffix == "axf") {
        image = HexParser::analyzeElfFile(filePath);
    }
    if (!image.success || image.data.isEmpty()) {
        const QString reason = image.errorMessage.isEmpty() ? QString("File type not supported.") : image.errorMessage;
        emit logMessage("Cannot verify " + QFileInfo(filePath).fileName() + ": " + reason, EngineLogLevel::Error);
        QTimer::singleShot(0, this, [this, reason]() { emit operationFinished(false, reason); });
        return;
    }

    // The Commander's own compare command shows no progress and no address of a difference. So the
    // chip is read in pieces, like a read, and compared here with what the image says.
    clearReadJob();
    m_verifyImage = image.data;
    m_verifyBase = image.baseAddress;
    startReadingParts(m_verifyBase, static_cast<uint64_t>(m_verifyImage.size()), State::Verifying,
                      QString("Verifying: %1").arg(QFileInfo(filePath).fileName()));
}

// Starts a run that saves a memory area in pieces; the finished pieces show the progress.
void JLinkEngine::startReadingParts(uint64_t address, uint64_t byteCount, State state, const QString& action) {
    const int parts = JLinkCommands::partCount(byteCount, kReadChunk);
    for (int i = 0; i < parts; ++i) {
        m_readParts << m_scratch.filePath(QString("part%1.bin").arg(i));
        m_readPartSizes << static_cast<qint64>(std::min<uint64_t>(kReadChunk, byteCount - i * kReadChunk));
    }
    run(JLinkCommands::readScript(address, byteCount, kReadChunk, m_readParts), state, action);
}

void JLinkEngine::readMemory(uint64_t address, uint64_t byteCount, const QString& outputFile) {
    if (isBusy() || byteCount == 0) return;

    clearReadJob();
    m_readOutput = outputFile;
    startReadingParts(address, byteCount, State::Reading,
                      QString("Reading %1 bytes from 0x%2").arg(byteCount).arg(address, 8, 16, QLatin1Char('0')));
}

void JLinkEngine::resetTarget(bool halt) {
    if (isBusy()) return;
    run(halt ? QString("r\nh\nexit\n") : JLinkCommands::resetScript(), State::Resetting, "Resetting target...");
}

void JLinkEngine::cancelCurrentOperation() {
    if (m_process->state() != QProcess::NotRunning) {
        m_cancelRequested = true;
        m_process->kill();
    }
}

void JLinkEngine::onProgressTick() {
    if (m_state == State::Erasing || m_state == State::Programming) {
        followLog();
        return;
    }
    if ((m_state != State::Reading && m_state != State::Verifying) || m_readParts.isEmpty()) return;

    // A piece is finished when its file has its full size.
    int finished = 0;
    for (int i = 0; i < m_readParts.size(); ++i) {
        if (QFileInfo(m_readParts.at(i)).size() == m_readPartSizes.at(i)) ++finished;
    }
    const int percent = std::min(99, finished * 100 / static_cast<int>(m_readParts.size()));
    const QString what = m_state == State::Verifying ? "Verifying" : "Reading";
    emit progressChanged(percent, QString("%1 %2%").arg(what).arg(percent));
}

// Reads what the Commander has added to its log file since the last look and turns it into progress.
void JLinkEngine::followLog() {
    QFile file(m_logFile);
    if (!file.open(QIODevice::ReadOnly) || !file.seek(m_logPos)) return;
    const QByteArray added = file.readAll();
    m_logPos += added.size();
    m_logPending += added;

    int newline;
    while ((newline = m_logPending.indexOf('\n')) >= 0) {
        const QString line = QString::fromLocal8Bit(m_logPending.left(newline));
        m_logPending.remove(0, newline + 1);
        const JLinkLogProgress::Update update = m_logProgress.feed(line);
        if (update.changed) emit progressChanged(update.percent, update.text);
    }
}

// Compares the finished pieces of a verify with the image, and says where the first difference is.
bool JLinkEngine::compareVerifyParts(QString* error) {
    qint64 offset = 0;
    for (int i = 0; i < m_readParts.size(); ++i) {
        QFile part(m_readParts.at(i));
        if (!part.open(QIODevice::ReadOnly)) {
            *error = QString("Piece %1 of the read is missing.").arg(i + 1);
            return false;
        }
        const QByteArray found = part.readAll();
        if (found.size() != m_readPartSizes.at(i)) {
            *error = QString("Piece %1 of the read is incomplete.").arg(i + 1);
            return false;
        }
        const QByteArray expected = m_verifyImage.mid(static_cast<int>(offset), found.size());
        if (found != expected) {
            int at = 0;
            while (at < found.size() && found.at(at) == expected.at(at)) ++at;
            *error = QString("Verification failed at 0x%1: expected %2, found %3")
                         .arg(m_verifyBase + static_cast<uint64_t>(offset + at), 8, 16, QLatin1Char('0'))
                         .arg(static_cast<uchar>(expected.at(at)), 2, 16, QLatin1Char('0'))
                         .arg(static_cast<uchar>(found.at(at)), 2, 16, QLatin1Char('0'));
            return false;
        }
        offset += found.size();
    }
    return true;
}

void JLinkEngine::clearReadJob() {
    for (const QString& part : m_readParts) QFile::remove(part);
    m_readParts.clear();
    m_readPartSizes.clear();
    m_readOutput.clear();
}

// Joins the pieces of a finished read into the file the user chose.
bool JLinkEngine::joinReadParts(QString* error) {
    QFile out(m_readOutput);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = "Cannot create " + m_readOutput;
        return false;
    }
    for (int i = 0; i < m_readParts.size(); ++i) {
        QFile part(m_readParts.at(i));
        if (!part.open(QIODevice::ReadOnly)) {
            *error = QString("Piece %1 of the read is missing.").arg(i + 1);
            return false;
        }
        const QByteArray data = part.readAll();
        if (data.size() != m_readPartSizes.at(i) || out.write(data) != data.size()) {
            *error = QString("Piece %1 of the read is incomplete.").arg(i + 1);
            return false;
        }
    }
    return true;
}

void JLinkEngine::onFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    m_progressTimer->stop();
    const QString output = QString::fromLocal8Bit(m_process->readAll());
    const State done = m_state;
    m_state = State::Idle;

    if (m_cancelRequested) {
        m_cancelRequested = false;
        clearReadJob();
        emit logMessage("Operation cancelled by user.", EngineLogLevel::Warning);
        emit operationFinished(false, "Operation cancelled.");
        return;
    }

    JLinkCommands::Outcome outcome = JLinkCommands::analyze(exitCode, output);
    if (exitStatus != QProcess::NormalExit && outcome.ok) {
        outcome.ok = false;
        outcome.error = "The J-Link Commander crashed.";
    }
    if (outcome.ok && done == State::Reading) {
        QString error;
        if (!joinReadParts(&error)) {
            outcome.ok = false;
            outcome.error = error;
        }
    }
    if (outcome.ok && done == State::Verifying) {
        QString error;
        if (!compareVerifyParts(&error)) {
            outcome.ok = false;
            outcome.error = error;
        }
    }
    if (done == State::Reading || done == State::Verifying) {
        clearReadJob();
        m_verifyImage.clear();
    }

    // After a success only the useful lines; after a failure everything, to find the cause.
    for (const QString& raw : output.split('\n')) {
        QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith("J-Link>")) continue;
        // The Commander draws a progress bar as text ("Erasing flash [000%]...[100%] Done."). The
        // status bar shows the progress, so only the name and the result are kept.
        if (line.contains("%]")) {
            line = line.left(line.indexOf('[')).trimmed() + (line.endsWith("Done.") ? " ... Done." : "");
        }
        if (!outcome.ok) {
            emit logMessage(line, line == outcome.error ? EngineLogLevel::Error : EngineLogLevel::Warning);
        } else if (isInteresting(line)) {
            emit logMessage(line, EngineLogLevel::Info);
        }
    }

    QString advice;
    if (!outcome.ok) {
        advice = JLinkCommands::advice(output);
        if (!advice.isEmpty()) emit logMessage(advice, EngineLogLevel::Warning);
    }

    if (done == State::Connecting && outcome.ok
        && JLinkCommands::linkCheck(output) == JLinkCommands::LinkCheck::Mismatch) {
        // No error is reported by such a probe: the data is simply wrong. Say it loudly.
        emit logMessage(QString("This J-Link returns different data at %1 kHz than at %2 kHz. At %1 kHz "
                                "reads can be wrong and writes can fail. Set the speed to %2 kHz.")
                            .arg(m_target.speedKhz).arg(JLinkCommands::kSafeSpeedKhz),
                        EngineLogLevel::Warning);
    }

    if (done == State::Connecting) {
        m_connected = outcome.ok;
        emit connected(outcome.ok, outcome.ok ? m_target.chipName : advice);
    }

    const QString message = outcome.ok
        ? QString("%1 completed successfully.").arg(m_action)
        : QString("%1 failed (%2).").arg(m_action, outcome.error);
    emit progressChanged(outcome.ok ? 100 : 0, message);
    emit logMessage(message, outcome.ok ? EngineLogLevel::Success : EngineLogLevel::Error);
    emit operationFinished(outcome.ok, message);
}

void JLinkEngine::onErrorOccurred(QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart) return;

    m_progressTimer->stop();
    m_state = State::Idle;
    clearReadJob();
    const QString message = QString("Failed to start '%1'. Is SEGGER's J-Link software installed?").arg(m_commander);
    emit logMessage(message, EngineLogLevel::Error);
    emit operationFinished(false, message);
}
