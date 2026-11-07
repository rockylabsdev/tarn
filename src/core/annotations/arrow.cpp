// The Arrow tool (A) in three styles:
//   tapered  a solid arrow whose shaft grows from a point at the tail to the line
//            width at the head, with a filled triangular head;
//   open     a line ending in two barbs (a "V");
//   double   the open style with a "V" at both ends.
// The style is part of the Annotation (it was the style when it was drawn); the
// tool remembers the last one picked (ToolStyle::arrowStyle).
// An arrow can bend: m_bend is the signed distance of the middle of the curve
// from the tail-to-head line (along the line's left normal), so moving an end keeps
// the shape. The curve is a quadratic Bezier through that middle point; the head
// follows its end tangent and the tapered shaft grows along it. A zero bend takes
// the straight drawing path unchanged.
#include "document/annotation.h"
#include "document/annotation_json.h"
#include "gesture_geometry.h"

#include <QLineF>
#include <QPainterPath>
#include <QPen>
#include <cmath>
#include <optional>

namespace tarn {

namespace {

class Arrow : public Annotation {
public:
    Arrow(const QPointF &tail, const QPointF &head, const ToolStyle &style)
        : m_tail(tail), m_head(head), m_style(style) {}
    QString kind() const override { return QStringLiteral("arrow"); }
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<Arrow>(*this); }
    QJsonObject toJson() const override {
        QJsonObject o{{QStringLiteral("tail"), json::point(m_tail)}, {QStringLiteral("head"), json::point(m_head)}, {QStringLiteral("style"), json::style(m_style)}};
        if (m_bend != 0)
            o.insert(QStringLiteral("bend"), m_bend);
        return o;
    }
    QRectF bounds() const override {
        const qreal pad = headLength() + m_style.lineWidth;
        QRectF box = QRectF(m_tail, m_head).normalized();
        if (m_bend != 0) { // the curve stays inside the hull of tail, control point and head
            const QPointF c = control();
            box = box.united(QRectF(c, QSizeF(0, 0)));
            box = QRectF(QPointF(std::min(box.left(), c.x()), std::min(box.top(), c.y())), QPointF(std::max(box.right(), c.x()), std::max(box.bottom(), c.y())));
        }
        return box.adjusted(-pad, -pad, pad, pad);
    }
    void setBend(qreal bend) { m_bend = bend; }
    std::optional<QPointF> bendHandle() const override { return pointAt(0.5); }
    void dragBend(const QPointF &p, qreal snap) override {
        if (QLineF(m_tail, m_head).length() < 1)
            return;
        const QPointF mid = (m_tail + m_head) / 2, n = normal();
        const qreal bend = (p.x() - mid.x()) * n.x() + (p.y() - mid.y()) * n.y();
        m_bend = std::abs(bend) < snap ? 0 : bend;
    }
    bool straighten() override { return std::exchange(m_bend, 0.0) != 0.0; }
    HandleKind handleKind() const override { return HandleKind::Ends; }
    void translate(const QPointF &delta) override { m_tail += delta; m_head += delta; }
    QList<QPointF> ends() const override { return {m_tail, m_head}; }
    void setEnd(int index, const QPointF &p) override { (index == 0 ? m_tail : m_head) = p; }
    bool hit(const QPointF &p, qreal tol) const override {
        const qreal reach = std::max(m_style.lineWidth, 6.0) / 2 + tol;
        if (m_bend == 0)
            return geometry::distanceToSegment(p, m_tail, m_head) <= reach;
        const QList<QPointF> pts = curvePoints(24);
        for (int i = 1; i < pts.size(); ++i)
            if (geometry::distanceToSegment(p, pts[i - 1], pts[i]) <= reach)
                return true;
        return false;
    }
    void setColor(const QColor &color) override { m_style.color = color; }
    bool restyle(const QString &what, const ToolStyle &style) override {
        if (what == QLatin1String("line"))
            return std::exchange(m_style.lineWidth, style.lineWidth) != style.lineWidth;
        if (what == QLatin1String("arrow_style"))
            return std::exchange(m_style.arrowStyle, style.arrowStyle) != style.arrowStyle;
        return false;
    }

    void paint(QPainter &painter, const QImage &) const override {
        const QLineF line(m_tail, m_head);
        if (line.length() < 1)
            return;
        painter.save();
        if (m_bend != 0) {
            paintCurved(painter);
            painter.restore();
            return;
        }
        const qreal w = m_style.lineWidth;
        const QPointF u = (m_head - m_tail) / line.length();
        const QPointF n(-u.y(), u.x());
        const qreal headLen = headLength();
        if (m_style.arrowStyle == QLatin1String("tapered")) {
            const QPointF base = m_head - u * headLen;
            const qreal wing = std::max(headLen * 0.5, w * 1.2);
            QPolygonF shape;
            shape << m_tail << base + n * (w / 2) << base + n * wing << m_head << base - n * wing << base - n * (w / 2);
            painter.setPen(QPen(m_style.color, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(m_style.color);
            painter.drawPolygon(shape);
        } else {
            painter.setPen(QPen(m_style.color, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            QPainterPath path;
            path.moveTo(m_tail);
            path.lineTo(m_head);
            addBarbs(path, m_head, -u, headLen);
            if (m_style.arrowStyle == QLatin1String("double"))
                addBarbs(path, m_tail, u, headLen);
            painter.drawPath(path);
        }
        painter.restore();
    }

private:
    QPointF normal() const {
        const QLineF line(m_tail, m_head);
        if (line.length() < 1e-9)
            return {0, 0};
        const QPointF u = (m_head - m_tail) / line.length();
        return {-u.y(), u.x()};
    }
    // Bezier control point: the curve's middle sits m_bend off the chord's middle.
    QPointF control() const { return (m_tail + m_head) / 2 + normal() * (2 * m_bend); }
    QPointF pointAt(qreal t) const {
        const QPointF c = control();
        const qreal a = (1 - t) * (1 - t), b = 2 * (1 - t) * t, d = t * t;
        return m_tail * a + c * b + m_head * d;
    }
    QList<QPointF> curvePoints(int steps) const {
        QList<QPointF> pts;
        for (int i = 0; i <= steps; ++i)
            pts << pointAt(qreal(i) / steps);
        return pts;
    }
    static QPointF unit(const QPointF &v) {
        const qreal l = std::hypot(v.x(), v.y());
        return l < 1e-9 ? QPointF(1, 0) : v / l;
    }

    void paintCurved(QPainter &painter) const {
        const qreal w = m_style.lineWidth;
        const qreal headLen = headLength();
        const QPointF c = control();
        const QPointF forward = unit(m_head - c); // the curve's direction at the head
        if (m_style.arrowStyle == QLatin1String("tapered")) {
            // The shaft runs to the head's base: the t where the curve is headLen from the head.
            qreal lo = 0, hi = 1;
            for (int i = 0; i < 30; ++i) {
                const qreal mid = (lo + hi) / 2;
                (QLineF(pointAt(mid), m_head).length() > headLen ? lo : hi) = mid;
            }
            const qreal tb = lo;
            const QPointF base = pointAt(tb);
            const QPointF hn(-forward.y(), forward.x());
            const qreal wing = std::max(headLen * 0.5, w * 1.2);
            constexpr int steps = 32;
            QPolygonF left, right;
            for (int i = 0; i <= steps; ++i) {
                const qreal t = tb * i / steps;
                const QPointF dir = unit((c - m_tail) * (2 * (1 - t)) + (m_head - c) * (2 * t));
                const QPointF n(-dir.y(), dir.x());
                const qreal half = (w / 2) * i / steps;
                left << pointAt(t) + n * half;
                right << pointAt(t) - n * half;
            }
            QPolygonF shape = left;
            shape << base + hn * wing << m_head << base - hn * wing;
            for (int i = int(right.size()) - 1; i >= 0; --i)
                shape << right[i];
            painter.setPen(QPen(m_style.color, 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(m_style.color);
            painter.drawPolygon(shape);
        } else {
            painter.setPen(QPen(m_style.color, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            QPainterPath path;
            path.moveTo(m_tail);
            path.quadTo(c, m_head);
            addBarbs(path, m_head, -forward, headLen);
            if (m_style.arrowStyle == QLatin1String("double"))
                addBarbs(path, m_tail, unit(c - m_tail), headLen);
            painter.drawPath(path);
        }
    }

    // Head size follows the line width and never swallows a short arrow.
    qreal headLength() const {
        const qreal length = QLineF(m_tail, m_head).length();
        return std::min(std::max(m_style.lineWidth * 3.5, 14.0), std::max(length * 0.6, 1.0));
    }
    // Two barbs at `tip`, swept back along `back` by 30 degrees each side.
    static void addBarbs(QPainterPath &path, const QPointF &tip, const QPointF &back, qreal length) {
        const qreal a = M_PI / 6;
        for (const qreal sign : {1.0, -1.0}) {
            const qreal c = std::cos(a * sign), s = std::sin(a * sign);
            const QPointF dir(back.x() * c - back.y() * s, back.x() * s + back.y() * c);
            path.moveTo(tip);
            path.lineTo(tip + dir * length);
        }
    }

    QPointF m_tail, m_head;
    ToolStyle m_style;
    qreal m_bend = 0;
};

const RegisterAnnotationLoader loadArrow(QStringLiteral("arrow"), [](const QJsonObject &o) -> std::unique_ptr<Annotation> {
    const auto a = json::readPoint(o.value(QStringLiteral("tail")));
    const auto b = json::readPoint(o.value(QStringLiteral("head")));
    if (!a || !b)
        return nullptr;
    auto arrow = std::make_unique<Arrow>(*a, *b, json::readStyle(o.value(QStringLiteral("style")).toObject()));
    arrow->setBend(o.value(QStringLiteral("bend")).toDouble(0));
    return arrow;
});

const RegisterAnnotationKind registerArrow({
    .name = QStringLiteral("arrow"),
    .label = QStringLiteral("Arrow"),
    .icon = QStringLiteral("arrow-up-right"),
    .key = QStringLiteral("A"),
    .order = 50,
    .sizeGroup = QStringLiteral("line"),
    .fromDrag = [](const DragGesture &g) -> std::unique_ptr<Annotation> {
        const auto [tail, head] = geometry::endsOf(g);
        if (QLineF(tail, head).length() < 4)
            return nullptr;
        return std::make_unique<Arrow>(tail, head, g.style);
    },
});

} // namespace

} // namespace tarn
