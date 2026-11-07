#include "palette.h"

#include "daemon/daemon.h"
#include "settings/settings.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <array>

namespace tarn {

namespace {

// Tarn's built-in colors: neutral dark tokens with a blue accent.
constexpr std::array<const char *, 15> kFallback = {
    "#101010", // canvas
    "#181818", // surface
    "#1D1D1D", // raised
    "#222222", // control
    "#292929", // hover
    "#303030", // pressed
    "#303030", // track
    "#F2F2F2", // text
    "#B0B0B0", // text2
    "#909090", // muted
    "#646464", // disabled
    "#252525", // border
    "#262626", // divider
    "#9ABCF2", // accent
    "#111111", // accentText
};

// Which Palette file key feeds each token (Material 3 names).
constexpr std::array<const char *, 15> kKey = {
    "surface_container_lowest",  // canvas
    "surface_container_low",     // surface
    "surface_container",         // raised
    "surface_container_high",    // control
    "surface_container_highest", // hover
    "surface_bright",            // pressed
    "surface_bright",            // track
    "on_surface",                // text
    "on_surface_variant",        // text2
    "outline",                   // muted
    "outline_variant",           // disabled
    "surface_container_highest", // border
    "surface_container_highest", // divider
    "primary",                   // accent
    "on_primary",                // accentText
};

// A strict "#rrggbb" color; anything else is invalid.
QColor parseColor(const QJsonValue &v) {
    if (!v.isString())
        return {};
    const QString s = v.toString();
    if (s.size() != 7 || s[0] != QLatin1Char('#'))
        return {};
    return QColor(s); // invalid QColor if not hex
}

QJsonObject readObject(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QJsonParseError err;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    return err.error == QJsonParseError::NoError && doc.isObject() ? doc.object() : QJsonObject{};
}

} // namespace

Palette::Palette(Daemon &daemon) : QObject(nullptr) {
    auto &settings = daemon.service<Settings>();
    m_path = settings.paletteFile();
    m_accentSetting = parseColor(settings.accentColor());
    connect(&settings, &Settings::changed, this, [this, &settings](const QString &key) {
        if (key == QLatin1String("palette_file")) {
            m_path = settings.paletteFile();
            m_file.setPath(m_path);
            reload();
        } else if (key == QLatin1String("accent_color")) {
            m_accentSetting = parseColor(settings.accentColor());
            reload();
        }
    });
    init();
}

Palette::Palette(const QString &palettePath, QObject *parent) : QObject(parent), m_path(palettePath) { init(); }

void Palette::init() {
    connect(&m_file, &WatchedFile::changed, this, &Palette::reload);
    m_file.setPath(m_path);
    for (int i = 0; i < TokenCount; ++i)
        m_c[i] = QColor(kFallback[i]);
    reload();
}

void Palette::reload() {
    QColor next[TokenCount];
    for (int i = 0; i < TokenCount; ++i)
        next[i] = QColor(kFallback[i]);

    const QJsonObject pal = m_path.isEmpty() ? QJsonObject{} : readObject(m_path);
    const bool fromFile = !pal.isEmpty();
    if (fromFile) {
        for (int i = 0; i < TokenCount; ++i) {
            const QColor c = parseColor(pal.value(QLatin1String(kKey[i])));
            if (c.isValid())
                next[i] = c;
        }
        // An explicit accent / on_accent wins over primary / on_primary.
        if (const QColor a = parseColor(pal.value(QStringLiteral("accent"))); a.isValid())
            next[Accent] = a;
        if (const QColor t = parseColor(pal.value(QStringLiteral("on_accent"))); t.isValid())
            next[AccentText] = t;
    }
    // The accent_color Setting beats everything for the accent itself.
    if (m_accentSetting.isValid())
        next[Accent] = m_accentSetting;

    bool differs = fromFile != m_fromFile;
    for (int i = 0; i < TokenCount; ++i)
        differs = differs || next[i] != m_c[i];
    m_fromFile = fromFile;
    for (int i = 0; i < TokenCount; ++i)
        m_c[i] = next[i];
    m_file.rewatch();
    if (differs)
        emit changed();
}

} // namespace tarn
