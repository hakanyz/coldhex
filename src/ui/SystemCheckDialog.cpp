#include "SystemCheckDialog.h"
#include "Theme.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

SystemCheckDialog::SystemCheckDialog(const QString& probeRsPath, QWidget* parent)
    : QDialog(parent), m_probeRsPath(probeRsPath), m_view(new QTextBrowser(this))
{
    setWindowTitle("System Check");
    resize(620, 460);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_view);

    auto* buttons = new QDialogButtonBox(this);
    QPushButton* recheck = buttons->addButton("Check Again", QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);

    connect(recheck, &QPushButton::clicked, this, &SystemCheckDialog::runCheck);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

bool SystemCheckDialog::runCheck() {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const SystemCheck::Input input = SystemCheck::gather(m_probeRsPath);
    QApplication::restoreOverrideCursor();
    return showResult(input);
}

bool SystemCheckDialog::showResult(const SystemCheck::Input& input) {
    const QList<CheckItem> items = SystemCheck::evaluate(input);
    m_view->setHtml(toHtml(items));
    return SystemCheck::needsAttention(items);
}

QString SystemCheckDialog::toHtml(const QList<CheckItem>& items) {
    QString html = "<h3>What ColdHex found on this computer</h3>";
    for (const CheckItem& item : items) {
        QString icon;
        QString color;
        switch (item.status) {
        case CheckItem::Status::Ok:      icon = "&#10004;"; color = Theme::hex(Theme::Color::logSuccess); break;
        case CheckItem::Status::Info:    icon = "&#8505;";  color = Theme::hex(Theme::Color::logInfo); break;
        case CheckItem::Status::Warning: icon = "&#9888;";  color = Theme::hex(Theme::Color::logWarning); break;
        case CheckItem::Status::Error:   icon = "&#10006;"; color = Theme::hex(Theme::Color::logError); break;
        }
        html += QString("<p><span style='color:%1; font-size:15px;'><b>%2 %3</b></span>")
                    .arg(color, icon, item.title.toHtmlEscaped());
        if (!item.detail.isEmpty()) {
            html += "<br>" + item.detail.toHtmlEscaped().replace("\n", "<br>");
        }
        html += "</p>";
    }
    return html;
}
