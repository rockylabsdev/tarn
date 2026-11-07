#include "frame.h"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <vector>

namespace tarn {

// ── The value ───────────────────────────────────────────────────────────────────────

qreal Frame::ratioValue() const {
    const int colon = ratio.indexOf(QLatin1Char(':'));
    if (colon < 0)
        return 0;
    bool okW = false, okH = false;
    const qreal w = ratio.left(colon).trimmed().toDouble(&okW);
    const qreal h = ratio.mid(colon + 1).trimmed().toDouble(&okH);
    return okW && okH && w > 0 && h > 0 ? w / h : 0;
}

QVariantMap Frame::toMap() const {
    QVariantMap bg = background.params;
    bg.insert(QStringLiteral("kind"), background.kind);
    const BackgroundKind *k = BackgroundKinds::find(background.kind);
    return {
        {QStringLiteral("missing"), k && k->missing && k->missing(background.params)}, // a deleted wallpaper file
        {QStringLiteral("on"), active()},
        {QStringLiteral("background"), bg},
        {QStringLiteral("kind"), background.kind},
        {QStringLiteral("color"), background.params.value(QStringLiteral("color"))},
        {QStringLiteral("slot"), background.params.value(QStringLiteral("slot"))},
        {QStringLiteral("padding"), padding},
        {QStringLiteral("inset"), inset},
        {QStringLiteral("autoBalance"), autoBalance},
        {QStringLiteral("shadow"), shadow},
        {QStringLiteral("corners"), corners},
        {QStringLiteral("ah"), alignH},
        {QStringLiteral("av"), alignV},
        {QStringLiteral("ratio"), ratio},
        {QStringLiteral("watermark"), watermark.toMap()},
    };
}

namespace {
// Project files and presets.toml written before Tarn spelled everything American call the
// color of a Plain background, and of a gradient stop written as a table, "colour". Every
// Frame read from disk comes through fromMap, so this is the only place that knows the
// old name, and a file saved long ago still opens with its colors.
QVariantMap withAmericanKeys(QVariantMap params) {
    const QString oldKey = QStringLiteral("colour"), key = QStringLiteral("color");
    if (params.contains(oldKey)) {
        const QVariant old = params.take(oldKey);
        if (!params.contains(key))
            params.insert(key, old);
    }
    const QString stopsKey = QStringLiteral("stops");
    QVariantList stops = params.value(stopsKey).toList();
    for (QVariant &stop : stops) {
        if (stop.typeId() != QMetaType::QVariantMap)
            continue;
        QVariantMap s = stop.toMap();
        if (s.contains(oldKey)) {
            const QVariant old = s.take(oldKey);
            if (!s.contains(key))
                s.insert(key, old);
            stop = s;
        }
    }
    if (params.contains(stopsKey))
        params.insert(stopsKey, stops);
    return params;
}
} // namespace

Frame Frame::fromMap(const QVariantMap &m) {
    Frame f;
    const QVariantMap bg = withAmericanKeys(m.value(QStringLiteral("background")).toMap());
    f.background.kind = bg.value(QStringLiteral("kind"), QStringLiteral("none")).toString();
    f.background.params = bg;
    f.background.params.remove(QStringLiteral("kind"));
    auto number = [&](const char *key, qreal fallback, qreal hi) {
        return std::clamp(m.value(QLatin1String(key), fallback).toDouble(), 0.0, hi);
    };
    f.padding = number("padding", f.padding, Frame::kMaxPadding);
    f.inset = number("inset", f.inset, Frame::kMaxInset);
    f.shadow = number("shadow", f.shadow, Frame::kMaxShadow);
    f.corners = number("corners", f.corners, Frame::kMaxCorners);
    f.autoBalance = m.value(QStringLiteral("autoBalance")).toBool();
    f.alignH = std::clamp(m.value(QStringLiteral("ah")).toInt(), -1, 1);
    f.alignV = std::clamp(m.value(QStringLiteral("av")).toInt(), -1, 1);
    f.ratio = m.value(QStringLiteral("ratio"), QStringLiteral("Auto")).toString();
    f.watermark = Watermark::fromMap(m.value(QStringLiteral("watermark")).toMap());
    return f;
}

bool frameIsOpaque(const Frame &frame) {
    if (!frame.active())
        return false;
    const BackgroundKind *k = BackgroundKinds::find(frame.background.kind);
    return k && k->opaque && k->opaque(frame.background.params);
}

// ── Layout ──────────────────────────────────────────────────────────────────────────

FrameLayout layoutFrame(const QSize &content, const Frame &frame) {
    FrameLayout L;
    if (!frame.active()) {
        L.size = content;
        L.content = L.card = QRect(QPoint(0, 0), content);
        return L;
    }
    L.active = true;
    const qreal s = std::min(content.width(), content.height()); // "the shot's size"
    const int pad = int(std::lround(frame.padding * s));
    const int ins = int(std::lround(frame.inset * s));
    const QSize cardSize(content.width() + 2 * ins, content.height() + 2 * ins);
    QSize out(cardSize.width() + 2 * pad, cardSize.height() + 2 * pad);
    if (const qreal r = frame.ratioValue(); r > 0) {
        if (qreal(out.width()) / out.height() < r)
            out.setWidth(int(std::ceil(out.height() * r)));
        else
            out.setHeight(int(std::ceil(out.width() / r)));
    }
    const int spareX = out.width() - cardSize.width() - 2 * pad;
    const int spareY = out.height() - cardSize.height() - 2 * pad;
    // Alignment moves the card toward its edge: the aligned side keeps no padding and
    // the opposite side gets double (the padded size is the same), then any room a
    // Ratio added is shared the same way.
    const QPoint at(int(std::lround(pad * (1 + frame.alignH) + spareX * (frame.alignH + 1) / 2.0)),
                    int(std::lround(pad * (1 + frame.alignV) + spareY * (frame.alignV + 1) / 2.0)));
    L.size = out;
    L.card = QRect(at, cardSize);
    L.content = QRect(at + QPoint(ins, ins), content);
    L.cardRadius = std::min(frame.corners * s, std::min(cardSize.width(), cardSize.height()) / 2.0);
    L.contentRadius = std::max(0.0, L.cardRadius - ins);
    L.shadowBlur = frame.shadow * s;
    return L;
}

QPainterPath contentClip(const FrameLayout &L) {
    QPainterPath path;
    path.addRoundedRect(QRectF(L.content), L.contentRadius, L.contentRadius);
    return path;
}

// ── Painting behind the content ─────────────────────────────────────────────────────

namespace {

// One pass of a box blur over a w x h plane, radius r (outside reads as 0).
void boxBlur(std::vector<float> &plane, int w, int h, int r, bool horizontal) {
    const int lines = horizontal ? h : w;
    const int length = horizontal ? w : h;
    const int stride = horizontal ? 1 : w;
    const int lineStep = horizontal ? w : 1;
    std::vector<float> out(plane.size(), 0.0f);
    const float norm = 1.0f / float(2 * r + 1);
    for (int l = 0; l < lines; ++l) {
        const float *in = plane.data() + l * lineStep;
        float *o = out.data() + l * lineStep;
        float sum = 0;
        for (int i = 0; i <= std::min(r, length - 1); ++i)
            sum += in[i * stride];
        for (int i = 0; i < length; ++i) {
            o[i * stride] = sum * norm;
            const int add = i + r + 1, drop = i - r;
            if (add < length)
                sum += in[add * stride];
            if (drop >= 0)
                sum -= in[drop * stride];
        }
    }
    plane.swap(out);
}

struct ShadowGeometry {
    int blur = 0;    // px
    int radius = 0;  // box radius per pass
    int margin = 0;  // the image extends this far past the card on every side
    int offsetY = 0; // light from above
};

ShadowGeometry shadowGeometry(const FrameLayout &L) {
    ShadowGeometry g;
    g.blur = int(std::lround(L.shadowBlur));
    g.radius = std::max(1, int(std::lround(g.blur / 2.0)));
    g.margin = 3 * g.radius + 1;
    g.offsetY = int(std::lround(g.blur * 0.4));
    return g;
}

// The card's shadow as a black premultiplied image the size of the card plus
// the margin. Three box passes approximate a Gaussian blur; integer geometry,
// so reference images compare exactly.
QImage makeShadow(const FrameLayout &L, const ShadowGeometry &g) {
    const QSize size = L.card.size() + QSize(2 * g.margin, 2 * g.margin);
    QImage mask(size, QImage::Format_Alpha8);
    mask.fill(0);
    {
        QPainter p(&mask);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(Qt::white);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(g.margin, g.margin, L.card.width(), L.card.height()), L.cardRadius, L.cardRadius);
    }
    const int w = size.width(), h = size.height();
    std::vector<float> plane(size_t(w) * h);
    for (int y = 0; y < h; ++y) {
        const uchar *line = mask.constScanLine(y);
        for (int x = 0; x < w; ++x)
            plane[size_t(y) * w + x] = line[x] / 255.0f;
    }
    for (int pass = 0; pass < 3; ++pass) {
        boxBlur(plane, w, h, g.radius, true);
        boxBlur(plane, w, h, g.radius, false);
    }
    constexpr float kStrength = 0.5f;
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y) {
        auto *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x)
            line[x] = qRgba(0, 0, 0, int(std::lround(std::clamp(plane[size_t(y) * w + x] * kStrength, 0.0f, 1.0f) * 255)));
    }
    return out;
}

// The shadow is the costly part and the canvas repaints it on every update: keep the last.
const QImage &cachedShadow(const FrameLayout &L, const ShadowGeometry &g) {
    struct Key {
        QSize card;
        int radius100, blur;
        bool operator==(const Key &) const = default;
    };
    thread_local Key key{{}, -1, -1};
    thread_local QImage image;
    const Key now{L.card.size(), int(std::lround(L.cardRadius * 100)), g.blur};
    if (!(now == key)) {
        image = makeShadow(L, g);
        key = now;
    }
    return image;
}

// The mean color of the shot's outer ring along one side (0 top, 1 right, 2 bottom, 3 left), or all four (-1).
QColor edgeColor(const QImage &shot, int side) {
    if (shot.isNull())
        return Qt::transparent;
    const int w = shot.width(), h = shot.height();
    const int depth = std::max(1, std::min(w, h) / 50);
    qint64 r = 0, g = 0, b = 0, n = 0;
    auto take = [&](int x, int y) {
        const QColor c = shot.pixelColor(x, y);
        r += c.red();
        g += c.green();
        b += c.blue();
        ++n;
    };
    for (int d = 0; d < depth; ++d) {
        if (side == 0 || side == -1)
            for (int x = 0; x < w; ++x)
                take(x, d);
        if (side == 2 || side == -1)
            for (int x = 0; x < w; ++x)
                take(x, h - 1 - d);
        if (side == 3 || side == -1)
            for (int y = 0; y < h; ++y)
                take(d, y);
        if (side == 1 || side == -1)
            for (int y = 0; y < h; ++y)
                take(w - 1 - d, y);
    }
    return n ? QColor(int(r / n), int(g / n), int(b / n)) : QColor(Qt::black);
}

} // namespace

void paintFrameBehind(QPainter &painter, const FrameLayout &L, const Frame &frame, const QImage &shot) {
    if (!L.active)
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (const BackgroundKind *kind = BackgroundKinds::find(frame.background.kind); kind && kind->paint)
        kind->paint(painter, QRectF(QPointF(0, 0), QSizeF(L.size)), frame.background.params, shot);

    if (L.shadowBlur >= 0.5) {
        const ShadowGeometry g = shadowGeometry(L);
        painter.drawImage(L.card.topLeft() - QPoint(g.margin, g.margin - g.offsetY), cachedShadow(L, g));
    }

    // The card: the Inset band in the shot's own edge colors, under the shot.
    const int ins = L.content.left() - L.card.left();
    if (ins > 0) {
        QPainterPath cardPath;
        cardPath.addRoundedRect(QRectF(L.card), L.cardRadius, L.cardRadius);
        painter.save();
        painter.setClipPath(cardPath);
        if (!frame.autoBalance) {
            painter.fillPath(cardPath, edgeColor(shot, -1));
        } else {
            const QRectF c(L.card), i(L.content);
            const QPolygonF sides[4] = {
                QPolygonF({c.topLeft(), c.topRight(), i.topRight(), i.topLeft()}),
                QPolygonF({c.topRight(), c.bottomRight(), i.bottomRight(), i.topRight()}),
                QPolygonF({c.bottomRight(), c.bottomLeft(), i.bottomLeft(), i.bottomRight()}),
                QPolygonF({c.bottomLeft(), c.topLeft(), i.topLeft(), i.bottomLeft()}),
            };
            for (int side = 0; side < 4; ++side) {
                const QColor col = edgeColor(shot, side);
                painter.setBrush(col);
                painter.setPen(QPen(col, 1)); // a hairline hides the seam between sides
                painter.drawPolygon(sides[side]);
            }
        }
        painter.restore();
    }
    painter.restore();
}

} // namespace tarn
