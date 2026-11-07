#include "trim.h"

#include "daemon/daemon.h"
#include "editor/opening_size.h"
#include "hyprland/hyprland_bridge.h"
#include "thumbnails/thumbnail_stack.h"

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QStandardPaths>

namespace tarn {

Trim::Trim(Daemon &daemon) : m_daemon(daemon) {
    auto &thumbs = stack();
    connect(&thumbs, &ThumbnailStack::trimRequested, this, &Trim::open);
    connect(&thumbs, &ThumbnailStack::closed, this, &Trim::onClosed);
}

ThumbnailStack &Trim::stack() { return m_daemon.service<ThumbnailStack>(); }

QString Trim::workDir() {
    QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if (cache.isEmpty())
        cache = QDir::homePath() + QStringLiteral("/.cache");
    return cache + QStringLiteral("/tarn/trim-work");
}

void Trim::clearWorkDir() {
    QDir(workDir()).removeRecursively();
}

TrimSession *Trim::session(int captureId) const {
    const auto it = m_sessions.find(captureId);
    return it == m_sessions.end() ? nullptr : it->second;
}

TrimSession *Trim::open(int captureId) {
    if (TrimSession *existing = session(captureId))
        return existing;
    auto &thumbs = stack();
    if (thumbs.isHidden(captureId) || thumbs.kind(captureId) != ThumbnailStack::Recording)
        return nullptr;
    // A GIF Recording is trimmed through the MP4 it was made from, and re-made from that.
    QString path = thumbs.videoPath(captureId);
    QString gifPath;
    if (QFileInfo(path).suffix().compare(QStringLiteral("gif"), Qt::CaseInsensitive) == 0) {
        gifPath = path;
        path = thumbs.sourcePath(captureId);
        if (!QFileInfo::exists(gifPath))
            return nullptr;
    }
    if (QFileInfo(path).suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive) != 0 || !QFileInfo::exists(path))
        return nullptr;
    const qint64 durationMs = thumbs.durationMs(captureId);
    if (durationMs <= 0)
        return nullptr;

    // The poster frame is a frame of the video: its size is the video's.
    const QSize videoSize = thumbs.image(captureId).size();
    const MonitorSize monitor = monitorSizeUnderPointer(m_daemon);
    const QSize size = editorOpeningSize(videoSize, monitor.size, monitor.scale);

    auto *created = new TrimSession(*this, captureId, path, durationMs, videoSize, size, gifPath);
    created->setParent(this);
    m_sessions[captureId] = created;
    thumbs.setHidden(captureId, true); // out of the stack, not counted toward the 4
    emit opened(created);
    // Connected after the window's own handler, so the window is deleted first
    // and its QML never sees the session vanish under it.
    connect(created, &TrimSession::finished, created, &QObject::deleteLater);
    return created;
}

void Trim::release(TrimSession *session) {
    const int id = session->captureId();
    m_sessions.erase(id);
    stack().setHidden(id, false); // no-op if the Thumbnail is gone
}

void Trim::onClosed(int captureId) {
    if (TrimSession *open = session(captureId)) {
        m_sessions.erase(captureId);
        open->end();
    }
}

TARN_MODULE(trim, [](Daemon &daemon) {
    Trim::clearWorkDir(); // a crash may have left a half-written work file
    daemon.service<Trim>(); // listens for Recording clicks from the start
});

} // namespace tarn
