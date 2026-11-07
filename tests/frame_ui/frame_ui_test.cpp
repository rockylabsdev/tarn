// The Background panel in the Editor window (QML, compiled in), driven with
// the mouse and keys a user makes, off-screen. What this cannot judge is
// needs-eyes: the panel's look against the prototype and how the canvas push
// feels on the real monitor.
#include "test_env.h"

#include "editor/editor.h"
#include "editor_windows.h"
#include "palette/palette.h"
#include "state/remembered_state.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QQuickItem>
#include <QQuickView>
#include <QTest>

using tarn::test::TestEnv;

namespace {

QQuickItem *findItem(QQuickItem *from, const QString &objectName) {
    if (from->objectName() == objectName)
        return from;
    for (QQuickItem *child : from->childItems())
        if (QQuickItem *hit = findItem(child, objectName))
            return hit;
    return nullptr;
}

QByteArray pngOf(const QImage &image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

struct Fixture {
    TestEnv env;
    tarn::ThumbnailStack &stack = env.daemon().service<tarn::ThumbnailStack>();
    tarn::Editor &editor = env.daemon().service<tarn::Editor>();
    tarn::EditorWindows windows{editor, env.daemon().service<tarn::Palette>()};
    int id = [this] {
        QImage image(400, 300, QImage::Format_RGB32);
        image.fill(qRgb(40, 120, 200));
        return stack.add(pngOf(image), env.daemon().now());
    }();

    // A Palette file in use, the way a user's shell would write one.
    bool usePaletteFile() {
        QDir().mkpath(env.scratch() + "/state/shell");
        QFile f(env.scratch() + "/state/shell/palette.json");
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write("{\"primary\":\"#fbb974\",\"on_primary\":\"#492900\",\"surface_container_low\":\"#211a14\"}");
        f.close();
        QFile config(env.scratch() + "/config/tarn/config.toml");
        if (!config.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        config.write(("palette_file = \"" + env.scratch() + "/state/shell/palette.json\"\n").toUtf8());
        config.close();
        return true;
    }

    QQuickView *open() {
        stack.edit(id);
        QQuickView *view = windows.viewFor(id);
        if (!view || view->status() != QQuickView::Ready || !QTest::qWaitForWindowExposed(view))
            return nullptr;
        QTest::qWait(50);
        return view;
    }
    tarn::EditorSession *session() { return editor.session(id); }
    static QPoint centerOf(QQuickItem *item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); }
    static void click(QQuickView *view, QQuickItem *item) { QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(item)); }
};

} // namespace

class FrameUiTest : public QObject {
    Q_OBJECT
private slots:
    void thePanelIsClosedAtFirstAndTheBackgroundButtonOpensItAtOnce() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        QVERIFY(!findItem(root, "framePanel")->isVisible());
        Fixture::click(view, findItem(root, "backgroundButton"));
        QVERIFY(findItem(root, "framePanel")->isVisible());
        QCOMPARE(findItem(root, "framePanel")->width(), 284.0);
        Fixture::click(view, findItem(root, "backgroundButton"));
        QVERIFY(!findItem(root, "framePanel")->isVisible());
    }

    void theBKeyTogglesThePanel() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        QTest::keyClick(view, Qt::Key_B);
        QVERIFY(findItem(root, "framePanel")->isVisible());
        QTest::keyClick(view, Qt::Key_B);
        QVERIFY(!findItem(root, "framePanel")->isVisible());
    }

    void thePanelPushesTheCanvasOver() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        auto *canvas = findItem(root, "canvas");
        QCOMPARE(canvas->width(), 1280.0);
        QTest::keyClick(view, Qt::Key_B);
        QCOMPARE(canvas->width(), 1280.0 - 284);
        QCOMPARE(canvas->mapToScene(QPointF(0, 0)).x(), 284.0);
    }

    void clickingASwatchPutsAFrameRoundTheShot() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        QTest::keyClick(view, Qt::Key_B);
        QVERIFY(!f.session()->frameMap().value("on").toBool());
        Fixture::click(view, findItem(root, "frameSwatch-2")); // red
        QVERIFY(f.session()->frameMap().value("on").toBool());
        QCOMPARE(f.session()->frameMap().value("color").toString(), QString("#ffff453a"));
        QVERIFY(f.session()->documentSize() != QSize(400, 300));
        Fixture::click(view, findItem(root, "noneButton"));
        QCOMPARE(f.session()->documentSize(), QSize(400, 300));
        f.session()->undo(); // back to red
        QVERIFY(f.session()->frameMap().value("on").toBool());
    }

    void theCustomSwatchTogglesItsPopoverAndEscClosesIt() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        QTest::keyClick(view, Qt::Key_B);
        QObject *pop = root->findChild<QObject *>("customColorPopover");
        QVERIFY(pop);
        QQuickItem *swatch = findItem(root, "customSwatch");
        Fixture::click(view, swatch);
        QTRY_VERIFY(pop->property("opened").toBool());
        Fixture::click(view, swatch);
        QTest::qWait(100);
        QVERIFY(!pop->property("visible").toBool()); // closed, not closed-and-reopened
        Fixture::click(view, swatch);
        QTRY_VERIFY(pop->property("opened").toBool());
        QTest::keyClick(view, Qt::Key_Escape);
        QTRY_VERIFY(!pop->property("visible").toBool());
        QVERIFY(findItem(root, "framePanel")->isVisible()); // Esc only closed the popover
        QVERIFY(f.session() && f.session()->isOpen());
    }

    void theTransparentSwatchGivesATransparentFrame() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        QTest::keyClick(view, Qt::Key_B);
        Fixture::click(view, findItem(view->rootObject(), "frameSwatch-8"));
        QVERIFY(f.session()->document().flatten().hasAlphaChannel());
    }

    void withNoPaletteFileThePanelLeavesThePaletteSwatchesOut() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        QTest::keyClick(view, Qt::Key_B);
        auto *root = view->rootObject();
        QVERIFY(!f.env.daemon().service<tarn::Palette>().fromFile());
        QVERIFY(!findItem(root, "paletteSwatches")->isVisible());
        QVERIFY(!findItem(root, "paletteSwatchesCaption")->isVisible());
        QVERIFY(!findItem(root, "paletteSwatch-0")->isVisible());
    }

    void aFrameUsingAPaletteGradientStillOpensWithNoPaletteFile() {
        Fixture f;
        QVERIFY(!f.env.daemon().service<tarn::Palette>().fromFile());
        f.stack.edit(f.id);
        f.session()->setFrameBackground("gradient", {{"id", "accent-canvas"},
                                                     {"stops", QVariantList{"#fffbb974", "#ff211a14"}},
                                                     {"slots", QVariantList{"accent", "canvas"}},
                                                     {"angle", 135}});
        QQuickView *view = f.open();
        QVERIFY(view);
        QTest::keyClick(view, Qt::Key_B);
        QVERIFY(findItem(view->rootObject(), "framePanel")->isVisible());
        QVERIFY(f.session()->frameMap().value("on").toBool());
        const QImage out = f.session()->document().flatten();
        QVERIFY(out.width() > 400); // the Frame is still round the shot, painted from its saved stops
        QCOMPARE(out.pixelColor(2, 2).alpha(), 255);
    }

    void aPaletteSwatchKeepsItsSlotForPresets() {
        Fixture f;
        QVERIFY(f.usePaletteFile());
        QTRY_VERIFY(f.env.daemon().service<tarn::Palette>().fromFile());
        QQuickView *view = f.open();
        QVERIFY(view);
        QTest::keyClick(view, Qt::Key_B);
        QVERIFY(findItem(view->rootObject(), "paletteSwatches")->isVisible());
        QVERIFY(findItem(view->rootObject(), "paletteSwatchesCaption")->isVisible());
        Fixture::click(view, findItem(view->rootObject(), "paletteSwatch-0"));
        QCOMPARE(f.session()->frameMap().value("slot").toString(), QString("accent"));
    }

    void draggingASliderIsOneUndoStep() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        QTest::keyClick(view, Qt::Key_B);
        Fixture::click(view, findItem(root, "frameSwatch-1"));
        findItem(root, "frameScroll")->setProperty("contentY", 200); // the panel scrolls: the Saved row pushed the slider down
        auto *slider = findItem(root, "paddingSlider");
        const QPoint left = slider->mapToScene(QPointF(10, 12)).toPoint();
        const QPoint right = slider->mapToScene(QPointF(slider->width() - 4, 12)).toPoint();
        QTest::mousePress(view, Qt::LeftButton, {}, left);
        QTest::mouseMove(view, (left + right) / 2);
        QTest::mouseMove(view, right);
        QTest::mouseRelease(view, Qt::LeftButton, {}, right);
        QVERIFY(f.session()->frameMap().value("padding").toDouble() > 0.2);
        QCOMPARE(f.session()->document().history().undoCount(), 2);
        f.session()->undo();
        QCOMPARE(f.session()->frameMap().value("padding").toDouble(), 0.08);
    }

    void aSliderShowsAValueTipWhileItIsDraggedAndHidesItAfter() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        QTest::keyClick(view, Qt::Key_B);
        Fixture::click(view, findItem(root, "frameSwatch-1"));
        findItem(root, "frameScroll")->setProperty("contentY", 200); // the panel scrolls: the Saved row pushed the slider down
        auto *slider = findItem(root, "paddingSlider");
        auto *tip = slider->findChild<QObject *>("valueTip");
        QVERIFY(tip);
        QCOMPARE(tip->property("visible").toBool(), false);
        const QPoint left = slider->mapToScene(QPointF(10, 12)).toPoint();
        const QPoint right = slider->mapToScene(QPointF(slider->width() - 4, 12)).toPoint();
        QTest::mousePress(view, Qt::LeftButton, {}, left);
        QTest::mouseMove(view, (left + right) / 2);
        QTRY_COMPARE(tip->property("visible").toBool(), true);
        QCOMPARE(tip->property("text").toString(), QString::number(qRound(slider->property("value").toDouble() * 100)));
        QTest::mouseRelease(view, Qt::LeftButton, {}, (left + right) / 2);
        QTRY_COMPARE(tip->property("visible").toBool(), false);
    }

    void aAlignmentZoneAndTheAutoBalanceBoxSetTheirControls() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        QTest::keyClick(view, Qt::Key_B);
        Fixture::click(view, findItem(root, "frameSwatch-1"));
        // The panel scrolls (it grew with the Background tiles): bring the lower controls into view.
        findItem(root, "frameScroll")->setProperty("contentY", 600);
        QTest::qWait(60);
        Fixture::click(view, findItem(root, "alignZone-2")); // top right
        QCOMPARE(f.session()->frameMap().value("ah").toInt(), 1);
        QCOMPARE(f.session()->frameMap().value("av").toInt(), -1);
        Fixture::click(view, findItem(root, "autoBalance"));
        QVERIFY(f.session()->frameMap().value("autoBalance").toBool());
    }

    void theCanvasPaintsTheFramedImageTheExportWrites() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        Fixture::click(view, findItem(view->rootObject(), "backgroundButton"));
        Fixture::click(view, findItem(root, "frameSwatch-5")); // green
        f.session()->setFrameValue("shadow", 0);
        f.session()->setFrameValue("corners", 0);
        QTest::qWait(60);
        // At Fit the first pixel of the framed image is the Frame's color on screen.
        const QPointF o = f.session()->origin();
        auto *canvas = findItem(root, "canvas");
        const QImage screen = view->grabWindow();
        const QPoint at = (canvas->mapToScene(QPointF(0, 0)) + o + QPointF(2, 2)).toPoint();
        QCOMPARE(screen.pixelColor(at), QColor("#30d158"));
    }

    // The Watermark section at the bottom of the panel loads and binds to the session.
    void theWatermarkSectionLoadsAndBindsToTheSession() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        QTest::keyClick(view, Qt::Key_B);
        QVERIFY(findItem(root, "watermarkSection"));
        QVERIFY(findItem(root, "watermarkMode-off") && findItem(root, "watermarkMode-image") && findItem(root, "watermarkMode-text"));
        QVERIFY(!findItem(root, "watermarkCorners")->isVisible()); // Off: nothing more to set
        auto *scroll = findItem(root, "frameScroll");
        scroll->setProperty("contentY", scroll->property("contentHeight").toDouble() - scroll->height()); // the section is at the bottom
        QTest::qWait(30);
        Fixture::click(view, findItem(root, "watermarkMode-text"));
        QCOMPARE(f.session()->frameMap().value("watermark").toMap().value("mode").toString(), QString("text"));
        QTest::qWait(60); // the rows below the mode buttons appear and settle
        scroll->setProperty("contentY", scroll->property("contentHeight").toDouble() - scroll->height());
        QTest::qWait(30);
        QVERIFY(findItem(root, "watermarkCorners")->isVisible());
        QVERIFY(findItem(root, "watermarkSize")->isVisible() && findItem(root, "watermarkOpacity")->isVisible());
        QVERIFY(findItem(root, "watermarkText")->isVisible());
        Fixture::click(view, findItem(root, "watermarkCorner-0"));
        QCOMPARE(f.session()->frameMap().value("watermark").toMap().value("corner").toInt(), 0);
        f.session()->undo(); // the corner
        QCOMPARE(f.session()->frameMap().value("watermark").toMap().value("corner").toInt(), 3);
        f.session()->undo(); // the mode
        QCOMPARE(f.session()->frameMap().value("watermark").toMap().value("mode").toString(), QString("off"));
    }

    // Typing a text Watermark shows it as you type; Enter (or leaving the field)
    // ends it, and the whole typing is one undo step.
    void aTextWatermarkShowsAsYouTypeAndIsOneUndoStep() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        QTest::keyClick(view, Qt::Key_B);
        f.session()->setFrameValue("wmMode", "text");
        QTest::qWait(60);
        auto *scroll = findItem(root, "frameScroll");
        scroll->setProperty("contentY", scroll->property("contentHeight").toDouble() - scroll->height());
        QTest::qWait(30);
        auto *field = findItem(root, "watermarkText");
        QVERIFY(field && field->isVisible());
        Fixture::click(view, field);
        QTRY_VERIFY(field->hasActiveFocus());
        // Letters that are Editor shortcuts (B, K, E, C, I, [) and Backspace go to the field.
        for (char c : {'B', 'i', 'k', 'e', 'C', 'o', '['})
            QTest::keyClick(view, c);
        QTest::keyClick(view, Qt::Key_Backspace);
        auto wmText = [&] { return f.session() ? f.session()->frameMap().value("watermark").toMap().value("text").toString() : QString("<Editor closed>"); };
        QCOMPARE(wmText(), QString("BikeCo")); // live, before Enter
        QVERIFY(findItem(root, "framePanel")->isVisible());
        QTest::keyClick(view, Qt::Key_Return); // ends the typing, not the Editor
        QVERIFY(f.session() && f.session()->isOpen());
        QVERIFY(!field->hasActiveFocus());
        QCOMPARE(wmText(), QString("BikeCo"));
        f.session()->undo(); // the whole typing at once
        QCOMPARE(wmText(), QString());
        QCOMPARE(f.session()->frameMap().value("watermark").toMap().value("mode").toString(), QString("text"));
    }

    // The Watermark button and W put your last Watermark on a shot, or take it off.
    void theWatermarkButtonTurnsTheLastWatermarkOnAndOffOneUndoStepEach() {
        Fixture f;
        auto &state = f.env.daemon().service<tarn::RememberedState>();
        state.setValue("watermark.mode", "text");
        state.setValue("watermark.text", "@me");
        state.setValue("watermark.corner", 0);
        state.setValue("watermark.size", 0.1);
        state.setValue("watermark.opacity", 0.5);
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        auto *button = findItem(root, "watermarkButton");
        QVERIFY(button);
        auto wm = [&] { return f.session()->frameMap().value("watermark").toMap(); };
        QVERIFY(!button->property("active").toBool());
        Fixture::click(view, button);
        QCOMPARE(wm().value("mode").toString(), QString("text"));
        QCOMPARE(wm().value("text").toString(), QString("@me"));
        QCOMPARE(wm().value("corner").toInt(), 0);
        QCOMPARE(wm().value("size").toDouble(), 0.1);
        QCOMPARE(wm().value("opacity").toDouble(), 0.5);
        QVERIFY(button->property("active").toBool());
        QVERIFY(!findItem(root, "framePanel")->isVisible());
        QTest::keyClick(view, Qt::Key_W); // off again
        QCOMPARE(wm().value("mode").toString(), QString("off"));
        QVERIFY(!button->property("active").toBool());
        f.session()->undo(); // the turning off
        QCOMPARE(wm().value("mode").toString(), QString("text"));
        f.session()->undo(); // the turning on
        QCOMPARE(wm().value("mode").toString(), QString("off"));
        QVERIFY(!f.session()->canUndo());
    }

    void withNoWatermarkEverSetTheWKeyOpensThePanelAtTheWatermarkAndChangesNothing() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        QVERIFY(!findItem(root, "framePanel")->isVisible());
        QTest::keyClick(view, Qt::Key_W);
        QVERIFY(findItem(root, "framePanel")->isVisible());
        QTest::qWait(60);
        auto *scroll = findItem(root, "frameScroll");
        auto *section = findItem(root, "watermarkSection");
        const QPointF at = section->mapToItem(scroll->property("contentItem").value<QQuickItem *>(), QPointF(0, 0));
        QVERIFY(at.y() >= scroll->property("contentY").toDouble() - 1);
        QVERIFY(at.y() < scroll->property("contentY").toDouble() + scroll->height());
        QCOMPARE(f.session()->frameMap().value("watermark").toMap().value("mode").toString(), QString("off"));
        QVERIFY(!f.session()->canUndo());
    }

    void aWatermarkSetInTheEditorIsRememberedInStateTomlAndSurvivesARestart() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        f.session()->setFrameValue("wmMode", "text");
        f.session()->setFrameValue("wmText", "@me");
        f.session()->setFrameValue("wmCorner", 1);
        f.session()->dragFrameValue("wmSize", 0.12);
        f.session()->endFrameDrag();
        f.session()->setFrameValue("wmOpacity", 0.4);
        f.session()->setFrameValue("wmMode", "off"); // Off does not forget it
        tarn::RememberedState reopened(f.env.daemon().service<tarn::RememberedState>().path()); // a restart reads the file
        QCOMPARE(reopened.value("watermark.mode").toString(), QString("text"));
        QCOMPARE(reopened.value("watermark.text").toString(), QString("@me"));
        QCOMPARE(reopened.value("watermark.corner").toInt(), 1);
        QCOMPARE(reopened.value("watermark.size").toDouble(), 0.12);
        QCOMPARE(reopened.value("watermark.opacity").toDouble(), 0.4);
    }

    void anImageWatermarkIsRememberedByPathAndAMissingOneNotifiesOnceWhenToggledOn() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        const QString logo = f.env.pictures() + "/logo.png";
        QDir().mkpath(f.env.pictures());
        QImage img(20, 10, QImage::Format_RGB32);
        img.fill(Qt::red);
        QVERIFY(img.save(logo));
        QVERIFY(f.session()->setWatermarkImage(logo));
        QCOMPARE(f.env.daemon().service<tarn::RememberedState>().value("watermark.mode").toString(), QString("image"));
        QCOMPARE(f.env.daemon().service<tarn::RememberedState>().value("watermark.path").toString(), logo);
        f.session()->setFrameValue("wmMode", "off");
        QFile::remove(logo);
        const int before = f.env.calls("notify-send").size();
        QTest::keyClick(view, Qt::Key_W);
        QCOMPARE(f.env.calls("notify-send").size(), before + 1);
        QCOMPARE(f.session()->frameMap().value("watermark").toMap().value("mode").toString(), QString("off"));
    }
};

QTEST_MAIN(FrameUiTest)
#include "frame_ui_test.moc"
