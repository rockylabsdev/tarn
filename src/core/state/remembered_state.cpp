#include "remembered_state.h"

#include "daemon/daemon.h"
#include "toml/toml_variant.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <sstream>
#include <toml++/toml.hpp>

namespace tarn {

namespace {
// state.toml holds flat scalars; an array or table inside it is not ours.
QVariant scalarFrom(const toml::node &node) {
    const QVariant v = tomlvariant::fromNode(node);
    return v.typeId() == QMetaType::QVariantList || v.typeId() == QMetaType::QVariantMap ? QVariant() : v;
}
} // namespace

QString RememberedState::defaultPath() {
    QString state = qEnvironmentVariable("XDG_STATE_HOME");
    if (state.isEmpty())
        state = QDir::homePath() + QStringLiteral("/.local/state");
    return state + QStringLiteral("/tarn/state.toml");
}

RememberedState::RememberedState(Daemon &) : RememberedState(defaultPath()) {}

RememberedState::RememberedState(const QString &path, QObject *parent) : QObject(parent), m_path(path) {
    m_soon.setSingleShot(true);
    m_soon.setInterval(kSoonMs);
    connect(&m_soon, &QTimer::timeout, this, [this] { save(); });
    load();
}

RememberedState::~RememberedState() {
    if (m_soon.isActive())
        save();
}

void RememberedState::load() {
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QByteArray bytes = file.readAll();
    toml::table root;
    try {
        root = toml::parse(std::string_view(bytes.constData(), size_t(bytes.size())));
    } catch (const toml::parse_error &) {
        return; // garbled: nothing remembered
    }
    for (const auto &[key, node] : root) {
        const QString name = QString::fromUtf8(key.data(), qsizetype(key.length()));
        if (const toml::table *section = node.as_table()) {
            for (const auto &[k, v] : *section) {
                const QVariant value = scalarFrom(v);
                if (value.isValid())
                    m_values.insert(name + '.' + QString::fromUtf8(k.data(), qsizetype(k.length())), value);
            }
        } else if (const QVariant value = scalarFrom(node); value.isValid()) {
            m_values.insert(name, value);
        }
    }
    // state.toml files written before Tarn spelled everything American keep their saved colors
    // and tool color under the old names. This is the only place that knows them: the value
    // moves to the new name (unless the new name is already there) and the next save
    // writes only the new one.
    const std::pair<const char *, const char *> renamed[] = {{"colours.saved", "colors.saved"}, {"tool.colour", "tool.color"}};
    for (const auto &[oldKey, key] : renamed)
        if (m_values.contains(QLatin1String(oldKey))) {
            const QVariant old = m_values.take(QLatin1String(oldKey));
            if (!m_values.contains(QLatin1String(key)))
                m_values.insert(QLatin1String(key), old);
        }
}

void RememberedState::save() const {
    m_soon.stop(); // this write carries any value waiting to be written
    toml::table root;
    for (auto it = m_values.cbegin(); it != m_values.cend(); ++it) {
        const int dot = it.key().indexOf('.');
        toml::table *target = &root;
        if (dot >= 0) {
            const std::string section = it.key().left(dot).toStdString();
            target = root.emplace<toml::table>(section).first->second.as_table();
            if (!target)
                continue;
        }
        const std::string name = (dot < 0 ? it.key() : it.key().mid(dot + 1)).toStdString();
        // An empty string is still a value (a null QVariant would be skipped).
        const QVariant &v = it.value();
        tomlvariant::assign(*target, QString::fromStdString(name), v.typeId() == QMetaType::QString && v.isNull() ? QVariant(QStringLiteral("")) : v);
    }
    std::ostringstream text;
    text << "# Written by Tarn. Safe to delete; Tarn starts fresh. Your Settings are in config.toml.\n\n" << toml::toml_formatter{root, toml::format_flags::indentation} << '\n'; // plain "strings", no 'literal' ones
    const std::string bytes = text.str();

    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly))
        return; // remembering is best effort: never fail a capture over it
    file.write(bytes.data(), qint64(bytes.size()));
    file.commit();
}

void RememberedState::setValue(const QString &key, const QVariant &v) {
    if (m_values.contains(key) && m_values.value(key) == v)
        return;
    m_values.insert(key, v);
    save();
}

void RememberedState::setValueSoon(const QString &key, const QVariant &v) {
    if (m_values.contains(key) && m_values.value(key) == v)
        return;
    m_values.insert(key, v);
    m_soon.start(); // restarted by every change: written once it settles
}

void RememberedState::remove(const QString &key) {
    if (m_values.remove(key))
        save();
}

QRect RememberedState::rect(const QString &prefix) const {
    for (const char *part : {".x", ".y", ".width", ".height"})
        if (!contains(prefix + part))
            return {};
    return QRect(value(prefix + ".x").toInt(), value(prefix + ".y").toInt(), value(prefix + ".width").toInt(),
                 value(prefix + ".height").toInt());
}

void RememberedState::setRect(const QString &prefix, const QRect &r) {
    // One write for four keys.
    const QMap<QString, QVariant> before = m_values;
    m_values.insert(prefix + ".x", r.x());
    m_values.insert(prefix + ".y", r.y());
    m_values.insert(prefix + ".width", r.width());
    m_values.insert(prefix + ".height", r.height());
    if (m_values != before)
        save();
}

QVariantMap RememberedState::map(const QString &prefix) const {
    const QString start = prefix + '.';
    QVariantMap out;
    for (auto it = m_values.lowerBound(start); it != m_values.cend() && it.key().startsWith(start); ++it)
        out.insert(it.key().mid(start.size()), it.value());
    return out;
}

void RememberedState::setMap(const QString &prefix, const QVariantMap &values, bool soon) {
    bool changed = false;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        const QString key = prefix + '.' + it.key();
        if (m_values.contains(key) && m_values.value(key) == it.value())
            continue;
        m_values.insert(key, it.value());
        changed = true;
    }
    if (!changed)
        return;
    if (soon)
        m_soon.start();
    else
        save();
}

} // namespace tarn
