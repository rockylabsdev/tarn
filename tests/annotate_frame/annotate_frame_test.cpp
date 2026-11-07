// Annotations on the Frame's background. A Counter and an arrow drawn on the Frame are
// painted over it (never clipped to the shot); Pixelate and Spotlight stay on the shot; what
// falls outside the finished image (Frame off, padding shrunk) is hidden and comes back.
// Reference PNGs in tests/annotate_frame/reference, exact compare; regenerate with
// TARN_UPDATE_REFERENCES=1 through ctest's environment (see docs/agents/verify.md).
#include "test_env.h"

#include "document/capture_document.h"
#include "editor/editor.h"
#include "frame/frame.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QImage>
#include <QTest>

using tarn::CaptureDocument;
using tarn::DragGesture;
using tarn::EditorSession;
using tarn::Frame;
using tarn::ToolStyle;

#ifndef TARN_ANNOTATE_FRAME_REFERENCE_DIR
#error "set by CMake"
#endif

namespace {

const QSize kShot(96, 64);
const QRgb kBlue = qRgb(0x2d, 0x6c, 0xdf);

QImage shotImage() {
    QImage image(kShot, QImage::Format_RGB32);
    for (int y = 0; y < kShot.height(); ++y)
        for (int x = 0; x < kShot.width(); ++x)
            image.setPixel(x, y, qRgb(40 + x, 60 + y, 150));
    return image;
}

QByteArray shotPng() {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    shotImage().save(&buffer, "PNG");
    return bytes;
}

Frame frameOf(qreal padding) {
    Frame f;
    f.background.kind = QStringLiteral("plain");
    f.background.params = {{QStringLiteral("color"), QStringLiteral("#ff2d6cdf")}};
    f.padding = padding;
    f.shadow = 0;
    f.corners = 0;
    return f;
}

void compareWithReference(const QImage &actual, const QString &name) {
    const QString path = QStringLiteral(TARN_ANNOTATE_FRAME_REFERENCE_DIR "/") + name;
    if (qEnvironmentVariableIsSet("TARN_UPDATE_REFERENCES")) {
        QVERIFY(actual.save(path, "PNG"));
        return;
    }
    QImage expected(path);
    QVERIFY2(!expected.isNull(), qPrintable("missing reference " + path));
    QCOMPARE(actual.size(), expected.size());
    QCOMPARE(actual.convertToFormat(QImage::Format_ARGB32), expected.convertToFormat(QImage::Format_ARGB32));
}

// Padding 0.5 of 64 = 32 px on each side: the framed image is 160 x 128, the shot at (32,32).
// A Counter wholly in the Frame's top-left margin, an arrow along the bottom margin (canvas px).
constexpr qreal kPad = 0.5;
ToolStyle smallCounter() {
    ToolStyle style;
    style.counterSize = 14; // 22 px across
    return style;
}
void drawOnBackground(CaptureDocument &doc) {
    doc.draw(QStringLiteral("counter"), DragGesture{{-18, -18}, {-18, -18}, {}, smallCounter()});
    doc.draw(QStringLiteral("arrow"), DragGesture{{10, 80}, {80, 80}, {}, ToolStyle{}});
}

bool differs(const QImage &a, const QImage &b) { return a.convertToFormat(QImage::Format_ARGB32) != b.convertToFormat(QImage::Format_ARGB32); }

} // namespace

class AnnotateFrameTest : public QObject {
    Q_OBJECT
    std::unique_ptr<tarn::test::TestEnv> env;

    EditorSession *openSession() {
        tarn::ThumbnailStack &stack = env->daemon().service<tarn::ThumbnailStack>();
        const int id = stack.add(shotPng(), QDateTime(QDate(2026, 11, 3), QTime(9, 30, 0)));
        EditorSession *s = env->daemon().service<tarn::Editor>().open(id);
        s->setViewportSize(1000, 800);
        s->setFrameBackground("plain", {{"color", "#ff2d6cdf"}});
        s->setFrameValue("padding", kPad);
        s->setFrameValue("shadow", 0);
        s->setFrameValue("corners", 0);
        return s;
    }

private slots:
    void init() { env = std::make_unique<tarn::test::TestEnv>(); }
    void cleanup() { env.reset(); }

    void aCounterAndAnArrowOnTheBackgroundArePaintedOverIt() {
        CaptureDocument doc(shotImage());
        doc.setFrame(frameOf(kPad));
        QCOMPARE(doc.flatten().size(), QSize(160, 128));
        drawOnBackground(doc);
        const QImage out = doc.flatten();
        QVERIFY(out.pixel(32 - 18, 32 - 18) != kBlue); // the Counter's circle, on the Frame
        QVERIFY(out.pixel(32 + 40, 32 + 80) != kBlue); // the arrow's shaft, on the Frame
        QCOMPARE(out.pixel(150, 4), kBlue);            // elsewhere the Frame is untouched
        compareWithReference(out, QStringLiteral("counter-arrow-padding.png"));
    }

    void theSameAfterACrop() {
        CaptureDocument doc(shotImage());
        doc.setFrame(frameOf(kPad));
        drawOnBackground(doc);
        tarn::Crop crop;
        crop.rect = QRect(8, 0, 80, 64);
        QVERIFY(doc.setCrop(crop));
        const QImage out = doc.flatten();
        QCOMPARE(out.size(), QSize(144, 128)); // content 80 x 64, padding 32
        QVERIFY(out.pixel(32 + (40 - 8), 32 + 80) != kBlue); // the arrow, 16 px below the cut
        compareWithReference(out, QStringLiteral("counter-arrow-padding-crop.png"));
    }

    void theSameAfterATurn() {
        CaptureDocument doc(shotImage());
        doc.setFrame(frameOf(kPad));
        drawOnBackground(doc);
        tarn::Crop crop;
        crop.quarterTurns = 1;
        QVERIFY(doc.setCrop(crop));
        const QImage out = doc.flatten();
        QCOMPARE(out.size(), QSize(128, 160)); // content 64 x 96, padding 32
        // Clockwise: canvas (x, y) -> content (63 - y, x): the arrow's shaft (y 80) lands left of the content.
        QVERIFY(out.pixel(32 - 17, 32 + 40) != kBlue);
        compareWithReference(out, QStringLiteral("counter-arrow-padding-turn.png"));
    }

    void anAnnotationOnTheBackgroundIsHiddenWithTheFrameAndComesBackWithIt() {
        CaptureDocument doc(shotImage());
        const QImage bare = doc.flatten();
        doc.setFrame(frameOf(kPad));
        drawOnBackground(doc);
        const QImage withFrame = doc.flatten();
        doc.setFrame(Frame()); // Frame off: the shot as it was; the Counter and the arrow are out of sight
        QCOMPARE(doc.count(), 2);
        QVERIFY(!differs(doc.flatten(), bare));
        doc.setFrame(frameOf(kPad)); // back on: the same picture
        QVERIFY(!differs(doc.flatten(), withFrame));
        // Less padding: the ones now outside go too.
        doc.setFrame(frameOf(0.0));
        QVERIFY(!differs(doc.flatten(), bare));
        doc.setFrame(frameOf(kPad));
        QVERIFY(!differs(doc.flatten(), withFrame));
    }

    void undoAcrossTheFrameAndTheAnnotationsWalksBackAndForth() {
        CaptureDocument doc(shotImage());
        const QImage bare = doc.flatten();
        doc.setFrame(frameOf(kPad));
        drawOnBackground(doc);
        const QImage done = doc.flatten();
        doc.setFrame(Frame(), QStringLiteral("Frame off"));
        QVERIFY(!differs(doc.flatten(), bare));
        QVERIFY(doc.history().undo()); // Frame back on
        QVERIFY(!differs(doc.flatten(), done));
        QVERIFY(doc.history().undo()); // arrow gone
        QVERIFY(doc.history().undo()); // Counter gone
        QCOMPARE(doc.count(), 0);
        QVERIFY(doc.history().undo()); // Frame gone
        QVERIFY(!differs(doc.flatten(), bare));
        while (doc.history().redo()) {
        }
        QCOMPARE(doc.count(), 2);
        QVERIFY(!differs(doc.flatten(), bare)); // the last step was Frame off
        QVERIFY(doc.history().undo());
        QVERIFY(!differs(doc.flatten(), done));
    }

    void pixelateStaysOnTheShotAndSpotlightNeverDimsTheFrame() {
        CaptureDocument doc(shotImage());
        doc.setFrame(frameOf(kPad));
        ToolStyle style;
        style.choices.insert(QStringLiteral("pixelate_style"), QStringLiteral("black"));
        // Straddles the shot's left edge and reaches into the Frame.
        QVERIFY(doc.draw(QStringLiteral("pixelate"), DragGesture{{-10, 10}, {30, 40}, {}, style}) >= 0);
        QVERIFY(doc.draw(QStringLiteral("spotlight"), DragGesture{{60, 10}, {90, 40}, {}, ToolStyle{}}) >= 0);
        const QImage out = doc.flatten();
        QCOMPARE(out.pixel(32 - 6, 32 + 20), kBlue);          // Pixelate: nothing on the Frame
        QCOMPARE(out.pixel(32 + 10, 32 + 20), qRgb(0, 0, 0)); // Pixelate (black out) on the shot
        QCOMPARE(out.pixel(3, 120), kBlue);                    // the dim never reaches the Frame
    }

    void aSessionDrawsAndMovesAnAnnotationAnywhereOnTheFramedImage() {
        EditorSession *s = openSession();
        s->setTool(QStringLiteral("counter"));
        // A click in the Frame's top-left margin, through the view mapping the canvas uses.
        const QPointF view = s->docToView(-18, -18);
        const QPointF doc = s->viewToDoc(view.x(), view.y());
        QVERIFY(doc.x() < 0 && doc.y() < 0);
        s->pointerPressed(doc.x(), doc.y());
        s->pointerReleased(doc.x(), doc.y());
        QCOMPARE(s->annotationCount(), 1);
        QVERIFY(s->document().flatten().pixel(14, 14) != kBlue);
        // The Select tool picks it up on the Frame and moves it onto the shot.
        s->setTool(QStringLiteral("select"));
        s->deselect();
        s->pointerPressed(-18, -18);
        s->pointerMoved(30, 30);
        s->pointerReleased(30, 30);
        QCOMPARE(s->selection().size(), 1);
        QVERIFY(s->document().annotation(s->selection().first())->bounds().center().x() > 20);
        s->undo(); // the move
        s->undo(); // the Counter
        QCOMPARE(s->annotationCount(), 0);
    }

    void aHiddenAnnotationCannotBePickedWhileTheFrameIsOff() {
        EditorSession *s = openSession();
        s->document().draw(QStringLiteral("counter"), DragGesture{{-18, -18}, {-18, -18}, {}, smallCounter()});
        s->pointerHovered(-18, -18);
        QVERIFY(s->chrome().size() > 0); // the hover ghost on the visible Counter
        s->setFrameBackground("none", {});
        s->pointerHovered(-18, -18);
        QCOMPARE(s->chrome().size(), 0);
        s->setFrameBackground("plain", {{"color", "#ff2d6cdf"}}); // it comes back with the Frame
        s->pointerHovered(-18, -18);
        QVERIFY(s->chrome().size() > 0);
    }
};

QTEST_MAIN(AnnotateFrameTest)
#include "annotate_frame_test.moc"
