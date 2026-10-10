#include "Theme.h"
#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QStyleFactory>

namespace {

QPalette darkPalette() {
    QPalette p;
    p.setColor(QPalette::Window, QColor(32, 33, 36));
    p.setColor(QPalette::WindowText, QColor(220, 220, 220));
    p.setColor(QPalette::Base, QColor(24, 25, 27));
    p.setColor(QPalette::AlternateBase, QColor(32, 33, 36));
    p.setColor(QPalette::ToolTipBase, Qt::white);
    p.setColor(QPalette::ToolTipText, Qt::white);
    p.setColor(QPalette::Text, QColor(225, 225, 225));
    p.setColor(QPalette::Button, QColor(45, 47, 51));
    p.setColor(QPalette::ButtonText, QColor(225, 225, 225));
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Link, QColor(41, 128, 185));
    p.setColor(QPalette::Highlight, QColor(0, 122, 204));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(110, 110, 110));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(110, 110, 110));
    return p;
}

}  // namespace

void Theme::applyDark(QApplication& app) {
    app.setStyle(QStyleFactory::create("Fusion"));
    app.setPalette(darkPalette());

    QFile sheet(":/theme/dark.qss");
    if (sheet.open(QIODevice::ReadOnly)) {
        app.setStyleSheet(QString::fromUtf8(sheet.readAll()));
    }
}
