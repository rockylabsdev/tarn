#pragma once
// An Annotation: one thing drawn on the shot. This is the extension point for
// every drawing tool. A new tool is mostly its own file in src/core/annotations/:
// a subclass of Annotation plus one static RegisterAnnotationKind. Nothing else
// needs editing: the tool capsule, its key and the drawing gesture are all read
// from the registry.
//
// Rendering is shared. An Annotation paints itself with a QPainter in shot
// pixels (see paint). The on-screen canvas and the flattened export both call
// the same CaptureDocument::paint, so what is drawn is what is exported.
#include <QColor>
#include <QJsonObject>
#include <QImage>
#include <QList>
#include <QMap>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <functional>
#include <memory>
#include <optional>

namespace tarn {

// What a tool draws with: the one shared color and the current size of each
// size group (ToolOptions fills it; each opens on its third step). Styles and
// strengths join here with the tools that have them.
struct ToolStyle {
    QColor color{0xFF, 0x45, 0x3A}; // iOS red, the default Annotation color
    qreal lineWidth = 6;             // "line" group, px: 2, 4, 6, 10, 16, 24
    qreal textSize = 30;             // "text" group, pt: 16, 22, 30, 40, 56, 80
    qreal counterSize = 20;          // "counter" group, pt: 11, 14, 20, 24, 31, 45
    qreal highlighterSize = 26;      // "highlighter" group, px: 12, 18, 26, 36, 50, 70
    int textLook = 0;                // Text: 0 plain, 1 outlined, 2 label
    QString arrowStyle = QStringLiteral("tapered"); // Arrow: "tapered", "open" or "double"
    int counterStyle = 0;            // Counter: 0 = 1 2 3, 1 = A B C, 2 = i ii iii
    int counterStart = 1;            // Counter: the value of its first number
    // The current value of each tool's choices ("pixelate_style" -> "blur"; see
    // AnnotationKind::choices). ToolOptions fills it for every registered tool.
    QMap<QString, QString> choices;
    QString choice(const QString &name, const QString &fallback = {}) const { return choices.value(name, fallback); }
};

// One drag with a tool, in shot pixels.
struct DragGesture {
    QPointF from;
    QPointF to;
    Qt::KeyboardModifiers modifiers;
    ToolStyle style;
    QList<QPointF> path; // every point of the drag so far, from first to last (Pencil, Highlighter)
};

class Annotation {
public:
    // Pixelate hides only the picture and Annotations stay on top, so the
    // document paints every Picture-pass Annotation before any Marks one.
    // Dim sits between: the one dim layer of every Spotlight, over the picture
    // (and Pixelate) but under the Marks.
    enum Pass { Picture, Marks, Dim };

    virtual ~Annotation() = default;
    virtual QString kind() const = 0;
    virtual std::unique_ptr<Annotation> clone() const = 0;
    // What the Project file keeps of it (see annotation_json.h); the kind rebuilds
    // it with the loader it registers (RegisterAnnotationLoader). The document
    // stores the kind, id and stacking order itself. A kind with no registered
    // loader is left out of the Project file.
    virtual QJsonObject toJson() const { return {}; }
    // Paints in shot pixels. The painter has antialiasing on and is already
    // scaled for the canvas zoom; `shot` is the untouched picture, for tools
    // that read it (Pixelate, Spotlight).
    virtual void paint(QPainter &painter, const QImage &shot) const = 0;
    // The area it covers, in shot pixels (hit tests, and growing the canvas
    // under an added image).
    virtual QRectF bounds() const = 0;
    virtual Pass pass() const { return Marks; }
    // Paints for an export (Copy, Save, the Thumbnail). The default is paint().
    // Pixelate overrides it to draw fresh noise from `seed`, so two exports of
    // the same area differ and the pixelation cannot be reversed.
    virtual void paintFlat(QPainter &painter, const QImage &shot, quint64 /*seed*/) const { paint(painter, shot); }
    // Dim pass only: the shape left undimmed, in shot pixels. The document
    // merges every Dim Annotation's cutout and darkens the rest once.
    virtual QPainterPath cutout(const QSizeF & /*shot*/) const { return {}; }
    // An added image says yes: where its bounds reach past the shot, the canvas
    // grows to hold them (CaptureDocument::canvas). Drawn marks are cut at the edge.
    virtual bool growsCanvas() const { return false; }

    // ---- Selecting and editing ----
    // Which Handles a selected Annotation shows: a Box has eight dots round its
    // frame, Ends one dot at each end of a line or arrow, and Outline only an
    // outline (Text, Counter, Pencil, Highlighter: they scale by their size, not
    // by dragging).
    enum class HandleKind { Box, Ends, Outline };
    virtual HandleKind handleKind() const { return HandleKind::Box; }
    // Moves the whole Annotation.
    virtual void translate(const QPointF &delta) = 0;
    // Box: the rectangle its eight Handles sit on, and how a Handle drag resizes it.
    virtual QRectF frame() const { return bounds(); }
    virtual void setFrame(const QRectF &) {}
    // Ends: the two end points, and moving one of them.
    virtual QList<QPointF> ends() const { return {}; }
    virtual void setEnd(int /*index*/, const QPointF &) {}
    // Bend: an Ends Annotation that curves shows one more Handle, number 2,
    // at the middle of its curve. bendHandle() is where it sits (none: no bend).
    // dragBend() moves the middle of the curve to `point`; a bend within `snap`
    // of the straight line becomes zero. straighten() zeroes it (true if it changed).
    static constexpr int kBendHandle = 2;
    virtual std::optional<QPointF> bendHandle() const { return std::nullopt; }
    virtual void dragBend(const QPointF & /*point*/, qreal /*snap*/) {}
    virtual bool straighten() { return false; }
    // Is a click at `point` on this Annotation? `tolerance` is a few screen
    // pixels in shot pixels. The default is the whole bounds; an outline-only
    // shape narrows it to the stroke.
    virtual bool hit(const QPointF &point, qreal tolerance) const {
        return bounds().adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(point);
    }
    // Recolors it (the shared color was picked while it is selected). A tool
    // with a color overrides this and keeps its own alpha rules.
    virtual void setColor(const QColor &) {}
    // A tool option changed while this Annotation was selected. `what`
    // names the option: a size group ("line", "counter", "highlighter"), a
    // ToolChoice name ("pixelate_style", "spotlight_shape") or "arrow_style",
    // "counter_style", "counter_start". `style` holds the new values. Take the
    // one that applies to you; return true if you changed. (Text restyles
    // through the session's own text path.)
    virtual bool restyle(const QString & /*what*/, const ToolStyle & /*style*/) { return false; }

    // ---- Numbered Annotations ----
    // A non-empty series puts the Annotation in a numbered run. The document
    // counts the run in placement (id) order and passes each its ordinal
    // (0 for the first) when painting, so deleting one renumbers the rest and
    // undo brings the old numbers back: nothing is stored.
    virtual QString series() const { return {}; }
    virtual void paintNumbered(QPainter &painter, const QImage &shot, int /*ordinal*/) const { paint(painter, shot); }

    // ---- Joined Annotations ----
    // A Callout belongs to one Counter. anchorId() is the document id of the Annotation this
    // one hangs on (0 = none). The document calls anchorTo() with that Annotation whenever it
    // is read (CaptureDocument::items, annotation, paint), so what the follower keeps of it
    // (position, size, color) is never stale; null when the anchor is gone. Deleting the
    // anchor deletes its followers (CaptureDocument::remove).
    virtual int anchorId() const { return 0; }
    virtual void anchorTo(const Annotation * /*anchor*/) {}
    // What a follower takes from the Annotation it hangs on: its color (invalid = none).
    virtual QColor anchorColor() const { return {}; }
};

// A choice a tool offers in the top bar next to the size (Pixelate style and
// strength, Spotlight shape). Remembered per tool; read from ToolStyle::choice.
struct ToolChoice {
    struct Value {
        QString value;
        QString label;
    };
    QString name; // "pixelate_style": unique across tools
    QString tip;
    QList<Value> values;
    QString fallback; // the value it opens on
};

// A kind of Annotation a tool can draw.
struct AnnotationKind {
    QString name;  // stable id: "rect"
    QString label; // tooltip: "Rectangle"
    QString icon;  // Tabler icon name in src/ui/assets/icons
    QString key;   // single-letter tool key: "R"
    int order = 0; // position in the tool capsule, left to right
    // Which size steps the top bar offers for this tool: "line", "text",
    // "counter" or "highlighter" (see ToolOptions); empty = no size select.
    QString sizeGroup;
    // Extra selects for the top bar (see ToolChoice); empty = none.
    QList<ToolChoice> choices;
    // Makes the Annotation for a drag; null when the drag is too small to be one.
    std::function<std::unique_ptr<Annotation>(const DragGesture &)> fromDrag;
};

class AnnotationKinds {
public:
    static void add(AnnotationKind kind);
    static const AnnotationKind *find(const QString &name);
    static QList<AnnotationKind> all(); // by order
};

// Rebuilds an Annotation of one kind from what toJson() wrote; null if the data is
// malformed. Separate from AnnotationKind because not every Annotation is a tool
// (an added image has no key or button).
using AnnotationLoader = std::function<std::unique_ptr<Annotation>(const QJsonObject &)>;

class AnnotationLoaders {
public:
    static void add(const QString &kind, AnnotationLoader loader);
    static const AnnotationLoader *find(const QString &kind);
};

// Put one `const RegisterAnnotationLoader reg("rect", [](const QJsonObject &) {...});` next to the kind.
struct RegisterAnnotationLoader {
    RegisterAnnotationLoader(const QString &kind, AnnotationLoader loader) { AnnotationLoaders::add(kind, std::move(loader)); }
};

// Put one `const RegisterAnnotationKind reg({...});` in the tool's file.
struct RegisterAnnotationKind {
    explicit RegisterAnnotationKind(AnnotationKind kind) { AnnotationKinds::add(std::move(kind)); }
};

} // namespace tarn
