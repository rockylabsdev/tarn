#include "recording_session.h"

#include "daemon/daemon.h"
#include "hyprland/hyprland_bridge.h"
#include "notify/notifier.h"
#include "process/process_runner.h"
#include "recording_bar.h"
#include "recording_store.h"
#include "record_options.h"
#include "settings/settings.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <QStandardPaths>
#include <csignal>
#include <signal.h>

namespace tarn {

namespace {
constexpr int kPollMs = 20;            // how often the output file is looked for
constexpr int kStartTimeoutMs = 8000;  // the recorder normally writes within ~0.45 s
constexpr int kStopTimeoutMs = 5000;   // it finalizes the file ~0.2 s after SIGINT
constexpr qint64 kLogTail = 2000;
constexpr int kGifFps = 20;
constexpr int kGifQuality = 100; // 90 dithers dark flat areas into blotches (docs/recording.md)
constexpr int kProgressIntervalMs = 100;  // at most 10 progress changes a second

// ffprobe's answer: the length of a video in ms, 0 if unknown.
qint64 durationFrom(const ProcessResult &r) {
    bool ok = false;
    const double seconds = QString::fromUtf8(r.out).trimmed().toDouble(&ok);
    return r.succeeded() && ok ? qRound64(seconds * 1000) : 0;
}

QByteArray placeholderPoster(const QSize &size) {
    QImage image(size.expandedTo(QSize(2, 2)), QImage::Format_RGB32);
    image.fill(QColor(0x1a, 0x1a, 0x1c));
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return png;
}
} // namespace

RecordingSession::RecordingSession(Daemon &daemon) : m_daemon(daemon) {
    m_poll.setInterval(kPollMs);
    connect(&m_poll, &QTimer::timeout, this, &RecordingSession::pollOutput);
    m_autoTimer.setInterval(100);
    connect(&m_autoTimer, &QTimer::timeout, this, &RecordingSession::tick);
    m_startTimeout.setSingleShot(true);
    m_startTimeout.setInterval(kStartTimeoutMs);
    connect(&m_startTimeout, &QTimer::timeout, this, [this] { fail(QStringLiteral("the recorder did not start writing a file")); });
    m_stopTimeout.setSingleShot(true);
    m_stopTimeout.setInterval(kStopTimeoutMs);
    connect(&m_stopTimeout, &QTimer::timeout, this, [this] {
        if (m_process)
            m_process->kill(); // finished() then runs the normal wrap-up with whatever was written
    });
}

RecordingSession::~RecordingSession() {
    for (QProcess *p : {m_ffmpeg, m_gifski})
        if (p && p->state() != QProcess::NotRunning) {
            p->disconnect(this);
            p->kill();
            p->waitForFinished(1000);
        }
    // A clean quit still finishes the file; the kept file is then saved at the next start.
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->disconnect(this);
        ::kill(pid_t(m_process->processId()), SIGINT);
        if (!m_process->waitForFinished(3000))
            m_process->kill();
    }
}

QStringList RecordingSession::recorderArguments(const QRect &px, bool showPointer, const QString &output, bool mic, bool systemAudio, bool gifBound) {
    QStringList audio;
    if (mic || systemAudio)
        audio = {QStringLiteral("-a"), mic && systemAudio ? QStringLiteral("default_output|default_input")
                                       : mic              ? QStringLiteral("default_input")
                                                          : QStringLiteral("default_output")};
    QStringList args{
        QStringLiteral("-w"), QStringLiteral("region"),
        QStringLiteral("-region"), QStringLiteral("%1x%2+%3+%4").arg(px.width()).arg(px.height()).arg(px.x()).arg(px.y()),
        QStringLiteral("-f"), QStringLiteral("60"),
        QStringLiteral("-k"), QStringLiteral("h264"),
        QStringLiteral("-c"), QStringLiteral("mp4"),
        QStringLiteral("-cursor"), showPointer ? QStringLiteral("yes") : QStringLiteral("no"),
    };
    // A GIF is made from this file, and gifski turns every encoder artifact into a patch of dither:
    // the highest recorder quality preset (QP mode) keeps the footage clean.
    if (gifBound)
        args << QStringLiteral("-q") << QStringLiteral("ultra");
    args += audio;
    // Always asked for, with no probe of the machine: it does nothing where a hardware encoder exists,
    // and records on the CPU where there is none. h264 (-k above) is the only codec it supports.
    args << QStringLiteral("-fallback-cpu-encoding") << QStringLiteral("yes");
    args << QStringLiteral("-o") << output;
    return args;
}

// Time recorded so far: the stretches before the last pause plus the running one.
qint64 RecordingSession::runningMs() const {
    return m_accumMs + (m_state == Recording && m_clock.isValid() ? m_clock.elapsed() : 0);
}

qint64 RecordingSession::elapsedMs() const {
    if ((m_state == Recording || m_state == Paused) && !m_stopping)
        return runningMs();
    return m_frozenMs;
}

void RecordingSession::setMicOn(bool on) {
    if (m_micOn == on)
        return;
    m_micOn = on;
    emit micOnChanged();
}

void RecordingSession::setState(State state) {
    if (m_state == state)
        return;
    const bool barWas = barVisible();
    m_state = state;
    if (state != Recording && state != Paused)
        m_autoTimer.stop();
    emit stateChanged(state);
    if (barWas != barVisible())
        emit barVisibleChanged();
    syncStrip();
}

// The strip exists only for an auto-hiding bar, and only while its Recording runs.
QRect RecordingSession::stripRect() const {
    if (!m_autoHide || (m_state != Recording && m_state != Paused) || m_stopping)
        return {};
    return QRect(0, m_monitorRect.height() - kBarStripHeight, m_monitorRect.width(), kBarStripHeight);
}

void RecordingSession::syncStrip() {
    const bool now = !stripRect().isNull();
    if (now == m_stripWas)
        return;
    m_stripWas = now;
    if (!now)
        m_onStrip = false;
    emit stripChanged();
}

void RecordingSession::setAutoShown(bool shown) {
    if (m_autoShown == shown)
        return;
    const bool barWas = barVisible();
    m_autoShown = shown;
    if (barWas != barVisible())
        emit barVisibleChanged();
}

// An auto-hiding bar is shown for 3 s, then hides, unless the pointer is on it.
void RecordingSession::showAutoBar() {
    m_hideAt = (m_onBar || m_onStrip) ? QDateTime() : m_daemon.now().addMSecs(kBarShownMs);
    setAutoShown(true);
}

bool RecordingSession::running() const {
    return (m_state == Recording || m_state == Paused) && !m_stopping;
}

void RecordingSession::setPointerOnBar(bool on) {
    m_onBar = on;
    pointerMoved();
}

void RecordingSession::setPointerOnStrip(bool on) {
    m_onStrip = on;
    pointerMoved();
}

// Touching the bar or the strip shows the bar and keeps it; leaving both starts the 1 s.
void RecordingSession::pointerMoved() {
    if (!m_autoHide || !running())
        return;
    if (m_onBar || m_onStrip) {
        m_hideAt = QDateTime();
        setAutoShown(true);
    } else if (m_autoShown) {
        m_hideAt = m_daemon.now().addMSecs(kBarLingerMs);
    }
}

void RecordingSession::tick() {
    if (!m_autoHide || m_state != Recording || m_stopping || !m_autoShown || !m_hideAt.isValid()) // paused: it stays
        return;
    if (m_daemon.now() >= m_hideAt)
        setAutoShown(false);
}

void RecordingSession::setBarSize(int width, int height) {
    if (m_barSize == QSize(width, height) || width <= 0 || height <= 0)
        return;
    m_barSize = QSize(width, height);
    updateBar();
}

void RecordingSession::updateBar() {
    const BarPlacement placement = placeRecordingBarOrInside(m_area, m_monitorRect.size(), m_barSize);
    QRect placed = placement.rect;
    const bool autoHide = placement.autoHide;
    if (m_barMoved && !placed.isNull()) { // the user dragged it: it stays where they put it, on the monitor
        const QSize screen = m_monitorRect.size();
        placed = QRect(QPoint(qBound(0, m_barPos.x(), qMax(0, screen.width() - m_barSize.width())),
                              qBound(0, m_barPos.y(), qMax(0, screen.height() - m_barSize.height()))),
                       m_barSize);
    }
    if (placed == m_barRect && autoHide == m_autoHide)
        return;
    m_barRect = placed;
    m_autoHide = autoHide;
    emit barRectChanged();
    syncStrip();
}

void RecordingSession::moveBarTo(int x, int y) {
    if (!barVisible() || !hasBar())
        return;
    m_barMoved = true;
    m_barPos = QPoint(x, y);
    updateBar();
}

Reply RecordingSession::start(const QRect &area, const QRect &monitorRect, qreal pixelScale, const QString &monitorName, Format format) {
    if (active())
        return Reply::failure(QStringLiteral("a Recording is already running"));
    m_barMoved = false; // a new Recording: the bar is placed by the rule again
    return begin(area, monitorRect, pixelScale, monitorName, format);
}

Reply RecordingSession::begin(const QRect &area, const QRect &monitorRect, qreal pixelScale, const QString &monitorName, Format format) {
    auto &notifier = m_daemon.service<Notifier>();
    const QString summary = QStringLiteral("Tarn could not start the recording");
    if (format == Gif) {
        // Checked now, not after minutes of footage: GIFs need gifski, and there is no fallback.
        for (const char *tool : {"gifski", "ffmpeg"})
            if (QStandardPaths::findExecutable(QString::fromLatin1(tool)).isEmpty()) {
                const QString why = QStringLiteral("%1 is not installed. Tarn makes GIFs with gifski and ffmpeg, and has no other way to. Install it, or record a video instead.")
                                        .arg(QString::fromLatin1(tool));
                notifier.notify(QStringLiteral("Tarn cannot record a GIF"), why);
                return Reply::failure(why);
            }
    }

    QString error;
    m_startedAt = m_daemon.now();
    // The window the Recording's area overlaps most, from the snapshot taken before the Overlay opened.
    const auto window = m_daemon.service<HyprlandBridge>().windowOverlapping(area.translated(monitorRect.topLeft()));
    m_captureName = {QStringLiteral("recording"), window ? window->app : QString(), window ? window->title : QString()};
    m_output = m_daemon.service<RecordingStore>().newKeptPath(m_startedAt, QStringLiteral("mp4"), &error, m_captureName);
    if (m_output.isEmpty()) {
        notifier.notify(summary, error);
        return Reply::failure(error);
    }

    // The recorder takes device pixels of the layout; the Selection is logical.
    const qreal f = pixelScale > 0 ? pixelScale : 1.0;
    const int x = qRound((monitorRect.x() + area.x()) * f), y = qRound((monitorRect.y() + area.y()) * f);
    // H.264 wants even sizes.
    const int w = qMax(2, qRound(area.width() * f) & ~1), h = qMax(2, qRound(area.height() * f) & ~1);

    m_pixelScale = pixelScale;
    m_area = area;
    m_monitorRect = monitorRect;
    m_monitorName = monitorName;
    m_format = format;
    m_pixelWidth = w;
    m_progress = 0;
    m_frozenMs = 0;
    m_accumMs = 0;
    m_clock.invalidate();
    m_stopWanted = m_stopping = false;
    m_autoShown = m_onBar = m_onStrip = false;
    m_hideAt = QDateTime();
    m_log.clear();
    emit areaChanged();
    updateBar();
    setState(Starting); // the outline turns red now

    // Out of the footage: off screen before the recorder's first frame.
    m_hide = m_daemon.service<ThumbnailStack>().hideForCapture();
    m_daemon.service<ThumbnailStack>().waitUntilOffScreen();
    delete m_process;
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyRead, this, [this] {
        m_log += m_process->readAll();
        if (m_log.size() > kLogTail)
            m_log = m_log.right(kLogTail);
    });
    connect(m_process, &QProcess::finished, this, &RecordingSession::onFinished);
    // A GIF has no sound; under a software pointer the Setting cannot take it out.
    auto &options = m_daemon.service<RecordOptions>();
    const bool sound = format == Mp4;
    setMicOn(sound && options.micOn());
    m_process->start(QStringLiteral("gpu-screen-recorder"),
                     recorderArguments(QRect(x, y, w, h), options.pointerShown(), m_output, sound && options.micOn(), sound && options.systemAudio(), format == Gif));
    if (!m_process->waitForStarted(2000)) {
        const QString why = QStringLiteral("gpu-screen-recorder could not be started (is it installed?). Run `tarn doctor` for a full check.");
        fail(why);
        return Reply::failure(why);
    }
    m_poll.start();
    m_startTimeout.start();
    pollOutput();
    return Reply::success();
}

void RecordingSession::pollOutput() {
    if (m_state != Starting)
        return;
    if (!QFileInfo::exists(m_output))
        return;
    m_poll.stop();
    m_startTimeout.stop();
    m_clock.start();
    m_autoShown = m_autoHide; // an auto-hiding bar is shown now too, for 3 s
    m_hideAt = m_autoHide ? m_daemon.now().addMSecs(kBarShownMs) : QDateTime();
    if (m_autoHide)
        m_autoTimer.start();
    setState(Recording); // the bar appears now
    if (m_stopWanted)
        sendStop();
}

void RecordingSession::stop() {
    if (m_state == Starting)
        m_stopWanted = true; // stops as soon as the file exists: a signal sent earlier would find nothing to finish
    else if (m_state == Recording || m_state == Paused)
        sendStop();
}

void RecordingSession::sendStop() {
    if (m_stopping || !m_process)
        return;
    m_frozenMs = runningMs();
    const bool barWas = barVisible();
    m_stopping = true;
    if (barWas)
        emit barVisibleChanged(); // the bar vanishes at once
    syncStrip();
    ::kill(pid_t(m_process->processId()), SIGINT);
    m_stopTimeout.start();
}

void RecordingSession::signalRecorder(int signal) {
    if (m_process && m_process->state() != QProcess::NotRunning)
        ::kill(pid_t(m_process->processId()), signal);
}

void RecordingSession::pause() {
    if (!canPause())
        return;
    m_accumMs = runningMs();
    signalRecorder(SIGUSR2);
    setState(Paused);
}

void RecordingSession::resume() {
    if (m_state != Paused || m_stopping)
        return;
    signalRecorder(SIGUSR2);
    m_clock.restart();
    if (m_autoHide)
        showAutoBar(); // it was kept shown while paused; the 3 s start again
    setState(Recording);
}

// Ends the recorder and drops what it wrote. SIGINT first, so the recorder
// closes the file and its helper; a stuck one is killed.
void RecordingSession::teardownRecorder() {
    m_poll.stop();
    m_startTimeout.stop();
    m_stopTimeout.stop();
    if (m_process) {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning) {
            ::kill(pid_t(m_process->processId()), SIGINT);
            if (!m_process->waitForFinished(3000)) {
                m_process->kill();
                m_process->waitForFinished(1000);
            }
        }
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_daemon.service<RecordingStore>().discard(m_output);
}

void RecordingSession::restart() {
    if ((m_state != Recording && m_state != Paused) || m_stopping)
        return;
    teardownRecorder();
    // Back through `starting`, in the same format. If it cannot begin again (it has already notified), nothing is left running.
    if (!begin(m_area, m_monitorRect, m_pixelScale, m_monitorName, m_format).ok)
        if (m_state != Idle)
            reset();
}

void RecordingSession::deleteRecording() {
    if ((m_state != Recording && m_state != Paused) || m_stopping)
        return;
    teardownRecorder();
    m_hide.reset();
    m_frozenMs = m_accumMs = 0;
    setState(Deleted); // the bar goes with it; no Thumbnail, no History
}

void RecordingSession::onFinished(int, QProcess::ExitStatus) {
    m_hide.reset(); // the recorder is gone: the Thumbnails come back, before any Thumbnail or GIF is made
    m_poll.stop();
    m_startTimeout.stop();
    m_stopTimeout.stop();
    if (m_stopping) {
        finish();
        return;
    }
    const QString tail = QString::fromUtf8(m_log).trimmed().section('\n', -1);
    fail(tail.isEmpty() ? QStringLiteral("gpu-screen-recorder stopped by itself") : QStringLiteral("gpu-screen-recorder stopped: %1").arg(tail));
}

void RecordingSession::fail(const QString &why) {
    m_poll.stop();
    m_startTimeout.stop();
    m_stopTimeout.stop();
    if (m_process) {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_daemon.service<RecordingStore>().discard(m_output);
    m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not record"), why);
    reset();
}

void RecordingSession::reset() {
    m_hide.reset();
    m_stopWanted = m_stopping = false;
    m_area = QRect();
    emit areaChanged();
    updateBar();
    setState(Idle);
}

// The recorder has finalized the file: its length and a poster frame make the Thumbnail.
void RecordingSession::finish() {
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    const QFileInfo file(m_output);
    if (!file.exists() || file.size() == 0) {
        fail(QStringLiteral("the recording is empty"));
        return;
    }
    // ffprobe and the poster frame can take a moment on a long file: they run in
    // the background. The state stays as it is (m_stopping is set, so the bar is
    // gone and nothing else can start) until the Thumbnail is made.
    m_daemon.service<ProcessRunner>().runAsync(
        QStringLiteral("ffprobe"), {"-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", m_output}, {}, 5000, this,
        [this](const ProcessResult &r) {
            m_durationMs = durationFrom(r);
            if (m_durationMs <= 0)
                m_durationMs = m_frozenMs;
            fetchPoster(0);
        });
}

// One frame from the middle of the video as PNG; the first frame if that fails.
void RecordingSession::fetchPoster(int attempt) {
    const QString at = attempt == 0 ? QString::number(m_durationMs / 2000.0, 'f', 3) : QStringLiteral("0");
    m_daemon.service<ProcessRunner>().runAsync(
        QStringLiteral("ffmpeg"), {"-v", "error", "-ss", at, "-i", m_output, "-frames:v", "1", "-f", "image2pipe", "-c:v", "png", "-"}, {}, 10000, this,
        [this, attempt](const ProcessResult &r) {
            if (r.succeeded() && !QImage::fromData(r.out, "PNG").isNull()) {
                finishWithPoster(r.out);
            } else if (attempt == 0) {
                fetchPoster(1);
            } else {
                finishWithPoster(placeholderPoster(m_area.size()));
            }
        });
}

void RecordingSession::finishWithPoster(const QByteArray &poster) {
    m_poster = poster;
    if (m_format == Gif) {
        startEncoding();
        return;
    }
    addThumbnail(m_output, {});
}

void RecordingSession::addThumbnail(const QString &videoPath, const QString &sourcePath) {
    const int id = m_daemon.service<ThumbnailStack>().addRecording(m_poster, m_startedAt, videoPath, m_durationMs, sourcePath, m_captureName);
    m_poster.clear();
    if (id >= 0 && !m_area.isNull()) // where it was recorded, in layout pixels: a GIF Pin opens there
        m_daemon.service<ThumbnailStack>().setOrigin(id, m_area.translated(m_monitorRect.topLeft()));
    setState(Done);
    emit recorded(id);
}

// A wider video shrinks to maxWidth, a narrower one keeps its own width; -2 keeps the aspect ratio
// and rounds the height to even, which the y4m stream needs. maxWidth 0 = no scaling.
QStringList RecordingSession::ffmpegGifArguments(const QString &input, int maxWidth) {
    QString filter = QStringLiteral("fps=%1").arg(kGifFps);
    if (maxWidth > 0)
        filter += QStringLiteral(",scale='min(iw,%1)':-2").arg(maxWidth);
    return {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-i"), input,
            QStringLiteral("-vf"), filter, QStringLiteral("-f"), QStringLiteral("yuv4mpegpipe"), QStringLiteral("-")};
}

// The width the GIF comes out at, which is what gifski is told.
int RecordingSession::gifWidth(int videoWidth, int maxWidth) {
    return maxWidth > 0 ? qMin(videoWidth, maxWidth) : videoWidth;
}

// gifski limits an animation to ~800x600 unless told a width, so the width is the Recording's own.
QStringList RecordingSession::gifskiArguments(int width, const QString &output) {
    return {QStringLiteral("--fps"), QString::number(kGifFps), QStringLiteral("--width"), QString::number(width),
            QStringLiteral("--quality"), QString::number(kGifQuality), QStringLiteral("-o"), output, QStringLiteral("-")};
}

void RecordingSession::startGifPipeline(QProcess &ffmpeg, QProcess &gifski, const QString &input, int videoWidth, int maxWidth,
                                        const QString &output) {
    const auto niced = [](const QString &program, const QStringList &args) {
        return QStringList{QStringLiteral("-n"), QStringLiteral("10"), program} + args;
    };
    gifski.start(QStringLiteral("nice"), niced(QStringLiteral("gifski"), gifskiArguments(gifWidth(videoWidth, maxWidth), output)));
    ffmpeg.start(QStringLiteral("nice"), niced(QStringLiteral("ffmpeg"), ffmpegGifArguments(input, maxWidth)));
}

double RecordingSession::parseGifskiProgress(const QByteArray &text) {
    // The number must be followed by something, or "Frame 1 / 1" could be half of "Frame 1 / 100".
    static const QRegularExpression re(QStringLiteral("Frame\\s+(\\d+)\\s*/\\s*(\\d+)(?=\\D)"));
    double fraction = -1;
    auto it = re.globalMatch(QString::fromLatin1(text));
    while (it.hasNext()) {
        const auto m = it.next();
        const double total = m.captured(2).toDouble();
        if (total > 0)
            fraction = qBound(0.0, m.captured(1).toDouble() / total, 1.0);
    }
    return fraction;
}

// The MP4 is final and kept; the GIF is made beside it by `ffmpeg | gifski`.
void RecordingSession::startEncoding() {
    m_gifPath = QFileInfo(m_output).absolutePath() + '/' + QFileInfo(m_output).completeBaseName() + QStringLiteral(".gif");
    m_gifskiTail.clear();
    m_progress = 0;
    setState(Encoding); // the bar turns into the progress ring
    emit encodingProgressChanged(0);
    m_progressClock.start();

    delete m_ffmpeg;
    delete m_gifski;
    m_ffmpeg = new QProcess(this);
    m_gifski = new QProcess(this);
    m_gifski->setProcessChannelMode(QProcess::MergedChannels);
    m_ffmpeg->setStandardOutputProcess(m_gifski);
    connect(m_gifski, &QProcess::readyRead, this, &RecordingSession::onGifskiOutput);
    connect(m_gifski, &QProcess::finished, this, &RecordingSession::onEncodingFinished);
    startGifPipeline(*m_ffmpeg, *m_gifski, m_output, m_pixelWidth, m_daemon.service<Settings>().gifMaxWidth(),
                     m_gifPath + QStringLiteral(".part"));
    if (!m_gifski->waitForStarted(2000) || !m_ffmpeg->waitForStarted(2000)) {
        m_gifskiTail = "gifski or ffmpeg could not be started. Run `tarn doctor` for a full check.";
        m_gifski->disconnect(this);
        m_gifski->kill();
        m_ffmpeg->kill();
        m_gifski->waitForFinished(1000);
        m_ffmpeg->waitForFinished(1000);
        m_gifski->deleteLater();
        m_ffmpeg->deleteLater();
        m_gifski = m_ffmpeg = nullptr;
        QFile::remove(m_gifPath + QStringLiteral(".part"));
        m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not make the GIF"),
                                            QStringLiteral("%1. The video was kept instead.").arg(QString::fromUtf8(m_gifskiTail)));
        m_format = Mp4;
        addThumbnail(m_output, {});
    }
}

void RecordingSession::onGifskiOutput() {
    m_gifskiTail += m_gifski->readAll();
    if (m_gifskiTail.size() > kLogTail)
        m_gifskiTail = m_gifskiTail.right(kLogTail);
    const double fraction = parseGifskiProgress(m_gifskiTail);
    if (fraction >= 0)
        setProgress(qMin(fraction, 0.99), false); // 1 is for a GIF that is written
}

// At most one change per 100 ms (10 a second), only ever forward; `force` is for 1.
void RecordingSession::setProgress(double progress, bool force) {
    if (progress <= m_progress && !force)
        return;
    if (!force && m_progressClock.isValid() && m_progressClock.elapsed() < kProgressIntervalMs)
        return;
    m_progress = progress;
    m_progressClock.restart();
    emit encodingProgressChanged(progress);
}

void RecordingSession::onEncodingFinished() {
    onGifskiOutput();
    QProcess *gifski = std::exchange(m_gifski, nullptr);
    QProcess *ffmpeg = std::exchange(m_ffmpeg, nullptr);
    bool decoded = true;
    QString ffmpegError;
    if (ffmpeg) {
        if (ffmpeg->state() != QProcess::NotRunning && !ffmpeg->waitForFinished(2000))
            ffmpeg->kill(), ffmpeg->waitForFinished(1000);
        decoded = ffmpeg->exitStatus() == QProcess::NormalExit && ffmpeg->exitCode() == 0;
        ffmpegError = QString::fromUtf8(ffmpeg->readAllStandardError()).trimmed();
        ffmpeg->deleteLater();
    }
    const bool encoded = gifski->exitStatus() == QProcess::NormalExit && gifski->exitCode() == 0;
    gifski->deleteLater();

    const QString part = m_gifPath + QStringLiteral(".part");
    if (encoded && decoded && QFileInfo(part).size() > 0 && QFile::rename(part, m_gifPath)) {
        setProgress(1, true);
        addThumbnail(m_gifPath, m_output);
        return;
    }
    // The video is fine, so it is kept as a video; the notification says the GIF failed. Never a silent fallback.
    QFile::remove(part);
    QString why = ffmpegError.isEmpty() ? QString::fromUtf8(m_gifskiTail).trimmed().section(QRegularExpression(QStringLiteral("[\\r\\n]")), -1) : ffmpegError;
    if (why.isEmpty())
        why = QStringLiteral("gifski failed");
    m_daemon.service<Notifier>().notify(QStringLiteral("Tarn could not make the GIF"),
                                        QStringLiteral("%1. The video was kept instead.").arg(why));
    m_format = Mp4;
    addThumbnail(m_output, {});
}

} // namespace tarn
