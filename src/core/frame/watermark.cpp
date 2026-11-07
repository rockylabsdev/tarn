#include "watermark.h"

#include "annotations/text_annotation.h"

#include <QBuffer>
#include <QFontMetricsF>
#include <QImageReader>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace tarn {

namespace {
const QStringList kModes = {QStringLiteral("off"), QStringLiteral("image"), QStringLiteral("text")};
const QStringList kCorners = {QStringLiteral("top-left"), QStringLiteral("top-right"), QStringLiteral("bottom-left"),
                              QStringLiteral("bottom-right")};
} // namespace

QString Watermark::modeName(Mode mode) { return kModes.value(mode); }
QString Watermark::cornerName(int corner) { return kCorners.value(std::clamp(corner, 0, 3)); }
int Watermark::cornerFromName(const QString &name, int fallback) {
    const int i = kCorners.indexOf(name.trimmed().toLower());
    return i < 0 ? fallback : i;
}

bool Watermark::loadImage(const QString &file) {
    QImageReader reader(file);
    reader.setAutoTransform(true);
    QImage loaded = reader.read();
    if (loaded.isNull())
        return false;
    image = loaded.convertToFormat(QImage::Format_ARGB32);
    path = file;
    return true;
}

QByteArray Watermark::imagePng() const {
    QByteArray bytes;
    if (image.isNull())
        return bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QVariantMap Watermark::toMap() const {
    return {{QStringLiteral("mode"), modeName(mode)}, {QStringLiteral("path"), path},       {QStringLiteral("text"), text},
            {QStringLiteral("corner"), corner},       {QStringLiteral("size"), size},       {QStringLiteral("opacity"), opacity},
            {QStringLiteral("hasImage"), !image.isNull()}};
}

QVariantMap Watermark::rememberedMap() const {
    QVariantMap m = toMap();
    m.remove(QStringLiteral("hasImage"));
    return m;
}

Watermark Watermark::fromMap(const QVariantMap &m) {
    Watermark w;
    const int mode = kModes.indexOf(m.value(QStringLiteral("mode")).toString());
    w.mode = mode < 0 ? Off : Mode(mode);
    w.path = m.value(QStringLiteral("path")).toString();
    w.text = m.value(QStringLiteral("text")).toString();
    const QVariant corner = m.value(QStringLiteral("corner"));
    w.corner = corner.typeId() == QMetaType::QString ? cornerFromName(corner.toString()) : std::clamp(corner.isValid() ? corner.toInt() : int(BottomRight), 0, 3);
    w.size = std::clamp(m.value(QStringLiteral("size"), w.size).toDouble(), kMinSize, kMaxSize);
    w.opacity = std::clamp(m.value(QStringLiteral("opacity"), w.opacity).toDouble(), kMinOpacity, 1.0);
    const QByteArray png = QByteArray::fromBase64(m.value(QStringLiteral("png")).toByteArray());
    if (!png.isEmpty())
        w.image = QImage::fromData(png, "PNG").convertToFormat(QImage::Format_ARGB32);
    return w;
}

namespace {
struct MarkGeometry {
    QRect rect;
    QFont font; // Text only
};

MarkGeometry geometryOf(const Watermark &mark, const QRect &content) {
    const qreal s = std::min(content.width(), content.height());
    const int margin = int(std::lround(Watermark::kMargin * s));
    const int room = std::max(1, content.width() - 2 * margin); // never wider than the shot's inside
    const int roomH = std::max(1, content.height() - 2 * margin);
    const qreal longSide = std::lround(std::clamp(mark.size, Watermark::kMinSize, Watermark::kMaxSize) * s);

    int w = 0, h = 0;
    QFont font;
    if (mark.mode == Watermark::Image) {
        const QSizeF fit = QSizeF(mark.image.size()).scaled(QSizeF(std::min<qreal>(longSide, room), std::min<qreal>(longSide, roomH)), Qt::KeepAspectRatio);
        w = std::max(1, int(std::lround(fit.width())));
        h = std::max(1, int(std::lround(fit.height())));
    } else {
        qreal px = std::max(1.0, std::round(longSide * 0.7));
        font = TextAnnotation::fontFor(px);
        QFontMetricsF fm(font);
        const qreal natural = fm.horizontalAdvance(mark.text);
        if (natural > room) {
            font = TextAnnotation::fontFor(std::max(1.0, std::floor(px * room / natural)));
            fm = QFontMetricsF(font);
        }
        w = std::max(1, int(std::ceil(fm.horizontalAdvance(mark.text))));
        h = std::max(1, int(std::ceil(fm.height())));
    }
    const int x = content.left() + ((mark.corner % 2) ? content.width() - margin - w : margin);
    const int y = content.top() + ((mark.corner / 2) ? content.height() - margin - h : margin);
    return {QRect(x, y, w, h), font};
}
} // namespace

QRect watermarkRect(const Watermark &mark, const QRect &content) {
    if (!mark.on() || content.isEmpty())
        return {};
    if (mark.mode == Watermark::Image && mark.image.isNull())
        return {};
    if (mark.mode == Watermark::Text && mark.text.trimmed().isEmpty())
        return {};
    return geometryOf(mark, content).rect;
}

bool isLightUnder(const QImage &shot, const QRect &area) {
    const QRect r = area.intersected(shot.rect());
    if (r.isEmpty() || shot.isNull())
        return false;
    const int stride = std::max(1, std::min(r.width(), r.height()) / 16);
    double sum = 0;
    qint64 n = 0;
    for (int y = r.top(); y <= r.bottom(); y += stride)
        for (int x = r.left(); x <= r.right(); x += stride) {
            const QColor c = shot.pixelColor(x, y);
            const double a = c.alphaF(); // transparent pixels show nothing: count them as dark
            sum += a * (0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF());
            ++n;
        }
    return sum / double(n) > Watermark::kLightLuminance;
}

void paintWatermark(QPainter &painter, const Watermark &mark, const QRect &content, const QPainterPath &clip, bool lightUnder) {
    if (watermarkRect(mark, content).isEmpty())
        return;

    // The layer is drawn at the device's own scale (the Editor's zoom, the output scale) so it stays sharp.
    const qreal k = std::max(1.0, std::sqrt(std::abs(painter.worldTransform().determinant())));
    const MarkGeometry g = geometryOf(mark, content);
    const QFont font = g.font;
    const int x = g.rect.x(), y = g.rect.y(), w = g.rect.width(), h = g.rect.height();

    const int lw = std::max(1, int(std::lround(w * k))), lh = std::max(1, int(std::lround(h * k)));
    QImage layer(lw, lh, QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);
    {
        QPainter lp(&layer);
        lp.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
        if (mark.mode == Watermark::Image) {
            lp.drawImage(QRectF(0, 0, lw, lh), mark.image);
        } else {
            QFont scaled = font;
            scaled.setPixelSize(std::max(1, int(std::lround(font.pixelSize() * k))));
            lp.setFont(scaled);
            const QFontMetricsF fm(scaled);
            const QPointF base(0, fm.ascent());
            // A soft shadow of the opposite tone: a few faint copies spread just below and round the text.
            lp.setPen(lightUnder ? QColor(255, 255, 255, 38) : QColor(0, 0, 0, 38));
            const int reach = std::max(1, int(std::lround(k)));
            for (int dy = 0; dy <= 2 * reach; dy += reach)
                for (int dx = -reach; dx <= reach; dx += reach)
                    lp.drawText(base + QPointF(dx, dy), mark.text);
            lp.setPen(lightUnder ? QColor(0x1a, 0x1a, 0x1a) : QColor(Qt::white));
            lp.drawText(base, mark.text);
        }
    }

    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setClipPath(clip, Qt::IntersectClip);
    painter.setOpacity(painter.opacity() * mark.opacity);
    painter.drawImage(QRectF(x, y, w, h), layer);
    painter.restore();
}

} // namespace tarn
