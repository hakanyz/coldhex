#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>

class FlashCoordinator;
class IProgrammerEngine;
class QLabel;
class QProgressBar;
class QStatusBar;
class QTimer;

// Shows on the right of the status bar what the program is doing: the action, a progress bar and
// the time it has taken. A single command (erase, read, connect, ...) has no known percentage
// unless probe-rs prints one, so its bar runs as a busy animation until a percentage arrives.
// A flashing sequence reports its own steps and overall percentage. The message on the left of
// the status bar is not touched.
class OperationStatus : public QObject {
    Q_OBJECT

public:
    OperationStatus(QStatusBar* statusBar, IProgrammerEngine* engine, FlashCoordinator* coordinator,
                    QObject* parent = nullptr);

    bool isRunning() const { return m_running; }
    QLabel* textLabel() const { return m_text; }
    QProgressBar* progressBar() const { return m_bar; }
    QLabel* elapsedLabel() const { return m_elapsed; }

private:
    void begin(const QString& text);
    void setText(const QString& text);
    void setPercent(int percent);
    void end(bool success, const QString& what);
    void hideAll();
    void updateElapsed();
    QString elapsedText() const;

    QLabel* m_text;
    QProgressBar* m_bar;
    QLabel* m_elapsed;
    QTimer* m_tick;
    QTimer* m_hideTimer;
    QElapsedTimer m_clock;
    QString m_action;       // what is running, without trailing dots
    bool m_running = false;
    bool m_sequence = false;  // a flashing sequence is running: single commands inside it are not reported
};
