#include "OperationStatus.h"
#include "core/FlashCoordinator.h"
#include "engine/IProgrammerEngine.h"
#include <QLabel>
#include <QProgressBar>
#include <QRegularExpression>
#include <QStatusBar>
#include <QTimer>

OperationStatus::OperationStatus(QStatusBar* statusBar, IProgrammerEngine* engine, FlashCoordinator* coordinator,
                                 QObject* parent)
    : QObject(parent),
      m_text(new QLabel(statusBar)),
      m_bar(new QProgressBar(statusBar)),
      m_elapsed(new QLabel(statusBar)),
      m_tick(new QTimer(this)),
      m_hideTimer(new QTimer(this))
{
    m_text->setMaximumWidth(320);
    m_bar->setFixedWidth(160);
    m_bar->setMaximumHeight(14);
    m_bar->setTextVisible(false);
    m_elapsed->setMinimumWidth(40);
    statusBar->addPermanentWidget(m_text);
    statusBar->addPermanentWidget(m_bar);
    statusBar->addPermanentWidget(m_elapsed);
    hideAll();

    m_tick->setInterval(500);
    connect(m_tick, &QTimer::timeout, this, &OperationStatus::updateElapsed);
    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(5000);
    connect(m_hideTimer, &QTimer::timeout, this, &OperationStatus::hideAll);

    // A single command. Inside a flashing sequence the coordinator reports instead.
    connect(engine, &IProgrammerEngine::operationStarted, this, [this](const QString& action) {
        if (!m_sequence) begin(action);
    });
    connect(engine, &IProgrammerEngine::progressChanged, this, [this](int percent, const QString&) {
        if (m_running && !m_sequence) setPercent(percent);
    });
    connect(engine, &IProgrammerEngine::operationFinished, this, [this](bool success, const QString&) {
        if (m_running && !m_sequence) end(success, m_action);
    });

    connect(coordinator, &FlashCoordinator::sequenceStarted, this, [this]() {
        m_sequence = true;
        begin(QStringLiteral("Starting..."));
        setPercent(0);
    });
    connect(coordinator, &FlashCoordinator::stepChanged, this, [this](const QString& step, int index, int total) {
        if (!m_running) return;
        setText(QString("[%1/%2] %3").arg(index).arg(total).arg(step));
        // The bar moves at least one step at a time even when probe-rs prints no percentage.
        if (total > 0) setPercent(((index - 1) * 100) / total);
    });
    connect(coordinator, &FlashCoordinator::totalProgressChanged, this, [this](int percent) {
        if (m_running) setPercent(percent);
    });
    connect(coordinator, &FlashCoordinator::sequenceFinished, this, [this](bool success, const QString&) {
        m_sequence = false;
        // On success the last step would be misleading ("Resetting - done"); a failure names its step.
        if (m_running) end(success, success ? QStringLiteral("All steps") : m_action);
    });
}

void OperationStatus::begin(const QString& text) {
    m_hideTimer->stop();
    m_running = true;
    m_clock.start();
    m_tick->start();
    m_bar->setRange(0, 0);  // busy animation until a percentage is known
    setText(text);
    m_bar->show();
    m_elapsed->show();
    updateElapsed();
}

void OperationStatus::setText(const QString& text) {
    // Without the trailing dots "Erasing chip..." can be followed by "done" or "failed".
    QString action = text;
    action.remove(QRegularExpression(QStringLiteral("\\.+$")));
    m_action = action;
    m_text->setText(action);
    m_text->show();
}

void OperationStatus::setPercent(int percent) {
    if (m_bar->maximum() == 0) m_bar->setRange(0, 100);
    m_bar->setValue(qBound(0, percent, 100));
}

void OperationStatus::end(bool success, const QString& what) {
    m_running = false;
    m_tick->stop();
    updateElapsed();
    m_text->setText(QString("%1 - %2").arg(what, success ? "done" : "failed"));
    if (success) {
        m_bar->setRange(0, 100);
        m_bar->setValue(100);
    } else {
        m_bar->hide();
    }
    m_hideTimer->start();
}

void OperationStatus::hideAll() {
    m_text->hide();
    m_bar->hide();
    m_elapsed->hide();
}

void OperationStatus::updateElapsed() {
    m_elapsed->setText(elapsedText());
}

QString OperationStatus::elapsedText() const {
    const qint64 seconds = m_clock.isValid() ? m_clock.elapsed() / 1000 : 0;
    return QString("%1:%2").arg(seconds / 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}
