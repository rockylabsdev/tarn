// The Text tool (T): click the shot and type. See text_annotation.h. Drawing is
// by click, so the tool has no drag gesture: the EditorSession starts a text
// edit on a press (beginText) and the Annotation joins the document when the
// Text is finished.
#include "text_annotation.h"

#include "document/annotation_json.h"

#include <QFontMetricsF>
#include <QPainterPath>
#include <QPen>
#include <algorithm>

namespace tarn {

TextAnnotation::TextAnnotation(const QPointF &origin, const QString &text, const ToolStyle &style)
    : m_origin(origin), m_text(text), m_color(style.color), m_size(style.textSize),
      m_look(Look(std::clamp(style.textLook, 0, kLooks - 1))) {}

QJsonObject TextAnnotation::toJson() const {
    return {{QStringLiteral("origin"), json::point(m_origin)}, {QStringLiteral("text"), m_text},
            {QStringLiteral("color"), m_color.name(QColor::HexArgb)}, {QStringLiteral("size"), m_size}, {QStringLiteral("look"), int(m_look)}};
}

QFont TextAnnotation::fontFor(qreal size) {
    QFont font(QStringLiteral("Geist"));
    font.setWeight(QFont::Medium);
    font.setPixelSize(qMax(1, qRound(size)));
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
}

QColor TextAnnotation::contrastOf(const QColor &color) {
    const qreal luma = 0.299 * color.redF() + 0.587 * color.greenF() + 0.114 * color.blueF();
    return luma > 0.62 ? QColor(Qt::black) : QColor(Qt::white);
}

QRectF TextAnnotation::textRect() const {
    const QFontMetricsF fm(fontFor(m_size));
    qreal width = 0;
    const QStringList all = lines();
    for (const QString &line : all)
        width = qMax(width, fm.horizontalAdvance(line));
    return QRectF(m_origin, QSizeF(qMax<qreal>(width, 1), fm.lineSpacing() * all.size()));
}

QSizeF TextAnnotation::padding() const {
    return m_look == Label ? QSizeF(m_size * 0.4, m_size * 0.2) : QSizeF(0, 0);
}

QRectF TextAnnotation::bounds() const {
    const QSizeF pad = padding();
    return textRect().adjusted(-pad.width(), -pad.height(), pad.width(), pad.height());
}

void TextAnnotation::paint(QPainter &painter, const QImage &) const {
    if (m_text.isEmpty())
        return;
    const QFont font = fontFor(m_size);
    const QFontMetricsF fm(font);
    const QStringList all = lines();
    auto baseline = [&](int i) { return QPointF(m_origin.x(), m_origin.y() + i * fm.lineSpacing() + fm.ascent()); };

    painter.save();
    painter.setFont(font);
    QColor ink = m_color;
    if (m_look == Label) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_color);
        painter.drawRoundedRect(bounds(), m_size * 0.3, m_size * 0.3);
        ink = contrastOf(m_color);
    } else if (m_look == Outlined) {
        // The outline first (half of it lies under the letters), the fill over it.
        QPainterPath path;
        for (int i = 0; i < all.size(); ++i)
            path.addText(baseline(i), font, all[i]);
        QColor outline = contrastOf(m_color);
        outline.setAlpha(m_color.alpha());
        painter.setPen(QPen(outline, qMax<qreal>(2, m_size * 0.14), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }
    painter.setPen(ink);
    for (int i = 0; i < all.size(); ++i)
        painter.drawText(baseline(i), all[i]);
    painter.restore();
}

namespace {

const RegisterAnnotationLoader loadText(QStringLiteral("text"), [](const QJsonObject &o) -> std::unique_ptr<Annotation> {
    const auto origin = json::readPoint(o.value(QStringLiteral("origin")));
    if (!origin || !o.value(QStringLiteral("text")).isString())
        return nullptr;
    ToolStyle style;
    if (const QColor c(json::field(o, "color", "colour").toString()); c.isValid())
        style.color = c;
    style.textSize = o.value(QStringLiteral("size")).toDouble(style.textSize);
    style.textLook = o.value(QStringLiteral("look")).toInt(0);
    return std::make_unique<TextAnnotation>(*origin, o.value(QStringLiteral("text")).toString(), style);
});

const RegisterAnnotationKind registerText({
    .name = QStringLiteral("text"),
    .label = QStringLiteral("Text"),
    .icon = QStringLiteral("letter-t"),
    .key = QStringLiteral("T"),
    .order = 60,
    .sizeGroup = QStringLiteral("text"),
    .fromDrag = nullptr, // placed by a click: EditorSession::beginText
});

} // namespace

} // namespace tarn
