// Pixelate and Spotlight, the two tools that change the picture. Checked
// the way the user sees them: the exported PNG, against stored references where
// the output is exact (black out, Spotlights) and against the shot itself where
// it is random (the mosaic and the blur).
//
// References: tests/pixelate/reference/. Regenerate with TARN_UPDATE_REFERENCES=1
// (through ctest only) and look at the diff.
#include "test_env.h"

#include "document/annotation.h"
#include "annotations/image.h"
#include "document/capture_document.h"
#include "annotations/pixelate_render.h"
#include "editor/editor.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QTest>

using tarn::CaptureDocument;
using tarn::DragGesture;
using tarn::Editor;
using tarn::EditorSession;
using tarn::ThumbnailStack;
using tarn::ToolStyle;
using tarn::test::TestEnv;

#ifndef TARN_PIXELATE_REFERENCE_DIR
#error "TARN_PIXELATE_REFERENCE_DIR is set by tests/pixelate/CMakeLists.txt"
#endif

namespace {

// 96 x 64 diagonal gradient (the Editor tests' shot): every pixel differs.
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
    const QString path = QStringLiteral(TARN_PIXELATE_REFERENCE_DIR "/") + name;
    if (qEnvironmentVariableIsSet("TARN_UPDATE_REFERENCES")) {
        QVERIFY(actual.save(path, "PNG"));
        return;
    }
    QImage expected(path);
    QVERIFY2(!expected.isNull(), qPrintable("missing reference " + path));
    QCOMPARE(actual.size(), expected.size());
    QCOMPARE(actual.convertToFormat(QImage::Format_ARGB32), expected.convertToFormat(QImage::Format_ARGB32));
}

ToolStyle styleWith(const QString &pixelateStyle, const QString &strength = "3", const QString &shape = "rectangle") {
    ToolStyle s;
    s.choices.insert("pixelate_style", pixelateStyle);
    s.choices.insert("pixelate_strength", strength);
    s.choices.insert("spotlight_shape", shape);
    return s;
}

int draw(CaptureDocument &doc, const QString &tool, QPointF from, QPointF to, const ToolStyle &style) {
    return doc.draw(tool, DragGesture{from, to, {}, style});
}

QRgb px(const QImage &i, int x, int y) { return i.pixel(x, y) | 0xFF000000u; }

// What a pixel becomes under the Spotlight layer.
QRgb dimmed(QRgb p) {
    const int keep = 255 - CaptureDocument::kDimAlpha;
    return qRgb(qRed(p) * keep / 255, qGreen(p) * keep / 255, qBlue(p) * keep / 255);
}

bool nearly(QRgb a, QRgb b, int tol = 2) {
    return qAbs(qRed(a) - qRed(b)) <= tol && qAbs(qGreen(a) - qGreen(b)) <= tol && qAbs(qBlue(a) - qBlue(b)) <= tol;
}

// Plain block-average mosaic of `region`, blocks starting at its top left.
QImage plainMosaic(const QImage &shot, const QRect &region, int block) {
    QImage out = shot;
    for (int by = region.top(); by <= region.bottom(); by += block)
        for (int bx = region.left(); bx <= region.right(); bx += block) {
            const QRect cell = QRect(bx, by, block, block).intersected(region);
            int r = 0, g = 0, b = 0;
            for (int y = cell.top(); y <= cell.bottom(); ++y)
                for (int x = cell.left(); x <= cell.right(); ++x)
                    r += qRed(shot.pixel(x, y)), g += qGreen(shot.pixel(x, y)), b += qBlue(shot.pixel(x, y));
            const int n = cell.width() * cell.height();
            for (int y = cell.top(); y <= cell.bottom(); ++y)
                for (int x = cell.left(); x <= cell.right(); ++x)
                    out.setPixel(x, y, qRgb(r / n, g / n, b / n));
        }
    return out;
}

struct Compare {
    int differing = 0;
    double meanAbs = 0;
};
Compare compare(const QImage &a, const QImage &b, const QRect &region) {
    Compare c;
    long total = 0;
    for (int y = region.top(); y <= region.bottom(); ++y)
        for (int x = region.left(); x <= region.right(); ++x) {
            const QRgb p = a.pixel(x, y), q = b.pixel(x, y);
            if (p != q)
                ++c.differing;
            total += qAbs(qRed(p) - qRed(q)) + qAbs(qGreen(p) - qGreen(q)) + qAbs(qBlue(p) - qBlue(q));
        }
    c.meanAbs = double(total) / (region.width() * region.height() * 3);
    return c;
}

} // namespace

class PixelateTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;
    QDateTime when{QDate(2026, 11, 3), QTime(9, 30, 0)};

private slots:
    void init() { env = std::make_unique<TestEnv>(); }
    void cleanup() { env.reset(); }

    // ---- Pixelate hides the picture, never the Annotations ----

    void pixelateOverAnAnnotationHidesOnlyThePicture() {
        CaptureDocument doc(testShot());
        draw(doc, "rect", {16, 12}, {80, 52}, ToolStyle{});           // drawn FIRST...
        draw(doc, "pixelate", {8, 20}, {88, 44}, styleWith("black")); // ...then covered by Pixelate
        const QImage out = doc.flatten();
        compareWithReference(out, "pixelate_black_under_rectangle.png");
        QCOMPARE(px(out, 40, 30), qRgb(0, 0, 0));                         // the picture is hidden in the box
        QCOMPARE(px(out, 40, 5), px(testShot(), 40, 5));                  // and untouched outside it
        QVERIFY(nearly(px(out, 16, 30), qRgb(0xFF, 0x45, 0x3A), 1));      // the Rectangle's stroke crosses the box, still on top
        QVERIFY(nearly(px(out, 80, 30), qRgb(0xFF, 0x45, 0x3A), 1));
    }

    void anAnnotationDrawnAfterPixelateIsAlsoOnTop() {
        CaptureDocument doc(testShot());
        draw(doc, "pixelate", {8, 20}, {88, 44}, styleWith("black"));
        draw(doc, "rect", {16, 12}, {80, 52}, ToolStyle{});
        QVERIFY(nearly(px(doc.flatten(), 16, 30), qRgb(0xFF, 0x45, 0x3A), 1));
    }

    void pixelateReadsThePictureNeverTheAnnotation() {
        // A blur over a red stroke must not smear the red: the stroke is drawn after, crisp.
        CaptureDocument doc(testShot());
        draw(doc, "rect", {16, 12}, {80, 52}, ToolStyle{});
        draw(doc, "pixelate", {8, 20}, {88, 44}, styleWith("blur", "5"));
        const QImage out = doc.flatten();
        QVERIFY(nearly(px(out, 16, 30), qRgb(0xFF, 0x45, 0x3A), 1));
        // beside the stroke the blur holds only picture colors: red-ish smear would push red up past the shot's range
        const int maxRed = 30 + 95 + 10;
        QVERIFY(qRed(out.pixel(30, 30)) <= maxRed);
    }

    // ---- randomized, so it cannot be reversed ----

    void twoExportsOfTheSameAreaDifferAndNeitherIsAPlainMosaic() {
        CaptureDocument doc(testShot());
        draw(doc, "pixelate", {8, 8}, {88, 56}, styleWith("pixelate", "3")); // block 14
        const QImage a = doc.flatten(), b = doc.flatten();
        const QRect region(8, 8, 80, 48);
        const QImage plain = plainMosaic(testShot(), testShot().rect(), 14); // blocks sit on the picture's grid

        const int pixels = region.width() * region.height();
        QVERIFY2(compare(a, b, region).differing > pixels * 9 / 10, "two exports share their noise");
        QVERIFY2(compare(a, plain, region).differing > pixels * 9 / 10, "export 1 is a plain mosaic");
        QVERIFY2(compare(b, plain, region).differing > pixels * 9 / 10, "export 2 is a plain mosaic");
        // still a mosaic and still the same picture, not garbage, and the shot is hidden
        QVERIFY(compare(a, plain, region).meanAbs < 20);
        QVERIFY(compare(a, testShot(), region).differing > pixels * 9 / 10);
        // outside the box nothing changed
        QCOMPARE(compare(a, testShot(), QRect(0, 0, 96, 8)).differing, 0);
    }

    void theCanvasDoesNotShimmerBetweenRepaints() {
        CaptureDocument doc(testShot());
        draw(doc, "pixelate", {8, 8}, {88, 56}, styleWith("pixelate"));
        auto paintOnce = [&] {
            QImage canvas(96, 64, QImage::Format_ARGB32_Premultiplied);
            QPainter p(&canvas);
            doc.paint(p);
            return canvas;
        };
        QCOMPARE(paintOnce(), paintOnce());
    }

    void blurIsAlsoRandomizedAndBlackOutIsNot() {
        {
            CaptureDocument doc(testShot());
            draw(doc, "pixelate", {8, 8}, {88, 56}, styleWith("blur", "2"));
            QVERIFY(compare(doc.flatten(), doc.flatten(), QRect(8, 8, 80, 48)).differing > 0);
        }
        {
            CaptureDocument doc(testShot());
            draw(doc, "pixelate", {8, 8}, {88, 56}, styleWith("black"));
            QCOMPARE(doc.flatten(), doc.flatten());
        }
    }

    void strengthMakesTheBlocksBigger() {
        auto blockRun = [](const QString &strength) {
            CaptureDocument doc(testShot());
            draw(doc, "pixelate", {0, 0}, {96, 64}, styleWith("pixelate", strength));
            // how far the picture is from the source: a stronger mosaic loses more of the gradient
            return compare(doc.flatten(), testShot(), QRect(0, 0, 96, 64)).meanAbs;
        };
        QVERIFY(blockRun("5") > blockRun("1"));
    }

    // ---- Spotlight ----

    void overlappingSpotlightsShareOneDimLayer() {
        CaptureDocument doc(testShot());
        draw(doc, "spotlight", {8, 8}, {48, 40}, styleWith("pixelate"));
        draw(doc, "spotlight", {32, 24}, {72, 56}, styleWith("pixelate"));
        const QImage out = doc.flatten();
        compareWithReference(out, "spotlights_overlapping.png");
        const QImage shot = testShot();
        QCOMPARE(px(out, 40, 32), px(shot, 40, 32));  // inside both: bright
        QCOMPARE(px(out, 12, 12), px(shot, 12, 12));  // inside one: bright
        QCOMPARE(px(out, 68, 52), px(shot, 68, 52));  // inside the other: bright
        QVERIFY(nearly(px(out, 90, 4), dimmed(px(shot, 90, 4))));  // outside both: dimmed ONCE (not twice)
        QVERIFY(nearly(px(out, 4, 60), dimmed(px(shot, 4, 60))));
    }

    void twoSpotlightsDimLikeOneSpotlightOverTheirUnion() {
        // The dim outside both equals the dim outside a single Spotlight.
        CaptureDocument one(testShot()), two(testShot());
        draw(one, "spotlight", {8, 8}, {48, 40}, styleWith("pixelate"));
        draw(two, "spotlight", {8, 8}, {48, 40}, styleWith("pixelate"));
        draw(two, "spotlight", {20, 12}, {60, 50}, styleWith("pixelate"));
        QCOMPARE(px(one.flatten(), 90, 60), px(two.flatten(), 90, 60));
    }

    void anEllipseSpotlightLeavesItsCornersDim() {
        CaptureDocument doc(testShot());
        draw(doc, "spotlight", {8, 8}, {88, 56}, styleWith("pixelate", "3", "ellipse"));
        const QImage out = doc.flatten(), shot = testShot();
        compareWithReference(out, "spotlight_ellipse.png");
        QCOMPARE(px(out, 48, 32), px(shot, 48, 32));
        QVERIFY(nearly(px(out, 10, 10), dimmed(px(shot, 10, 10))));
    }

    void annotationsStayBrightOnTheDimmedPicture() {
        CaptureDocument doc(testShot());
        draw(doc, "spotlight", {30, 20}, {60, 40}, styleWith("pixelate"));
        draw(doc, "rect", {4, 4}, {20, 16}, ToolStyle{}); // outside the Spotlight
        QVERIFY(nearly(px(doc.flatten(), 4, 10), qRgb(0xFF, 0x45, 0x3A), 1));
    }

    void theDimDoesNotCoverTheFrame() {
        // The Frame paints around the shot: the dim layer is limited to dimArea(),
        // the shot's rectangle, so a margin painted outside it stays untouched.
        CaptureDocument doc(testShot());
        draw(doc, "spotlight", {30, 20}, {60, 40}, styleWith("pixelate"));
        QCOMPARE(doc.dimArea(), QRectF(0, 0, 96, 64));
        QImage canvas(120, 90, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(QColor(0xFF, 0x00, 0xFF)); // stands for the Frame
        {
            QPainter p(&canvas);
            p.translate(12, 13);
            doc.paint(p);
        }
        QCOMPARE(px(canvas, 2, 2), qRgb(0xFF, 0, 0xFF));
        QCOMPARE(px(canvas, 117, 87), qRgb(0xFF, 0, 0xFF));
        QVERIFY(nearly(px(canvas, 12 + 90, 13 + 60), dimmed(px(testShot(), 90, 60))));
    }

    void theDimCoversTheGrownCanvasButNotTheFrame() {
        // Decision: the area an added image grew is canvas, not Frame,
        // so the dim covers the whole canvas (shot plus grown area).
        CaptureDocument doc(testShot());
        QImage pic(20, 20, QImage::Format_ARGB32);
        pic.fill(Qt::white);
        doc.add(tarn::makeImageAnnotation(pic, QRectF(-10, 30, 20, 20))); // reaches 10 px past the left edge
        draw(doc, "spotlight", {50, 10}, {70, 25}, styleWith("pixelate"));
        QCOMPARE(doc.canvas(), QRect(-10, 0, 106, 64));
        QCOMPARE(doc.dimArea(), QRectF(-10, 0, 106, 64));
        const QImage out = doc.flatten();
        QCOMPARE(out.size(), QSize(106, 64));
        QVERIFY(nearly(px(out, 10 + 40, 60), dimmed(px(testShot(), 40, 60))));  // shot, dimmed (canvas offset 10)
        QCOMPARE(qAlpha(out.pixel(2, 5)), int(CaptureDocument::kDimAlpha));    // grown, empty area: dim over transparent
        QCOMPARE(px(out, 5, 40), qRgb(255, 255, 255));                          // the image is a mark: bright above the dim
        QImage framed(130, 90, QImage::Format_ARGB32_Premultiplied);
        framed.fill(QColor(0xFF, 0, 0xFF)); // stands for the Frame
        {
            QPainter p(&framed);
            p.translate(12, 13);
            doc.paint(p);
        }
        QCOMPARE(px(framed, 2, 2), qRgb(0xFF, 0, 0xFF));
    }

    void aSpotlightBeingDrawnDimsTheCanvasAlready() {
        // The draft takes part in the shared layer too, so the preview matches the result.
        CaptureDocument doc(testShot());
        auto draft = tarn::AnnotationKinds::find("spotlight")->fromDrag(DragGesture{{30, 20}, {60, 40}, {}, styleWith("pixelate")});
        QImage canvas(96, 64, QImage::Format_ARGB32_Premultiplied);
        QPainter p(&canvas);
        doc.paint(p, draft.get());
        p.end();
        QVERIFY(nearly(px(canvas, 90, 60), dimmed(px(testShot(), 90, 60))));
        QCOMPARE(px(canvas, 45, 30), px(testShot(), 45, 30));
    }

    // ---- through the Editor session ----

    void thePixelateToolOffersStyleAndStrengthAndDrawsWithThem() {
        ThumbnailStack &stack = env->daemon().service<ThumbnailStack>();
        const int id = stack.add(pngOf(testShot()), when);
        EditorSession *s = env->daemon().service<Editor>().open(id);
        QVERIFY(s);
        s->setTool("pixelate");
        const QVariantList choices = s->choices();
        QCOMPARE(choices.size(), 2);
        QCOMPARE(choices[0].toMap()["current"].toString(), QString("pixelate"));
        QCOMPARE(choices[0].toMap()["options"].toList().size(), 3); // pixelate, blur, black out
        s->setChoice("pixelate_style", "nonsense");                // not offered: ignored
        QCOMPARE(s->choices()[0].toMap()["current"].toString(), QString("pixelate"));
        s->setChoice("pixelate_style", "black");
        QCOMPARE(s->choices()[0].toMap()["label"].toString(), QString("Black out"));

        s->pointerPressed(20, 20);
        s->pointerMoved(40, 30);
        s->pointerMoved(60, 40);
        s->pointerReleased(60, 40);
        s->done();
        QImage out;
        QVERIFY(out.loadFromData(stack.edited(id)));
        QCOMPARE(px(out, 40, 30), qRgb(0, 0, 0));
        QCOMPARE(px(out, 5, 5), px(testShot(), 5, 5));
    }

    void spotlightHasAShapeChoice() {
        ThumbnailStack &stack = env->daemon().service<ThumbnailStack>();
        const int id = stack.add(pngOf(testShot()), when);
        EditorSession *s = env->daemon().service<Editor>().open(id);
        s->setTool("spotlight");
        QCOMPARE(s->choices().size(), 1);
        s->setChoice("spotlight_shape", "ellipse");
        s->pointerPressed(8, 8);
        s->pointerMoved(50, 30);
        s->pointerMoved(88, 56);
        s->pointerReleased(88, 56);
        QCOMPARE(s->annotationCount(), 1);
        QVERIFY(nearly(px(s->document().flatten(), 10, 10), dimmed(px(testShot(), 10, 10)))); // the corner is outside the ellipse
        s->setTool("rect");
        QCOMPARE(s->choices().size(), 0);
    }

    // ---- A clean mosaic ----

    void theMosaicIsFlatBlocksEachTakenFromOneRandomPixel() {
        const QImage shot = testShot();
        auto pixelate = tarn::AnnotationKinds::find("pixelate")->fromDrag(DragGesture{{0, 0}, {96, 64}, {}, styleWith("pixelate", "3")});
        QImage out = shot;
        {
            QPainter p(&out);
            pixelate->paintFlat(p, shot, 12345); // a fixed seed: the same picture every run
        }
        compareWithReference(out, "pixelate_mosaic_flat.png");
        const int block = 14; // strength 3
        for (int by = 0; by < shot.height(); by += block)
            for (int bx = 0; bx < shot.width(); bx += block) {
                const QRect cell = QRect(bx, by, block, block).intersected(shot.rect());
                const QRgb color = out.pixel(bx, by);
                bool taken = false; // the block's color is one of its own pixels, shifted as a whole
                for (int y = cell.top(); y <= cell.bottom(); ++y)
                    for (int x = cell.left(); x <= cell.right(); ++x) {
                        QVERIFY2(out.pixel(x, y) == color, "a block is not flat");
                        const QRgb s = shot.pixel(x, y);
                        const int d = qRed(color) - qRed(s);
                        if (qAbs(d) <= 4 && qGreen(color) - qGreen(s) == d && qBlue(color) - qBlue(s) == d)
                            taken = true;
                    }
                QVERIFY2(taken, "a block's color is not one of its pixels");
            }
    }

    void aRegionRendersTheSameAsTheWholePictureClipped() {
        // Why a cached whole-picture effect can be clipped to a moving region.
        const QImage shot = testShot();
        for (const QString style : {QString("pixelate"), QString("blur")})
            for (int strength : {1, 3, 5}) {
                const QImage whole = tarn::pixelate::render(shot, style, strength, 99, shot.rect());
                const QRect area(11, 7, 50, 40);
                QCOMPARE(tarn::pixelate::render(shot, style, strength, 99, area), whole.copy(area));
            }
    }

    void draggingABlurRegionOnAFullScreenShotDoesNotReblur() {
        QImage big(2560, 1440, QImage::Format_RGB32);
        for (int y = 0; y < big.height(); ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(big.scanLine(y));
            for (int x = 0; x < big.width(); ++x)
                line[x] = qRgb(x & 255, y & 255, (x + y) & 255);
        }
        CaptureDocument doc(big);
        const int id = draw(doc, "pixelate", {400, 300}, {1000, 700}, styleWith("blur", "3"));
        QImage canvas(2560, 1440, QImage::Format_ARGB32_Premultiplied);
        auto paintOnce = [&] {
            QPainter p(&canvas);
            doc.paint(p);
        };
        paintOnce();
        const quint64 renders = tarn::pixelate::renderCount();
        QVERIFY(renders >= 1);
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 20; ++i) { // a drag: 20 moves, each followed by a repaint
            doc.modify("Move", {id}, [](tarn::Annotation &a) { a.translate({12, 5}); });
            paintOnce();
        }
        QCOMPARE(tarn::pixelate::renderCount(), renders); // nothing re-blurred
        QVERIFY2(timer.elapsed() < 20 * 400, "a repaint of the moved region is slow");
        // resizing is just as cheap
        doc.modify("Resize", {id}, [](tarn::Annotation &a) { a.setFrame(QRectF(100, 100, 1500, 900)); });
        paintOnce();
        QCOMPARE(tarn::pixelate::renderCount(), renders);
        // a different strength is a different effect and renders once more
        const int other = draw(doc, "pixelate", {10, 10}, {200, 200}, styleWith("blur", "5"));
        QVERIFY(other >= 0);
        paintOnce();
        paintOnce();
        QCOMPARE(tarn::pixelate::renderCount(), renders + 1);
    }

    // ---- Tool options restyle the selection ----

    void aSelectedPixelateTakesANewStyleAndStrengthAsOneUndoStep() {
        auto *s = openEditor();
        s->setTool("pixelate");
        s->setChoice("pixelate_style", "black");
        drag(s, 8, 8, 88, 56);
        QCOMPARE(s->selectionCount(), 1);
        QCOMPARE(px(s->document().flatten(), 40, 30), qRgb(0, 0, 0));
        const int steps = s->document().history().undoCount();
        s->setChoice("pixelate_style", "pixelate");
        QCOMPARE(s->document().history().undoCount(), steps + 1);
        QVERIFY(px(s->document().flatten(), 40, 30) != qRgb(0, 0, 0));
        s->undo();
        QCOMPARE(px(s->document().flatten(), 40, 30), qRgb(0, 0, 0));
        s->redo();
        QVERIFY(px(s->document().flatten(), 40, 30) != qRgb(0, 0, 0));
        // strength: bigger blocks, so the first block's color differs from the second's width
        s->setChoice("pixelate_strength", "1");
        const QImage fine = canvasOf(s);
        s->setChoice("pixelate_strength", "5");
        QVERIFY(canvasOf(s) != fine);
    }

    void aSelectedSpotlightChangesShape() {
        auto *s = openEditor();
        s->setTool("spotlight");
        drag(s, 8, 8, 88, 56);
        const QImage rectangle = canvasOf(s);
        s->setChoice("spotlight_shape", "ellipse");
        const QImage ellipse = canvasOf(s);
        QVERIFY(ellipse != rectangle);
        QVERIFY(nearly(px(ellipse, 10, 10), dimmed(px(testShot(), 10, 10)))); // the corner is outside the ellipse now
        s->undo();
        QCOMPARE(canvasOf(s), rectangle);
    }

    void aSelectedAnnotationFollowsTheSizeSelect() {
        for (const QString tool : {QString("rect"), QString("ellipse"), QString("line"), QString("arrow"), QString("pencil"), QString("highlighter")}) {
            auto *s = openEditor();
            s->setTool(tool);
            s->setSizeIndex(1);
            drag(s, 12, 12, 80, 48);
            QCOMPARE(s->selectionCount(), 1);
            const QImage before = canvasOf(s);
            s->setSizeIndex(4);
            const QImage after = canvasOf(s);
            QVERIFY2(after != before, qPrintable(tool + " did not follow the size"));
            s->undo();
            QCOMPARE(canvasOf(s), before);
            s->redo();
            QCOMPARE(canvasOf(s), after);
        }
    }

    void aSelectedArrowFollowsTheArrowStyle() {
        auto *s = openEditor();
        s->setTool("arrow");
        s->setArrowStyle("tapered");
        drag(s, 12, 40, 80, 20);
        const QImage tapered = canvasOf(s);
        s->setArrowStyle("double");
        const QImage twoHeads = canvasOf(s);
        QVERIFY(twoHeads != tapered);
        s->undo();
        QCOMPARE(canvasOf(s), tapered);
    }

    void aSelectedCounterFollowsItsStyleStartAndSize() {
        auto *s = openEditor();
        s->setTool("counter");
        s->pointerPressed(30, 30);
        s->pointerReleased(30, 30);
        QCOMPARE(s->selectionCount(), 1);
        const QImage one = canvasOf(s);
        s->setCounterStyle(1); // A
        const QImage letter = canvasOf(s);
        QVERIFY(letter != one);
        s->setCounterStart(3); // C
        QVERIFY(canvasOf(s) != letter);
        s->setSizeIndex(5);
        const QImage big = canvasOf(s);
        s->undo();
        QVERIFY(canvasOf(s) != big);
        s->undo();
        s->undo();
        QCOMPARE(canvasOf(s), one);
    }

    void aSelectedTextFollowsItsLook() {
        auto *s = openEditor();
        s->setTool("text");
        s->setTextLook(0);
        s->beginText(10, 10);
        s->setText("Hi");
        s->finishText();
        QCOMPARE(s->selectionCount(), 1);
        const QRectF plain = s->document().annotation(s->selection().first())->bounds();
        s->setTextLook(2); // label: padded box
        QVERIFY(s->document().annotation(s->selection().first())->bounds().width() > plain.width());
        s->undo();
        QCOMPARE(s->document().annotation(s->selection().first())->bounds(), plain);
    }

    void anAnnotationOfAnotherToolIsLeftAlone() {
        auto *s = openEditor();
        s->setTool("rect");
        drag(s, 12, 12, 80, 48);
        const QImage before = canvasOf(s);
        const int steps = s->document().history().undoCount();
        s->setTool("line"); // the Rectangle stays selected, but the options are the Line's now
        s->setSizeIndex(5);
        QCOMPARE(canvasOf(s), before);
        QCOMPARE(s->document().history().undoCount(), steps);
    }

    // ---- The style crossfade (canvas only) ----

    void aStyleChangeCrossfadesOnTheCanvasAndNotInTheExport() {
        qint64 now = 1000;
        auto *s = openEditor();
        s->document().setFadeClock([&now] { return now; });
        s->setTool("pixelate");
        s->setChoice("pixelate_style", "black");
        drag(s, 8, 8, 88, 56);
        s->setChoice("pixelate_style", "pixelate");
        QVERIFY(s->document().fading());
        QCOMPARE(px(canvasAt(s, now), 40, 30), qRgb(0, 0, 0)); // the first frame is still the old look
        const QRgb mid = px(canvasAt(s, now + CaptureDocument::kFadeMs / 2), 40, 30);
        QVERIFY(s->document().fading());
        const QRgb target = px(canvasAt(s, now + CaptureDocument::kFadeMs), 40, 30);
        QVERIFY(!s->document().fading()); // done after 150 ms
        QVERIFY(mid != qRgb(0, 0, 0) && mid != target);
        QVERIFY(qRed(mid) > 0 && qRed(mid) < qRed(target)); // black fading into the mosaic
        // the export never fades: it is the new look at once
        s->setChoice("pixelate_style", "black");
        QCOMPARE(px(s->document().flatten(), 40, 30), qRgb(0, 0, 0));
        s->setChoice("pixelate_style", "blur");
        QVERIFY(px(s->document().flatten(), 40, 30) != qRgb(0, 0, 0));
    }

private:
    EditorSession *openEditor() {
        ThumbnailStack &stack = env->daemon().service<ThumbnailStack>();
        return env->daemon().service<Editor>().open(stack.add(pngOf(testShot()), when));
    }
    static void drag(EditorSession *s, qreal x0, qreal y0, qreal x1, qreal y1) {
        s->pointerPressed(x0, y0);
        s->pointerMoved((x0 + x1) / 2, (y0 + y1) / 2);
        s->pointerMoved(x1, y1);
        s->pointerReleased(x1, y1);
    }
    // What the canvas shows (no selection chrome): the document painted at 100%.
    static QImage canvasOf(EditorSession *s) {
        QImage canvas(s->document().size(), QImage::Format_ARGB32_Premultiplied);
        canvas.fill(Qt::transparent);
        QPainter p(&canvas);
        s->document().paint(p);
        return canvas;
    }
    // The canvas at a moment of the fade (the test clock is `now`; set through the caller's variable).
    static QImage canvasAt(EditorSession *s, qint64 time) {
        s->document().setFadeClock([time] { return time; });
        return canvasOf(s);
    }
};

QTEST_MAIN(PixelateTest)
#include "pixelate_test.moc"
