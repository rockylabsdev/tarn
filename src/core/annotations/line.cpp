// The Line tool (L): a straight line with round ends.
#include "document/annotation.h"
#include "document/annotation_json.h"
#include "gesture_geometry.h"

#include <QLineF>
#include <QPen>

namespace tarn {

namespace {

class Line : public Annotation {
public:
    Line(const QPointF &a, const QPointF &b, const ToolStyle &style) : m_a(a), m_b(b), m_style(style) {}
    QString kind() const override { return QStringLiteral("line"); }
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<Line>(*this); }
    QJsonObject toJson() const override {
        return {{QStringLiteral("a"), json::point(m_a)}, {QStringLiteral("b"), json::point(m_b)}, {QStringLiteral("style"), json::style(m_style)}};
    }
    QRectF bounds() const override {
        const qreal half = m_style.lineWidth / 2;
        return QRectF(m_a, m_b).normalized().adjusted(-half, -half, half, half);
    }
    HandleKind handleKind() const override { return HandleKind::Ends; }
    void translate(const QPointF &delta) override { m_a += delta; m_b += delta; }
    QList<QPointF> ends() const override { return {m_a, m_b}; }
    void setEnd(int index, const QPointF &p) override { (index == 0 ? m_a : m_b) = p; }
    bool hit(const QPointF &p, qreal tol) const override {
        return geometry::distanceToSegment(p, m_a, m_b) <= m_style.lineWidth / 2 + tol;
    }
    void setColor(const QColor &color) override { m_style.color = color; }
    bool restyle(const QString &what, const ToolStyle &style) override {
        return what == QLatin1String("line") && std::exchange(m_style.lineWidth, style.lineWidth) != style.lineWidth;
    }
    void paint(QPainter &painter, const QImage &) const override {
        painter.save();
        painter.setPen(QPen(m_style.color, m_style.lineWidth, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(m_a, m_b);
        painter.restore();
    }

private:
    QPointF m_a, m_b;
    ToolStyle m_style;
};

const RegisterAnnotationLoader loadLine(QStringLiteral("line"), [](const QJsonObject &o) -> std::unique_ptr<Annotation> {
    const auto a = json::readPoint(o.value(QStringLiteral("a")));
    const auto b = json::readPoint(o.value(QStringLiteral("b")));
    if (!a || !b)
        return nullptr;
    return std::make_unique<Line>(*a, *b, json::readStyle(o.value(QStringLiteral("style")).toObject()));
});

const RegisterAnnotationKind registerLine({
    .name = QStringLiteral("line"),
    .label = QStringLiteral("Line"),
    .icon = QStringLiteral("line"),
    .key = QStringLiteral("L"),
    .order = 40,
    .sizeGroup = QStringLiteral("line"),
    .fromDrag = [](const DragGesture &g) -> std::unique_ptr<Annotation> {
        const auto [a, b] = geometry::endsOf(g);
        if (QLineF(a, b).length() < 2)
            return nullptr;
        return std::make_unique<Line>(a, b, g.style);
    },
});

} // namespace

} // namespace tarn
