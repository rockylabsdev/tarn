// The Rectangle tool (R): an outlined box. The template for every other tool:
// one Annotation subclass, one registration, nothing else to edit.
#include "document/annotation.h"
#include "document/annotation_json.h"
#include "gesture_geometry.h"

#include <QPen>

namespace tarn {

namespace {

class Rectangle : public Annotation {
public:
    Rectangle(const QRectF &rect, const ToolStyle &style) : m_rect(rect), m_style(style) {}
    QString kind() const override { return QStringLiteral("rect"); }
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<Rectangle>(*this); }
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
    // An outline: only its stroke is clickable, so a new Rectangle can start inside an old one.
    bool hit(const QPointF &p, qreal tol) const override {
        const qreal half = m_style.lineWidth / 2 + tol;
        return m_rect.adjusted(-half, -half, half, half).contains(p) && !m_rect.adjusted(half, half, -half, -half).contains(p);
    }
    void setColor(const QColor &color) override { m_style.color = color; }
    bool restyle(const QString &what, const ToolStyle &style) override {
        return what == QLatin1String("line") && std::exchange(m_style.lineWidth, style.lineWidth) != style.lineWidth;
    }
    void paint(QPainter &painter, const QImage &) const override {
        painter.save();
        painter.setPen(QPen(m_style.color, m_style.lineWidth, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(m_rect, 2, 2);
        painter.restore();
    }

private:
    QRectF m_rect;
    ToolStyle m_style;
};

const RegisterAnnotationLoader loadRectangle(QStringLiteral("rect"), [](const QJsonObject &o) -> std::unique_ptr<Annotation> {
    const auto rect = json::readRect(o.value(QStringLiteral("rect")));
    if (!rect)
        return nullptr;
    return std::make_unique<Rectangle>(*rect, json::readStyle(o.value(QStringLiteral("style")).toObject()));
});

const RegisterAnnotationKind registerRectangle({
    .name = QStringLiteral("rect"),
    .label = QStringLiteral("Rectangle"),
    .icon = QStringLiteral("square"),
    .key = QStringLiteral("R"),
    .order = 10,
    .sizeGroup = QStringLiteral("line"),
    .fromDrag = [](const DragGesture &g) -> std::unique_ptr<Annotation> {
        const QRectF rect = geometry::boxOf(g);
        if (rect.width() < 2 && rect.height() < 2) // a click, not a drag
            return nullptr;
        return std::make_unique<Rectangle>(rect, g.style);
    },
});

} // namespace

} // namespace tarn
