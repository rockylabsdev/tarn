#include "png_encoder.h"

#include <QBuffer>
#include <QPromise>
#include <QThreadPool>

#include <memory>

namespace tarn {

QFuture<QByteArray> encodePngInBackground(const QImage &image) {
    auto promise = std::make_shared<QPromise<QByteArray>>(); // QThreadPool::start copies the job
    QFuture<QByteArray> future = promise->future();
    promise->start();
    QThreadPool::globalInstance()->start([promise, image] {
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG"))
            png.clear();
        promise->addResult(png);
        promise->finish();
    });
    return future;
}

} // namespace tarn
