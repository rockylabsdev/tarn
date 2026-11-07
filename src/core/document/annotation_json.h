#pragma once
// Helpers for writing and reading an Annotation in the Project file.
// A kind saves itself with Annotation::toJson() and is rebuilt by the loader it
// registers (RegisterAnnotationLoader, annotation.h). Rules that keep the file
// easy to extend: fields are only ever added, a missing optional field means its
// default, and a loader returns null only when a field it cannot do without is
// missing or has the wrong shape (then the whole Project file counts as unreadable).
#include "annotation.h"

#include <QJsonArray>
#include <QJsonObject>
#include <optional>

namespace tarn::json {

inline QJsonObject point(const QPointF &p) { return {{QStringLiteral("x"), p.x()}, {QStringLiteral("y"), p.y()}}; }
inline QJsonObject rect(const QRectF &r) {
    return {{QStringLiteral("x"), r.x()}, {QStringLiteral("y"), r.y()}, {QStringLiteral("w"), r.width()}, {QStringLiteral("h"), r.height()}};
}
inline QJsonArray points(const QList<QPointF> &list) {
    QJsonArray out;
    for (const QPointF &p : list)
        out.append(point(p));
    return out;
}
// The whole ToolStyle an Annotation was drawn with.
inline QJsonObject style(const ToolStyle &s) {
    QJsonObject out{{QStringLiteral("color"), s.color.name(QColor::HexArgb)}, {QStringLiteral("lineWidth"), s.lineWidth},
                    {QStringLiteral("textSize"), s.textSize}, {QStringLiteral("counterSize"), s.counterSize},
                    {QStringLiteral("highlighterSize"), s.highlighterSize}, {QStringLiteral("textLook"), s.textLook},
                    {QStringLiteral("arrowStyle"), s.arrowStyle}, {QStringLiteral("counterStyle"), s.counterStyle},
                    {QStringLiteral("counterStart"), s.counterStart}};
    QJsonObject choices;
    for (auto it = s.choices.begin(); it != s.choices.end(); ++it)
        choices.insert(it.key(), it.value());
    out.insert(QStringLiteral("choices"), choices);
    return out;
}

// A field by its American name, or by the British name Project files used before Tarn
// spelled everything American ("colour", "centre"). This is the only place that knows
// the old names, so a file saved long ago still opens with its colors and positions.
inline QJsonValue field(const QJsonObject &o, const char *name, const char *oldName) {
    const QJsonValue v = o.value(QLatin1String(name));
    return v.isUndefined() ? o.value(QLatin1String(oldName)) : v;
}

inline std::optional<QPointF> readPoint(const QJsonValue &v) {
    const QJsonObject o = v.toObject();
    if (!o.value(QStringLiteral("x")).isDouble() || !o.value(QStringLiteral("y")).isDouble())
        return std::nullopt;
    return QPointF(o.value(QStringLiteral("x")).toDouble(), o.value(QStringLiteral("y")).toDouble());
}
inline std::optional<QRectF> readRect(const QJsonValue &v) {
    const QJsonObject o = v.toObject();
    for (const char *k : {"x", "y", "w", "h"})
        if (!o.value(QLatin1String(k)).isDouble())
            return std::nullopt;
    return QRectF(o.value(QStringLiteral("x")).toDouble(), o.value(QStringLiteral("y")).toDouble(), o.value(QStringLiteral("w")).toDouble(),
                  o.value(QStringLiteral("h")).toDouble());
}
inline std::optional<QList<QPointF>> readPoints(const QJsonValue &v) {
    if (!v.isArray())
        return std::nullopt;
    QList<QPointF> out;
    for (const QJsonValue &item : v.toArray()) {
        const auto p = readPoint(item);
        if (!p)
            return std::nullopt;
        out << *p;
    }
    return out;
}
// Missing fields keep ToolStyle's defaults.
inline ToolStyle readStyle(const QJsonObject &o) {
    ToolStyle s;
    auto number = [&](const char *key, qreal &into) {
        if (o.value(QLatin1String(key)).isDouble())
            into = o.value(QLatin1String(key)).toDouble();
    };
    auto integer = [&](const char *key, int &into) {
        if (o.value(QLatin1String(key)).isDouble())
            into = o.value(QLatin1String(key)).toInt();
    };
    if (const QColor c(field(o, "color", "colour").toString()); c.isValid())
        s.color = c;
    number("lineWidth", s.lineWidth);
    number("textSize", s.textSize);
    number("counterSize", s.counterSize);
    number("highlighterSize", s.highlighterSize);
    integer("textLook", s.textLook);
    integer("counterStyle", s.counterStyle);
    integer("counterStart", s.counterStart);
    if (o.value(QStringLiteral("arrowStyle")).isString())
        s.arrowStyle = o.value(QStringLiteral("arrowStyle")).toString();
    const QJsonObject choices = o.value(QStringLiteral("choices")).toObject();
    for (auto it = choices.begin(); it != choices.end(); ++it)
        s.choices.insert(it.key(), it.value().toString());
    return s;
}

} // namespace tarn::json
