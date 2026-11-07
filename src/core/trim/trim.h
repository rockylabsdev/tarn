#pragma once
// The Trim service: opens a Trim window on a Recording's Thumbnail (a click or
// its Trim button), hides the Thumbnail while the window is open, and owns the
// work folder for the re-encode.
//
// The QML window is a thin layer (src/ui/trim_windows.*) that listens for
// `opened`; everything the user does goes through TrimSession.
#include "trim_session.h"

#include <QObject>
#include <map>

namespace tarn {

class Daemon;
class ThumbnailStack;

class Trim : public QObject {
    Q_OBJECT
public:
    explicit Trim(Daemon &daemon);

    // Opens (or returns the already open) Trim window for a Recording's
    // Thumbnail. Null for an unknown or hidden id, a screenshot, or a Recording
    // that is not an MP4 on disk (a GIF is re-made from its trimmed MP4).
    TrimSession *open(int captureId);
    TrimSession *session(int captureId) const;

    // $XDG_CACHE_HOME/tarn/trim-work: where ffmpeg writes before the rename. Not
    // the kept folder, so a crash never saves a half-written file as a Recording.
    static QString workDir();
    // Start-up: deletes whatever a crash left in the work folder.
    static void clearWorkDir();

    // The most memory a preview FrameCache may hold (bytes); read when a window opens.
    qint64 cacheBudget() const { return m_cacheBudget; }
    void setCacheBudget(qint64 bytes) { m_cacheBudget = bytes; }

signals:
    // A new Trim window opened: the UI puts a window on it. It is deleted after `finished`.
    void opened(tarn::TrimSession *session);

private:
    friend class TrimSession;
    ThumbnailStack &stack();
    Daemon &daemon() { return m_daemon; }
    void release(TrimSession *session);
    void onClosed(int captureId);

    Daemon &m_daemon;
    std::map<int, TrimSession *> m_sessions;
    qint64 m_cacheBudget = FrameCache::kDefaultBudget;
};

} // namespace tarn
