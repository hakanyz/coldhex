#include "Theme.h"
#include <QApplication>
#include <QColor>
#include <QCursor>
#include <QEvent>
#include <QFile>
#include <QLabel>
#include <QPalette>
#include <QPointer>
#include <QStyleFactory>
#include <QTimer>
#include <QToolBar>
#include <QToolTip>

namespace {

struct Entry {
    const char* name;
    const char* hex;
};

const Entry kColors[] = {
#define COLDHEX_ENTRY(name, hex) {#name, hex},
    COLDHEX_COLORS(COLDHEX_ENTRY)
#undef COLDHEX_ENTRY
};

QColor qcolor(Theme::Color c) {
    return QColor(Theme::hex(c));
}

QPalette darkPalette() {
    using C = Theme::Color;
    QPalette p;
    p.setColor(QPalette::Window, qcolor(C::window));
    p.setColor(QPalette::WindowText, qcolor(C::text));
    p.setColor(QPalette::Base, qcolor(C::consoleBase));
    p.setColor(QPalette::AlternateBase, qcolor(C::window));
    p.setColor(QPalette::ToolTipBase, qcolor(C::consoleBase));
    p.setColor(QPalette::ToolTipText, qcolor(C::text));
    p.setColor(QPalette::Text, qcolor(C::textPrimary));
    p.setColor(QPalette::Button, qcolor(C::paletteButton));
    p.setColor(QPalette::ButtonText, qcolor(C::textPrimary));
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Link, qcolor(C::link));
    p.setColor(QPalette::Highlight, qcolor(C::accent));
    p.setColor(QPalette::HighlightedText, qcolor(C::textBright));
    p.setColor(QPalette::Disabled, QPalette::Text, qcolor(C::textDisabled));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, qcolor(C::textDisabled));
    return p;
}

// Draws widget tooltips as a compact, borderless label right above the hovered widget instead of
// Qt's bulky native tooltip box below the cursor.
class CompactToolTipFilter : public QObject {
public:
    explicit CompactToolTipFilter(QApplication* app) : QObject(app) {
        setObjectName("coldhexCompactToolTipFilter");
        m_hideTimer.setSingleShot(true);
        m_hideTimer.setInterval(4000);
        connect(&m_hideTimer, &QTimer::timeout, this, &CompactToolTipFilter::hideTip);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        switch (event->type()) {
        case QEvent::ToolTip: {
            auto* w = qobject_cast<QWidget*>(watched);
            if (!w || !w->isVisible() || w->toolTip().isEmpty()) return false;
            QWidget* win = w->window();
            if (!win || (win->windowFlags() & Qt::Popup)) return false;
            showTip(w, win, w->toolTip());
            return true;
        }
        case QEvent::Leave:
            if (watched == m_target) hideTip();
            break;
        case QEvent::MouseMove:
            if (m_target && m_label && m_label->isVisible()) {
                const QPoint local = m_target->mapFromGlobal(QCursor::pos());
                if (!m_target->rect().contains(local)) hideTip();
            }
            break;
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        case QEvent::Wheel:
        case QEvent::KeyPress:
        case QEvent::WindowDeactivate:
            hideTip();
            break;
        case QEvent::Hide:
        case QEvent::Destroy:
            if (watched == m_target || watched == m_window) hideTip();
            break;
        default:
            break;
        }
        return false;
    }

private:
    void showTip(QWidget* target, QWidget* win, const QString& text) {
        if (m_window != win || !m_label) {
            if (m_label) m_label->deleteLater();
            m_window = win;
            m_label = new QLabel(win);
            m_label->setAttribute(Qt::WA_TransparentForMouseEvents);
            m_label->setAttribute(Qt::WA_StyledBackground, true);
            m_label->setStyleSheet(QString(
                "QLabel {"
                " background-color: %1;"
                " color: %2;"
                " border: none;"
                " border-radius: 3px;"
                " padding: 1px 6px;"
                " font-size: 11px;"
                "}")
                .arg(Theme::hex(Theme::Color::consoleBase),
                     Theme::hex(Theme::Color::text)));
        }

        m_target = target;
        m_label->setWordWrap(false);
        m_label->setMinimumSize(0, 0);
        m_label->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        m_label->setText(text);
        m_label->adjustSize();

        constexpr int kMaxWidth = 320;
        if (m_label->width() > kMaxWidth) {
            m_label->setWordWrap(true);
            m_label->setFixedWidth(kMaxWidth);
            m_label->adjustSize();
        }

        const QPoint topLeft = target->mapTo(win, QPoint(0, 0));
        const QRect widgetRect(topLeft, target->size());
        const QSize sz = m_label->size();

        int x = widgetRect.center().x() - sz.width() / 2;
        x = qBound(4, x, qMax(4, win->width() - sz.width() - 4));

        int y = widgetRect.top() - sz.height() - 2;
        if (y < 2) {
            const bool inToolBar = qobject_cast<QToolBar*>(target->parentWidget()) != nullptr;
            y = inToolBar ? 2 : (widgetRect.bottom() + 2);
        }

        m_label->move(x, y);
        m_label->raise();
        m_label->show();
        m_hideTimer.start();
    }

    void hideTip() {
        m_hideTimer.stop();
        m_target = nullptr;
        if (m_label) m_label->hide();
    }

    QPointer<QWidget> m_target;
    QPointer<QWidget> m_window;
    QPointer<QLabel> m_label;
    QTimer m_hideTimer;
};

}  // namespace

QString Theme::hex(Color color) {
    return QString::fromLatin1(kColors[static_cast<int>(color)].hex);
}

QString Theme::logColor(const QString& level) {
    const QString l = level.toUpper();
    if (l == "ERROR") return hex(Color::logError);
    if (l == "WARNING" || l == "WARN") return hex(Color::logWarning);
    if (l == "SUCCESS") return hex(Color::logSuccess);
    if (l == "INFO") return hex(Color::logInfo);
    if (l == "DEBUG") return hex(Color::logDebug);
    return hex(Color::text);
}

QString Theme::renderStyleSheet(const QString& templateText) {
    QString result = templateText;
    for (const Entry& e : kColors) {
        result.replace(QString("@%1@").arg(QLatin1String(e.name)), QLatin1String(e.hex));
    }
    return result;
}

void Theme::applyDark(QApplication& app) {
    app.setStyle(QStyleFactory::create("Fusion"));
    const QPalette p = darkPalette();
    app.setPalette(p);
    QToolTip::setPalette(p);

    QFile sheet(":/theme/dark.qss.in");
    if (sheet.open(QIODevice::ReadOnly)) {
        app.setStyleSheet(renderStyleSheet(QString::fromUtf8(sheet.readAll())));
    }

    if (!app.findChild<QObject*>("coldhexCompactToolTipFilter", Qt::FindDirectChildrenOnly)) {
        app.installEventFilter(new CompactToolTipFilter(&app));
    }
}
