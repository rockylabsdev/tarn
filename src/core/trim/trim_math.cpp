#include "trim_math.h"

namespace tarn::trim {

QString formatTime(qint64 frame, int fps) {
    const qint64 hundredths = frame * 100 / fps;
    const qint64 seconds = hundredths / 100;
    return QStringLiteral("%1:%2.%3")
        .arg(seconds / 60)
        .arg(seconds % 60, 2, 10, QLatin1Char('0'))
        .arg(hundredths % 100, 2, 10, QLatin1Char('0'));
}

QStringList ffmpegArguments(const QString &input, const QString &output, qint64 startFrame, qint64 endFrame, int fps, int sourceFps) {
    // Half a source frame early, so the cut falls between two frames of the file.
    const double guard = 0.5 / (sourceFps > 0 ? sourceFps : fps);
    const QString from = QString::number(double(startFrame) / fps - guard, 'f', 6);
    const QString to = QString::number(double(endFrame) / fps - guard, 'f', 6);
    return {
        QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-progress"), QStringLiteral("pipe:1"), QStringLiteral("-nostats"),
        QStringLiteral("-stats_period"), QStringLiteral("0.1"),
        QStringLiteral("-i"), input,
        QStringLiteral("-map"), QStringLiteral("0:v:0"), QStringLiteral("-map"), QStringLiteral("0:a?"),
        QStringLiteral("-vf"), QStringLiteral("trim=start=%1:end=%2,setpts=PTS-STARTPTS").arg(from, to),
        QStringLiteral("-af"), QStringLiteral("atrim=start=%1:end=%2,asetpts=PTS-STARTPTS").arg(from, to),
        QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"), QStringLiteral("veryfast"),
        QStringLiteral("-crf"), QStringLiteral("20"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        QStringLiteral("-c:a"), QStringLiteral("aac"),
        QStringLiteral("-movflags"), QStringLiteral("+faststart"),
        QStringLiteral("-f"), QStringLiteral("mp4"),
        output,
    };
}

} // namespace tarn::trim
