// The Callout: see callout.h.
#include "callout.h"

#include "document/annotation_json.h"
#include "text_annotation.h"

#include <QFontMetricsF>
#include <QPen>
#include <algorithm>
#include <cmath>
#include <limits>

namespace tarn {

QJsonObject CalloutAnnotation::toJson() const {
    return {{QStringLiteral("counter"), m_counterId}, {QStringLiteral("origin"), json::point(m_origin)}, {QStringLiteral("text"), m_text}};
}

void CalloutAnnotation::anchorTo(const Annotation *counter) {
    m_linked = counter != nullptr;
    if (!counter)
        return;
    const QRectF b = counter->bounds();
    m_center = b.center();
    m_radius = b.width() / 2;
    if (const QColor c = counter->anchorColor(); c.isValid())
        m_color = c;
}

void CalloutAnnotation::carryCounter(std::shared_ptr<const Annotation> counter, int ordinal) {
    m_carried = std::move(counter);
    m_carriedOrdinal = ordinal;
    anchorTo(m_carried.get());
}

// The Counter's number is 0.9 of its size and its circle 1.6 of it; the text takes the size.
qreal CalloutAnnotation::size() const { return std::max<qreal>(10, m_radius * 2 / 1.6); }

QSizeF CalloutAnnotation::padding() const { return QSizeF(size() * 0.6, size() * 0.4); }

QRectF CalloutAnnotation::boxRect() const {
    const QFontMetricsF fm(TextAnnotation::fontFor(size()));
    qreal width = 0;
    const QStringList lines = m_text.split(QLatin1Char('\n'));
    for (const QString &line : lines)
        width = std::max(width, fm.horizontalAdvance(line));
    const QSizeF pad = padding();
    return QRectF(m_origin, QSizeF(width + 2 * pad.width(), fm.lineSpacing() * lines.size() + 2 * pad.height())); // hugs the text at any length
}

QPointF CalloutAnnotation::textOrigin() const { return m_origin + QPointF(padding().width(), padding().height()); }

qreal CalloutAnnotation::leaderWidth() const { return std::max<qreal>(2, size() * 0.12); }

QLineF CalloutAnnotation::leader() const {
    if (!m_linked)
        return {};
    const QRectF box = boxRect();
    const QPointF center = box.center();
    const QPointF toCounter = m_center - center;
    if (toCounter.isNull())
        return {};
    // How far along center -> Counter the box's edge is (1 = at the Counter's center).
    const qreal inf = std::numeric_limits<qreal>::infinity();
    const qreal sx = toCounter.x() != 0 ? box.width() / 2 / std::abs(toCounter.x()) : inf;
    const qreal sy = toCounter.y() != 0 ? box.height() / 2 / std::abs(toCounter.y()) : inf;
    const qreal along = std::min(sx, sy);
    if (along >= 1) // the Counter's center is inside the box
        return {};
    const QPointF end = center + toCounter * along;
    const qreal length = QLineF(m_center, end).length();
    if (length <= m_radius) // the circle already touches the box
        return {};
    const QLineF fromCounter(m_center, end);
    return QLineF(fromCounter.pointAt(m_radius / length), end);
}

QRectF CalloutAnnotation::bounds() const {
    QRectF r = boxRect();
    const QLineF line = leader();
    if (!line.isNull()) {
        const qreal half = leaderWidth() / 2;
        r = r.united(QRectF(line.p1(), line.p2()).normalized().adjusted(-half, -half, half, half));
    }
    return r;
}

CalloutAnnotation::Part CalloutAnnotation::partAt(const QPointF &point, qreal tolerance) const {
    if (boxRect().adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(point))
        return Box;
    const QLineF line = leader();
    if (line.isNull())
        return Nothing;
    // Distance from the point to the segment.
    const QPointF d = line.p2() - line.p1();
    const qreal len2 = d.x() * d.x() + d.y() * d.y();
    const qreal t = len2 > 0 ? std::clamp(QPointF::dotProduct(point - line.p1(), d) / len2, 0.0, 1.0) : 0;
    const QPointF nearest = line.p1() + d * t;
    return QLineF(nearest, point).length() <= leaderWidth() / 2 + tolerance ? Leader : Nothing;
}

void CalloutAnnotation::paint(QPainter &painter, const QImage &shot) const {
    if (m_carried)
        m_carried->paintNumbered(painter, shot, m_carriedOrdinal);
    if (!m_linked)
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const QLineF line = leader();
    if (!line.isNull()) {
        painter.setPen(QPen(m_color, leaderWidth(), Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(line);
    }
    const QRectF box = boxRect();
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_color);
    painter.drawRoundedRect(box, size() * 0.3, size() * 0.3);
    painter.setPen(TextAnnotation::contrastOf(m_color));
    const QFont font = TextAnnotation::fontFor(size());
    painter.setFont(font);
    const QFontMetricsF fm(font);
    const QStringList lines = m_text.split(QLatin1Char('\n'));
    const QPointF origin = textOrigin();
    for (int i = 0; i < lines.size(); ++i)
        painter.drawText(QPointF(origin.x(), origin.y() + i * fm.lineSpacing() + fm.ascent()), lines[i]);
    painter.restore();
}

namespace {

const RegisterAnnotationLoader loadCallout(QStringLiteral("callout"), [](const QJsonObject &o) -> std::unique_ptr<Annotation> {
    const auto origin = json::readPoint(o.value(QStringLiteral("origin")));
    if (!origin || !o.value(QStringLiteral("counter")).isDouble() || !o.value(QStringLiteral("text")).isString())
        return nullptr;
    return std::make_unique<CalloutAnnotation>(o.value(QStringLiteral("counter")).toInt(), *origin, o.value(QStringLiteral("text")).toString());
});

} // namespace

} // namespace tarn
