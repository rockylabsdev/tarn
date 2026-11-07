// Selecting and changing Annotations, with undo. Driven the way the QML
// canvas and keys drive it (the EditorSession calls), asserting on the exported
// image and on what the user could see: the selection, the Handles and the
// ghost. "Edit, then undo or redo, then export" is compared with reference PNGs
// in tests/selection/reference/ (pixel for pixel; regenerate with
// TARN_UPDATE_REFERENCES=1 build/tests/selection/test_selection and look at them).
//
// The shot is 96 x 64. Rectangle A is (8,8)-(48,40) and B is (56,16)-(88,56),
// both with the default 6 px stroke, so their Handles are 16 px or more apart.
#include "test_env.h"

#include "document/annotation.h"
#include "editor/editor.h"
#include "editor/handles.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QImage>
#include <QPen>
#include <QTest>
#include <cmath>

using tarn::Annotation;
using tarn::Editor;
using tarn::EditorSession;
using tarn::test::TestEnv;

#ifndef TARN_SELECTION_REFERENCE_DIR
#error "TARN_SELECTION_REFERENCE_DIR is set by tests/selection/CMakeLists.txt"
#endif

namespace {

QImage testShot() {
    QImage image(96, 64, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixel(x, y, qRgb(30 + x, 60 + y * 2, 120 + (x + y) / 2));
    return image;
}

QByteArray pngOf(const QImage &image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

void compareWithReference(const QImage &actual, const QString &name) {
    const QString path = QStringLiteral(TARN_SELECTION_REFERENCE_DIR "/") + name;
    if (qEnvironmentVariableIsSet("TARN_UPDATE_REFERENCES")) {
        QVERIFY(actual.save(path, "PNG"));
        return;
    }
    QImage expected(path);
    QVERIFY2(!expected.isNull(), qPrintable("missing reference " + path));
    QCOMPARE(actual.size(), expected.size());
    QCOMPARE(actual.convertToFormat(QImage::Format_ARGB32), expected.convertToFormat(QImage::Format_ARGB32));
}

// Test-only kinds, to cover the Handle looks Rectangle does not have: a line
// (one dot at each end) and a label (outline only). Registered like any tool.
class TestLine : public Annotation {
public:
    TestLine(QPointF a, QPointF b) : m_a(a), m_b(b) {}
    QString kind() const override { return "testline"; }
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<TestLine>(*this); }
    QRectF bounds() const override { return QRectF(m_a, m_b).normalized().adjusted(-2, -2, 2, 2); }
    void paint(QPainter &p, const QImage &) const override {
        p.setPen(QPen(Qt::yellow, 4));
        p.drawLine(m_a, m_b);
    }
    HandleKind handleKind() const override { return HandleKind::Ends; }
    void translate(const QPointF &d) override { m_a += d, m_b += d; }
    QList<QPointF> ends() const override { return {m_a, m_b}; }
    void setEnd(int i, const QPointF &p) override { (i == 0 ? m_a : m_b) = p; }

private:
    QPointF m_a, m_b;
};

class TestLabel : public Annotation {
public:
    explicit TestLabel(QRectF r) : m_r(r) {}
    QString kind() const override { return "testlabel"; }
    std::unique_ptr<Annotation> clone() const override { return std::make_unique<TestLabel>(*this); }
    QRectF bounds() const override { return m_r; }
    void paint(QPainter &p, const QImage &) const override { p.fillRect(m_r, Qt::white); }
    HandleKind handleKind() const override { return HandleKind::Outline; }
    void translate(const QPointF &d) override { m_r.translate(d); }

private:
    QRectF m_r;
};

const tarn::RegisterAnnotationKind registerTestLine({
    .name = "testline", .label = "Test line", .icon = "line", .key = "Q", .order = 900,
    .fromDrag = [](const tarn::DragGesture &g) -> std::unique_ptr<Annotation> {
        return std::make_unique<TestLine>(g.from, g.to);
    },
});
const tarn::RegisterAnnotationKind registerTestLabel({
    .name = "testlabel", .label = "Test label", .icon = "letter-t", .key = "W", .order = 901,
    .fromDrag = [](const tarn::DragGesture &g) -> std::unique_ptr<Annotation> {
        return std::make_unique<TestLabel>(QRectF(g.from, g.to).normalized());
    },
});

constexpr int Shift = Qt::ShiftModifier;
constexpr int Alt = Qt::AltModifier;

} // namespace

class SelectionTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;
    QDateTime when{QDate(2026, 11, 3), QTime(9, 30, 0)};
    int shots = 0;

    tarn::ThumbnailStack &stack() { return env->daemon().service<tarn::ThumbnailStack>(); }
    Editor &editor() { return env->daemon().service<Editor>(); }
    EditorSession *open() {
        EditorSession *s = editor().open(stack().add(pngOf(testShot()), when.addSecs(shots++)));
        s->setViewportSize(176, 144); // 100% with the shot at (40, 40) in view pixels
        return s;
    }
    static void press(EditorSession *s, QPointF p, int mods = 0) { s->pointerPressed(p.x(), p.y(), mods); }
    static void moveTo(EditorSession *s, QPointF p, int mods = 0) { s->pointerMoved(p.x(), p.y(), mods); }
    static void release(EditorSession *s, QPointF p, int mods = 0) { s->pointerReleased(p.x(), p.y(), mods); }
    static void drag(EditorSession *s, QPointF a, QPointF b, int mods = 0) {
        press(s, a, mods);
        moveTo(s, (a + b) / 2, mods);
        moveTo(s, b, mods);
        release(s, b, mods);
    }
    static void click(EditorSession *s, QPointF p, int mods = 0) {
        press(s, p, mods);
        release(s, p, mods);
    }
    // Draws with the Rectangle tool, then lets go of the selection that leaves.
    static void rectangle(EditorSession *s, QPointF a, QPointF b) {
        s->setTool("rect");
        drag(s, a, b);
        s->deselect();
    }
    EditorSession *twoRects() {
        EditorSession *s = open();
        rectangle(s, {8, 8}, {48, 40});   // A, lower
        rectangle(s, {56, 16}, {88, 56}); // B, above it
        return s;
    }
    static QRectF frameOf(EditorSession *s, int index) { return s->document().items()[index].annotation->frame(); }
    static QImage image(EditorSession *s) { return s->document().flatten(); }
    // A point on A's top edge that is not near a Handle.
    static QPointF onA() { return {18, 8}; }
    static QPointF onB() { return {64, 56}; }

    static QVariantList dots(const QVariantList &chrome) {
        QVariantList out;
        for (const auto &c : chrome)
            if (c.toMap()["type"] == "dot")
                out << c;
        return out;
    }
    static QVariantList outlines(const QVariantList &chrome) {
        QVariantList out;
        for (const auto &c : chrome)
            if (c.toMap()["type"] == "outline")
                out << c;
        return out;
    }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        shots = 0;
    }
    void cleanup() { env.reset(); }

    // ---- selecting ----

    void aToolStaysActiveAfterDrawing() {
        EditorSession *s = open();
        rectangle(s, {8, 8}, {48, 40});
        rectangle(s, {56, 16}, {88, 56});
        QCOMPARE(s->tool(), QString("rect"));
        QCOMPARE(s->annotationCount(), 2);
    }

    void aNewlyDrawnAnnotationIsSelected() {
        EditorSession *s = open();
        s->setTool("rect");
        drag(s, {8, 8}, {48, 40});
        QCOMPARE(s->selectionCount(), 1);
    }

    void clickingAnAnnotationSelectsItEvenWhileADrawingToolIsActive() {
        EditorSession *s = twoRects();
        click(s, onA());
        QCOMPARE(s->selection(), QList<int>{s->document().items()[0].id});
        QCOMPARE(s->annotationCount(), 2); // nothing was drawn
        click(s, onB());
        QCOMPARE(s->selection(), QList<int>{s->document().items()[1].id});
    }

    void clickingTheEmptyInsideOfAnOutlineDoesNotSelectIt() {
        EditorSession *s = twoRects();
        click(s, {28, 24}); // inside A, away from the stroke: a click with the Rectangle tool
        QCOMPARE(s->selectionCount(), 0);
        QCOMPARE(s->annotationCount(), 2);
    }

    void aDragStartingInsideAnOutlineDrawsANewRectangle() {
        EditorSession *s = twoRects();
        drag(s, {20, 16}, {36, 32});
        QCOMPARE(s->annotationCount(), 3);
    }

    void shiftClickAddsAndRemovesFromTheSelection() {
        EditorSession *s = twoRects();
        const int a = s->document().items()[0].id, b = s->document().items()[1].id;
        click(s, onA());
        click(s, onB(), Shift);
        QCOMPARE(s->selection(), (QList<int>{a, b}));
        click(s, onB(), Shift);
        QCOMPARE(s->selection(), QList<int>{a});
    }

    void clickingEmptyCanvasDeselects() {
        EditorSession *s = twoRects();
        click(s, onA());
        click(s, {4, 60});
        QCOMPARE(s->selectionCount(), 0);
    }

    void draggingABoxWithTheSelectToolSelectsSeveral() {
        EditorSession *s = twoRects();
        s->setTool("select");
        drag(s, {-2, -2}, {94, 62});
        QCOMPARE(s->selectionCount(), 2);
        s->deselect();
        drag(s, {-2, -2}, {30, 30}); // reaches only A
        QCOMPARE(s->selection(), QList<int>{s->document().items()[0].id});
    }

    void theSelectToolNeverDraws() {
        EditorSession *s = twoRects();
        s->setTool("select");
        drag(s, {2, 50}, {30, 62});
        QCOMPARE(s->annotationCount(), 2);
    }

    void ctrlASelectsAll() {
        EditorSession *s = twoRects();
        s->selectAll();
        QCOMPARE(s->selectionCount(), 2);
    }

    void escStepsBackOneLevelDeselectThenCancel() {
        EditorSession *s = twoRects();
        click(s, onA());
        s->escape();
        QVERIFY(s->isOpen());
        QCOMPARE(s->selectionCount(), 0);
        s->escape();
        QVERIFY(!s->isOpen());
    }

    // ---- Handles and ghost ----

    void aSelectedRectangleHasEightDotsAndAnOneAccentOutline() {
        EditorSession *s = twoRects();
        click(s, onA());
        const QVariantList chrome = s->chrome();
        QCOMPARE(dots(chrome).size(), 8);
        QCOMPARE(outlines(chrome).size(), 1);
        for (const auto &d : dots(chrome)) {
            QCOMPARE(d.toMap()["size"].toDouble(), 14.0);
            QCOMPARE(d.toMap()["opacity"].toDouble(), 1.0);
        }
        // The outline is the Rectangle's frame in view pixels (the shot sits at 40, 40).
        const QVariantMap o = outlines(chrome).first().toMap();
        QCOMPARE(QRectF(o["x"].toDouble(), o["y"].toDouble(), o["w"].toDouble(), o["h"].toDouble()), QRectF(48, 48, 40, 32));
        // Dot 2 is the bottom-right corner.
        const QVariantMap br = dots(chrome)[2].toMap();
        QCOMPARE(QPointF(br["x"].toDouble(), br["y"].toDouble()), QPointF(88, 80));
    }

    void aLineHasOneDotAtEachEndAndNoOutline() {
        EditorSession *s = open();
        s->setTool("testline");
        drag(s, {10, 10}, {70, 50});
        const QVariantList chrome = s->chrome();
        QCOMPARE(dots(chrome).size(), 2);
        QCOMPARE(outlines(chrome).size(), 0);
        QCOMPARE(dots(chrome)[1].toMap()["x"].toDouble(), 110.0);
    }

    void aLabelHasAnOutline4PxOutAndNoDots() {
        EditorSession *s = open();
        s->setTool("testlabel");
        drag(s, {10, 10}, {50, 30});
        const QVariantList chrome = s->chrome();
        QCOMPARE(dots(chrome).size(), 0);
        QCOMPARE(outlines(chrome).size(), 1);
        const QVariantMap o = outlines(chrome).first().toMap();
        QCOMPARE(QRectF(o["x"].toDouble(), o["y"].toDouble(), o["w"].toDouble(), o["h"].toDouble()), QRectF(46, 46, 48, 28));
    }

    void aDotGrowsTo18PxUnderThePointerWithTheMatchingResizePointer() {
        EditorSession *s = twoRects();
        click(s, onA());
        struct Row { int handle; QPointF at; Qt::CursorShape cursor; };
        const Row rows[] = {
            {0, {8, 8}, Qt::SizeFDiagCursor},  {1, {48, 8}, Qt::SizeBDiagCursor}, {2, {48, 40}, Qt::SizeFDiagCursor},
            {3, {8, 40}, Qt::SizeBDiagCursor}, {4, {28, 8}, Qt::SizeVerCursor},   {5, {48, 24}, Qt::SizeHorCursor},
            {6, {28, 40}, Qt::SizeVerCursor},  {7, {8, 24}, Qt::SizeHorCursor},
        };
        for (const Row &r : rows) {
            s->pointerHovered(r.at.x() + 2, r.at.y() - 2); // near, not exactly on
            QCOMPARE(s->cursor(), int(r.cursor));
            int grown = 0;
            for (const auto &d : dots(s->chrome())) {
                const QVariantMap m = d.toMap();
                if (m["size"].toDouble() == 18.0) {
                    ++grown;
                    QCOMPARE(m["handle"].toInt(), r.handle);
                } else {
                    QCOMPARE(m["size"].toDouble(), 14.0);
                }
            }
            QCOMPARE(grown, 1);
        }
        s->pointerHovered(28, 24); // inside A, away from stroke and Handles
        QCOMPARE(s->cursor(), int(Qt::CrossCursor));
    }

    void hoveringAnUnselectedAnnotationShowsItsSelectionAt40Percent() {
        EditorSession *s = twoRects();
        s->pointerHovered(onA().x(), onA().y());
        QCOMPARE(s->hoveredAnnotation(), s->document().items()[0].id);
        const QVariantList chrome = s->chrome();
        QCOMPARE(dots(chrome).size(), 8);
        QCOMPARE(outlines(chrome).size(), 1);
        for (const auto &c : chrome)
            QCOMPARE(c.toMap()["opacity"].toDouble(), 0.4);
        QCOMPARE(s->cursor(), int(Qt::SizeAllCursor));
        QCOMPARE(s->selectionCount(), 0); // a ghost is not a selection
        s->pointerHovered(4, 60);
        QVERIFY(s->chrome().isEmpty());
        QCOMPARE(s->hoveredAnnotation(), -1);
    }

    void aSelectedAnnotationShowsNoGhostAndLeavingTheCanvasClearsTheHover() {
        EditorSession *s = twoRects();
        click(s, onA());
        s->pointerHovered(onA().x(), onA().y());
        QCOMPARE(s->hoveredAnnotation(), -1);
        for (const auto &c : s->chrome())
            QCOMPARE(c.toMap()["opacity"].toDouble(), 1.0);
        s->pointerHovered(onB().x(), onB().y());
        QCOMPARE(s->hoveredAnnotation(), s->document().items()[1].id);
        s->pointerLeft();
        QCOMPARE(s->hoveredAnnotation(), -1);
    }

    void severalSelectedShowEachOutlineAndOneGroupWithEightDots() {
        EditorSession *s = twoRects();
        s->selectAll();
        const QVariantList chrome = s->chrome();
        QCOMPARE(outlines(chrome).size(), 3); // A, B, and the group
        QCOMPARE(dots(chrome).size(), 8);
    }

    // ---- moving ----

    void draggingAnAnnotationMovesItAndUndoRedoRestoreIt() {
        EditorSession *s = twoRects();
        const QImage before = image(s);
        compareWithReference(before, "two-rects.png");
        drag(s, onA(), onA() + QPointF(6, 4));
        QCOMPARE(frameOf(s, 0), QRectF(14, 12, 40, 32));
        compareWithReference(image(s), "a-moved.png");
        s->undo();
        QCOMPARE(image(s), before);
        s->redo();
        compareWithReference(image(s), "a-moved.png");
    }

    void aWholeDragIsOneUndoStep() {
        EditorSession *s = twoRects();
        const int steps = s->document().history().undoCount();
        drag(s, onA(), onA() + QPointF(6, 4));
        QCOMPARE(s->document().history().undoCount(), steps + 1);
    }

    void aClickWithoutMovingIsNotAnUndoStep() {
        EditorSession *s = twoRects();
        const int steps = s->document().history().undoCount();
        click(s, onA());
        QCOMPARE(s->document().history().undoCount(), steps);
    }

    void shiftLocksAMoveToOneAxis() {
        EditorSession *s = twoRects();
        drag(s, onA(), onA() + QPointF(12, 5), Shift);
        QCOMPARE(frameOf(s, 0), QRectF(20, 8, 40, 32)); // the larger axis wins
        s->undo();
        drag(s, onA(), onA() + QPointF(3, -9), Shift);
        QCOMPARE(frameOf(s, 0), QRectF(8, -1, 40, 32));
    }

    void draggingASelectionedMemberMovesTheWholeSelection() {
        EditorSession *s = twoRects();
        s->selectAll();
        drag(s, onA(), onA() + QPointF(5, 3));
        QCOMPARE(frameOf(s, 0), QRectF(13, 11, 40, 32));
        QCOMPARE(frameOf(s, 1), QRectF(61, 19, 32, 40));
        compareWithReference(image(s), "both-moved.png");
        s->undo();
        compareWithReference(image(s), "two-rects.png");
    }

    // ---- resizing ----

    void draggingAHandleResizesAndUndoRedoRestoreIt() {
        EditorSession *s = twoRects();
        click(s, onA());
        drag(s, {48, 40}, {60, 46}); // bottom-right corner
        QCOMPARE(frameOf(s, 0), QRectF(8, 8, 52, 38));
        compareWithReference(image(s), "a-resized.png");
        s->undo();
        compareWithReference(image(s), "two-rects.png");
        s->redo();
        compareWithReference(image(s), "a-resized.png");
    }

    void everyHandleMovesItsOwnEdges() {
        struct Row { int handle; QPointF to; QRectF expected; };
        const Row rows[] = {
            {0, {4, 2}, QRectF(4, 2, 44, 38)},    {1, {52, 4}, QRectF(8, 4, 44, 36)},  {2, {50, 44}, QRectF(8, 8, 42, 36)},
            {3, {2, 46}, QRectF(2, 8, 46, 38)},   {4, {30, 3}, QRectF(8, 3, 40, 37)},  {5, {55, 22}, QRectF(8, 8, 47, 32)},
            {6, {30, 50}, QRectF(8, 8, 40, 42)},  {7, {1, 22}, QRectF(1, 8, 47, 32)},
        };
        for (const Row &r : rows) {
            EditorSession *s = twoRects();
            click(s, onA());
            const QPointF from = tarn::Handles::boxPoint(QRectF(8, 8, 40, 32), r.handle);
            drag(s, from, r.to);
            QCOMPARE(frameOf(s, 0), r.expected);
        }
    }

    void draggingAHandlePastTheOppositeEdgeFlips() {
        EditorSession *s = twoRects();
        click(s, onA());
        drag(s, {48, 40}, {2, 50}); // bottom-right dragged beyond the left edge
        QCOMPARE(frameOf(s, 0), QRectF(2, 8, 6, 42));
    }

    void shiftOnACornerKeepsTheProportions() {
        EditorSession *s = twoRects();
        click(s, onA());
        drag(s, {48, 40}, {88, 50}, Shift); // 40 x 32 -> wider: scale by the larger ratio
        QCOMPARE(frameOf(s, 0), QRectF(8, 8, 80, 64));
    }

    void resizingSeveralScalesThemTogether() {
        EditorSession *s = twoRects();
        s->selectAll(); // group frame: (8,8)-(88,56), 80 x 48
        drag(s, {88, 56}, {88, 104}); // bottom-right corner: twice as tall
        QCOMPARE(frameOf(s, 0), QRectF(8, 8, 40, 64));
        QCOMPARE(frameOf(s, 1), QRectF(56, 24, 32, 80));
        s->undo();
        compareWithReference(image(s), "two-rects.png");
    }

    void aLineEndMovesAndUndoPutsItBack() {
        EditorSession *s = open();
        s->setTool("testline");
        drag(s, {10, 10}, {70, 50});
        const QImage before = image(s);
        drag(s, {70, 50}, {60, 20});
        QCOMPARE(s->document().items()[0].annotation->ends()[1], QPointF(60, 20));
        QCOMPARE(s->document().items()[0].annotation->ends()[0], QPointF(10, 10));
        s->undo();
        QCOMPARE(image(s), before);
    }

    void aLabelFollowsItsCenterWhenAGroupIsResizedAndItsSizeStays() {
        EditorSession *s = open();
        s->setTool("testlabel");
        drag(s, {10, 10}, {30, 20});
        s->setTool("rect");
        rectangle(s, {40, 30}, {80, 60});
        s->selectAll(); // group (10,10)-(80,60)
        drag(s, {80, 60}, {150, 110}); // twice the size about the top-left
        const QRectF label = s->document().items()[0].annotation->frame();
        QCOMPARE(label.size(), QSizeF(20, 10));
        QCOMPARE(label.center(), QPointF(30, 20)); // (20, 15) about the top-left (10, 10), doubled
    }

    // ---- deleting, duplicating, nudging, reordering ----

    void deleteRemovesAndUndoBringsItBackInTheSamePlace() {
        EditorSession *s = twoRects();
        click(s, onA());
        s->deleteSelection();
        QCOMPARE(s->annotationCount(), 1);
        QCOMPARE(s->selectionCount(), 0);
        s->undo();
        compareWithReference(image(s), "two-rects.png");
        QCOMPARE(frameOf(s, 0), QRectF(8, 8, 40, 32)); // A is below B again
        s->redo();
        QCOMPARE(s->annotationCount(), 1);
    }

    void deleteWorksOnAMultiSelectionAsOneStep() {
        EditorSession *s = twoRects();
        s->selectAll();
        s->deleteSelection();
        QCOMPARE(s->annotationCount(), 0);
        s->undo();
        QCOMPARE(s->annotationCount(), 2);
        compareWithReference(image(s), "two-rects.png");
    }

    void duplicateAddsACopy10PxAwayAndSelectsIt() {
        EditorSession *s = twoRects();
        click(s, onA());
        s->duplicateSelection();
        QCOMPARE(s->annotationCount(), 3);
        QCOMPARE(frameOf(s, 2), QRectF(18, 18, 40, 32));
        QCOMPARE(s->selection(), QList<int>{s->document().items()[2].id});
        compareWithReference(image(s), "a-duplicated.png");
        s->undo();
        compareWithReference(image(s), "two-rects.png");
        s->redo();
        compareWithReference(image(s), "a-duplicated.png");
    }

    void altDragMovesACopyAndLeavesTheOriginal() {
        EditorSession *s = twoRects();
        drag(s, onA(), onA() + QPointF(6, 4), Alt);
        QCOMPARE(s->annotationCount(), 3);
        QCOMPARE(frameOf(s, 0), QRectF(8, 8, 40, 32));
        QCOMPARE(frameOf(s, 2), QRectF(14, 12, 40, 32));
        s->undo(); // the move
        QCOMPARE(frameOf(s, 2), QRectF(8, 8, 40, 32));
        s->undo(); // the duplicate
        compareWithReference(image(s), "two-rects.png");
    }

    void arrowsNudgeBy1AndShiftArrowsBy10() {
        EditorSession *s = twoRects();
        click(s, onA());
        s->nudge(1, 0);
        s->nudge(0, -1);
        QCOMPARE(frameOf(s, 0), QRectF(9, 7, 40, 32));
        s->nudge(-1, 0, true);
        s->nudge(0, 1, true);
        QCOMPARE(frameOf(s, 0), QRectF(-1, 17, 40, 32));
        s->undo();
        s->undo();
        QCOMPARE(frameOf(s, 0), QRectF(9, 7, 40, 32));
    }

    void nudgeMovesAMultiSelectionAndUndoRestoresIt() {
        EditorSession *s = twoRects();
        s->selectAll();
        s->nudge(1, 1, true);
        QCOMPARE(frameOf(s, 0), QRectF(18, 18, 40, 32));
        QCOMPARE(frameOf(s, 1), QRectF(66, 26, 32, 40));
        compareWithReference(image(s), "both-nudged.png");
        s->undo();
        compareWithReference(image(s), "two-rects.png");
    }

    void reorderingChangesWhatIsOnTopAndUndoRestoresIt() {
        EditorSession *s = open();
        s->setColor(Qt::red);
        rectangle(s, {8, 8}, {48, 40}); // A, red
        s->setColor(Qt::blue);
        rectangle(s, {30, 20}, {70, 56}); // B, blue, crossing A's right edge at (48, 20)
        QCOMPARE(QColor(image(s).pixel(48, 20)), QColor(Qt::blue));
        click(s, {18, 8}); // A
        s->bringForward();
        QCOMPARE(s->document().order().last(), s->selection().first());
        QCOMPARE(QColor(image(s).pixel(48, 20)), QColor(Qt::red));
        compareWithReference(image(s), "a-forward.png");
        s->undo();
        QCOMPARE(QColor(image(s).pixel(48, 20)), QColor(Qt::blue));
        s->redo();
        s->sendBackward();
        QCOMPARE(QColor(image(s).pixel(48, 20)), QColor(Qt::blue));
    }

    void reorderingAtTheEndOfTheStackDoesNothing() {
        EditorSession *s = twoRects();
        click(s, onB());
        const int steps = s->document().history().undoCount();
        s->bringForward();
        QCOMPARE(s->document().history().undoCount(), steps);
    }

    void recoloringTheSelectionIsUndoable() {
        EditorSession *s = twoRects();
        const QImage before = image(s);
        click(s, onA());
        s->setColor(Qt::green);
        QVERIFY(image(s) != before);
        QCOMPARE(s->document().history().undoText(), QString("Recolor"));
        s->undo();
        QCOMPARE(image(s), before);
        s->redo();
        QVERIFY(image(s) != before);
    }

    // ---- between Editors ----

    void annotationsCopyAndPasteBetweenTwoOpenEditors() {
        EditorSession *one = twoRects();
        EditorSession *two = open();
        click(one, onA());
        QVERIFY(one->copySelection());
        two->paste();
        QCOMPARE(two->annotationCount(), 1);
        QCOMPARE(frameOf(two, 0), QRectF(8, 8, 40, 32)); // in place in the other Editor
        QCOMPARE(two->selectionCount(), 1);
        QCOMPARE(one->annotationCount(), 2); // the source is untouched
        two->deselect();
        // The pasted Annotation draws the same as the original did.
        EditorSession *three = open();
        rectangle(three, {8, 8}, {48, 40});
        QCOMPARE(image(two), image(three));
        two->undo();
        QCOMPARE(two->annotationCount(), 0);
    }

    void pastingIntoTheSameEditorOffsetsEachCopyBy10() {
        EditorSession *s = twoRects();
        click(s, onA());
        s->copySelection();
        s->paste();
        s->paste();
        QCOMPARE(s->annotationCount(), 4);
        QCOMPARE(frameOf(s, 2), QRectF(18, 18, 40, 32));
        QCOMPARE(frameOf(s, 3), QRectF(28, 28, 40, 32));
    }

    void copyingSeveralPastesSeveralInOrder() {
        EditorSession *one = twoRects();
        EditorSession *two = open();
        one->selectAll();
        one->copySelection();
        two->paste();
        QCOMPARE(two->annotationCount(), 2);
        QCOMPARE(frameOf(two, 1), QRectF(56, 16, 32, 40));
        two->deselect();
        compareWithReference(image(two), "two-rects.png");
    }

    void pastingWithNothingCopiedDoesNothing() {
        EditorSession *s = open();
        s->paste();
        QCOMPARE(s->annotationCount(), 0);
        QVERIFY(!s->canUndo());
    }

    // ---- undo and redo ----

    void undoAndRedoCoverDrawingToo() {
        EditorSession *s = open();
        rectangle(s, {8, 8}, {48, 40});
        QVERIFY(s->canUndo());
        s->undo();
        QCOMPARE(s->annotationCount(), 0);
        QVERIFY(!s->canUndo());
        QVERIFY(s->canRedo());
        s->redo();
        QCOMPARE(s->annotationCount(), 1);
    }

    void aNewEditDropsTheRedoSteps() {
        EditorSession *s = twoRects();
        s->undo();
        QVERIFY(s->canRedo());
        rectangle(s, {20, 44}, {40, 60});
        QVERIFY(!s->canRedo());
    }

    void undoHasNoLimit() {
        EditorSession *s = twoRects();
        click(s, onA());
        const QImage start = image(s);
        for (int i = 0; i < 600; ++i)
            s->nudge(i % 2 ? -1 : 1, 0);
        QCOMPARE(frameOf(s, 0), QRectF(8, 8, 40, 32)); // 300 right, 300 left
        s->nudge(1, 0, true);
        QVERIFY(image(s) != start);
        int undone = 0;
        while (s->canUndo()) {
            s->undo();
            ++undone;
        }
        QCOMPARE(undone, 2 /* two rectangles */ + 601);
        QCOMPARE(s->annotationCount(), 0);
        while (s->canRedo())
            s->redo();
        QCOMPARE(frameOf(s, 0), QRectF(18, 8, 40, 32));
    }

    void undoLeavesOnlyExistingAnnotationsSelected() {
        EditorSession *s = open();
        rectangle(s, {8, 8}, {48, 40});
        click(s, onA());
        QCOMPARE(s->selectionCount(), 1);
        s->undo(); // the drawing goes
        QCOMPARE(s->selectionCount(), 0);
    }

    void aGestureInProgressIgnoresUndo() {
        EditorSession *s = twoRects();
        press(s, onA());
        moveTo(s, onA() + QPointF(4, 4));
        s->undo();
        QCOMPARE(s->annotationCount(), 2);
        release(s, onA() + QPointF(4, 4));
        QCOMPARE(frameOf(s, 0), QRectF(12, 12, 40, 32));
    }

    // Crop and Frame record into the same history. A stand-in for them:
    // a piece of document state changed through document().history().
    void otherKindsOfChangeShareTheSameHistoryInOrder() {
        EditorSession *s = open();
        int frame = 0; // "the Frame"
        rectangle(s, {8, 8}, {48, 40});
        frame = 5;
        s->document().history().record("Frame", [&frame] { frame = 0; }, [&frame] { frame = 5; });
        click(s, onA());
        s->nudge(1, 0);
        QCOMPARE(s->document().history().undoText(), QString("Nudge"));
        s->undo(); // the nudge
        QCOMPARE(frame, 5);
        s->undo(); // the Frame
        QCOMPARE(frame, 0);
        QCOMPARE(s->annotationCount(), 1);
        s->undo(); // the rectangle
        QCOMPARE(s->annotationCount(), 0);
        s->redo(); // the rectangle
        s->redo(); // the Frame
        QCOMPARE(frame, 5);
        s->redo(); // the nudge
        QCOMPARE(frameOf(s, 0), QRectF(9, 8, 40, 32));
    }

    // ---- end to end: edit, undo, export through Copy ----

    void aMovedThenUndoneThenRedoneAnnotationCopiesTheReferenceImage() {
        EditorSession *s = twoRects();
        drag(s, onA(), onA() + QPointF(6, 4));
        s->undo();
        s->redo();
        QVERIFY(s->copyAndClose());
        const QString file = env->scratch() + "/cache/tarn/shared/2026-11-03_09-30-00.png";
        compareWithReference(QImage(file), "a-moved.png");
    }

    void deletingEverythingThenDoneLeavesTheThumbnailUnedited() {
        EditorSession *s = twoRects();
        const int id = s->captureId();
        s->selectAll();
        s->deleteSelection();
        s->done();
        QVERIFY(stack().edited(id).isEmpty());
    }
};

QTEST_MAIN(SelectionTest)
#include "selection_test.moc"
