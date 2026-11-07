// The Filled rectangle tool (F): a solid box in the shared color.
#include "document/annotation.h"
#include "document/annotation_json.h"
#include "gesture_geometry.h"

namespace tarn {

namespace {

class FilledRectangle : public Annotation {
public:
    FilledRectangle(const QRectF &rect, const ToolStyle &style) : m_rect(rect), m_style(style) {}
    QString kind() const override { return QStringLiteral("filled-rect"); }
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<FilledRectangle>(*this); }
    QJsonObject toJson() const override {
        return {{QStringLiteral("rect"), json::rect(m_rect)}, {QStringLiteral("style"), json::style(m_style)}};
    }
    QRectF bounds() const override { return m_rect; }
    void translate(const QPointF &delta) override { m_rect.translate(delta); }
    QRectF frame() const override { return m_rect; }
    void setFrame(const QRectF &frame) override { m_rect = frame.normalized(); }
    void setColor(const QColor &color) override { m_style.color = color; }
    void paint(QPainter &painter, const QImage &) const override {
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_style.color);
        painter.drawRoundedRect(m_rect, 2, 2);
        painter.restore();
    }

private:
    QRectF m_rect;
    ToolStyle m_style;
};

const RegisterAnnotationLoader loadFilledRectangle(QStringLiteral("filled-rect"), [](const QJsonObject &o) -> std::unique_ptr<Annotation> {
    const auto rect = json::readRect(o.value(QStringLiteral("rect")));
    if (!rect)
        return nullptr;
    return std::make_unique<FilledRectangle>(*rect, json::readStyle(o.value(QStringLiteral("style")).toObject()));
});

const RegisterAnnotationKind registerFilledRectangle({
    .name = QStringLiteral("filled-rect"),
    .label = QStringLiteral("Filled rectangle"),
    .icon = QStringLiteral("square-filled"),
    .key = QStringLiteral("F"),
    .order = 20,
    .fromDrag = [](const DragGesture &g) -> std::unique_ptr<Annotation> {
        const QRectF rect = geometry::boxOf(g);
        if (rect.width() < 2 && rect.height() < 2)
            return nullptr;
        return std::make_unique<FilledRectangle>(rect, g.style);
    },
});

} // namespace

} // namespace tarn
