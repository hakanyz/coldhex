#pragma once

#include "core/UpdateInfo.h"
#include <QNetworkAccessManager>
#include <QObject>

class QNetworkReply;

// Asks GitHub for the newest release of ColdHex and tells whether it is newer than this program.
// `manual` is true when the user asked for the check, so that the answer is always shown.
class Updater : public QObject {
    Q_OBJECT

public:
    explicit Updater(QObject* parent = nullptr);

    void check(bool manual);
    bool isChecking() const { return m_reply != nullptr; }

signals:
    void updateAvailable(const ReleaseInfo& release, bool manual);
    void upToDate(bool manual);
    void failed(const QString& reason, bool manual);

private:
    void onFinished();

    QNetworkAccessManager m_network;
    QNetworkReply* m_reply = nullptr;
    bool m_manual = false;
};
