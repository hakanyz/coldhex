#include "Updater.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QSslSocket>
#include <QStandardPaths>
#include <QUrl>

namespace {
const char* kLatestReleaseUrl = "https://api.github.com/repos/hakanyz/coldhex/releases/latest";
constexpr int kTimeoutMs = 15000;
constexpr int kDownloadTimeoutMs = 30000;
constexpr qint64 kMaxAnswerBytes = 1024 * 1024;
constexpr qint64 kMaxChecksumFileBytes = 64 * 1024;
constexpr qint64 kMaxCachedAnswerBytes = 200 * 1024;

QNetworkRequest makeRequest(const QUrl& url, int timeoutMs) {
    QNetworkRequest request{url};
    request.setRawHeader("User-Agent", QByteArray("ColdHex/") + QApplication::applicationVersion().toUtf8());
    request.setTransferTimeout(timeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

QString downloadFolder() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("ColdHex-update"));
}
}

Updater::Updater(QObject* parent)
    : QObject(parent)
{
}

// ---- the check -------------------------------------------------------------------------------

void Updater::check(bool manual) {
    if (m_reply) return;  // a check is already running
    m_manual = manual;

    if (!QSslSocket::supportsSsl()) {
        emit failed("This copy of ColdHex cannot make secure (HTTPS) connections.", manual);
        return;
    }

    QNetworkRequest request = makeRequest(QUrl(QString::fromLatin1(kLatestReleaseUrl)), kTimeoutMs);
    request.setRawHeader("Accept", "application/vnd.github+json");

    // Ask whether the answer changed since the last time. GitHub answers "not changed" (304) without
    // counting it against its limit on requests, so ColdHex can ask at every start.
    QSettings settings;
    const UpdateSettings saved = UpdateSettings::load(settings);
    if (!saved.etag.isEmpty() && !saved.cachedRelease.isEmpty()) {
        request.setRawHeader("If-None-Match", saved.etag.toUtf8());
    }

    m_reply = m_network.get(request);
    connect(m_reply, &QNetworkReply::finished, this, &Updater::onFinished);
}

void Updater::onFinished() {
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray answer = reply->read(kMaxAnswerBytes);

    // 404: the project has no release (yet), so there is nothing newer.
    if (status == 404) {
        emit upToDate(m_manual);
        return;
    }

    QSettings settings;
    UpdateSettings saved = UpdateSettings::load(settings);
    if (status == 304 && !saved.cachedRelease.isEmpty()) {
        answer = saved.cachedRelease.toUtf8();  // not changed since the last check
    } else if (reply->error() != QNetworkReply::NoError && answer.isEmpty()) {
        emit failed(reply->errorString(), m_manual);
        return;
    }

    QString why;
    const ReleaseInfo release = UpdateInfo::parseRelease(answer, currentPlatform(), &why);
    if (!release.valid) {
        emit failed(why.isEmpty() ? reply->errorString() : why, m_manual);
        return;
    }

    if (status == 200) {
        const QString etag = QString::fromLatin1(reply->rawHeader("ETag"));
        if (!etag.isEmpty() && answer.size() <= kMaxCachedAnswerBytes) {
            saved.etag = etag;
            saved.cachedRelease = QString::fromUtf8(answer);
            saved.save(settings);
        }
    }

    if (UpdateInfo::isNewer(release.version, QApplication::applicationVersion())) {
        emit updateAvailable(release, m_manual);
    } else {
        emit upToDate(m_manual);
    }
}

// ---- the download ----------------------------------------------------------------------------

void Updater::download(const ReleaseInfo& release) {
    if (m_downloadStage != DownloadStage::None) return;

    m_release = release;
    m_cancelled = false;
    m_received = 0;
    m_expectedChecksum.clear();

    // The name becomes a file name: it must be a plain name.
    if (release.assetName.isEmpty() || QFileInfo(release.assetName).fileName() != release.assetName) {
        emit downloadFailed("The release names its installer in a way that cannot be used.");
        return;
    }
    if (release.assetUrl.isEmpty() || release.checksumUrl.isEmpty()) {
        emit downloadFailed("The release has no installer or no checksum file.");
        return;
    }

    m_downloadStage = DownloadStage::Checksums;
    m_downloadReply = m_network.get(makeRequest(QUrl(release.checksumUrl), kDownloadTimeoutMs));
    connect(m_downloadReply, &QNetworkReply::finished, this, &Updater::onChecksumsReceived);
}

void Updater::onChecksumsReceived() {
    QNetworkReply* reply = m_downloadReply;
    m_downloadReply = nullptr;
    reply->deleteLater();
    if (m_cancelled) { finishDownload(QString()); return; }

    if (reply->error() != QNetworkReply::NoError) {
        finishDownload(QString("Could not read the checksum file: %1").arg(reply->errorString()));
        return;
    }
    const QByteArray body = reply->read(kMaxChecksumFileBytes + 1);
    if (body.size() > kMaxChecksumFileBytes) { finishDownload("The checksum file is too large."); return; }

    m_expectedChecksum = UpdateInfo::expectedChecksum(body, m_release.assetName);
    if (m_expectedChecksum.isEmpty()) {
        finishDownload("The checksum of the installer is not listed in the release.");
        return;
    }
    startInstallerDownload();
}

void Updater::startInstallerDownload() {
    // Earlier downloads are of no use any more.
    QDir folder(downloadFolder());
    if (folder.exists()) folder.removeRecursively();
    QDir().mkpath(downloadFolder());

    m_installerPath = QDir(downloadFolder()).filePath(m_release.assetName);
    m_installerFile.setFileName(m_installerPath);
    if (!m_installerFile.open(QIODevice::WriteOnly)) {
        finishDownload(QString("Could not write '%1': %2").arg(m_installerPath, m_installerFile.errorString()));
        return;
    }
    m_hash.reset();
    m_received = 0;

    m_downloadStage = DownloadStage::Installer;
    m_downloadReply = m_network.get(makeRequest(QUrl(m_release.assetUrl), kDownloadTimeoutMs));
    connect(m_downloadReply, &QNetworkReply::readyRead, this, &Updater::onInstallerData);
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64) {
        emit downloadProgress(received, m_release.assetSize);
    });
    connect(m_downloadReply, &QNetworkReply::finished, this, &Updater::onInstallerFinished);
}

void Updater::onInstallerData() {
    if (!m_downloadReply) return;
    const QByteArray chunk = m_downloadReply->readAll();
    m_received += chunk.size();
    // The release said how large the file is; a download that grows beyond it is not that file.
    if (m_received > m_release.assetSize + 1024) {
        m_downloadReply->abort();
        return;
    }
    m_hash.addData(chunk);
    if (m_installerFile.write(chunk) != chunk.size()) m_downloadReply->abort();
}

void Updater::onInstallerFinished() {
    if (m_downloadReply) onInstallerData();  // what arrived last
    QNetworkReply* reply = m_downloadReply;
    m_downloadReply = nullptr;
    reply->deleteLater();
    m_installerFile.close();
    if (m_cancelled) { finishDownload(QString()); return; }

    if (reply->error() != QNetworkReply::NoError) {
        finishDownload(QString("The download failed: %1").arg(reply->errorString()));
        return;
    }
    if (m_received != m_release.assetSize) {
        finishDownload(QString("The downloaded file has the wrong size (%1 of %2 bytes).").arg(m_received).arg(m_release.assetSize));
        return;
    }
    const QString actual = QString::fromLatin1(m_hash.result().toHex());
    if (actual != m_expectedChecksum) {
        finishDownload("The downloaded file does not match its checksum, so it was thrown away. It may be damaged or changed.");
        return;
    }

    m_downloadStage = DownloadStage::None;
    emit downloadFinished(m_installerPath);
}

void Updater::cancelDownload() {
    if (m_downloadStage == DownloadStage::None) return;
    m_cancelled = true;
    if (m_downloadReply) m_downloadReply->abort();
}

// Ends a download that did not succeed (an empty `error` means it was cancelled) and removes what it left.
void Updater::finishDownload(const QString& error) {
    m_downloadStage = DownloadStage::None;
    if (m_installerFile.isOpen()) m_installerFile.close();
    if (!m_installerPath.isEmpty()) QFile::remove(m_installerPath);
    if (!error.isEmpty()) emit downloadFailed(error);
}
