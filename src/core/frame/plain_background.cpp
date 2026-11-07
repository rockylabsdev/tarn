// The plain color Background: fixed swatches, the transparent swatch, a custom
// color and Palette swatches are all this one kind (params: color, and for a
// Palette swatch `slot`, the name of the Palette color, so a Preset can follow
// the wallpaper later). Copy this file to add a Background kind.
#include "background.h"

#include <QColor>

namespace tarn {

namespace {
QColor colorOf(const QVariantMap &params) {
    const QColor c(params.value(QStringLiteral("color")).toString());
    return c.isValid() ? c : QColor(Qt::white);
}

const RegisterBackgroundKind reg({
    QStringLiteral("plain"),
    QStringLiteral("Plain color"),
    [](QPainter &p, const QRectF &target, const QVariantMap &params, const QImage &) {
        p.save();
        p.setCompositionMode(QPainter::CompositionMode_Source); // transparent stays transparent
        p.fillRect(target, colorOf(params));
        p.restore();
    },
    [](const QVariantMap &params) { return colorOf(params).alpha() == 255; },
});
} // namespace

} // namespace tarn
