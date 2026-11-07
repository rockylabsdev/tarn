// The Pixelate tool (X): hides the PICTURE beneath a box, never the Annotations.
// It is in the Picture pass, so the document paints it before every Mark and
// whatever is drawn on top of it stays visible.
//
// Styles: pixelate (a mosaic), blur, black out; strength 1 to 5.
// Randomized (see pixelate_render.h): each mosaic block takes ONE random pixel's
// color plus a small shift, never an average. The Editor canvas uses one stable
// seed (no shimmer) and clips a cached whole-picture effect to the region, so a
// drag never re-renders; every export draws a fresh seed (paintFlat), so two
// exports of the same area differ and nobody can recover the picture by
// matching against a plain mosaic.
#include "document/annotation.h"
#include "document/annotation_json.h"
#include "pixelate_render.h"

#include <QRandomGenerator>
#include <algorithm>
#include <utility>

namespace tarn {

namespace {

// The seed every Editor canvas paint uses: random per run, then stable, so the
// mosaic never shimmers and the effect for a (style, strength) is cached once for
// all Pixelates. Exports draw their own seed (paintFlat).
quint64 canvasSeed() {
    static const quint64 seed = QRandomGenerator::system()->generate64();
    return seed;
}

class Pixelate : public Annotation {
public:
    Pixelate(const QRectF &rect, const ToolStyle &style)
        : m_rect(rect), m_style(style.choice(QStringLiteral("pixelate_style"), QStringLiteral("pixelate"))),
          m_strength(std::clamp(style.choice(QStringLiteral("pixelate_strength"), QStringLiteral("3")).toInt(), 1, 5)) {}

    QString kind() const override { return QStringLiteral("pixelate"); }
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<Pixelate>(*this); }
    QJsonObject toJson() const override {
        return {{QStringLiteral("rect"), json::rect(m_rect)}, {QStringLiteral("style"), m_style}, {QStringLiteral("strength"), m_strength}};
    }
    Pass pass() const override { return Picture; }
    QRectF bounds() const override { return m_rect; }
    void translate(const QPointF &delta) override { m_rect.translate(delta); }
    QRectF frame() const override { return m_rect; }
    void setFrame(const QRectF &frame) override { m_rect = frame.normalized(); }

    bool restyle(const QString &what, const ToolStyle &style) override {
        if (what == QLatin1String("pixelate_style")) {
            const QString next = style.choice(what, m_style);
            return std::exchange(m_style, next) != next;
        }
        if (what == QLatin1String("pixelate_strength")) {
            const int next = std::clamp(style.choice(what, QString::number(m_strength)).toInt(), 1, 5);
            return std::exchange(m_strength, next) != next;
        }
        return false;
    }

    // The canvas: the whole picture's effect, rendered once and cached, clipped to the region.
    void paint(QPainter &painter, const QImage &shot) const override {
        const QRect area = m_rect.toAlignedRect().intersected(shot.rect());
        if (area.isEmpty())
            return;
        if (m_style == QLatin1String("black")) {
            painter.fillRect(area, Qt::black);
            return;
        }
        const auto whole = pixelate::wholePicture(shot, m_style, m_strength, canvasSeed());
        painter.save();
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawImage(QPointF(area.topLeft()), *whole, area);
        painter.restore();
    }

    // An export: a fresh seed, and only the region is worked out.
    void paintFlat(QPainter &painter, const QImage &shot, quint64 seed) const override {
        const QRect area = m_rect.toAlignedRect().intersected(shot.rect());
        if (area.isEmpty())
            return;
        if (m_style == QLatin1String("black")) {
            painter.fillRect(area, Qt::black);
            return;
        }
        const QImage result = pixelate::render(shot, m_style, m_strength, seed, area);
        painter.save();
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawImage(area.topLeft(), result);
        painter.restore();
    }

private:
    QRectF m_rect;
    QString m_style;
    int m_strength;
};

const RegisterAnnotationLoader loadPixelate(QStringLiteral("pixelate"), [](const QJsonObject &o) -> std::unique_ptr<Annotation> {
    const auto rect = json::readRect(o.value(QStringLiteral("rect")));
    if (!rect)
        return nullptr;
    ToolStyle style;
    style.choices.insert(QStringLiteral("pixelate_style"), o.value(QStringLiteral("style")).toString(QStringLiteral("pixelate")));
    style.choices.insert(QStringLiteral("pixelate_strength"), QString::number(o.value(QStringLiteral("strength")).toInt(3)));
    return std::make_unique<Pixelate>(*rect, style);
});

const RegisterAnnotationKind registerPixelate({
    .name = QStringLiteral("pixelate"),
    .label = QStringLiteral("Pixelate"),
    .icon = QStringLiteral("grid-4x4"),
    .key = QStringLiteral("X"),
    .order = 70,
    .choices =
        {
            {.name = QStringLiteral("pixelate_style"),
             .tip = QStringLiteral("Style"),
             .values = {{QStringLiteral("pixelate"), QStringLiteral("Pixelate")},
                        {QStringLiteral("blur"), QStringLiteral("Blur")},
                        {QStringLiteral("black"), QStringLiteral("Black out")}},
             .fallback = QStringLiteral("pixelate")},
            {.name = QStringLiteral("pixelate_strength"),
             .tip = QStringLiteral("Strength"),
             .values = {{QStringLiteral("1"), QStringLiteral("Strength 1")},
                        {QStringLiteral("2"), QStringLiteral("Strength 2")},
                        {QStringLiteral("3"), QStringLiteral("Strength 3")},
                        {QStringLiteral("4"), QStringLiteral("Strength 4")},
                        {QStringLiteral("5"), QStringLiteral("Strength 5")}},
             .fallback = QStringLiteral("3")},
        },
    .fromDrag = [](const DragGesture &g) -> std::unique_ptr<Annotation> {
        const QRectF rect = QRectF(g.from, g.to).normalized();
        if (rect.width() < 2 && rect.height() < 2) // a click, not a drag
            return nullptr;
        return std::make_unique<Pixelate>(rect, g.style);
    },
});

} // namespace

} // namespace tarn
