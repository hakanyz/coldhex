#pragma once

#include <QByteArray>
#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QTimer>

// Tells when the content of one file changes on disk, for example after a rebuild. A compiler
// writes the file in pieces and may delete and recreate it, so a change is only reported after
// the file has been quiet for a moment, and only when its content really differs from before.
class FileWatcher : public QObject {
    Q_OBJECT

public:
    explicit FileWatcher(QObject* parent = nullptr, int quietMs = 500);

    // Starts watching `path` and remembers its current content. Empty stops watching.
    // Does nothing when `path` is already the watched one.
    void setFile(const QString& path);
    QString path() const { return m_path; }

signals:
    void changed();

private slots:
    void onSomethingChanged();
    void check();

private:
    static QByteArray hashOf(const QString& path);
    void rewatch();

    QString m_path;
    QByteArray m_hash;
    QFileSystemWatcher m_watcher;
    QTimer m_quiet;
};
