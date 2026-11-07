#pragma once
// The Crop of a Capture document: a document-level transform from the
// Annotations' own space ("canvas space": the shot, and later whatever an added
// image grows it to) to the finished image. It destroys nothing: the shot and
// every Annotation stay in the document, the Crop only says which part of the
// canvas is shown and how it is turned and scaled.
//
// The pipeline, in order:
//   1. cut to `rect` (canvas px; a null rect is the whole canvas),
//   2. mirror left-right if `mirrored`, then
//   3. turn clockwise by `quarterTurns`   -> the DISPLAY: what the Editor shows,
//   4. scale by `scale`                   -> the OUTPUT: what Copy, Save and Pin get.
//
// The user's "flip horizontally / vertically" and "rotate" act on what they SEE,
// so they are operations on this pair (see Crop::rotated, flippedH, flippedV),
// not separate flags. Frame and an added image that grows the canvas
// plug in at the ends: the canvas rect they pass to the functions below is the
// only thing that changes for them.
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QTransform>

namespace tarn {

struct Crop {
    QRect rect;            // in canvas px; null = the whole canvas
    int quarterTurns = 0;  // clockwise, 0..3
    bool mirrored = false; // left-right, applied before the turn
    qreal scale = 1.0;     // output scale

    static constexpr qreal kMinScale = 0.1;
    static constexpr qreal kMaxScale = 8.0;

    bool operator==(const Crop &o) const {
        return rect == o.rect && quarterTurns == o.quarterTurns && mirrored == o.mirrored && qFuzzyCompare(scale, o.scale);
    }
    bool operator!=(const Crop &o) const { return !(*this == o); }
    // No cut, no turn, no flip, no scale: the finished image is the canvas.
    bool isIdentity() const { return rect.isNull() && quarterTurns == 0 && !mirrored && qFuzzyCompare(scale, 1.0); }

    // The part of `canvas` that is shown (the whole canvas when `rect` is null; never empty).
    QRect resolved(const QRect &canvas) const;
    // Canvas px -> display px (no scale). `cropped = false` shows the whole canvas
    // turned and flipped the same way (Crop mode edits the rect on that).
    QTransform toDisplay(const QRect &canvas, bool cropped = true) const;
    QSize displaySize(const QRect &canvas, bool cropped = true) const;
    // Canvas px -> output px.
    QTransform toOutput(const QRect &canvas) const;
    QSize outputSize(const QRect &canvas) const;

    // What the user's buttons do, as new Crops (rotate acts on what is shown).
    Crop rotated(int turns) const; // +1 clockwise, -1 counter-clockwise
    Crop flippedH() const;         // left-right on screen
    Crop flippedV() const;         // top-bottom on screen
    Crop withScale(qreal s) const;
    Crop withRect(const QRect &r) const;
};

} // namespace tarn
