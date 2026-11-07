#include "overlay_model.h"

#include <QtMath>

namespace tarn {

namespace {
constexpr int kClickSlopPx = 3; // a press-release this close is a click, not a drag
}

void OverlayModel::open(Mode mode, const QSize &screen, qreal pixelScale, bool timer) {
    m_mode = mode;
    m_timer = timer && mode == Screenshot;
    m_sequence = false;
    m_taken = 0;
    emit sequenceChanged();
    m_screen = screen;
    m_pixelScale = pixelScale > 0 ? pixelScale : 1.0;
    m_space = false;
    m_keyboardDrag = false;
    m_mods = {};
    setPhase(Phase::Picking);
    setSelection({});
    m_windows.clear();
    m_pressedOnWindow = m_dragged = false;
    setHighlighted(-1);
    if (m_noFrame) {
        m_noFrame = false;
        emit noFrameChanged();
    }
    emit modeChanged();
    emit timerChanged();
    emit activeChanged();
}

void OverlayModel::setMode(int mode) {
    if (!active() || mode < Screenshot || mode > Color || mode == m_mode)
        return;
    m_mode = Mode(mode);
    if (m_mode == Color) { // a pick, never an area
        setHighlighted(-1);
        m_pressedOnWindow = m_keyboardDrag = false;
        setPhase(Phase::Picking);
        setSelection({});
    }
    emit magnifierChanged(); // forced on in Color Mode, the user's setting elsewhere
    const bool timerWasOn = m_timer;
    m_timer = false;
    refreshHighlight();
    emit modeChanged();
    if (timerWasOn)
        emit timerChanged();
}

void OverlayModel::setTextLayout(TextLayout layout) {
    if (layout == m_textLayout)
        return;
    m_textLayout = layout;
    emit textLayoutChanged();
}

void OverlayModel::toggleSequence() {
    if (!active())
        return;
    m_sequence = !m_sequence;
    emit sequenceChanged();
}

// A Sequence shot was handed over: count it and go back to selecting on the same image.
void OverlayModel::afterSequenceShot() {
    ++m_taken;
    m_pressedOnWindow = m_dragged = m_keyboardDrag = false;
    setHighlighted(-1);
    setPhase(Phase::Picking);
    setSelection({});
    updateHighlight(m_pointer);
    emit sequenceChanged();
}

void OverlayModel::toggleTimer() {
    if (!active() || !timerAvailable())
        return;
    m_timer = !m_timer;
    refreshHighlight();
    emit timerChanged();
}

QString OverlayModel::timerLabel() const {
    return QStringLiteral("%1 s").arg(m_timerDelay, 0, 'g', 4);
}

void OverlayModel::takeFullScreen() {
    if (!active() || m_mode == Color || (m_phase != Phase::Picking && m_phase != Phase::Dragging && m_phase != Phase::Kept))
        return;
    setHighlighted(-1);
    m_pressedOnWindow = false;
    m_keyboardDrag = false;
    setSelection(QRect(QPoint(0, 0), m_screen));
    if (m_mode == Record)
        setPhase(Phase::Kept); // the Record panel decides when to start
    else
        confirm();
}

void OverlayModel::restoreLastArea() {
    if (!active() || (m_phase != Phase::Picking && m_phase != Phase::Dragging && m_phase != Phase::Kept))
        return;
    emit lastAreaRequested();
}

void OverlayModel::keepArea(const QRect &area) {
    if (!active() || area.isEmpty())
        return;
    setHighlighted(-1);
    m_pressedOnWindow = false;
    m_keyboardDrag = false;
    setSelection(area);
    setPhase(Phase::Kept);
}

int OverlayModel::textButtonExtra(TextLayout layout) {
    switch (layout) {
    case TextLayout::Paragraphs: return 76;
    case TextLayout::Lines: return 42;
    case TextLayout::Table: return 41;
    }
    return 0;
}

QRect OverlayModel::modeBarTopSpot() const {
    const int width = kModeBarWidth + (m_mode == Text ? textButtonExtra(m_textLayout) : 0);
    return QRect((m_screen.width() - width) / 2, kModeBarMargin, width, kModeBarHeight);
}

QRect OverlayModel::modeBarRect() const {
    QRect spot = modeBarTopSpot();
    if (modeBarAtBottom())
        spot.moveTop(m_screen.height() - kModeBarMargin - spot.height());
    return spot;
}

bool OverlayModel::modeBarAtBottom() const {
    if (m_selection.isEmpty())
        return false;
    const QRect spot = modeBarTopSpot();
    const int m = kModeBarMargin;
    return m_selection.left() <= spot.left() + spot.width() + m && m_selection.left() + m_selection.width() >= spot.left() - m &&
           m_selection.top() <= spot.top() + spot.height() + m && m_selection.top() + m_selection.height() >= spot.top() - m;
}

// Window highlight rules changed (Mode or Timer): drop it, or pick again under the pointer.
void OverlayModel::refreshHighlight() {
    if (!highlightAllowed()) {
        m_pressedOnWindow = false;
        setHighlighted(-1);
    } else if (m_phase == Phase::Picking) {
        updateHighlight(m_pointer);
    }
}

void OverlayModel::setWindows(const QList<WindowInfo> &windows) {
    m_windows = windows;
    setHighlighted(-1); // the first pointer move picks the window under it
}

void OverlayModel::setHighlighted(int index) {
    if (index == m_highlighted)
        return;
    m_highlighted = index;
    emit highlightChanged();
}

// The frontmost window under the pointer wins; with none, the current one is
// kept while the pointer is within kStickyPx of it.
void OverlayModel::updateHighlight(const QPoint &p) {
    if (!highlightAllowed()) {
        setHighlighted(-1);
        return;
    }
    for (int i = 0; i < m_windows.size(); ++i) {
        if (m_windows[i].rect.contains(p)) {
            setHighlighted(i);
            return;
        }
    }
    if (m_highlighted >= 0 && m_windows[m_highlighted].rect.adjusted(-kStickyPx, -kStickyPx, kStickyPx, kStickyPx).contains(p))
        return;
    setHighlighted(-1);
}

QString OverlayModel::sizeLabel() const {
    return QStringLiteral("%1 × %2").arg(qRound(m_selection.width() * m_pixelScale)).arg(qRound(m_selection.height() * m_pixelScale));
}

void OverlayModel::setPixelSize(int width, int height) {
    if (!kept() || m_selection.isEmpty())
        return;
    const qreal f = m_pixelScale > 0 ? m_pixelScale : 1.0;
    const int w = qBound(1, qRound(width / f), qMax(1, m_screen.width() - m_selection.x()));
    const int h = qBound(1, qRound(height / f), qMax(1, m_screen.height() - m_selection.y()));
    setSelection(QRect(m_selection.topLeft(), QSize(w, h)));
}

QColor OverlayModel::pointerColor() const {
    if (m_pixels.isNull())
        return {};
    const int x = qBound(0, qFloor(m_pointer.x() * m_pixelScale), m_pixels.width() - 1);
    const int y = qBound(0, qFloor(m_pointer.y() * m_pixelScale), m_pixels.height() - 1);
    return QColor::fromRgb(m_pixels.pixel(x, y));
}

void OverlayModel::pickColor(bool asRgb) {
    if (!active() || m_mode != Color)
        return;
    const QColor color = pointerColor();
    if (continuesSequence()) {
        if (!color.isValid())
            return;
        afterSequenceShot();
        emit colorPicked(color, asRgb);
        return;
    }
    finish();
    if (color.isValid())
        emit colorPicked(color, asRgb);
}

void OverlayModel::setMagnifier(bool on) {
    if (on == m_magnifier)
        return;
    m_magnifier = on;
    emit magnifierChanged();
}

QPoint OverlayModel::magnifierTileAt(const QPoint &p, const QSize &screen) {
    const int w = kMagnifierTile, h = kMagnifierTile + kMagnifierPillGap + kMagnifierPillHeight;
    int x = p.x() + kMagnifierGap;
    if (x + w > screen.width() - kMagnifierEdge)
        x = p.x() - kMagnifierGap - w;
    int y = p.y() + kMagnifierGap;
    if (y + h > screen.height() - kMagnifierEdge)
        y = p.y() - kMagnifierGap - h;
    return {x, y};
}

QString OverlayModel::magnifierLabel() const {
    if (m_mode == Color && pointerColor().isValid()) // what a click would copy
        return colorText(pointerColor(), m_mods & Qt::ShiftModifier);
    return QStringLiteral("%1, %2").arg(qFloor(m_pointer.x() * m_pixelScale)).arg(qFloor(m_pointer.y() * m_pixelScale));
}

QString OverlayModel::colorText(const QColor &color, bool asRgb) {
    return asRgb ? QStringLiteral("rgb(%1, %2, %3)").arg(color.red()).arg(color.green()).arg(color.blue()) : color.name();
}

void OverlayModel::setMods(Qt::KeyboardModifiers mods) {
    const bool shiftChanged = (mods ^ m_mods) & Qt::ShiftModifier;
    m_mods = mods;
    if (shiftChanged && m_mode == Color)
        emit pointerChanged(); // the pill's label
}

QString OverlayModel::positionLabel() const {
    return QStringLiteral("%1, %2").arg(qRound(m_selection.x() * m_pixelScale)).arg(qRound(m_selection.y() * m_pixelScale));
}

QPoint OverlayModel::clamped(qreal x, qreal y) const {
    return {qBound(0, qFloor(x), qMax(0, m_screen.width() - 1)), qBound(0, qFloor(y), qMax(0, m_screen.height() - 1))};
}

void OverlayModel::setSelection(const QRect &rect) {
    if (rect == m_selection)
        return;
    m_selection = rect;
    emit selectionChanged();
}

void OverlayModel::setPointer(const QPoint &point) {
    if (point == m_pointer)
        return;
    m_pointer = point;
    emit pointerChanged();
}

void OverlayModel::setPhase(Phase phase) {
    const bool wasKept = kept();
    m_phase = phase;
    if (wasKept != kept())
        emit keptChanged();
}

void OverlayModel::finish() {
    setPhase(Phase::Closed);
    emit activeChanged();
}

// The drag's rectangle from its anchor and moving corner. Shift makes it a
// square, Alt grows it from the anchor as center; both at once is a square
// around the center. Always inside the monitor.
QRect OverlayModel::dragRect() const {
    const bool square = (m_mods & Qt::ShiftModifier) && !m_keyboardDrag;
    const bool center = (m_mods & Qt::AltModifier) && !m_keyboardDrag;
    const int sx = m_corner.x() < m_anchor.x() ? -1 : 1, sy = m_corner.y() < m_anchor.y() ? -1 : 1;
    int w = qAbs(m_corner.x() - m_anchor.x()), h = qAbs(m_corner.y() - m_anchor.y());
    // Room from the anchor to the monitor's edge in each direction.
    const int roomX = center ? qMin(m_anchor.x(), m_screen.width() - m_anchor.x()) : (sx < 0 ? m_anchor.x() : m_screen.width() - m_anchor.x());
    const int roomY = center ? qMin(m_anchor.y(), m_screen.height() - m_anchor.y()) : (sy < 0 ? m_anchor.y() : m_screen.height() - m_anchor.y());
    if (square)
        w = h = qMin(qMax(w, h), qMin(roomX, roomY));
    w = qMin(w, roomX);
    h = qMin(h, roomY);
    if (center)
        return QRect(m_anchor.x() - w, m_anchor.y() - h, 2 * w, 2 * h);
    return QRect(QPoint(sx < 0 ? m_anchor.x() - w : m_anchor.x(), sy < 0 ? m_anchor.y() - h : m_anchor.y()), QSize(w, h));
}

// Moves the drag's corner to `pointer`; with Space held the whole Selection
// moves by the same step instead (kept inside the monitor).
void OverlayModel::updateDrag(const QPoint &pointer) {
    if (m_space) {
        const QPoint step = pointer - m_pointer;
        const QRect r = dragRect();
        const QPoint d(qBound(-r.left(), step.x(), m_screen.width() - (r.left() + r.width())),
                       qBound(-r.top(), step.y(), m_screen.height() - (r.top() + r.height())));
        m_anchor += d;
        m_corner += d;
    } else {
        m_corner = pointer;
    }
    setSelection(dragRect());
}

void OverlayModel::pointerMoved(qreal x, qreal y, int modifiers) {
    if (!active())
        return;
    setMods(Qt::KeyboardModifiers(modifiers));
    const QPoint p = clamped(x, y);
    if (m_phase == Phase::Dragging) {
        if (m_pressedOnWindow && !m_dragged) {
            setPointer(p);
            if (qMax(qAbs(p.x() - m_anchor.x()), qAbs(p.y() - m_anchor.y())) <= kClickSlopPx)
                return; // still a click on the window
            m_dragged = true;
            setHighlighted(-1); // it is an area now
        }
        updateDrag(p);
        setPointer(p);
        return;
    }
    setPointer(p);
    if (m_phase == Phase::Picking)
        updateHighlight(p);
    if (m_phase == Phase::Moving) {
        const QPoint d = QPoint(qFloor(x), qFloor(y)) - m_grabAt;
        QRect r = m_grabbed;
        r.moveTo(qBound(0, r.x() + d.x(), qMax(0, m_screen.width() - r.width())),
                 qBound(0, r.y() + d.y(), qMax(0, m_screen.height() - r.height())));
        setSelection(r);
    } else if (m_phase == Phase::Resizing) {
        const QPoint q(qBound(0, qRound(x), m_screen.width()), qBound(0, qRound(y), m_screen.height()));
        int l = m_grabbed.left(), t = m_grabbed.top();
        int r = m_grabbed.left() + m_grabbed.width(), b = m_grabbed.top() + m_grabbed.height();
        const bool moveL = m_handle == 0 || m_handle == 3 || m_handle == 7;
        const bool moveR = m_handle == 1 || m_handle == 2 || m_handle == 5;
        const bool moveT = m_handle == 0 || m_handle == 1 || m_handle == 4;
        const bool moveB = m_handle == 2 || m_handle == 3 || m_handle == 6;
        if (moveL) l = q.x();
        if (moveR) r = q.x();
        if (moveT) t = q.y();
        if (moveB) b = q.y();
        // Dragging past the opposite edge flips the Selection over.
        setSelection(QRect(QPoint(qMin(l, r), qMin(t, b)), QSize(qAbs(r - l), qAbs(b - t))));
    }
}

void OverlayModel::pointerPressed(qreal x, qreal y, int button, int modifiers) {
    if (!active())
        return;
    setMods(Qt::KeyboardModifiers(modifiers));
    if (button == Qt::RightButton) {
        if (kept())
            clearKept();
        else
            cancel();
        return;
    }
    if (button != Qt::LeftButton)
        return;
    if (m_mode == Color) { // the pick happens on release
        setPointer(clamped(x, y));
        return;
    }
    if (kept()) {
        const QPoint p = clamped(x, y);
        setPointer(p);
        m_grabbed = m_selection;
        m_grabAt = p;
        m_handle = handleAt(x, y);
        if (m_handle >= 0)
            setPhase(Phase::Resizing);
        else if (m_selection.contains(p))
            setPhase(Phase::Moving);
        return;
    }
    m_anchor = m_corner = clamped(x, y);
    m_keyboardDrag = false;
    setPointer(m_anchor);
    setPhase(Phase::Dragging);
    m_pressedOnWindow = hasHighlight();
    m_dragged = false;
    setSelection({});
}

void OverlayModel::pointerReleased(qreal x, qreal y, int button, int modifiers) {
    if (m_mode == Color) {
        if (active() && button == Qt::LeftButton) {
            setPointer(clamped(x, y));
            pickColor(modifiers & Qt::ShiftModifier);
        }
        return;
    }
    if ((m_phase == Phase::Moving || m_phase == Phase::Resizing) && button == Qt::LeftButton) {
        pointerMoved(x, y, modifiers);
        m_handle = -1;
        setPhase(Phase::Kept);
        return;
    }
    if (m_phase != Phase::Dragging || button != Qt::LeftButton || m_keyboardDrag)
        return;
    pointerMoved(x, y, modifiers);
    if (m_pressedOnWindow && !m_dragged && hasHighlight()) {
        const WindowInfo window = m_windows[m_highlighted];
        if (continuesSequence()) {
            afterSequenceShot();
            emit confirmedWindow(window);
            return;
        }
        setHighlighted(-1);
        finish();
        emit confirmedWindow(window);
        return;
    }
    if (m_selection.isEmpty()) { // a click with no drag picks nothing
        setPhase(Phase::Picking);
        setSelection({});
        updateHighlight(m_pointer);
        return;
    }
    if ((modifiers & Qt::ControlModifier) || m_mode == Record) { // Ctrl-let go keeps it to adjust; Record always does
        setPhase(Phase::Kept);
        return;
    }
    confirm();
}

void OverlayModel::confirm() {
    if (!active() || m_selection.isEmpty())
        return;
    const QRect picked = m_selection;
    const Mode mode = m_mode;
    if (continuesSequence()) {
        afterSequenceShot();
        emit confirmed(picked, mode);
        return;
    }
    if (m_sequence) { // a Record start or a Timer shot ends the Sequence
        m_sequence = false;
        emit sequenceChanged();
    }
    finish();
    emit confirmed(picked, mode);
    m_confirmAsGif = false;
}

void OverlayModel::confirmGif() {
    if (m_mode != Record)
        return;
    m_confirmAsGif = true;
    confirm();
    m_confirmAsGif = false; // confirm() ignored it (nothing selected)
}

void OverlayModel::clearKept() {
    if (!kept())
        return;
    setPhase(Phase::Picking);
    setSelection({});
}

void OverlayModel::pointerDoubleClicked(qreal x, qreal y) {
    if (kept() && m_selection.contains(clamped(x, y)))
        confirm();
}

int OverlayModel::handleAt(qreal x, qreal y) const {
    if (!kept())
        return -1;
    const int l = m_selection.x(), t = m_selection.y();
    const int r = l + m_selection.width(), b = t + m_selection.height();
    const int cx = (l + r) / 2, cy = (t + b) / 2;
    const QPoint centers[8] = {{l, t}, {r, t}, {r, b}, {l, b}, {cx, t}, {r, cy}, {cx, b}, {l, cy}};
    constexpr int reach = 8; // a little more than the 10 px dot's radius
    for (int i = 0; i < 8; ++i)
        if (qAbs(x - centers[i].x()) <= reach && qAbs(y - centers[i].y()) <= reach)
            return i;
    return -1;
}

void OverlayModel::keptKey(int key, int modifiers) {
    int dx = 0, dy = 0;
    switch (key) {
    case Qt::Key_Left: dx = -1; break;
    case Qt::Key_Right: dx = 1; break;
    case Qt::Key_Up: dy = -1; break;
    case Qt::Key_Down: dy = 1; break;
    default: return;
    }
    QRect r = m_selection;
    if (modifiers & Qt::AltModifier) { // Alt+arrows resize from the bottom-right
        r.setSize({qBound(1, r.width() + dx, m_screen.width() - r.x()), qBound(1, r.height() + dy, m_screen.height() - r.y())});
    } else {
        r.moveTo(qBound(0, r.x() + dx, qMax(0, m_screen.width() - r.width())), qBound(0, r.y() + dy, qMax(0, m_screen.height() - r.height())));
    }
    setSelection(r);
}

void OverlayModel::keyPressed(int key, int modifiers) {
    if (!active())
        return;
    if (key == Qt::Key_M && m_mode == Color) // forced on there
        return;
    if (key == Qt::Key_M) { // the Magnifier, in any phase
        setMagnifier(!m_magnifier);
        emit magnifierToggled(m_magnifier);
        return;
    }
    if (key == Qt::Key_B && m_presetAvailable && m_mode == Screenshot &&
        !(modifiers & (Qt::ControlModifier | Qt::AltModifier))) { // skip the auto-applied Preset's Frame
        m_noFrame = !m_noFrame;
        emit noFrameChanged();
        return;
    }
    const bool plain = !(modifiers & (Qt::ControlModifier | Qt::AltModifier));
    if (plain && key == Qt::Key_K && m_mode == Text) { // Paragraphs, Lines, Table
        setTextLayout(nextTextLayout(m_textLayout));
        emit textLayoutToggled(m_textLayout);
        return;
    }
    if (plain && key == Qt::Key_F) { // the whole monitor, in every Mode
        takeFullScreen();
        return;
    }
    if (plain && key == Qt::Key_L) {
        restoreLastArea();
        return;
    }
    if (plain && key >= Qt::Key_1 && key <= Qt::Key_4) {
        setMode(key - Qt::Key_1);
        return;
    }
    if (plain && key == Qt::Key_S) {
        toggleSequence();
        return;
    }
    if (plain && key == Qt::Key_T) {
        toggleTimer();
        return;
    }
    if (m_mode == Color) { // arrows move the pointer 1 px, Enter picks (Shift: rgb)
        if (key == Qt::Key_Shift)
            setMods(Qt::KeyboardModifiers(modifiers) | Qt::ShiftModifier);
        else if (key == Qt::Key_Escape)
            cancel();
        else if (key == Qt::Key_Return || key == Qt::Key_Enter)
            pickColor(modifiers & Qt::ShiftModifier);
        else
            arrowKey(key, modifiers);
        return;
    }
    if (kept()) {
        if (key == Qt::Key_Escape)
            clearKept();
        else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            if (m_mode == Record && (modifiers & Qt::AltModifier))
                confirmGif(); // Alt+Enter is Record GIF
            else
                confirm();
        } else if (m_phase == Phase::Kept)
            keptKey(key, modifiers);
        return;
    }
    if (key == Qt::Key_Escape) {
        cancel();
        return;
    }
    if (m_phase != Phase::Dragging && m_phase != Phase::Picking)
        return;
    // Shift, Alt and Space change the shape of a drag the moment they go down.
    setMods(Qt::KeyboardModifiers(modifiers));
    if (key == Qt::Key_Space) {
        m_space = true;
        return;
    }
    if (key == Qt::Key_Shift || key == Qt::Key_Alt) {
        if (m_phase == Phase::Dragging)
            setSelection(dragRect());
        return;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (m_phase == Phase::Picking) { // Enter, arrows, Enter: start here
            m_anchor = m_corner = m_pointer;
            m_keyboardDrag = true;
            m_pressedOnWindow = false;
            setHighlighted(-1);
            setPhase(Phase::Dragging);
            setSelection({});
        } else if (m_keyboardDrag) {
            if (m_mode == Record && !m_selection.isEmpty()) { // keep it for the Record panel
                m_keyboardDrag = false;
                setPhase(Phase::Kept);
            } else {
                confirm();
            }
        }
        return;
    }
    arrowKey(key, modifiers);
}

void OverlayModel::keyReleased(int key, int modifiers) {
    if (!active())
        return;
    setMods(Qt::KeyboardModifiers(modifiers));
    if (key == Qt::Key_Space) {
        m_space = false;
    } else if (key == Qt::Key_Shift || key == Qt::Key_Alt) {
        // Qt may still report the key in `modifiers` on its own release.
        setMods(m_mods & ~(key == Qt::Key_Shift ? Qt::ShiftModifier : Qt::AltModifier));
        if (m_phase == Phase::Dragging)
            setSelection(dragRect());
    }
}

// Moves the real pointer 1 px (10 with Shift). The model follows at once; the
// Overlay service does the warping, and the pointer events that come back from
// the compositor then agree with it.
void OverlayModel::arrowKey(int key, int modifiers) {
    int dx = 0, dy = 0;
    switch (key) {
    case Qt::Key_Left: dx = -1; break;
    case Qt::Key_Right: dx = 1; break;
    case Qt::Key_Up: dy = -1; break;
    case Qt::Key_Down: dy = 1; break;
    default: return;
    }
    const int step = (modifiers & Qt::ShiftModifier) ? 10 : 1;
    const QPoint to = clamped(m_pointer.x() + dx * step, m_pointer.y() + dy * step);
    if (to == m_pointer)
        return;
    const bool space = m_space;
    m_space = false; // arrows resize; Space moves with the mouse only
    if (m_phase == Phase::Dragging)
        updateDrag(to);
    m_space = space;
    setPointer(to);
    emit pointerWarpRequested(to);
}

void OverlayModel::cancel() {
    if (!active())
        return;
    setSelection({});
    finish();
    emit canceled();
}

} // namespace tarn
