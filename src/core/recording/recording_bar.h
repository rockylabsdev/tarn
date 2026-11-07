#pragma once
// Where the Recording bar sits: outside the recorded area, so it is never in
// the video. Below it, else above, right, left. When none of them fits (full
// screen, or an area nearly filling the monitor) it goes inside, at the
// monitor's bottom center, and auto-hides.
#include <QRect>
#include <QSize>

namespace tarn {

// `area` is the recorded Selection and `screen` the monitor, both in logical
// px with (0, 0) at the monitor's top-left. The area's outline is drawn
// `outline` px outside the area; the bar stays `gap` px beyond that. Along the
// edge the bar is centered on the area, moved just enough to stay on screen.
// Returns a null QRect when it fits nowhere.
inline QRect placeRecordingBar(const QRect &area, const QSize &screen, const QSize &bar, int gap = 12, int outline = 2) {
    const QRect monitor(QPoint(0, 0), screen);
    const int cx = qBound(0, area.x() + (area.width() - bar.width()) / 2, qMax(0, screen.width() - bar.width()));
    const int cy = qBound(0, area.y() + (area.height() - bar.height()) / 2, qMax(0, screen.height() - bar.height()));
    const int away = outline + gap;
    const QRect candidates[] = {
        QRect(QPoint(cx, area.y() + area.height() + away), bar), // below
        QRect(QPoint(cx, area.y() - away - bar.height()), bar),  // above
        QRect(QPoint(area.x() + area.width() + away, cy), bar),  // right
        QRect(QPoint(area.x() - away - bar.width(), cy), bar),   // left
    };
    for (const QRect &r : candidates)
        if (monitor.contains(r))
            return r;
    return {};
}

// Inside the area, for when nothing fits outside: the monitor's bottom center,
// `gap` px above the edge.
inline QRect placeRecordingBarInside(const QSize &screen, const QSize &bar, int gap = 12) {
    return QRect(QPoint(qMax(0, (screen.width() - bar.width()) / 2), qMax(0, screen.height() - gap - bar.height())), bar);
}

struct BarPlacement {
    QRect rect;
    bool autoHide = false; // inside the recorded area: shown briefly, then hidden
};

// v1's rule, else inside and auto-hiding. `area` null means no Recording: no bar.
inline BarPlacement placeRecordingBarOrInside(const QRect &area, const QSize &screen, const QSize &bar) {
    if (area.isNull())
        return {};
    const QRect outside = placeRecordingBar(area, screen, bar);
    if (!outside.isNull())
        return {outside, false};
    return {placeRecordingBarInside(screen, bar), true};
}

// Auto-hide timing, on the daemon's clock.
constexpr int kBarShownMs = 3000;       // shown this long after the recorder starts writing
constexpr int kBarLingerMs = 1000;      // hidden this long after the pointer left the bar and the strip
constexpr int kBarStripHeight = 2;      // the invisible strip along the monitor's bottom edge

} // namespace tarn
