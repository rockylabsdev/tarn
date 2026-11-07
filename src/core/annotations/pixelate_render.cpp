#include "pixelate_render.h"

#include <QMutex>
#include <QMutexLocker>
#include <QRandomGenerator>
#include <algorithm>
#include <atomic>
#include <deque>
#include <vector>

namespace tarn::pixelate {

namespace {

constexpr int kBlock[kStrengths] = {6, 10, 14, 20, 28};
constexpr int kBlurRadius[kStrengths] = {3, 6, 9, 13, 18};
constexpr int kBlockShift = 4;  // +/- on the three channels together, one value per block
constexpr int kBlurNoise = 4;   // +/- per pixel on the blur, so it cannot be inverted exactly
constexpr size_t kCacheEntries = 6;

std::atomic<quint64> g_renders{0};

int clampChannel(int v) { return std::clamp(v, 0, 255); }

// A well mixed 64-bit value from the seed and two coordinates.
quint64 mix(quint64 seed, quint64 a, quint64 b) {
    quint64 z = seed + 0x9E3779B97F4A7C15ull * (a + 1) + 0xBF58476D1CE4E5B9ull * (b + 1);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

QImage mosaic(const QImage &src, int block, quint64 seed, const QRect &area) {
    QImage out(area.size(), QImage::Format_RGB32);
    const int firstBx = area.left() / block * block, firstBy = area.top() / block * block;
    for (int by = firstBy; by <= area.bottom(); by += block)
        for (int bx = firstBx; bx <= area.right(); bx += block) {
            const QRect cell = QRect(bx, by, block, block).intersected(src.rect());
            QRandomGenerator rng(mix(seed, quint64(bx / block), quint64(by / block)));
            const int px = cell.left() + int(rng.bounded(quint32(cell.width())));
            const int py = cell.top() + int(rng.bounded(quint32(cell.height())));
            const int shift = int(rng.bounded(2 * kBlockShift + 1)) - kBlockShift;
            const QRgb p = src.pixel(px, py);
            const QRgb color = qRgb(clampChannel(qRed(p) + shift), clampChannel(qGreen(p) + shift), clampChannel(qBlue(p) + shift));
            const QRect visible = cell.intersected(area);
            for (int y = visible.top(); y <= visible.bottom(); ++y) {
                QRgb *line = reinterpret_cast<QRgb *>(out.scanLine(y - area.top()));
                for (int x = visible.left(); x <= visible.right(); ++x)
                    line[x - area.left()] = color;
            }
        }
    return out;
}

// One box-blur pass along a line of `len` samples (edges clamped), O(len).
void boxLine(const float *in, float *out, int len, int radius, int stride) {
    const double inv = 1.0 / (2 * radius + 1);
    double sum = 0;
    for (int d = -radius; d <= radius; ++d)
        sum += in[size_t(std::clamp(d, 0, len - 1)) * stride];
    for (int k = 0; k < len; ++k) {
        out[size_t(k) * stride] = float(sum * inv);
        sum += in[size_t(std::min(k + radius + 1, len - 1)) * stride] - in[size_t(std::max(k - radius, 0)) * stride];
    }
}

// Three box passes on each axis of `src`; the pixels of `want` (relative to src) are returned.
// Pixels nearer than 3 * radius to an edge of `src` feel its clamping.
QImage blurred(const QImage &src, int radius, const QRect &want, quint64 seed, const QPoint &srcOrigin) {
    const int w = src.width(), h = src.height();
    std::vector<float> a(size_t(w) * h * 3), b(a.size());
    for (int y = 0; y < h; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const size_t i = (size_t(y) * w + x) * 3;
            a[i] = qRed(line[x]), a[i + 1] = qGreen(line[x]), a[i + 2] = qBlue(line[x]);
        }
    }
    for (int round = 0; round < 3; ++round) {
        for (int y = 0; y < h; ++y)
            for (int c = 0; c < 3; ++c)
                boxLine(&a[size_t(y) * w * 3 + c], &b[size_t(y) * w * 3 + c], w, radius, 3);
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c)
                boxLine(&b[size_t(x) * 3 + c], &a[size_t(x) * 3 + c], h, radius, w * 3);
    }
    QImage out(want.size(), QImage::Format_RGB32);
    for (int y = 0; y < want.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < want.width(); ++x) {
            const int sx = want.left() + x, sy = want.top() + y;
            const size_t i = (size_t(sy) * w + sx) * 3;
            // Noise from the position in the PICTURE, so any area agrees with the whole.
            const int d = int(mix(seed, quint64(srcOrigin.x() + sx), quint64(srcOrigin.y() + sy)) % (2 * kBlurNoise + 1)) - kBlurNoise;
            line[x] = qRgb(clampChannel(qRound(a[i]) + d), clampChannel(qRound(a[i + 1]) + d), clampChannel(qRound(a[i + 2]) + d));
        }
    }
    return out;
}

struct CacheEntry {
    qint64 key;
    QSize size;
    QString style;
    int strength;
    quint64 seed;
    std::shared_ptr<const QImage> image;
};
QMutex g_mutex;
std::deque<CacheEntry> g_cache; // newest first

} // namespace

int blockSize(int strength) { return kBlock[std::clamp(strength, 1, kStrengths) - 1]; }
int blurRadius(int strength) { return kBlurRadius[std::clamp(strength, 1, kStrengths) - 1]; }

QImage render(const QImage &shot, const QString &style, int strength, quint64 seed, const QRect &wanted) {
    const QRect area = wanted.intersected(shot.rect());
    if (area.isEmpty())
        return {};
    ++g_renders;
    if (style == QLatin1String("blur")) {
        const int r = blurRadius(strength);
        const QRect source = area.adjusted(-3 * r, -3 * r, 3 * r, 3 * r).intersected(shot.rect());
        const QImage part = shot.copy(source).convertToFormat(QImage::Format_RGB32);
        return blurred(part, r, area.translated(-source.topLeft()), seed, source.topLeft());
    }
    return mosaic(shot.format() == QImage::Format_RGB32 ? shot : shot.convertToFormat(QImage::Format_RGB32), blockSize(strength), seed, area);
}

std::shared_ptr<const QImage> wholePicture(const QImage &shot, const QString &style, int strength, quint64 seed) {
    const qint64 key = shot.cacheKey();
    {
        QMutexLocker lock(&g_mutex);
        for (const CacheEntry &e : g_cache)
            if (e.key == key && e.size == shot.size() && e.style == style && e.strength == strength && e.seed == seed)
                return e.image;
    }
    auto image = std::make_shared<const QImage>(render(shot, style, strength, seed, shot.rect()));
    QMutexLocker lock(&g_mutex);
    g_cache.push_front({key, shot.size(), style, strength, seed, image});
    while (g_cache.size() > kCacheEntries)
        g_cache.pop_back();
    return image;
}

quint64 renderCount() { return g_renders.load(); }

} // namespace tarn::pixelate
