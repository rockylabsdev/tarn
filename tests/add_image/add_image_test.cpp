// Add image. An image becomes a movable, resizable Annotation by a file
// (the file dialog's answer, or a drop), or by Ctrl+V of a picture on the
// clipboard. Where it reaches past the shot the exported canvas grows, and undo
// shrinks it back. Driven through the EditorSession, the way QML drives it.
//
// The system clipboard is never read: the test replaces the Editor's clipboard
// source. The file dialog is QML only; its answer is addImageFile (tested here).
//
// The shot is 96 x 64. The added picture is 40 x 30: red, green, blue and yellow
// quarters. Grown canvases are compared with tests/add_image/reference/*.png
// (regenerate with TARN_UPDATE_REFERENCES=1 and look at them).
#include "test_env.h"

#include "editor/editor.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QTest>

using tarn::Editor;
using tarn::EditorSession;
using tarn::test::TestEnv;

#ifndef TARN_ADD_IMAGE_REFERENCE_DIR
#error "TARN_ADD_IMAGE_REFERENCE_DIR is set by tests/add_image/CMakeLists.txt"
#endif

namespace {

QImage testShot() {
    QImage image(96, 64, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixel(x, y, qRgb(30 + x, 60 + y * 2, 120 + (x + y) / 2));
    return image;
}

QImage picture() {
    QImage image(40, 30, QImage::Format_ARGB32);
    QPainter p(&image);
    p.fillRect(0, 0, 20, 15, Qt::red);
    p.fillRect(20, 0, 20, 15, Qt::green);
    p.fillRect(0, 15, 20, 15, Qt::blue);
    p.fillRect(20, 15, 20, 15, Qt::yellow);
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
    const QString path = QStringLiteral(TARN_ADD_IMAGE_REFERENCE_DIR "/") + name;
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

class AddImageTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;
    QDateTime when{QDate(2026, 11, 3), QTime(9, 30, 0)};
    int shots = 0;
    QImage clipboard; // what the stubbed clipboard holds

    tarn::ThumbnailStack &stack() { return env->daemon().service<tarn::ThumbnailStack>(); }
    Editor &editor() { return env->daemon().service<Editor>(); }
    EditorSession *open() {
        EditorSession *s = editor().open(stack().add(pngOf(testShot()), when.addSecs(shots++)));
        s->setViewportSize(176, 144);
        return s;
    }
    QString pictureFile(const QString &name = "logo.png") {
        const QString path = env->scratch() + "/" + name;
        picture().save(path, "PNG");
        return path;
    }
    static QImage image(EditorSession *s) { return s->document().flatten(); }
    static QRectF frameOf(EditorSession *s, int index) { return s->document().items()[index].annotation->frame(); }
    // Presses on the picture and drags it by `by`, as the pointer does.
    static void dragBy(EditorSession *s, QPointF from, QPointF by) {
        s->pointerPressed(from.x(), from.y());
        s->pointerMoved(from.x() + by.x() / 2, from.y() + by.y() / 2);
        s->pointerMoved(from.x() + by.x(), from.y() + by.y());
        s->pointerReleased(from.x() + by.x(), from.y() + by.y());
    }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        shots = 0;
        clipboard = QImage();
        editor().setClipboardImageSource([this] { return clipboard; });
    }
    void cleanup() { env.reset(); }

    // ---- the three ways in ----

    void aChosenFileIsAddedCenteredOnTheShotAtItsOwnSize() { // the file dialog's answer
        EditorSession *s = open();
        QVERIFY(s->addImageFile(pictureFile()));
        QCOMPARE(s->annotationCount(), 1);
        QCOMPARE(frameOf(s, 0), QRectF(28, 17, 40, 30));
        QCOMPARE(s->selectionCount(), 1);
        QCOMPARE(s->documentSize(), QSize(96, 64)); // inside the shot: no growth
        s->deselect();
        s->pointerLeft();
        const QImage out = image(s);
        QCOMPARE(out.pixel(30, 19), qRgb(255, 0, 0));
        QCOMPARE(out.pixel(60, 19), qRgb(0, 255, 0));
        QCOMPARE(out.pixel(30, 40), qRgb(0, 0, 255));
        QCOMPARE(out.pixel(60, 40), qRgb(255, 255, 0));
    }

    void aFileUrlLikeADropIsAddedCenteredWhereItWasDropped() {
        EditorSession *s = open();
        QVERIFY(s->addImageFile("file://" + pictureFile(), 30, 20));
        QCOMPARE(frameOf(s, 0), QRectF(10, 5, 40, 30));
    }

    void anUnreadableFileAddsNothingAndSaysSo() {
        EditorSession *s = open();
        const QString bad = env->scratch() + "/notes.txt";
        QFile f(bad);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not a picture");
        f.close();
        QVERIFY(!s->addImageFile(bad));
        QVERIFY(!s->addImageFile(env->scratch() + "/missing.png"));
        QCOMPARE(s->annotationCount(), 0);
        QCOMPARE(env->calls("notify-send").size(), 2);
    }

    void ctrlVOfAPictureOnTheClipboardAddsIt() {
        EditorSession *s = open();
        clipboard = picture();
        QVERIFY(s->pasteClipboard());
        QCOMPARE(s->annotationCount(), 1);
        QCOMPARE(frameOf(s, 0), QRectF(28, 17, 40, 30));
        s->undo();
        QCOMPARE(s->annotationCount(), 0);
    }

    void ctrlVWithNothingOnTheClipboardAndNothingCopiedDoesNothing() {
        EditorSession *s = open();
        QVERIFY(!s->pasteClipboard());
        QCOMPARE(s->annotationCount(), 0);
    }

    void ctrlVPastesCopiedAnnotationsWhenThePictureOnTheClipboardWasThereBeforeTheCopy() {
        EditorSession *s = open();
        clipboard = picture(); // an old picture is still on the system clipboard
        s->addImageFile(pictureFile());
        s->deselect();
        s->pointerPressed(48, 32); // select it again, then Ctrl+C copies Annotations only
        s->pointerReleased(48, 32);
        QVERIFY(s->copySelection());
        QVERIFY(s->pasteClipboard());
        QCOMPARE(s->annotationCount(), 2);
        QCOMPARE(frameOf(s, 1), QRectF(38, 27, 40, 30)); // the Annotation, nudged by 10
        // A different picture copied elsewhere afterwards wins.
        clipboard = QImage(10, 10, QImage::Format_ARGB32);
        clipboard.fill(Qt::black);
        QVERIFY(s->pasteClipboard());
        QCOMPARE(s->annotationCount(), 3);
        QCOMPARE(frameOf(s, 2).size(), QSizeF(10, 10));
    }

    void theImageButtonAsksTheWindowForItsFileDialog() {
        EditorSession *s = open();
        QSignalSpy spy(s, &EditorSession::imageFileRequested);
        s->chooseImage();
        QCOMPARE(spy.count(), 1);
    }

    // ---- movable and resizable ----

    void anAddedImageMovesAndResizesLikeAnyAnnotation() {
        EditorSession *s = open();
        s->addImageFile(pictureFile());
        dragBy(s, {48, 32}, {-10, 5});
        QCOMPARE(frameOf(s, 0), QRectF(18, 22, 40, 30));
        // The bottom-right Handle resizes it.
        s->pointerPressed(58, 52);
        s->pointerMoved(68, 62);
        s->pointerReleased(68, 62);
        QCOMPARE(frameOf(s, 0), QRectF(18, 22, 50, 40));
        s->undo();
        QCOMPARE(frameOf(s, 0), QRectF(18, 22, 40, 30));
        s->undo();
        QCOMPARE(frameOf(s, 0), QRectF(28, 17, 40, 30));
    }

    // ---- the canvas grows ----

    void movingTheImagePastTheRightEdgeGrowsTheExportedCanvas() {
        EditorSession *s = open();
        s->addImageFile(pictureFile());
        const QImage before = image(s);
        QCOMPARE(before.size(), QSize(96, 64));
        dragBy(s, {48, 32}, {50, 0}); // the image now spans x 78..118
        QCOMPARE(s->documentSize(), QSize(118, 64));
        s->deselect();
        s->pointerLeft();
        const QImage out = image(s);
        QCOMPARE(out.size(), QSize(118, 64));
        QCOMPARE(qAlpha(out.pixel(110, 5)), 0); // the new area outside the picture is transparent
        QCOMPARE(out.pixel(100, 20), qRgb(0, 255, 0));
        compareWithReference(out, "grown_right.png");

        s->undo(); // the move: back inside
        QCOMPARE(s->documentSize(), QSize(96, 64));
        QCOMPARE(image(s).size(), QSize(96, 64));
        s->redo();
        QCOMPARE(image(s).size(), QSize(118, 64));
    }

    void movingItPastTheTopLeftGrowsTheCanvasTheOtherWayAndKeepsTheShotPicture() {
        EditorSession *s = open();
        s->addImageFile(pictureFile());
        dragBy(s, {48, 32}, {-40, -30}); // the image now spans x -12..28, y -13..17
        QCOMPARE(s->document().canvas(), QRect(-12, -13, 108, 77));
        s->deselect();
        s->pointerLeft();
        const QImage out = image(s);
        QCOMPARE(out.size(), QSize(108, 77));
        QCOMPARE(out.pixel(12 + 50, 13 + 40), testShot().pixel(50, 40)); // the shot sits at (12, 13) in the export
        compareWithReference(out, "grown_top_left.png");
        s->undo();
        QCOMPARE(image(s).size(), QSize(96, 64));
        QCOMPARE(image(s).pixel(5, 60), testShot().pixel(5, 60));
    }

    void theViewKeepsTheSamePointUnderTheCursorWhenTheCanvasGrowsToTheTopLeft() {
        EditorSession *s = open();
        s->addImageFile(pictureFile());
        dragBy(s, {48, 32}, {-40, -30});
        const QPointF doc(10, 10);
        const QPointF view = s->docToView(doc.x(), doc.y());
        QCOMPARE(s->viewToDoc(view.x(), view.y()), doc);
    }

    void resizingPastTheBottomEdgeGrowsTheCanvasAndUndoShrinksIt() {
        EditorSession *s = open();
        s->addImageFile(pictureFile()); // 28,17 .. 68,47
        s->pointerPressed(68, 47);      // bottom-right Handle
        s->pointerMoved(80, 90);
        s->pointerReleased(80, 90);
        QCOMPARE(s->documentSize(), QSize(96, 90));
        s->undo();
        QCOMPARE(s->documentSize(), QSize(96, 64));
    }

    void anAnnotationThatIsNotAnImageNeverGrowsTheCanvas() {
        EditorSession *s = open();
        s->setTool("rect");
        s->pointerPressed(80, 40);
        s->pointerMoved(120, 70);
        s->pointerReleased(120, 70);
        QCOMPARE(s->annotationCount(), 1);
        QCOMPARE(s->documentSize(), QSize(96, 64));
    }

    void doneKeepsTheGrownCanvasOnTheThumbnail() {
        EditorSession *s = open();
        const int id = s->captureId();
        s->addImageFile(pictureFile());
        dragBy(s, {48, 32}, {50, 0});
        s->done();
        QImage edited;
        edited.loadFromData(stack().edited(id));
        QCOMPARE(edited.size(), QSize(118, 64));
    }
};

QTEST_MAIN(AddImageTest)
#include "add_image_test.moc"
