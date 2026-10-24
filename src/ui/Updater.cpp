#include "Updater.h"
#include <QApplication>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslSocket>
#include <QUrl>

namespace {
const char* kLatestReleaseUrl = "https://api.github.com/repos/hakanyz/coldhex/releases/latest";
constexpr int kTimeoutMs = 15000;
constexpr qint64 kMaxAnswerBytes = 1024 * 1024;
}

Updater::Updater(QObject* parent)
    : QObject(parent)
{
}

void Updater::check(bool manual) {
    if (m_reply) return;  // a check is already running
    m_manual = manual;

    if (!QSslSocket::supportsSsl()) {
        emit failed("This copy of ColdHex cannot make secure (HTTPS) connections.", manual);
        return;
    }

    QNetworkRequest request{QUrl(QString::fromLatin1(kLatestReleaseUrl))};
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", QByteArray("ColdHex/") + QApplication::applicationVersion().toUtf8());
    request.setTransferTimeout(kTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_reply = m_network.get(request);
    connect(m_reply, &QNetworkReply::finished, this, &Updater::onFinished);
}

void Updater::onFinished() {
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray answer = reply->read(kMaxAnswerBytes);

    // 404: the project has no release (yet), so there is nothing newer.
    if (status == 404) {
        emit upToDate(m_manual);
        return;
    }
    if (reply->error() != QNetworkReply::NoError && answer.isEmpty()) {
        emit failed(reply->errorString(), m_manual);
        return;
    }

    QString why;
    const ReleaseInfo release = UpdateInfo::parseRelease(answer, currentPlatform(), &why);
    if (!release.valid) {
        emit failed(why.isEmpty() ? reply->errorString() : why, m_manual);
        return;
    }

    if (UpdateInfo::isNewer(release.version, QApplication::applicationVersion())) {
        emit updateAvailable(release, m_manual);
    } else {
        emit upToDate(m_manual);
    }
}
