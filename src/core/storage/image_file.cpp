#include "image_file.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QPainter>
#include <QSaveFile>

namespace tarn {

QByteArray formatForSuffix(const QString &suffixIn) {
    const QString suffix = suffixIn.toLower();
    if (suffix == QLatin1String("png"))
        return "png";
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg"))
        return "jpeg";
    if (suffix == QLatin1String("webp"))
        return "webp";
    return {};
}

QByteArray encodeImage(const QByteArray &png, const QByteArray &format, QString *error) {
    if (format == "png")
        return png;
    if (!QImageWriter::supportedImageFormats().contains(format)) {
        *error = QStringLiteral("this computer cannot write %1 (the Qt image format plugin is missing)").arg(QString::fromLatin1(format).toUpper());
        return {};
    }
    QImage image = QImage::fromData(png, "PNG");
    if (image.isNull()) {
        *error = QStringLiteral("the picture could not be read");
        return {};
    }
    if (image.hasAlphaChannel()) { // these formats have no see-through: lay it on white
        QImage flat(image.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter(&flat).drawImage(0, 0, image);
        image = flat;
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, format);
    writer.setQuality(kImageQuality);
    if (!writer.write(image)) {
        *error = writer.errorString();
        return {};
    }
    return bytes;
}

bool writeImageFile(const QByteArray &png, const QString &path, QString *error) {
    const QByteArray format = formatForSuffix(QFileInfo(path).suffix());
    if (format.isEmpty()) {
        *error = QStringLiteral("%1 is not PNG, JPEG or WebP").arg(QFileInfo(path).fileName());
        return false;
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        *error = QStringLiteral("could not create the folder %1").arg(QFileInfo(path).absolutePath());
        return false;
    }
    QString problem;
    const QByteArray bytes = encodeImage(png, format, &problem);
    // Written beside the target and renamed over it only when complete, so a
    // failed write never costs the user the file that was already there.
    QSaveFile file(path);
    if (!problem.isEmpty()) {
        problem = QStringLiteral("could not write %1: %2").arg(path, problem);
    } else if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        problem = QStringLiteral("could not write %1: %2").arg(path, file.errorString());
    } else if (!file.commit()) {
        problem = QStringLiteral("could not write %1: %2").arg(path, file.errorString());
    }
    if (!problem.isEmpty()) {
        file.cancelWriting();
        *error = problem;
        return false;
    }
    return true;
}

} // namespace tarn
