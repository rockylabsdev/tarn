// The Spotlight tool (S): dims everything outside a rectangle or ellipse.
// A Spotlight paints nothing itself. It is in the Dim pass and only describes
// the shape left bright (cutout); CaptureDocument merges the cutouts of all
// Spotlights and darkens the rest of the shot once, so overlapping Spotlights
// never darken each other, and the dim stops at the shot's edge (not the Frame).
#include "document/annotation.h"
#include "document/annotation_json.h"

#include <algorithm>

namespace tarn {

namespace {

constexpr qreal kCornerShare = 0.03;

class Spotlight : public Annotation {
public:
    Spotlight(const QRectF &rect, bool ellipse) : m_rect(rect), m_ellipse(ellipse) {}
    QString kind() const override { return QStringLiteral("spotlight"); }
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<Spotlight>(*this); }
    QJsonObject toJson() const override {
        return {{QStringLiteral("rect"), json::rect(m_rect)}, {QStringLiteral("ellipse"), m_ellipse}};
    }
    Pass pass() const override { return Dim; }
    bool restyle(const QString &what, const ToolStyle &style) override {
        if (what != QLatin1String("spotlight_shape"))
            return false;
        const bool next = style.choice(what) == QLatin1String("ellipse");
        return std::exchange(m_ellipse, next) != next;
    }
    void paint(QPainter &, const QImage &) const override {}
    QPainterPath cutout(const QSizeF &shot) const override {
        QPainterPath path;
        if (m_ellipse)
            path.addEllipse(m_rect);
        else {
            // Rounded like the Frame's corners: a share of the shot's short side, never more than half the shape.
            const qreal radius = std::min(kCornerShare * std::min(shot.width(), shot.height()), std::min(m_rect.width(), m_rect.height()) / 2);
            path.addRoundedRect(m_rect, radius, radius);
        }
        return path;
    }
    QRectF bounds() const override { return m_rect; }
    void translate(const QPointF &delta) override { m_rect.translate(delta); }
    QRectF frame() const override { return m_rect; }
    void setFrame(const QRectF &frame) override { m_rect = frame.normalized(); }

private:
    QRectF m_rect;
    bool m_ellipse;
};

const RegisterAnnotationLoader loadSpotlight(QStringLiteral("spotlight"), [](const QJsonObject &o) -> std::unique_ptr<Annotation> {
    const auto rect = json::readRect(o.value(QStringLiteral("rect")));
    if (!rect)
        return nullptr;
    return std::make_unique<Spotlight>(*rect, o.value(QStringLiteral("ellipse")).toBool());
});

const RegisterAnnotationKind registerSpotlight({
    .name = QStringLiteral("spotlight"),
    .label = QStringLiteral("Spotlight"),
    .icon = QStringLiteral("focus-2"),
    .key = QStringLiteral("S"),
    .order = 80,
    .choices =
        {
            {.name = QStringLiteral("spotlight_shape"),
             .tip = QStringLiteral("Shape"),
             .values = {{QStringLiteral("rectangle"), QStringLiteral("Rectangle")}, {QStringLiteral("ellipse"), QStringLiteral("Ellipse")}},
             .fallback = QStringLiteral("rectangle")},
        },
    .fromDrag = [](const DragGesture &g) -> std::unique_ptr<Annotation> {
        const QRectF rect = QRectF(g.from, g.to).normalized();
        if (rect.width() < 2 && rect.height() < 2) // a click, not a drag
            return nullptr;
        return std::make_unique<Spotlight>(rect, g.style.choice(QStringLiteral("spotlight_shape")) == QLatin1String("ellipse"));
    },
});

} // namespace

} // namespace tarn
