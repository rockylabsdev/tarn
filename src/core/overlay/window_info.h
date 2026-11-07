#pragma once
// A window the Overlay can highlight and capture.
#include <QMetaType>
#include <QRect>
#include <QString>

namespace tarn {

struct WindowInfo {
    QRect rect;         // the window itself, without border or gap; monitor-local logical pixels
    QString app;        // Hyprland's window class, e.g. "firefox"
    QString title;
    qreal rounding = 0; // corner radius in logical pixels
};

} // namespace tarn

Q_DECLARE_METATYPE(tarn::WindowInfo)
