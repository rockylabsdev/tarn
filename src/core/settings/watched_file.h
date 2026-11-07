#pragma once
// Watches one file another tool writes, and says when it may have changed.
//
// A tool may replace the file by renaming over it, which ends a plain file
// watch, so the folders above it are watched too and everything is added back
// after each change. A burst of events becomes one `changed` signal, 40 ms later.
// The file may not exist yet: it is picked up when it appears.
#include <QFileSystemWatcher>
#include <QObject>
#include <QString>
#include <QTimer>

namespace tarn {

class WatchedFile : public QObject {
    Q_OBJECT
public:
    explicit WatchedFile(QObject *parent = nullptr);

    // Watch this file instead (empty: none).
    void setPath(const QString &path);
    // Add back any watch a replacement dropped. Call after reading the file.
    void rewatch();

signals:
    void changed();

private:
    QString m_path;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};

} // namespace tarn
