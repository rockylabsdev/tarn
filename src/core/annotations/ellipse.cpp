// The Ellipse tool (O): an outlined ellipse inside the dragged box.
#include "document/annotation.h"
#include "document/annotation_json.h"
#include "gesture_geometry.h"

#include <QPen>

namespace tarn {

namespace {

class Ellipse : public Annotation {
public:
    Ellipse(const QRectF &rect, const ToolStyle &style) : m_rect(rect), m_style(style) {}
    QString kind() const override { return QStringLiteral("ellipse"); }
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<Ellipse>(*this); }
    QJsonObject toJson() const override {
        return {{QStringLiteral("rect"), json::rect(m_rect)}, {QStringLiteral("style"), json::style(m_style)}};
    }
    QRectF bounds() const override {
        const qreal half = m_style.lineWidth / 2;
        return m_rect.adjusted(-half, -half, half, half);
    }
    void translate(const QPointF &delta) override { m_rect.translate(delta); }
    QRectF frame() const override { return m_rect; }
    void setFrame(const QRectF &frame) override { m_rect = frame.normalized(); }
    // Only the ring is clickable, so a new shape can start inside an old one.
    bool hit(const QPointF &p, qreal tol) const override {
        const qreal half = m_style.lineWidth / 2 + tol;
        return inside(m_rect.adjusted(-half, -half, half, half), p) && !inside(m_rect.adjusted(half, half, -half, -half), p);
    }
    void setColor(const QColor &color) override { m_style.color = color; }
    bool restyle(const QString &what, const ToolStyle &style) override {
        return what == QLatin1String("line") && std::exchange(m_style.lineWidth, style.lineWidth) != style.lineWidth;
    }
    void paint(QPainter &painter, const QImage &) const override {
        painter.save();
        painter.setPen(QPen(m_style.color, m_style.lineWidth));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(m_rect);
        painter.restore();
    }

private:
    static bool inside(const QRectF &r, const QPointF &p) {
        if (r.width() <= 0 || r.height() <= 0)
            return false;
        const qreal nx = (p.x() - r.center().x()) / (r.width() / 2);
        const qreal ny = (p.y() - r.center().y()) / (r.height() / 2);
        return nx * nx + ny * ny <= 1;
    }
    QRectF m_rect;
    ToolStyle m_style;
};

const RegisterAnnotationLoader loadEllipse(QStringLiteral("ellipse"), [](const QJsonObject &o) -> std::unique_ptr<Annotation> {
    const auto rect = json::readRect(o.value(QStringLiteral("rect")));
    if (!rect)
        return nullptr;
    return std::make_unique<Ellipse>(*rect, json::readStyle(o.value(QStringLiteral("style")).toObject()));
});

const RegisterAnnotationKind registerEllipse({
    .name = QStringLiteral("ellipse"),
    .label = QStringLiteral("Ellipse"),
    .icon = QStringLiteral("circle"),
    .key = QStringLiteral("O"),
    .order = 30,
    .sizeGroup = QStringLiteral("line"),
    .fromDrag = [](const DragGesture &g) -> std::unique_ptr<Annotation> {
        const QRectF rect = geometry::boxOf(g);
        if (rect.width() < 2 && rect.height() < 2)
            return nullptr;
        return std::make_unique<Ellipse>(rect, g.style);
    },
});

} // namespace

} // namespace tarn
