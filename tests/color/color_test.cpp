// Color and sizes in the Editor. Core: the one shared color, recoloring
// the selection, the size steps, the eyedropper and state.toml, with exports
// compared against stored reference PNGs (regenerate with
// TARN_UPDATE_REFERENCES=1 build/tests/color/test_color, then look at them).
// UI: the color popover, the size select, [ and ], tooltips, driven with real
// mouse and key events off-screen.
#include "test_env.h"

#include "editor/editor.h"
#include "editor/tool_options.h"
#include "editor_windows.h"
#include "palette/palette.h"
#include "state/remembered_state.h"
#include "thumbnails/thumbnail_stack.h"

#include <QPainter>
#include <QBuffer>
#include <memory>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QQuickItem>
#include <QQuickView>
#include <QTest>

using tarn::EditorSession;
using tarn::ToolOptions;
using tarn::test::TestEnv;

#ifndef TARN_COLOR_REFERENCE_DIR
#error "set by tests/color/CMakeLists.txt"
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
    const QString path = QStringLiteral(TARN_COLOR_REFERENCE_DIR "/") + name;
    if (qEnvironmentVariableIsSet("TARN_UPDATE_REFERENCES")) {
        QVERIFY(actual.save(path, "PNG"));
        return;
    }
    QImage expected(path);
    QVERIFY2(!expected.isNull(), qPrintable("missing reference " + path));
    QCOMPARE(actual.size(), expected.size());
    QCOMPARE(actual.convertToFormat(QImage::Format_ARGB32), expected.convertToFormat(QImage::Format_ARGB32));
}

QQuickItem *findItem(QQuickItem *from, const QString &objectName) {
    if (from->objectName() == objectName)
        return from;
    for (QQuickItem *child : from->childItems())
        if (QQuickItem *hit = findItem(child, objectName))
            return hit;
    return nullptr;
}

void collect(QQuickItem *from, QList<QQuickItem *> &out) {
    out << from;
    for (QQuickItem *child : from->childItems())
        collect(child, out);
}

QPoint centerOf(QQuickItem *item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); }

} // namespace

class ColorTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;
    QDateTime when{QDate(2026, 11, 3), QTime(9, 30, 0)};
    int shots = 0;

    tarn::ThumbnailStack &stack() { return env->daemon().service<tarn::ThumbnailStack>(); }
    tarn::Editor &editor() { return env->daemon().service<tarn::Editor>(); }
    ToolOptions &options() { return env->daemon().service<ToolOptions>(); }
    int addShot() { return stack().add(pngOf(testShot()), when.addSecs(shots++)); }
    QString exportedFile(int n) const { return env->scratch() + "/cache/tarn/shared/" + when.addSecs(n).toString("yyyy-MM-dd_HH-mm-ss") + ".png"; }
    static void drawRectangle(EditorSession &s) {
        s.pointerPressed(16, 12);
        s.pointerMoved(50, 30);
        s.pointerMoved(80, 52);
        s.pointerReleased(80, 52);
    }
    // The color an Annotation paints its top edge with (the export closes the Editor, so
    // this paints the Annotation on its own).
    static QColor edgeColor(EditorSession &s, int id) {
        QImage img(100, 70, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::black);
        QPainter p(&img);
        s.document().annotation(id)->paint(p, img);
        p.end();
        return img.pixelColor(40, 12);
    }
    // Exports through Copy and returns the written image.
    QImage exportOf(EditorSession &s, int shotNumber) {
        if (!s.copyAndClose())
            return {};
        return QImage(exportedFile(shotNumber));
    }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        shots = 0;
    }
    void cleanup() { env.reset(); }

    // ---- the steps ----

    void sizeStepsMatchTheTable() {
        const QList<std::pair<QString, QList<int>>> table = {
            {"line", {2, 4, 6, 10, 16, 24}},
            {"text", {16, 22, 30, 40, 56, 80}},
            {"counter", {11, 14, 20, 24, 31, 45}},
            {"highlighter", {12, 18, 26, 36, 50, 70}},
        };
        QCOMPARE(ToolOptions::groups().size(), 4);
        for (const auto &row : table) {
            const auto *g = ToolOptions::group(row.first);
            QVERIFY(g);
            for (int i = 0; i < 6; ++i)
                QCOMPARE(g->steps[i], qreal(row.second[i]));
            // each group opens on its third
            QCOMPARE(options().stepIndex(row.first), 2);
            QCOMPARE(options().size(row.first), qreal(row.second[2]));
        }
        QCOMPARE(ToolOptions::group("line")->unit, QString("px"));
        QCOMPARE(ToolOptions::group("text")->unit, QString("pt"));
        QCOMPARE(ToolOptions::group("counter")->unit, QString("pt"));
        QCOMPARE(ToolOptions::group("highlighter")->unit, QString("px"));
    }

    void bracketsStepTheCurrentToolsSizeAndStopAtTheEnds() {
        EditorSession *s = editor().open(addShot());
        QCOMPARE(s->sizeGroup(), QString("line")); // Rectangle
        QCOMPARE(s->size(), 6.0);
        s->stepSize(1);
        QCOMPARE(s->size(), 10.0);
        QCOMPARE(s->sizeLabel(), QString("10 px"));
        for (int i = 0; i < 10; ++i)
            s->stepSize(1);
        QCOMPARE(s->size(), 24.0);
        for (int i = 0; i < 10; ++i)
            s->stepSize(-1);
        QCOMPARE(s->size(), 2.0);
    }

    void sizesAreKeptPerGroup() {
        options().step("line", 1);
        options().step("text", -1);
        QCOMPARE(options().size("line"), 10.0);
        QCOMPARE(options().size("text"), 22.0);
        QCOMPARE(options().size("counter"), 20.0); // untouched
        QCOMPARE(options().size("highlighter"), 26.0);
        const tarn::ToolStyle style = options().style();
        QCOMPARE(style.lineWidth, 10.0);
        QCOMPARE(style.textSize, 22.0);
    }

    // ---- color ----

    void oneColorIsSharedAcrossSessionsAndTools() {
        EditorSession *a = editor().open(addShot());
        EditorSession *b = editor().open(addShot());
        QCOMPARE(a->color(), QColor(0xFF, 0x45, 0x3A));
        a->setColor(QColor("#0a84ff"));
        QCOMPARE(b->color(), QColor("#0a84ff"));
        QCOMPARE(b->style().color, QColor("#0a84ff"));
    }

    void thereAreEightFixedSwatches() {
        EditorSession *s = editor().open(addShot());
        QCOMPARE(s->swatches().size(), 8);
        QCOMPARE(ToolOptions::fixedSwatches().size(), 8);
    }

    void pickingAColorRecolorsTheSelectedAnnotationOnly() {
        EditorSession *s = editor().open(addShot());
        drawRectangle(*s);
        const int first = s->document().items().back().id;
        // a second, separate Rectangle that stays red
        s->pointerPressed(2, 2);
        s->pointerMoved(10, 8);
        s->pointerReleased(10, 8);
        QCOMPARE(s->annotationCount(), 2);
        s->setSelection({first});
        s->setColor(QColor("#0a84ff"));

        const QImage out = exportOf(*s, 0);
        QVERIFY(!out.isNull());
        // the selected box's top edge is blue, the small one is still red
        QCOMPARE(out.pixelColor(40, 12), QColor("#0a84ff"));
        QCOMPARE(out.pixelColor(5, 2), QColor(0xFF, 0x45, 0x3A));
    }

    void recoloredExportMatchesTheReference() {
        EditorSession *s = editor().open(addShot());
        drawRectangle(*s);
        s->setSelection({s->document().items().back().id});
        s->setColor(QColor("#30d158"));
        compareWithReference(exportOf(*s, 0), "recolored.png");
    }

    void aColorWithOpacityDrawsTranslucent() {
        EditorSession *s = editor().open(addShot());
        s->setColor(QColor::fromRgba(qRgba(0xff, 0xff, 0xff, 0x80)));
        drawRectangle(*s);
        compareWithReference(exportOf(*s, 0), "translucent.png");
    }

    void withNothingSelectedTheColorOnlyChangesLaterDrawing() {
        EditorSession *s = editor().open(addShot());
        drawRectangle(*s);
        s->deselect(); // a drawn Annotation is selected
        s->setColor(QColor("#bf5af2"));
        const QImage out = exportOf(*s, 0);
        QCOMPARE(out.pixelColor(40, 12), QColor(0xFF, 0x45, 0x3A));
    }

    // ---- resize ----

    void thinestLineWidthMatchesTheReference() {
        EditorSession *s = editor().open(addShot());
        s->setSizeIndex(0);
        drawRectangle(*s);
        compareWithReference(exportOf(*s, 0), "width2.png");
    }

    void widestLineWidthMatchesTheReference() {
        EditorSession *s = editor().open(addShot());
        s->setSizeIndex(5);
        drawRectangle(*s);
        compareWithReference(exportOf(*s, 0), "width24.png");
    }

    // ---- eyedropper ----

    void theEyedropperSamplesTheShotNotTheAnnotations() {
        EditorSession *s = editor().open(addShot());
        drawRectangle(*s); // red line over the pixel at (40, 12)
        const QColor shotPixel = testShot().pixelColor(40, 12);
        QVERIFY(shotPixel != QColor(0xFF, 0x45, 0x3A));
        s->beginEyedropper();
        QVERIFY(s->eyedropper());
        const int before = s->annotationCount();
        s->pointerPressed(40.4, 12.7); // picks, does not draw
        s->pointerReleased(40.4, 12.7);
        QVERIFY(!s->eyedropper());
        QCOMPARE(s->color(), shotPixel);
        QCOMPARE(s->annotationCount(), before);
    }

    void aClickOutsideTheShotKeepsTheEyedropperWaiting() {
        EditorSession *s = editor().open(addShot());
        s->beginEyedropper();
        s->pointerPressed(-5, 10);
        QVERIFY(s->eyedropper());
        s->cancelEyedropper();
        QVERIFY(!s->eyedropper());
        QCOMPARE(s->color(), QColor(0xFF, 0x45, 0x3A));
    }

    void theEyedropperRecolorsTheSelectionToo() {
        EditorSession *s = editor().open(addShot());
        drawRectangle(*s);
        s->setSelection({s->document().items().back().id});
        s->beginEyedropper();
        s->pointerPressed(1, 1);
        const QImage out = exportOf(*s, 0);
        QCOMPARE(out.pixelColor(40, 12), testShot().pixelColor(1, 1));
    }

    // ---- state.toml ----

    void colorAndSizesSurviveARestart() {
        options().setColor(QColor("#0a84ff"));
        const QColor translucent = QColor::fromRgba(qRgba(0xbf, 0x5a, 0xf2, 0x80));
        options().setColor(translucent);
        options().step("line", 3);       // 24
        options().step("text", -2);      // 16
        options().step("counter", 1);    // 24
        options().step("highlighter", 3); // clamps to 70
        options().setStyleValue("counter_style", "ring");

        env->restartDaemon();
        QCOMPARE(options().color(), translucent);
        QCOMPARE(options().size("line"), 24.0);
        QCOMPARE(options().size("text"), 16.0);
        QCOMPARE(options().size("counter"), 24.0);
        QCOMPARE(options().size("highlighter"), 70.0);
        QCOMPARE(options().styleValue("counter_style").toString(), QString("ring"));
        // and a new Editor starts with them
        EditorSession *s = editor().open(addShot());
        QCOMPARE(s->color(), translucent);
        QCOMPARE(s->size(), 24.0);
    }

    void stateTomlHoldsTheValuesInReadableSections() {
        options().setColor(QColor("#0a84ff"));
        options().step("line", 1);
        QFile f(env->daemon().service<tarn::RememberedState>().path());
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY2(text.contains("[tool]"), qPrintable(text));
        QVERIFY2(text.contains("color = \"#ff0a84ff\""), qPrintable(text));
        QVERIFY2(text.contains("[size]"), qPrintable(text));
        QVERIFY2(text.contains("line = 3"), qPrintable(text));
    }

    void aGarbledSizeFallsBackToTheThird() {
        auto &state = env->daemon().service<tarn::RememberedState>();
        state.setValue("size.line", "huge");
        state.setValue("size.text", 99);
        state.setValue("tool.color", "not a color");
        env->restartDaemon();
        QCOMPARE(options().size("line"), 6.0);
        QCOMPARE(options().size("text"), 80.0); // clamped to the last step
        QCOMPARE(options().color(), QColor(0xFF, 0x45, 0x3A));
    }

    // ---- the Editor window ----

    // A Palette file in use before the Editor opens, the way a user's shell would write one.
    void usePaletteFile() {
        QDir().mkpath(env->scratch() + "/state/shell");
        QFile f(env->scratch() + "/state/shell/palette.json");
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{\"primary\":\"#fbb974\",\"on_primary\":\"#492900\",\"surface_container_low\":\"#211a14\"}");
        f.close();
        QFile config(env->scratch() + "/config/tarn/config.toml");
        QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Truncate));
        config.write(("palette_file = \"" + env->scratch() + "/state/shell/palette.json\"\n").toUtf8());
        config.close();
        QTRY_VERIFY(env->daemon().service<tarn::Palette>().fromFile());
    }

    void thePopoverLabelsItsPaletteSwatchesPaletteWhileAPaletteFileIsInUse() {
        usePaletteFile();
        const int id = addShot();
        tarn::EditorWindows windows{editor(), env->daemon().service<tarn::Palette>()};
        stack().edit(id);
        QQuickView *view = windows.viewFor(id);
        QVERIFY(view && QTest::qWaitForWindowExposed(view));
        QTest::qWait(50);
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(findItem(view->rootObject(), "colorButton")));
        QTRY_VERIFY(findItem(view->contentItem(), "swatch-0"));
        auto *label = findItem(view->contentItem(), "popoverPaletteLabel");
        QVERIFY(label && label->isVisible());
        QCOMPARE(label->property("text").toString(), QString("Palette"));
        QVERIFY(findItem(view->contentItem(), "popoverPaletteSwatches")->isVisible());
    }

    void withNoPaletteFileThePopoverLeavesThePaletteSwatchesOutWithNoGap() {
        const int id = addShot();
        tarn::EditorWindows windows{editor(), env->daemon().service<tarn::Palette>()};
        stack().edit(id);
        QQuickView *view = windows.viewFor(id);
        QVERIFY(view && QTest::qWaitForWindowExposed(view));
        QTest::qWait(50);
        QVERIFY(!env->daemon().service<tarn::Palette>().fromFile());
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(findItem(view->rootObject(), "colorButton")));
        QTRY_VERIFY(findItem(view->contentItem(), "swatch-0"));
        QQuickItem *scene = view->contentItem();
        QVERIFY(!findItem(scene, "popoverPaletteLabel")->isVisible());
        QVERIFY(!findItem(scene, "popoverPaletteSwatches")->isVisible());
        // no gap: the Saved label sits one column step below the fixed swatches, as if the Palette rows were not there
        auto *fixed = findItem(scene, "swatch-0");
        auto *saved = findItem(scene, "savedLabel");
        QVERIFY(saved);
        QCOMPARE(saved->mapToScene(QPointF(0, 0)).y() - fixed->mapToScene(QPointF(0, 0)).y(), fixed->height() + 10);
    }

    void thePopoverOffersAllFourSourcesAndSwatchesPickTheColor() {
        usePaletteFile();
        const int id = addShot();
        tarn::EditorWindows windows{editor(), env->daemon().service<tarn::Palette>()};
        stack().edit(id);
        QQuickView *view = windows.viewFor(id);
        QVERIFY(view && QTest::qWaitForWindowExposed(view));
        QTest::qWait(50);
        auto *root = view->rootObject();
        auto *session = editor().session(id);

        auto *button = findItem(root, "colorButton");
        QVERIFY(button);
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(button));
        QTRY_VERIFY(findItem(view->contentItem(), "swatch-0")); // the popover lives in the window's overlay
        QQuickItem *scene = view->contentItem();
        for (int i = 0; i < 8; ++i)
            QVERIFY2(findItem(scene, QString("swatch-%1").arg(i)), "fixed swatch");
        for (int i = 0; i < 6; ++i)
            QVERIFY2(findItem(scene, QString("palette-%1").arg(i)), "palette swatch");
        QVERIFY(findItem(scene, "hueSlider"));
        QVERIFY(findItem(scene, "opacitySlider"));
        QVERIFY(findItem(scene, "eyedropperButton"));

        // a fixed swatch sets the shared color
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(findItem(scene, "swatch-4")));
        QCOMPARE(session->color(), QColor("#0a84ff"));
        // a Palette swatch is the live Palette color
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(findItem(scene, "palette-0")));
        QCOMPARE(session->color(), env->daemon().service<tarn::Palette>().accent());
        // the opacity slider changes only the alpha
        auto *opacity = findItem(scene, "opacitySlider");
        const QRectF r(opacity->mapToScene(QPointF(0, 0)), QSizeF(opacity->width(), opacity->height()));
        QTest::mouseClick(view, Qt::LeftButton, {}, QPoint(int(r.left() + r.width() / 2), int(r.center().y())));
        QVERIFY(session->color().alphaF() > 0.4 && session->color().alphaF() < 0.6);
        QCOMPARE(session->color().rgb(), env->daemon().service<tarn::Palette>().accent().rgb());
    }

    // Dragging the hue slider: the knob stays under the pointer (no snapping back to the
    // hue re-read from the rounded color) and a gray, white or black gets that hue.
    QQuickItem *openPopover(QQuickView *&view, int &id, std::unique_ptr<tarn::EditorWindows> &windows) {
        id = addShot();
        windows = std::make_unique<tarn::EditorWindows>(editor(), env->daemon().service<tarn::Palette>());
        stack().edit(id);
        view = windows->viewFor(id);
        if (!view || !QTest::qWaitForWindowExposed(view))
            return nullptr;
        QTest::qWait(50);
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(findItem(view->rootObject(), "colorButton")));
        QTest::qWait(50);
        return findItem(view->contentItem(), "hueSlider");
    }
    void dragHue(QQuickView *view, QQuickItem *hue, const QList<qreal> &stops) {
        const QPointF origin = hue->mapToScene(QPointF(0, hue->height() / 2));
        auto at = [&](qreal v) { return QPoint(qRound(origin.x() + 8 + v * (hue->width() - 16)), qRound(origin.y())); };
        QTest::mousePress(view, Qt::LeftButton, {}, at(stops.first()));
        for (qreal v : stops)
            QTest::mouseMove(view, at(v));
        QTest::mouseRelease(view, Qt::LeftButton, {}, at(stops.last()));
    }

    void theHueKnobStaysWhereItIsDragged() {
        QQuickView *view = nullptr;
        int id = 0;
        std::unique_ptr<tarn::EditorWindows> windows;
        options().setColor(QColor("#ff0034"));
        QQuickItem *hue = openPopover(view, id, windows);
        QVERIFY(hue);
        const qreal step = 1.0 / (hue->width() - 16); // one pixel of the track
        dragHue(view, hue, {0.20, 0.20 + 17 * step, 0.20 + 33 * step, 0.20 + 51 * step});
        const qreal expected = qRound((0.20 + 51 * step) * (hue->width() - 16)) / (hue->width() - 16);
        QVERIFY2(qAbs(hue->property("value").toReal() - expected * 359.9 / 360) < 1e-6,
                 qPrintable(QString("knob %1, pointer %2").arg(hue->property("value").toReal()).arg(expected)));
        const QColor c = editor().session(id)->color();
        QVERIFY2(qAbs(c.hsvHueF() - expected * 359.9 / 360) < 0.01, qPrintable(c.name()));
    }

    // Hue on a gray moves only the hue: the color stays gray, and the hue is what the
    // saturation slider then brings in (as in Figma). Nothing jumps to full.
    void draggingHueOnAGrayKeepsItGrayUntilSaturationIsRaised() {
        QQuickView *view = nullptr;
        int id = 0;
        std::unique_ptr<tarn::EditorWindows> windows;
        options().setColor(QColor("#808080"));
        QQuickItem *hue = openPopover(view, id, windows);
        QVERIFY(hue);
        dragHue(view, hue, {0.30, 0.33});
        QCOMPARE(editor().session(id)->color(), QColor("#808080")); // still gray
        QVERIFY(qAbs(hue->property("value").toReal() - 0.33) < 0.01); // the knob stays where it was put
        QQuickItem *saturation = findItem(view->contentItem(), "saturationSlider");
        QVERIFY(saturation);
        QVERIFY(qAbs(saturation->property("value").toReal()) < 0.01); // saturation still at 0
        dragHue(view, saturation, {0.5, 1.0});
        const QColor c = editor().session(id)->color();
        QVERIFY2(c.hsvSaturation() > 240, qPrintable(c.name()));
        QVERIFY2(qAbs(c.hsvHueF() - 0.33) < 0.02, qPrintable(c.name())); // the hue chosen on the gray
        QCOMPARE(c.value(), 128); // brightness untouched
    }

    // A custom-color slider drag on a selection is ONE undo step.
    void aSliderDragOnASelectionIsOneUndoStep() {
        QQuickView *view = nullptr;
        int id = 0;
        std::unique_ptr<tarn::EditorWindows> windows;
        QQuickItem *hue = openPopover(view, id, windows);
        QVERIFY(hue);
        EditorSession *s = editor().session(id);
        drawRectangle(*s);
        const int rect = s->document().items().back().id;
        s->setSelection({rect});
        const QColor original = edgeColor(*s, rect);
        const int undoable = s->document().history().undoCount();
        dragHue(view, hue, {0.10, 0.20, 0.30, 0.40, 0.50});
        QVERIFY(edgeColor(*s, rect) != original); // live
        QCOMPARE(s->document().history().undoCount(), undoable + 1);
        s->undo();
        QCOMPARE(edgeColor(*s, rect), original);
        QCOMPARE(s->document().history().undoCount(), undoable);
    }

    void closingThePopoverMidDragStillRecordsOneStep() {
        EditorSession *s = editor().open(addShot());
        drawRectangle(*s);
        const int rect = s->document().items().back().id;
        s->setSelection({rect});
        const QColor original = edgeColor(*s, rect);
        const int undoable = s->document().history().undoCount();
        s->dragColor(QColor("#112233"));
        s->dragColor(QColor("#223344"));
        QCOMPARE(s->document().history().undoCount(), undoable);
        s->endColorDrag();
        s->endColorDrag(); // idempotent
        QCOMPARE(s->document().history().undoCount(), undoable + 1);
        s->undo();
        QCOMPARE(edgeColor(*s, rect), original);
    }

    void aSwatchClickIsStillOneUndoStep() {
        EditorSession *s = editor().open(addShot());
        drawRectangle(*s);
        const int rect = s->document().items().back().id;
        s->setSelection({rect});
        const int undoable = s->document().history().undoCount();
        s->setColor(QColor("#0a84ff"));
        QCOMPARE(s->document().history().undoCount(), undoable + 1);
    }

    void aSliderDragWithNothingSelectedRecordsNothing() {
        EditorSession *s = editor().open(addShot());
        drawRectangle(*s);
        s->deselect();
        const int undoable = s->document().history().undoCount();
        s->dragColor(QColor("#112233"));
        s->endColorDrag();
        QCOMPARE(s->color(), QColor("#112233"));
        QCOMPARE(s->document().history().undoCount(), undoable);
    }

    // Dragging a color slider writes state.toml once it settles, not on every step.
    void aColorWritesStateTomlOnceItSettles() {
        auto &state = env->daemon().service<tarn::RememberedState>();
        auto fileText = [&] {
            QFile f(state.path());
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        options().setColor(QColor("#0a84ff"));
        options().setColor(QColor("#0a85ff"));
        QVERIFY(!fileText().contains("#ff0a85ff")); // not yet
        QCOMPARE(options().color(), QColor("#0a85ff")); // but already in use
        QTRY_VERIFY(fileText().contains("color = \"#ff0a85ff\""));
        options().setColor(QColor("#0a86ff"));
        env->restartDaemon(); // a quit still writes it
        QCOMPARE(options().color(), QColor("#0a86ff"));
    }

    // The color button toggles its popover (a second click closes it and it stays closed),
    // and Esc closes it without stepping the Editor back. The size select too.
    void theButtonTogglesThePopoverAndEscClosesIt() {
        const int id = addShot();
        tarn::EditorWindows windows{editor(), env->daemon().service<tarn::Palette>()};
        stack().edit(id);
        QQuickView *view = windows.viewFor(id);
        QVERIFY(view && QTest::qWaitForWindowExposed(view));
        QTest::qWait(50);
        auto *root = view->rootObject();
        QObject *pop = findItem(root, "colorButton")->property("popup").value<QObject *>();
        QVERIFY(pop);
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(findItem(root, "colorButton")));
        QTRY_VERIFY(pop->property("opened").toBool());
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(findItem(root, "colorButton")));
        QTest::qWait(100);
        QVERIFY(!pop->property("visible").toBool()); // closed, not closed-and-reopened
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(findItem(root, "colorButton")));
        QTRY_VERIFY(pop->property("opened").toBool());
        QTest::keyClick(view, Qt::Key_Escape);
        QTRY_VERIFY(!pop->property("visible").toBool());
        QVERIFY(editor().session(id) && editor().session(id)->isOpen()); // Esc only closed the popover

        QQuickItem *size = findItem(root, "sizeMenu");
        QObject *menu = size->property("popup").value<QObject *>();
        QVERIFY(menu);
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(size));
        QTRY_VERIFY(menu->property("opened").toBool());
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(size));
        QTest::qWait(100);
        QVERIFY(!menu->property("visible").toBool());
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(size));
        QTRY_VERIFY(menu->property("opened").toBool());
        QTest::keyClick(view, Qt::Key_Escape);
        QTRY_VERIFY(!menu->property("visible").toBool());
        QVERIFY(editor().session(id) && editor().session(id)->isOpen());
    }

    void theEyedropperButtonThenACanvasClickPicksFromTheShot() {
        const int id = addShot();
        tarn::EditorWindows windows{editor(), env->daemon().service<tarn::Palette>()};
        stack().edit(id);
        QQuickView *view = windows.viewFor(id);
        QVERIFY(view && QTest::qWaitForWindowExposed(view));
        QTest::qWait(50);
        auto *session = editor().session(id);
        QTest::keyClick(view, Qt::Key_E);
        QVERIFY(session->eyedropper());
        auto *canvas = findItem(view->rootObject(), "canvas");
        const QPointF viewPoint = session->docToView(10.5, 20.5);
        const QPoint at = canvas->mapToScene(viewPoint).toPoint();
        const QPointF hit = session->viewToDoc(canvas->mapFromScene(at).x(), canvas->mapFromScene(at).y());
        QTest::mouseClick(view, Qt::LeftButton, {}, at);
        QVERIFY(!session->eyedropper());
        QCOMPARE(session->color(), testShot().pixelColor(qFloor(hit.x()), qFloor(hit.y())));
        QCOMPARE(session->annotationCount(), 0);

        QTest::keyClick(view, Qt::Key_E);
        QVERIFY(session->eyedropper());
        QTest::keyClick(view, Qt::Key_Escape); // steps back one level: only the eyedropper
        QVERIFY(!session->eyedropper());
        QVERIFY(session->isOpen());
    }

    void theTopBarShowsTheSizeSelectAndBracketsStepIt() {
        const int id = addShot();
        tarn::EditorWindows windows{editor(), env->daemon().service<tarn::Palette>()};
        stack().edit(id);
        QQuickView *view = windows.viewFor(id);
        QVERIFY(view && QTest::qWaitForWindowExposed(view));
        QTest::qWait(50);
        auto *session = editor().session(id);
        auto *menu = findItem(view->rootObject(), "sizeMenu");
        QVERIFY(menu && menu->isVisible());
        QCOMPARE(findItem(menu, "label")->property("text").toString(), QString("6 px"));
        QTest::keyClick(view, Qt::Key_BracketRight);
        QCOMPARE(session->size(), 10.0);
        QCOMPARE(findItem(menu, "label")->property("text").toString(), QString("10 px"));
        QTest::keyClick(view, Qt::Key_BracketLeft);
        QTest::keyClick(view, Qt::Key_BracketLeft);
        QCOMPARE(session->size(), 4.0);
        // the select's menu sets a step too
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(menu));
        QQuickItem *option = nullptr;
        QTRY_VERIFY((option = findItem(view->contentItem(), "option-5")) != nullptr);
        QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(option));
        QCOMPARE(session->size(), 24.0);
    }

    void everyButtonsTooltipShowsItsKey() {
        const int id = addShot();
        tarn::EditorWindows windows{editor(), env->daemon().service<tarn::Palette>()};
        stack().edit(id);
        QQuickView *view = windows.viewFor(id);
        QVERIFY(view && QTest::qWaitForWindowExposed(view));
        QList<QQuickItem *> all;
        collect(view->rootObject(), all);
        int checked = 0;
        for (QQuickItem *item : all) {
            const QVariant tip = item->property("tip");
            if (!tip.isValid() || tip.toString().isEmpty())
                continue;
            ++checked;
            QVERIFY2(tip.toString().contains('(') && tip.toString().contains(')'), qPrintable(item->objectName() + ": " + tip.toString()));
        }
        QVERIFY(checked >= 6); // tools, color, size, Save as, Done, zoom, Copy, close
        QVERIFY(findItem(view->rootObject(), "colorButton")->property("tip").toString().contains("(K)"));
    }
};

QTEST_MAIN(ColorTest)
#include "color_test.moc"
