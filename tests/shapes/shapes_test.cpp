// The shape and stroke tools (Filled rectangle, Ellipse, Line, Arrow,
// Pencil, Highlighter), driven the way the canvas drives them (EditorSession
// pointer calls) and checked on the exported image against reference PNGs in
// tests/shapes/reference/ (pixel for pixel; regenerate with
// TARN_UPDATE_REFERENCES=1 through ctest's environment and look at the files).
//
// The shot is 160 x 100: a flat mid-gray, so a mark is easy to see. The
// Highlighter tests use their own shot with white and dark halves.
#include "test_env.h"

#include "document/annotation.h"
#include "editor/editor.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QTest>
#include <cmath>

using tarn::Annotation;
using tarn::Editor;
using tarn::EditorSession;
using tarn::test::TestEnv;

#ifndef TARN_SHAPES_REFERENCE_DIR
#error "TARN_SHAPES_REFERENCE_DIR is set by tests/shapes/CMakeLists.txt"
#endif

namespace {

constexpr int kShift = Qt::ShiftModifier;
constexpr int kAlt = Qt::AltModifier;

QImage grayShot() {
    QImage image(160, 100, QImage::Format_RGB32);
    image.fill(qRgb(120, 120, 120));
    return image;
}

// Left half white, right half dark gray, and a black bar across both at y 40..49.
QImage lightAndDarkShot() {
    QImage image(160, 100, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixel(x, y, x < 80 ? qRgb(255, 255, 255) : qRgb(60, 60, 60));
    QPainter p(&image);
    p.fillRect(0, 40, 160, 10, Qt::black);
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
    const QString path = QStringLiteral(TARN_SHAPES_REFERENCE_DIR "/") + name;
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

class ShapesTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;
    QDateTime when{QDate(2026, 11, 3), QTime(9, 30, 0)};
    int shots = 0;

    EditorSession *open(const QImage &shot = grayShot()) {
        const int id = env->daemon().service<tarn::ThumbnailStack>().add(pngOf(shot), when.addSecs(shots++));
        return env->daemon().service<Editor>().open(id);
    }
    static void drag(EditorSession &s, QPointF from, QPointF to, int mods = 0) {
        s.pointerPressed(from.x(), from.y(), mods);
        s.pointerMoved((from.x() + to.x()) / 2, (from.y() + to.y()) / 2, mods);
        s.pointerMoved(to.x(), to.y(), mods);
        s.pointerReleased(to.x(), to.y(), mods);
    }
    static void stroke(EditorSession &s, const QList<QPointF> &points, int mods = 0) {
        s.pointerPressed(points.first().x(), points.first().y(), mods);
        for (const QPointF &p : points)
            s.pointerMoved(p.x(), p.y(), mods);
        s.pointerReleased(points.last().x(), points.last().y(), mods);
    }
    static const Annotation *only(EditorSession &s) {
        return s.document().items().back().annotation.get();
    }
    static QImage exported(EditorSession &s) {
        s.deselect();
        s.pointerLeft();
        return s.document().flatten();
    }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        shots = 0;
    }
    void cleanup() { env.reset(); }

    // ---- the tools exist, with their keys ----

    void everyToolHasItsKeyAndSizeGroup() {
        EditorSession *s = open();
        const QList<QStringList> expected{
            {"filled-rect", "F", ""}, {"ellipse", "O", "line"}, {"line", "L", "line"},
            {"arrow", "A", "line"},   {"pencil", "P", "line"},  {"highlighter", "H", "highlighter"},
        };
        for (const auto &row : expected) {
            const auto *kind = tarn::AnnotationKinds::find(row[0]);
            QVERIFY2(kind, qPrintable(row[0]));
            QCOMPARE(kind->key, row[1]);
            QCOMPARE(kind->sizeGroup, row[2]);
            s->setTool(row[0]);
            QCOMPARE(s->sizeGroup(), row[2]);
        }
    }

    // ---- reference images ----

    void filledRectangle() {
        EditorSession *s = open();
        s->setTool("filled-rect");
        drag(*s, {20, 15}, {90, 70});
        QCOMPARE(s->annotationCount(), 1);
        compareWithReference(exported(*s), "filled-rectangle.png");
    }

    void ellipse() {
        EditorSession *s = open();
        s->setTool("ellipse");
        drag(*s, {20, 15}, {120, 80});
        compareWithReference(exported(*s), "ellipse.png");
    }

    void line() {
        EditorSession *s = open();
        s->setTool("line");
        drag(*s, {15, 20}, {140, 75});
        compareWithReference(exported(*s), "line.png");
    }

    void taperedArrow() {
        EditorSession *s = open();
        s->setTool("arrow");
        QCOMPARE(s->arrowStyle(), QString("tapered")); // the default
        drag(*s, {20, 80}, {130, 25});
        compareWithReference(exported(*s), "arrow-tapered.png");
    }

    void openHeadArrow() {
        EditorSession *s = open();
        s->setTool("arrow");
        s->setArrowStyle("open");
        drag(*s, {20, 80}, {130, 25});
        compareWithReference(exported(*s), "arrow-open.png");
    }

    void doubleHeadedArrow() {
        EditorSession *s = open();
        s->setTool("arrow");
        s->setArrowStyle("double");
        drag(*s, {20, 80}, {130, 25});
        compareWithReference(exported(*s), "arrow-double.png");
    }

    void pencil() {
        EditorSession *s = open();
        s->setTool("pencil");
        stroke(*s, {{20, 70}, {35, 30}, {55, 65}, {75, 25}, {100, 60}, {135, 30}});
        compareWithReference(exported(*s), "pencil.png");
    }

    void highlighterOverLightAndDarkPixels() {
        EditorSession *s = open(lightAndDarkShot());
        s->setTool("highlighter");
        s->setColor(QColor(255, 230, 0));
        drag(*s, {20, 45}, {140, 45});
        compareWithReference(exported(*s), "highlighter.png");
    }

    // ---- the three Arrow styles differ, and each is remembered ----

    void arrowStylesRenderDifferently() {
        QImage images[3];
        int i = 0;
        for (const QString style : {"tapered", "open", "double"}) {
            EditorSession *s = open();
            s->setTool("arrow");
            s->setArrowStyle(style);
            drag(*s, {20, 80}, {130, 25});
            images[i++] = exported(*s);
        }
        QVERIFY(images[0] != images[1]);
        QVERIFY(images[1] != images[2]);
        QVERIFY(images[0] != images[2]);
    }

    void taperedArrowGrowsFromTailToHead() {
        EditorSession *s = open();
        s->setTool("arrow");
        s->setSizeIndex(4); // 16 px
        drag(*s, {10, 50}, {150, 50});
        const QImage out = exported(*s);
        auto thickness = [&](int x) {
            int n = 0;
            for (int y = 0; y < out.height(); ++y)
                n += out.pixelColor(x, y) != QColor(120, 120, 120);
            return n;
        };
        QVERIFY(thickness(30) < thickness(80));
        QVERIFY(thickness(80) < thickness(100)); // just behind the tip, the head is the widest
    }

    void theArrowStyleIsRemembered() {
        EditorSession *s = open();
        s->setArrowStyle("double");
        env->restartDaemon();
        EditorSession *again = open();
        QCOMPARE(again->arrowStyle(), QString("double"));
        again->setArrowStyle("nonsense"); // ignored
        QCOMPARE(again->arrowStyle(), QString("double"));
    }

    void anArrowKeepsTheStyleItWasDrawnWith() {
        EditorSession *s = open();
        s->setTool("arrow");
        s->setArrowStyle("open");
        drag(*s, {20, 80}, {130, 25});
        const QImage before = exported(*s);
        s->setArrowStyle("double");
        QCOMPARE(exported(*s), before);
    }

    // ---- Shift and Alt ----

    void shiftMakesFilledRectangleEllipseAndRectangleSquare() {
        for (const QString tool : {"rect", "filled-rect", "ellipse"}) {
            EditorSession *s = open();
            s->setTool(tool);
            drag(*s, {30, 20}, {90, 50}, kShift);
            const QRectF f = only(*s)->frame();
            QCOMPARE(f.width(), f.height());
            QCOMPARE(f, QRectF(30, 20, 60, 60));
            // dragging up and left keeps the square on that side
            EditorSession *t = open();
            t->setTool(tool);
            drag(*t, {90, 80}, {50, 70}, kShift);
            QCOMPARE(only(*t)->frame(), QRectF(50, 40, 40, 40));
        }
    }

    void altDrawsFromTheCenter() {
        for (const QString tool : {"rect", "filled-rect", "ellipse"}) {
            EditorSession *s = open();
            s->setTool(tool);
            drag(*s, {80, 50}, {100, 60}, kAlt);
            QCOMPARE(only(*s)->frame(), QRectF(60, 40, 40, 20));
        }
    }

    void shiftAndAltTogetherDrawACenteredSquare() {
        EditorSession *s = open();
        s->setTool("ellipse");
        drag(*s, {80, 50}, {100, 55}, kShift | kAlt);
        QCOMPARE(only(*s)->frame(), QRectF(60, 30, 40, 40));
    }

    void shiftSnapsLinesAndArrowsToFortyFiveDegrees() {
        for (const QString tool : {"line", "arrow"}) {
            EditorSession *s = open();
            s->setTool(tool);
            drag(*s, {20, 50}, {100, 56}, kShift); // nearly flat: snaps flat
            QList<QPointF> e = only(*s)->ends();
            QCOMPARE(e[0], QPointF(20, 50));
            QVERIFY(std::abs(e[1].y() - 50) < 1e-9);
            QVERIFY(e[1].x() > 99);

            EditorSession *t = open();
            t->setTool(tool);
            drag(*t, {20, 20}, {80, 70}, kShift); // between flat and 45: snaps to 45
            e = only(*t)->ends();
            QVERIFY(std::abs((e[1].x() - 20) - (e[1].y() - 20)) < 1e-9);
        }
    }

    void altDrawsLinesAndArrowsFromTheirMiddle() {
        for (const QString tool : {"line", "arrow"}) {
            EditorSession *s = open();
            s->setTool(tool);
            drag(*s, {80, 50}, {110, 50}, kAlt);
            const QList<QPointF> e = only(*s)->ends();
            QCOMPARE(e[0], QPointF(50, 50));
            QCOMPARE(e[1], QPointF(110, 50));
        }
    }

    void shiftMakesTheHighlighterStraight() {
        EditorSession *s = open();
        s->setTool("highlighter");
        stroke(*s, {{20, 50}, {50, 58}, {80, 44}, {120, 53}}, kShift);
        // one flat run: as tall as the marker, not as tall as the wobble
        QCOMPARE(only(*s)->bounds().height(), 26.0);
        QCOMPARE(only(*s)->bounds().width(), 100.0 + 26.0);
    }

    void withoutShiftTheHighlighterFollowsTheHand() {
        EditorSession *s = open();
        s->setTool("highlighter");
        stroke(*s, {{20, 50}, {50, 58}, {80, 44}, {120, 53}});
        QVERIFY(only(*s)->bounds().height() > 26.0);
    }

    // ---- the Highlighter darkens and never covers ----

    void theHighlighterNeverLightensAnyPixel() {
        const QImage shot = lightAndDarkShot();
        EditorSession *s = open(shot);
        s->setTool("highlighter");
        s->setColor(QColor(255, 230, 0));
        stroke(*s, {{10, 30}, {70, 55}, {150, 35}}); // across light, dark and the black bar
        const QImage out = exported(*s);
        int changed = 0;
        for (int y = 0; y < out.height(); ++y)
            for (int x = 0; x < out.width(); ++x) {
                const QColor a = shot.pixelColor(x, y), b = out.pixelColor(x, y);
                QVERIFY2(b.red() <= a.red() && b.green() <= a.green() && b.blue() <= a.blue(),
                         qPrintable(QString("pixel %1,%2 got lighter").arg(x).arg(y)));
                changed += a != b;
            }
        QVERIFY(changed > 0);
    }

    void theHighlighterKeepsWhatIsUnderIt() {
        const QImage shot = lightAndDarkShot();
        EditorSession *s = open(shot);
        s->setTool("highlighter");
        s->setColor(QColor(255, 230, 0));
        drag(*s, {20, 45}, {140, 45});
        const QImage out = exported(*s);
        QCOMPARE(out.pixelColor(40, 45), QColor(0, 0, 0));       // the black bar shows through, still black
        QCOMPARE(out.pixelColor(40, 35), QColor(255, 230, 0));   // white takes the color in full
        QVERIFY(out.pixelColor(120, 38).red() <= 60);            // dark gray stays dark
        QVERIFY(out.pixelColor(120, 38).blue() < out.pixelColor(120, 38).red()); // and picks up the tint
        QCOMPARE(out.pixelColor(40, 80), QColor(255, 255, 255)); // outside the marker: untouched
    }

    void theHighlighterIsFlatTipped() {
        EditorSession *s = open(lightAndDarkShot());
        s->setTool("highlighter");
        s->setColor(QColor(255, 230, 0));
        drag(*s, {40, 70}, {100, 70});
        const QImage out = exported(*s);
        QCOMPARE(out.pixelColor(41, 70), QColor(255, 230, 0));
        QCOMPARE(out.pixelColor(36, 70), QColor(255, 255, 255)); // no round cap past the start
    }

    // ---- the Pencil is smoothed ----

    void pencilStrokesAreSmoothed() {
        const QList<QPointF> points{{20, 70}, {35, 30}, {55, 65}, {75, 25}, {100, 60}, {135, 30}};
        EditorSession *s = open();
        s->setTool("pencil");
        stroke(*s, points);
        const QImage smooth = exported(*s);

        QImage raw = grayShot(); // the same points joined with straight segments
        {
            QPainter p(&raw);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(QColor(0xFF, 0x45, 0x3A), 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            for (int i = 1; i < points.size(); ++i)
                p.drawLine(points[i - 1], points[i]);
        }
        QVERIFY(smooth != raw);
        // it still passes through where the hand went
        for (const QPointF &p : points)
            QVERIFY(smooth.pixelColor(p.toPoint()) != QColor(120, 120, 120));
    }

    void hairJitterDoesNotMakeMoreStrokePoints() {
        EditorSession *s = open();
        s->setTool("pencil");
        stroke(*s, {{20, 50}, {20.5, 50.4}, {21, 50}, {60, 50}, {60.4, 50.5}, {100, 50}});
        QCOMPARE(s->annotationCount(), 1);
        compareWithReference(exported(*s), "pencil-straight.png");
    }

    void aClickWithTheStrokeToolsDrawsNothing() {
        for (const QString tool : {"pencil", "highlighter", "line", "arrow", "ellipse", "filled-rect"}) {
            EditorSession *s = open();
            s->setTool(tool);
            s->pointerPressed(40, 40);
            s->pointerReleased(40, 40);
            QCOMPARE(s->annotationCount(), 0);
        }
    }

    // ---- selecting with the right Handles, editing, undo ----

    void eachToolHasTheRightHandleKind() {
        using HK = Annotation::HandleKind;
        const QList<QPair<QString, HK>> expected{
            {"filled-rect", HK::Box}, {"ellipse", HK::Box},   {"line", HK::Ends},
            {"arrow", HK::Ends},      {"pencil", HK::Outline}, {"highlighter", HK::Outline},
        };
        for (const auto &[tool, kind] : expected) {
            EditorSession *s = open();
            s->setTool(tool);
            drag(*s, {20, 20}, {110, 70});
            QVERIFY2(s->annotationCount() == 1, qPrintable(tool));
            QCOMPARE(only(*s)->handleKind(), kind);
        }
    }

    void drawingSelectsTheShapeAndASelectClickPicksItAgain() {
        for (const QString tool : {"filled-rect", "ellipse", "line", "arrow", "pencil", "highlighter"}) {
            EditorSession *s = open();
            s->setTool(tool);
            drag(*s, {20, 20}, {110, 70});
            QCOMPARE(s->selectionCount(), 1);
            s->deselect();
            s->setTool("select");
            const QPointF at = tool == "ellipse" ? QPointF(20, 45) : QPointF(65, 45); // on the ring / the diagonal
            s->pointerPressed(at.x(), at.y());
            s->pointerReleased(at.x(), at.y());
            QVERIFY2(s->selectionCount() == 1, qPrintable(tool));
        }
    }

    void anEllipseOnlyHitsOnItsRing() {
        EditorSession *s = open();
        s->setTool("ellipse");
        drag(*s, {20, 15}, {120, 80});
        QVERIFY(!only(*s)->hit({70, 47}, 3)); // the middle
        QVERIFY(only(*s)->hit({20, 47}, 3));  // the left of the ring
    }

    void aSelectedLineEndCanBeDraggedAndUndone() {
        EditorSession *s = open();
        s->setTool("line");
        drag(*s, {20, 50}, {100, 50});
        const QImage before = exported(*s);
        s->setTool("select");
        s->setSelection({s->document().items().back().id});
        s->pointerPressed(100, 50); // the head's dot
        s->pointerMoved(120, 80);
        s->pointerReleased(120, 80);
        QCOMPARE(only(*s)->ends()[1], QPointF(120, 80));
        s->undo();
        QCOMPARE(exported(*s), before);
    }

    void aBoxResizesFromItsHandles() {
        EditorSession *s = open();
        s->setTool("filled-rect");
        drag(*s, {20, 20}, {80, 60});
        s->setTool("select");
        s->pointerPressed(80, 60); // bottom-right dot
        s->pointerMoved(100, 90);
        s->pointerReleased(100, 90);
        QCOMPARE(only(*s)->frame(), QRectF(20, 20, 80, 70));
    }

    void movingAPencilStrokeMovesAllItsPoints() {
        EditorSession *s = open();
        s->setTool("pencil");
        stroke(*s, {{20, 30}, {40, 50}, {60, 30}});
        const QRectF before = only(*s)->bounds();
        s->setTool("select");
        s->pointerPressed(40, 50);
        s->pointerMoved(50, 60);
        s->pointerReleased(50, 60);
        QCOMPARE(only(*s)->bounds(), before.translated(10, 10));
    }

    void recoloringChangesAShape() {
        EditorSession *s = open();
        s->setTool("filled-rect");
        drag(*s, {20, 20}, {80, 60});
        s->setColor(QColor(0, 200, 0)); // the shape is selected, so it follows
        QCOMPARE(exported(*s).pixelColor(40, 40), QColor(0, 200, 0));
    }

    void undoRemovesTheLastShape() {
        EditorSession *s = open();
        s->setTool("pencil");
        stroke(*s, {{20, 30}, {40, 50}, {60, 30}});
        QCOMPARE(s->annotationCount(), 1);
        s->undo();
        QCOMPARE(s->annotationCount(), 0);
    }
};

QTEST_MAIN(ShapesTest)
#include "shapes_test.moc"
