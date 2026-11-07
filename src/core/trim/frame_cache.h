#pragma once
// The Trim preview's frames, decoded ONCE when the Trim window opens by one
// long-lived ffmpeg process (not one run per shown frame) and held in memory at
// the preview's display size. Playback, scrubbing and Handle drags then read
// pictures from here.
//
// Memory is bounded by `budget` bytes: `plan` picks the largest display size that
// fits every frame; if even the smallest does not (a long clip), it keeps every
// Nth frame instead (`stride`). `frameAt` then answers with the nearest cached
// frame, so there is never a blank picture, and `isExact` says whether it is the
// frame asked for (the window refines an inexact, settled frame on demand).
#include <QImage>
#include <QObject>
#include <QProcess>
#include <QSize>
#include <QTimer>
#include <QVector>

#include <memory>

namespace tarn {

class FrameCache : public QObject {
    Q_OBJECT
public:
    // What the cache will hold. Pure; tested.
    struct Plan {
        QSize size;        // display size of every cached frame (even numbers)
        int stride = 1;    // every Nth frame of the clip is cached
        qint64 frames = 0; // cached frames in all
        qint64 bytes = 0;  // frames * size * 4, never above the budget (but at least one frame)
    };
    static constexpr qint64 kDefaultBudget = 512LL * 1024 * 1024;
    static Plan plan(const QSize &videoSize, qint64 clipFrames, const QSize &displayBox, qint64 budget);
    static QStringList arguments(const QString &video, const Plan &plan, int fps);

    FrameCache(qint64 budget, const QSize &displayBox, QObject *parent = nullptr);
    ~FrameCache() override;

    // (Re)starts decoding `video`, dropping anything cached before.
    void start(const QString &video, const QSize &videoSize, qint64 clipFrames, int fps);
    void stop();

    // The cached frame nearest to clip frame `frame`; a null image only before the first one is decoded.
    QImage frameAt(qint64 frame) const;
    // True when `frameAt(frame)` is that very frame (cached and decoded).
    bool isExact(qint64 frame) const;
    qint64 count() const { return m_frames.size(); }
    qint64 bytes() const { return m_bytes; }
    bool complete() const { return m_complete; }
    bool running() const { return m_process && m_process->state() != QProcess::NotRunning; }
    const Plan &currentPlan() const { return m_plan; }

signals:
    // New frames arrived (at most ~10 times a second).
    void changed();

private:
    void onReadyRead();
    void onFinished();
    void flushChanged();

    qint64 m_budget;
    QSize m_box;
    Plan m_plan;
    std::unique_ptr<QProcess> m_process;
    QVector<QImage> m_frames;
    QByteArray m_pending;
    qint64 m_bytes = 0;
    bool m_complete = false;
    bool m_dirty = false;
    QTimer m_throttle;
};

} // namespace tarn
