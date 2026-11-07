#pragma once
// One open Trim window on a Recording: the kept range (two Handles that snap to
// whole frames), the Playhead, and the ways out. The QML Trim window and the
// tests drive the same calls.
//
// Positions are frame boundaries: 0 is before the first frame and frameCount()
// after the last. The kept range is [startFrame, endFrame): at least one frame.
//
// Done re-encodes Tarn's kept copy with ffmpeg into a work file and then renames
// it over the kept path. A rename swaps the directory entry and never writes into
// the old file, so a file that was already saved (a separate copy) or copied (a
// hard link in the shared folder) is left exactly as it was.
#include "frame_cache.h"

#include <QImage>
#include <QObject>
#include <QElapsedTimer>
#include <QProcess>
#include <QTimer>
#include <QSize>
#include <QString>

#include <memory>

namespace tarn {

class Trim;

class TrimSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(int captureId READ captureId CONSTANT)
    Q_PROPERTY(QSize openingSize READ openingSize CONSTANT)
    Q_PROPERTY(QSize videoSize READ videoSize CONSTANT)
    Q_PROPERTY(int fps READ fps CONSTANT)
    Q_PROPERTY(qint64 frameCount READ frameCount NOTIFY sourceChanged)
    Q_PROPERTY(qint64 startFrame READ startFrame NOTIFY rangeChanged)
    Q_PROPERTY(qint64 endFrame READ endFrame NOTIFY rangeChanged)
    Q_PROPERTY(qint64 playheadFrame READ playheadFrame NOTIFY playheadChanged)
    Q_PROPERTY(qint64 previewFrame READ previewFrame NOTIFY playheadChanged)
    Q_PROPERTY(qint64 keptFrames READ keptFrames NOTIFY rangeChanged)
    Q_PROPERTY(QString keptText READ keptText NOTIFY rangeChanged)
    Q_PROPERTY(QString durationText READ durationText NOTIFY sourceChanged)
    Q_PROPERTY(bool trimming READ trimming NOTIFY trimmingChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(int generation READ generation NOTIFY sourceChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool gif READ isGif CONSTANT)
    Q_PROPERTY(int cacheTick READ cacheTick NOTIFY cacheChanged)
public:
    // `gifPath` is set for a GIF Recording: `videoPath` is then its kept MP4, Trim
    // works on that at 20 fps, and Done re-makes the GIF at `gifPath` from it.
    TrimSession(Trim &trim, int captureId, const QString &videoPath, qint64 durationMs, const QSize &videoSize,
                const QSize &openingSize, const QString &gifPath = {});
    ~TrimSession() override;

    int captureId() const { return m_captureId; }
    QString videoPath() const { return m_videoPath; }
    QSize openingSize() const { return m_openingSize; }
    QSize videoSize() const { return m_videoSize; }
    int fps() const; // 60 for an MP4 Recording, 20 for a GIF's
    bool isGif() const { return !m_gifPath.isEmpty(); }
    QString gifPath() const { return m_gifPath; }
    qint64 frameCount() const { return m_frameCount; }
    qint64 startFrame() const { return m_start; }
    qint64 endFrame() const { return m_end; }
    qint64 keptFrames() const { return m_end - m_start; }
    qint64 playheadFrame() const { return m_playhead; }
    // The frame the preview shows: the Playhead, but never past the last kept frame.
    qint64 previewFrame() const;
    qint64 keptMs() const;
    qint64 durationMs() const { return m_durationMs; }
    bool trimming() const { return m_trimming; }
    qreal progress() const { return m_progress; }
    // Counts up when the video file was replaced, so a cached frame is not reused.
    int generation() const { return m_generation; }
    bool isOpen() const { return m_open; }
    // Nothing is cut: the range is the whole clip.
    bool unchanged() const { return m_start == 0 && m_end == m_frameCount; }

    // The preview's frames (FrameCache): decoded once, in the background, when the window opens.
    // `cachedFrame` is the nearest cached picture (null before the first is decoded); `previewExact`
    // says whether it is the frame under the Playhead. `cacheTick` changes whenever frames arrive.
    FrameCache &frameCache() { return *m_cache; }
    QImage cachedFrame(qint64 frame) const { return m_cache->frameAt(frame); }
    Q_INVOKABLE bool previewExact() const { return m_cache->isExact(previewFrame()); }
    Q_INVOKABLE bool cacheHasFrames() const { return m_cache->count() > 0; }
    int cacheTick() const { return m_cacheTick; }

    // "0:04.40" for a frame boundary; and what the bars show.
    Q_INVOKABLE QString timeText(qint64 frame) const;
    QString keptText() const;      // "Keeps 0:07.60" without the word
    QString durationText() const;

    // Dragging a Handle (or setting it by time): the position snaps to the
    // nearest whole frame, the range keeps at least one frame, and the preview
    // and Playhead follow the Handle. Times in ms from the start of the clip.
    Q_INVOKABLE void dragStart(qreal ms);
    Q_INVOKABLE void dragEnd(qreal ms);
    // Pressing or dragging on the filmstrip: the Playhead, kept inside the range.
    Q_INVOKABLE void scrub(qreal ms);
    Q_INVOKABLE void setStartFrame(qint64 frame);
    Q_INVOKABLE void setEndFrame(qint64 frame);
    Q_INVOKABLE void setPlayheadFrame(qint64 frame);
    // Playback: Space. Plays the kept range on a loop from the Playhead (from the
    // start Handle if it is not inside the range) and keeps time by the clock, so a
    // slow preview drops frames and never slows the clip. `advance` is what the
    // clock calls; tests call it with the milliseconds that passed.
    bool playing() const { return m_playing; }
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void advance(qint64 ms);
    // Transport and keys. All of them pause playback except the stepping of the loop itself.
    Q_INVOKABLE void toStart(); // Home: the Playhead to the start Handle
    Q_INVOKABLE void toEnd();   // End: to the end Handle
    Q_INVOKABLE void stepFrames(int frames); // Left / Right: one frame
    Q_INVOKABLE void stepSeconds(int seconds); // Shift+Left / Right: one second
    // Start here / End here: the Handle goes to the Playhead.
    Q_INVOKABLE void startHere();
    Q_INVOKABLE void endHere();

    // Done (Enter): re-encodes the kept copy to the range, then the window
    // closes and the Thumbnail returns with its new length. Nothing cut = it
    // just closes. While trimming, `progress` runs 0..1.
    Q_INVOKABLE void done();
    // The Cancel button while trimming: stops ffmpeg, drops the work file, back
    // to editing. Tarn's copy is as it was.
    Q_INVOKABLE void cancelTrim();
    // Esc: stops any trim, drops it, closes the window. The Thumbnail is as it was.
    Q_INVOKABLE void cancel();
    // Copy (Ctrl+C): trims first if the range was cut, then copies the file and
    // closes window and Thumbnail.
    Q_INVOKABLE void copyAndClose();
    // Save as… (Ctrl+S): trims first if cut, then writes the file to `path` and
    // closes window and Thumbnail.
    Q_INVOKABLE void saveAs(const QString &path);
    Q_INVOKABLE QString saveAsFolder() const;

signals:
    void sourceChanged();
    void rangeChanged();
    void playheadChanged();
    void playingChanged();
    void cacheChanged();
    void trimmingChanged();
    void progressChanged();
    // The Trim window is over (any way out): its window goes.
    void finished();

private:
    friend class Trim;
    enum class After { Close, Copy, SaveAs };
    // The Thumbnail is gone (or the daemon is stopping): no further stack calls.
    void end();
    void startTrim(After after, const QString &path = {});
    void onProcessOutput();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void dropWork();
    void startGifStage();
    void onGifOutput();
    void onGifFinished();
    void killGif();
    void commit();
    void finishCommit(const QByteArray &poster); // after the new first frame is made
    void abortTrim(const QString &why);
    void setProgress(qreal progress);
    void reloadFromFile();
    void leave(); // reveal the Thumbnail and tell the window to go
    void setPlayheadClamped(qint64 frame);

    Trim &m_trim;
    std::unique_ptr<FrameCache> m_cache;
    int m_cacheTick = 0;
    int m_captureId;
    QString m_videoPath;
    qint64 m_durationMs;
    QSize m_videoSize;
    QSize m_openingSize;
    qint64 m_frameCount;
    qint64 m_start = 0;
    qint64 m_end;
    qint64 m_playhead = 0;
    bool m_open = true;
    bool m_trimming = false;
    bool m_committing = false; // the files are swapped; the new poster is being made
    bool m_canceled = false;
    qreal m_progress = 0;
    int m_generation = 0;
    After m_after = After::Close;
    QString m_afterPath;
    QString m_workPath;
    std::unique_ptr<QProcess> m_process;
    QByteArray m_pendingOutput;
    // Playback
    bool m_playing = false;
    QTimer m_clock;
    QElapsedTimer m_clockSince;
    qint64 m_playedMs = 0;
    qint64 m_playedFrames = 0;
    // GIF
    QString m_gifPath;
    bool m_gifStage = false;
    std::unique_ptr<QProcess> m_gifski;
    QByteArray m_gifTail;
};

} // namespace tarn
