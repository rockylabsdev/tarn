#pragma once
// image://trim/<captureId>/<generation>/f<frame>      one frame of the Recording
// image://trim/<captureId>/<generation>/strip<tiles>  the filmstrip: that many frames
//                                                     side by side, evenly spaced, 104 px tall
// image://trimcache/<captureId>/<generation>/c<frame>/<tick>
//                                                     the FRAME CACHE'S nearest picture, at once and
//                                                     with no process (TrimSession::cachedFrame). The
//                                                     tick only makes a new URL when more frames arrived.
// The first two are made by ffmpeg on the Recording's kept file, asynchronously, so the
// window opens at once and the preview fills in a moment later. The generation
// changes when Trim replaces the file, so a cached picture is never reused.
#include <QQuickAsyncImageProvider>
#include <QQuickImageProvider>

namespace tarn {

class Trim;

class TrimFrames : public QQuickAsyncImageProvider {
public:
    explicit TrimFrames(Trim &trim) : m_trim(trim) {}
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

    // The ffmpeg arguments for each kind of picture (pure; tested).
    static QStringList frameArguments(const QString &video, qint64 frame, int fps);
    static QStringList stripArguments(const QString &video, qint64 durationMs, int tiles);

private:
    Trim &m_trim;
};

// Pictures from the session's FrameCache. Synchronous: a copy of an in-memory image is
// microseconds, and a synchronous provider can never show a blank frame in between.
class TrimCacheFrames : public QQuickImageProvider {
public:
    explicit TrimCacheFrames(Trim &trim) : QQuickImageProvider(QQuickImageProvider::Image), m_trim(trim) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    Trim &m_trim;
};

} // namespace tarn
