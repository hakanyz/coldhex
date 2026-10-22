#include "FileWatcher.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

FileWatcher::FileWatcher(QObject* parent, int quietMs)
    : QObject(parent)
{
    m_quiet.setSingleShot(true);
    m_quiet.setInterval(quietMs);
    connect(&m_quiet, &QTimer::timeout, this, &FileWatcher::check);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &FileWatcher::onSomethingChanged);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &FileWatcher::onSomethingChanged);
}

void FileWatcher::setFile(const QString& path) {
    if (path == m_path) return;

    m_quiet.stop();
    m_path = path;
    m_hash = path.isEmpty() ? QByteArray() : hashOf(path);
    rewatch();
}

// Empty when the file cannot be read (for example while the compiler is replacing it).
QByteArray FileWatcher::hashOf(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QByteArray();
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(&file);
    return hash.result();
}

// The watcher drops a file that is deleted, so the folder is watched too, and the file is added
// again whenever it is back.
void FileWatcher::rewatch() {
    if (!m_watcher.files().isEmpty()) m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
    if (m_path.isEmpty()) return;

    const QFileInfo info(m_path);
    m_watcher.addPath(info.absolutePath());
    if (info.exists()) m_watcher.addPath(m_path);
}

void FileWatcher::onSomethingChanged() {
    m_quiet.start();
}

void FileWatcher::check() {
    rewatch();
    const QByteArray now = hashOf(m_path);
    if (now.isEmpty()) return;  // gone or locked: wait for the next event
    if (now == m_hash) return;
    m_hash = now;
    emit changed();
}
