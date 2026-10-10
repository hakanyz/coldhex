#include "Theme.h"
#include <QApplication>
#include <QColor>
#include <QFile>
#include <QPalette>
#include <QStyleFactory>

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
    p.setColor(QPalette::ToolTipBase, qcolor(C::textBright));
    p.setColor(QPalette::ToolTipText, qcolor(C::textBright));
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
    app.setPalette(darkPalette());

    QFile sheet(":/theme/dark.qss.in");
    if (sheet.open(QIODevice::ReadOnly)) {
        app.setStyleSheet(renderStyleSheet(QString::fromUtf8(sheet.readAll())));
    }
}
