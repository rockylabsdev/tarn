#include "window_shot.h"

#include <QPainter>

namespace tarn {

QImage cutWindow(const QImage &frozen, const WindowInfo &window, qreal factor) {
    const QRect r = window.rect;
    const int x1 = qRound(r.left() * factor), y1 = qRound(r.top() * factor);
    const int x2 = qRound((r.left() + r.width()) * factor), y2 = qRound((r.top() + r.height()) * factor);
    const QRect crop = QRect(x1, y1, x2 - x1, y2 - y1).intersected(frozen.rect());
    if (crop.isEmpty())
        return {};
    QImage out = frozen.copy(crop).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const qreal radius = window.rounding * factor;
    if (radius > 0) {
        QImage mask(out.size(), QImage::Format_ARGB32_Premultiplied);
        mask.fill(Qt::transparent);
        QPainter mp(&mask);
        mp.setRenderHint(QPainter::Antialiasing);
        mp.setPen(Qt::NoPen);
        mp.setBrush(Qt::white);
        // The whole window's rounded rectangle, placed relative to the crop (a window
        // cut off by the monitor edge is rounded only where its own corners are).
        mp.drawRoundedRect(QRectF(x1 - crop.left(), y1 - crop.top(), x2 - x1, y2 - y1), radius, radius);
        mp.end();
        QPainter p(&out);
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.drawImage(0, 0, mask);
    }
    return out;
}

QString fileSafeApp(const QString &app) {
    QString out;
    for (const QChar c : app.toLower())
        out += (c.isLetterOrNumber() || c == '.' || c == '-' || c == '_') ? c : QChar('-');
    return out;
}

} // namespace tarn
