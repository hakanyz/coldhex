#include "ProbeSelector.h"
#include "engine/UsbScanner.h"
#include <QAbstractNativeEventFilter>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QTimer>
#include <QWidget>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbt.h>

namespace {

// Windows tells every top-level window about USB devices that arrive or leave.
class DeviceChangeFilter : public QAbstractNativeEventFilter {
public:
    explicit DeviceChangeFilter(std::function<void()> onChange) : m_onChange(std::move(onChange)) {}

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr*) override {
        if (eventType != "windows_generic_MSG") return false;
        const MSG* msg = static_cast<const MSG*>(message);
        if (msg->message == WM_DEVICECHANGE
            && (msg->wParam == DBT_DEVNODES_CHANGED || msg->wParam == DBT_DEVICEARRIVAL
                || msg->wParam == DBT_DEVICEREMOVECOMPLETE)) {
            m_onChange();
        }
        return false;  // never swallow the message
    }

private:
    std::function<void()> m_onChange;
};

}  // namespace
#endif

ProbeSelector::ProbeSelector(QWidget* window, QComboBox* probeBox, IProgrammerEngine* engine,
                             std::function<bool()> isBusy)
    : QObject(window), m_window(window), m_box(probeBox), m_engine(engine), m_isBusy(std::move(isBusy))
{
    connect(m_engine, &IProgrammerEngine::probeListReady, this, &ProbeSelector::onProbeListReady);

    // Look again whenever the window is activated.
    m_window->installEventFilter(this);

#ifdef Q_OS_WIN
    m_deviceFilter = new DeviceChangeFilter([this]() { scheduleRefresh(); });
    QCoreApplication::instance()->installNativeEventFilter(m_deviceFilter);
#endif

    // On Linux a cheap look at /sys/bus/usb every second does the job of the Windows
    // notification. A slow tick stays as a safety net on both systems.
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &ProbeSelector::onTimer);
#ifdef Q_OS_WIN
    m_timer->start(5000);
#else
    m_timer->start(1000);
#endif

    refresh(true);
}

ProbeSelector::~ProbeSelector() {
    if (m_deviceFilter) {
        QCoreApplication::instance()->removeNativeEventFilter(m_deviceFilter);
        delete m_deviceFilter;
    }
}

QString ProbeSelector::selector() const {
    return m_box->currentData().toString();
}

QString ProbeSelector::label() const {
    return m_box->currentText();
}

void ProbeSelector::restore(const QString& label) {
    const int idx = m_box->findText(label);
    if (idx >= 0) m_box->setCurrentIndex(idx);
}

void ProbeSelector::refresh(bool verbose) {
    if (verbose) m_verbose = true;  // the user asked for it: always report the result
    m_engine->requestProbeList();
}

bool ProbeSelector::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_window && event->type() == QEvent::ActivationChange && m_window->isActiveWindow()
        && !m_isBusy()) {
        m_engine->requestProbeList();
    }
    return QObject::eventFilter(watched, event);
}

// Asks for the probe list now and once more shortly after: right after a probe is plugged in,
// its driver may not be ready yet.
void ProbeSelector::scheduleRefresh() {
    auto refreshIfIdle = [this]() {
        if (!m_isBusy()) m_engine->requestProbeList();
    };
    QTimer::singleShot(300, this, refreshIfIdle);
    QTimer::singleShot(1500, this, refreshIfIdle);
}

void ProbeSelector::onTimer() {
#ifndef Q_OS_WIN
    const QList<UsbDevice> seen = UsbScanner::scan();
    QStringList keys;
    for (const UsbDevice& d : seen) keys << d.id();
    if (keys != m_lastUsbKeys) {
        m_lastUsbKeys = keys;
        scheduleRefresh();
        return;
    }
#endif
    if (m_safetyTick++ % 5 == 0) scheduleRefresh();
}

// Fills the box with what probe-rs found. Automatic refreshes only write to the console when
// the set of probes changed; a manual refresh always does.
void ProbeSelector::onProbeListReady(bool success, const QList<ProbeInfo>& probes, const QString& error) {
    const bool verbose = m_verbose;
    m_verbose = false;

    if (!success) {
        if (verbose || !m_lastError) emit message("ERROR", error);
        m_lastError = true;
        return;
    }
    m_lastError = false;

    QStringList keys;
    for (const ProbeInfo& p : probes) keys << p.selector();
    const bool changed = !m_listSeen || (keys != m_lastKeys);
    m_listSeen = true;
    m_lastKeys = keys;
    if (!changed && !verbose) return;  // leave the box alone, an open drop-down would close

    const QString previous = m_box->currentText();
    m_box->clear();
    if (probes.isEmpty()) {
        // Empty data means "let probe-rs pick"; connecting will report the real error.
        m_box->addItem("(no probe detected)", QString());
        emit message("WARNING", "No debug probe detected. Check the USB connection, the probe driver and that probe-rs is installed.");
        return;
    }

    for (const ProbeInfo& p : probes) {
        const QString label = p.displayName + " [" + p.selector() + "]";
        m_box->addItem(label, p.selector());
        emit message("INFO", QString("Found probe: %1").arg(label));
    }
    const int idx = m_box->findText(previous);
    if (idx >= 0) m_box->setCurrentIndex(idx);

    if (probes.size() > 1) {
        const QString msg = QString("%1 probes found. Using: %2. Check the Probe selection before flashing.")
                                .arg(probes.size())
                                .arg(m_box->currentText());
        emit message("WARNING", msg);
        emit status(msg, 8000);
    }
}
