// The Pin window (QML, compiled in), driven with the same mouse, wheel and
// key events a user makes, off-screen. What this cannot check is needs-eyes: the
// real compositor (floating, above every window on every workspace, opening at
// the captured spot, being dragged by startSystemMove) and how scroll feels.
#include "hyprland_input.h"
#include "test_env.h"

#include "editor/editor.h"
#include "palette/palette.h"
#include "pin/pins.h"
#include "pin_windows.h"
#include "thumbnail_surface.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
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

// The card of one Thumbnail (a leaving card stays on screen for a while).
QQuickItem *cardOf(QQuickItem *from, int captureId) {
    if (from->objectName() == "card" && from->property("captureId").toInt() == captureId)
        return from;
    for (QQuickItem *child : from->childItems())
        if (QQuickItem *hit = cardOf(child, captureId))
            return hit;
    return nullptr;
}

QByteArray pngOf(int w, int h) {
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(Qt::darkCyan);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

struct Fixture {
    TestEnv env;
    tarn::ThumbnailStack &stack = env.daemon().service<tarn::ThumbnailStack>();
    tarn::Pins &pins = env.daemon().service<tarn::Pins>();
    tarn::PinWindows windows{pins, env.daemon().service<tarn::Palette>()};

    // Pins a 400 x 300 px capture of a 200 x 150 area (scale 2) at (300, 200) and waits for its window.
    int pin(QQuickView **view) {
        const int thumb = stack.add(pngOf(400, 300), env.daemon().now());
        stack.setOrigin(thumb, QRect(300, 200, 200, 150));
        const int id = pins.pin(thumb);
        *view = windows.viewFor(id);
        if (!*view || (*view)->status() != QQuickView::Ready || !QTest::qWaitForWindowExposed(*view))
            return -1;
        QTest::qWait(50);
        return id;
    }
    static QPoint centerOf(QQuickItem *item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); }
    // The offscreen pointer is global: off the spot first, then onto it.
    static void hoverOver(QQuickView *view, QPoint at = QPoint(60, 60)) {
        QTest::mouseMove(view, QPoint(0, 0));
        QTest::mouseMove(view, at);
    }
    static void wheel(QQuickView *view, QPoint at, int delta, Qt::KeyboardModifiers mods = {}) {
        tarn::test::sendWheel(view, at, delta, mods);
    }
    int evals() const {
        int n = 0;
        for (const auto &call : env.calls("hyprctl"))
            n += !call.isEmpty() && call.first() == "eval";
        return n;
    }
};

} // namespace

class PinUiTest : public QObject {
    Q_OBJECT
private slots:
    void aWindowClosedByTheCompositorClosesThePin() {
        Fixture f;
        QQuickView *view = nullptr;
        const int id = f.pin(&view);
        QVERIFY(id > 0);
        view->close(); // Hyprland's Super+Q
        QVERIFY(!f.pins.ids().contains(id));
        QTRY_VERIFY(!f.windows.viewFor(id));
    }

    void theWindowIsFramelessTitledForTheWindowRuleAndSizedBeforeItShows() {
        Fixture f;
        QQuickView *view = nullptr;
        const int id = f.pin(&view);
        QVERIFY(id > 0);
        QCOMPARE(view->title(), QString("Tarn pin %1").arg(id)); // what tarn.lua's window rule matches
        QVERIFY(view->flags() & Qt::FramelessWindowHint);
        QCOMPARE(view->size(), QSize(200, 150)); // the captured size, not the pixel size
        auto *shot = findItem(view->rootObject(), "shot");
        QVERIFY(shot);
        QCOMPARE(shot->property("source").toString(), QString("image://pin/%1").arg(id));
        QCOMPARE(shot->property("fillMode").toInt(), 1); // Image.PreserveAspectFit: never stretched
        QTRY_COMPARE(shot->property("status").toInt(), 1); // Image.Ready
    }

    // Shrinking a Pin must stay sharp: once the size settles, the picture is resampled
    // to the window's pixels (smooth, area-averaged), not left to GPU mipmap filtering.
    void aShrunkPinIsResampledToItsNewSizeNotMipmapped() {
        Fixture f;
        QQuickView *view = nullptr;
        const int id = f.pin(&view);
        QVERIFY(id > 0);
        auto *shot = findItem(view->rootObject(), "shot");
        QTRY_COMPARE(shot->property("status").toInt(), 1);
        QVERIFY(!shot->property("mipmap").toBool());
        view->resize(100, 75);
        const qreal dpr = view->devicePixelRatio();
        QTRY_COMPARE(shot->property("implicitWidth").toReal(), qRound(100 * dpr) * 1.0);
        QCOMPARE(shot->property("implicitHeight").toReal(), qRound(75 * dpr) * 1.0);
        QTRY_COMPARE(shot->property("status").toInt(), 1);
    }

    void ctrlScrollChangesTheOpacityAndPlainScrollResizes() {
        Fixture f;
        QQuickView *view = nullptr;
        const int id = f.pin(&view);
        QVERIFY(id > 0);
        auto *shot = findItem(view->rootObject(), "shot");
        QCOMPARE(shot->property("opacity").toDouble(), 1.0);

        Fixture::wheel(view, QPoint(100, 75), -120, Qt::ControlModifier);
        QVERIFY(qFuzzyCompare(f.pins.opacity(id), 0.95));
        QTRY_VERIFY(qFuzzyCompare(shot->property("opacity").toDouble(), 0.95));
        QCOMPARE(f.evals(), 1); // only the opening rule

        Fixture::wheel(view, QPoint(100, 75), 120);
        QTRY_COMPARE(f.evals(), 2); // a resize: Hyprland was asked (after the 30 ms coalescing)
        QVERIFY(qFuzzyCompare(f.pins.opacity(id), 0.95)); // and the opacity did not move
    }

    void middleClickClosesIt() {
        Fixture f;
        QQuickView *view = nullptr;
        const int id = f.pin(&view);
        QVERIFY(id > 0);
        QTest::mouseClick(view, Qt::MiddleButton, {}, QPoint(100, 75));
        QVERIFY(!f.pins.has(id));
        QVERIFY(!f.windows.viewFor(id));
    }

    void theHoverXClosesIt() {
        Fixture f;
        QQuickView *view = nullptr;
        const int id = f.pin(&view);
        QVERIFY(id > 0);
        auto *close = findItem(view->rootObject(), "closeButton");
        QVERIFY(close);
        Fixture::hoverOver(view);
        QTRY_VERIFY(close->isVisible());
        QTest::mouseClick(view, Qt::LeftButton, {}, Fixture::centerOf(close));
        QVERIFY(!f.pins.has(id));
    }

    void escClosesIt() {
        Fixture f;
        QQuickView *view = nullptr;
        const int id = f.pin(&view);
        QVERIFY(id > 0);
        QTest::keyClick(view, Qt::Key_Escape);
        QVERIFY(!f.pins.has(id));
    }

    void ctrlCCopiesAndTheThePinStays() {
        Fixture f;
        QQuickView *view = nullptr;
        const int id = f.pin(&view);
        QVERIFY(id > 0);
        QTest::keyClick(view, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(f.env.calls("tarn-clip").size(), 1);
        QVERIFY(f.pins.has(id));
    }

    // A GIF Pin loads its own GIF file in an AnimatedImage (uncached) and binds to it.
    void aGifPinPlaysItsGifUncachedAndItsMenuHasNoEdit() {
        Fixture f;
        QDir().mkpath(f.env.keptRecordings());
        const QString gif = f.env.keptRecordings() + "/2026-11-03_09-30-15.gif";
        QFile::copy(QStringLiteral(TARN_STUB_DIR "/fixtures/gifski/sample.gif"), gif);
        const int rec = f.stack.addRecording(pngOf(400, 300), f.env.daemon().now(), gif, 3000);
        f.stack.setOrigin(rec, QRect(300, 200, 200, 150));
        const int id = f.pins.pin(rec);
        QVERIFY(id > 0);
        QQuickView *view = f.windows.viewFor(id);
        QVERIFY(view && view->status() == QQuickView::Ready && QTest::qWaitForWindowExposed(view));
        QCOMPARE(view->size(), QSize(200, 150)); // full size as recorded
        auto *anim = findItem(view->rootObject(), "gifShot");
        QVERIFY(anim);
        QVERIFY(anim->isVisible());
        QCOMPARE(anim->property("source").toUrl(), QUrl::fromLocalFile(f.pins.gifPath(id)));
        QVERIFY(!anim->property("cache").toBool()); // the default caches every frame
        QVERIFY(anim->property("playing").toBool());
        QTRY_COMPARE(anim->property("status").toInt(), 1); // Image.Ready
        QVERIFY(!findItem(view->rootObject(), "shot")->isVisible());

        QTest::mouseClick(view, Qt::RightButton, {}, QPoint(20, 20));
        auto *menu = findItem(view->rootObject(), "menu");
        QTRY_VERIFY(menu->isVisible());
        for (const char *row : {"menuCopy", "menuSaveAs", "menuLock", "menuCloseAll"})
            QVERIFY2(findItem(menu, row), row);
        QVERIFY(!findItem(menu, "menuEdit"));

        // Ctrl+scroll changes the opacity, as on an image Pin.
        Fixture::wheel(view, QPoint(100, 75), -120, Qt::ControlModifier);
        QTRY_VERIFY(anim->property("opacity").toDouble() < 1.0);
    }

    void theThumbnailOffersPinOnAGifRecordingOnly() {
        TestEnv env;
        auto &stack = env.daemon().service<tarn::ThumbnailStack>();
        QDir().mkpath(env.keptRecordings());
        const QString gif = env.keptRecordings() + "/a.gif";
        QFile::copy(QStringLiteral(TARN_STUB_DIR "/fixtures/gifski/sample.gif"), gif);
        const int g = stack.addRecording(pngOf(400, 300), env.daemon().now(), gif, 3000);
        const int m = stack.addRecording(pngOf(400, 300), env.daemon().now(), env.scratch() + "/x.mp4", 3000);
        QVERIFY(stack.pinnable(g));
        QVERIFY(!stack.pinnable(m));
    }

    void rightClickOffersCopySaveAsEditAndCloseAllPins() {
        Fixture f;
        QQuickView *view = nullptr;
        const int id = f.pin(&view);
        QVERIFY(id > 0);
        auto *root = view->rootObject();
        auto *menu = findItem(root, "menu");
        QVERIFY(menu);
        QVERIFY(!menu->isVisible());
        QTest::mouseClick(view, Qt::RightButton, {}, QPoint(20, 20));
        QTRY_VERIFY(menu->isVisible());
        for (const char *row : {"menuCopy", "menuSaveAs", "menuEdit", "menuCloseAll"})
            QVERIFY2(findItem(menu, row), row);
        QCOMPARE(menu->childItems().size(), 1); // the one Column of rows: no more entries

        QTest::mouseClick(view, Qt::LeftButton, {}, Fixture::centerOf(findItem(menu, "menuCopy")));
        QCOMPARE(f.env.calls("tarn-clip").size(), 1);
        QVERIFY(!menu->isVisible());
        QVERIFY(f.pins.has(id));
    }

    void closeAllPinsClosesEveryPin() {
        Fixture f;
        QQuickView *first = nullptr, *second = nullptr;
        const int a = f.pin(&first);
        const int b = f.pin(&second);
        QVERIFY(a > 0 && b > 0);
        QTest::mouseClick(second, Qt::RightButton, {}, QPoint(20, 20));
        auto *row = findItem(second->rootObject(), "menuCloseAll");
        QTRY_VERIFY(row->isVisible());
        QTest::mouseClick(second, Qt::LeftButton, {}, Fixture::centerOf(row));
        QVERIFY(f.pins.ids().isEmpty());
    }

    // ---- Locked ----

    static void windowOf(Fixture &f, int id) {
        f.env.setFixture("hyprctl", "clients.json", QString(R"([{"address":"0xa1","title":"Tarn pin %1","at":[0,0],"size":[10,10]}])").arg(id).toUtf8());
    }

    void lKeyLocksAndTheBadgeShows() {
        Fixture f;
        QQuickView *view = nullptr;
        const int id = f.pin(&view);
        QVERIFY(id > 0);
        windowOf(f, id);
        auto *badge = findItem(view->rootObject(), "lockBadge");
        QVERIFY(badge && !badge->isVisible());
        QTest::keyClick(view, Qt::Key_L);
        QVERIFY(f.pins.isLocked(id));
        QTRY_VERIFY(badge->isVisible());
        auto *close = findItem(view->rootObject(), "closeButton");
        QCOMPARE(badge->width(), close->width());
        QCOMPARE(badge->x(), 8.0); // top-left
        Fixture::hoverOver(view);
        QVERIFY(!close->isVisible()); // no hover x on a Locked Pin
    }

    void aLockedPinIgnoresClicksAndScroll() {
        Fixture f;
        QQuickView *view = nullptr;
        const int id = f.pin(&view);
        windowOf(f, id);
        QVERIFY(f.pins.lock(id));
        const int evals = f.evals();
        Fixture::wheel(view, QPoint(100, 75), 120);
        Fixture::wheel(view, QPoint(100, 75), -120, Qt::ControlModifier);
        QTest::mouseClick(view, Qt::MiddleButton, {}, QPoint(100, 75));
        QTest::mouseClick(view, Qt::RightButton, {}, QPoint(20, 20));
        QTest::qWait(80);
        QVERIFY(f.pins.has(id));
        QCOMPARE(f.pins.opacity(id), 1.0);
        QCOMPARE(f.evals(), evals);
        QVERIFY(!findItem(view->rootObject(), "menu")->isVisible());
    }

    void theMenuLocksAPin() {
        Fixture f;
        QQuickView *view = nullptr;
        const int id = f.pin(&view);
        windowOf(f, id);
        QTest::mouseClick(view, Qt::RightButton, {}, QPoint(20, 20));
        auto *row = findItem(view->rootObject(), "menuLock");
        QTRY_VERIFY(row && row->isVisible());
        QTest::mouseClick(view, Qt::LeftButton, {}, Fixture::centerOf(row));
        QVERIFY(f.pins.isLocked(id));
    }

    void editInTheMenuOpensTheEditorOnTheImage() {
        Fixture f;
        QQuickView *view = nullptr;
        const int id = f.pin(&view);
        QVERIFY(id > 0);
        auto &editor = f.env.daemon().service<tarn::Editor>();
        QSignalSpy opened(&editor, &tarn::Editor::opened);
        QTest::mouseClick(view, Qt::RightButton, {}, QPoint(20, 20));
        auto *row = findItem(view->rootObject(), "menuEdit");
        QTRY_VERIFY(row->isVisible());
        QTest::mouseClick(view, Qt::LeftButton, {}, Fixture::centerOf(row));
        QVERIFY(!f.pins.has(id));
        QCOMPARE(opened.size(), 1);
    }

    void theThumbnailsPinButtonPinsItAndARecordingHasNone() {
        TestEnv env;
        auto &stack = env.daemon().service<tarn::ThumbnailStack>();
        auto &pins = env.daemon().service<tarn::Pins>();
        tarn::ThumbnailSurface surface(stack, env.daemon().service<tarn::Palette>());
        const int shot = stack.add(pngOf(400, 300), env.daemon().now());
        stack.setOrigin(shot, QRect(300, 200, 200, 150));
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        QQuickItem *card = nullptr;
        QTRY_VERIFY((card = findItem(surface.view().rootObject(), "card")));
        QTRY_COMPARE_WITH_TIMEOUT(card->x(), 16.0, 1500); // done springing in
        QTest::mouseMove(&surface.view(), QPoint(0, 0));
        QTest::mouseMove(&surface.view(), card->mapToScene(QPointF(card->width() - 20, card->height() - 20)).toPoint());
        auto *button = findItem(card, "pinButton");
        QVERIFY(button);
        QTRY_VERIFY(button->isVisible());
        QTRY_VERIFY(findItem(card, "controls")->property("opacity").toDouble() > 0.99);
        QTest::mouseClick(&surface.view(), Qt::LeftButton, {}, Fixture::centerOf(button));
        QCOMPARE(pins.ids().size(), 1);
        QCOMPARE(stack.count(), 0);

        // A Recording's card has no Pin button.
        const int rec = stack.addRecording(pngOf(400, 300), env.daemon().now(), env.scratch() + "/x.mp4");
        QVERIFY(!stack.pinnable(rec));
        QTRY_VERIFY(cardOf(surface.view().rootObject(), rec));
        QVERIFY(!findItem(cardOf(surface.view().rootObject(), rec), "pinButton")->isVisible());
    }
};

QTEST_MAIN(PinUiTest)
#include "pin_ui_test.moc"
