#pragma once
// Cutting a window out of the frozen image.
#include "window_info.h"

#include <QImage>

namespace tarn {

// The window's pixels from `frozen` (grim's full-resolution image of the
// monitor; `factor` = device pixels per logical pixel), as ARGB with the
// corners outside `window.rounding` transparent. Whatever is drawn over the
// window in the frozen image (an open menu) stays in. Empty if it is off the image.
QImage cutWindow(const QImage &frozen, const WindowInfo &window, qreal factor);

// Window file names carry the app: lower case, safe for a file name.
QString fileSafeApp(const QString &app);

} // namespace tarn
