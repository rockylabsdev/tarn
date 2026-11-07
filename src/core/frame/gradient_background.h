#pragma once
// Gradient Backgrounds: color-stop gradients, used only for
// gradients built from the Palette by slot name. (Tarn's own gradients are
// pictures now: image_gradient_background.h.)
//
// Kind "gradient", params:
//   stops   list of colors ("#aarrggbb" text), evenly spaced; an element may
//           instead be a map {color, pos} with pos 0..1
//   angle   degrees, CSS style (0 = bottom to top, 90 = left to right,
//           180 = top to bottom). Default 135.
//   id      the catalog entry it came from (for the panel's ring)
//   slots   Palette gradients only: the Palette color names the stops were
//           taken from (so a Preset can follow the wallpaper)
#include "background.h"

#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>

namespace tarn {

class Palette;

struct PaletteGradientEntry {
    QString id;
    QString label;
    QStringList slotNames; // Palette color names (canvas, accent, text, ...)
    QString fallbackId; // an old built-in gradient (legacyGradientStops); its colors pick the image gradient used while there is no Palette file
    qreal angle = 135;
};

// Gradients made from the Palette, in panel order.
const QList<PaletteGradientEntry> &paletteGradients();

// The color stops of the eight color-stop gradients Tarn had before the image gradients
// ("sunset", "ocean", "aurora", "peach", "berry", "mint", "dusk", "graphite").
// They are no longer offered; the ids and colors live on so a Preset naming one
// resolves to the closest image gradient. Empty for any other id.
QStringList legacyGradientStops(const QString &id);
// The image gradient whose average color is closest to the old gradient `id`.
QString closestImageGradientForLegacy(const QString &id);
// Params for a Palette gradient. While no Palette file is in use (Palette not
// fromFile) these are the params of the closest image gradient instead.
QVariantMap paletteGradientParams(const QString &id, const Palette &palette);
// The color a Palette slot name stands for (invalid for an unknown name).
QColor paletteSlotColor(const Palette &palette, const QString &slot);

} // namespace tarn
