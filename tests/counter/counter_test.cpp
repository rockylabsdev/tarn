// The Counter tool. Clicks place consecutive numbers in the chosen style
// and start; deleting one renumbers the rest and undo restores the numbering;
// the style and start are remembered. Exports are compared with stored
// reference PNGs (regenerate with TARN_UPDATE_REFERENCES=1 and ctest, then look).
#include "test_env.h"

#include "annotations/counter.h"
#include "document/capture_document.h"
#include "editor/editor.h"
#include "editor/editor_session.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QImage>
#include <QTest>
#include <QtMath>

using tarn::EditorSession;
using tarn::test::TestEnv;

#ifndef TARN_COUNTER_REFERENCE_DIR
#error "set by tests/counter/CMakeLists.txt"
#endif

namespace {

QImage testShot() {
    QImage image(120, 48, QImage::Format_RGB32);
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
    const QString path = QStringLiteral(TARN_COUNTER_REFERENCE_DIR "/") + name;
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

class CounterTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;
    EditorSession *session = nullptr;

    tarn::ThumbnailStack &stack() { return env->daemon().service<tarn::ThumbnailStack>(); }
    tarn::Editor &editor() { return env->daemon().service<tarn::Editor>(); }
    // A fresh Editor on the tool: small Counters (11 pt) so four fit in a row.
    void openCounter() {
        session = editor().open(stack().add(pngOf(testShot()), QDateTime(QDate(2026, 11, 3), QTime(9, 30, 0))));
        session->setTool("counter");
        session->setSizeIndex(0);
    }
    // Four clicks, 26 px apart.
    void clickFour() {
        for (int i = 0; i < 4; ++i)
            click(20 + 26 * i, 24);
    }
    void click(qreal x, qreal y) {
        session->pointerPressed(x, y);
        session->pointerReleased(x, y);
    }
    // The export, with the selection and hover chrome out of the way.
    QImage image() {
        session->setSelection({});
        session->pointerLeft();
        return session->document().flatten();
    }
    QList<int> ids() const { return session->document().order(); }

private slots:
    void init() { env = std::make_unique<TestEnv>(); }
    void cleanup() { env.reset(); }

    void labelsInEveryStyle() {
        QCOMPARE(tarn::counterLabel(0, 12), QString("12"));
        QCOMPARE(tarn::counterLabel(1, 1), QString("A"));
        QCOMPARE(tarn::counterLabel(1, 26), QString("Z"));
        QCOMPARE(tarn::counterLabel(1, 27), QString("AA"));
        QCOMPARE(tarn::counterLabel(1, 52), QString("AZ"));
        QCOMPARE(tarn::counterLabel(1, 53), QString("BA"));
        QCOMPARE(tarn::counterLabel(2, 1), QString("i"));
        QCOMPARE(tarn::counterLabel(2, 4), QString("iv"));
        QCOMPARE(tarn::counterLabel(2, 9), QString("ix"));
        QCOMPARE(tarn::counterLabel(2, 14), QString("xiv"));
        QCOMPARE(tarn::counterLabel(2, 1994), QString("mcmxciv"));
        QCOMPARE(tarn::counterLabel(1, 0), QString("0")); // no letter for zero
        QCOMPARE(tarn::counterLabel(2, 0), QString("0"));
    }

    void theToolIsInTheRegistryOnKeyN() {
        const auto *kind = tarn::AnnotationKinds::find("counter");
        QVERIFY(kind);
        QCOMPARE(kind->key, QString("N"));
        QCOMPARE(kind->sizeGroup, QString("counter"));
    }

    void clicksPlaceConsecutiveNumbers() {
        openCounter();
        QCOMPARE(session->counterPreview(), QString("1"));
        clickFour();
        QCOMPARE(session->annotationCount(), 4);
        QCOMPARE(session->counterPreview(), QString("5"));
        compareWithReference(image(), "numbers.png");
    }

    void lettersFromAnyStart() {
        openCounter();
        session->setCounterStyle(1);
        session->setCounterStart(3);
        QCOMPARE(session->counterPreview(), QString("C"));
        clickFour(); // C D E F
        QCOMPARE(session->counterPreview(), QString("G"));
        compareWithReference(image(), "letters-from-3.png");
    }

    void romanFromAnyStart() {
        openCounter();
        session->setCounterStyle(2);
        session->setCounterStart(4);
        clickFour(); // iv v vi vii
        compareWithReference(image(), "roman-from-4.png");
    }

    void deletingAMiddleCounterRenumbersTheRestAndUndoRestores() {
        openCounter();
        clickFour();
        const QImage before = image();
        const QList<int> all = ids();
        session->setSelection({all[1]}); // the "2"
        session->deleteSelection();
        QCOMPARE(session->annotationCount(), 3);
        QCOMPARE(session->counterPreview(), QString("4"));
        compareWithReference(image(), "deleted-second.png"); // 1, then 3 and 4 become 2 and 3
        const QImage afterDelete = image();

        session->undo();
        QCOMPARE(session->annotationCount(), 4);
        QCOMPARE(image(), before);
        session->redo();
        QCOMPARE(image(), afterDelete);
    }

    void deletingRenumbersInEveryStyle() {
        for (int style = 0; style < 3; ++style) {
            openCounter();
            session->setCounterStyle(style);
            session->setCounterStart(2);
            clickFour();
            const QImage before = image();
            session->setSelection({ids()[0]});
            session->deleteSelection();
            QVERIFY(image() != before);
            session->undo();
            QCOMPARE(image(), before);
            session->cancel();
        }
    }

    void numberingFollowsPlacementNotStacking() {
        openCounter();
        clickFour();
        const QImage before = image();
        session->document().reorder({ids()[0]}, true); // the "1" now sits above the "2"
        QCOMPARE(session->counterPreview(), QString("5"));
        // Circles do not overlap, so stacking changes no pixel: every number kept its place.
        QCOMPARE(image(), before);
    }

    void changingTheStartBeginsANewRun() {
        openCounter();
        click(20, 24);
        click(46, 24); // 1 2
        session->deselect(); // (a selected Counter would follow the new start)
        session->setCounterStart(10);
        QCOMPARE(session->counterPreview(), QString("10"));
        click(72, 24);
        click(98, 24); // 10 11, and the first run is untouched
        session->setSelection({ids()[0]});
        session->deleteSelection(); // the 2 becomes 1; the 10 run keeps its numbers
        compareWithReference(image(), "two-runs.png");
    }

    void theStyleAndStartAreRemembered() {
        openCounter();
        session->setCounterStyle(2);
        session->setCounterStart(7);
        env->restartDaemon();
        openCounter();
        QCOMPARE(session->counterStyle(), 2);
        QCOMPARE(session->counterStart(), 7);
        QCOMPARE(session->counterPreview(), QString("vii"));
    }

    // ---- The number sits in the middle of its circle, white on dark, dark on light ----

    // One Counter at the middle of a plain 100 px shot: every style, the smallest and
    // largest size, a dark and a light color (three starting values vary the glyph shapes).
    void theInkIsCenteredInTheCircleAtEverySizeStyleAndColor() {
        struct Color { const char *name; QColor color; };
        const Color colors[] = {{"dark", QColor(0x30, 0x40, 0xB0)}, {"light", QColor(0xF8, 0xE8, 0x90)}};
        const char *styleNames[] = {"123", "abc", "roman"};
        for (int style = 0; style < 3; ++style)
            for (int step : {0, 5})
                for (const Color &c : colors)
                    for (int start : {1, 3, 8}) {
                        QImage shot(100, 100, QImage::Format_RGB32);
                        shot.fill(QColor(0x80, 0x80, 0x80));
                        tarn::CaptureDocument doc(shot);
                        tarn::ToolStyle s;
                        s.color = c.color;
                        s.counterSize = step == 0 ? 11 : 45; // the smallest and the largest step
                        s.counterStyle = style;
                        s.counterStart = start;
                        doc.draw("counter", tarn::DragGesture{{50, 50}, {50, 50}, {}, s});
                        const QImage out = doc.flatten();
                        const QString what = QStringLiteral("%1 %2 size %3 start %4").arg(styleNames[style], c.name).arg(int(s.counterSize)).arg(start);
                        // Ink = pixels inside the circle that are far from the circle's color.
                        const qreal r = s.counterSize * 1.6 / 2 - 1;
                        int x0 = 1000, x1 = -1, y0 = 1000, y1 = -1;
                        for (int y = 0; y < 100; ++y)
                            for (int x = 0; x < 100; ++x) {
                                if (qHypot(x + 0.5 - 50, y + 0.5 - 50) > r)
                                    continue;
                                const QRgb p = out.pixel(x, y);
                                const int d = qAbs(qRed(p) - c.color.red()) + qAbs(qGreen(p) - c.color.green()) + qAbs(qBlue(p) - c.color.blue());
                                if (d < 120)
                                    continue;
                                x0 = qMin(x0, x), x1 = qMax(x1, x), y0 = qMin(y0, y), y1 = qMax(y1, y);
                            }
                        QVERIFY2(x1 >= 0, qPrintable("no ink: " + what));
                        QVERIFY2(qAbs((x0 + x1 + 1) / 2.0 - 50) <= 1.0, qPrintable("x off center: " + what));
                        QVERIFY2(qAbs((y0 + y1 + 1) / 2.0 - 50) <= 1.0, qPrintable("y off center: " + what));
                                        if (start == 1)
                            compareWithReference(out, QStringLiteral("look-%1-%2-%3-%4.png").arg(styleNames[style]).arg(int(s.counterSize)).arg(c.name).arg(start));
                    }
    }

    void theNumberIsWhiteOnADarkCircleAndDarkOnALightOne() {
        for (const bool light : {false, true}) {
            QImage shot(100, 100, QImage::Format_RGB32);
            shot.fill(QColor(0x80, 0x80, 0x80));
            tarn::CaptureDocument doc(shot);
            tarn::ToolStyle s;
            s.color = light ? QColor(0xF8, 0xE8, 0x90) : QColor(0x30, 0x40, 0xB0);
            s.counterSize = 45;
            doc.draw("counter", tarn::DragGesture{{50, 50}, {50, 50}, {}, s});
            const QImage out = doc.flatten();
            bool whiteInk = false, darkInk = false;
            for (int y = 0; y < 100; ++y)
                for (int x = 0; x < 100; ++x) {
                    const QRgb p = out.pixel(x, y);
                    whiteInk |= qRed(p) > 245 && qGreen(p) > 245 && qBlue(p) > 245;
                    darkInk |= qRed(p) < 40 && qGreen(p) < 40 && qBlue(p) < 40;
                }
            QCOMPARE(whiteInk, !light);
            QCOMPARE(darkInk, light);
        }
    }
};

QTEST_MAIN(CounterTest)
#include "counter_test.moc"
