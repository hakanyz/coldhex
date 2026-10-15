#include "ProbeInspector.h"
#include "JLinkCommands.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>

namespace ProbeInspect {

namespace {

// First capture group of `pattern` in `text`, or an empty string.
QString capture(const QString& text, const char* pattern) {
    const QRegularExpressionMatch m =
        QRegularExpression(pattern, QRegularExpression::MultilineOption).match(text);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

}  // namespace

bool parseJLinkBanner(const QString& output, ProbeDetails* details) {
    // "Firmware: J-Link V9 compiled May  7 2021 16:26:12"
    const QString firmware = capture(output, R"(^Firmware:\s*(.+)$)");
    if (firmware.isEmpty()) return false;

    const int compiled = firmware.indexOf(" compiled ");
    details->name = compiled > 0 ? firmware.left(compiled) : firmware;
    details->firmware = compiled > 0 ? firmware.mid(compiled + 10) : QString();
    details->hardware = capture(output, R"(^Hardware version:\s*(.+)$)");
    details->serial = capture(output, R"(^S/N:\s*(\S+))");

    const QString volt = capture(output, R"(VTref\s*=\s*([0-9.]+)\s*V)");
    bool ok = false;
    const double v = volt.toDouble(&ok);
    details->voltage = ok ? v : -1;
    return true;
}

bool parseHelperInfo(const QString& output, ProbeDetails* details) {
    for (const QString& line : output.split('\n')) {
        const QJsonObject o = QJsonDocument::fromJson(line.trimmed().toUtf8()).object();
        if (o.value("event").toString() != "probe") continue;
        details->name = o.value("name").toString();
        const QJsonValue v = o.value("voltage");  // null when the probe cannot measure it
        details->voltage = v.isDouble() ? v.toDouble() : -1;
        return true;
    }
    return false;
}

QString describe(const ProbeDetails& d) {
    QStringList parts{d.name.isEmpty() ? QString("unknown probe") : d.name};
    if (!d.hardware.isEmpty()) parts << "hardware " + d.hardware;
    if (!d.serial.isEmpty()) parts << "S/N " + d.serial;
    if (!d.firmware.isEmpty()) parts << "firmware " + d.firmware;
    if (!d.hasVoltage()) {
        parts << "target voltage not reported by this probe";
    } else if (d.voltage < 1.0) {
        parts << QString("target voltage %1 V (no target power on VTref)").arg(d.voltage, 0, 'f', 2);
    } else {
        parts << QString("target voltage %1 V").arg(d.voltage, 0, 'f', 2);
    }
    return parts.join(" | ");
}

}  // namespace ProbeInspect

ProbeInspector::ProbeInspector(QObject* parent) : QObject(parent), m_timeout(new QTimer(this)) {
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, &ProbeInspector::onTimeout);
}

void ProbeInspector::inspect(const QString& selector, const QString& commanderPath, const QString& helperPath) {
    m_queue.append({selector, commanderPath, helperPath});
    if (!m_busy) startNext();
}

void ProbeInspector::startNext() {
    if (m_queue.isEmpty()) return;
    m_current = m_queue.takeFirst();
    m_busy = true;
    m_done = false;
    m_isJLink = JLinkCommands::isJLinkProbe(m_current.selector) && !m_current.commander.isEmpty();

    const QString program = m_isJLink ? m_current.commander : m_current.helper;
    if (program.isEmpty()) {
        // complete() would start the next job from inside this call: let the caller finish first.
        QTimer::singleShot(0, this, [this]() { complete(false, "the program that can ask this probe is not installed"); });
        m_process = nullptr;
        return;
    }

    QStringList args;
    if (m_isJLink) {
        args << "-NoGui" << "1";
        const QString serial = JLinkCommands::serialOf(m_current.selector);
        if (!serial.isEmpty()) args << "-SelectEmuBySN" << serial;
    } else {
        args << "info" << "--probe" << m_current.selector;
    }

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::finished, this, &ProbeInspector::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &ProbeInspector::onProcessError);
    m_process->start(program, args);
    if (m_isJLink) {
        // Without a device the Commander stays at its prompt: ask it to leave.
        m_process->write("exit\n");
        m_process->closeWriteChannel();
    }
    m_timeout->start(15000);
}

void ProbeInspector::onProcessFinished(int exitCode, QProcess::ExitStatus status) {
    if (m_done) return;
    const QString output = QString::fromLocal8Bit(m_process->readAll());
    ProbeDetails details;
    const bool parsed = m_isJLink ? ProbeInspect::parseJLinkBanner(output, &details)
                                  : ProbeInspect::parseHelperInfo(output, &details);
    if (parsed) {
        m_details = details;
        complete(true, QString());
        return;
    }

    // The helper puts its reason in the "result" line; for the Commander the last line will do.
    QString reason;
    for (const QString& line : output.split('\n')) {
        const QJsonObject o = QJsonDocument::fromJson(line.trimmed().toUtf8()).object();
        if (!o.value("error").toString().isEmpty()) reason = o.value("error").toString();
    }
    if (reason.isEmpty()) reason = output.trimmed().section('\n', -1).trimmed();
    if (reason.isEmpty()) reason = QString("no answer (exit code %1%2)").arg(exitCode).arg(status == QProcess::CrashExit ? ", crashed" : "");
    complete(false, reason);
}

void ProbeInspector::onProcessError(QProcess::ProcessError error) {
    if (m_done || error != QProcess::FailedToStart) return;  // other errors end in finished()
    complete(false, "the program could not be started");
}

void ProbeInspector::onTimeout() {
    if (m_done) return;
    if (m_process) m_process->kill();
    complete(false, "no answer within 15 seconds (is another program using the probe?)");
}

void ProbeInspector::complete(bool ok, const QString& error) {
    if (m_done) return;
    m_done = true;
    m_timeout->stop();
    const ProbeDetails details = ok ? m_details : ProbeDetails();
    if (m_process) {
        m_process->disconnect(this);
        m_process->deleteLater();
        m_process = nullptr;
    }
    const QString selector = m_current.selector;
    m_busy = false;
    emit finished(selector, details, ok, error);
    startNext();
}
