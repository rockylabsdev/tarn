#pragma once
// The Capture document: the shot and its Annotations, flattened to an image for
// Copy, Save and the Thumbnail. It lives in memory; project_file.h saves and loads the edits.
// Every change to it is recorded in history(): unlimited undo and redo.
// Later tickets grow it in place: Crop, Frame, added images; each
// records its own steps in the same history.
//
// One rendering path: paint() draws the picture and every Annotation. The Editor
// canvas calls it with the zoom as a transform, flatten() with none.
#include "annotation.h"
#include "crop.h"
#include "frame/frame.h"
#include "undo_history.h"

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QList>
#include <QSize>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace tarn {

class CaptureDocument {
public:
    struct Item {
        int id;
        std::unique_ptr<Annotation> annotation;
    };

    explicit CaptureDocument(QImage shot) : m_shot(std::move(shot)) {}
    CaptureDocument(const CaptureDocument &) = delete;
    CaptureDocument &operator=(const CaptureDocument &) = delete;

    // Fills a freshly made document from a Project file (project_file.h): these
    // Annotations with these ids (so Counters keep their order), Crop and Frame,
    // and an empty undo history. Only for a document that has nothing in it yet.
    void restoreFrom(std::vector<Item> items, int nextId, const Crop &crop, const Frame &frame);

    // An independent copy (Annotations cloned): an Editor session works on one
    // so that Esc can drop its edits.
    std::unique_ptr<CaptureDocument> clone() const;

    const QImage &shot() const { return m_shot; }
    // The size of the finished image, in pixels, as Copy and Save get it: the content, plus the
    // Frame when there is one, times the Crop's output scale (which covers the Frame too).
    QSize size() const;
    // The finished image before the output scale: what the Editor shows. In Crop mode
    // (`cropped = false`) the whole canvas, turned and flipped, with no Frame.
    QSize viewSize(bool cropped = true) const;

    // ONE geometry, in this order: Annotations and the shot live in CANVAS space
    // (canvas(): the shot, grown to hold every added image that reaches past it, so it may
    // start at a negative x or y; worked out from the Annotations, so moving the image back
    // or undoing shrinks it again) -> Crop (cut, mirror, turn: canvasToDisplay) -> CONTENT
    // (displaySize) -> Frame wraps the content (contentOffset) -> VIEW px -> output scale.
    QRect canvas() const;
    QRect canvasRect() const { return canvas(); }
    QSize contentSize() const { return displaySize(); }
    // Where the content sits in the view (0,0 without a Frame, and in Crop mode).
    QPoint contentOffset(bool cropped = true) const { return cropped ? frameLayout().content.topLeft() : QPoint(); }

    // ---- Crop: a document-level transform, nothing is destroyed. ----
    // The Crop cuts the canvas, turns, flips and scales it; see crop.h. Annotations
    // outside the Crop stay in the document and are only not painted.
    const Crop &crop() const { return m_crop; }
    QRect cropRect() const { return m_crop.resolved(canvasRect()); }
    // The content (cut, turned and flipped, not scaled). `cropped = false`
    // is the whole canvas turned and flipped the same way (Crop mode).
    QSize displaySize(bool cropped = true) const { return m_crop.displaySize(canvasRect(), cropped); }
    QTransform canvasToDisplay(bool cropped = true) const { return m_crop.toDisplay(canvasRect(), cropped); }
    // Sets the Crop (clamped to the canvas) and records ONE undo step. False if nothing changed.
    bool setCrop(const Crop &crop, const QString &text = QStringLiteral("Crop"));
    // A drag in progress: applied, not recorded; recordCropChange() then records the whole drag.
    void setCropLive(const Crop &crop) { m_crop = normalized(crop); }
    void recordCropChange(const QString &text, const Crop &before);
    // Anything to export besides the plain shot (Annotations, a Crop or a Frame).
    bool isEdited() const { return !m_items.empty() || !m_crop.isIdentity() || m_frame.active() || m_frame.watermark.on(); }

    // The Frame. setFrame applies and records one undo step. A drag of a
    // slider uses setFrameLive while it moves (not recorded) and recordFrameChange
    // with the Frame from before the drag when it ends.
    const Frame &frame() const { return m_frame; }
    FrameLayout frameLayout() const { return layoutFrame(contentSize(), m_frame); }
    void setFrame(const Frame &frame, const QString &text = QStringLiteral("Frame"));
    void setFrameLive(const Frame &frame) { m_frame = frame; }
    void recordFrameChange(const QString &text, const Frame &before);

    // The undo history. Annotation edits below record into it themselves; Crop
    // and Frame record their own steps with history().record(...).
    UndoHistory &history() { return m_history; }
    const UndoHistory &history() const { return m_history; }

    // Adds an Annotation on top (one undo step); returns its id.
    int add(std::unique_ptr<Annotation> annotation);
    // Adds several on top, in order, as one undo step (paste, duplicate).
    QList<int> addMany(std::vector<std::unique_ptr<Annotation>> annotations, const QString &text = QStringLiteral("Add"));
    // Deletes these Annotations (one undo step). Unknown ids are ignored.
    void remove(const QList<int> &ids);
    // Moves each of these one place forward (toward the top) or backward in the
    // stacking order, keeping their order among themselves (one undo step).
    // Returns false when nothing changed.
    bool reorder(const QList<int> &ids, bool forward);
    // Stacking order, bottom first.
    QList<int> order() const;
    // A drag with the named tool. Returns the new id, or -1 for an unknown tool
    // or a drag too small to be anything.
    int draw(const QString &kind, const DragGesture &gesture);
    int count() const { return int(m_items.size()); }
    const std::vector<Item> &items() const;
    const Annotation *annotation(int id) const;
    // The id the next added Annotation gets (ids only grow).
    int nextId() const { return m_nextId; }
    // Recolors one Annotation; false for an unknown id.
    bool recolor(int id, const QColor &color);
    // Restyles one Annotation after a tool option changed (see Annotation::restyle);
    // true if it changed. Not recorded: the caller wraps it in snapshot/recordChange.
    bool restyle(int id, const QString &what, const ToolStyle &style);

    // Crossfade on the CANVAS only (never in an export): for `kFadeMs` the canvas
    // paints `from` (the look the Annotation had) and the Annotation's current
    // look over it, fading in. Used when a Pixelate changes style.
    static constexpr int kFadeMs = 150;
    void beginFade(int id, std::unique_ptr<Annotation> from);
    // True while a fade is still running (the canvas repaints until it ends).
    bool fading() const;
    // Tests drive the fade clock (milliseconds); the default is the steady clock.
    void setFadeClock(std::function<qint64()> now) { m_fadeNow = std::move(now); }

    // Editing in place. An interactive change (a drag) takes a snapshot first,
    // replaces Annotations as the pointer moves with setLive (not recorded), and
    // records the whole thing as ONE step with recordChange when it ends.
    using Snapshot = std::vector<std::pair<int, std::shared_ptr<Annotation>>>;
    Snapshot snapshot(const QList<int> &ids) const;
    void setLive(int id, std::unique_ptr<Annotation> annotation);
    void recordChange(const QString &text, const Snapshot &before);
    // A change made in one go: runs `edit` on each, and records it.
    void modify(const QString &text, const QList<int> &ids, const std::function<void(Annotation &)> &edit);

    // Paints the finished image onto `painter` in VIEW pixels (the caller sets scale and
    // offset): the Frame, then the picture and the Annotations through the Crop and the
    // Frame's content clip. `cropped = false` paints the whole canvas (Crop mode, no
    // Frame). `draft` is an Annotation still being drawn. The Frame is outside everything
    // an Annotation can touch, so a dim layer (Spotlight) covers the content, never the Frame.
    void paint(QPainter &painter, const Annotation *draft = nullptr, bool cropped = true) const;
    // The area the Spotlights' dim layer covers, in canvas pixels: the whole canvas (shot
    // plus any grown area), further cut by the Crop's clip, never the Frame.
    QRectF dimArea() const;
    static constexpr int kDimAlpha = 140; // black at 55%
    // The part of the canvas that shows in the finished image, in canvas pixels: the Crop's
    // rect, or with a Frame the whole framed image (Annotations may sit on the Frame).
    // Annotations wholly outside it are hidden; they stay in the document.
    QRectF visibleCanvas(bool cropped = true) const;
    // Just the picture and Annotations in content pixels (no Frame).
    void paintContent(QPainter &painter, const Annotation *draft = nullptr) const;
    // The finished image at full size (output scale included), with no outline or
    // handles. Each call draws Pixelate's noise afresh.
    QImage flatten() const;
    QByteArray toPng() const;
    // How many pre-shrunk copies of the shot (1/2, 1/4, 1/8) have been made so far. The canvas
    // draws from them when zoomed out (box-filtered, so 1 px strokes survive); exports never do.
    int shrunkCopiesBuilt() const { return m_shrunkBuilt; }

private:
    // The smallest copy still at least `scale` of full size (level 1..3 = 1/2^level), or null
    // when the shot should be drawn as is. Built lazily; dropped when the shot's pixels change.
    const QImage *shrunkFor(qreal scale, int &level) const;
    enum Layers { All, ContentOnly, MarksOnly }; // ContentOnly = picture, Pixelate, dim; MarksOnly = the rest
    void paintLayers(QPainter &painter, const Annotation *draft, const quint64 *exportSeed, bool cropped, Layers layers) const;
    void paintFinished(QPainter &painter, const Annotation *draft, const quint64 *exportSeed, bool cropped) const;
    bool watermarkOnLight(const QRect &content) const;
    Crop normalized(Crop crop) const;
    int indexOf(int id) const;
    void relink() const; // hands every follower (Callout) the Annotation it hangs on
    QHash<int, int> ordinals() const; // id -> place in its numbered run
    void insertAt(int index, Item item);
    void applyOrder(const QList<int> &order);
    void restore(const Snapshot &snapshot);

    QImage m_shot;
    mutable QImage m_shrunk[3];
    mutable qint64 m_shrunkKey = 0; // m_shot.cacheKey() the copies were made from
    mutable int m_shrunkBuilt = 0;
    Frame m_frame;
    std::vector<Item> m_items;
    Crop m_crop;
    UndoHistory m_history;
    int m_nextId = 1;
    struct Fade {
        std::shared_ptr<Annotation> from;
        qint64 start;
    };
    qreal fadeProgress(const Fade &fade) const; // 0..1
    mutable QHash<int, Fade> m_fades;
    std::function<qint64()> m_fadeNow;
};

} // namespace tarn
