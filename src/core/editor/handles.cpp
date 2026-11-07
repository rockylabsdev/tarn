#include "handles.h"

#include <Qt>
#include <algorithm>
#include <cmath>

namespace tarn::Handles {

QPointF boxPoint(const QRectF &r, int h) {
    switch (h) {
    case 0: return r.topLeft();
    case 1: return r.topRight();
    case 2: return r.bottomRight();
    case 3: return r.bottomLeft();
    case 4: return {r.center().x(), r.top()};
    case 5: return {r.right(), r.center().y()};
    case 6: return {r.center().x(), r.bottom()};
    default: return {r.left(), r.center().y()};
    }
}

QRectF resizedFrame(const QRectF &r, int h, const QPointF &to, bool keepAspect) {
    if (h <= 3) {
        const QPointF opposite = boxPoint(r, (h + 2) % 4);
        QPointF corner = to;
        if (keepAspect && r.width() > 0 && r.height() > 0) {
            const qreal scale = std::max(std::abs(to.x() - opposite.x()) / r.width(), std::abs(to.y() - opposite.y()) / r.height());
            corner = {opposite.x() + (to.x() < opposite.x() ? -1 : 1) * r.width() * scale,
                      opposite.y() + (to.y() < opposite.y() ? -1 : 1) * r.height() * scale};
        }
        return QRectF(opposite, corner).normalized();
    }
    switch (h) {
    case 4: return QRectF(QPointF(r.left(), to.y()), QPointF(r.right(), r.bottom())).normalized();
    case 5: return QRectF(QPointF(r.left(), r.top()), QPointF(to.x(), r.bottom())).normalized();
    case 6: return QRectF(QPointF(r.left(), r.top()), QPointF(r.right(), to.y())).normalized();
    default: return QRectF(QPointF(to.x(), r.top()), QPointF(r.right(), r.bottom())).normalized();
    }
}

QPointF mapPoint(const QPointF &p, const QRectF &from, const QRectF &to) {
    const qreal sx = from.width() > 0 ? to.width() / from.width() : 1;
    const qreal sy = from.height() > 0 ? to.height() / from.height() : 1;
    return {to.x() + (p.x() - from.x()) * sx, to.y() + (p.y() - from.y()) * sy};
}

int boxCursor(int h) {
    switch (h) {
    case 0:
    case 2: return Qt::SizeFDiagCursor;
    case 1:
    case 3: return Qt::SizeBDiagCursor;
    case 4:
    case 6: return Qt::SizeVerCursor;
    default: return Qt::SizeHorCursor;
    }
}

} // namespace tarn::Handles
