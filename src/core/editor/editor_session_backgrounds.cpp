// The Background panel's gradients, wallpapers and blurred tiles: the lists
// the panel shows and the calls that pick one. Picking is setFrameBackground with
// the right kind and params, so it is one undo step like every Background.
#include "editor_session.h"

#include "daemon/daemon.h"
#include "editor.h"
#include "frame/gradient_background.h"
#include "frame/image_gradient_background.h"
#include "notify/notifier.h"
#include "palette/palette.h"
#include "wallpaper/wallpapers.h"

#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace tarn {

namespace {

// image://background/<kind>/<percent-encoded JSON of the params>
QString tileUrl(const QString &kind, const QVariantMap &params) {
    const QByteArray json = QJsonDocument(QJsonObject::fromVariantMap(params)).toJson(QJsonDocument::Compact);
    return QStringLiteral("image://background/%1/%2").arg(kind, QString::fromLatin1(QUrl::toPercentEncoding(QString::fromUtf8(json))));
}

QVariantMap entry(const QString &kind, const QString &key, const QString &label, const QVariantMap &params) {
    return {{QStringLiteral("kind"), kind}, {QStringLiteral("id"), key}, {QStringLiteral("label"), label},
            {QStringLiteral("params"), params}, {QStringLiteral("tile"), tileUrl(kind, params)}};
}

const QList<QPair<QString, QString>> &blurStyles() {
    static const QList<QPair<QString, QString>> list = {
        {QStringLiteral("light"), QStringLiteral("Light blur")},
        {QStringLiteral("heavy"), QStringLiteral("Heavy blur")},
        {QStringLiteral("dark"), QStringLiteral("Heavy blur, darkened")},
    };
    return list;
}

} // namespace

QVariantList EditorSession::gradients() const {
    QVariantList list;
    for (const QString &name : imageGradientNames()) // the 20 image gradients, named by file
        list << entry(QStringLiteral("image-gradient"), name, name, imageGradientParams(name));
    return list;
}

QVariantList EditorSession::paletteGradients() const {
    const Palette &palette = m_editor.daemon().service<Palette>();
    QVariantList list;
    if (!palette.fromFile()) // no Palette file: hidden (a Preset falls back to the closest built-in gradient)
        return list;
    for (const auto &g : tarn::paletteGradients())
        list << entry(QStringLiteral("gradient"), g.id, g.label, paletteGradientParams(g.id, palette));
    return list;
}

QVariantList EditorSession::wallpapers() const {
    QVariantList list;
    for (const QVariant &v : m_editor.daemon().service<Wallpapers>().list()) {
        const QVariantMap w = v.toMap();
        const QVariantMap params = {{QStringLiteral("path"), w.value(QStringLiteral("path"))},
                                    {QStringLiteral("current"), w.value(QStringLiteral("current"))}};
        QVariantMap e = entry(QStringLiteral("wallpaper"), w.value(QStringLiteral("path")).toString(),
                              w.value(QStringLiteral("current")).toBool() ? QStringLiteral("Current wallpaper")
                                                                          : w.value(QStringLiteral("label")).toString(),
                              params);
        e.insert(QStringLiteral("current"), w.value(QStringLiteral("current")));
        list << e;
    }
    return list;
}

QVariantList EditorSession::blurTiles() const {
    QVariantList list;
    for (const auto &[style, label] : blurStyles())
        list << entry(QStringLiteral("blur"), style, label, {{QStringLiteral("style"), style}});
    return list;
}

void EditorSession::setFrameGradient(const QString &id) {
    const QVariantMap params = imageGradientParams(id);
    if (!params.isEmpty())
        setFrameBackground(QStringLiteral("image-gradient"), params);
}

void EditorSession::setFramePaletteGradient(const QString &id) {
    const QVariantMap params = paletteGradientParams(id, m_editor.daemon().service<Palette>());
    // Without a Palette file the Palette gradient is its closest image gradient (no slots).
    if (!params.isEmpty())
        setFrameBackground(params.contains(QStringLiteral("slots")) ? QStringLiteral("gradient") : QStringLiteral("image-gradient"), params);
}

void EditorSession::setFrameBlur(const QString &style) {
    for (const auto &s : blurStyles())
        if (s.first == style)
            return setFrameBackground(QStringLiteral("blur"), {{QStringLiteral("style"), style}});
}

bool EditorSession::setFrameWallpaper(const QString &pathOrUrl, bool current) {
    const QUrl url(pathOrUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
    if (!QFileInfo(path).isFile() || !QImageReader(path).canRead()) {
        m_editor.daemon().service<Notifier>().notify(QStringLiteral("Tarn could not use that image"),
                                                     QFileInfo(path).fileName() + QStringLiteral(" is not a picture Tarn can read"));
        return false;
    }
    setFrameBackground(QStringLiteral("wallpaper"),
                       {{QStringLiteral("path"), QFileInfo(path).absoluteFilePath()}, {QStringLiteral("current"), current}});
    return true;
}

QImage EditorSession::backgroundTile(const QString &kind, const QVariantMap &params, const QSize &size) const {
    return renderBackgroundTile(kind, params, size, m_document->shot());
}

} // namespace tarn
