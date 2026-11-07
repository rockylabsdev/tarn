#pragma once
// The Watermark (GLOSSARY: Watermark): a small image or one line of text that the
// Frame places in a corner of the shot. It is part of the Frame value, so Presets,
// the Project file, B and undo treat it like the rest of the Frame.
//
//   Size     3-25% of the shot's shorter side: an image's longer side, or the text's
//            line (the font is 70% of it). Default 8%.
//   Opacity  10-100%. Default 60%.
//   Margin   3% of the shot's shorter side from the corner, measured on the shot
//            itself, so inside any Inset band. Works with Background None too.
//
// `image` is the picture as loaded (ARGB32). The Presets file keeps only `path`
// and the image is read from it each time a Preset is applied; the Project file
// keeps the pixels as PNG data, so it survives the file being deleted.
#include <QByteArray>
#include <QImage>
#include <QPainter>
#include <QRect>
#include <QString>
#include <QVariantMap>

namespace tarn {

struct Watermark {
    enum Mode { Off, Image, Text };
    enum Corner { TopLeft, TopRight, BottomLeft, BottomRight };

    static constexpr qreal kMinSize = 0.03;
    static constexpr qreal kMaxSize = 0.25;
    static constexpr qreal kMinOpacity = 0.10;
    static constexpr qreal kMargin = 0.03;
    static constexpr qreal kLightLuminance = 0.6; // above this under a text Watermark: near-black text

    Mode mode = Off;
    QString path;           // Image: the file it came from
    QImage image;           // Image: the picture itself
    QString text;           // Text: one line
    int corner = BottomRight;
    qreal size = 0.08;
    qreal opacity = 0.6;

    bool on() const { return mode != Off; }
    bool operator==(const Watermark &) const = default;

    // Reads `path` into `image`. False (and nothing changed) if it is not a readable picture.
    bool loadImage(const QString &path);

    // Plain values for QML and the Presets file (no pixels: `hasImage` says whether there are any).
    QVariantMap toMap() const;
    // `png` (base64) in the map restores the pixels. Values are clamped to their ranges.
    static Watermark fromMap(const QVariantMap &map);
    // The "last Watermark" as Remembered state keeps it: toMap() without `hasImage`.
    // Read it back with fromMap; an Image has no pixels then, so loadImage(path) again.
    QVariantMap rememberedMap() const;
    // The picture as PNG bytes, and back.
    QByteArray imagePng() const;

    static QString modeName(Mode mode);
    static QString cornerName(int corner);
    static int cornerFromName(const QString &name, int fallback = BottomRight);
};

// Where `mark` sits inside `content` (the shot, in the painter's pixels); empty when nothing
// would be drawn (Off, an empty text, a missing picture).
QRect watermarkRect(const Watermark &mark, const QRect &content);

// True when the average Rec. 709 luminance of `shot` inside `area` (shot pixels) is above
// Watermark::kLightLuminance. Sampled with a stride; the part of `area` outside the shot is ignored.
bool isLightUnder(const QImage &shot, const QRect &area);

// Draws `mark` in the corner of `content` (the shot, in the painter's pixels), clipped
// to `clip`. Nothing for Off, an empty text, or a missing picture. A text Watermark is white
// with a dark shadow, or near-black with a light shadow when `lightUnder` (an image ignores it).
void paintWatermark(QPainter &painter, const Watermark &mark, const QRect &content, const QPainterPath &clip, bool lightUnder = false);

} // namespace tarn
