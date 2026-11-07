#pragma once
// How Shift and Alt shape a drag, the same for every shape tool.
//   Shift: boxes become squares, lines and arrows snap to 45 degree steps.
//   Alt:   the press point is the center instead of a corner / the first end.
#include "document/annotation.h"

#include <QLineF>
#include <QPair>
#include <QRectF>
#include <algorithm>
#include <cmath>

namespace tarn::geometry {

inline QRectF boxOf(const DragGesture &g) {
    QPointF d = g.to - g.from;
    if (g.modifiers & Qt::ShiftModifier) {
        const qreal side = std::max(std::abs(d.x()), std::abs(d.y()));
        d = QPointF(d.x() < 0 ? -side : side, d.y() < 0 ? -side : side);
    }
    if (g.modifiers & Qt::AltModifier)
        return QRectF(g.from - d, g.from + d).normalized();
    return QRectF(g.from, g.from + d).normalized();
}

// The two ends of a line or arrow: tail first, head second.
inline QPair<QPointF, QPointF> endsOf(const DragGesture &g) {
    QPointF head = g.to;
    if (g.modifiers & Qt::ShiftModifier) {
        const QPointF d = g.to - g.from;
        const qreal step = M_PI / 4;
        const qreal angle = std::round(std::atan2(d.y(), d.x()) / step) * step;
        const qreal len = std::hypot(d.x(), d.y());
        head = g.from + QPointF(std::cos(angle) * len, std::sin(angle) * len);
    }
    QPointF tail = g.from;
    if (g.modifiers & Qt::AltModifier)
        tail = g.from - (head - g.from);
    return {tail, head};
}

// The box round a set of points (QRectF::united ignores empty rectangles, so
// it cannot be used for this).
inline QRectF boundsOfPoints(const QList<QPointF> &points) {
    qreal l = points.first().x(), r = l, t = points.first().y(), b = t;
    for (const QPointF &p : points) {
        l = std::min(l, p.x());
        r = std::max(r, p.x());
        t = std::min(t, p.y());
        b = std::max(b, p.y());
    }
    return QRectF(QPointF(l, t), QPointF(r, b));
}

// Distance from p to the segment a-b.
inline qreal distanceToSegment(const QPointF &p, const QPointF &a, const QPointF &b) {
    const QPointF ab = b - a;
    const qreal len2 = ab.x() * ab.x() + ab.y() * ab.y();
    qreal t = len2 > 0 ? ((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / len2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    return QLineF(p, a + ab * t).length();
}

} // namespace tarn::geometry
