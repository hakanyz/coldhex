#pragma once

#include "core/UpdateInfo.h"
#include <QCryptographicHash>
#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>

class QNetworkReply;

// Asks GitHub for the newest release of ColdHex, and downloads the installer of a newer one.
// `manual` is true when the user asked for the check, so that the answer is always shown.
class Updater : public QObject {
    Q_OBJECT

public:
    explicit Updater(QObject* parent = nullptr);

    void check(bool manual);
    bool isChecking() const { return m_reply != nullptr; }

    // Downloads the installer of `release` and checks it against the SHA256SUMS.txt of the release.
    // Nothing is started: downloadFinished() gives the path of the verified file.
    void download(const ReleaseInfo& release);
    void cancelDownload();
    bool isDownloading() const { return m_downloadStage != DownloadStage::None; }

signals:
    void updateAvailable(const ReleaseInfo& release, bool manual);
    void upToDate(bool manual);
    void failed(const QString& reason, bool manual);

    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(const QString& installerPath);
    void downloadFailed(const QString& reason);

private:
    enum class DownloadStage { None, Checksums, Installer };

    void onFinished();
    void onChecksumsReceived();
    void startInstallerDownload();
    void onInstallerData();
    void onInstallerFinished();
    void finishDownload(const QString& error);

    QNetworkAccessManager m_network;
    QNetworkReply* m_reply = nullptr;
    bool m_manual = false;

    DownloadStage m_downloadStage = DownloadStage::None;
    QNetworkReply* m_downloadReply = nullptr;
    ReleaseInfo m_release;
    QString m_expectedChecksum;
    QString m_installerPath;
    QFile m_installerFile;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    qint64 m_received = 0;
    bool m_cancelled = false;
};
