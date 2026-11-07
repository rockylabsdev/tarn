#pragma once
// The Callout: a box of text joined to a Counter by a leader line, pulled out of the
// Counter with the Counter tool. Public (like the Text) because the EditorSession places,
// moves and edits one.
//
// A Callout stores only its Counter's id, the box position and the text. Everything else
// comes from the Counter, handed over by the document through anchorTo(): the color, the
// text size (it follows the Counter's size) and where the leader starts. So it follows the
// Counter's color, renumbering and deletion without storing any of it. The leader is
// derived: from the Counter's edge to where the line from the box center towards the
// Counter crosses the box's edge, so it is redrawn whenever either end moves.
#include "document/annotation.h"

#include <QLineF>
#include <memory>

namespace tarn {

class CalloutAnnotation : public Annotation {
public:
    // A drag has to pass this far beyond the Counter's edge to open a Callout (shot pixels).
    static constexpr qreal kPullDistance = 24;

    enum Part { Nothing, Box, Leader };

    // `origin` is the top-left of the box.
    CalloutAnnotation(int counterId, const QPointF &origin, const QString &text) : m_counterId(counterId), m_origin(origin), m_text(text) {}

    QString kind() const override { return QStringLiteral("callout"); }
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<CalloutAnnotation>(*this); }
    QJsonObject toJson() const override;
    void paint(QPainter &painter, const QImage &shot) const override;
    QRectF bounds() const override;
    HandleKind handleKind() const override { return HandleKind::Outline; }
    void translate(const QPointF &delta) override { m_origin += delta; }
    bool hit(const QPointF &point, qreal tolerance) const override { return partAt(point, tolerance) != Nothing; }
    int anchorId() const override { return m_counterId; }
    void anchorTo(const Annotation *counter) override;

    // Whether a click at `point` is on the box or only on the leader.
    Part partAt(const QPointF &point, qreal tolerance) const;

    const QString &text() const { return m_text; }
    void setText(const QString &text) { m_text = text; }
    int counterId() const { return m_counterId; }
    void setCounterId(int id) { m_counterId = id; }
    // The text size in shot pixels: it follows the Counter's size.
    qreal size() const;
    // Where the typed text starts, and the box (it grows with the text).
    QPointF textOrigin() const;
    QRectF boxRect() const;
    // The leader, from the Counter's edge to the box's edge; a null line when the Counter
    // sits inside the box or touches it (nothing to draw).
    QLineF leader() const;
    qreal leaderWidth() const;
    QColor color() const { return m_color; }
    // Where the Counter is (center and radius), as last handed over.
    QPointF counterCenter() const { return m_center; }
    qreal counterRadius() const { return m_radius; }

    // A Callout still being pulled out of a Counter that is not in the document yet
    // (a drag from empty picture): the draft carries and paints that Counter too, as the
    // `ordinal`th of its run.
    void carryCounter(std::shared_ptr<const Annotation> counter, int ordinal);
    std::shared_ptr<const Annotation> carriedCounter() const { return m_carried; }

private:
    QSizeF padding() const;

    int m_counterId;
    QPointF m_origin;
    QString m_text;
    // Handed over by the Counter (anchorTo); not saved.
    QPointF m_center;
    qreal m_radius = 16;
    QColor m_color{0xFF, 0x45, 0x3A};
    bool m_linked = false;
    std::shared_ptr<const Annotation> m_carried;
    int m_carriedOrdinal = 0;
};

} // namespace tarn
