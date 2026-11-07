// The Counter tool (N): a click places the next number in a filled circle.
// Numbers are never stored: the document counts the Counters of one run
// (same style and start) in placement order, so deleting one renumbers the
// rest and undo brings the old numbers back.
#include "counter.h"

#include "document/annotation.h"
#include "document/annotation_json.h"

#include <QFont>
#include <QPainterPath>
#include <QTransform>

namespace tarn {

QString counterLabel(int style, int value) {
    if (value < 1 || style == 0)
        return QString::number(value);
    if (style == 1) {
        QString out;
        for (int n = value; n > 0; n = (n - 1) / 26)
            out.prepend(QChar('A' + (n - 1) % 26));
        return out;
    }
    static const struct {
        int value;
        const char *text;
    } kRoman[] = {{1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"}, {90, "xc"}, {50, "l"},
                  {40, "xl"},  {10, "x"},   {9, "ix"},  {5, "v"},    {4, "iv"},  {1, "i"}};
    QString out;
    int rest = value;
    for (const auto &r : kRoman)
        while (rest >= r.value) {
            out += QLatin1String(r.text);
            rest -= r.value;
        }
    return out;
}

namespace {

class Counter : public Annotation {
public:
    Counter(const QPointF &center, const ToolStyle &style) : m_center(center), m_style(style) {}
    QString kind() const override { return QStringLiteral("counter"); }
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<Counter>(*this); }
    QJsonObject toJson() const override {
        return {{QStringLiteral("center"), json::point(m_center)}, {QStringLiteral("style"), json::style(m_style)}};
    }
    HandleKind handleKind() const override { return HandleKind::Outline; }
    QString series() const override { return QStringLiteral("%1:%2").arg(m_style.counterStyle).arg(m_style.counterStart); }
    qreal diameter() const { return m_style.counterSize * 1.6; }
    QRectF bounds() const override {
        const qreal r = diameter() / 2;
        return QRectF(m_center.x() - r, m_center.y() - r, 2 * r, 2 * r);
    }
    void translate(const QPointF &delta) override { m_center += delta; }
    void setColor(const QColor &color) override { m_style.color = color; }
    QColor anchorColor() const override { return m_style.color; }
    bool restyle(const QString &what, const ToolStyle &style) override {
        if (what == QLatin1String("counter"))
            return std::exchange(m_style.counterSize, style.counterSize) != style.counterSize;
        if (what == QLatin1String("counter_style"))
            return std::exchange(m_style.counterStyle, style.counterStyle) != style.counterStyle;
        if (what == QLatin1String("counter_start"))
            return std::exchange(m_style.counterStart, style.counterStart) != style.counterStart;
        return false;
    }
    void paint(QPainter &painter, const QImage &shot) const override { paintNumbered(painter, shot, 0); }
    void paintNumbered(QPainter &painter, const QImage &, int ordinal) const override {
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_style.color);
        painter.drawEllipse(bounds());
        // Dark text on a light color, white on a dark one.
        const QColor c = m_style.color;
        const qreal luma = 0.299 * c.redF() + 0.587 * c.greenF() + 0.114 * c.blueF();
        const QColor ink = luma > 0.62 ? QColor(0x1C, 0x1C, 0x1E) : QColor(Qt::white);
        const QString text = counterLabel(m_style.counterStyle, m_style.counterStart + ordinal);
        QFont font;
        font.setBold(true);
        font.setPixelSize(qMax(1, qRound(m_style.counterSize * 0.9)));
        // The number is drawn as an outline and moved so the middle of its INK box
        // (not the font's line box, which sits high or low per glyph) is the circle's
        // center: "1", "A" and "iii" all look centered at every size.
        QPainterPath glyphs;
        glyphs.addText(QPointF(0, 0), font, text);
        const qreal limit = diameter() * 0.78; // long numbers shrink to fit
        const QRectF raw = glyphs.boundingRect();
        if (raw.width() > limit) {
            QTransform shrink;
            shrink.scale(limit / raw.width(), limit / raw.width());
            glyphs = shrink.map(glyphs);
        }
        glyphs.translate(m_center - glyphs.boundingRect().center());
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink);
        painter.drawPath(glyphs);
        painter.restore();
    }

private:
    QPointF m_center;
    ToolStyle m_style;
};

const RegisterAnnotationLoader loadCounter(QStringLiteral("counter"), [](const QJsonObject &o) -> std::unique_ptr<Annotation> {
    const auto center = json::readPoint(json::field(o, "center", "centre"));
    if (!center)
        return nullptr;
    return std::make_unique<Counter>(*center, json::readStyle(o.value(QStringLiteral("style")).toObject()));
});

const RegisterAnnotationKind registerCounter({
    .name = QStringLiteral("counter"),
    .label = QStringLiteral("Counter"),
    .icon = QStringLiteral("circle-number-1"),
    .key = QStringLiteral("N"),
    .order = 100,
    .sizeGroup = QStringLiteral("counter"),
    .fromDrag = [](const DragGesture &g) -> std::unique_ptr<Annotation> { return std::make_unique<Counter>(g.to, g.style); },
});

} // namespace

} // namespace tarn
