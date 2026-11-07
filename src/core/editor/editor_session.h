#pragma once
// One open Editor: a working copy of a Thumbnail's Capture document, the current
// tool, the pointer gestures that draw, the zoom and pan, and the ways out (Done,
// cancel, Copy, Save). The QML Editor window and the tests drive the same calls.
//
// Coordinates: drawing takes the shot's own pixels (a canvas converts with
// viewToDoc). Zoom and pan are in view pixels, the canvas area under the bars.
#include "annotations/callout.h"
#include "document/capture_document.h"
#include "tool_options.h"

#include <QColor>
#include <QImage>
#include <QtMath>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QSize>
#include <QSizeF>
#include <QStringList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <optional>
#include <memory>

namespace tarn {

class Daemon;
class Editor;

class EditorSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(int captureId READ captureId CONSTANT)
    Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(QVariantList tools READ tools CONSTANT)
    Q_PROPERTY(bool handAvailable READ handAvailable NOTIFY viewChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(QVariantList swatches READ swatches CONSTANT)
    Q_PROPERTY(QVariantList savedColors READ savedColors NOTIFY savedColorsChanged)
    Q_PROPERTY(bool frameIsPlain READ frameIsPlain NOTIFY documentChanged)
    Q_PROPERTY(bool eyedropper READ eyedropper NOTIFY eyedropperChanged)
    Q_PROPERTY(QString sizeGroup READ sizeGroup NOTIFY sizeChanged)
    Q_PROPERTY(QVariantList sizeSteps READ sizeSteps NOTIFY sizeChanged)
    Q_PROPERTY(int sizeIndex READ sizeIndex WRITE setSizeIndex NOTIFY sizeChanged)
    Q_PROPERTY(qreal size READ size NOTIFY sizeChanged)
    Q_PROPERTY(QString sizeLabel READ sizeLabel NOTIFY sizeChanged)
    Q_PROPERTY(QString arrowStyle READ arrowStyle WRITE setArrowStyle NOTIFY arrowStyleChanged)
    Q_PROPERTY(int counterStyle READ counterStyle WRITE setCounterStyle NOTIFY counterChanged)
    Q_PROPERTY(int counterStart READ counterStart WRITE setCounterStart NOTIFY counterChanged)
    Q_PROPERTY(QString counterPreview READ counterPreview NOTIFY counterChanged)
    Q_PROPERTY(QVariantList choices READ choices NOTIFY choicesChanged)
    Q_PROPERTY(QSize openingSize READ openingSize CONSTANT)
    Q_PROPERTY(QSize documentSize READ documentSize NOTIFY documentChanged)
    Q_PROPERTY(int annotationCount READ annotationCount NOTIFY documentChanged)
    Q_PROPERTY(QVariantList selection READ selectionList NOTIFY selectionChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList chrome READ chrome NOTIFY chromeChanged)
    Q_PROPERTY(int cursor READ cursor NOTIFY chromeChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY documentChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY documentChanged)
    Q_PROPERTY(bool fit READ fit NOTIFY viewChanged)
    Q_PROPERTY(qreal zoom READ zoom NOTIFY viewChanged)
    Q_PROPERTY(int zoomPercent READ zoomPercent NOTIFY viewChanged)
    Q_PROPERTY(QPointF origin READ origin NOTIFY viewChanged)
    Q_PROPERTY(QVariantMap frame READ frameMap NOTIFY documentChanged)
    Q_PROPERTY(QVariantMap frameRanges READ frameRanges CONSTANT)
    // Crop mode. Widths and heights are what the user sees (turned).
    Q_PROPERTY(bool cropping READ cropping NOTIFY cropChanged)
    Q_PROPERTY(bool cropped READ cropped NOTIFY cropChanged)
    Q_PROPERTY(int cropWidth READ cropWidth NOTIFY cropChanged)
    Q_PROPERTY(int cropHeight READ cropHeight NOTIFY cropChanged)
    Q_PROPERTY(QString cropAspect READ cropAspect NOTIFY cropChanged)
    Q_PROPERTY(QStringList cropPresets READ cropPresets CONSTANT)
    Q_PROPERTY(qreal outputScale READ outputScale NOTIFY cropChanged)
    Q_PROPERTY(QSize outputSize READ outputSize NOTIFY cropChanged)
    Q_PROPERTY(QVariantList cropChrome READ cropChrome NOTIFY cropChanged)
    // Text
    Q_PROPERTY(bool textEditing READ textEditing NOTIFY textEditingChanged)
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    Q_PROPERTY(QPointF textOrigin READ textOrigin NOTIFY textEditingChanged)
    Q_PROPERTY(qreal textSize READ textSize NOTIFY textStyleChanged)
    Q_PROPERTY(int textLook READ textLook WRITE setTextLook NOTIFY textStyleChanged)
public:
    static constexpr qreal kMinZoom = 0.25;
    static constexpr qreal kMaxZoom = 4.0;

    EditorSession(Editor &editor, int captureId, std::unique_ptr<CaptureDocument> document, const QSize &openingSize,
                  const QString &historyId = {});

    int captureId() const { return m_captureId; }
    // Opened from the History window: the History entry it edits. Such an
    // Editor has no Thumbnail; every close that keeps edits writes the Project file.
    bool fromHistory() const { return !m_historyId.isEmpty(); }
    QString historyId() const { return m_historyId; }
    // The window's title, its own for each Editor ("Tarn editor #3"), so Hyprland can be
    // asked to focus this one: Tarn's rule matches ^(Tarn editor.*)$.
    QString windowTitle() const { return m_windowTitle; }
    bool isOpen() const { return m_open; }
    CaptureDocument &document() { return *m_document; }
    const CaptureDocument &document() const { return *m_document; }
    // The Annotation being drawn right now (painted over the document), or null.
    const Annotation *draft() const { return m_draft.get(); }
    QSize openingSize() const { return m_openingSize; }
    QSize documentSize() const { return m_document->viewSize(!m_cropping); }
    int annotationCount() const { return m_document->count(); }

    // Tools. Every Editor opens on Rectangle. "select" is the pointer tool: it
    // never draws, and a drag on empty canvas selects a box of Annotations.
    QString tool() const { return m_tool; }
    void setTool(const QString &name);
    // The Hand tool is a view tool (G): left-drag pans. It exists only while the shot
    // is larger than the view (zoomed past Fit); back at Fit it hands over to the tool before it.
    bool handAvailable() const;
    QVariantList tools() const; // [{name, label, icon, key}] in capsule order
    // What the current tool draws with (the shared color and sizes).
    ToolStyle style() const;

    // Color: ONE for every tool, remembered. Setting it also recolors the
    // selected Annotations (setSelection).
    QColor color() const;
    void setColor(const QColor &color);
    QVariantList swatches() const; // the 8 fixed swatches, as "#rrggbb"
    // Saved colors: the shared row, newest first, as "#aarrggbb". The popover's + saves
    // the current color; the Frame panel's + saves the Frame's plain color (frameIsPlain is
    // false when the Frame is off or not plain). The Eyedropper never saves.
    QVariantList savedColors() const;
    bool frameIsPlain() const;
    Q_INVOKABLE void saveCurrentColor();
    Q_INVOKABLE void saveFrameColor();
    Q_INVOKABLE void removeSavedColor(int index);
    // The eyedropper samples the shot itself (never the Annotations): after
    // beginEyedropper() the next press on the canvas picks that pixel instead of drawing.
    bool eyedropper() const { return m_eyedropper; }
    Q_INVOKABLE void beginEyedropper();
    Q_INVOKABLE void cancelEyedropper();
    Q_INVOKABLE QColor sampleShot(qreal x, qreal y) const; // invalid outside the shot
    // Recolors the given Annotations with `color`; returns how many changed.
    int recolor(const QList<int> &ids, const QColor &color);
    // A custom-color slider drag: like setColor, but the selection's recolor is live and
    // only recorded (as ONE step) by endColorDrag(), on release or when the popover closes.
    Q_INVOKABLE void dragColor(const QColor &color);
    Q_INVOKABLE void endColorDrag();

    // Size: the current tool's group (empty when it has no size) and its six steps.
    QString sizeGroup() const;
    QVariantList sizeSteps() const;
    int sizeIndex() const;
    QString arrowStyle() const;
    void setArrowStyle(const QString &name);
    void setSizeIndex(int index);
    qreal size() const;
    QString sizeLabel() const; // "6 px"
    Q_INVOKABLE void stepSize(int delta); // [ is -1, ] is +1

    // Counter: the style (0 = 1 2 3, 1 = A B C, 2 = i ii iii) and the
    // value of the first number. Both are remembered. counterPreview is the
    // label the next click would place ("3", "C", "iii").
    int counterStyle() const;
    void setCounterStyle(int style);
    int counterStart() const;
    void setCounterStart(int start);
    QString counterPreview() const;
    // The current tool's extra selects (Pixelate style and strength, Spotlight
    // shape): [{name, tip, current, label, options:[{v, label}]}].
    QVariantList choices() const;
    Q_INVOKABLE void setChoice(const QString &name, const QString &value);

    // The pointer, in shot pixels. A press on a Handle of the selection resizes
    // it; on an Annotation selects it (Shift toggles, Alt duplicates) and starts
    // moving the selection (Shift locks one axis); on empty canvas it draws with
    // the current tool, or with the Select tool drags a selection box. A release
    // commits: a drawn Annotation (a drag too small to be one commits nothing)
    // or ONE undo step for the whole move or resize.
    Q_INVOKABLE void pointerPressed(qreal x, qreal y, int modifiers = 0);
    Q_INVOKABLE void pointerMoved(qreal x, qreal y, int modifiers = 0);
    Q_INVOKABLE void pointerReleased(qreal x, qreal y, int modifiers = 0);
    // The pointer moved with no button down: hover ghost and resize pointers.
    Q_INVOKABLE void pointerHovered(qreal x, qreal y);
    Q_INVOKABLE void pointerLeft();
    bool drawing() const { return m_mode == Mode::Drawing; }
    bool gestureActive() const { return m_mode != Mode::None; }

    // ---- Selection and editing. Every change is one undo step. ----
    // The selected Annotations (document ids), bottom first. Setting a color
    // recolors them (one undo step).
    const QList<int> &selection() const { return m_selection; }
    void setSelection(const QList<int> &ids);
    QVariantList selectionList() const;
    int selectionCount() const { return int(m_selection.size()); }
    void select(const QList<int> &ids);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void deselect();
    // Esc: deselects first; with nothing selected, cancels the Editor.
    Q_INVOKABLE void escape();
    Q_INVOKABLE void deleteSelection();
    Q_INVOKABLE void duplicateSelection();
    // Arrow keys: 1 px, or 10 px with Shift (`big`), in shot pixels.
    Q_INVOKABLE void nudge(int dx, int dy, bool big = false);
    Q_INVOKABLE void bringForward();
    Q_INVOKABLE void sendBackward();
    // Copy and paste of Annotations between Editors (not the system clipboard).
    Q_INVOKABLE bool copySelection();
    Q_INVOKABLE void paste();

    // ---- Add image: an image becomes a movable, resizable Annotation, selected,
    // one undo step. It lands at its own size, centered on `center` (default: the
    // canvas center); where it reaches past the shot the canvas grows. Each returns
    // false when nothing was added (an unreadable file says why in a notification).
    bool addImage(const QImage &image, const QPointF &center = QPointF(qQNaN(), qQNaN()));
    Q_INVOKABLE bool addImageFile(const QString &pathOrUrl, qreal centerX = qQNaN(), qreal centerY = qQNaN());
    // Ctrl+V: a picture on the system clipboard becomes an added image; otherwise
    // Annotations copied in Tarn are pasted (paste()).
    Q_INVOKABLE bool pasteClipboard();
    // I and the toolbar button: asks the window to show its file dialog, which
    // calls addImageFile with the choice (QML; tests call addImageFile directly).
    Q_INVOKABLE void chooseImage() { emit imageFileRequested(); }
    // Unlimited. They run whatever was recorded in document().history(): Annotation
    // edits now, Crop and Frame when they exist.
    bool canUndo() const { return m_document->history().canUndo(); }
    bool canRedo() const { return m_document->history().canRedo(); }
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // The Frame, as plain values for the Background panel (Frame::toMap: on, kind,
    // color, padding, inset, autoBalance, shadow, corners, ah, av, ratio). Each setter
    // is one undo step. A slider drag calls dragFrameValue as it moves and
    // endFrameDrag on release: the whole drag is one step.
    QVariantMap frameMap() const { return m_document->frame().toMap(); }
    // The largest fraction each Frame slider reaches (its full travel).
    QVariantMap frameRanges() const {
        return {{"padding", Frame::kMaxPadding}, {"inset", Frame::kMaxInset}, {"shadow", Frame::kMaxShadow}, {"corners", Frame::kMaxCorners},
                {"wmMinSize", Watermark::kMinSize}, {"wmMaxSize", Watermark::kMaxSize}, {"wmMinOpacity", Watermark::kMinOpacity}};
    }
    Q_INVOKABLE void setFrameBackground(const QString &kind, const QVariantMap &params = {}); // "none" = no Frame
    // Like setFrameBackground while a custom color is being dragged: not recorded until endFrameDrag().
    Q_INVOKABLE void dragFrameBackground(const QString &kind, const QVariantMap &params);
    // Gradients, wallpapers and blurred tiles (src/core/editor/editor_session_backgrounds.cpp).
    // Each list entry is {id | path, label, params}: `params` goes to setFrameBackground
    // with the kind named by the setFrame… call, and `tile` is an image://background/ URL
    // for the panel's tile.
    Q_PROPERTY(QVariantList gradients READ gradients CONSTANT)
    Q_PROPERTY(QVariantList paletteGradients READ paletteGradients NOTIFY backgroundsChanged)
    Q_PROPERTY(QVariantList wallpapers READ wallpapers NOTIFY backgroundsChanged)
    Q_PROPERTY(QVariantList blurTiles READ blurTiles CONSTANT)
    QVariantList gradients() const;
    // Empty while no Palette file is in use (the Palette is only its fallback colors then).
    QVariantList paletteGradients() const;
    // The current wallpaper, then the wallpaper folder's images.
    QVariantList wallpapers() const;
    QVariantList blurTiles() const;
    Q_INVOKABLE void setFrameGradient(const QString &id);
    Q_INVOKABLE void setFramePaletteGradient(const QString &id);
    Q_INVOKABLE void setFrameBlur(const QString &style);
    // A wallpaper by path or file:// URL ("+" answers with a URL). `current` marks the
    // user's current wallpaper so a Preset can follow it later. False: not a readable image.
    Q_INVOKABLE bool setFrameWallpaper(const QString &pathOrUrl, bool current = false);
    // "+": the panel opens the system file dialog.
    Q_INVOKABLE void chooseBackgroundImage() { emit backgroundImageRequested(); }
    // The panel opened: look at the wallpaper and the folder again.
    Q_INVOKABLE void refreshBackgrounds() { emit backgroundsChanged(); }
    // A tile for any Background (what the image provider calls); the shot for blurred tiles.
    QImage backgroundTile(const QString &kind, const QVariantMap &params, const QSize &size) const;
    // Presets (src/core/editor/editor_session_presets.cpp): saved Frames in
    // ~/.config/tarn/presets.toml. Each list entry is {name, auto (marked to frame
    // every capture), missing (its wallpaper picture is gone: the quiet mark)}.
    Q_PROPERTY(QVariantList presets READ presets NOTIFY backgroundsChanged)
    QVariantList presets() const;
    Q_INVOKABLE bool savePreset(const QString &name);   // the current Frame, under that name
    Q_INVOKABLE bool applyPreset(const QString &name);  // one undo step; follows the wallpaper
    Q_INVOKABLE bool deletePreset(const QString &name);
    Q_INVOKABLE bool setAutoPreset(const QString &name); // "" = no Preset on every capture
    // The Watermark section: keys wmMode ("off"|"image"|"text"), wmText, wmCorner
    // (0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right), wmSize and wmOpacity go through
    // setFrameValue / dragFrameValue. The image chooser calls this with the chosen file; false
    // (and a notification) when it is not a readable picture.
    Q_INVOKABLE bool setWatermarkImage(const QString &pathOrUrl);
    // The Watermark button and W: see editor_session_frame.cpp. watermarkSetupRequested:
    // nothing is remembered, so the panel should open at the Watermark section.
    Q_INVOKABLE void toggleWatermark();
    Q_INVOKABLE void setFrameValue(const QString &key, const QVariant &value);
    Q_INVOKABLE void dragFrameValue(const QString &key, const QVariant &value);
    Q_INVOKABLE void endFrameDrag();

    // The selection's chrome, in view pixels (screen size, drawn above the
    // Annotations): a list of {type: "outline"|"dot"|"marquee", x, y, w, h,
    // opacity, hot, handle}. Includes the 40% ghost of a hovered, unselected
    // Annotation. cursor() is the Qt::CursorShape for the pointer's position.
    QVariantList chrome() const;
    int cursor() const;
    int hoveredAnnotation() const { return m_hoverId; }

    // Zoom and pan. It opens at Fit, never above 100%.
    void setViewport(const QSizeF &size);
    bool fit() const { return m_fit; }
    qreal zoom() const;
    int zoomPercent() const { return qRound(zoom() * 100); }
    QPointF origin() const; // where the shot's top-left sits in the view
    Q_INVOKABLE void setViewportSize(qreal width, qreal height) { setViewport({width, height}); }
    Q_INVOKABLE void zoomFit();
    Q_INVOKABLE void zoomTo(qreal zoom); // about the view's center
    Q_INVOKABLE void zoomAt(qreal zoom, qreal viewX, qreal viewY);
    Q_INVOKABLE void zoomIn();  // next of 25, 50, 100, 200, 400 %
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void wheelZoom(qreal angleDeltaY, qreal viewX, qreal viewY); // Ctrl+scroll
    Q_INVOKABLE void panBy(qreal dx, qreal dy);
    Q_INVOKABLE QPointF viewToDoc(qreal x, qreal y) const;
    Q_INVOKABLE QPointF docToView(qreal x, qreal y) const;
    QPointF pan() const { return m_pan; }

    // The ways out. Each returns false when it failed (the stack has said why
    // in a notification) and the Editor stays open.
    // Done (Enter, Ctrl+Enter): back to the Thumbnail with the edits, still editable.
    Q_INVOKABLE void done();
    // Esc: the Editor closes, its edits are dropped, the Thumbnail is as it was.
    Q_INVOKABLE void cancel();
    // Copy (Ctrl+C): copies the flattened image, closes the Editor and the Thumbnail.
    Q_INVOKABLE bool copyAndClose();
    // Ctrl+Shift+C: copies and stays open.
    Q_INVOKABLE bool copyKeepOpen();
    // Ctrl+S: saves to the screenshot folder, closes both.
    Q_INVOKABLE bool saveAndClose();
    // Save as… (Ctrl+Shift+S): the chosen path, closes both.
    Q_INVOKABLE bool saveAs(const QString &path);
    Q_INVOKABLE QString saveAsFolder() const;
    Q_INVOKABLE QString saveAsSuggestion() const; // folder + File name pattern + Image format extension
    Q_INVOKABLE int saveAsFilterIndex() const;    // 0 PNG, 1 JPEG, 2 WebP
    // Pin (Ctrl+P): floats the edited image, closes the Editor and the Thumbnail.
    Q_INVOKABLE bool pin();

    // ---- Crop (editor_session_crop.cpp) ----
    // C enters Crop mode, which swaps the toolbar and shows the whole canvas with
    // the Crop drawn over it; Esc or Enter leaves it. Every change is applied at
    // once and is ONE undo step in the document's history. Nothing is destroyed:
    // widening the Crop again brings the cut pixels and Annotations back.
    bool cropping() const { return m_cropping; }
    bool cropped() const { return !m_document->crop().isIdentity(); }
    Q_INVOKABLE void enterCrop();
    Q_INVOKABLE void leaveCrop();
    int cropWidth() const;
    int cropHeight() const;
    QString cropAspect() const { return m_aspect; }
    QStringList cropPresets() const;
    // "Freeform", "Original" or "a:b" from cropPresets(): sets the Crop to the
    // largest such rectangle inside the current one (exact integers) and keeps
    // later drags and fields on that ratio.
    Q_INVOKABLE void setCropAspect(const QString &name);
    // The W x H fields: set the Crop's size exactly (top-left stays, shifted in if
    // it would leave the canvas). With a ratio chosen the other side follows.
    Q_INVOKABLE void setCropWidth(int width);
    Q_INVOKABLE void setCropHeight(int height);
    Q_INVOKABLE void rotateCrop(int direction); // +1 clockwise, -1 counter-clockwise
    Q_INVOKABLE void flipCropH();
    Q_INVOKABLE void flipCropV();
    qreal outputScale() const { return m_document->crop().scale; }
    QSize outputSize() const { return m_document->size(); }
    Q_INVOKABLE void setOutputScale(qreal scale);
    Q_INVOKABLE void resetCrop();
    // The overlay for Crop mode in view pixels: {type: "dim"|"frame"|"dot", x, y, w, h, hot, handle}.
    QVariantList cropChrome() const;

    // ---- Text ----
    // A Text is typed in a text field the QML puts over the canvas; this is what
    // that field and the tests drive. beginText(x, y) starts a new Text at a
    // point in shot pixels; editText(id) edits a Text again. setText(...) is the
    // field's whole contents, so far (Enter makes a newline in it). finishText()
    // (Esc, a click elsewhere, Done, a tool change) puts the Text in the document
    // as one undo step; an empty Text is dropped.
    bool textEditing() const { return m_textActive; }
    QString text() const { return m_text; }
    QPointF textOrigin() const { return m_textOrigin; }
    qreal textSize() const;
    // 0 plain, 1 outlined, 2 label. Remembered; also restyles the selected Texts.
    int textLook() const;
    void setTextLook(int look);
    void setText(const QString &text);
    Q_INVOKABLE void beginText(qreal x, qreal y);
    Q_INVOKABLE bool editText(int id);
    // Double-click: edits the Text under the point, if there is one.
    Q_INVOKABLE bool editTextAt(qreal x, qreal y);
    // Double-click on a selected arrow's Bend Handle (shot pixels): straightens it, one undo step.
    Q_INVOKABLE bool straightenAt(qreal x, qreal y);
    Q_INVOKABLE void finishText();
    // Enter on a selected Callout edits its text again; false when the selection is not one.
    Q_INVOKABLE bool editSelectedCallout();

signals:
    void cropChanged();
    void textEditingChanged();
    void textChanged();
    void textStyleChanged();
    void toolChanged();
    void colorChanged();
    void savedColorsChanged();
    void eyedropperChanged();
    void sizeChanged();
    void arrowStyleChanged();
    void counterChanged();
    void choicesChanged();
    void selectionChanged();
    void imageFileRequested();
    void backgroundImageRequested();
    void watermarkSetupRequested();
    // The wallpapers or the Palette changed: the panel's lists are different.
    void backgroundsChanged();
    // Chrome or cursor changed (selection, hover, a gesture, zoom).
    void chromeChanged();
    // The document or the draft changed: the canvas repaints.
    void documentChanged();
    void viewChanged();
    // The Editor is over (any way out): its window goes.
    void finished();

private:
    friend class Editor;
    // The stack closed the Thumbnail (or it is being left): no further stack calls.
    void end();
    void applyEdits();
    void revertEdits();
    void writeProject(const QString &historyId = {});
    // Edit from History: the closes that act on the finished picture, then close.
    enum class HistoryAction { Copy, Save, SaveAs, Pin };
    bool historyAction(HistoryAction action, const QString &path = {});
    void closeHistorySession();
    void clampPan();
    QSizeF docSize() const { return QSizeF(m_document->viewSize(!m_cropping)); }
    QPointF viewToDisplay(const QPointF &view) const;
    QPointF displayToView(const QPointF &display) const;
    // Crop mode internals (editor_session_crop.cpp). "Oriented" = the display of
    // the whole canvas, which is what the user drags the Crop on.
    void cropPressed(qreal x, qreal y, int modifiers);
    void cropMoved(qreal x, qreal y, int modifiers);
    void cropReleased(qreal x, qreal y, int modifiers);
    void cropHovered(qreal x, qreal y);
    QRect orientedCrop() const;
    QRect orientedCanvas() const;
    void applyOrientedCrop(const QRect &oriented, bool record);
    void applyCrop(const Crop &crop, const QString &text);
    QRectF draggedCrop(const QPointF &to) const;
    int cropHandleAt(const QPointF &oriented) const;
    void cropEdited();
    void ratioOf(const QString &name, int &w, int &h) const;
    int cropCursor() const;
    qreal fitZoom() const;
    DragGesture gesture(qreal x, qreal y, int modifiers) const;

    enum class Mode { None, Drawing, Moving, Resizing, Marquee, Pulling };
    // Called after the document changed behind the selection (any edit, undo, redo).
    void changed();
    // The Frame changed size or look: repaint and keep the view in range.
    void frameChanged();
    void rememberWatermark(const Watermark &watermark, bool soon);
    bool applyFrameValue(Frame &frame, const QString &key, const QVariant &value) const;
    int topHit(const QPointF &doc) const;
    // Handle points of the current selection in shot pixels.
    QRectF selectionFrame() const;
    QList<QPointF> handlePoints() const;
    int handleAt(const QPointF &doc) const;
    void chromeFor(const QList<int> &ids, qreal opacity, int hotHandle, QVariantList &out) const;
    void applyMove(const QPointF &p, int modifiers);
    void applyResize(const QPointF &p, int modifiers);
    void applyMarquee(const QPointF &p);
    void refreshHover();

    Editor &m_editor;
    int m_captureId;
    QString m_historyId;
    QString m_windowTitle;
    std::unique_ptr<CaptureDocument> m_document;
    QSize m_openingSize;
    bool m_open = true;
    QByteArray m_editedBefore; // the Thumbnail's edits when the Editor opened
    bool m_pushedEdits = false;
    std::optional<CaptureDocument::Snapshot> m_colorBeforeDrag; // set while a color slider is being dragged
    std::optional<Frame> m_frameBeforeDrag; // set while a Frame slider is being dragged

    QString m_tool;
    QString m_toolBeforeHand;
    ToolOptions &m_options;
    bool m_eyedropper = false;
    std::unique_ptr<Annotation> m_draft;
    Mode m_mode = Mode::None;
    QPointF m_dragFrom;
    QList<QPointF> m_path; // the points of the current Drawing drag

    QList<int> m_selection;
    int m_hoverId = -1;     // an unselected Annotation under the pointer: its ghost shows
    int m_hoverHandle = -1; // the Handle under the pointer
    bool m_overSelected = false;
    bool m_hoverValid = false;
    QPointF m_hoverPoint;
    // A move, resize or marquee in progress.
    CaptureDocument::Snapshot m_before;
    QRectF m_beforeFrame;
    int m_gestureHandle = -1;
    QPointF m_gestureGrab; // the dot of an arrow or line minus the press: a click on it moves nothing
    bool m_gestureChanged = false;
    int m_toggleOffOnRelease = -1; // Shift-press on a selected Annotation: leaves the selection on a click
    QList<int> m_marqueeBase;
    QRectF m_marquee;

    // Crop mode.
    enum class CropDrag { None, Move, Handle, Draw };
    bool m_cropping = false;
    QString m_aspect = QStringLiteral("Freeform");
    CropDrag m_cropDrag = CropDrag::None;
    int m_cropHandle = -1;
    int m_cropHover = -1;
    QPointF m_cropFrom;
    QRectF m_cropFrame; // the oriented Crop when the drag began
    Crop m_cropBefore;
    bool m_cropMoved = false;
    void restyleSelection(const QString &what);
    // Text, see editor_session_text.cpp.
    void connectText();
    bool isText(int id) const;
    void rebuildText(bool restyle = false);
    void textOptionChanged(bool sizeChanged);
    // Callouts, see editor_session_callout.cpp.
    void pullPressed(int counterId, const QPointF &p);
    void pullMoved(const QPointF &p);
    void pullReleased(const QPointF &p);
    bool isCallout(int id) const;
    bool hasCallout(int counterId) const;
    void finishNewCallout(bool empty);
    int m_pullCounter = -1;                            // the Counter being pulled from, or -1 for empty picture
    std::shared_ptr<const Annotation> m_pullNew;       // the Counter a drag from empty picture places
    int m_pullOrdinal = 0;                             // its place in its run
    std::unique_ptr<CalloutAnnotation> m_calloutProto; // a new Callout whose text is being typed (then m_draft shows it)
    int m_leaderMoveWith = -1;                         // dragging a leader: the Counter that moves along
    bool m_textActive = false;
    int m_textId = -1; // the Text being edited again, or -1 for a new one (then it is m_draft)
    QPointF m_textOrigin;
    QString m_text;
    CaptureDocument::Snapshot m_textBefore;
    bool m_editOnRelease = false;

    QSizeF m_viewport;
    bool m_fit = true;
    qreal m_zoom = 1;
    QPointF m_pan;
};

} // namespace tarn
