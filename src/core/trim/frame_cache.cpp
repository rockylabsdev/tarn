#include "frame_cache.h"

#include <QtMath>
#include <algorithm>

namespace tarn {

namespace {

int even(qreal v) { return std::max(2, int(qRound(v / 2.0)) * 2); }

// Display sizes tried in turn: the largest that fits the box, then smaller steps.
constexpr qreal kLadder[] = {1.0, 0.875, 0.75, 0.625, 0.5, 0.4};

} // namespace

FrameCache::Plan FrameCache::plan(const QSize &videoSize, qint64 clipFrames, const QSize &box, qint64 budget) {
    Plan p;
    clipFrames = std::max<qint64>(1, clipFrames);
    // Fit the video into the box without enlarging it.
    const qreal fit = std::min({1.0, qreal(box.width()) / std::max(1, videoSize.width()),
                                qreal(box.height()) / std::max(1, videoSize.height())});
    for (qreal step : kLadder) {
        p.size = QSize(even(videoSize.width() * fit * step), even(videoSize.height() * fit * step));
        const qint64 frameBytes = qint64(p.size.width()) * p.size.height() * 4;
        p.stride = 1;
        p.frames = clipFrames;
        p.bytes = frameBytes * p.frames;
        if (p.bytes <= budget)
            return p;
    }
    // Too long even at the smallest size: keep every Nth frame.
    const qint64 frameBytes = qint64(p.size.width()) * p.size.height() * 4;
    const qint64 fits = std::max<qint64>(1, budget / frameBytes);
    p.stride = int((clipFrames + fits - 1) / fits);
    p.frames = (clipFrames + p.stride - 1) / p.stride;
    p.bytes = frameBytes * p.frames;
    return p;
}

QStringList FrameCache::arguments(const QString &video, const Plan &plan, int fps) {
    return {"-v", "error", "-i", video, "-an",
            "-vf", QStringLiteral("fps=%1/%2,scale=%3:%4:flags=bilinear").arg(fps).arg(plan.stride).arg(plan.size.width()).arg(plan.size.height()),
            "-frames:v", QString::number(plan.frames), "-f", "rawvideo", "-pix_fmt", "bgr0", "-"};
}

FrameCache::FrameCache(qint64 budget, const QSize &box, QObject *parent) : QObject(parent), m_budget(budget), m_box(box) {
    m_throttle.setInterval(100);
    m_throttle.setSingleShot(true);
    connect(&m_throttle, &QTimer::timeout, this, &FrameCache::flushChanged);
}

FrameCache::~FrameCache() { stop(); }

void FrameCache::stop() {
    if (m_process) {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
        m_process.reset();
    }
}

void FrameCache::start(const QString &video, const QSize &videoSize, qint64 clipFrames, int fps) {
    stop();
    m_frames.clear();
    m_pending.clear();
    m_bytes = 0;
    m_complete = false;
    m_plan = plan(videoSize, clipFrames, m_box, m_budget);
    m_frames.reserve(int(std::min<qint64>(m_plan.frames, 1 << 20)));
    m_process = std::make_unique<QProcess>();
    // Frames are read as they come, so the pipe never fills and ffmpeg never waits for us.
    connect(m_process.get(), &QProcess::readyReadStandardOutput, this, &FrameCache::onReadyRead);
    connect(m_process.get(), &QProcess::finished, this, &FrameCache::onFinished);
    m_process->setProgram(QStringLiteral("ffmpeg"));
    m_process->setArguments(arguments(video, m_plan, fps));
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->start();
    emit changed();
}

void FrameCache::onReadyRead() {
    m_pending += m_process->readAllStandardOutput();
    const qint64 frameBytes = qint64(m_plan.size.width()) * m_plan.size.height() * 4;
    const int lineBytes = m_plan.size.width() * 4;
    qsizetype at = 0;
    while (m_pending.size() - at >= frameBytes && m_frames.size() < m_plan.frames) {
        const QImage view(reinterpret_cast<const uchar *>(m_pending.constData() + at), m_plan.size.width(), m_plan.size.height(),
                          lineBytes, QImage::Format_RGB32);
        m_frames.append(view.copy());
        m_bytes += frameBytes;
        at += frameBytes;
        m_dirty = true;
    }
    m_pending.remove(0, at);
    if (m_frames.size() >= m_plan.frames) { // the memory cap: nothing past the plan is kept
        m_pending.clear();
        m_complete = true;
        if (m_process->state() != QProcess::NotRunning)
            m_process->kill();
    }
    if (m_dirty && !m_throttle.isActive()) {
        m_dirty = false;
        emit changed();
        m_throttle.start();
    }
}

void FrameCache::onFinished() {
    onReadyRead(); // whatever whole frames the last read held
    m_complete = true;
    m_dirty = false;
    emit changed();
}

void FrameCache::flushChanged() {
    if (m_dirty) {
        m_dirty = false;
        emit changed();
        m_throttle.start();
    }
}

QImage FrameCache::frameAt(qint64 frame) const {
    if (m_frames.isEmpty())
        return {};
    const qint64 index = std::clamp<qint64>(qRound64(double(frame) / m_plan.stride), 0, m_frames.size() - 1);
    return m_frames.at(int(index));
}

bool FrameCache::isExact(qint64 frame) const {
    return frame % m_plan.stride == 0 && frame / m_plan.stride < m_frames.size();
}

} // namespace tarn
