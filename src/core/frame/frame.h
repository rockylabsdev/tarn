#pragma once
// The Frame: the look placed round a shot for sharing (GLOSSARY: Frame). A
// Background plus Padding, Inset, Auto-balance, Shadow, Corners, Alignment and
// Ratio. It is a plain value; CaptureDocument owns one and records every change
// in its undo history. Presets store and apply whole Frames.
//
// Padding, Inset, Shadow and Corners are FRACTIONS of the shot's shorter side,
// so the same Frame looks the same on any capture. What "the shot" is = the
// content the Frame wraps: the (cropped, grown) picture with its Annotations.
//
//   Padding   the margin between the card and the edge of the framed image
//   Inset     a band inside the card, round the shot, in the shot's own edge
//             colors (Auto-balance: each side takes its own edge's color,
//             off: one average color all round)
//   Shadow    blur of the card's shadow
//   Corners   radius of the card's corners
//   Alignment where the card sits in the padded image (-1, 0, 1): the aligned side
//             keeps no padding and the opposite side gets double; Ratio room is shared too
//   Ratio     "Auto", or "w:h" (the framed image grows to it, never crops)
//   Watermark an image or a line of text in a corner of the shot (watermark.h)
#include "background.h"
#include "watermark.h"

#include <QPainterPath>
#include <QRect>
#include <QSize>
#include <QVariantMap>

namespace tarn {

struct Frame {
    // Slider ranges (fractions of the shot's shorter side).
    static constexpr qreal kMaxPadding = 1.0; // about a shot of background on each side
    static constexpr qreal kMaxInset = 0.10;
    static constexpr qreal kMaxShadow = 0.08;
    static constexpr qreal kMaxCorners = 0.15;

    Background background;     // kind "none" = no Frame at all
    qreal padding = 0.08;
    qreal inset = 0;
    bool autoBalance = false;
    qreal shadow = 0.03;
    qreal corners = 0.02;
    int alignH = 0;            // -1 left, 0 center, 1 right
    int alignV = 0;            // -1 top, 0 center, 1 bottom
    QString ratio = QStringLiteral("Auto");
    Watermark watermark;       // drawn last, with or without a Background (kind "none")

    bool active() const { return background.kind != QLatin1String("none"); }
    // The width / height the framed image is grown to, or 0 for Auto (or a bad value).
    qreal ratioValue() const;
    bool operator==(const Frame &) const = default;

    // For QML and the Presets file: plain values (colors as "#aarrggbb").
    QVariantMap toMap() const;
    static Frame fromMap(const QVariantMap &map);
};

// Where everything sits in the framed image, in its pixels.
struct FrameLayout {
    QSize size;          // the framed image
    QRect card;          // the shadowed rounded block (shot + inset band)
    QRect content;       // the shot, inside the card (no Frame: the whole image)
    qreal cardRadius = 0;
    qreal contentRadius = 0;
    qreal shadowBlur = 0; // px
    bool active = false;
};

FrameLayout layoutFrame(const QSize &content, const Frame &frame);

// Paints everything behind the content: the Background, the card's shadow and
// the card (with its Inset band). `shot` is the picture, for Background kinds
// made from it. The caller then paints the content through contentClip().
void paintFrameBehind(QPainter &painter, const FrameLayout &layout, const Frame &frame, const QImage &shot);

// The clip the content is painted through (the shot's rounded corners).
QPainterPath contentClip(const FrameLayout &layout);

// True when the framed image has no transparent pixel.
bool frameIsOpaque(const Frame &frame);

} // namespace tarn
