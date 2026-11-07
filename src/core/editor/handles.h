#pragma once
// Handle geometry shared by the Editor session and its tests: where the eight
// Handles of a box sit, what a Handle drag does to the box, and which pointer
// belongs to a Handle. Handle numbers are the Overlay's: 0-3 the corners
// clockwise from the top-left, then the midpoints of the top, right, bottom and
// left edges.
#include <QPointF>
#include <QRectF>

namespace tarn {

namespace Handles {

constexpr int kBoxCount = 8;
// Sizes in screen pixels (spec "Annotation Handles"): the Overlay's white dot
// with a 2 px accent rim, growing under the pointer; the outline is 3 px, accent at 50%.
constexpr qreal kDot = 14;
constexpr qreal kDotHot = 18;
constexpr qreal kRim = 2;
constexpr qreal kOutlineWidth = 3;
constexpr qreal kOutlineOut = 4; // outline of Text, Counter, Pencil, Highlighter
constexpr qreal kGhostOpacity = 0.4;

QPointF boxPoint(const QRectF &frame, int handle);
// The box after dragging `handle` to `to` (flipping past the opposite edge).
// `keepAspect` (Shift on a corner) keeps the original proportions.
QRectF resizedFrame(const QRectF &frame, int handle, const QPointF &to, bool keepAspect = false);
// Where `p` goes when `from` is stretched into `to`.
QPointF mapPoint(const QPointF &p, const QRectF &from, const QRectF &to);
// A Qt::CursorShape (as int) for the resize pointer of a box Handle.
int boxCursor(int handle);

} // namespace Handles

} // namespace tarn
