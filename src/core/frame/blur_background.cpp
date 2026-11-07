// The blurred-tile Backgrounds: the shot itself, scaled to cover the framed
// image and blurred. Params: `style`, one of
//   light   a soft blur: the picture is still recognizable
//   heavy   a strong blur: colors only
//   dark    the heavy blur, darkened
// The blur is worked out on a small copy (a fixed number of pixels along the
// shorter side), so it looks the same on any shot size and is quick to repaint.
#include "background.h"

#include <QVariantMap>
#include <algorithm>
#include <vector>

namespace tarn {

namespace {

struct Strength {
    int workSide; // px along the shorter side of the small copy
    int radius;   // box-blur radius in small-copy px (three passes ~ a Gaussian)
    int darken;   // 0..255 black laid over the blur
};

Strength strengthOf(const QString &style) {
    if (style == QLatin1String("heavy"))
        return {96, 6, 0};
    if (style == QLatin1String("dark"))
        return {96, 6, 115};
    return {160, 2, 0}; // light, and anything unknown
}

// One pass of a box blur along a line of pixels; edges repeat.
void boxPass(QImage &image, int radius, bool horizontal) {
    const int w = image.width(), h = image.height();
    const int lines = horizontal ? h : w;
    const int length = horizontal ? w : h;
    const float norm = 1.0f / float(2 * radius + 1);
    std::vector<QRgb> out(static_cast<size_t>(length));
    for (int l = 0; l < lines; ++l) {
        auto at = [&](int i) -> QRgb & { return horizontal ? reinterpret_cast<QRgb *>(image.scanLine(l))[i] : reinterpret_cast<QRgb *>(image.scanLine(i))[l]; };
        float sum[3] = {0, 0, 0};
        auto add = [&](int i, float sign) {
            const QRgb c = at(std::clamp(i, 0, length - 1));
            sum[0] += sign * qRed(c);
            sum[1] += sign * qGreen(c);
            sum[2] += sign * qBlue(c);
        };
        for (int i = -radius; i <= radius; ++i)
            add(i, 1);
        for (int i = 0; i < length; ++i) {
            out[size_t(i)] = qRgb(int(sum[0] * norm + 0.5f), int(sum[1] * norm + 0.5f), int(sum[2] * norm + 0.5f));
            add(i + radius + 1, 1);
            add(i - radius, -1);
        }
        for (int i = 0; i < length; ++i)
            at(i) = out[size_t(i)];
    }
}

QImage blurredCover(const QImage &shot, const QSize &target, const Strength &s) {
    const qreal k = qreal(s.workSide) / std::min(target.width(), target.height());
    const QSize work(std::max(1, int(target.width() * k + 0.5)), std::max(1, int(target.height() * k + 0.5)));
    QImage small = shot.convertToFormat(QImage::Format_RGB32).scaled(work, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    small = small.copy(QRect(QPoint((small.width() - work.width()) / 2, (small.height() - work.height()) / 2), work));
    for (int pass = 0; pass < 3; ++pass) {
        boxPass(small, s.radius, true);
        boxPass(small, s.radius, false);
    }
    return small;
}

const RegisterBackgroundKind reg({
    QStringLiteral("blur"),
    QStringLiteral("Blurred shot"),
    [](QPainter &p, const QRectF &target, const QVariantMap &params, const QImage &shot) {
        const Strength s = strengthOf(params.value(QStringLiteral("style")).toString());
        p.save();
        p.setCompositionMode(QPainter::CompositionMode_Source);
        if (shot.isNull()) {
            p.fillRect(target, QColor(0x17, 0x17, 0x1a));
        } else {
            const QImage small = blurredCover(shot, target.toAlignedRect().size(), s);
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.drawImage(target, small);
        }
        if (s.darken > 0) {
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            p.fillRect(target, QColor(0, 0, 0, s.darken));
        }
        p.restore();
    },
    [](const QVariantMap &) { return true; },
});

} // namespace

} // namespace tarn
