// Crop mode of the Editor session. The Crop itself is a document-level
// transform (document/crop.h): this file only edits it. While cropping, the
// canvas shows the WHOLE canvas (turned and flipped like the result), and the
// user drags the Crop on that, so every rectangle here is "oriented": in the
// pixels of that view. Conversion to the canvas rectangle the document stores is
// in applyOrientedCrop (exact: turns are by 90 degrees).
#include "editor.h"
#include "editor_session.h"
#include "handles.h"

#include <QLineF>
#include <QtMath>
#include <algorithm>
#include <numeric>

namespace tarn {

namespace {
constexpr qreal kHandleReach = 10; // view px
constexpr qreal kSnapReach = 8;    // view px: an edge within this of the shot's edge snaps to it
const QStringList kPresets = {"Freeform", "Original", "1:1", "4:3", "3:2", "16:9", "3:4", "2:3", "9:16"};
} // namespace

QStringList EditorSession::cropPresets() const { return kPresets; }

QRect EditorSession::orientedCanvas() const { return QRect(QPoint(0, 0), m_document->displaySize(false)); }

QRect EditorSession::orientedCrop() const {
    return m_document->canvasToDisplay(false).mapRect(QRectF(m_document->cropRect())).toRect();
}

int EditorSession::cropWidth() const { return orientedCrop().width(); }
int EditorSession::cropHeight() const { return orientedCrop().height(); }

// ── Entering, leaving, applying ─────────────────────────────────────────────────────

void EditorSession::cropEdited() {
    clampPan();
    refreshHover();
    emit viewChanged();
    emit documentChanged();
    emit chromeChanged();
    emit cropChanged();
}

void EditorSession::enterCrop() {
    if (!m_open || m_cropping || m_mode != Mode::None)
        return;
    cancelEyedropper();
    setSelection({});
    m_cropping = true;
    m_aspect = QStringLiteral("Freeform");
    m_cropDrag = CropDrag::None;
    m_cropHover = -1;
    cropEdited();
}

void EditorSession::leaveCrop() {
    if (!m_cropping)
        return;
    m_cropping = false;
    m_cropDrag = CropDrag::None;
    cropEdited();
}

void EditorSession::applyCrop(const Crop &crop, const QString &text) {
    if (m_document->setCrop(crop, text))
        cropEdited();
}

void EditorSession::applyOrientedCrop(const QRect &oriented, bool record) {
    const QRect canvas = orientedCanvas();
    QRect r = oriented.normalized().intersected(canvas);
    if (r.isEmpty())
        r = QRect(canvas.topLeft(), QSize(1, 1));
    const QRect inCanvas = m_document->canvasToDisplay(false).inverted().mapRect(QRectF(r)).toRect();
    const Crop c = m_document->crop().withRect(inCanvas);
    if (record) {
        applyCrop(c, QStringLiteral("Crop"));
    } else {
        m_document->setCropLive(c);
        cropEdited();
    }
}

void EditorSession::ratioOf(const QString &name, int &w, int &h) const {
    w = h = 0;
    if (name == QLatin1String("Original")) {
        const QSize s = orientedCanvas().size();
        const int g = std::gcd(s.width(), s.height());
        w = s.width() / g;
        h = s.height() / g;
    } else if (name != QLatin1String("Freeform")) {
        const QStringList parts = name.split(':');
        if (parts.size() == 2) {
            w = parts[0].toInt();
            h = parts[1].toInt();
        }
        if (w <= 0 || h <= 0)
            w = h = 0;
    }
}

// ── The fields, presets, turns and scale ────────────────────────────────────────────

void EditorSession::setCropAspect(const QString &name) {
    if (!m_cropping)
        return;
    int a, b;
    ratioOf(name, a, b);
    m_aspect = a > 0 ? name : QStringLiteral("Freeform");
    if (a <= 0) {
        emit cropChanged();
        return;
    }
    // The largest rectangle of this ratio inside the current Crop, on whole numbers
    // (so 16:9 gives exactly 16k x 9k), centered in it.
    const QRect r = orientedCrop();
    const int k = std::min(r.width() / a, r.height() / b);
    int w, h;
    if (k >= 1) {
        w = a * k;
        h = b * k;
    } else { // narrower than one unit of the ratio: the nearest we can do
        const qreal f = std::min(qreal(r.width()) / a, qreal(r.height()) / b);
        w = std::max(1, qRound(a * f));
        h = std::max(1, qRound(b * f));
    }
    applyOrientedCrop(QRect(r.x() + (r.width() - w) / 2, r.y() + (r.height() - h) / 2, w, h), true);
    emit cropChanged();
}

void EditorSession::setCropWidth(int width) {
    if (!m_cropping)
        return;
    const QRect r = orientedCrop(), c = orientedCanvas();
    int a, b;
    ratioOf(m_aspect, a, b);
    int w = std::clamp(width, 1, c.width()), h = r.height();
    if (a > 0) {
        h = std::max(1, qRound(qreal(w) * b / a));
        if (h > c.height()) {
            h = c.height();
            w = std::max(1, qRound(qreal(h) * a / b));
        }
    }
    applyOrientedCrop(QRect(std::clamp(r.x(), 0, c.width() - w), std::clamp(r.y(), 0, c.height() - h), w, h), true);
}

void EditorSession::setCropHeight(int height) {
    if (!m_cropping)
        return;
    const QRect r = orientedCrop(), c = orientedCanvas();
    int a, b;
    ratioOf(m_aspect, a, b);
    int h = std::clamp(height, 1, c.height()), w = r.width();
    if (a > 0) {
        w = std::max(1, qRound(qreal(h) * a / b));
        if (w > c.width()) {
            w = c.width();
            h = std::max(1, qRound(qreal(w) * b / a));
        }
    }
    applyOrientedCrop(QRect(std::clamp(r.x(), 0, c.width() - w), std::clamp(r.y(), 0, c.height() - h), w, h), true);
}

void EditorSession::rotateCrop(int direction) {
    if (!m_cropping || direction == 0)
        return;
    int a, b;
    ratioOf(m_aspect, a, b);
    if (a > 0 && m_aspect != QLatin1String("Original")) // a turned picture swaps the ratio too
        m_aspect = QStringLiteral("%1:%2").arg(b).arg(a);
    applyCrop(m_document->crop().rotated(direction > 0 ? 1 : -1), QStringLiteral("Rotate"));
}

void EditorSession::flipCropH() {
    if (m_cropping)
        applyCrop(m_document->crop().flippedH(), QStringLiteral("Flip"));
}

void EditorSession::flipCropV() {
    if (m_cropping)
        applyCrop(m_document->crop().flippedV(), QStringLiteral("Flip"));
}

void EditorSession::setOutputScale(qreal scale) { applyCrop(m_document->crop().withScale(scale), QStringLiteral("Output size")); }

void EditorSession::resetCrop() {
    m_aspect = QStringLiteral("Freeform");
    applyCrop(Crop{}, QStringLiteral("Reset crop"));
    emit cropChanged();
}

// ── Dragging the Crop ───────────────────────────────────────────────────────────────

int EditorSession::cropHandleAt(const QPointF &o) const {
    const QRectF r(orientedCrop());
    const qreal reach = kHandleReach / zoom();
    for (int h = 0; h < Handles::kBoxCount; ++h)
        if (QLineF(o, Handles::boxPoint(r, h)).length() <= reach)
            return h;
    return -1;
}

QRectF EditorSession::draggedCrop(const QPointF &to) const {
    const QRectF cv(orientedCanvas());
    const qreal snap = kSnapReach / zoom();
    auto snapTo = [&](qreal v, qreal lo, qreal hi) { return std::abs(v - lo) <= snap ? lo : std::abs(v - hi) <= snap ? hi : v; };

    if (m_cropDrag == CropDrag::Move) {
        const QRectF r = m_cropFrame.translated(to - m_cropFrom);
        qreal x = std::clamp(r.x(), cv.left(), cv.right() - r.width());
        qreal y = std::clamp(r.y(), cv.top(), cv.bottom() - r.height());
        // Either edge of the Crop sticks to the matching edge of the shot.
        if (std::abs(x - cv.left()) <= snap)
            x = cv.left();
        else if (std::abs(x + r.width() - cv.right()) <= snap)
            x = cv.right() - r.width();
        if (std::abs(y - cv.top()) <= snap)
            y = cv.top();
        else if (std::abs(y + r.height() - cv.bottom()) <= snap)
            y = cv.bottom() - r.height();
        return QRectF(x, y, r.width(), r.height());
    }

    const int h = m_cropHandle;
    QPointF p(std::clamp(to.x(), cv.left(), cv.right()), std::clamp(to.y(), cv.top(), cv.bottom()));
    const bool movesX = h <= 3 || h == 5 || h == 7, movesY = h <= 3 || h == 4 || h == 6;
    if (movesX)
        p.setX(snapTo(p.x(), cv.left(), cv.right()));
    if (movesY)
        p.setY(snapTo(p.y(), cv.top(), cv.bottom()));

    int a, b;
    ratioOf(m_aspect, a, b);
    if (a <= 0)
        return Handles::resizedFrame(m_cropFrame, h, p);

    const qreal ratio = qreal(a) / b;
    const QRectF f = m_cropFrame;
    if (h <= 3) { // a corner: the opposite corner stays, the ratio decides the rest
        const QPointF o = Handles::boxPoint(f, (h + 2) % 4);
        const qreal sx = p.x() < o.x() ? -1 : 1, sy = p.y() < o.y() ? -1 : 1;
        const qreal maxW = sx > 0 ? cv.right() - o.x() : o.x() - cv.left();
        const qreal maxH = sy > 0 ? cv.bottom() - o.y() : o.y() - cv.top();
        const qreal w = std::min({std::max(std::abs(p.x() - o.x()), std::abs(p.y() - o.y()) * ratio), maxW, maxH * ratio});
        return QRectF(o, QPointF(o.x() + sx * w, o.y() + sy * w / ratio)).normalized();
    }
    // An edge: the opposite edge stays, the other side grows about the middle.
    if (h == 5 || h == 7) {
        const qreal fixedX = h == 5 ? f.left() : f.right();
        const qreal maxW = h == 5 ? cv.right() - fixedX : fixedX - cv.left();
        const qreal cy = f.center().y();
        const qreal maxHalf = std::min(cy - cv.top(), cv.bottom() - cy);
        qreal w = std::clamp(std::abs(p.x() - fixedX), 1.0, maxW);
        w = std::min(w, 2 * maxHalf * ratio);
        return QRectF(h == 5 ? fixedX : fixedX - w, cy - w / ratio / 2, w, w / ratio);
    }
    const qreal fixedY = h == 6 ? f.top() : f.bottom();
    const qreal maxH = h == 6 ? cv.bottom() - fixedY : fixedY - cv.top();
    const qreal cx = f.center().x();
    const qreal maxHalf = std::min(cx - cv.left(), cv.right() - cx);
    qreal ht = std::clamp(std::abs(p.y() - fixedY), 1.0, maxH);
    ht = std::min(ht, 2 * maxHalf / ratio);
    return QRectF(cx - ht * ratio / 2, h == 6 ? fixedY : fixedY - ht, ht * ratio, ht);
}

void EditorSession::cropPressed(qreal x, qreal y, int) {
    const QPointF o = m_document->canvasToDisplay(false).map(QPointF(x, y));
    m_cropBefore = m_document->crop();
    m_cropFrame = QRectF(orientedCrop());
    m_cropFrom = o;
    m_cropMoved = false;
    const int h = cropHandleAt(o);
    if (h >= 0) {
        m_cropDrag = CropDrag::Handle;
        m_cropHandle = h;
    } else if (m_cropFrame.contains(o)) {
        m_cropDrag = CropDrag::Move;
    } else { // a new rectangle from this point
        const QRectF cv(orientedCanvas());
        const QPointF anchor(std::clamp(o.x(), cv.left(), cv.right()), std::clamp(o.y(), cv.top(), cv.bottom()));
        m_cropFrame = QRectF(anchor, QSizeF(0, 0));
        m_cropFrom = anchor;
        m_cropDrag = CropDrag::Draw;
        m_cropHandle = 2;
    }
    emit chromeChanged();
}

void EditorSession::cropMoved(qreal x, qreal y, int) {
    if (m_cropDrag == CropDrag::None)
        return;
    const QRectF r = draggedCrop(m_document->canvasToDisplay(false).map(QPointF(x, y)));
    const QRect rounded(qRound(r.x()), qRound(r.y()), std::max(1, qRound(r.width())), std::max(1, qRound(r.height())));
    if (!m_cropMoved && rounded == orientedCrop())
        return;
    m_cropMoved = true;
    applyOrientedCrop(rounded, false);
}

void EditorSession::cropReleased(qreal x, qreal y, int modifiers) {
    if (m_cropDrag == CropDrag::None)
        return;
    cropMoved(x, y, modifiers); // the release point counts
    m_cropDrag = CropDrag::None;
    if (m_cropMoved)
        m_document->recordCropChange(QStringLiteral("Crop"), m_cropBefore);
    cropEdited();
}

void EditorSession::cropHovered(qreal x, qreal y) {
    const QPointF o = m_document->canvasToDisplay(false).map(QPointF(x, y));
    const int h = cropHandleAt(o);
    const int now = h >= 0 ? h : QRectF(orientedCrop()).contains(o) ? -2 : -1; // -2: inside, -1: outside
    if (now == m_cropHover)
        return;
    m_cropHover = now;
    emit chromeChanged();
    emit cropChanged();
}

int EditorSession::cropCursor() const {
    const int h = m_cropDrag == CropDrag::Handle ? m_cropHandle : m_cropDrag == CropDrag::None ? m_cropHover : -1;
    if (h >= 0)
        return Handles::boxCursor(h);
    if (m_cropDrag == CropDrag::Move || (m_cropDrag == CropDrag::None && h == -2))
        return Qt::SizeAllCursor;
    return Qt::CrossCursor;
}

QVariantList EditorSession::cropChrome() const {
    QVariantList out;
    if (!m_cropping)
        return out;
    auto rect = [&](const QString &type, const QRectF &displayRect) {
        const QRectF v = QRectF(displayToView(displayRect.topLeft()), displayToView(displayRect.bottomRight())).normalized();
        out << QVariantMap{{"type", type}, {"x", v.x()}, {"y", v.y()}, {"w", v.width()}, {"h", v.height()}};
    };
    const QRectF cv(orientedCanvas()), r(orientedCrop());
    // Everything outside the Crop is dimmed: four bars round it.
    rect("dim", QRectF(cv.left(), cv.top(), cv.width(), r.top() - cv.top()));
    rect("dim", QRectF(cv.left(), r.bottom(), cv.width(), cv.bottom() - r.bottom()));
    rect("dim", QRectF(cv.left(), r.top(), r.left() - cv.left(), r.height()));
    rect("dim", QRectF(r.right(), r.top(), cv.right() - r.right(), r.height()));
    rect("frame", r);
    const int hot = m_cropDrag == CropDrag::Handle ? m_cropHandle : m_cropHover;
    for (int h = 0; h < Handles::kBoxCount; ++h) {
        const QPointF v = displayToView(Handles::boxPoint(r, h));
        out << QVariantMap{{"type", "dot"}, {"x", v.x()}, {"y", v.y()}, {"hot", h == hot}, {"handle", h}};
    }
    return out;
}

} // namespace tarn
