// The Highlighter tool (H): a flat-tipped marker. It paints with the Multiply
// blend, so it can only darken what is under it (light text stays readable, a
// black pixel stays black, white takes the full color) and never covers it.
// Freehand by default; with Shift it runs straight along the dominant axis,
// horizontal or vertical, from where the drag began.
#include "document/annotation.h"
#include "document/annotation_json.h"
#include "gesture_geometry.h"

#include <QLineF>
#include <QPainterPath>
#include <QPen>

namespace tarn {

namespace {

class Highlighter : public Annotation {
public:
    Highlighter(const QList<QPointF> &points, const ToolStyle &style) : m_points(points), m_style(style) {}
    QString kind() const override { return QStringLiteral("highlighter"); }
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<Highlighter>(*this); }
    QJsonObject toJson() const override {
        return {{QStringLiteral("points"), json::points(m_points)}, {QStringLiteral("style"), json::style(m_style)}};
    }
    QRectF bounds() const override {
        const QRectF r = geometry::boundsOfPoints(m_points);
        const qreal half = m_style.highlighterSize / 2;
        return r.adjusted(-half, -half, half, half);
    }
    HandleKind handleKind() const override { return HandleKind::Outline; }
    void translate(const QPointF &delta) override {
        for (QPointF &p : m_points)
            p += delta;
    }
    bool hit(const QPointF &p, qreal tol) const override {
        const qreal reach = m_style.highlighterSize / 2 + tol;
        for (int i = 1; i < m_points.size(); ++i)
            if (geometry::distanceToSegment(p, m_points[i - 1], m_points[i]) <= reach)
                return true;
        return false;
    }
    void setColor(const QColor &color) override { m_style.color = color; }
    bool restyle(const QString &what, const ToolStyle &style) override {
        return what == QLatin1String("highlighter") && std::exchange(m_style.highlighterSize, style.highlighterSize) != style.highlighterSize;
    }
    void paint(QPainter &painter, const QImage &) const override {
        painter.save();
        painter.setCompositionMode(QPainter::CompositionMode_Multiply);
        painter.setPen(QPen(m_style.color, m_style.highlighterSize, Qt::SolidLine, Qt::FlatCap, Qt::BevelJoin));
        painter.setBrush(Qt::NoBrush);
        QPainterPath path; // one path, so a stroke crossing itself darkens once
        path.moveTo(m_points.first());
        for (int i = 1; i < m_points.size(); ++i)
            path.lineTo(m_points[i]);
        painter.drawPath(path);
        painter.restore();
    }

private:
    QList<QPointF> m_points;
    ToolStyle m_style;
};

const RegisterAnnotationLoader loadHighlighter(QStringLiteral("highlighter"), [](const QJsonObject &o) -> std::unique_ptr<Annotation> {
    const auto points = json::readPoints(o.value(QStringLiteral("points")));
    if (!points || points->isEmpty())
        return nullptr;
    return std::make_unique<Highlighter>(*points, json::readStyle(o.value(QStringLiteral("style")).toObject()));
});

const RegisterAnnotationKind registerHighlighter({
    .name = QStringLiteral("highlighter"),
    .label = QStringLiteral("Highlighter"),
    .icon = QStringLiteral("highlight"),
    .key = QStringLiteral("H"),
    .order = 110,
    .sizeGroup = QStringLiteral("highlighter"),
    .fromDrag = [](const DragGesture &g) -> std::unique_ptr<Annotation> {
        QList<QPointF> points = g.path.size() > 1 ? g.path : QList<QPointF>{g.from, g.to};
        if (g.modifiers & Qt::ShiftModifier) {
            const QPointF d = g.to - g.from;
            const QPointF end = std::abs(d.x()) >= std::abs(d.y()) ? QPointF(g.to.x(), g.from.y()) : QPointF(g.from.x(), g.to.y());
            points = {g.from, end};
        }
        if (QLineF(points.first(), points.last()).length() < 2 && points.size() < 3)
            return nullptr;
        return std::make_unique<Highlighter>(points, g.style);
    },
});

} // namespace

} // namespace tarn
