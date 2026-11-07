#include "toml_variant.h"

#include <limits>

namespace tarn::tomlvariant {

QString toQString(std::string_view s) { return QString::fromUtf8(s.data(), qsizetype(s.size())); }

QVariant fromNode(const toml::node &node) {
    if (node.is_boolean())
        return *node.value<bool>();
    if (node.is_integer()) {
        const int64_t i = *node.value<int64_t>();
        if (i >= std::numeric_limits<int>::min() && i <= std::numeric_limits<int>::max())
            return int(i);
        return qlonglong(i);
    }
    if (node.is_floating_point())
        return *node.value<double>();
    if (node.is_string())
        return toQString(*node.value<std::string_view>());
    if (const toml::array *array = node.as_array()) {
        QVariantList list;
        for (const toml::node &item : *array) {
            const QVariant v = fromNode(item);
            if (v.isValid())
                list << v;
        }
        return list;
    }
    if (const toml::table *table = node.as_table()) {
        QVariantMap map;
        for (const auto &[key, value] : *table) {
            const QVariant v = fromNode(value);
            if (v.isValid())
                map.insert(toQString(key.str()), v);
        }
        return map;
    }
    return {}; // dates and times: not ours
}

namespace {

toml::array arrayFromList(const QVariantList &list);

// Calls sink(x) with the toml++ value for v.
template <class Sink>
void emitValue(const QVariant &v, Sink &&sink) {
    switch (v.typeId()) {
    case QMetaType::Bool: sink(v.toBool()); break;
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong: sink(int64_t(v.toLongLong())); break;
    case QMetaType::Double:
    case QMetaType::Float: sink(v.toDouble()); break;
    case QMetaType::QVariantList: sink(arrayFromList(v.toList())); break;
    case QMetaType::QVariantMap: sink(tableFromMap(v.toMap())); break;
    default: sink(v.toString().toStdString()); break;
    }
}

toml::array arrayFromList(const QVariantList &list) {
    toml::array array;
    for (const QVariant &v : list)
        emitValue(v, [&](auto &&x) { array.push_back(std::forward<decltype(x)>(x)); });
    return array;
}

} // namespace

void assign(toml::table &table, const QString &key, const QVariant &value) {
    if (!value.isValid() || value.isNull())
        return;
    emitValue(value, [&](auto &&x) { table.insert_or_assign(key.toStdString(), std::forward<decltype(x)>(x)); });
}

toml::table tableFromMap(const QVariantMap &map) {
    toml::table table;
    for (auto it = map.begin(); it != map.end(); ++it)
        assign(table, it.key(), it.value());
    table.is_inline(true);
    return table;
}

} // namespace tarn::tomlvariant
