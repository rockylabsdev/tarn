#pragma once
// The Text Annotation: Tarn's UI font (Geist), in three looks, over one
// or more lines. Public (unlike most tools) because the EditorSession edits the
// text of one in place: typing, finishing, and editing it again later.
#include "document/annotation.h"

namespace tarn {

class TextAnnotation : public Annotation {
public:
    // Plain: colored text. Outlined: colored text with a contrasting outline.
    // Label: text, in a contrasting color, on a rounded box in the color.
    enum Look { Plain = 0, Outlined = 1, Label = 2 };
    static constexpr int kLooks = 3;

    // `origin` is the top-left of the first line's text (the label box reaches
    // past it by its padding). Size and look come from the style.
    TextAnnotation(const QPointF &origin, const QString &text, const ToolStyle &style);

    // Geist Medium, `size` px tall in shot pixels, unhinted so it scales cleanly.
    static QFont fontFor(qreal size);
    // Black or white, whichever reads on `color`.
    static QColor contrastOf(const QColor &color);

    QString kind() const override { return QStringLiteral("text"); }
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<TextAnnotation>(*this); }
    QJsonObject toJson() const override;
    void paint(QPainter &painter, const QImage &shot) const override;
    QRectF bounds() const override;
    HandleKind handleKind() const override { return HandleKind::Outline; }
    void translate(const QPointF &delta) override { m_origin += delta; }
    void setColor(const QColor &color) override { m_color = color; }

    const QString &text() const { return m_text; }
    void setText(const QString &text) { m_text = text; }
    QPointF origin() const { return m_origin; }
    qreal size() const { return m_size; }
    void setSize(qreal size) { m_size = size; }
    Look look() const { return m_look; }
    void setLook(Look look) { m_look = look; }
    QColor color() const { return m_color; }

private:
    QStringList lines() const { return m_text.split(QLatin1Char('\n')); }
    QRectF textRect() const; // the text alone
    QSizeF padding() const;  // the label's, else none

    QPointF m_origin;
    QString m_text;
    QColor m_color;
    qreal m_size;
    Look m_look;
};

} // namespace tarn
