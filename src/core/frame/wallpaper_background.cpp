// The Wallpaper Background: an image file scaled to cover the framed image
// (centered, cropped, never stretched). Params: `path` (absolute), `label`.
// "The current wallpaper", a wallpaper-folder image and a "+" image are all this
// kind; they differ only in how the panel found the path (`current` marks the
// first so a Preset can follow the wallpaper).
//
// A file that is gone or unreadable never breaks a capture: the Background
// paints a neutral dark gradient instead and reports `missing`, which the panel
// shows as a quiet mark. The cover-scaling is shared with the image gradients
// (cover_image.h).
#include "background.h"
#include "cover_image.h"

#include <QFileInfo>
#include <QLinearGradient>

namespace tarn {

namespace {

bool pathMissing(const QVariantMap &params) {
    const QString path = params.value(QStringLiteral("path")).toString();
    return path.isEmpty() || !QFileInfo(path).isFile();
}

const RegisterBackgroundKind reg({
    QStringLiteral("wallpaper"),
    QStringLiteral("Wallpaper"),
    [](QPainter &p, const QRectF &target, const QVariantMap &params, const QImage &) {
        const QSize size = target.toAlignedRect().size();
        const QImage image = pathMissing(params) ? QImage() : coveredImage(params.value(QStringLiteral("path")).toString(), size);
        p.save();
        p.setCompositionMode(QPainter::CompositionMode_Source);
        if (image.isNull()) {
            QLinearGradient g(target.topLeft(), target.bottomLeft());
            g.setColorAt(0, QColor(0x4a, 0x4a, 0x4f));
            g.setColorAt(1, QColor(0x17, 0x17, 0x1a));
            p.fillRect(target, g);
        } else {
            p.drawImage(target.topLeft(), image);
        }
        p.restore();
    },
    [](const QVariantMap &) { return true; }, // the fallback is opaque too
    pathMissing,
    [](const QVariantMap &params, const QSize &size) {
        return coveredTile(pathMissing(params) ? QString() : params.value(QStringLiteral("path")).toString(), size);
    },
});

} // namespace

} // namespace tarn
