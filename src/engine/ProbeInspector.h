#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

class QTimer;

// What a probe says about itself, before any chip is involved.
struct ProbeDetails {
    QString name;      // e.g. "ST-Link V2" or "J-Link V9"
    QString firmware;  // build date of the probe firmware, when the probe tells
    QString hardware;  // e.g. "V9.70", when the probe tells
    QString serial;    // as the probe reports it; some probes have none or a made-up one
    double voltage = -1;  // volts on the VTref pin, negative when the probe cannot tell

    bool hasVoltage() const { return voltage >= 0; }
};

namespace ProbeInspect {

// Reads the start-up text of SEGGER's J-Link Commander ("Firmware: ...", "VTref=3.270V").
// False when the text is not from a J-Link Commander.
bool parseJLinkBanner(const QString& output, ProbeDetails* details);

// Reads the {"event":"probe",...} line of `coldhex-flash info`. False when there is none.
bool parseHelperInfo(const QString& output, ProbeDetails* details);

// One console line, e.g. "ST-Link V2 | target voltage 3.27 V".
QString describe(const ProbeDetails& details);

}  // namespace ProbeInspect

// Asks one probe at a time who it is and which voltage it sees. A J-Link is asked through SEGGER's
// J-Link Commander (so it keeps SEGGER's USB driver), every other probe through the coldhex-flash
// helper. Nothing here connects to a chip.
class ProbeInspector : public QObject {
    Q_OBJECT

public:
    explicit ProbeInspector(QObject* parent = nullptr);

    // Queues a probe (VID:PID[:serial]). `commanderPath` is the J-Link Commander, `helperPath` the
    // coldhex-flash helper; either may be empty when it is not installed.
    void inspect(const QString& selector, const QString& commanderPath, const QString& helperPath);
    bool isRunning() const { return m_busy; }

signals:
    // `ok` is false when the probe could not be asked; `error` then says why.
    void finished(const QString& selector, const ProbeDetails& details, bool ok, const QString& error);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);
    void onTimeout();

private:
    struct Job {
        QString selector;
        QString commander;
        QString helper;
    };
    void startNext();
    void complete(bool ok, const QString& error);

    QList<Job> m_queue;
    Job m_current;
    QProcess* m_process = nullptr;
    QTimer* m_timeout;
    bool m_isJLink = false;
    bool m_done = false;
    bool m_busy = false;  // a job is being asked, including the moment before its process exists
    ProbeDetails m_details;
};
