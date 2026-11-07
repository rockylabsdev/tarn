#include "trim_session.h"

#include "daemon/daemon.h"
#include "notify/notifier.h"
#include "process/process_runner.h"
#include "recording/recording_session.h"
#include "recording/recording_store.h"
#include "settings/settings.h"
#include "state/remembered_state.h"
#include "thumbnails/thumbnail_stack.h"
#include "trim.h"
#include "trim_math.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <algorithm>
#include <cstdio>

namespace tarn {

TrimSession::TrimSession(Trim &trim, int captureId, const QString &videoPath, qint64 durationMs, const QSize &videoSize,
                         const QSize &openingSize, const QString &gifPath)
    : QObject(), m_trim(trim), m_captureId(captureId), m_videoPath(videoPath), m_durationMs(durationMs), m_videoSize(videoSize),
      m_openingSize(openingSize),
      m_frameCount(trim::frameCountFor(durationMs, gifPath.isEmpty() ? trim::kMp4Fps : trim::kGifFps)), m_end(m_frameCount),
      m_gifPath(gifPath) {
    m_cache = std::make_unique<FrameCache>(trim.cacheBudget(), openingSize, this);
    connect(m_cache.get(), &FrameCache::changed, this, [this] {
        ++m_cacheTick;
        emit cacheChanged();
    });
    m_cache->start(m_videoPath, m_videoSize, m_frameCount, fps());
    m_clock.setInterval(16);
    m_clock.setTimerType(Qt::PreciseTimer);
    connect(&m_clock, &QTimer::timeout, this, [this] { advance(m_clockSince.restart()); });
}

TrimSession::~TrimSession() {
    m_cache->stop();
    killGif();
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    dropWork();
}

int TrimSession::fps() const { return isGif() ? trim::kGifFps : trim::kMp4Fps; }

qint64 TrimSession::previewFrame() const { return std::min(m_playhead, m_end - 1); }

qint64 TrimSession::keptMs() const { return qRound64(trim::msOfFrame(keptFrames(), fps())); }

QString TrimSession::timeText(qint64 frame) const { return trim::formatTime(frame, fps()); }
QString TrimSession::keptText() const { return trim::formatTime(keptFrames(), fps()); }
QString TrimSession::durationText() const { return trim::formatTime(m_frameCount, fps()); }

// ── The Handles and the Playhead ────────────────────────────────────────────────────

void TrimSession::setPlayheadClamped(qint64 frame) {
    frame = std::clamp(frame, m_start, m_end);
    if (frame == m_playhead)
        return;
    m_playhead = frame;
    emit playheadChanged();
}

void TrimSession::setStartFrame(qint64 frame) {
    if (m_trimming || !m_open)
        return;
    frame = std::clamp<qint64>(frame, 0, m_end - 1);
    if (frame != m_start) {
        m_start = frame;
        emit rangeChanged();
    }
    // The preview follows the Handle, and the Playhead sits on it afterwards.
    if (m_playhead != m_start) {
        m_playhead = m_start;
        emit playheadChanged();
    }
}

void TrimSession::setEndFrame(qint64 frame) {
    if (m_trimming || !m_open)
        return;
    frame = std::clamp(frame, m_start + 1, m_frameCount);
    if (frame != m_end) {
        m_end = frame;
        emit rangeChanged();
    }
    if (m_playhead != m_end) {
        m_playhead = m_end;
        emit playheadChanged();
    }
}

void TrimSession::setPlayheadFrame(qint64 frame) {
    if (!m_trimming && m_open)
        setPlayheadClamped(frame);
}

void TrimSession::dragStart(qreal ms) { setStartFrame(trim::boundaryAtMs(ms, fps())); }
void TrimSession::dragEnd(qreal ms) { setEndFrame(trim::boundaryAtMs(ms, fps())); }
void TrimSession::scrub(qreal ms) { setPlayheadFrame(trim::boundaryAtMs(ms, fps())); }

void TrimSession::startHere() {
    const qint64 at = m_playhead;
    setStartFrame(at);
}

void TrimSession::endHere() {
    const qint64 at = m_playhead;
    setEndFrame(at);
}

// ── Playback and the keys ───────────────────────────────────────────────────────────

void TrimSession::play() {
    if (m_playing || m_trimming || !m_open)
        return;
    if (m_playhead < m_start || m_playhead >= m_end) // at the end (or outside): from the start Handle
        setPlayheadClamped(m_start);
    m_playing = true;
    m_playedMs = 0;
    m_playedFrames = 0;
    m_clockSince.start();
    m_clock.start();
    emit playingChanged();
}

void TrimSession::pause() {
    if (!m_playing)
        return;
    m_playing = false;
    m_clock.stop();
    emit playingChanged();
}

void TrimSession::togglePlay() {
    if (m_playing)
        pause();
    else
        play();
}

void TrimSession::advance(qint64 ms) {
    if (!m_playing || ms <= 0)
        return;
    // Whole milliseconds in, whole frames out: nothing drifts however the ticks fall.
    m_playedMs += ms;
    const qint64 frames = m_playedMs * fps() / 1000 - m_playedFrames;
    m_playedFrames += frames;
    if (frames <= 0)
        return;
    const qint64 length = keptFrames();
    const qint64 at = std::clamp(m_playhead, m_start, m_end - 1) - m_start;
    setPlayheadClamped(m_start + (at + frames) % length); // the loop
}

void TrimSession::toStart() {
    pause();
    setPlayheadFrame(m_start);
}

void TrimSession::toEnd() {
    pause();
    setPlayheadFrame(m_end);
}

void TrimSession::stepFrames(int frames) {
    pause();
    setPlayheadFrame(m_playhead + frames);
}

void TrimSession::stepSeconds(int seconds) { stepFrames(seconds * fps()); }

// ── Ways out ────────────────────────────────────────────────────────────────────────

void TrimSession::end() {
    m_open = false;
    m_cache->stop();
    pause();
    killGif();
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
        m_process.reset();
    }
    dropWork();
    m_trimming = false;
    emit finished();
}

void TrimSession::leave() {
    m_open = false;
    pause();
    m_trim.release(this);
    emit finished();
}

void TrimSession::dropWork() {
    if (!m_workPath.isEmpty())
        QFile::remove(m_workPath);
    m_workPath.clear();
}

void TrimSession::cancel() {
    if (!m_open)
        return;
    cancelTrim();
    leave();
}

void TrimSession::done() {
    if (!m_open || m_trimming)
        return;
    if (unchanged()) { // nothing cut: nothing to write
        leave();
        return;
    }
    startTrim(After::Close);
}

void TrimSession::copyAndClose() {
    if (!m_open || m_trimming)
        return;
    if (unchanged()) {
        m_trim.stack().copy(m_captureId); // success closes the Thumbnail, which ends this session
        return;
    }
    startTrim(After::Copy);
}

void TrimSession::saveAs(const QString &path) {
    if (!m_open || m_trimming || path.isEmpty())
        return;
    if (unchanged()) {
        m_trim.stack().saveRecordingAs(m_captureId, path);
        return;
    }
    startTrim(After::SaveAs, path);
}

QString TrimSession::saveAsFolder() const {
    const QString last = m_trim.daemon().service<RememberedState>().value(QStringLiteral("save_as.folder")).toString();
    if (!last.isEmpty() && QFileInfo(last).isDir())
        return last;
    return m_trim.daemon().service<RecordingStore>().recordingsDir();
}

// ── The re-encode ───────────────────────────────────────────────────────────────────

void TrimSession::startTrim(After after, const QString &path) {
    pause();
    m_after = after;
    m_afterPath = path;
    m_canceled = false;
    m_gifStage = false;
    m_progress = 0;
    m_pendingOutput.clear();

    QDir().mkpath(Trim::workDir());
    m_workPath = Trim::workDir() + '/' + QFileInfo(m_videoPath).completeBaseName() + QStringLiteral(".trim.mp4");
    QFile::remove(m_workPath);

    m_process = std::make_unique<QProcess>();
    m_process->setProgram(QStringLiteral("ffmpeg"));
    m_process->setArguments(trim::ffmpegArguments(m_videoPath, m_workPath, m_start, m_end, fps(), trim::kMp4Fps));
    connect(m_process.get(), &QProcess::readyReadStandardOutput, this, &TrimSession::onProcessOutput);
    connect(m_process.get(), &QProcess::finished, this, &TrimSession::onProcessFinished);
    connect(m_process.get(), &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            onProcessFinished(-1, QProcess::CrashExit);
    });
    m_trimming = true;
    emit trimmingChanged();
    emit progressChanged();
    m_process->start();
}

// ffmpeg's `-progress` lines: out_time_us=<microseconds written so far>.
void TrimSession::onProcessOutput() {
    if (!m_process)
        return;
    m_pendingOutput += m_process->readAllStandardOutput();
    qsizetype newline;
    while ((newline = m_pendingOutput.indexOf('\n')) >= 0) {
        const QByteArray line = m_pendingOutput.left(newline).trimmed();
        m_pendingOutput.remove(0, newline + 1);
        if (!line.startsWith("out_time_us="))
            continue;
        bool ok = false;
        const qint64 us = line.mid(12).toLongLong(&ok);
        const qint64 total = keptFrames() * 1000000LL / fps();
        if (!ok || total <= 0)
            continue;
        // A GIF's first half is the cut, its second half the GIF. 100% is the rename, below.
        setProgress(std::clamp<qreal>(qreal(us) / qreal(total), 0.0, 1.0) * (isGif() ? 0.5 : 1.0));
    }
}

void TrimSession::setProgress(qreal progress) {
    progress = std::min<qreal>(progress, 0.99);
    if (progress > m_progress) {
        m_progress = progress;
        emit progressChanged();
    }
}

void TrimSession::cancelTrim() {
    if (!m_trimming || m_committing) // too late once the files are swapped
        return;
    m_canceled = true;
    // Killing a process runs its finished handler, which drops the work files.
    if (m_gifStage && m_gifski)
        m_gifski->kill();
    if (m_process)
        m_process->kill();
}

// A trim that did not happen: nothing of Tarn's is touched, the window goes back to editing.
void TrimSession::abortTrim(const QString &why) {
    dropWork();
    if (isGif())
        QFile::remove(m_gifPath + QStringLiteral(".part"));
    m_gifStage = false;
    m_trimming = false;
    m_progress = 0;
    emit trimmingChanged();
    emit progressChanged();
    if (!m_canceled && m_open)
        m_trim.daemon().service<Notifier>().notify(QStringLiteral("Tarn could not trim the recording"), why);
    m_canceled = false;
}

void TrimSession::onProcessFinished(int exitCode, QProcess::ExitStatus status) {
    if (!m_process || m_gifStage)
        return;
    const QByteArray errors = m_process->readAllStandardError();
    m_process.release()->deleteLater();
    const bool ok = !m_canceled && status == QProcess::NormalExit && exitCode == 0 && QFileInfo(m_workPath).size() > 0;

    if (!ok) {
        const QString why = QString::fromUtf8(errors).trimmed().section('\n', -1);
        abortTrim(why.isEmpty() ? QStringLiteral("ffmpeg did not finish") : why);
        return;
    }
    if (isGif()) { // the cut is made; the GIF is made from it before anything of Tarn's is replaced
        startGifStage();
        return;
    }
    commit();
}

// The swap: the kept path (and the GIF) now name the trimmed files. The old files are
// not written to (a hard link in the clipboard's folder still has them).
void TrimSession::commit() {
    if (std::rename(QFile::encodeName(m_workPath).constData(), QFile::encodeName(m_videoPath).constData()) != 0) {
        abortTrim(QStringLiteral("the trimmed file could not replace Tarn's copy"));
        return;
    }
    m_workPath.clear();
    if (isGif()) {
        const QString part = m_gifPath + QStringLiteral(".part");
        if (std::rename(QFile::encodeName(part).constData(), QFile::encodeName(m_gifPath).constData()) != 0) {
            abortTrim(QStringLiteral("the new GIF could not replace Tarn's copy"));
            return;
        }
    }
    m_gifStage = false;

    // The new first frame is the Thumbnail's poster. ffmpeg makes it in the
    // background; the window stays busy (m_trimming) until it is done.
    m_committing = true;
    m_trim.daemon().service<ProcessRunner>().runAsync(
        QStringLiteral("ffmpeg"), {"-v", "error", "-i", m_videoPath, "-frames:v", "1", "-f", "image2pipe", "-c:v", "png", "-"}, {}, 10000, this,
        [this](const ProcessResult &r) {
            finishCommit(r.succeeded() && !QImage::fromData(r.out, "PNG").isNull() ? r.out : QByteArray());
        });
}

void TrimSession::finishCommit(const QByteArray &poster) {
    m_committing = false;
    m_trimming = false;
    auto &stack = m_trim.stack();
    const qint64 newDuration = keptMs();
    stack.setRecordingTrimmed(m_captureId, poster, newDuration);
    reloadFromFile();
    m_progress = 1;
    emit progressChanged();
    emit trimmingChanged();

    switch (m_after) {
    case After::Close:
        leave();
        break;
    case After::Copy:
        stack.copy(m_captureId); // success closes the Thumbnail, which ends this session
        break;
    case After::SaveAs:
        stack.saveRecordingAs(m_captureId, m_afterPath);
        break;
    }
}

// ── A GIF: the trimmed kept MP4 goes through `ffmpeg | gifski` (as a GIF Recording does) ──────────────

void TrimSession::startGifStage() {
    m_gifStage = true;
    m_gifTail.clear();
    setProgress(0.5);
    const QString part = m_gifPath + QStringLiteral(".part");
    QFile::remove(part);

    m_process = std::make_unique<QProcess>();
    m_gifski = std::make_unique<QProcess>();
    m_gifski->setProcessChannelMode(QProcess::MergedChannels);
    m_process->setStandardOutputProcess(m_gifski.get());
    connect(m_gifski.get(), &QProcess::readyRead, this, &TrimSession::onGifOutput);
    connect(m_gifski.get(), &QProcess::finished, this, &TrimSession::onGifFinished);
    auto failed = [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_gifski)
            QMetaObject::invokeMethod(this, [this] {
                if (m_gifski) {
                    m_gifTail = "gifski or ffmpeg could not be started. Run `tarn doctor` for a full check.";
                    m_canceled = false;
                    killGif();
                    abortTrim(QString::fromUtf8(m_gifTail));
                }
            }, Qt::QueuedConnection);
    };
    connect(m_gifski.get(), &QProcess::errorOccurred, this, failed);
    connect(m_process.get(), &QProcess::errorOccurred, this, failed);
    RecordingSession::startGifPipeline(*m_process, *m_gifski, m_workPath, m_videoSize.width(),
                                       m_trim.daemon().service<Settings>().gifMaxWidth(), part);
}

void TrimSession::onGifOutput() {
    if (!m_gifski)
        return;
    m_gifTail += m_gifski->readAll();
    if (m_gifTail.size() > 4096)
        m_gifTail = m_gifTail.right(4096);
    const double fraction = RecordingSession::parseGifskiProgress(m_gifTail);
    if (fraction >= 0)
        setProgress(0.5 + fraction * 0.49);
}

// Stops both GIF processes now and forgets them (the window is going, or they failed to start).
void TrimSession::killGif() {
    for (QProcess *process : {m_gifski.get(), m_gifStage ? m_process.get() : nullptr}) {
        if (!process)
            continue;
        process->disconnect(this);
        process->kill();
        process->waitForFinished(1000);
    }
    m_gifski.reset();
    if (m_gifStage)
        m_process.reset();
}

void TrimSession::onGifFinished() {
    if (!m_gifski)
        return;
    onGifOutput();
    std::unique_ptr<QProcess> gifski = std::move(m_gifski);
    std::unique_ptr<QProcess> ffmpeg = std::move(m_process);
    bool decoded = true;
    QString ffmpegError;
    if (ffmpeg) {
        if (ffmpeg->state() != QProcess::NotRunning && !ffmpeg->waitForFinished(2000)) {
            ffmpeg->kill();
            ffmpeg->waitForFinished(1000);
        }
        decoded = ffmpeg->exitStatus() == QProcess::NormalExit && ffmpeg->exitCode() == 0;
        ffmpegError = QString::fromUtf8(ffmpeg->readAllStandardError()).trimmed();
    }
    const bool encoded = gifski->exitStatus() == QProcess::NormalExit && gifski->exitCode() == 0;
    gifski.release()->deleteLater();
    if (ffmpeg)
        ffmpeg.release()->deleteLater();

    const QString part = m_gifPath + QStringLiteral(".part");
    if (!m_canceled && encoded && decoded && QFileInfo(part).size() > 0) {
        commit();
        return;
    }
    QString why = ffmpegError.isEmpty()
                      ? QString::fromUtf8(m_gifTail).trimmed().section(QRegularExpression(QStringLiteral("[\\r\\n]")), -1)
                      : ffmpegError;
    abortTrim(why.isEmpty() ? QStringLiteral("gifski failed") : why);
}

// The file on disk is now the kept range: the window's clip is the whole of it.
void TrimSession::reloadFromFile() {
    m_durationMs = keptMs();
    m_frameCount = trim::frameCountFor(m_durationMs, fps());
    m_start = 0;
    m_end = m_frameCount;
    m_playhead = 0;
    ++m_generation;
    m_cache->start(m_videoPath, m_videoSize, m_frameCount, fps()); // the file changed: decode it again
    emit sourceChanged();
    emit rangeChanged();
    emit playheadChanged();
}

} // namespace tarn
