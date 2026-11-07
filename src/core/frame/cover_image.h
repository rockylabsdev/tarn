#pragma once
// Cover-scaling shared by the Backgrounds that are a picture file (the Wallpaper
// and the image gradients): the picture is scaled to cover the target (centered,
// cropped, never stretched). `path` may be a Qt resource (":/...").
#include <QImage>
#include <QSize>
#include <QString>

namespace tarn {

// The last picture, decoded and scaled once: the canvas repaints on every slider
// drag, and a large JPEG takes tens of milliseconds to decode. Null if unreadable.
QImage coveredImage(const QString &path, const QSize &target);

// A small cover-scaled tile, decoding only a reduced copy (JPEG decodes at 1/2,
// 1/4, 1/8). Dark gray if the file cannot be read (or `path` is empty).
QImage coveredTile(const QString &path, const QSize &size);

} // namespace tarn
