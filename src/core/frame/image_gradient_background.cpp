#include "image_gradient_background.h"

#include "cover_image.h"

#include <QImageReader>
#include <QLinearGradient>
#include <QStringList>
#include <climits>
#include <mutex>

namespace tarn {

const QStringList &imageGradientNames() {
    static const QStringList names = {
        QStringLiteral("glacial-bloom"),    QStringLiteral("pearl-blush"),    QStringLiteral("crimson-refraction"),
        QStringLiteral("lilac-drift"),      QStringLiteral("solar-flare"),    QStringLiteral("prism-tide"),
        QStringLiteral("ember-plum"),       QStringLiteral("violet-afterglow"), QStringLiteral("amber-horizon"),
        QStringLiteral("coral-heat"),       QStringLiteral("chromatic-fault"), QStringLiteral("golden-current"),
        QStringLiteral("peach-frost"),      QStringLiteral("dusk-haze"),      QStringLiteral("sea-glass"),
        QStringLiteral("moonlit-ink"),      QStringLiteral("peach-mirage"),   QStringLiteral("midnight-current"),
        QStringLiteral("orchid-veil"),      QStringLiteral("rose-eclipse"),
    };
    return names;
}

QString imageGradientPath(const QString &name) { return QStringLiteral(":/tarn/backgrounds/") + name + QStringLiteral(".jpg"); }

QVariantMap imageGradientParams(const QString &name) {
    if (!imageGradientNames().contains(name))
        return {};
    return {{QStringLiteral("name"), name}};
}

QColor imageGradientAverage(const QString &name) {
    static QHash<QString, QColor> cache;
    static std::mutex lock;
    if (!imageGradientNames().contains(name))
        return {};
    std::lock_guard<std::mutex> guard(lock);
    if (const auto it = cache.constFind(name); it != cache.constEnd())
        return it.value();
    QImageReader reader(imageGradientPath(name));
    reader.setScaledSize(QSize(32, 18));
    const QImage small = reader.read().convertToFormat(QImage::Format_RGB32);
    QColor mean;
    if (!small.isNull()) {
        qint64 r = 0, g = 0, b = 0;
        for (int y = 0; y < small.height(); ++y)
            for (int x = 0; x < small.width(); ++x) {
                const QRgb px = small.pixel(x, y);
                r += qRed(px), g += qGreen(px), b += qBlue(px);
            }
        const qint64 n = qint64(small.width()) * small.height();
        mean = QColor(int(r / n), int(g / n), int(b / n));
    }
    cache.insert(name, mean);
    return mean;
}

QColor meanColor(const QVariantList &stops) {
    int r = 0, g = 0, b = 0, n = 0;
    for (const QVariant &v : stops) {
        const QColor c(v.typeId() == QMetaType::QVariantMap ? v.toMap().value(QStringLiteral("color")).toString() : v.toString());
        if (!c.isValid())
            continue;
        r += c.red(), g += c.green(), b += c.blue(), ++n;
    }
    return n ? QColor(r / n, g / n, b / n) : QColor();
}

QString closestImageGradient(const QColor &color) {
    QString best = imageGradientNames().first();
    if (!color.isValid())
        return best;
    int bestDistance = INT_MAX;
    for (const QString &name : imageGradientNames()) {
        const QColor c = imageGradientAverage(name);
        if (!c.isValid())
            continue;
        const int dr = c.red() - color.red(), dg = c.green() - color.green(), db = c.blue() - color.blue();
        const int d = dr * dr + dg * dg + db * db;
        if (d < bestDistance) {
            bestDistance = d;
            best = name;
        }
    }
    return best;
}

QString closestImageGradient(const QVariantList &stops) { return closestImageGradient(meanColor(stops)); }

namespace {

bool nameMissing(const QVariantMap &params) { return !imageGradientNames().contains(params.value(QStringLiteral("name")).toString()); }

const RegisterBackgroundKind reg({
    QStringLiteral("image-gradient"),
    QStringLiteral("Gradient"),
    [](QPainter &p, const QRectF &target, const QVariantMap &params, const QImage &) {
        const QSize size = target.toAlignedRect().size();
        const QImage image = nameMissing(params) ? QImage() : coveredImage(imageGradientPath(params.value(QStringLiteral("name")).toString()), size);
        p.save();
        p.setCompositionMode(QPainter::CompositionMode_Source);
        if (image.isNull()) {
            QLinearGradient g(target.topLeft(), target.bottomLeft());
            g.setColorAt(0, QColor(0x4a, 0x4a, 0x4f));
            g.setColorAt(1, QColor(0x17, 0x17, 0x1a));
            p.fillRect(target, g);
        } else {
            p.drawImage(target.topLeft(), image);
        }
        p.restore();
    },
    [](const QVariantMap &) { return true; }, // JPEGs have no transparency; the fallback is opaque too
    nameMissing,
    [](const QVariantMap &params, const QSize &size) {
        return coveredTile(nameMissing(params) ? QString() : imageGradientPath(params.value(QStringLiteral("name")).toString()), size);
    },
});

} // namespace

} // namespace tarn
