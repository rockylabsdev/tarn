// Curved arrows. A selected arrow shows a middle Bend Handle (handle 2, after the
// two ends) that bends it; the bend is kept relative to the tail-to-head line, snaps
// to zero within 8 px of the line or on a double-click, and every style follows the
// curve. Driven like the canvas does (pointer calls in shot pixels) and asserted on
// the Handles in the chrome and on the exported picture, compared with reference PNGs
// (regenerate with TARN_UPDATE_REFERENCES=1 and look at them).
//
// The shot is 96 x 64 shown at 100% with its top-left at view (40, 40).
#include "test_env.h"

#include "document/annotation.h"
#include "document/capture_document.h"
#include "editor/editor.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QImage>
#include <QTest>
#include <cmath>

using tarn::Editor;
using tarn::EditorSession;
using tarn::test::TestEnv;

#ifndef TARN_CURVED_REFERENCE_DIR
#error "TARN_CURVED_REFERENCE_DIR is set by tests/curved_arrows/CMakeLists.txt"
#endif

namespace {

QImage testShot() {
    QImage image(96, 64, QImage::Format_RGB32);
    image.fill(QColor(40, 60, 90));
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
    const QString path = QStringLiteral(TARN_CURVED_REFERENCE_DIR "/") + name;
    if (qEnvironmentVariableIsSet("TARN_UPDATE_REFERENCES")) {
        QVERIFY(actual.save(path, "PNG"));
        return;
    }
    QImage expected(path);
    QVERIFY2(!expected.isNull(), qPrintable("missing reference " + path));
    QCOMPARE(actual.size(), expected.size());
    QCOMPARE(actual.convertToFormat(QImage::Format_ARGB32), expected.convertToFormat(QImage::Format_ARGB32));
}

} // namespace

class CurvedArrowsTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;
    int shots = 0;

    Editor &editor() { return env->daemon().service<Editor>(); }
    EditorSession *open() {
        auto &stack = env->daemon().service<tarn::ThumbnailStack>();
        EditorSession *s = editor().open(stack.add(pngOf(testShot()), QDateTime(QDate(2026, 11, 3), QTime(9, 30, 0)).addSecs(shots++)));
        s->setViewportSize(176, 144);
        return s;
    }

    // A straight arrow (what the tool's drag makes) along y = 32 from x 10 to 86, left selected.
    EditorSession *arrow(const QString &style = QStringLiteral("tapered")) {
        EditorSession *s = open();
        tarn::DragGesture g;
        g.from = {10, 32};
        g.to = {86, 32};
        g.style.arrowStyle = style;
        g.path = {g.from, g.to};
        const int id = s->document().draw("arrow", g);
        s->setSelection({id});
        return s;
    }
    static void drag(EditorSession *s, QPointF a, QPointF b) {
        s->pointerPressed(a.x(), a.y(), 0);
        s->pointerMoved((a.x() + b.x()) / 2, (a.y() + b.y()) / 2, 0);
        s->pointerMoved(b.x(), b.y(), 0);
        s->pointerReleased(b.x(), b.y(), 0);
    }
    // The dot with this handle number, in shot pixels (the chrome is in view pixels).
    static QPointF dot(EditorSession *s, int handle) {
        for (const auto &c : s->chrome()) {
            const QVariantMap m = c.toMap();
            if (m["type"] == "dot" && m["handle"].toInt() == handle)
                return QPointF(m["x"].toDouble() - 40, m["y"].toDouble() - 40);
        }
        return {-1000, -1000};
    }
    static int dotCount(EditorSession *s) {
        int n = 0;
        for (const auto &c : s->chrome())
            n += c.toMap()["type"] == "dot";
        return n;
    }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        shots = 0;
    }
    void cleanup() { env.reset(); }

    void drawingMakesAStraightArrowWithABendHandleAtTheMiddle() {
        EditorSession *s = arrow();
        QCOMPARE(dotCount(s), 3);
        QCOMPARE(dot(s, 2), QPointF(48, 32));
        s->deselect();
        QCOMPARE(dotCount(s), 0); // only a selected arrow shows it
    }

    void draggingTheBendHandleBendsTheArrow() {
        EditorSession *s = arrow();
        const QImage straight = s->document().flatten();
        drag(s, {48, 32}, {48, 12});
        QCOMPARE(dot(s, 2), QPointF(48, 12)); // the Handle sits on the curve
        const QImage bent = s->document().flatten();
        QVERIFY(bent != straight);
        QVERIFY(bent.pixel(48, 12) != testShot().pixel(48, 12)); // the shaft passes through the Handle
        QCOMPARE(bent.pixel(48, 32), testShot().pixel(48, 32));  // and has left the straight line
        s->undo();
        QCOMPARE(s->document().flatten(), straight);
        QCOMPARE(dot(s, 2), QPointF(48, 32));
        s->redo();
        QCOMPARE(dot(s, 2), QPointF(48, 12));
    }

    void movingAnEndKeepsTheCurvesShape() {
        EditorSession *s = arrow();
        drag(s, {48, 32}, {48, 12}); // a bend of 20 px to one side
        drag(s, {86, 32}, {86, 52}); // the head moves down: the chord tilts
        const QPointF tail = dot(s, 0), head = dot(s, 1), mid = dot(s, 2);
        QCOMPARE(head, QPointF(86, 52));
        const QPointF chordMid = (tail + head) / 2, c = head - tail;
        const qreal len = std::hypot(c.x(), c.y());
        const QPointF n(-c.y() / len, c.x() / len);
        const QPointF off = mid - chordMid;
        QVERIFY(std::abs(off.x() * n.x() + off.y() * n.y() + 20) < 0.01);  // still 20 px off the line, same side
        QVERIFY(std::abs(off.x() * c.x() + off.y() * c.y()) < 0.01 * len); // straight out from the middle
    }

    void withinEightPixelsOfTheLineTheBendSnapsToZero() {
        EditorSession *s = arrow();
        drag(s, {48, 32}, {48, 12});
        drag(s, {48, 12}, {48, 25}); // 7 px off the line
        QCOMPARE(dot(s, 2), QPointF(48, 32));
        drag(s, {48, 32}, {48, 23}); // 9 px off: stays bent
        QCOMPARE(dot(s, 2), QPointF(48, 23));
    }

    void aDoubleClickOnTheBendHandleStraightensItWithOneUndoStep() {
        EditorSession *s = arrow();
        drag(s, {48, 32}, {48, 12});
        const QImage bent = s->document().flatten();
        QVERIFY(!s->straightenAt(30, 5)); // not on the Handle
        QVERIFY(s->straightenAt(48, 12));
        QCOMPARE(dot(s, 2), QPointF(48, 32));
        s->undo();
        QCOMPARE(s->document().flatten(), bent);
    }

    // A click on a dot, a little off its center, moves nothing: a drag keeps the point
    // where the dot was grabbed.
    void aClickOnTheBendHandleOffCenterLeavesTheBend() {
        EditorSession *s = arrow();
        drag(s, {48, 32}, {48, 12});
        const int steps = s->document().history().undoCount();
        s->pointerPressed(48, 14, 0); // 2 px towards the line, still on the dot
        s->pointerReleased(48, 14, 0);
        QCOMPARE(dot(s, 2), QPointF(48, 12));
        QCOMPARE(s->document().history().undoCount(), steps);
        drag(s, {48, 14}, {48, 4}); // and a drag moves it by the drag, from where it was
        QCOMPARE(dot(s, 2), QPointF(48, 2));
    }

    // A real double-click on the middle dot, as the canvas sends it: press, release, press,
    // doubleClicked (straightenAt) during the second press, release. One undo step.
    void aRealDoubleClickOnTheBendHandleStraightensIt() {
        EditorSession *s = arrow();
        drag(s, {48, 32}, {48, 12});
        const QImage bent = s->document().flatten();
        const int steps = s->document().history().undoCount();
        s->pointerPressed(48, 13, 0);
        s->pointerReleased(48, 13, 0);
        s->pointerPressed(48, 13, 0);
        QVERIFY(s->straightenAt(48, 13));
        s->pointerReleased(48, 13, 0);
        QCOMPARE(dot(s, 2), QPointF(48, 32));
        QCOMPARE(s->document().history().undoCount(), steps + 1);
        s->undo();
        QCOMPARE(s->document().flatten(), bent);
    }

    void aSnappedBendDrawsTheSameAsAnArrowNeverBent() {
        EditorSession *s = arrow();
        drag(s, {48, 32}, {48, 12});
        drag(s, {48, 12}, {48, 30}); // snapped back
        QCOMPARE(s->document().flatten(), arrow()->document().flatten());
    }

    void everyStyleCurves_data() {
        QTest::addColumn<QString>("style");
        QTest::newRow("tapered") << "tapered";
        QTest::newRow("open") << "open";
        QTest::newRow("double") << "double";
    }
    void everyStyleCurves() {
        QFETCH(QString, style);
        EditorSession *s = arrow(style);
        drag(s, {48, 32}, {48, 10});
        compareWithReference(s->document().flatten(), "curved-" + style + ".png");
        EditorSession *t = arrow(style); // and bent the other way
        drag(t, {48, 32}, {48, 54});
        compareWithReference(t->document().flatten(), "curved-" + style + "-other-way.png");
    }
};

QTEST_MAIN(CurvedArrowsTest)
#include "curved_arrows_test.moc"
