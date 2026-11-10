#pragma once
// A full-size picture to PNG on a worker thread. Encoding a 2560x1440 shot takes
// about 365 ms, so the daemon never does it on its own thread (CODING_STANDARDS.md,
// "The daemon never waits").
#include <QByteArray>
#include <QFuture>
#include <QImage>

namespace tarn {

// Starts at once; the future holds the PNG bytes (empty only if encoding failed).
// QFuture::then(context, ...) gets them back on the daemon's thread; result() waits.
QFuture<QByteArray> encodePngInBackground(const QImage &image);

} // namespace tarn
