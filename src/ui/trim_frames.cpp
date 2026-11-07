#include "trim_frames.h"

#include "trim/trim.h"

#include <QImage>
#include <QProcess>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>

namespace tarn {

namespace {

class FfmpegResponse : public QQuickImageResponse {
public:
    explicit FfmpegResponse(const QStringList &arguments) {
        m_process.setProgram(QStringLiteral("ffmpeg"));
        m_process.setArguments(arguments);
        connect(&m_process, &QProcess::finished, this, [this] {
            m_image = QImage::fromData(m_process.readAllStandardOutput());
            if (m_image.isNull())
                m_error = QString::fromUtf8(m_process.readAllStandardError()).trimmed();
            emit finished();
        });
        connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) {
                m_error = QStringLiteral("ffmpeg could not start");
                emit finished();
            }
        });
        m_process.start();
    }
    ~FfmpegResponse() override {
        m_process.disconnect(this);
        if (m_process.state() != QProcess::NotRunning) {
            m_process.kill();
            m_process.waitForFinished(500);
        }
    }
    QQuickTextureFactory *textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_image); }
    QString errorString() const override { return m_error; }
    void cancel() override {
        m_process.disconnect(this);
        m_process.kill(); // a frame the user has already dragged past
        m_error = QStringLiteral("canceled");
        emit finished();
    }

private:
    QProcess m_process;
    QImage m_image;
    QString m_error;
};

// A response that is already over (a bad request).
class FailedResponse : public QQuickImageResponse {
public:
    explicit FailedResponse(const QString &why) : m_why(why) {
        QMetaObject::invokeMethod(this, &QQuickImageResponse::finished, Qt::QueuedConnection);
    }
    QQuickTextureFactory *textureFactory() const override { return nullptr; }
    QString errorString() const override { return m_why; }

private:
    QString m_why;
};

} // namespace

QImage TrimCacheFrames::requestImage(const QString &id, QSize *size, const QSize &) {
    const QStringList parts = id.split('/');
    QImage image;
    if (parts.size() >= 3 && parts[2].startsWith(QLatin1Char('c'))) {
        if (TrimSession *session = m_trim.session(parts[0].toInt()))
            image = session->cachedFrame(parts[2].mid(1).toLongLong());
    }
    if (image.isNull()) { // nothing decoded yet: a black frame, never an empty one
        image = QImage(2, 2, QImage::Format_RGB32);
        image.fill(Qt::black);
    }
    if (size)
        *size = image.size();
    return image;
}

QStringList TrimFrames::frameArguments(const QString &video, qint64 frame, int fps) {
    // -ss before -i: ffmpeg seeks to the key frame and decodes up to the exact time. The middle of
    // the frame's span, so it never lands on the frame before.
    return {"-v", "error", "-ss", QString::number((frame + 0.5) / fps, 'f', 4), "-i", video,
            "-frames:v", "1", "-f", "image2pipe", "-c:v", "mjpeg", "-q:v", "2", "-pix_fmt", "yuvj420p", "-"};
}

QStringList TrimFrames::stripArguments(const QString &video, qint64 durationMs, int tiles) {
    const double seconds = durationMs / 1000.0;
    QStringList args = {"-v", "error"};
    // A long clip is sampled far more sparsely than its key frames: decoding only those is much faster.
    if (seconds / tiles > 4.0)
        args << "-skip_frame" << "nokey";
    args << "-i" << video
         << "-vf" << QStringLiteral("fps=%1/%2,scale=-2:104,tile=%1x1").arg(tiles).arg(seconds, 0, 'f', 3)
         << "-frames:v" << "1" << "-f" << "image2pipe" << "-c:v" << "png" << "-";
    return args;
}

QQuickImageResponse *TrimFrames::requestImageResponse(const QString &id, const QSize &) {
    const QStringList parts = id.split('/');
    if (parts.size() != 3)
        return new FailedResponse(QStringLiteral("bad request"));
    TrimSession *session = m_trim.session(parts[0].toInt());
    if (!session)
        return new FailedResponse(QStringLiteral("no Trim window"));
    if (parts[2].startsWith(QLatin1String("strip")))
        return new FfmpegResponse(stripArguments(session->videoPath(), session->durationMs(), qMax(1, parts[2].mid(5).toInt())));
    if (parts[2].startsWith(QLatin1Char('f')))
        return new FfmpegResponse(frameArguments(session->videoPath(), parts[2].mid(1).toLongLong(), session->fps()));
    return new FailedResponse(QStringLiteral("bad request"));
}

} // namespace tarn
