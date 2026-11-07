#include "background.h"

#include <QList>
#include <QPainter>

namespace tarn {

namespace {
QList<BackgroundKind> &registry() {
    static QList<BackgroundKind> kinds;
    return kinds;
}
} // namespace

void BackgroundKinds::add(BackgroundKind kind) { registry().append(std::move(kind)); }

const BackgroundKind *BackgroundKinds::find(const QString &name) {
    for (const auto &k : registry())
        if (k.name == name)
            return &k;
    return nullptr;
}

QImage renderBackgroundTile(const QString &kind, const QVariantMap &params, const QSize &size, const QImage &shot) {
    const BackgroundKind *k = BackgroundKinds::find(kind);
    if (!k || !k->paint || size.isEmpty())
        return {};
    if (k->tile)
        return k->tile(params, size);
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    k->paint(p, QRectF(QPointF(0, 0), QSizeF(size)), params, shot);
    return out;
}

} // namespace tarn
