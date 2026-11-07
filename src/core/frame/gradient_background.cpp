#include "gradient_background.h"

#include "image_gradient_background.h"
#include "palette/palette.h"

#include <QLinearGradient>
#include <QMetaProperty>
#include <QVariantList>
#include <algorithm>
#include <cmath>

namespace tarn {

QStringList legacyGradientStops(const QString &id) {
    static const QHash<QString, QStringList> table = {
        {QStringLiteral("sunset"), {QStringLiteral("#ff9a5a"), QStringLiteral("#ff4f7b"), QStringLiteral("#7b3fe4")}},
        {QStringLiteral("ocean"), {QStringLiteral("#2ec5ce"), QStringLiteral("#1f6fe0"), QStringLiteral("#2a2f9c")}},
        {QStringLiteral("aurora"), {QStringLiteral("#41e6a8"), QStringLiteral("#2f8fe0"), QStringLiteral("#8a4fe0")}},
        {QStringLiteral("peach"), {QStringLiteral("#ffd1a8"), QStringLiteral("#ff8f8f")}},
        {QStringLiteral("berry"), {QStringLiteral("#d94fb5"), QStringLiteral("#5b2cc4")}},
        {QStringLiteral("mint"), {QStringLiteral("#b9f5d0"), QStringLiteral("#3bb8a1")}},
        {QStringLiteral("dusk"), {QStringLiteral("#4b5fa8"), QStringLiteral("#241b3f")}},
        {QStringLiteral("graphite"), {QStringLiteral("#4a4a4f"), QStringLiteral("#17171a")}},
    };
    return table.value(id);
}

const QList<PaletteGradientEntry> &paletteGradients() {
    static const QList<PaletteGradientEntry> list = {
        {QStringLiteral("accent-canvas"), QStringLiteral("Accent to canvas"), {QStringLiteral("accent"), QStringLiteral("canvas")}, QStringLiteral("dusk"), 135},
        {QStringLiteral("accent-text"), QStringLiteral("Accent to text"), {QStringLiteral("accent"), QStringLiteral("text")}, QStringLiteral("peach"), 135},
        {QStringLiteral("text2-canvas"), QStringLiteral("Soft to canvas"), {QStringLiteral("text2"), QStringLiteral("canvas")}, QStringLiteral("graphite"), 180},
        {QStringLiteral("border-accent-canvas"), QStringLiteral("Border, accent, canvas"),
         {QStringLiteral("border"), QStringLiteral("accent"), QStringLiteral("canvas")}, QStringLiteral("aurora"), 160},
    };
    return list;
}

QString closestImageGradientForLegacy(const QString &id) {
    const QStringList stops = legacyGradientStops(id);
    return closestImageGradient(QVariantList(stops.begin(), stops.end()));
}

QColor paletteSlotColor(const Palette &palette, const QString &slot) {
    const QByteArray name = slot.toUtf8();
    const QMetaObject *mo = palette.metaObject();
    const int i = mo->indexOfProperty(name.constData());
    if (i < 0 || mo->property(i).metaType() != QMetaType::fromType<QColor>())
        return {};
    return mo->property(i).read(&palette).value<QColor>();
}

QVariantMap paletteGradientParams(const QString &id, const Palette &palette) {
    for (const auto &g : paletteGradients()) {
        if (g.id != id)
            continue;
        if (!palette.fromFile())
            return imageGradientParams(closestImageGradientForLegacy(g.fallbackId));
        QVariantList stops;
        QVariantList slotList;
        for (const QString &slot : g.slotNames) {
            const QColor c = paletteSlotColor(palette, slot);
            stops << (c.isValid() ? c : QColor(Qt::gray)).name(QColor::HexArgb);
            slotList << slot;
        }
        return {{QStringLiteral("id"), g.id}, {QStringLiteral("stops"), stops}, {QStringLiteral("slots"), slotList}, {QStringLiteral("angle"), g.angle}};
    }
    return {};
}

namespace {

struct Stop {
    qreal pos;
    QColor color;
};

QList<Stop> stopsOf(const QVariantMap &params) {
    const QVariantList raw = params.value(QStringLiteral("stops")).toList();
    QList<Stop> out;
    for (int i = 0; i < raw.size(); ++i) {
        QColor c;
        qreal pos = raw.size() > 1 ? qreal(i) / (raw.size() - 1) : 0;
        if (raw[i].typeId() == QMetaType::QVariantMap) {
            const QVariantMap m = raw[i].toMap();
            c = QColor(m.value(QStringLiteral("color")).toString());
            if (m.contains(QStringLiteral("pos")))
                pos = std::clamp(m.value(QStringLiteral("pos")).toDouble(), 0.0, 1.0);
        } else {
            c = QColor(raw[i].toString());
        }
        if (c.isValid())
            out.append({pos, c});
    }
    return out;
}

// A gradient always has something to paint: fewer than two valid stops give a
// neutral dark one rather than nothing.
QList<Stop> stopsOrNeutral(const QVariantMap &params) {
    QList<Stop> stops = stopsOf(params);
    if (stops.size() < 2)
        stops = {{0, QColor(0x4a, 0x4a, 0x4f)}, {1, QColor(0x17, 0x17, 0x1a)}};
    return stops;
}

void paintGradient(QPainter &p, const QRectF &target, const QVariantMap &params, const QImage &) {
    const QList<Stop> stops = stopsOrNeutral(params);
    const qreal angle = params.value(QStringLiteral("angle"), 135.0).toDouble();
    // CSS: the gradient line passes through the center; its length is chosen so the
    // corners get exactly the end colors.
    const qreal rad = angle * M_PI / 180.0;
    const QPointF dir(std::sin(rad), -std::cos(rad));
    const qreal half = (std::abs(target.width() * dir.x()) + std::abs(target.height() * dir.y())) / 2;
    const QPointF c = target.center();
    QLinearGradient g(c - dir * half, c + dir * half);
    for (const Stop &s : stops)
        g.setColorAt(s.pos, s.color);
    p.save();
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(target, g);
    p.restore();
}

const RegisterBackgroundKind reg({
    QStringLiteral("gradient"),
    QStringLiteral("Gradient"),
    paintGradient,
    [](const QVariantMap &params) {
        for (const Stop &s : stopsOrNeutral(params))
            if (s.color.alpha() != 255)
                return false;
        return true;
    },
});

} // namespace

} // namespace tarn
