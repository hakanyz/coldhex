#pragma once

#include <QObject>
#include <QString>

class QAction;
class QDockWidget;
class QLabel;
class QMainWindow;
class QPlainTextEdit;
class QToolButton;

// The console at the bottom of the window: colored log lines, a slim title bar with minimize and
// close buttons, a right-click menu, and saving the log to a file.
//
// Minimized, only the title bar stays (a red dot shows up there when something went wrong). Closed,
// the console is hidden until ensureVisible() is called, which the window does when an action starts.
class ConsoleDock : public QObject {
    Q_OBJECT

public:
    ConsoleDock(QMainWindow* window, QDockWidget* dock, QPlainTextEdit* log,
                QAction* clearAction, QAction* saveAction);

    // Adds a line such as "[12:00:01] ERROR: text", colored by its level (see Theme::logColor). "ERROR",
    // "WARNING" and "WARN" also flag a minimized console.
    void append(const QString& level, const QString& message);

    // Shows the console if it was closed (a minimized console stays minimized).
    void ensureVisible();

    // Asks for a file name and writes the log text into it.
    void saveToFile();

    bool isMinimized() const { return m_minimized; }

private:
    void buildTitleBar();
    void buildContextMenu(QAction* clearAction, QAction* saveAction);
    void setMinimized(bool minimized);

    QMainWindow* m_window;
    QDockWidget* m_dock;
    QPlainTextEdit* m_log;
    QLabel* m_title = nullptr;
    QToolButton* m_minimizeButton = nullptr;
    bool m_minimized = false;
    int m_restoreHeight = 160;
};
