#pragma once
// Image gradients: the 20 ready-made gradient pictures Tarn ships (made by
// Jamey Smith with ChatGPT, MIT; see THIRD-PARTY.md). They replaced the eight
// color-stop gradients Tarn first had.
//
// Kind "image-gradient", params:
//   name   the picture's file name without ".jpg" ("glacial-bloom"); this is what
//          a Preset stores. A name Tarn does not ship paints a dark fallback and
//          reports `missing`.
// It fills the Frame's background the way a wallpaper does: cover-scaled and
// centered, on any shot size and Ratio.
#include "background.h"

#include <QColor>
#include <QList>
#include <QString>
#include <QVariantList>

namespace tarn {

// The images in panel order (fixed). `name` is also the label.
const QStringList &imageGradientNames();
// ":/tarn/backgrounds/<name>.jpg"
QString imageGradientPath(const QString &name);
// Params for the Background (empty map for a name Tarn does not ship).
QVariantMap imageGradientParams(const QString &name);

// The picture's average color (invalid for an unknown name). Worked out once on
// a small copy.
QColor imageGradientAverage(const QString &name);
// The shipped image whose average color is nearest to `color`.
QString closestImageGradient(const QColor &color);
// The same for color stops ("#aarrggbb" text, or {color, pos} maps): nearest to
// the stops' mean. Used when a Preset names an old color-stop gradient or a
// Palette gradient without a Palette. No usable stop = the first image.
QString closestImageGradient(const QVariantList &stops);
// The mean of the stops' colors (invalid when there are none).
QColor meanColor(const QVariantList &stops);

} // namespace tarn
