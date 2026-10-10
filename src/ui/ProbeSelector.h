#pragma once

#include "engine/IProgrammerEngine.h"
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

class QAbstractNativeEventFilter;
class QComboBox;
class QTimer;
class QWidget;

// The probe box. It shows the debug probes probe-rs can see and keeps the list current: it looks
// again when USB devices come and go (Windows reports this itself, on Linux /sys/bus/usb is
// watched), when the window is activated, and now and then as a safety net.
class ProbeSelector : public QObject {
    Q_OBJECT

public:
    // `isBusy` tells whether an operation is running; the list is not refreshed meanwhile.
    ProbeSelector(QWidget* window, QComboBox* probeBox, IProgrammerEngine* engine, std::function<bool()> isBusy);
    ~ProbeSelector() override;

    // Looks for probes now. `verbose` writes the result to the console even when nothing changed.
    void refresh(bool verbose);

    // VID:PID[:serial] of the selected probe; empty lets probe-rs pick.
    QString selector() const;
    QString label() const;
    void restore(const QString& label);

signals:
    void message(const QString& level, const QString& text);
    void status(const QString& text, int timeoutMs);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onProbeListReady(bool success, const QList<ProbeInfo>& probes, const QString& error);

private:
    void scheduleRefresh();
    void onTimer();

    QWidget* m_window;
    QComboBox* m_box;
    IProgrammerEngine* m_engine;
    std::function<bool()> m_isBusy;

    QTimer* m_timer = nullptr;
    QAbstractNativeEventFilter* m_deviceFilter = nullptr;
    bool m_verbose = false;
    bool m_listSeen = false;
    bool m_lastError = false;
    QStringList m_lastKeys;
    QStringList m_lastUsbKeys;
    int m_safetyTick = 0;
};
