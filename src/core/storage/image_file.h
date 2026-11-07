#pragma once
// Writes a capture's PNG bytes to a path chosen by the user (Save as…), in the
// format its extension names: png, jpg / jpeg or webp.
#include <QByteArray>
#include <QString>

namespace tarn {

// JPEG and WebP quality for everything Tarn writes.
constexpr int kImageQuality = 90;

// "png", "jpeg" or "webp" for a file extension (jpg too); empty if it is none of them.
QByteArray formatForSuffix(const QString &suffix);

// The picture in the given format (see above), from PNG bytes. Empty with *error set.
QByteArray encodeImage(const QByteArray &png, const QByteArray &format, QString *error);

// Returns true on success. On failure it removes any partial file and sets *error
// to one line of plain text.
bool writeImageFile(const QByteArray &png, const QString &path, QString *error);

} // namespace tarn
