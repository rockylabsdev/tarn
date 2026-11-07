#pragma once
// The Recording session: the one Recording that is starting, running or just
// finished, and the gpu-screen-recorder process behind it.
//
//   idle -> starting -> recording <-> paused -> encoding (GIF only) -> done
//                                          \-> deleted
//
// It is "recording" only once the recorder's output file exists (the recorder
// needs ~0.45 s; a pause signal sent before that kills it). Everything the user
// sees follows from the state: the area's outline is red from `starting` on, the
// Recording bar shows from `recording` on (barVisible). The Recording
// bar's controls use paused (the bar stays) and deleted; restart goes back
// through `starting`. Encoding is GIF; Ripples, the Shell socket
// and Trim add to this class instead of reshaping it.
//
// A GIF Recording is recorded as an MP4 like any other and kept; on stop
// the state is `encoding` while `ffmpeg | gifski` make the GIF beside it, with
// encodingProgress() 0..1 (at most 10 changes a second, for the bar and the Shell socket).
// No gifski, no GIF: the Recording does not start (a notification says why).
//
// The recorder is an asynchronous QProcess (ProcessRunner blocks), started with
// the arguments in recorderArguments() and stopped with SIGINT.
#include "storage/file_name.h"

#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QRect>
#include <QSize>
#include <QTimer>

namespace tarn {

class Daemon;
struct Reply;

class RecordingSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    // The recorded area, logical px of its monitor; null while idle.
    Q_PROPERTY(QRect area READ area NOTIFY areaChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)           // the outline shows (starting .. encoding)
    Q_PROPERTY(bool barVisible READ barVisible NOTIFY barVisibleChanged) // the Recording bar shows
    Q_PROPERTY(bool canPause READ canPause NOTIFY stateChanged)       // pause is enabled: the recorder is writing
    Q_PROPERTY(bool micOn READ micOn WRITE setMicOn NOTIFY micOnChanged) // the bar shows a mic glyph (set by the Record panel)
    Q_PROPERTY(QRect barRect READ barRect NOTIFY barRectChanged)      // null when it fits nowhere
    Q_PROPERTY(bool hasBar READ hasBar NOTIFY barRectChanged)
    // The bar fits nowhere outside the area, so it sits inside and auto-hides.
    Q_PROPERTY(bool autoHide READ autoHide NOTIFY barRectChanged)
    // The strip along the monitor's bottom edge that takes the pointer; null unless an auto-hiding bar's Recording runs.
    Q_PROPERTY(QRect stripRect READ stripRect NOTIFY stripChanged)
    Q_PROPERTY(QSize monitorSize READ monitorSize NOTIFY areaChanged)
    Q_PROPERTY(bool encoding READ encoding NOTIFY stateChanged)       // a GIF is being made
    Q_PROPERTY(double encodingProgress READ encodingProgress NOTIFY encodingProgressChanged)
    Q_PROPERTY(bool gif READ gif NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY stateChanged)           // the bar shows resume (QML avoids magic enum numbers)
public:
    enum State { Idle, Starting, Recording, Paused, Encoding, Done, Deleted };
    Q_ENUM(State)
    enum Format { Mp4, Gif };
    Q_ENUM(Format)

    explicit RecordingSession(Daemon &daemon);
    ~RecordingSession() override;

    State state() const { return m_state; }
    Format format() const { return m_format; }
    // starting .. encoding: a Recording is in progress and Ctrl+Print stops it.
    bool active() const { return m_state == Starting || m_state == Recording || m_state == Paused || m_state == Encoding; }
    // The bar goes at once when a stop is asked for (or on delete), not when the file is done;
    // it comes back as the progress ring while a GIF is encoding.
    // An auto-hiding bar shows only while m_autoShown (or while paused: Resume is on it).
    bool barVisible() const {
        return ((m_state == Recording || m_state == Paused) && !m_stopping && (!m_autoHide || m_autoShown || m_state == Paused)) ||
               m_state == Encoding;
    }
    bool paused() const { return m_state == Paused; }
    // A stop (or delete) has been asked for and the recorder is closing its file.
    bool stopping() const { return m_stopping; }
    // Pause only works once the recorder is writing: a pause signal sent earlier kills it.
    bool canPause() const { return m_state == Recording && !m_stopping; }
    bool micOn() const { return m_micOn; }
    void setMicOn(bool on);
    bool encoding() const { return m_state == Encoding; }
    bool gif() const { return m_format == Gif; }
    // 0..1 while encoding; 1 once the GIF is written. At most 10 changes a second.
    double encodingProgress() const { return m_progress; }
    QRect area() const { return m_area; }
    QString monitorName() const { return m_monitorName; }
    QSize monitorSize() const { return m_monitorRect.size(); }
    // Top-left of the recorded monitor in Hyprland's layout (logical px).
    QPoint monitorOrigin() const { return m_monitorRect.topLeft(); }
    // The kept file being written (or just finished).
    QString outputPath() const { return m_output; }
    QDateTime startedAt() const { return m_startedAt; }
    // Recorded time so far, in ms; not counting pauses. Holds its last value once stopped.
    Q_INVOKABLE qint64 elapsedMs() const;

    // Starts an MP4 of `area` (logical px of the monitor at `monitorRect` in the
    // layout; `pixelScale` is device px per logical px). The state is `starting`
    // at once; `recording` follows when the output file exists. Fails (and
    // notifies) if one is already running, or the recorder cannot start.
    // With Gif the MP4 is recorded the same way and the GIF is made on stop.
    Reply start(const QRect &area, const QRect &monitorRect, qreal pixelScale, const QString &monitorName = {}, Format format = Mp4);
    // Ctrl+Print / the bar's stop. During `starting` it waits for the first
    // frame and then stops. Does nothing when idle. Esc never calls this.
    void stop();
    // The bar's pause / resume (SIGUSR2 toggles the recorder). Pause does nothing
    // unless `Recording`; resume does nothing unless `Paused`. Paused time does
    // not count in elapsedMs().
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void togglePause() { m_state == Paused ? resume() : pause(); }
    // Throws this Recording away (file included) and starts the same area again
    // at 0:00: the state goes back through `starting`.
    Q_INVOKABLE void restart();
    // Throws it away: no Thumbnail, no file, no question. State `deleted`.
    Q_INVOKABLE void deleteRecording();
    // The grip: puts the bar's top-left at (x, y) for this Recording only (kept
    // on the monitor). A new Recording places it by the rule again.
    Q_INVOKABLE void moveBarTo(int x, int y);

    // The recorder's command line for a region: `-w region -region WxH+X+Y`
    // (the form the installed gpu-screen-recorder 6.1.5 documents), 60 fps h264
    // mp4, and the pointer per `showPointer` (the Setting "Show the pointer in
    // Recordings", read when a Recording starts). Pixels are device pixels of the
    // layout, even in width and height.
    // gifBound: the MP4 only feeds a GIF, so it is recorded at `-q ultra`.
    // Audio (MP4 only): the mic is `default_input`, system audio `default_output`;
    // both together are mixed into one track (`default_output|default_input`).
    static QStringList recorderArguments(const QRect &devicePixels, bool showPointer, const QString &output, bool mic = false,
                                         bool systemAudio = false, bool gifBound = false);
    // The GIF pipeline: ffmpeg decodes the kept MP4 to 20 fps y4m on a pipe,
    // gifski (quality 100, `width` px wide = full size) reads it and writes `output`.
    static QStringList ffmpegGifArguments(const QString &input, int maxWidth = 0);
    static int gifWidth(int videoWidth, int maxWidth);
    static QStringList gifskiArguments(int width, const QString &output);
    // Starts both ends of that pipeline at low CPU priority (gifski takes every core, so the
    // desktop would stutter meanwhile). `nice` execs the program, so canceling still reaches it.
    // The caller has already piped ffmpeg's output into gifski and connected the signals.
    static void startGifPipeline(QProcess &ffmpeg, QProcess &gifski, const QString &input, int videoWidth, int maxWidth,
                                 const QString &output);
    // gifski prints "Frame 12 / 120 ####" over and over with carriage returns.
    // Returns the fraction 0..1 of the last complete such report in `text`, or -1.
    static double parseGifskiProgress(const QByteArray &text);
    // The Recording bar's size; QML reports it, so the placement follows its content.
    Q_INVOKABLE void stopRecording() { stop(); }
    Q_INVOKABLE void setBarSize(int width, int height);
    QRect barRect() const { return m_barRect; }
    bool hasBar() const { return !m_barRect.isNull(); }
    bool autoHide() const { return m_autoHide; }
    QRect stripRect() const;
    // The pointer is on the auto-hiding bar / its bottom-edge strip (the surface reports it).
    Q_INVOKABLE void setPointerOnBar(bool on);
    Q_INVOKABLE void setPointerOnStrip(bool on);
    // Moves the auto-hide on to the daemon's "now" (a QTimer calls it every 100 ms; tests move the clock and call it).
    void tick();

signals:
    void stateChanged(tarn::RecordingSession::State state);
    void areaChanged();
    void barRectChanged();
    void barVisibleChanged();
    void stripChanged();
    void micOnChanged();
    // A finished Recording is now a Thumbnail with this id.
    void recorded(int thumbnailId);
    // The GIF's progress moved (0 when encoding starts, 1 when it is written).
    void encodingProgressChanged(double progress);

private:
    void setState(State state);
    void pollOutput();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);
    void sendStop();
    Reply begin(const QRect &area, const QRect &monitorRect, qreal pixelScale, const QString &monitorName, Format format);
    void signalRecorder(int signal);
    void teardownRecorder();   // SIGINT, wait for the file to close, drop it
    qint64 runningMs() const;
    void fail(const QString &why);
    void finish();
    void fetchPoster(int attempt);
    void finishWithPoster(const QByteArray &poster);
    void startEncoding();
    void onGifskiOutput();
    void onEncodingFinished();
    void setProgress(double progress, bool force);
    void addThumbnail(const QString &videoPath, const QString &sourcePath);
    void updateBar();
    void reset();
    void showAutoBar();           // an auto-hiding bar appears (or reappears) now
    void setAutoShown(bool shown);
    void pointerMoved();
    void syncStrip();
    bool running() const;

    Daemon &m_daemon;
    State m_state = Idle;
    Format m_format = Mp4;
    QProcess *m_process = nullptr;
    QTimer m_poll;
    QTimer m_startTimeout;
    QTimer m_stopTimeout;
    QByteArray m_log;            // the tail of the recorder's output, for error messages
    QElapsedTimer m_clock;
    qint64 m_frozenMs = 0;
    qint64 m_accumMs = 0;        // recorded time before the current stretch (pauses excluded)
    qreal m_pixelScale = 1.0;
    bool m_micOn = false;
    bool m_barMoved = false;
    QPoint m_barPos;
    QRect m_area;
    QRect m_monitorRect;
    QString m_monitorName;
    QString m_output;
    QDateTime m_startedAt;
    CaptureName m_captureName; // what the File name pattern needs; the Thumbnail and a GIF Pin keep it
    bool m_stopWanted = false;   // stop asked for while still starting
    bool m_stopping = false;     // SIGINT sent; waiting for the file to be finished
    // GIF encoding
    QProcess *m_ffmpeg = nullptr;
    QProcess *m_gifski = nullptr;
    QByteArray m_gifskiTail;      // the last bytes of gifski's output, for progress and errors
    QString m_gifPath;            // the finished GIF; written as <gif>.part first
    QElapsedTimer m_progressClock;
    double m_progress = 0;
    int m_pixelWidth = 0;
    QByteArray m_poster;          // the Thumbnail's picture, taken before encoding
    qint64 m_durationMs = 0;
    QSize m_barSize{128, 44};
    QRect m_barRect;
    // Auto-hide
    bool m_autoHide = false;
    bool m_autoShown = false;     // the auto-hiding bar is on screen
    bool m_onBar = false, m_onStrip = false;
    bool m_stripWas = false;
    QDateTime m_hideAt;           // when it hides; invalid while the pointer is on it (or paused)
    QTimer m_autoTimer;
};

} // namespace tarn
