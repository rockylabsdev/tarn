#include "watched_file.h"

#include <QFileInfo>

namespace tarn {

WatchedFile::WatchedFile(QObject *parent) : QObject(parent) {
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(40);
    connect(&m_debounce, &QTimer::timeout, this, &WatchedFile::changed);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce, qOverload<>(&QTimer::start));
}

void WatchedFile::setPath(const QString &path) {
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    m_path = path;
    rewatch();
}

void WatchedFile::rewatch() {
    if (m_path.isEmpty())
        return;
    const QFileInfo file(m_path);
    const QString dir = file.absolutePath();
    const QStringList wanted = {file.absoluteFilePath(), dir, QFileInfo(dir).absolutePath()};
    QStringList todo;
    for (const QString &p : wanted)
        if (QFileInfo::exists(p) && !m_watcher.files().contains(p) && !m_watcher.directories().contains(p))
            todo << p;
    if (!todo.isEmpty())
        m_watcher.addPaths(todo);
}

} // namespace tarn
