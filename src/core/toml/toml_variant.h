#pragma once
// The one place that turns toml++ nodes into QVariants and back. Used by
// RememberedState (state.toml) and PresetStore (presets.toml).
//
// QVariant side: bool, int / long long, double, string, QVariantList (array),
// QVariantMap (table). Dates and times are not Tarn's and read as an invalid QVariant.
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <toml++/toml.hpp>

namespace tarn::tomlvariant {

// A node as a QVariant. Integers that fit an int come back as int. An unsupported
// node (a date) is an invalid QVariant. Array items that are unsupported are left out.
QVariant fromNode(const toml::node &node);

// table[key] = value (replacing). Strings are written as strings; a QVariantMap
// becomes an inline table, a QVariantList an array. An invalid or null QVariant
// is skipped.
void assign(toml::table &table, const QString &key, const QVariant &value);

// A QVariantMap as an inline table (invalid / null values left out).
toml::table tableFromMap(const QVariantMap &map);

// A UTF-8 view as a QString.
QString toQString(std::string_view s);

} // namespace tarn::tomlvariant
