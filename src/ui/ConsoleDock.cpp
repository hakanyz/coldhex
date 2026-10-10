#include "ConsoleDock.h"
#include "Theme.h"
#include <QAction>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QStandardPaths>
#include <QToolButton>

ConsoleDock::ConsoleDock(QMainWindow* window, QDockWidget* dock, QPlainTextEdit* log,
                         QAction* clearAction, QAction* saveAction)
    : QObject(window), m_window(window), m_dock(dock), m_log(log)
{

    buildTitleBar();
    buildContextMenu(clearAction, saveAction);

    connect(clearAction, &QAction::triggered, m_log, &QPlainTextEdit::clear);
    connect(saveAction, &QAction::triggered, this, &ConsoleDock::saveToFile);
}

void ConsoleDock::append(const QString& level, const QString& message) {
    const QString time = QDateTime::currentDateTime().toString("HH:mm:ss");
    const QString line =
        QString("<span style='color: %1;'>[%2]</span> "
                "<span style='color: %3; font-weight: bold;'>%4:</span> "
                "<span style='color: %5;'>%6</span>")
            .arg(Theme::hex(Theme::Color::textMuted), time, Theme::logColor(level), level,
                 Theme::hex(Theme::Color::text), message.toHtmlEscaped().replace("\n", "<br>"));
    m_log->appendHtml(line);

    // A minimized console cannot be read: flag problems on its small title bar.
    if (m_minimized && (level == "ERROR" || level == "WARNING" || level == "WARN")) {
        m_title->setText("Console Output  \u25CF");
        m_title->setStyleSheet(QString("font-weight: bold; color: %1;").arg(Theme::hex(Theme::Color::logError)));
    }
}

void ConsoleDock::ensureVisible() {
    if (!m_dock->isVisible()) m_dock->show();
}

void ConsoleDock::saveToFile() {
    const QString suggested = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                                  .filePath("coldhex-log.txt");
    const QString file = QFileDialog::getSaveFileName(m_window, "Save Log", suggested,
                                                      "Text (*.txt);;All Files (*.*)");
    if (file.isEmpty()) return;

    QFile out(file);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(m_window, "Save Log", "Cannot write to " + file);
        return;
    }
    out.write(m_log->toPlainText().toUtf8());
    append("INFO", "Log saved to " + file);
}

// A slim title bar with two small buttons: minimize (keeps only the bar, click again to restore)
// and close (hides the console until the next action reopens it).
void ConsoleDock::buildTitleBar() {
    auto* bar = new QWidget(m_dock);
    auto* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(8, 2, 4, 2);
    layout->setSpacing(2);

    m_title = new QLabel("Console Output", bar);
    m_title->setStyleSheet("font-weight: bold;");
    layout->addWidget(m_title);
    layout->addStretch();

    const QString buttonStyle = QString(
        "QToolButton { border: none; border-radius: 3px; padding: 1px; }"
        "QToolButton:hover { background: %1; }"
        "QToolButton:pressed { background: %2; }")
        .arg(Theme::hex(Theme::Color::hover), Theme::hex(Theme::Color::pressed));
    auto makeButton = [&](const char* icon, const QString& tip) {
        auto* b = new QToolButton(bar);
        b->setIcon(QIcon(icon));
        b->setIconSize(QSize(14, 14));
        b->setAutoRaise(true);
        b->setStyleSheet(buttonStyle);
        b->setToolTip(tip);
        layout->addWidget(b);
        return b;
    };

    m_minimizeButton = makeButton(":/icons/dock/minimize.svg", "Minimize the console");
    connect(m_minimizeButton, &QToolButton::clicked, this, [this]() { setMinimized(!m_minimized); });

    QToolButton* close = makeButton(":/icons/dock/close.svg",
        "Close the console (it opens again with the next action, or via View > Console Output)");
    connect(close, &QToolButton::clicked, m_dock, &QWidget::hide);

    m_dock->setTitleBarWidget(bar);

    // Showing the console again (View menu, F12) always shows it expanded.
    connect(m_dock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible && m_minimized) setMinimized(false);
    });
}

// Right-click in the console: Copy, Select All, Clear and Save (Clear also has the Ctrl+L shortcut).
void ConsoleDock::buildContextMenu(QAction* clearAction, QAction* saveAction) {
    m_log->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_log, &QWidget::customContextMenuRequested, this, [this, clearAction, saveAction](const QPoint& pos) {
        QMenu* menu = m_log->createStandardContextMenu();
        menu->addSeparator();
        menu->addAction(clearAction);
        menu->addAction(saveAction);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(m_log->mapToGlobal(pos));
    });
}

void ConsoleDock::setMinimized(bool minimized) {
    if (minimized == m_minimized) return;

    if (minimized) {
        m_restoreHeight = qMax(80, m_dock->height());
        m_dock->widget()->setMaximumHeight(0);  // keep it laid out so the bar keeps the full width
        m_window->resizeDocks({m_dock}, {m_dock->titleBarWidget()->sizeHint().height()}, Qt::Vertical);
    } else {
        m_dock->widget()->setMaximumHeight(QWIDGETSIZE_MAX);
        m_window->resizeDocks({m_dock}, {m_restoreHeight}, Qt::Vertical);
        m_title->setText("Console Output");
        m_title->setStyleSheet("font-weight: bold;");
    }
    m_minimized = minimized;
    m_minimizeButton->setIcon(QIcon(minimized ? ":/icons/dock/restore.svg" : ":/icons/dock/minimize.svg"));
    m_minimizeButton->setToolTip(minimized ? "Restore the console" : "Minimize the console");
}
