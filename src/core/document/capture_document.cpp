#include <cmath>
#include "capture_document.h"

#include <QBuffer>
#include <QDateTime>
#include <QRandomGenerator>
#include <algorithm>

namespace tarn {

std::unique_ptr<CaptureDocument> CaptureDocument::clone() const {
    auto copy = std::make_unique<CaptureDocument>(m_shot);
    copy->m_frame = m_frame;
    for (const auto &item : m_items)
        copy->m_items.push_back({item.id, item.annotation->clone()});
    copy->m_nextId = m_nextId;
    copy->m_crop = m_crop;
    return copy;
}

void CaptureDocument::restoreFrom(std::vector<Item> items, int nextId, const Crop &crop, const Frame &frame) {
    m_items = std::move(items);
    m_nextId = nextId;
    m_frame = frame;
    m_crop = normalized(crop);
    m_history = UndoHistory();
}

int CaptureDocument::indexOf(int id) const {
    for (int i = 0; i < int(m_items.size()); ++i)
        if (m_items[i].id == id)
            return i;
    return -1;
}

void CaptureDocument::insertAt(int index, Item item) { m_items.insert(m_items.begin() + std::min<int>(index, int(m_items.size())), std::move(item)); }

int CaptureDocument::add(std::unique_ptr<Annotation> annotation) {
    std::vector<std::unique_ptr<Annotation>> one;
    one.push_back(std::move(annotation));
    return addMany(std::move(one)).value(0, -1);
}

QList<int> CaptureDocument::addMany(std::vector<std::unique_ptr<Annotation>> annotations, const QString &text) {
    // The added Annotations live in shared holders so undo and redo can pass them back and forth.
    auto added = std::make_shared<std::vector<std::shared_ptr<Annotation>>>();
    QList<int> ids;
    for (auto &a : annotations) {
        ids << m_nextId;
        added->push_back(std::shared_ptr<Annotation>(std::move(a)));
        m_items.push_back({m_nextId++, added->back()->clone()});
    }
    if (ids.isEmpty())
        return ids;
    m_history.record(
        text,
        [this, ids] {
            for (int id : ids)
                m_items.erase(m_items.begin() + indexOf(id));
        },
        [this, ids, added] {
            for (int i = 0; i < ids.size(); ++i)
                m_items.push_back({ids[i], (*added)[i]->clone()});
        });
    return ids;
}

void CaptureDocument::remove(const QList<int> &requested) {
    // Deleting an Annotation deletes the ones hanging on it (a Counter's Callout).
    QList<int> ids = requested;
    for (const auto &item : m_items)
        if (item.annotation->anchorId() != 0 && requested.contains(item.annotation->anchorId()) && !ids.contains(item.id))
            ids << item.id;
    // Remembers where each one sat so undo puts it back in the same place.
    auto removed = std::make_shared<std::vector<std::pair<int, std::shared_ptr<Annotation>>>>(); // index, annotation
    std::vector<int> removedIds;
    for (int i = 0; i < int(m_items.size()); ++i)
        if (ids.contains(m_items[i].id)) {
            removed->push_back({i, std::shared_ptr<Annotation>(m_items[i].annotation->clone())});
            removedIds.push_back(m_items[i].id);
        }
    if (removed->empty())
        return;
    for (int i = int(removed->size()) - 1; i >= 0; --i)
        m_items.erase(m_items.begin() + (*removed)[i].first);
    m_history.record(
        QStringLiteral("Delete"),
        [this, removed, removedIds] {
            for (size_t k = 0; k < removed->size(); ++k) // ascending: each index is right once the earlier ones are back
                insertAt((*removed)[k].first, {removedIds[k], (*removed)[k].second->clone()});
        },
        [this, removedIds] {
            for (int id : removedIds)
                m_items.erase(m_items.begin() + indexOf(id));
        });
}

QList<int> CaptureDocument::order() const {
    QList<int> out;
    for (const auto &item : m_items)
        out << item.id;
    return out;
}

void CaptureDocument::applyOrder(const QList<int> &order) {
    std::vector<Item> sorted;
    for (int id : order) {
        const int i = indexOf(id);
        if (i >= 0)
            sorted.push_back(std::move(m_items[i]));
    }
    m_items = std::move(sorted);
}

bool CaptureDocument::reorder(const QList<int> &ids, bool forward) {
    const QList<int> before = order();
    QList<int> after = before;
    const int n = after.size();
    if (forward) {
        for (int i = n - 2; i >= 0; --i)
            if (ids.contains(after[i]) && !ids.contains(after[i + 1]))
                after.swapItemsAt(i, i + 1);
    } else {
        for (int i = 1; i < n; ++i)
            if (ids.contains(after[i]) && !ids.contains(after[i - 1]))
                after.swapItemsAt(i, i - 1);
    }
    if (after == before)
        return false;
    applyOrder(after);
    m_history.record(forward ? QStringLiteral("Bring forward") : QStringLiteral("Send backward"), [this, before] { applyOrder(before); },
                     [this, after] { applyOrder(after); });
    return true;
}

CaptureDocument::Snapshot CaptureDocument::snapshot(const QList<int> &ids) const {
    Snapshot out;
    for (const auto &item : m_items)
        if (ids.contains(item.id))
            out.push_back({item.id, std::shared_ptr<Annotation>(item.annotation->clone())});
    return out;
}

void CaptureDocument::setLive(int id, std::unique_ptr<Annotation> annotation) {
    const int i = indexOf(id);
    if (i >= 0)
        m_items[i].annotation = std::move(annotation);
}

void CaptureDocument::restore(const Snapshot &snap) {
    for (const auto &[id, annotation] : snap)
        setLive(id, annotation->clone());
}

void CaptureDocument::recordChange(const QString &text, const Snapshot &before) {
    QList<int> ids;
    for (const auto &entry : before)
        ids << entry.first;
    const Snapshot after = snapshot(ids);
    m_history.record(text, [this, before] { restore(before); }, [this, after] { restore(after); });
}

void CaptureDocument::modify(const QString &text, const QList<int> &ids, const std::function<void(Annotation &)> &edit) {
    const Snapshot before = snapshot(ids);
    if (before.empty())
        return;
    for (const auto &entry : before)
        edit(*m_items[indexOf(entry.first)].annotation);
    recordChange(text, before);
}

int CaptureDocument::draw(const QString &kind, const DragGesture &gesture) {
    const AnnotationKind *k = AnnotationKinds::find(kind);
    if (!k || !k->fromDrag)
        return -1;
    auto made = k->fromDrag(gesture);
    return made ? add(std::move(made)) : -1;
}

void CaptureDocument::relink() const {
    for (const auto &item : m_items) {
        const int anchor = item.annotation->anchorId();
        if (anchor == 0)
            continue;
        const int i = indexOf(anchor);
        item.annotation->anchorTo(i >= 0 ? m_items[i].annotation.get() : nullptr);
    }
}

const std::vector<CaptureDocument::Item> &CaptureDocument::items() const {
    relink();
    return m_items;
}

const Annotation *CaptureDocument::annotation(int id) const {
    relink();
    for (const auto &item : m_items)
        if (item.id == id)
            return item.annotation.get();
    return nullptr;
}

bool CaptureDocument::recolor(int id, const QColor &color) {
    for (auto &item : m_items)
        if (item.id == id) {
            item.annotation->setColor(color);
            return true;
        }
    return false;
}

bool CaptureDocument::restyle(int id, const QString &what, const ToolStyle &style) {
    const int i = indexOf(id);
    return i >= 0 && m_items[i].annotation->restyle(what, style);
}

void CaptureDocument::beginFade(int id, std::unique_ptr<Annotation> from) {
    if (!from)
        return;
    const qint64 now = m_fadeNow ? m_fadeNow() : QDateTime::currentMSecsSinceEpoch();
    m_fades.insert(id, {std::shared_ptr<Annotation>(std::move(from)), now});
}

qreal CaptureDocument::fadeProgress(const Fade &fade) const {
    const qint64 now = m_fadeNow ? m_fadeNow() : QDateTime::currentMSecsSinceEpoch();
    return std::clamp(qreal(now - fade.start) / kFadeMs, 0.0, 1.0);
}

bool CaptureDocument::fading() const {
    for (auto it = m_fades.begin(); it != m_fades.end();) {
        if (fadeProgress(it.value()) >= 1.0)
            it = m_fades.erase(it);
        else
            ++it;
    }
    return !m_fades.isEmpty();
}

void CaptureDocument::setFrame(const Frame &frame, const QString &text) {
    if (frame == m_frame)
        return;
    const Frame before = m_frame;
    m_frame = frame;
    recordFrameChange(text, before);
}

void CaptureDocument::recordFrameChange(const QString &text, const Frame &before) {
    const Frame after = m_frame;
    if (before == after)
        return;
    m_history.record(
        text, [this, before] { m_frame = before; }, [this, after] { m_frame = after; });
}

QHash<int, int> CaptureDocument::ordinals() const {
    // Ids only grow, so id order is placement order, whatever the stacking order is now.
    std::vector<const Item *> byId;
    for (const auto &item : m_items)
        if (!item.annotation->series().isEmpty())
            byId.push_back(&item);
    std::sort(byId.begin(), byId.end(), [](const Item *a, const Item *b) { return a->id < b->id; });
    QHash<QString, int> seen;
    QHash<int, int> out;
    for (const Item *item : byId)
        out.insert(item->id, seen[item->annotation->series()]++);
    return out;
}

QRect CaptureDocument::canvas() const {
    QRect rect(QPoint(0, 0), m_shot.size());
    for (const auto &item : m_items)
        if (item.annotation->growsCanvas())
            rect = rect.united(item.annotation->bounds().toAlignedRect());
    return rect;
}

QRectF CaptureDocument::dimArea() const { return QRectF(canvas()); } // the content: shot plus grown canvas, never a Frame

Crop CaptureDocument::normalized(Crop crop) const {
    crop.quarterTurns = ((crop.quarterTurns % 4) + 4) % 4;
    crop.scale = std::clamp(crop.scale, Crop::kMinScale, Crop::kMaxScale);
    if (!crop.rect.isNull()) {
        crop.rect = crop.rect.normalized().intersected(canvasRect());
        if (crop.rect.isEmpty() || crop.rect == canvasRect())
            crop.rect = QRect(); // nothing left to cut: the whole canvas
    }
    return crop;
}

bool CaptureDocument::setCrop(const Crop &crop, const QString &text) {
    const Crop before = m_crop, after = normalized(crop);
    if (after == before)
        return false;
    m_crop = after;
    m_history.record(text, [this, before] { m_crop = before; }, [this, after] { m_crop = after; });
    return true;
}

void CaptureDocument::recordCropChange(const QString &text, const Crop &before) {
    const Crop after = m_crop;
    if (after == before)
        return;
    m_history.record(text, [this, before] { m_crop = before; }, [this, after] { m_crop = after; });
}

const QImage *CaptureDocument::shrunkFor(qreal scale, int &level) const {
    level = 0;
    while (level < 3 && scale <= 1.0 / (2 << level))
        ++level;
    if (level == 0)
        return nullptr;
    if (m_shrunkKey != m_shot.cacheKey()) {
        for (QImage &c : m_shrunk)
            c = QImage();
        m_shrunkKey = m_shot.cacheKey();
    }
    for (int l = 0; l < level; ++l) {
        if (!m_shrunk[l].isNull())
            continue;
        const QImage &from = l == 0 ? m_shot : m_shrunk[l - 1];
        // Qt's smooth downscale averages the source pixels each output pixel covers.
        m_shrunk[l] = from.scaled(QSize(qMax(1, (from.width() + 1) / 2), qMax(1, (from.height() + 1) / 2)), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        ++m_shrunkBuilt;
    }
    return &m_shrunk[level - 1];
}

void CaptureDocument::paintLayers(QPainter &painter, const Annotation *draft, const quint64 *exportSeed, bool cropped, Layers layers) const {
    relink();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setWorldTransform(canvasToDisplay(cropped), true);
    // The cut; also where the canvas ends (inside any Frame clip). Marks over a Frame are the
    // exception: the caller clips them to the whole framed image instead.
    if (layers != MarksOnly)
        painter.setClipRect(cropped ? cropRect() : canvasRect(), Qt::IntersectClip);
    // The picture itself is copied pixel for pixel (a turn or flip must not blend neighbors):
    // smooth sampling stays off here unless the output is scaled. Antialiasing stays on, so a
    // Frame's rounded clip is not jagged.
    if (layers != MarksOnly) {
        const bool smooth = painter.testRenderHint(QPainter::SmoothPixmapTransform);
        painter.save();
        painter.setRenderHint(QPainter::SmoothPixmapTransform, m_crop.scale != 1.0 && smooth);
        // Zoomed out on the canvas (never an export): draw from the area-averaged copy, since
        // bilinear below 0.5x skips source pixels and breaks thin strokes.
        int level = 0;
        const qreal scale = std::sqrt(qAbs(painter.combinedTransform().determinant()));
        const QImage *small = !exportSeed && smooth && !m_shot.isNull() ? shrunkFor(scale, level) : nullptr;
        if (small) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true); // the last step, 0.5x to 1x
            painter.scale(qreal(1 << level), qreal(1 << level));
            painter.drawImage(QPointF(0, 0), *small);
        } else {
            painter.drawImage(QPointF(0, 0), m_shot);
        }
        painter.restore();
    }
    // One Annotation's paint: an export gets its own seed (so Pixelate's noise is fresh each export).
    const QHash<int, int> ordinal = ordinals();
    auto paintOne = [&](const Annotation &a, int index) {
        if (!a.series().isEmpty())
            a.paintNumbered(painter, m_shot, ordinal.value(m_items[index].id, 0));
        else if (exportSeed)
            a.paintFlat(painter, m_shot, *exportSeed + quint64(index) * 0x9E3779B97F4A7C15ull);
        else if (const auto fade = m_fades.constFind(m_items[index].id); fade != m_fades.constEnd() && fadeProgress(*fade) < 1.0) {
            // Canvas only: the old look, then the new one fading in over it.
            fade->from->paint(painter, m_shot);
            painter.save();
            painter.setOpacity(painter.opacity() * fadeProgress(*fade));
            a.paint(painter, m_shot);
            painter.restore();
        } else
            a.paint(painter, m_shot);
    };
    if (layers != MarksOnly) {
    for (int i = 0; i < int(m_items.size()); ++i)
        if (m_items[i].annotation->pass() == Annotation::Picture)
            paintOne(*m_items[i].annotation, i);
    if (draft && draft->pass() == Annotation::Picture)
        draft->paint(painter, m_shot);

    // The shared dim layer: every Spotlight's cutout is merged first and the rest
    // of the shot is darkened ONCE, so overlapping Spotlights never darken each
    // other. It covers dimArea() (the canvas: shot plus any grown area), so a Frame around it is
    // not dimmed.
    QPainterPath cut;
    bool anyDim = false;
    for (const auto &item : m_items)
        if (item.annotation->pass() == Annotation::Dim) {
            cut = cut.united(item.annotation->cutout(m_shot.size()));
            anyDim = true;
        }
    if (draft && draft->pass() == Annotation::Dim) {
        cut = cut.united(draft->cutout(m_shot.size()));
        anyDim = true;
    }
    if (anyDim) {
        QPainterPath dim;
        dim.addRect(dimArea());
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, kDimAlpha));
        painter.drawPath(dim.subtracted(cut));
    }
    }
    if (layers == ContentOnly) {
        painter.restore();
        return;
    }

    for (int i = 0; i < int(m_items.size()); ++i)
        if (m_items[i].annotation->pass() == Annotation::Marks)
            paintOne(*m_items[i].annotation, i);
    if (draft && draft->pass() == Annotation::Marks) {
        // A draft numbered Annotation shows the number it would get.
        int next = 0;
        if (!draft->series().isEmpty())
            for (const auto &item : m_items)
                if (item.annotation->series() == draft->series())
                    ++next;
        draft->paintNumbered(painter, m_shot, next);
    }
    painter.restore();
}

void CaptureDocument::paintContent(QPainter &painter, const Annotation *draft) const { paintLayers(painter, draft, nullptr, true, All); }

QRectF CaptureDocument::visibleCanvas(bool cropped) const {
    if (!cropped)
        return QRectF(canvasRect());
    if (!m_frame.active())
        return QRectF(cropRect());
    const FrameLayout layout = frameLayout();
    const QRectF view(QPointF(0, 0), QSizeF(layout.size));
    return canvasToDisplay(true).inverted().mapRect(view.translated(-QPointF(layout.content.topLeft())));
}

// Whether the shot (through the Crop; Annotations and the Frame do not count) is light under a text Watermark.
bool CaptureDocument::watermarkOnLight(const QRect &content) const {
    if (m_frame.watermark.mode != Watermark::Text)
        return false;
    const QRect rect = watermarkRect(m_frame.watermark, content);
    if (rect.isEmpty())
        return false;
    const QRect onCanvas = canvasToDisplay(true).inverted().mapRect(QRectF(rect.translated(-content.topLeft()))).toAlignedRect();
    return isLightUnder(m_shot, onCanvas);
}

// The finished image in view pixels (no output scale): the Frame, then the content (the
// canvas through the Crop) clipped to the Frame's rounded rectangle. `cropped = false`
// is Crop mode: the whole canvas, turned and flipped, with no Frame.
void CaptureDocument::paintFinished(QPainter &painter, const Annotation *draft, const quint64 *exportSeed, bool cropped) const {
    if (!cropped || !m_frame.active()) {
        paintLayers(painter, draft, exportSeed, cropped, All);
        if (cropped && m_frame.watermark.on()) { // no Frame: the shot is the whole image
            const FrameLayout layout = frameLayout();
            paintWatermark(painter, m_frame.watermark, layout.content, contentClip(layout), watermarkOnLight(layout.content));
        }
        return;
    }
    const FrameLayout layout = frameLayout();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    paintFrameBehind(painter, layout, m_frame, m_shot);
    // The picture, Pixelate and the Spotlight dim stay on the shot (clipped to its rounded
    // corners); every other Annotation is painted over the Frame, clipped only to the
    // finished image, so what falls outside it is hidden and comes back with the Frame.
    painter.save();
    painter.setClipPath(contentClip(layout), Qt::IntersectClip);
    painter.translate(layout.content.topLeft());
    paintLayers(painter, draft, exportSeed, true, ContentOnly);
    painter.restore();
    painter.setClipRect(QRect(QPoint(0, 0), layout.size), Qt::IntersectClip);
    painter.translate(layout.content.topLeft());
    paintLayers(painter, draft, exportSeed, true, MarksOnly);
    painter.restore();
    // The Watermark goes on last: above every Annotation and the dim, clipped to the shot's corners.
    paintWatermark(painter, m_frame.watermark, layout.content, contentClip(layout), watermarkOnLight(layout.content));
}

void CaptureDocument::paint(QPainter &painter, const Annotation *draft, bool cropped) const { paintFinished(painter, draft, nullptr, cropped); }

QSize CaptureDocument::viewSize(bool cropped) const { return cropped ? frameLayout().size : displaySize(false); }

QSize CaptureDocument::size() const {
    const QSize v = viewSize();
    const qreal k = std::clamp(m_crop.scale, Crop::kMinScale, Crop::kMaxScale);
    return {std::max(1, int(std::lround(v.width() * k))), std::max(1, int(std::lround(v.height() * k)))};
}

QImage CaptureDocument::flatten() const {
    const bool framed = m_frame.active();
    QImage out(size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    {
        QPainter painter(&out);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, !qFuzzyCompare(m_crop.scale, 1.0));
        // The output scale applies to the whole finished image, Frame included.
        const QSize v = viewSize(), o = out.size();
        painter.scale(qreal(o.width()) / v.width(), qreal(o.height()) / v.height());
        const quint64 seed = QRandomGenerator::system()->generate64();
        paintFinished(painter, nullptr, &seed, true);
    }
    if (framed)
        return frameIsOpaque(m_frame) ? out.convertToFormat(QImage::Format_RGB32) : out.convertToFormat(QImage::Format_ARGB32);
    return m_shot.hasAlphaChannel() || canvas() != m_shot.rect() ? out.convertToFormat(QImage::Format_ARGB32) : out.convertToFormat(QImage::Format_RGB32);
}

QByteArray CaptureDocument::toPng() const {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    flatten().save(&buffer, "PNG");
    return bytes;
}

} // namespace tarn
