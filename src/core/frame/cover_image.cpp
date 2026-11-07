#include "cover_image.h"

#include <QDateTime>
#include <QFileInfo>
#include <QImageReader>
#include <mutex>

namespace tarn {

namespace {

struct Cache {
    QString path;
    QDateTime modified;
    QImage image; // as decoded
    QSize target;
    QImage covered; // scaled to cover `target` and cropped to it
};

QImage centerCrop(const QImage &scaled, const QSize &size) {
    return scaled.copy(QRect(QPoint((scaled.width() - size.width()) / 2, (scaled.height() - size.height()) / 2), size));
}

} // namespace

QImage coveredImage(const QString &path, const QSize &target) {
    static Cache cache;
    static std::mutex lock;
    std::lock_guard<std::mutex> guard(lock);
    const QFileInfo info(path);
    if (cache.path != path || cache.modified != info.lastModified()) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        cache = {};
        cache.path = path;
        cache.modified = info.lastModified();
        cache.image = reader.read();
    }
    if (cache.image.isNull())
        return {};
    if (cache.target != target) {
        cache.target = target;
        const QImage scaled = cache.image.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        cache.covered = centerCrop(scaled, target).convertToFormat(QImage::Format_RGB32);
    }
    return cache.covered;
}

QImage coveredTile(const QString &path, const QSize &size) {
    QImage out(size, QImage::Format_RGB32);
    out.fill(QColor(0x23, 0x23, 0x26));
    if (path.isEmpty())
        return out;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize full = reader.size();
    if (full.isValid())
        reader.setScaledSize(full.scaled(size, Qt::KeepAspectRatioByExpanding));
    const QImage small = reader.read();
    if (small.isNull())
        return out;
    return centerCrop(small.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation), size).convertToFormat(QImage::Format_RGB32);
}

} // namespace tarn
