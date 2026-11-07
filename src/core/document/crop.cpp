#include "crop.h"

#include <algorithm>
#include <cmath>

namespace tarn {

QRect Crop::resolved(const QRect &canvas) const {
    if (rect.isNull())
        return canvas;
    const QRect r = rect.normalized().intersected(canvas);
    return r.isEmpty() ? QRect(canvas.topLeft(), QSize(1, 1)) : r;
}

QTransform Crop::toDisplay(const QRect &canvas, bool cropped) const {
    const QRect r = cropped ? resolved(canvas) : canvas;
    const qreal w = r.width(), h = r.height();
    QTransform t;
    t.translate(-r.x(), -r.y());
    if (mirrored)
        t *= QTransform(-1, 0, 0, 1, w, 0);
    switch (((quarterTurns % 4) + 4) % 4) {
    case 1: t *= QTransform(0, 1, -1, 0, h, 0); break;  // (x,y) -> (h-y, x)
    case 2: t *= QTransform(-1, 0, 0, -1, w, h); break; // (x,y) -> (w-x, h-y)
    case 3: t *= QTransform(0, -1, 1, 0, 0, w); break;  // (x,y) -> (y, w-x)
    default: break;
    }
    return t;
}

QSize Crop::displaySize(const QRect &canvas, bool cropped) const {
    const QSize s = (cropped ? resolved(canvas) : canvas).size();
    return quarterTurns % 2 != 0 ? s.transposed() : s;
}

QSize Crop::outputSize(const QRect &canvas) const {
    const QSize d = displaySize(canvas);
    const qreal s = std::clamp(scale, kMinScale, kMaxScale);
    return {std::max(1, int(std::lround(d.width() * s))), std::max(1, int(std::lround(d.height() * s)))};
}

QTransform Crop::toOutput(const QRect &canvas) const {
    // The output size is rounded, so scale by the exact ratio that fills it.
    const QSize d = displaySize(canvas), o = outputSize(canvas);
    return toDisplay(canvas) * QTransform::fromScale(qreal(o.width()) / d.width(), qreal(o.height()) / d.height());
}

// Turning what is on screen adds a turn; the mirror stays.
Crop Crop::rotated(int turns) const {
    Crop c = *this;
    c.quarterTurns = (((quarterTurns + turns) % 4) + 4) % 4;
    return c;
}

// A screen flip after (turn q, mirror m) is a turn by -q with the mirror toggled,
// because mirroring reverses the sense of a rotation.
Crop Crop::flippedH() const {
    Crop c = *this;
    c.quarterTurns = (4 - quarterTurns) % 4;
    c.mirrored = !mirrored;
    return c;
}

// Top-bottom = left-right then a half turn.
Crop Crop::flippedV() const { return flippedH().rotated(2); }

Crop Crop::withScale(qreal s) const {
    Crop c = *this;
    c.scale = std::clamp(s, kMinScale, kMaxScale);
    return c;
}

Crop Crop::withRect(const QRect &r) const {
    Crop c = *this;
    c.rect = r;
    return c;
}

} // namespace tarn
