// The Pencil tool (P): a freehand stroke, smoothed.
// Smoothing: points closer than 2 px to the last kept one are dropped (hand
// jitter), then the stroke is drawn through the rest as a Catmull-Rom spline,
// so corners of the raw mouse path are rounded off but the line still passes
// through every kept point.
#include "document/annotation.h"
#include "document/annotation_json.h"
#include "gesture_geometry.h"

#include <QLineF>
#include <QPainterPath>
#include <QPen>

namespace tarn {

namespace {

QList<QPointF> thinned(const QList<QPointF> &raw) {
    QList<QPointF> kept;
    for (const QPointF &p : raw)
        if (kept.isEmpty() || QLineF(kept.last(), p).length() >= 2)
            kept << p;
    if (!raw.isEmpty() && kept.last() != raw.last()) { // the last point is where the hand stopped
        if (kept.size() > 1 && QLineF(kept.last(), raw.last()).length() < 2)
            kept.last() = raw.last();
        else
            kept << raw.last();
    }
    return kept;
}

class Pencil : public Annotation {
public:
    Pencil(const QList<QPointF> &points, const ToolStyle &style) : m_points(thinned(points)), m_style(style) {}
    QString kind() const override { return QStringLiteral("pencil"); }
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<Pencil>(*this); }
    QJsonObject toJson() const override {
        return {{QStringLiteral("points"), json::points(m_points)}, {QStringLiteral("style"), json::style(m_style)}};
    }
    QRectF bounds() const override {
        const qreal half = m_style.lineWidth / 2;
        return pathBounds().adjusted(-half, -half, half, half);
    }
    HandleKind handleKind() const override { return HandleKind::Outline; }
    void translate(const QPointF &delta) override {
        for (QPointF &p : m_points)
            p += delta;
    }
    bool hit(const QPointF &p, qreal tol) const override {
        const qreal reach = m_style.lineWidth / 2 + tol;
        if (m_points.size() == 1)
            return QLineF(p, m_points.first()).length() <= reach;
        for (int i = 1; i < m_points.size(); ++i)
            if (geometry::distanceToSegment(p, m_points[i - 1], m_points[i]) <= reach)
                return true;
        return false;
    }
    void setColor(const QColor &color) override { m_style.color = color; }
    bool restyle(const QString &what, const ToolStyle &style) override {
        return what == QLatin1String("line") && std::exchange(m_style.lineWidth, style.lineWidth) != style.lineWidth;
    }
    void paint(QPainter &painter, const QImage &) const override {
        if (m_points.isEmpty())
            return;
        painter.save();
        painter.setPen(QPen(m_style.color, m_style.lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(smoothPath());
        painter.restore();
    }

    // The smoothed path the stroke is drawn along.
    QPainterPath smoothPath() const {
        QPainterPath path;
        path.moveTo(m_points.first());
        if (m_points.size() == 1)
            path.lineTo(m_points.first() + QPointF(0.01, 0));
        for (int i = 0; i + 1 < m_points.size(); ++i) {
            const QPointF p0 = m_points[std::max(i - 1, 0)], p1 = m_points[i], p2 = m_points[i + 1],
                          p3 = m_points[std::min<int>(i + 2, m_points.size() - 1)];
            path.cubicTo(p1 + (p2 - p0) / 6, p2 - (p3 - p1) / 6, p2);
        }
        return path;
    }

private:
    QRectF pathBounds() const { return geometry::boundsOfPoints(m_points); }
    QList<QPointF> m_points;
    ToolStyle m_style;
};

const RegisterAnnotationLoader loadPencil(QStringLiteral("pencil"), [](const QJsonObject &o) -> std::unique_ptr<Annotation> {
    const auto points = json::readPoints(o.value(QStringLiteral("points")));
    if (!points || points->isEmpty())
        return nullptr;
    return std::make_unique<Pencil>(*points, json::readStyle(o.value(QStringLiteral("style")).toObject()));
});

const RegisterAnnotationKind registerPencil({
    .name = QStringLiteral("pencil"),
    .label = QStringLiteral("Pencil"),
    .icon = QStringLiteral("pencil"),
    .key = QStringLiteral("P"),
    .order = 100,
    .sizeGroup = QStringLiteral("line"),
    .fromDrag = [](const DragGesture &g) -> std::unique_ptr<Annotation> {
        QList<QPointF> points = g.path;
        if (points.isEmpty())
            points = {g.from, g.to};
        const QPointF first = points.first();
        bool moved = false;
        for (const QPointF &p : points)
            moved = moved || QLineF(first, p).length() >= 2;
        if (!moved) // a click, not a stroke
            return nullptr;
        return std::make_unique<Pencil>(points, g.style);
    },
});

} // namespace

} // namespace tarn
