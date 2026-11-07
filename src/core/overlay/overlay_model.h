#pragma once
// The Overlay model: the state of the Selection, and nothing else. It turns
// pointer and key events into a confirmed Selection or a cancel. It knows no
// pixels, windows or surfaces, so every rule can be tested with plain events.
//
// Coordinates are logical pixels with (0, 0) at the top-left of the monitor
// the Overlay is open on. Later tickets grow this class in place: window
// highlight and its stickiness, the kept Selection and Ctrl-let go
//, drag modifiers and keyboard selection, the Magnifier flag
//, the B skip, and the record/timer/text modes. Each
// adds state next to `m_phase` and a branch in the handlers; the event entry
// points and the two outcome signals do not change.
#include "window_info.h"

#include <QColor>
#include <QImage>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

#include "text/text_layout.h"

namespace tarn {

class OverlayModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(Mode mode READ mode NOTIFY modeChanged)
    Q_PROPERTY(bool timer READ timer NOTIFY timerChanged)
    Q_PROPERTY(bool timerAvailable READ timerAvailable NOTIFY modeChanged)
    Q_PROPERTY(QString timerLabel READ timerLabel NOTIFY timerChanged)
    Q_PROPERTY(QRect modeBarTopSpot READ modeBarTopSpot NOTIFY modeBarChanged)
    Q_PROPERTY(bool modeBarAtBottom READ modeBarAtBottom NOTIFY modeBarChanged)
    Q_PROPERTY(QRect modeBarRect READ modeBarRect NOTIFY modeBarChanged)
    Q_PROPERTY(QRect selection READ selection NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(QString sizeLabel READ sizeLabel NOTIFY selectionChanged)
    Q_PROPERTY(int pixelWidth READ pixelWidth NOTIFY selectionChanged)   // the Selection in device px
    Q_PROPERTY(int pixelHeight READ pixelHeight NOTIFY selectionChanged)
    Q_PROPERTY(QString positionLabel READ positionLabel NOTIFY selectionChanged)
    Q_PROPERTY(QPoint pointer READ pointer NOTIFY pointerChanged)
    Q_PROPERTY(qreal pixelScale READ pixelScale CONSTANT)
    Q_PROPERTY(bool magnifier READ magnifier WRITE setMagnifier NOTIFY magnifierChanged)
    Q_PROPERTY(bool colorMode READ colorMode NOTIFY modeChanged)
    Q_PROPERTY(QColor pointerColor READ pointerColor NOTIFY pointerChanged)
    Q_PROPERTY(QPoint magnifierTile READ magnifierTile NOTIFY pointerChanged)
    Q_PROPERTY(QPoint magnifierPill READ magnifierPill NOTIFY pointerChanged)
    Q_PROPERTY(QString magnifierLabel READ magnifierLabel NOTIFY pointerChanged)
    // The window the pointer is on (Area mode only): its outline and app pill.
    Q_PROPERTY(bool hasHighlight READ hasHighlight NOTIFY highlightChanged)
    Q_PROPERTY(QRect highlight READ highlight NOTIFY highlightChanged)
    Q_PROPERTY(QString highlightApp READ highlightApp NOTIFY highlightChanged)
    Q_PROPERTY(qreal highlightRounding READ highlightRounding NOTIFY highlightChanged)
    Q_PROPERTY(bool kept READ kept NOTIFY keptChanged)
    // The Text layout: K cycles it in Text Mode; the Text button shows it.
    Q_PROPERTY(QString textLayoutName READ textLayoutLabel NOTIFY textLayoutChanged)
    Q_PROPERTY(bool recordMode READ recordMode NOTIFY modeChanged)
    Q_PROPERTY(bool noFrame READ noFrame NOTIFY noFrameChanged)
    Q_PROPERTY(bool sequence READ sequence NOTIFY sequenceChanged)
    Q_PROPERTY(int taken READ taken NOTIFY sequenceChanged)
public:
    // The Mode. The Timer is a toggle on Screenshot, not a Mode. Color is
    // listed so the Mode bar can show it grayed; switching to it is ignored
    // until its ticket wires it.
    enum Mode { Screenshot, Record, Text, Color };
    Q_ENUM(Mode)

    explicit OverlayModel(QObject *parent = nullptr) : QObject(parent) {
        // The bar's size follows the Mode and the Text layout; its top or bottom spot
        // follows the Selection.
        connect(this, &OverlayModel::modeChanged, this, &OverlayModel::modeBarChanged);
        connect(this, &OverlayModel::textLayoutChanged, this, &OverlayModel::modeBarChanged);
        connect(this, &OverlayModel::selectionChanged, this, &OverlayModel::modeBarChanged);
    }

    // Starts a fresh pick on a monitor of `screen` logical pixels. `pixelScale`
    // is device pixels per logical pixel, so the size pill shows real pixels.
    // `timer` presets the Timer toggle (Super+Shift+Print); every toggle else starts off.
    void open(Mode mode, const QSize &screen, qreal pixelScale = 1.0, bool timer = false);

    // Switching Mode (keys 1-3, a Mode bar click). A kept Selection stays. The
    // Timer goes off. Color
    // Mode clears the Selection: it picks a pixel, never an area.
    Q_INVOKABLE void setMode(int mode);
    // The Timer (T): Screenshot only. It turns the window highlight off.
    bool timer() const { return m_timer; }
    bool timerAvailable() const { return m_mode == Screenshot; }
    Q_INVOKABLE void toggleTimer();
    void setTimerDelay(double seconds) { m_timerDelay = seconds; emit timerChanged(); }
    QString timerLabel() const; // "3 s"
    // Sequence (S): after each shot the Overlay stays open on the same frozen
    // image, selecting again. It starts off at every open(). Esc clears a kept
    // Selection, then leaves (the Overlay keeps every shot). A Record start or a
    // Timer shot ends it. taken() counts the shots (Screenshot, Text and Color).
    bool sequence() const { return m_sequence; }
    int taken() const { return m_taken; }
    Q_INVOKABLE void toggleSequence();
    // F: the whole monitor, in every Mode (Record keeps it for the Record panel).
    Q_INVOKABLE void takeFullScreen();
    // L: asks the Overlay service for the focused app's Last area; it
    // answers with keepArea().
    Q_INVOKABLE void restoreLastArea();
    // Shows `area` (monitor-local logical px) as a kept Selection to adjust.
    void keepArea(const QRect &area);

    // The Mode bar: a capsule at the top center, or at the bottom edge
    // while a dragged or kept Selection lies within 24 px of its top spot. Only
    // the Selection decides: where the pointer hovers never does.
    static constexpr int kModeBarWidth = 418, kModeBarHeight = 44, kModeBarMargin = 24;
    // Text Mode puts the Text layout's name on the Text button, which hugs it and so
    // widens the bar by this much (the QML draws the same widths; a test keeps them equal).
    static int textButtonExtra(TextLayout layout);
    QRect modeBarTopSpot() const;
    bool modeBarAtBottom() const;
    // Where the Mode bar is now: its top spot, or the same at the bottom edge.
    QRect modeBarRect() const;

    bool active() const { return m_phase != Phase::Closed; }
    Mode mode() const { return m_mode; }
    // Record mode: a finished drag is always kept (the Record panel sits over
    // it), and Enter on the kept Selection starts the Recording.
    bool recordMode() const { return m_mode == Record; }
    QSize screen() const { return m_screen; }
    QRect selection() const { return m_selection; }
    bool hasSelection() const { return !m_selection.isEmpty(); }
    QPoint pointer() const { return m_pointer; }
    qreal pixelScale() const { return m_pixelScale; }

    // The Magnifier. Its on/off survives open() (the Overlay service
    // loads and saves it); M flips it and emits magnifierToggled.
    // In Color Mode it is forced on (M does nothing) and its readout is the color.
    bool magnifier() const { return m_magnifier || m_mode == Color; }
    bool colorMode() const { return m_mode == Color; }
    // Color pick: the frozen image to read the pixel under the pointer from
    // (full resolution; the model otherwise knows no pixels).
    void setPixelSource(const QImage &frozen) { m_pixels = frozen; }
    // The color of the device pixel under the pointer; invalid with no image.
    QColor pointerColor() const;
    // What a Color pick copies: the hex, or `rgb(r, g, b)` with Shift.
    static QString colorText(const QColor &color, bool asRgb);
    // Picks the pixel under the pointer (a click, or Enter), `asRgb` for Shift.
    Q_INVOKABLE void pickColor(bool asRgb);

    // The Text layout. Like the Magnifier it survives open() (the Overlay
    // service loads and saves it); K cycles it in Text Mode and emits
    // textLayoutToggled.
    TextLayout textLayout() const { return m_textLayout; }
    QString textLayoutLabel() const { return textLayoutName(m_textLayout); }
    void setTextLayout(TextLayout layout);

    // The B skip: with an auto-applied Preset (the Overlay service says so
    // through setPresetAvailable), B skips its Frame for THIS capture and the
    // "No frame" pill shows; B again takes the skip back. Area and Timer only (a
    // Recording and a Text capture never get a Frame). open() clears it.
    bool noFrame() const { return m_noFrame; }
    void setPresetAvailable(bool available) { m_presetAvailable = available; }
    void setMagnifier(bool on);
    // Top-left of the 120 px tile, and of the x, y pill under it, in logical
    // px. The tile sits 24 px below-right of the pointer, each axis flipping
    // to the other side where the tile and pill would leave the monitor.
    static constexpr int kMagnifierTile = 120, kMagnifierGap = 24, kMagnifierPillGap = 8, kMagnifierPillHeight = 24,
                         kMagnifierEdge = 4;
    static QPoint magnifierTileAt(const QPoint &pointer, const QSize &screen);
    QPoint magnifierTile() const { return magnifierTileAt(m_pointer, m_screen); }
    QPoint magnifierPill() const { return magnifierTile() + QPoint(0, kMagnifierTile + kMagnifierPillGap); }
    // "x, y" of the pointer in the monitor's device pixels.
    QString magnifierLabel() const;
    // Windows on this monitor, frontmost first, in monitor-local logical pixels.
    // Set after open(); Screenshot and Text use them (Record and the Timer pick areas only).
    void setWindows(const QList<WindowInfo> &windows);
    bool hasHighlight() const { return m_highlighted >= 0; }
    QRect highlight() const { return hasHighlight() ? m_windows[m_highlighted].rect : QRect(); }
    QString highlightApp() const { return hasHighlight() ? m_windows[m_highlighted].app : QString(); }
    qreal highlightRounding() const { return hasHighlight() ? m_windows[m_highlighted].rounding : 0; }
    // The highlight stays on its window while the pointer is within this many
    // pixels of it, so it survives the gap between two windows.
    static constexpr int kStickyPx = 16;
    // True while a Selection is kept on screen to adjust (Ctrl-let go).
    bool kept() const { return m_phase == Phase::Kept || m_phase == Phase::Moving || m_phase == Phase::Resizing; }
    // The Handle (0-7: corners clockwise from top-left, then edge midpoints
    // top, right, bottom, left) under a point of a kept Selection, or -1.
    Q_INVOKABLE int handleAt(qreal x, qreal y) const;
    // "W × H" of the Selection in device pixels.
    QString sizeLabel() const;
    // "x, y" of the Selection's top-left in device pixels (shown while kept).
    QString positionLabel() const;

    // The Selection's size in device pixels (what the pill and the Record panel's W x H show).
    int pixelWidth() const { return qRound(m_selection.width() * m_pixelScale); }
    int pixelHeight() const { return qRound(m_selection.height() * m_pixelScale); }
    // The Record panel's W x H fields: resizes the kept Selection to this many
    // device pixels, top-left fixed, at least 1 px and never past the monitor's edge.
    Q_INVOKABLE void setPixelSize(int width, int height);

    // Events. `button` is a Qt::MouseButton, `key` a Qt::Key, `modifiers` a
    // Qt::KeyboardModifiers, all as ints so QML can call them directly.
    Q_INVOKABLE void pointerMoved(qreal x, qreal y, int modifiers = 0);
    Q_INVOKABLE void pointerPressed(qreal x, qreal y, int button, int modifiers = 0);
    Q_INVOKABLE void pointerReleased(qreal x, qreal y, int button, int modifiers = 0);
    Q_INVOKABLE void keyPressed(int key, int modifiers = 0);
    // Needed while dragging: Shift, Alt and Space change the shape as they go
    // down and up, with no pointer movement to tell the model.
    Q_INVOKABLE void keyReleased(int key, int modifiers = 0);
    Q_INVOKABLE void pointerDoubleClicked(qreal x, qreal y);
    Q_INVOKABLE void cancel();
    // Takes the kept Selection (Enter, double-click, the Capture pill).
    Q_INVOKABLE void confirm();
    // Record mode: Alt+Enter / the Record GIF action. Confirms like confirm(), and
    // confirmedAsGif() is true while the confirmed() signal is being delivered.
    Q_INVOKABLE void confirmGif();
    bool confirmedAsGif() const { return m_confirmAsGif; }
    // Clears the kept Selection and goes back to picking (Esc).
    Q_INVOKABLE void clearKept();

signals:
    void activeChanged();
    void modeChanged();
    void timerChanged();
    void sequenceChanged();
    void selectionChanged();
    void pointerChanged();
    void magnifierChanged();
    void magnifierToggled(bool on); // the user pressed M
    // Color Mode: a pick. The Overlay copies it, saves it and closes.
    void colorPicked(const QColor &color, bool asRgb);
    void textLayoutChanged();
    void modeBarChanged(); // its size or its spot
    void textLayoutToggled(tarn::TextLayout layout); // the user pressed K
    void noFrameChanged();          // B skipped, or un-skipped, the auto-applied Preset
    void highlightChanged();
    void keptChanged();
    void lastAreaRequested();
    // Keyboard selection: the arrow keys want the real pointer at this
    // point (logical px of the monitor). The Overlay service warps it.
    void pointerWarpRequested(const QPoint &point);
    // The two outcomes. After either, the model is closed.
    void confirmed(const QRect &selection, tarn::OverlayModel::Mode mode);
    void canceled();
    // A click on a highlighted window: the outcome instead of confirmed().
    void confirmedWindow(const tarn::WindowInfo &window);

private:
    enum class Phase { Closed, Picking, Dragging, Kept, Moving, Resizing };
    void setSelection(const QRect &rect);
    void setPointer(const QPoint &point);
    QPoint clamped(qreal x, qreal y) const;
    void finish();
    void updateHighlight(const QPoint &p);
    bool highlightAllowed() const { return !m_timer && (m_mode == Screenshot || m_mode == Text); }
    void refreshHighlight();
    void setHighlighted(int index);
    // True when a shot now should leave the Overlay open (Sequence on, not a Record or Timer shot).
    bool continuesSequence() const { return m_sequence && m_mode != Record && !m_timer; }
    void afterSequenceShot();
    void keepSelection();
    void keptKey(int key, int modifiers);
    void setPhase(Phase phase);
    void updateDrag(const QPoint &pointer);
    QRect dragRect() const;
    void arrowKey(int key, int modifiers);

    Phase m_phase = Phase::Closed;
    Mode m_mode = Screenshot;
    bool m_confirmAsGif = false;
    bool m_timer = false;
    bool m_sequence = false;
    int m_taken = 0;
    double m_timerDelay = 3;
    QSize m_screen;
    qreal m_pixelScale = 1.0;
    QPoint m_anchor;
    QPoint m_pointer;
    QRect m_selection;
    bool m_magnifier = false;
    QImage m_pixels;
    TextLayout m_textLayout = TextLayout::Paragraphs;
    bool m_noFrame = false;
    bool m_presetAvailable = false;
    QList<WindowInfo> m_windows;
    int m_highlighted = -1;
    bool m_pressedOnWindow = false; // the press started on the highlight: a click takes the window
    bool m_dragged = false;         // ... unless the pointer then moved more than a click's slop
    QRect m_grabbed;   // the kept Selection when a move or resize began
    QPoint m_grabAt;   // where that press landed
    int m_handle = -1; // the Handle being dragged
    QPoint m_corner;   // the moving corner of a drag (the pointer, shifted by Space)
    Qt::KeyboardModifiers m_mods;
    void setMods(Qt::KeyboardModifiers mods); // Shift turns the Color Mode pill to rgb
    bool m_space = false;
    bool m_keyboardDrag = false; // Enter started this drag, so Shift means 10 px, not square
};

} // namespace tarn
