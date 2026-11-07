#pragma once
// Frame arithmetic and the ffmpeg command line for Trim. Pure functions: the
// tests check them without running anything.
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace tarn::trim {

constexpr int kMp4Fps = 60; // an MP4 Recording is snapped to whole frames at 60 fps
constexpr int kGifFps = 20; // a GIF Recording's Trim snaps to 20 fps, the GIF's own rate

// The whole frames in a clip of this length (at least 1).
inline qint64 frameCountFor(qint64 durationMs, int fps) {
    return qMax<qint64>(1, qRound64(durationMs * fps / 1000.0));
}

// The whole frame boundary nearest to a time. Handles sit between frames: a
// start at boundary 120 keeps frame 120 onwards, an end at 300 keeps up to frame 299.
inline qint64 boundaryAtMs(qreal ms, int fps) {
    return qRound64(ms * fps / 1000.0);
}

inline qreal msOfFrame(qint64 frame, int fps) {
    return frame * 1000.0 / fps;
}

// "0:04.40": minutes, seconds, hundredths (rounded down).
QString formatTime(qint64 frame, int fps);

// The ffmpeg arguments that write frames [startFrame, endFrame) of `input` to
// `output` (CPU x264 veryfast, crf 20). The cut is made by the trim filter on
// timestamps half a frame early, so each end falls between two frames and a
// whole frame is never lost or gained to rounding. Progress goes to stdout as
// key=value lines. Audio, if there is any, is cut to the same span. A GIF's Trim
// counts frames at 20 fps but cuts a 60 fps file: `sourceFps` (the file's rate,
// 0 = the same as `fps`) sets the half-frame guard.
QStringList ffmpegArguments(const QString &input, const QString &output, qint64 startFrame, qint64 endFrame, int fps,
                            int sourceFps = 0);

} // namespace tarn::trim
