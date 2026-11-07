// The Editor's first path through the daemon core: open on a Thumbnail,
// draw a Rectangle, finish with Done / Copy / Save. Drawing is checked by
// exporting and comparing the written PNG with a stored reference image.
//
// References live in tests/editor/reference/. They are compared pixel for pixel
// (tolerance 0): the test shot and the Rectangle sit on whole pixels, and the
// software rasterizer is deterministic. If a Qt update ever moves an antialiased
// corner, regenerate with TARN_UPDATE_REFERENCES=1 and look at the diff.
#include "test_env.h"

#include "editor/editor.h"
#include "editor/opening_size.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QTest>

using tarn::Editor;
using tarn::EditorSession;
using tarn::ThumbnailStack;
using tarn::test::TestEnv;

#ifndef TARN_EDITOR_REFERENCE_DIR
#error "TARN_EDITOR_REFERENCE_DIR is set by tests/editor/CMakeLists.txt"
#endif

namespace {

// A 96 x 64 shot with a diagonal gradient, so a drawn mark is easy to tell from the picture.
QImage testShot() {
    QImage image(96, 64, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixel(x, y, qRgb(30 + x, 60 + y * 2, 120 + (x + y) / 2));
    return image;
}

QImage bigShot() {
    QImage i(2000, 1000, QImage::Format_RGB32);
    i.fill(Qt::gray);
    return i;
}

// White with 1 px black lines every 8 px, both ways: the worst case for a zoomed-out shot.
QImage thinLines() {
    QImage i(600, 450, QImage::Format_RGB32);
    i.fill(Qt::white);
    for (int y = 0; y < i.height(); ++y)
        for (int x = 0; x < i.width(); ++x)
            if (x % 8 == 0 || y % 8 == 0)
                i.setPixel(x, y, qRgb(0, 0, 0));
    return i;
}

// What the canvas paints for a bare document at this zoom (no Frame, no Crop).
QImage paintedAt(const tarn::CaptureDocument &doc, qreal zoom, const QSize &out) {
    QImage img(out, QImage::Format_RGB32);
    img.fill(Qt::magenta);
    QPainter p(&img);
    p.scale(zoom, zoom);
    p.setRenderHint(QPainter::SmoothPixmapTransform, zoom < 1.0);
    doc.paint(p);
    return img;
}

QByteArray pngOf(const QImage &image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// Fails unless the image equals the stored reference exactly.
void compareWithReference(const QImage &actual, const QString &name) {
    const QString path = QStringLiteral(TARN_EDITOR_REFERENCE_DIR "/") + name;
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

class EditorTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;
    QDateTime when{QDate(2026, 11, 3), QTime(9, 30, 0)};
    int shots = 0;

    ThumbnailStack &stack() { return env->daemon().service<ThumbnailStack>(); }
    Editor &editor() { return env->daemon().service<Editor>(); }
    int addShot() { return stack().add(pngOf(testShot()), when.addSecs(shots++)); }
    QString shared() const { return env->scratch() + "/cache/tarn/shared"; }
    // The Rectangle the references show: from (16, 12) to (80, 52).
    static void drawRectangle(EditorSession &s) {
        s.pointerPressed(16, 12);
        s.pointerMoved(50, 30);
        s.pointerMoved(80, 52);
        s.pointerReleased(80, 52);
    }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        shots = 0;
    }
    void cleanup() { env.reset(); }

    // ---- opening ----

    void everyEditorOpensOnRectangleAtFit() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        QVERIFY(s);
        QCOMPARE(s->tool(), QString("rect"));
        QVERIFY(s->fit());
        QCOMPARE(s->annotationCount(), 0);
    }

    void aClickOrEditOnAThumbnailOpensTheEditor() {
        const int id = addShot();
        QSignalSpy opened(&editor(), &Editor::opened);
        stack().edit(id); // what a click and the Edit button call
        QCOMPARE(opened.size(), 1);
        QVERIFY(editor().session(id));
        QCOMPARE(editor().session(id)->captureId(), id);
    }

    void aRecordingDoesNotOpenTheEditor() {
        QByteArray poster = pngOf(testShot());
        const int id = stack().addRecording(poster, when, env->scratch() + "/x.mp4");
        QVERIFY(!editor().open(id));
    }

    void theThumbnailIsHiddenWhileItsEditorIsOpen() {
        const int id = addShot();
        QCOMPARE(stack().count(), 1);
        editor().open(id);
        QCOMPARE(stack().count(), 0);
        QVERIFY(stack().ids().isEmpty());
        QVERIFY(stack().isHidden(id));
    }

    void anEditingThumbnailDoesNotCountTowardTheFour() {
        const int editing = addShot();
        editor().open(editing);
        for (int i = 0; i < 4; ++i)
            addShot();
        QCOMPARE(stack().count(), 4);
        QVERIFY(TestEnv::filesIn(env->screenshots()).isEmpty()); // nothing was pushed out to make room
        editor().session(editing)->cancel();
        // back as a fifth: now the oldest on screen (it) is auto-saved
        QCOMPARE(stack().count(), 4);
        QCOMPARE(TestEnv::filesIn(env->screenshots()).size(), 1);
    }

    // ---- opening size ----

    void openingSizeOnA2560x1440Monitor_data() {
        QTest::addColumn<QSize>("shot");
        QTest::addColumn<QSize>("expected");
        // 1280 x 806 .. 1707 x 1008 on this monitor.
        QTest::newRow("tiny shot is clamped up") << QSize(200, 100) << QSize(1280, 806);
        QTest::newRow("small shot") << QSize(640, 400) << QSize(1280, 806);
        QTest::newRow("shot between the limits (width)") << QSize(1400, 300) << QSize(1480, 806);
        QTest::newRow("shot between the limits (both)") << QSize(1500, 700) << QSize(1580, 884);
        QTest::newRow("big shot is clamped down") << QSize(2560, 1440) << QSize(1707, 1008);
        QTest::newRow("wide, short shot") << QSize(2560, 200) << QSize(1707, 806);
    }
    void openingSizeOnA2560x1440Monitor() {
        QFETCH(QSize, shot);
        QFETCH(QSize, expected);
        QCOMPARE(tarn::editorOpeningSize(shot, QSize(2560, 1440)), expected);
    }

    void openingSizeOnA1920x1080Monitor_data() {
        QTest::addColumn<QSize>("shot");
        QTest::addColumn<QSize>("expected");
        // Half of 1920 is 960, less than the toolbar needs, so 1180 .. 1280 x 605 .. 756.
        QTest::newRow("small shot never narrower than 1180") << QSize(400, 300) << QSize(1180, 605);
        QTest::newRow("shot between the limits") << QSize(1200, 500) << QSize(1280, 684);
        QTest::newRow("big shot is clamped down") << QSize(1920, 1080) << QSize(1280, 756);
        QTest::newRow("shot 1100 wide") << QSize(1100, 300) << QSize(1180, 605);
    }
    void openingSizeOnA1920x1080Monitor() {
        QFETCH(QSize, shot);
        QFETCH(QSize, expected);
        QCOMPARE(tarn::editorOpeningSize(shot, QSize(1920, 1080)), expected);
    }

    void openingSizeCountsTheShotInLogicalPixelsOnAScaledMonitor() {
        // A 2x monitor of 1280 x 720 logical: a 2000 x 1000 shot at 100% is 1000 x 500 logical,
        // which wants more height than 70% of 720.
        QCOMPARE(tarn::editorOpeningSize(QSize(2000, 1000), QSize(1280, 720), 2.0), QSize(1180, 504));
    }

    void theSessionUsesTheMonitorUnderThePointer() {
        const int id = addShot();
        // monitors.json: one 2560 x 1440 monitor at scale 1
        QCOMPARE(editor().open(id)->openingSize(), QSize(1280, 806));
    }

    // ---- drawing and export ----

    void aDrawnRectangleExportedThroughCopyMatchesTheReference() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        drawRectangle(*s);
        QCOMPARE(s->annotationCount(), 1);
        QVERIFY(s->copyAndClose());

        const QString file = shared() + "/2026-11-03_09-30-00.png";
        QCOMPARE(env->calls("tarn-clip"), (QList<QStringList>{{file}}));
        compareWithReference(QImage(file), "rectangle.png");
    }

    void aDrawnRectangleExportedThroughSaveMatchesTheReference() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        drawRectangle(*s);
        QVERIFY(s->saveAndClose());
        QCOMPARE(TestEnv::filesIn(env->screenshots()), QStringList{"2026-11-03_09-30-00.png"});
        compareWithReference(QImage(env->screenshots() + "/2026-11-03_09-30-00.png"), "rectangle.png");
    }

    void saveAsWritesTheSameImageAndClosesBoth() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        drawRectangle(*s);
        const QString path = env->scratch() + "/elsewhere/mine.png";
        QVERIFY(s->saveAs(path));
        compareWithReference(QImage(path), "rectangle.png");
        QCOMPARE(stack().totalCount(), 0);
        QVERIFY(!editor().session(id));
    }

    void theExportIsTheShotsSizeWithNoHairlineOrMargin() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        drawRectangle(*s);
        const QImage out = s->document().flatten();
        QCOMPARE(out.size(), testShot().size());
        // Outside the Rectangle (and its stroke) every pixel is the shot's own.
        const QImage shot = testShot();
        for (QPoint p : {QPoint(0, 0), QPoint(95, 63), QPoint(5, 5), QPoint(90, 60), QPoint(48, 32)})
            QCOMPARE(out.pixel(p), shot.pixel(p));
        QVERIFY(out.pixel(16, 30) != shot.pixel(16, 30)); // on the stroke
    }

    void aClickWithoutADragDrawsNothing() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        s->pointerPressed(20, 20);
        s->pointerReleased(20, 20);
        QCOMPARE(s->annotationCount(), 0);
    }

    void copyKeepOpenCopiesButLeavesTheEditorAndThumbnailAlone() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        drawRectangle(*s);
        QVERIFY(s->copyKeepOpen());
        compareWithReference(QImage(shared() + "/2026-11-03_09-30-00.png"), "rectangle.png");
        QVERIFY(s->isOpen());
        QVERIFY(stack().isHidden(id));
        QVERIFY(stack().edited(id).isEmpty()); // the Thumbnail has not received the edits
    }

    void aFailedCopyKeepsTheEditorOpenAndTheThumbnailAsItWas() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        drawRectangle(*s);
        env->setMode("tarn-clip", "fail");
        QVERIFY(!s->copyAndClose());
        QVERIFY(s->isOpen());
        QVERIFY(stack().edited(id).isEmpty());
        QCOMPARE(env->calls("notify-send").size(), 1);
    }

    // ---- Done and Esc ----

    void doneReturnsToTheThumbnailWithTheEdits() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        QSignalSpy finished(s, &EditorSession::finished);
        drawRectangle(*s);
        s->done();
        QCOMPARE(finished.size(), 1);
        QVERIFY(!editor().session(id));
        QCOMPARE(stack().ids(), QList<int>{id});
        QVERIFY(!stack().isHidden(id));
        compareWithReference(QImage::fromData(stack().edited(id), "PNG"), "rectangle.png");
        // and saving the Thumbnail now saves the edited picture
        QVERIFY(!stack().save(id).isEmpty());
        compareWithReference(QImage(env->screenshots() + "/2026-11-03_09-30-00.png"), "rectangle.png");
    }

    void reopeningAfterDoneShowsTheRectangleStillEditable() {
        const int id = addShot();
        drawRectangle(*editor().open(id));
        editor().session(id)->done();

        EditorSession *again = editor().open(id);
        QVERIFY(again);
        QCOMPARE(again->annotationCount(), 1);
        // editable: the document holds the Annotation itself, and the shot is the untouched capture
        QCOMPARE(again->document().annotation(again->document().items().front().id)->kind(), QString("rect"));
        QCOMPARE(again->document().shot(), testShot());
        // and a new one can be added on top
        again->pointerPressed(4, 4);
        again->pointerReleased(30, 20);
        QCOMPARE(again->annotationCount(), 2);
    }

    void doneWithNothingDrawnLeavesTheCaptureUntouched() {
        const int id = addShot();
        editor().open(id)->done();
        QVERIFY(stack().edited(id).isEmpty());
        QVERIFY(!stack().save(id).isEmpty());
        QCOMPARE(TestEnv::readFile(env->screenshots() + "/2026-11-03_09-30-00.png"), pngOf(testShot()));
    }

    void escCancelsAndKeepsTheThumbnailAsItWas() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        drawRectangle(*s);
        QSignalSpy finished(s, &EditorSession::finished);
        s->cancel();
        QCOMPARE(finished.size(), 1);
        QVERIFY(!stack().isHidden(id));
        QCOMPARE(stack().ids(), QList<int>{id});
        QVERIFY(stack().edited(id).isEmpty());
        QVERIFY(env->calls("notify-send").isEmpty()); // no prompt
        QCOMPARE(editor().open(id)->annotationCount(), 0);
    }

    void escAfterAnEarlierDoneKeepsTheEarlierEdits() {
        const int id = addShot();
        drawRectangle(*editor().open(id));
        editor().session(id)->done();
        const QByteArray before = stack().edited(id);

        EditorSession *s = editor().open(id);
        s->pointerPressed(2, 2);
        s->pointerReleased(40, 40);
        QCOMPARE(s->annotationCount(), 2);
        s->cancel();
        QCOMPARE(stack().edited(id), before);
        QCOMPARE(editor().open(id)->annotationCount(), 1);
    }

    void closingTheThumbnailEndsItsEditor() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        QSignalSpy finished(s, &EditorSession::finished);
        stack().close(id); // e.g. Close all
        QCOMPARE(finished.size(), 1);
        QVERIFY(!editor().session(id));
    }

    // ---- zoom and pan ----

    void itOpensAtFitNeverAbove100() {
        const int id = addShot();
        EditorSession *s = editor().open(id); // 96 x 64
        s->setViewportSize(1280, 650);
        QVERIFY(s->fit());
        QCOMPARE(s->zoom(), 1.0);
        QCOMPARE(s->zoomPercent(), 100);
    }

    void fitLeavesAtLeast40pxOfMarginOnEverySide() {
        const int id = stack().add(pngOf(bigShot()), when);
        EditorSession *s = editor().open(id);
        s->setViewportSize(1280, 698);
        // (1280 - 80) / 2000 = 0.6 ; (698 - 80) / 1000 = 0.618
        QCOMPARE(s->zoom(), 0.6);
        QCOMPARE(s->origin(), QPointF(40, (698 - 600) / 2.0));
    }

    void theZoomMenuAndKeys() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        s->setViewportSize(1280, 650);
        s->zoomTo(2);
        QVERIFY(!s->fit());
        QCOMPARE(s->zoomPercent(), 200);
        s->zoomIn();
        QCOMPARE(s->zoomPercent(), 400);
        s->zoomIn(); // the top
        QCOMPARE(s->zoomPercent(), 400);
        s->zoomOut();
        QCOMPARE(s->zoomPercent(), 200);
        s->zoomTo(0.1); // clamped to 25%
        QCOMPARE(s->zoomPercent(), 25);
        s->zoomOut();
        QCOMPARE(s->zoomPercent(), 25);
        s->zoomTo(1); // Ctrl+1
        QCOMPARE(s->zoomPercent(), 100);
        s->zoomTo(4);
        s->zoomFit(); // Ctrl+0
        QVERIFY(s->fit());
    }

    void ctrlScrollZoomsAboutThePointer() {
        const int id = stack().add(pngOf(bigShot()), when);
        EditorSession *s = editor().open(id);
        s->setViewportSize(1280, 698); // Fit: 0.6
        const QPointF view(500, 300);
        const QPointF before = s->viewToDoc(view.x(), view.y());
        s->wheelZoom(120, view.x(), view.y());
        QVERIFY(s->zoom() > 0.6);
        QVERIFY(!s->fit());
        const QPointF after = s->viewToDoc(view.x(), view.y());
        QVERIFY(qAbs(after.x() - before.x()) < 1e-6 && qAbs(after.y() - before.y()) < 1e-6);
    }

    void panMovesTheShotAndLeavesFit() {
        const int id = stack().add(pngOf(bigShot()), when);
        EditorSession *s = editor().open(id);
        s->setViewportSize(1280, 698);
        s->zoomTo(1);
        const QPointF o = s->origin();
        s->panBy(-100, -30);
        QCOMPARE(s->origin(), o + QPointF(-100, -30));
        s->panBy(-100000, 0); // cannot be pulled out of reach
        QVERIFY(s->origin().x() + 2000 >= 40 - 1e-6);
    }

    void theHandToolIsOfferedOnlyWhileZoomedPastFit() {
        const int id = stack().add(pngOf(bigShot()), when);
        EditorSession *s = editor().open(id);
        s->setViewportSize(1280, 698); // Fit: 0.6
        QVERIFY(!s->handAvailable());
        const QString before = s->tool();
        s->setTool("hand"); // G at Fit does nothing
        QCOMPARE(s->tool(), before);
        s->zoomTo(1);
        QVERIFY(s->handAvailable());
        s->setTool("hand");
        QCOMPARE(s->tool(), QString("hand"));
        bool listed = false;
        for (const QVariant &t : s->tools())
            listed |= t.toMap().value("name") == "hand" && t.toMap().value("key") == "G";
        QVERIFY(listed);
        QCOMPARE(s->tools().at(1).toMap().value("name").toString(), QString("hand")); // next to Select, as in Figma
        s->zoomOut(); // 50% is below Fit's 60%: the shot fits again
        QVERIFY(!s->handAvailable());
    }

    void aLeftDragWithHandPansAndNeverDraws() {
        const int id = stack().add(pngOf(bigShot()), when);
        EditorSession *s = editor().open(id);
        s->setViewportSize(1280, 698);
        s->zoomTo(1);
        s->setTool("hand");
        s->pointerPressed(500, 400);
        s->pointerMoved(520, 410);
        s->pointerReleased(520, 410);
        QCOMPARE(s->annotationCount(), 0);
        QVERIFY(!s->canUndo());
    }

    void backToFitRestoresThePreviousTool() {
        const int id = stack().add(pngOf(bigShot()), when);
        EditorSession *s = editor().open(id);
        s->setViewportSize(1280, 698);
        s->setTool("arrow");
        s->zoomTo(1);
        s->setTool("hand");
        s->zoomFit();
        QCOMPARE(s->tool(), QString("arrow"));
        s->zoomTo(1);
        s->setTool("hand");
        s->zoomTo(0.25); // zooming out until it fits
        QCOMPARE(s->tool(), QString("arrow"));
    }

    void aShotSmallerThanTheViewStaysCentered() {
        const int id = addShot();
        EditorSession *s = editor().open(id);
        s->setViewportSize(1280, 650);
        s->panBy(300, 300);
        QCOMPARE(s->origin(), QPointF((1280 - 96) / 2.0, (650 - 64) / 2.0));
    }

    void drawingIsInShotPixelsWhateverTheZoom() {
        const int id = stack().add(pngOf(bigShot()), when);
        EditorSession *s = editor().open(id);
        s->setViewportSize(1280, 698); // Fit: 0.6
        const QPointF a = s->viewToDoc(s->docToView(100, 100).x(), s->docToView(100, 100).y());
        QVERIFY(qAbs(a.x() - 100) < 1e-6);
        QVERIFY(qAbs(s->viewToDoc(40, 49).x()) < 1e-6); // the shot's left edge sits at x = 40
    }

    void zoomedOutTheShotIsAreaAveraged() {
        // Mean gray difference from Qt's own area-averaged downscale at the same size. Thin
        // lines are the hard case (a copy at 1/2 then bilinear is close, not equal: old code ~64);
        // smooth content must match almost exactly.
        auto meanDiff = [](const QImage &shot, qreal zoom) {
            tarn::CaptureDocument doc(shot);
            const QSize out(qRound(shot.width() * zoom), qRound(shot.height() * zoom));
            const QImage got = paintedAt(doc, zoom, out);
            const QImage want = shot.scaled(out, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            qint64 diff = 0;
            for (int y = 0; y < out.height(); ++y)
                for (int x = 0; x < out.width(); ++x)
                    diff += qAbs(qGray(got.pixel(x, y)) - qGray(want.pixel(x, y)));
            return double(diff) / (out.width() * out.height());
        };
        const double lines = meanDiff(thinLines(), 0.4);
        QVERIFY2(lines <= 28.0, qPrintable(QString::number(lines)));
        QImage ramp(600, 450, QImage::Format_RGB32);
        for (int y = 0; y < ramp.height(); ++y)
            for (int x = 0; x < ramp.width(); ++x)
                ramp.setPixel(x, y, qRgb(x / 3, y / 2, (x + y) / 5));
        const double smooth = meanDiff(ramp, 0.4);
        QVERIFY2(smooth <= 2.0, qPrintable(QString::number(smooth)));
    }

    void onePixelStrokesSurviveAtLowZoom() {
        tarn::CaptureDocument doc(thinLines());
        const QSize out(180, 135); // 0.3
        const QImage got = paintedAt(doc, 0.3, out);
        const QImage want = doc.shot().scaled(out, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        auto darkPixels = [&](const QImage &i) {
            int n = 0;
            for (int y = 0; y < out.height(); ++y)
                for (int x = 0; x < out.width(); ++x)
                    n += qGray(i.pixel(x, y)) < 215;
            return n;
        };
        // Area averaging keeps every stroke as a gray line; skipping source pixels drops most.
        QVERIFY2(darkPixels(got) >= darkPixels(want) * 0.8, qPrintable(QString("%1 of %2").arg(darkPixels(got)).arg(darkPixels(want))));
    }

    void theShrunkCopiesAreBuiltOncePerShot() {
        tarn::CaptureDocument doc(thinLines());
        QCOMPARE(doc.shrunkCopiesBuilt(), 0);
        paintedAt(doc, 1.0, QSize(600, 450));
        QCOMPARE(doc.shrunkCopiesBuilt(), 0); // zoom 1 never uses them
        paintedAt(doc, 0.4, QSize(240, 180));
        const int built = doc.shrunkCopiesBuilt();
        QVERIFY(built > 0);
        paintedAt(doc, 0.4, QSize(240, 180));
        paintedAt(doc, 0.3, QSize(180, 135));
        QCOMPARE(doc.shrunkCopiesBuilt(), built);
    }

    void aFullScreenShotZoomsOutWithoutLag() {
        QImage shot(2560, 1440, QImage::Format_RGB32);
        for (int y = 0; y < shot.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(shot.scanLine(y));
            for (int x = 0; x < shot.width(); ++x)
                row[x] = qRgb((x * 7) & 255, (y * 5) & 255, (x ^ y) & 255);
        }
        tarn::CaptureDocument doc(shot);
        QElapsedTimer t;
        t.start();
        paintedAt(doc, 0.5, QSize(1280, 720)); // first paint builds the copy
        const qint64 first = t.elapsed();
        t.restart();
        paintedAt(doc, 0.5, QSize(1280, 720));
        const qint64 later = t.elapsed();
        qInfo("2560x1440 at 0.5x: first paint %lld ms, later paint %lld ms", first, later);
        QVERIFY2(first < 1000, "the first zoomed-out paint builds the 1/2 copy and must stay fast");
    }
};

QTEST_MAIN(EditorTest)
#include "editor_test.moc"
