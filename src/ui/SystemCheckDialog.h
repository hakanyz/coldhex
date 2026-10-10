#pragma once

#include "engine/SystemCheck.h"
#include <QDialog>

class QTextBrowser;

// Shows what ColdHex found on this computer (probe-rs, USB probes, drivers) and what to do
// about problems. It never changes the system.
class SystemCheckDialog : public QDialog {
    Q_OBJECT

public:
    explicit SystemCheckDialog(const QString& probeRsPath, QWidget* parent = nullptr);

    // Runs the check and fills the dialog. Returns true when something needs the user's attention.
    bool runCheck();

private:
    static QString toHtml(const QList<CheckItem>& items);

    QString m_probeRsPath;
    QTextBrowser* m_view = nullptr;
};
