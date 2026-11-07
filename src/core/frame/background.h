#pragma once
// A Background kind: how the fill behind the shot inside a Frame is painted.
// This is the extension point for the Background kinds (gradients, wallpapers, blurred tiles):
// a new kind is one file with one `const RegisterBackgroundKind reg({...});`
// (copy plain_background.cpp). The Background panel, the Frame's export and the
// Presets file all go through the registry, so nothing else is edited.
//
// A Background is a kind name plus a map of parameters (a QVariantMap, so a
// Preset can store it as plain text). "none" is not a kind: it means no Frame.
#include <QImage>
#include <QPainter>
#include <QRectF>
#include <QString>
#include <QVariantMap>
#include <functional>

namespace tarn {

struct Background {
    QString kind = QStringLiteral("none");
    QVariantMap params;
    bool operator==(const Background &) const = default;
};

struct BackgroundKind {
    QString name;  // "plain"
    QString label; // for tooltips
    // Fills `target` (the whole framed image, in its pixels). `shot` is the
    // picture being framed, for kinds made from it (blurred tiles).
    std::function<void(QPainter &, const QRectF &target, const QVariantMap &params, const QImage &shot)> paint;
    // True when paint() covers every pixel of the target with no transparency.
    std::function<bool(const QVariantMap &params)> opaque;
    // Optional: true when what the params point at is gone (a deleted wallpaper
    // file). paint() then falls back quietly; the panel shows a small mark.
    std::function<bool(const QVariantMap &params)> missing;
    // Optional: a cheaper way to make a small tile of this Background (a wallpaper
    // decodes only a reduced copy). Without it the tile is paint() into a small image.
    std::function<QImage(const QVariantMap &params, const QSize &size)> tile;
};

// The Background's picture at `size` (used for the panel's tiles and by tests).
// `shot` is what kinds made from the shot read. Null image for an unknown kind.
QImage renderBackgroundTile(const QString &kind, const QVariantMap &params, const QSize &size, const QImage &shot);

class BackgroundKinds {
public:
    static void add(BackgroundKind kind);
    static const BackgroundKind *find(const QString &name);
};

struct RegisterBackgroundKind {
    explicit RegisterBackgroundKind(BackgroundKind kind) { BackgroundKinds::add(std::move(kind)); }
};

} // namespace tarn
