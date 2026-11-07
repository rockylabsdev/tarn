#pragma once
// The picture effect behind the Pixelate tool: a mosaic or a blur of the shot.
//
// Everything here is a function of (shot, style, strength, seed) and of PICTURE
// coordinates only: the mosaic's blocks sit on a grid anchored at the picture's
// top left, and every random choice (which pixel a block takes, its shift, the
// blur's faint noise) comes from a hash of the seed and the position. So an
// effect rendered for the whole picture, cached, and clipped to a region shows
// exactly what rendering just that region would. The Editor canvas relies on
// that: moving or resizing a region never re-renders anything.
#include <QImage>
#include <QRect>
#include <QString>
#include <memory>

namespace tarn::pixelate {

constexpr int kStrengths = 5;

// Mosaic block size and blur radius (shot px) for strength 1..5.
int blockSize(int strength);
int blurRadius(int strength);

// The effect for `area` (picture px, clipped to the picture). Style is
// "pixelate" or "blur". The returned image is `area`-sized, RGB32.
//
// Mosaic: every block takes the color of ONE random pixel inside it, plus a
// small random shift of its own for the whole block. Never an average, so a
// block cannot be reversed by matching averages; and no per-pixel static, so
// the blocks are flat.
QImage render(const QImage &shot, const QString &style, int strength, quint64 seed, const QRect &area);

// The whole picture's effect, cached by (picture, style, strength, seed). The
// Editor canvas draws from it. At most a handful stay in memory.
std::shared_ptr<const QImage> wholePicture(const QImage &shot, const QString &style, int strength, quint64 seed);

// How many times render() has run since the program started (tests bound the
// repaint cost of a drag with it).
quint64 renderCount();

} // namespace tarn::pixelate
