#include "editor_session.h"

#include "annotations/callout.h"
#include "annotations/counter.h"
#include "annotations/image.h"
#include "clipboard/clipboard.h"
#include "daemon/daemon.h"
#include "editor.h"
#include "handles.h"
#include "history/history.h"
#include "notify/notifier.h"
#include "opening_size.h"
#include "palette/palette.h"
#include "pin/pins.h"
#include "saved_colors/saved_colors.h"
#include "wallpaper/wallpapers.h"
#include "state/remembered_state.h"
#include "storage/image_file.h"
#include "storage/storage.h"
#include "thumbnails/thumbnail_stack.h"

#include <QFileInfo>
#include <QImageReader>
#include <QLineF>
#include <QUrl>
#include <QtMath>
#include <algorithm>

namespace tarn {

namespace {
constexpr qreal kZoomSteps[] = {0.25, 0.5, 1.0, 2.0, 4.0};
}

EditorSession::EditorSession(Editor &editor, int captureId, std::unique_ptr<CaptureDocument> document, const QSize &openingSize,
                           const QString &historyId)
    : QObject(), m_editor(editor), m_captureId(captureId), m_historyId(historyId), m_document(std::move(document)), m_openingSize(openingSize),
      m_tool(QStringLiteral("rect")), m_options(editor.daemon().service<ToolOptions>()) {
    static int opened = 0;
    m_windowTitle = QStringLiteral("Tarn editor #%1").arg(++opened);
    m_editedBefore = m_editor.stack().edited(captureId);
    connect(&m_options, &ToolOptions::colorChanged, this, &EditorSession::colorChanged);
    connect(&m_options, &ToolOptions::sizeChanged, this, &EditorSession::sizeChanged);
    connect(&m_options, &ToolOptions::choiceChanged, this, &EditorSession::choicesChanged);
    // A tool option changed: the selected Annotations of the current tool follow.
    connect(&m_options, &ToolOptions::sizeChanged, this, [this](const QString &group) { restyleSelection(group); });
    connect(&m_options, &ToolOptions::choiceChanged, this, [this](const QString &name) { restyleSelection(name); });
    connect(&editor.daemon().service<SavedColors>(), &SavedColors::changed, this, &EditorSession::savedColorsChanged);
    connect(&editor.daemon().service<Wallpapers>(), &Wallpapers::changed, this, &EditorSession::backgroundsChanged);
    connect(&editor.daemon().service<Palette>(), &Palette::changed, this, &EditorSession::backgroundsChanged);
    connectText();
    // Back at Fit (or zoomed out until the shot fits) the Hand tool hands over to the tool before it.
    connect(this, &EditorSession::viewChanged, this, [this] {
        if (m_tool == QLatin1String("hand") && !handAvailable())
            setTool(m_toolBeforeHand.isEmpty() ? QStringLiteral("rect") : m_toolBeforeHand);
    });
}

// ── Tools and drawing ───────────────────────────────────────────────────────────────

void EditorSession::setTool(const QString &name) {
    if (m_cropping)
        leaveCrop(); // choosing a tool leaves Crop mode
    const bool hand = name == QLatin1String("hand");
    if (m_tool == name || (hand && !handAvailable()) || (name != QLatin1String("select") && !hand && !AnnotationKinds::find(name)))
        return;
    finishText(); // a Text being typed is finished by leaving the tool
    if (hand)
        m_toolBeforeHand = m_tool;
    m_tool = name;
    emit toolChanged();
    emit sizeChanged(); // the size select follows the tool
    emit choicesChanged();
}

QVariantList EditorSession::tools() const {
    QVariantList list;
    list << QVariantMap{{"name", "select"}, {"label", "Select"}, {"icon", "pointer"}, {"key", "V"}};
    list << QVariantMap{{"name", "hand"}, {"label", "Hand"}, {"icon", "hand-stop"}, {"key", "G"}}; // next to Select, as in Figma
    for (const auto &k : AnnotationKinds::all())
        list << QVariantMap{{"name", k.name}, {"label", k.label}, {"icon", k.icon}, {"key", k.key}};
    return list;
}

bool EditorSession::handAvailable() const { return m_open && zoom() > fitZoom() * 1.001; }

DragGesture EditorSession::gesture(qreal x, qreal y, int modifiers) const {
    return {m_dragFrom, QPointF(x, y), Qt::KeyboardModifiers(modifiers), style(), m_path};
}

QString EditorSession::arrowStyle() const { return style().arrowStyle; }

void EditorSession::setArrowStyle(const QString &name) {
    if (name == arrowStyle() || (name != QLatin1String("tapered") && name != QLatin1String("open") && name != QLatin1String("double")))
        return;
    m_options.setStyleValue(QStringLiteral("arrow"), name); // remembered
    emit arrowStyleChanged();
    restyleSelection(QStringLiteral("arrow_style"));
}

// A tool option changed (size, a choice, arrow style, counter style or start): every
// selected Annotation of the CURRENT tool takes the new value, as one undo step.
// Pixelate fades from its old look to the new one on the canvas (not in the export).
void EditorSession::restyleSelection(const QString &what) {
    if (m_selection.isEmpty() || m_mode != Mode::None)
        return;
    QList<int> ids;
    for (int id : m_selection)
        if (const Annotation *a = m_document->annotation(id))
            if (a->kind() == m_tool)
                ids << id;
    if (ids.isEmpty())
        return;
    const ToolStyle st = style();
    const CaptureDocument::Snapshot before = m_document->snapshot(ids);
    QList<int> changedIds;
    for (const auto &entry : before) {
        if (m_document->restyle(entry.first, what, st))
            changedIds << entry.first;
    }
    if (changedIds.isEmpty())
        return;
    for (const auto &entry : before)
        if (changedIds.contains(entry.first) && entry.second->kind() == QLatin1String("pixelate"))
            m_document->beginFade(entry.first, entry.second->clone());
    m_document->recordChange(QStringLiteral("Change style"), before);
    changed();
}

// ── Color and size ─────────────────────────────────────────────────────────────────

ToolStyle EditorSession::style() const { return m_options.style(); }

QColor EditorSession::color() const { return m_options.color(); }

void EditorSession::setColor(const QColor &color) {
    if (!color.isValid())
        return;
    m_options.setColor(color);
    recolor(m_selection, color);
}

void EditorSession::dragColor(const QColor &color) {
    if (!color.isValid())
        return;
    m_options.setColor(color);
    QList<int> ids = m_selection;
    for (int id : m_selection)
        if (const auto *callout = dynamic_cast<const CalloutAnnotation *>(m_document->annotation(id)); callout && !ids.contains(callout->counterId()))
            ids << callout->counterId();
    if (ids.isEmpty())
        return;
    CaptureDocument::Snapshot before = m_document->snapshot(ids);
    int changed = 0;
    for (const auto &entry : before)
        if (m_document->recolor(entry.first, color))
            ++changed;
    if (!changed)
        return;
    if (!m_colorBeforeDrag)
        m_colorBeforeDrag = std::move(before);
    this->changed();
}

void EditorSession::endColorDrag() {
    if (!m_colorBeforeDrag)
        return;
    const CaptureDocument::Snapshot before = std::move(*m_colorBeforeDrag);
    m_colorBeforeDrag.reset();
    m_document->recordChange(QStringLiteral("Recolor"), before);
    changed();
}

QVariantList EditorSession::swatches() const {
    QVariantList list;
    for (const QColor &c : ToolOptions::fixedSwatches())
        list << c.name();
    return list;
}

QVariantList EditorSession::savedColors() const {
    QVariantList list;
    for (const QColor &c : m_editor.daemon().service<SavedColors>().colors())
        list << c.name(QColor::HexArgb);
    return list;
}

bool EditorSession::frameIsPlain() const {
    const Frame &f = m_document->frame();
    return f.active() && f.background.kind == QLatin1String("plain");
}

void EditorSession::saveCurrentColor() { m_editor.daemon().service<SavedColors>().save(color()); }

void EditorSession::saveFrameColor() {
    if (frameIsPlain())
        m_editor.daemon().service<SavedColors>().save(QColor(m_document->frame().background.params.value(QStringLiteral("color")).toString()));
}

void EditorSession::removeSavedColor(int index) { m_editor.daemon().service<SavedColors>().remove(index); }

int EditorSession::recolor(const QList<int> &requested, const QColor &color) {
    // A Callout has no color of its own: recoloring it recolors its Counter (and so it).
    QList<int> ids = requested;
    for (int id : requested)
        if (const auto *callout = dynamic_cast<const CalloutAnnotation *>(m_document->annotation(id)); callout && !ids.contains(callout->counterId()))
            ids << callout->counterId();
    const CaptureDocument::Snapshot before = m_document->snapshot(ids);
    int changed = 0;
    for (const auto &entry : before)
        if (m_document->recolor(entry.first, color))
            ++changed;
    if (changed) { // one undo step, whatever the color picker sent
        m_document->recordChange(QStringLiteral("Recolor"), before);
        this->changed();
    }
    return changed;
}

void EditorSession::beginEyedropper() {
    if (!m_open || m_eyedropper)
        return;
    m_mode = Mode::None;
    m_draft.reset();
    m_eyedropper = true;
    emit eyedropperChanged();
}

void EditorSession::cancelEyedropper() {
    if (!m_eyedropper)
        return;
    m_eyedropper = false;
    emit eyedropperChanged();
}

QColor EditorSession::sampleShot(qreal x, qreal y) const {
    const QImage &shot = m_document->shot();
    const int px = qFloor(x), py = qFloor(y);
    if (px < 0 || py < 0 || px >= shot.width() || py >= shot.height())
        return {};
    return shot.pixelColor(px, py);
}

QString EditorSession::sizeGroup() const {
    const AnnotationKind *k = AnnotationKinds::find(m_tool);
    return k ? k->sizeGroup : QString();
}

QVariantList EditorSession::sizeSteps() const {
    QVariantList list;
    if (const auto *g = ToolOptions::group(sizeGroup()))
        for (qreal v : g->steps)
            list << v;
    return list;
}

int EditorSession::sizeIndex() const { return m_options.stepIndex(sizeGroup()); }

void EditorSession::setSizeIndex(int index) {
    if (!sizeGroup().isEmpty())
        m_options.setStepIndex(sizeGroup(), index);
}

qreal EditorSession::size() const { return m_options.size(sizeGroup()); }

QString EditorSession::sizeLabel() const {
    const auto *g = ToolOptions::group(sizeGroup());
    return g ? QString::number(size()) + QLatin1Char(' ') + g->unit : QString();
}

void EditorSession::stepSize(int delta) {
    if (!sizeGroup().isEmpty())
        m_options.step(sizeGroup(), delta);
}

// ── Counter ─────────────────────────────────────────────────────────────────────────

int EditorSession::counterStyle() const { return style().counterStyle; }

void EditorSession::setCounterStyle(int style) {
    style = std::clamp(style, 0, 2);
    if (style == counterStyle())
        return;
    m_options.setStyleValue(QStringLiteral("counter_style"), style);
    emit counterChanged();
    restyleSelection(QStringLiteral("counter_style"));
}

int EditorSession::counterStart() const { return style().counterStart; }

void EditorSession::setCounterStart(int start) {
    start = std::clamp(start, 0, 999);
    if (start == counterStart())
        return;
    m_options.setStyleValue(QStringLiteral("counter_start"), start);
    emit counterChanged();
    restyleSelection(QStringLiteral("counter_start"));
}

QString EditorSession::counterPreview() const {
    const ToolStyle s = style();
    int placed = 0; // Counters of this run already in the document
    for (const auto &item : m_document->items())
        if (item.annotation->kind() == QLatin1String("counter") && item.annotation->series() == QStringLiteral("%1:%2").arg(s.counterStyle).arg(s.counterStart))
            ++placed;
    return counterLabel(s.counterStyle, s.counterStart + placed);
}

QVariantList EditorSession::choices() const {
    QVariantList list;
    const AnnotationKind *k = AnnotationKinds::find(m_tool);
    if (!k)
        return list;
    for (const ToolChoice &c : k->choices) {
        const QString current = m_options.choice(c.name, c.fallback);
        QVariantList options;
        QString label = current;
        for (const auto &v : c.values) {
            options << QVariantMap{{"v", v.value}, {"label", v.label}};
            if (v.value == current)
                label = v.label;
        }
        list << QVariantMap{{"name", c.name}, {"tip", c.tip}, {"current", current}, {"label", label}, {"options", options}};
    }
    return list;
}

void EditorSession::setChoice(const QString &name, const QString &value) {
    const AnnotationKind *k = AnnotationKinds::find(m_tool);
    if (!k)
        return;
    for (const ToolChoice &c : k->choices)
        if (c.name == name)
            for (const auto &v : c.values)
                if (v.value == value) {
                    m_options.setChoice(name, value);
                    return;
                }
}

// ── Selecting, moving, resizing ─────────────────────────────────────────────────────

namespace {
QRectF mapRect(const QRectF &r, const QRectF &from, const QRectF &to) {
    return QRectF(Handles::mapPoint(r.topLeft(), from, to), Handles::mapPoint(r.bottomRight(), from, to)).normalized();
}
} // namespace

int EditorSession::topHit(const QPointF &p) const {
    const qreal tolerance = 4 / zoom();
    const auto &items = m_document->items();
    const QRectF visible = m_document->visibleCanvas(!m_cropping).adjusted(-tolerance, -tolerance, tolerance, tolerance);
    for (int i = int(items.size()) - 1; i >= 0; --i)
        if (items[i].annotation->bounds().intersects(visible) && items[i].annotation->hit(p, tolerance))
            return items[i].id;
    return -1;
}

QRectF EditorSession::selectionFrame() const {
    QRectF frame;
    for (int id : m_selection)
        if (const Annotation *a = m_document->annotation(id))
            frame = frame.isNull() ? a->frame() : frame.united(a->frame());
    return frame;
}

QList<QPointF> EditorSession::handlePoints() const {
    QList<QPointF> points;
    if (m_selection.isEmpty())
        return points;
    const Annotation *only = m_selection.size() == 1 ? m_document->annotation(m_selection.first()) : nullptr;
    if (only && only->handleKind() == Annotation::HandleKind::Ends) {
        points = only->ends();
        if (const auto bend = only->bendHandle())
            points << *bend; // handle number 2
        return points;
    }
    if (only && only->handleKind() == Annotation::HandleKind::Outline)
        return points;
    const QRectF frame = selectionFrame();
    for (int h = 0; h < Handles::kBoxCount; ++h)
        points << Handles::boxPoint(frame, h);
    return points;
}

bool EditorSession::straightenAt(qreal x, qreal y) {
    // A double-click arrives during its second press, which has started a drag of the
    // middle dot: drop that drag (put the arrow back as it was), then straighten.
    if (m_mode == Mode::Resizing && m_gestureHandle == Annotation::kBendHandle) {
        for (const auto &[id, original] : m_before)
            m_document->setLive(id, original->clone());
        m_mode = Mode::None;
        m_gestureChanged = false;
    }
    if (m_mode != Mode::None || m_selection.size() != 1 || handleAt(QPointF(x, y)) != Annotation::kBendHandle)
        return false;
    const auto before = m_document->snapshot(m_selection);
    const int id = m_selection.first();
    auto straight = m_document->annotation(id)->clone();
    if (!straight->straighten())
        return false;
    m_document->setLive(id, std::move(straight));
    m_document->recordChange(QStringLiteral("Straighten"), before);
    changed();
    return true;
}

int EditorSession::handleAt(const QPointF &p) const {
    const qreal reach = Handles::kDotHot / 2 / zoom();
    const QList<QPointF> points = handlePoints();
    int best = -1;
    qreal bestDistance = reach;
    for (int i = 0; i < points.size(); ++i) {
        const qreal d = QLineF(points[i], p).length();
        if (d <= bestDistance) {
            best = i;
            bestDistance = d;
        }
    }
    return best;
}

void EditorSession::setSelection(const QList<int> &ids) {
    if (ids == m_selection)
        return;
    m_selection = ids;
    refreshHover();
    emit selectionChanged();
    emit chromeChanged();
}

void EditorSession::select(const QList<int> &ids) {
    QList<int> kept;
    for (int id : ids)
        if (m_document->annotation(id) && !kept.contains(id))
            kept << id;
    setSelection(kept);
}

QVariantList EditorSession::selectionList() const {
    QVariantList list;
    for (int id : m_selection)
        list << id;
    return list;
}

void EditorSession::changed() {
    refreshHover();
    emit documentChanged();
    emit chromeChanged();
}

void EditorSession::refreshHover() {
    m_hoverId = -1;
    m_hoverHandle = -1;
    m_overSelected = false;
    if (!m_hoverValid || m_mode != Mode::None)
        return;
    m_hoverHandle = handleAt(m_hoverPoint);
    if (m_hoverHandle >= 0)
        return;
    const int id = topHit(m_hoverPoint);
    m_overSelected = id >= 0 && m_selection.contains(id);
    m_hoverId = (id >= 0 && !m_overSelected) ? id : -1;
}

void EditorSession::pointerHovered(qreal x, qreal y) {
    if (m_cropping)
        return cropHovered(x, y);
    m_hoverValid = true;
    m_hoverPoint = QPointF(x, y);
    refreshHover();
    emit chromeChanged();
}

void EditorSession::pointerLeft() {
    m_hoverValid = false;
    refreshHover();
    emit chromeChanged();
}

void EditorSession::pointerPressed(qreal x, qreal y, int modifiers) {
    if (!m_open || m_mode != Mode::None)
        return;
    if (m_cropping)
        return cropPressed(x, y, modifiers);
    if (m_tool == QLatin1String("hand"))
        return; // the canvas pans instead; the Hand never selects, draws or moves anything
    if (m_eyedropper) { // picks a pixel of the shot instead of drawing or selecting
        const QColor c = sampleShot(x, y);
        if (c.isValid()) {
            setColor(c);
            cancelEyedropper();
        }
        return;
    }
    finishText(); // a click outside the text field finishes the Text first
    const QPointF p(x, y);
    const Qt::KeyboardModifiers mods(modifiers);
    m_dragFrom = p;
    m_editOnRelease = false;
    m_hoverValid = true;
    m_hoverPoint = p;
    m_gestureChanged = false;
    m_toggleOffOnRelease = -1;

    // A Handle of the selection: resize.
    const int handle = handleAt(p);
    if (handle >= 0) {
        m_mode = Mode::Resizing;
        m_gestureHandle = handle;
        m_gestureGrab = handlePoints().value(handle, p) - p;
        m_before = m_document->snapshot(m_selection);
        m_beforeFrame = selectionFrame();
        changed();
        return;
    }
    // An Annotation: select it and start moving the selection.
    const int id = topHit(p);
    m_leaderMoveWith = -1;
    if (id >= 0) {
        // The Counter tool on a Counter with no Callout: a drag pulls one out.
        if (m_tool == QLatin1String("counter") && !(mods & (Qt::ShiftModifier | Qt::AltModifier)) && m_document->annotation(id)->kind() == QLatin1String("counter") &&
            !hasCallout(id)) {
            pullPressed(id, p);
            return;
        }
        if (const auto *callout = dynamic_cast<const CalloutAnnotation *>(m_document->annotation(id));
            callout && callout->partAt(p, 4 / zoom()) == CalloutAnnotation::Leader)
            m_leaderMoveWith = callout->counterId(); // dragging the leader moves the Counter and the box together
        if (m_tool == QLatin1String("text") && isText(id) && !(mods & Qt::ShiftModifier)) {
            if (!m_selection.contains(id)) { // the Text tool on a Text edits it
                editText(id);
                return;
            }
            m_editOnRelease = true; // already selected: a drag moves it, a plain click edits it
        }
        if (mods & Qt::ShiftModifier) {
            if (m_selection.contains(id)) // Shift-click on a selected one lets it go, unless it turns into a drag
                m_toggleOffOnRelease = id;
            else
                setSelection(m_selection + QList<int>{id});
        } else if (!m_selection.contains(id)) {
            setSelection({id});
        }
        m_mode = Mode::Moving;
        if (mods & Qt::AltModifier) // Alt-drag moves a duplicate that starts under the original
            duplicateSelection();
        m_before = m_document->snapshot(m_leaderMoveWith >= 0 ? m_selection + QList<int>{m_leaderMoveWith} : m_selection);
        changed();
        return;
    }
    // Empty canvas.
    if (m_tool == QLatin1String("select")) {
        m_mode = Mode::Marquee;
        m_marqueeBase = (mods & Qt::ShiftModifier) ? m_selection : QList<int>();
        m_marquee = QRectF(p, p);
        setSelection(m_marqueeBase);
        changed();
        return;
    }
    if (!AnnotationKinds::find(m_tool))
        return;
    if (m_tool == QLatin1String("text")) { // Text is placed by a click, not drawn by a drag
        beginText(x, y);
        return;
    }
    if (m_tool == QLatin1String("counter")) { // a click places a Counter, a longer drag pulls a Callout out of it
        setSelection({});
        pullPressed(-1, p);
        return;
    }
    setSelection({});
    m_mode = Mode::Drawing;
    m_path = {p};
    m_draft.reset();
    changed();
}

void EditorSession::applyMove(const QPointF &p, int modifiers) {
    QPointF delta = p - m_dragFrom;
    if (Qt::KeyboardModifiers(modifiers) & Qt::ShiftModifier) { // one axis only
        if (std::abs(delta.x()) >= std::abs(delta.y()))
            delta.setY(0);
        else
            delta.setX(0);
    }
    for (const auto &[id, original] : m_before) {
        auto moved = original->clone();
        moved->translate(delta);
        m_document->setLive(id, std::move(moved));
    }
    m_gestureChanged = !delta.isNull();
}

void EditorSession::applyResize(const QPointF &p, int modifiers) {
    const bool shift = Qt::KeyboardModifiers(modifiers) & Qt::ShiftModifier;
    if (m_before.size() == 1 && m_before.front().second->handleKind() == Annotation::HandleKind::Ends) {
        auto resized = m_before.front().second->clone();
        const QPointF at = p + m_gestureGrab; // where the dot was grabbed follows the pointer
        if (m_gestureHandle == Annotation::kBendHandle)
            resized->dragBend(at, 8 / zoom());
        else
            resized->setEnd(m_gestureHandle, at);
        m_document->setLive(m_before.front().first, std::move(resized));
    } else {
        const QRectF frame = Handles::resizedFrame(m_beforeFrame, m_gestureHandle, p, shift);
        for (const auto &[id, original] : m_before) {
            auto resized = original->clone();
            switch (original->handleKind()) {
            case Annotation::HandleKind::Box:
                resized->setFrame(mapRect(original->frame(), m_beforeFrame, frame));
                break;
            case Annotation::HandleKind::Ends: {
                const QList<QPointF> ends = original->ends();
                for (int i = 0; i < ends.size(); ++i)
                    resized->setEnd(i, Handles::mapPoint(ends[i], m_beforeFrame, frame));
                break;
            }
            case Annotation::HandleKind::Outline: { // keeps its size, follows its center
                const QPointF center = original->frame().center();
                resized->translate(Handles::mapPoint(center, m_beforeFrame, frame) - center);
                break;
            }
            }
            m_document->setLive(id, std::move(resized));
        }
    }
    m_gestureChanged = m_gestureChanged || p != m_dragFrom; // a click on a Handle is no change
}

void EditorSession::applyMarquee(const QPointF &p) {
    m_marquee = QRectF(m_dragFrom, p).normalized();
    QList<int> ids = m_marqueeBase;
    for (const auto &item : m_document->items())
        if (item.annotation->bounds().intersects(m_marquee) && !ids.contains(item.id))
            ids << item.id;
    setSelection(ids);
}

void EditorSession::pointerMoved(qreal x, qreal y, int modifiers) {
    if (m_cropping)
        return cropMoved(x, y, modifiers);
    const QPointF p(x, y);
    switch (m_mode) {
    case Mode::None:
        return;
    case Mode::Drawing:
        if (m_path.isEmpty() || m_path.last() != p)
            m_path << p;
        m_draft = AnnotationKinds::find(m_tool)->fromDrag(gesture(x, y, modifiers));
        break;
    case Mode::Moving:
        applyMove(p, modifiers);
        break;
    case Mode::Resizing:
        applyResize(p, modifiers);
        break;
    case Mode::Marquee:
        applyMarquee(p);
        break;
    case Mode::Pulling:
        pullMoved(p);
        break;
    }
    changed();
}

void EditorSession::pointerReleased(qreal x, qreal y, int modifiers) {
    if (m_cropping)
        return cropReleased(x, y, modifiers);
    const Mode mode = m_mode;
    if (mode == Mode::None)
        return;
    pointerMoved(x, y, modifiers); // the release point counts
    m_mode = Mode::None;
    m_draft.reset();
    switch (mode) {
    case Mode::Drawing: {
        const int id = m_document->draw(m_tool, gesture(x, y, modifiers));
        if (id >= 0)
            setSelection({id}); // a new Annotation is selected, ready to recolor
        break;
    }
    case Mode::Moving:
        if (m_gestureChanged) {
            m_document->recordChange(QStringLiteral("Move"), m_before);
        } else if (m_editOnRelease && m_selection.size() == 1 && isText(m_selection.first())) {
            editText(m_selection.first());
        } else if (m_toggleOffOnRelease >= 0) {
            QList<int> rest = m_selection;
            rest.removeAll(m_toggleOffOnRelease);
            setSelection(rest);
        }
        break;
    case Mode::Resizing:
        if (m_gestureChanged)
            m_document->recordChange(QStringLiteral("Resize"), m_before);
        break;
    case Mode::Marquee:
        m_marquee = {};
        break;
    case Mode::Pulling:
        pullReleased(QPointF(x, y));
        break;
    case Mode::None:
        break;
    }
    m_before.clear();
    m_hoverValid = true;
    m_hoverPoint = QPointF(x, y);
    changed();
}

// ── Editing actions (keys and menus) ────────────────────────────────────────────────

void EditorSession::selectAll() {
    if (m_mode == Mode::None && !m_cropping)
        setSelection(m_document->order());
}

void EditorSession::deselect() { setSelection({}); }

void EditorSession::escape() {
    if (m_textActive) { // Esc finishes the Text first; the next one steps back further
        finishText();
        return;
    }
    if (m_cropping) // Esc steps back one level: Crop mode first
        return leaveCrop();
    if (!m_selection.isEmpty())
        deselect();
    else
        cancel();
}

void EditorSession::deleteSelection() {
    if (m_mode != Mode::None || m_selection.isEmpty())
        return;
    m_document->remove(m_selection);
    setSelection({});
    changed();
}

void EditorSession::duplicateSelection() {
    if (m_mode != Mode::None && m_mode != Mode::Moving)
        return;
    if (m_selection.isEmpty())
        return;
    // Offset 10 px from the original, except while Alt-dragging: then the copy
    // starts exactly under it and the drag carries it away.
    const QPointF offset = m_mode == Mode::Moving ? QPointF() : QPointF(10, 10);
    std::vector<std::unique_ptr<Annotation>> copies;
    for (const auto &item : m_document->items())
        if (m_selection.contains(item.id) && item.annotation->anchorId() == 0) { // a Callout stays with its own Counter
            copies.push_back(item.annotation->clone());
            copies.back()->translate(offset);
        }
    if (copies.empty())
        return;
    setSelection(m_document->addMany(std::move(copies), QStringLiteral("Duplicate")));
    changed();
}

void EditorSession::nudge(int dx, int dy, bool big) {
    if (m_mode != Mode::None || m_selection.isEmpty())
        return;
    // Arrow keys move on screen; the Annotations live in canvas space (a Crop may turn it).
    const QTransform inv = m_document->canvasToDisplay(!m_cropping).inverted();
    const QPointF delta = inv.map(QPointF(dx, dy) * (big ? 10 : 1)) - inv.map(QPointF(0, 0));
    m_document->modify(QStringLiteral("Nudge"), m_selection, [delta](Annotation &a) { a.translate(delta); });
    changed();
}

void EditorSession::bringForward() {
    if (m_mode == Mode::None && m_document->reorder(m_selection, true))
        changed();
}

void EditorSession::sendBackward() {
    if (m_mode == Mode::None && m_document->reorder(m_selection, false))
        changed();
}

bool EditorSession::copySelection() {
    if (m_selection.isEmpty())
        return false;
    std::vector<std::unique_ptr<Annotation>> copies;
    for (const auto &item : m_document->items())
        if (m_selection.contains(item.id) && item.annotation->anchorId() == 0)
            copies.push_back(item.annotation->clone());
    if (copies.empty())
        return false;
    m_editor.copyAnnotations(m_captureId, std::move(copies));
    return true;
}

void EditorSession::paste() {
    if (m_mode != Mode::None || !m_open)
        return;
    auto copies = m_editor.annotationsToPaste(m_captureId);
    if (copies.empty())
        return;
    setSelection(m_document->addMany(std::move(copies), QStringLiteral("Paste")));
    changed();
}

bool EditorSession::addImage(const QImage &image, const QPointF &center) {
    if (!m_open || m_mode != Mode::None || image.isNull())
        return false;
    QPointF c = center;
    if (qIsNaN(c.x()) || qIsNaN(c.y()))
        c = QRectF(m_document->canvas()).center();
    const QPoint topLeft = (c - QPointF(image.width() / 2.0, image.height() / 2.0)).toPoint();
    std::vector<std::unique_ptr<Annotation>> one;
    one.push_back(makeImageAnnotation(image, QRectF(topLeft, QSizeF(image.size()))));
    setSelection(m_document->addMany(std::move(one), QStringLiteral("Add image")));
    changed();
    return true;
}

bool EditorSession::addImageFile(const QString &pathOrUrl, qreal centerX, qreal centerY) {
    const QUrl url(pathOrUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        m_editor.daemon().service<Notifier>().notify(QStringLiteral("Tarn could not add the image"),
                                                     QFileInfo(path).fileName() + QStringLiteral(" is not a picture Tarn can read"));
        return false;
    }
    return addImage(image, QPointF(centerX, centerY));
}

bool EditorSession::pasteClipboard() {
    if (const QImage image = m_editor.clipboardImageToPaste(); !image.isNull())
        return addImage(image);
    if (!m_editor.hasAnnotationsToPaste())
        return false;
    paste();
    return true;
}

void EditorSession::undo() {
    if (m_mode != Mode::None || !m_document->history().undo())
        return;
    emit viewChanged(); // a Crop or Frame step may have changed the size
    emit cropChanged();
    select(m_selection);
    changed();
}

void EditorSession::redo() {
    if (m_mode != Mode::None || !m_document->history().redo())
        return;
    emit viewChanged();
    emit cropChanged();
    select(m_selection);
    changed();
}

// ── Handles and ghost, in view pixels ───────────────────────────────────────────────

void EditorSession::chromeFor(const QList<int> &ids, qreal opacity, int hotHandle, QVariantList &out) const {
    auto outline = [&](const QRectF &docRect, qreal outset, qreal alpha) {
        const QPointF tl = docToView(docRect.left(), docRect.top());
        const QPointF br = docToView(docRect.right(), docRect.bottom());
        const QRectF r = QRectF(tl, br).normalized().adjusted(-outset, -outset, outset, outset);
        out << QVariantMap{{"type", "outline"}, {"x", r.x()}, {"y", r.y()}, {"w", r.width()}, {"h", r.height()}, {"opacity", alpha}};
    };
    auto dot = [&](const QPointF &docPoint, int handle, qreal alpha) {
        const QPointF v = docToView(docPoint.x(), docPoint.y());
        const bool hot = handle == hotHandle;
        out << QVariantMap{{"type", "dot"}, {"x", v.x()}, {"y", v.y()}, {"size", hot ? Handles::kDotHot : Handles::kDot},
                           {"hot", hot}, {"handle", handle}, {"opacity", alpha}};
    };
    using Kind = Annotation::HandleKind;
    QRectF group;
    for (int id : ids) {
        const Annotation *a = m_document->annotation(id);
        if (!a)
            continue;
        group = group.isNull() ? a->frame() : group.united(a->frame());
        if (ids.size() > 1 || a->handleKind() != Kind::Ends)
            outline(a->handleKind() == Kind::Outline ? a->bounds() : a->frame(), a->handleKind() == Kind::Outline ? Handles::kOutlineOut : 0, opacity);
        if (ids.size() == 1) {
            if (a->handleKind() == Kind::Box)
                for (int h = 0; h < Handles::kBoxCount; ++h)
                    dot(Handles::boxPoint(a->frame(), h), h, opacity);
            else if (a->handleKind() == Kind::Ends)
                for (int i = 0; i < a->ends().size(); ++i)
                    dot(a->ends()[i], i, opacity);
            if (a->handleKind() == Kind::Ends)
                if (const auto bend = a->bendHandle())
                    dot(*bend, Annotation::kBendHandle, opacity);
        }
    }
    if (ids.size() > 1) { // several: each keeps its outline, the group gets the eight dots
        outline(group, 0, opacity);
        for (int h = 0; h < Handles::kBoxCount; ++h)
            dot(Handles::boxPoint(group, h), h, opacity);
    }
}

QVariantList EditorSession::chrome() const {
    QVariantList out;
    if (m_mode == Mode::Marquee && !m_marquee.isNull()) {
        const QPointF tl = docToView(m_marquee.left(), m_marquee.top());
        const QPointF br = docToView(m_marquee.right(), m_marquee.bottom());
        const QRectF mr = QRectF(tl, br).normalized();
        out << QVariantMap{{"type", "marquee"}, {"x", mr.x()}, {"y", mr.y()}, {"w", mr.width()}, {"h", mr.height()}, {"opacity", 1.0}};
    }
    if (m_hoverId >= 0)
        chromeFor({m_hoverId}, Handles::kGhostOpacity, -1, out);
    chromeFor(m_selection, 1.0, m_mode == Mode::Resizing ? m_gestureHandle : m_hoverHandle, out);
    return out;
}

int EditorSession::cursor() const {
    if (m_cropping)
        return cropCursor();
    if (m_tool == QLatin1String("hand"))
        return Qt::OpenHandCursor;
    auto resizeCursor = [this](int handle) {
        const Annotation *only = m_selection.size() == 1 ? m_document->annotation(m_selection.first()) : nullptr;
        return only && only->handleKind() == Annotation::HandleKind::Ends ? int(Qt::SizeAllCursor) : Handles::boxCursor(handle);
    };
    switch (m_mode) {
    case Mode::Moving: return Qt::SizeAllCursor;
    case Mode::Resizing: return resizeCursor(m_gestureHandle);
    case Mode::Drawing:
    case Mode::Pulling:
    case Mode::Marquee: return Qt::CrossCursor;
    case Mode::None: break;
    }
    if (m_hoverHandle >= 0)
        return resizeCursor(m_hoverHandle);
    if (m_hoverId >= 0 || m_overSelected)
        return Qt::SizeAllCursor;
    return m_tool == QLatin1String("select") ? Qt::ArrowCursor : Qt::CrossCursor;
}

// ── Zoom and pan ────────────────────────────────────────────────────────────────────

void EditorSession::setViewport(const QSizeF &size) {
    if (size == m_viewport)
        return;
    m_viewport = size;
    clampPan();
    emit viewChanged();
}

qreal EditorSession::fitZoom() const {
    const QSizeF doc = docSize();
    if (doc.isEmpty() || m_viewport.isEmpty())
        return 1;
    const qreal m = 2 * EditorMetrics::canvasMargin;
    return std::clamp(std::min({1.0, (m_viewport.width() - m) / doc.width(), (m_viewport.height() - m) / doc.height()}), 0.01, 1.0);
}

qreal EditorSession::zoom() const { return m_fit ? fitZoom() : m_zoom; }

QPointF EditorSession::origin() const {
    const qreal z = zoom();
    const QSizeF doc = docSize();
    return {m_viewport.width() / 2 + m_pan.x() - doc.width() * z / 2, m_viewport.height() / 2 + m_pan.y() - doc.height() * z / 2};
}

// View <-> finished image (the Frame and content, before the output scale; in Crop mode the
// whole canvas, turned and flipped, with no Frame).
QPointF EditorSession::viewToDisplay(const QPointF &v) const {
    const QPointF o = origin();
    const qreal z = zoom();
    return {(v.x() - o.x()) / z, (v.y() - o.y()) / z};
}

QPointF EditorSession::displayToView(const QPointF &d) const {
    const QPointF o = origin();
    const qreal z = zoom();
    return {o.x() + d.x() * z, o.y() + d.y() * z};
}

// "doc" is canvas space, where the Annotations live. One chain, both ways:
// view -> finished image -> content (the Frame's offset off) -> canvas (the Crop undone).
QPointF EditorSession::viewToDoc(qreal x, qreal y) const {
    const QPointF content = viewToDisplay({x, y}) - QPointF(m_document->contentOffset(!m_cropping));
    return m_document->canvasToDisplay(!m_cropping).inverted().map(content);
}

QPointF EditorSession::docToView(qreal x, qreal y) const {
    const QPointF content = m_document->canvasToDisplay(!m_cropping).map(QPointF(x, y));
    return displayToView(content + QPointF(m_document->contentOffset(!m_cropping)));
}

void EditorSession::clampPan() {
    // A shot smaller than the view stays centered; a larger one may be pulled until
    // its edge reaches the margin.
    const qreal z = zoom();
    const QSizeF doc = docSize();
    const qreal ex = std::max(0.0, (doc.width() * z - m_viewport.width()) / 2 + EditorMetrics::canvasMargin);
    const qreal ey = std::max(0.0, (doc.height() * z - m_viewport.height()) / 2 + EditorMetrics::canvasMargin);
    m_pan = {std::clamp(m_pan.x(), -ex, ex), std::clamp(m_pan.y(), -ey, ey)};
}

void EditorSession::zoomFit() {
    m_fit = true;
    m_pan = {};
    emit viewChanged();
}

void EditorSession::zoomTo(qreal z) { zoomAt(z, m_viewport.width() / 2, m_viewport.height() / 2); }

void EditorSession::zoomAt(qreal z, qreal vx, qreal vy) {
    z = std::clamp(z, kMinZoom, kMaxZoom);
    const QPointF anchorDoc = viewToDisplay({vx, vy}); // finished-image px under the pointer
    const QSizeF doc = docSize();
    m_fit = false;
    m_zoom = z;
    m_pan = {vx - m_viewport.width() / 2 + doc.width() * z / 2 - anchorDoc.x() * z,
             vy - m_viewport.height() / 2 + doc.height() * z / 2 - anchorDoc.y() * z};
    clampPan();
    emit viewChanged();
}

void EditorSession::zoomIn() {
    const qreal z = zoom();
    for (qreal step : kZoomSteps)
        if (step > z * 1.001)
            return zoomTo(step);
}

void EditorSession::zoomOut() {
    const qreal z = zoom();
    for (int i = int(std::size(kZoomSteps)) - 1; i >= 0; --i)
        if (kZoomSteps[i] < z / 1.001)
            return zoomTo(kZoomSteps[i]);
}

void EditorSession::wheelZoom(qreal angleDeltaY, qreal vx, qreal vy) {
    zoomAt(zoom() * qPow(1.1, angleDeltaY / 120.0), vx, vy);
}

void EditorSession::panBy(qreal dx, qreal dy) {
    // Panning from Fit leaves Fit, at the zoom Fit had.
    if (m_fit) {
        m_zoom = fitZoom();
        m_fit = false;
    }
    m_pan += QPointF(dx, dy);
    clampPan();
    emit viewChanged();
}

// ── Ways out ────────────────────────────────────────────────────────────────────────

void EditorSession::end() {
    m_open = false;
    m_textActive = false;
    m_calloutProto.reset();
    m_mode = Mode::None;
    m_draft.reset();
    emit finished();
}

// What the Thumbnail gets from now on: the flattened image, or nothing at all
// when there is nothing drawn (then it stays the untouched capture).
void EditorSession::applyEdits() {
    finishText();
    if (fromHistory())
        return; // no Thumbnail to hand the picture to
    m_editor.stack().setEdited(m_captureId, m_document->isEdited() ? m_document->toPng() : QByteArray());
    m_pushedEdits = true;
}

// A copy or save failed: the Thumbnail goes back to what it had.
void EditorSession::revertEdits() {
    if (m_pushedEdits && !fromHistory())
        m_editor.stack().setEdited(m_captureId, m_editedBefore);
    m_pushedEdits = false;
}

// A close that keeps edits writes them beside the History original (the Project
// file), or removes the file when nothing is left on the picture. Esc never gets here.
// A copy, save or Pin closes the Thumbnail, so those read its History id first and pass
// it; with none given it is this session's (the Thumbnail's, or the entry it was opened from).
void EditorSession::writeProject(const QString &historyId) {
    const QString id = !historyId.isEmpty() ? historyId : fromHistory() ? m_historyId : m_editor.stack().historyId(m_captureId);
    if (!id.isEmpty())
        m_editor.daemon().service<History>().saveProject(id, *m_document);
}

void EditorSession::done() {
    if (!m_open)
        return;
    applyEdits();
    writeProject();
    m_editor.commit(this);
    m_open = false;
    m_editor.release(this);
    emit finished();
}

// ---- Edit from History: the closes that act on the finished picture ----

void EditorSession::closeHistorySession() {
    m_open = false;
    m_editor.release(this);
    emit finished();
}

// Copy, Ctrl+S, Save as and Pin of an Editor opened from History. The same results as
// on a Thumbnail, without one: the finished picture goes out, the Project file is
// written, the Editor closes and focus returns to the History window.
bool EditorSession::historyAction(HistoryAction action, const QString &path) {
    finishText();
    auto &daemon = m_editor.daemon();
    auto &history = daemon.service<History>();
    const QByteArray png = m_document->toPng();
    const QJsonObject description = history.description(m_historyId);
    const QDateTime captureTime = QDateTime::fromString(description.value(QStringLiteral("captureTime")).toString(), Qt::ISODate);
    const CaptureName name = history.captureName(m_historyId);
    QString error;
    switch (action) {
    case HistoryAction::Copy:
        if (daemon.service<Clipboard>().copyPng(png, captureTime, &error, name).isEmpty()) {
            daemon.service<Notifier>().notify(QStringLiteral("Tarn could not copy the screenshot"), error);
            return false;
        }
        break;
    case HistoryAction::Save:
        if (history.saveImage(m_historyId, png, captureTime, name).isEmpty())
            return false;
        break;
    case HistoryAction::SaveAs:
        if (path.isEmpty() || !history.saveImageAs(m_historyId, png, path))
            return false;
        break;
    case HistoryAction::Pin: {
        ThumbnailStack::Capture capture;
        capture.png = history.originalPng(m_historyId);
        capture.edited = m_document->isEdited() ? png : QByteArray();
        capture.captureTime = captureTime;
        capture.name = name;
        capture.historyId = m_historyId;
        if (daemon.service<Pins>().pinCapture(capture) < 0) {
            daemon.service<Notifier>().notify(QStringLiteral("Tarn could not pin the screenshot"), QStringLiteral("its picture could not be read"));
            return false;
        }
        break;
    }
    }
    writeProject();
    closeHistorySession();
    return true;
}

void EditorSession::cancel() {
    if (!m_open)
        return;
    m_open = false;
    m_editor.release(this);
    emit finished();
}

bool EditorSession::copyAndClose() {
    if (!m_open)
        return false;
    if (fromHistory())
        return historyAction(HistoryAction::Copy);
    applyEdits();
    const QString historyId = m_editor.stack().historyId(m_captureId);
    if (!m_editor.stack().copy(m_captureId)) { // success closes the Thumbnail, which ends this session
        revertEdits();
        return false;
    }
    writeProject(historyId);
    return true;
}

bool EditorSession::copyKeepOpen() {
    if (!m_open)
        return false;
    QString error;
    const QDateTime time = fromHistory() ? QDateTime::fromString(m_editor.daemon().service<History>().description(m_historyId).value(QStringLiteral("captureTime")).toString(), Qt::ISODate)
                                         : m_editor.stack().captureTime(m_captureId);
    const CaptureName name = fromHistory() ? m_editor.daemon().service<History>().captureName(m_historyId) : m_editor.stack().captureName(m_captureId);
    const bool ok = !m_editor.daemon().service<Clipboard>().copyPng(m_document->toPng(), time, &error, name).isEmpty();
    if (!ok)
        m_editor.daemon().service<Notifier>().notify(QStringLiteral("Tarn could not copy the screenshot"), error);
    return ok;
}

bool EditorSession::saveAndClose() {
    if (!m_open)
        return false;
    if (fromHistory())
        return historyAction(HistoryAction::Save);
    applyEdits();
    const QString historyId = m_editor.stack().historyId(m_captureId);
    if (m_editor.stack().save(m_captureId).isEmpty()) {
        revertEdits();
        return false;
    }
    writeProject(historyId);
    return true;
}

bool EditorSession::saveAs(const QString &path) {
    if (!m_open)
        return false;
    if (fromHistory())
        return historyAction(HistoryAction::SaveAs, path);
    applyEdits();
    const QString historyId = m_editor.stack().historyId(m_captureId);
    if (!m_editor.stack().saveAs(m_captureId, path)) {
        revertEdits();
        return false;
    }
    writeProject(historyId);
    return true;
}

bool EditorSession::pin() {
    if (!m_open)
        return false;
    if (fromHistory())
        return historyAction(HistoryAction::Pin);
    applyEdits();
    const QString historyId = m_editor.stack().historyId(m_captureId);
    if (m_editor.daemon().service<Pins>().pin(m_captureId) < 0) { // success closes the Thumbnail, which ends this session
        revertEdits();
        return false;
    }
    writeProject(historyId);
    return true;
}

QString EditorSession::saveAsFolder() const { return m_editor.stack().saveAsFolder(); }
QString EditorSession::saveAsSuggestion() const {
    if (fromHistory()) {
        return saveAsFolder() + '/' + m_editor.daemon().service<History>().saveAsFileName(m_historyId);
    }
    return m_editor.stack().saveAsSuggestion(m_captureId);
}
int EditorSession::saveAsFilterIndex() const { return m_editor.stack().saveAsFilterIndex(); }

} // namespace tarn
