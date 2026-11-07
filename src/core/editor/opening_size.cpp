#include "opening_size.h"

#include "daemon/daemon.h"
#include "hyprland/hyprland_bridge.h"

#include <QGuiApplication>
#include <QScreen>
#include <QtMath>
#include <algorithm>

namespace tarn {

MonitorSize monitorSizeUnderPointer(Daemon &daemon) {
    QString error;
    if (const auto monitor = daemon.service<HyprlandBridge>().monitorUnderPointer(&error))
        return {monitor->logicalRect.size(), monitor->scale};
    MonitorSize fallback;
    if (const QScreen *screen = QGuiApplication::primaryScreen())
        fallback.size = screen->size();
    return fallback;
}

QSize editorOpeningSize(const QSize &shotPixels, const QSize &monitor, double scale) {
    if (scale <= 0)
        scale = 1;
    const int wantW = qCeil(shotPixels.width() / scale) + 2 * EditorMetrics::canvasMargin;
    const int wantH = qCeil(shotPixels.height() / scale) + 2 * EditorMetrics::canvasMargin + 2 * EditorMetrics::barHeight;

    const int minW = std::max(EditorMetrics::minWidth, qRound(monitor.width() * 0.5));
    const int maxW = std::max(minW, qRound(monitor.width() * 2.0 / 3.0));
    const int minH = qRound(monitor.height() * 0.56);
    const int maxH = std::max(minH, qRound(monitor.height() * 0.70));
    return {std::clamp(wantW, minW, maxW), std::clamp(wantH, minH, maxH)};
}

} // namespace tarn
