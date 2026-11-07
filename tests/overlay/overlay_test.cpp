// Print opens the Overlay on a frozen image; dragging an area and letting
// go captures it to a Thumbnail; Esc or a right-click cancel. Layer-shell roles
// need a Wayland session, so the surface is checked only as far as it shows
// off-screen.
#include "test_env.h"

#include "overlay/overlay.h"
#include "overlay_surface.h"
#include "palette/palette.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QImage>
#include <QQuickItem>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

using tarn::Overlay;
using tarn::OverlayModel;
using tarn::ThumbnailStack;
using tarn::test::TestEnv;

namespace {
// A 128 x 80 image where every pixel differs, so a wrong crop shows.
QImage gradient() {
    QImage image(128, 80, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixel(x, y, qRgb(x * 2, y * 3, (x ^ y) * 4));
    return image;
}

QByteArray pngOf(const QImage &image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// One 128 x 80 monitor at scale 2: 64 x 40 logical pixels.
const QByteArray kMonitor = R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":128,"height":80,"scale":2.0,"transform":0}])";

QQuickItem *findItem(QQuickItem *from, const QString &objectName) {
    if (from->objectName() == objectName)
        return from;
    for (QQuickItem *child : from->childItems())
        if (QQuickItem *hit = findItem(child, objectName))
            return hit;
    return nullptr;
}
} // namespace

class OverlayTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    ThumbnailStack &stack() { return env->daemon().service<ThumbnailStack>(); }
    OverlayModel &model() { return env->daemon().service<Overlay>().model(); }
    bool press() { return env->request({"capture", "area"}).ok; }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        env->setFixture("hyprctl", "monitors.json", kMonitor);
        env->setFixture("grim", "output.png", pngOf(gradient()));
        env->setPointer(10, 10);
    }
    void cleanup() { env.reset(); }

    // The Overlay model on its own: events in, Selection or cancel out.
    void aPressDragReleaseIsAConfirmedSelection() {
        OverlayModel m;
        QSignalSpy confirmed(&m, &OverlayModel::confirmed);
        m.open(OverlayModel::Screenshot, QSize(64, 40));
        m.pointerPressed(10, 5, Qt::LeftButton);
        QVERIFY(!m.hasSelection());
        m.pointerMoved(30, 25);
        QCOMPARE(m.selection(), QRect(10, 5, 20, 20)); // follows the drag
        QVERIFY(confirmed.isEmpty());                  // nothing happens until the button is let go
        m.pointerReleased(30, 25, Qt::LeftButton);
        QCOMPARE(confirmed.count(), 1);
        QCOMPARE(confirmed.first().at(0).toRect(), QRect(10, 5, 20, 20));
        QCOMPARE(confirmed.first().at(1).value<OverlayModel::Mode>(), OverlayModel::Screenshot);
        QVERIFY(!m.active());
    }

    void draggingUpAndLeftGivesTheSameSelectionAndItStaysOnScreen() {
        OverlayModel m;
        QSignalSpy confirmed(&m, &OverlayModel::confirmed);
        m.open(OverlayModel::Screenshot, QSize(64, 40));
        m.pointerPressed(30, 25, Qt::LeftButton);
        m.pointerReleased(10, 5, Qt::LeftButton);
        QCOMPARE(confirmed.first().at(0).toRect(), QRect(10, 5, 20, 20));

        m.open(OverlayModel::Screenshot, QSize(64, 40));
        m.pointerPressed(50, 30, Qt::LeftButton);
        m.pointerReleased(500, 500, Qt::LeftButton); // dragged off the edge
        QCOMPARE(confirmed.last().at(0).toRect(), QRect(50, 30, 13, 9));
    }

    void aClickWithNoDragPicksNothingAndTheOverlayStaysOpen() {
        OverlayModel m;
        QSignalSpy confirmed(&m, &OverlayModel::confirmed);
        m.open(OverlayModel::Screenshot, QSize(64, 40));
        m.pointerPressed(10, 5, Qt::LeftButton);
        m.pointerReleased(10, 5, Qt::LeftButton);
        QVERIFY(confirmed.isEmpty());
        QVERIFY(m.active());
    }

    void escAndRightClickCancel() {
        OverlayModel m;
        QSignalSpy confirmed(&m, &OverlayModel::confirmed);
        QSignalSpy canceled(&m, &OverlayModel::canceled);
        m.open(OverlayModel::Screenshot, QSize(64, 40));
        m.keyPressed(Qt::Key_Escape);
        QCOMPARE(canceled.count(), 1);
        QVERIFY(!m.active());

        m.open(OverlayModel::Screenshot, QSize(64, 40));
        m.pointerPressed(10, 5, Qt::RightButton);
        QCOMPARE(canceled.count(), 2);

        m.open(OverlayModel::Screenshot, QSize(64, 40)); // Esc in the middle of a drag
        m.pointerPressed(10, 5, Qt::LeftButton);
        m.pointerMoved(30, 25);
        m.keyPressed(Qt::Key_Escape);
        m.pointerReleased(30, 25, Qt::LeftButton);
        QCOMPARE(canceled.count(), 3);
        QVERIFY(confirmed.isEmpty());
    }

    void theSizePillShowsDevicePixels() {
        OverlayModel m;
        m.open(OverlayModel::Screenshot, QSize(64, 40), 2.0);
        m.pointerPressed(10, 5, Qt::LeftButton);
        m.pointerMoved(30, 25);
        QCOMPARE(m.sizeLabel(), QString::fromUtf8("40 × 40"));
    }

    // The whole path through the daemon.
    void printGrabsTheFrozenImageOnceBeforeTheOverlayOpens() {
        QSignalSpy opened(&env->daemon().service<Overlay>(), &Overlay::opened);
        QVERIFY(press());
        QCOMPARE(env->calls("grim"), (QList<QStringList>{{"-o", "DP-1", "-t", "png", "-"}}));
        QCOMPARE(opened.count(), 1);
        QVERIFY(model().active());
        QCOMPARE(env->daemon().service<Overlay>().frozen().size(), QSize(128, 80));
        QCOMPARE(stack().count(), 0); // nothing captured yet
        QVERIFY(press());              // Print again while open: nothing new
        QCOMPARE(env->calls("grim").size(), 1);
    }

    void lettingGoCapturesTheCropOfGrimsFullResolutionImage() {
        env->setNow(QDateTime(QDate(2026, 11, 3), QTime(9, 30, 15)));
        QVERIFY(press());
        model().pointerPressed(10, 5, Qt::LeftButton);
        model().pointerMoved(30, 25);
        model().pointerReleased(30, 25, Qt::LeftButton);

        QCOMPARE(stack().count(), 1);
        QCOMPARE(env->calls("grim").size(), 1); // cropped, not grabbed again
        const QString path = stack().save(stack().ids().first());
        QCOMPARE(path, env->screenshots() + "/2026-11-03_09-30-15.png");
        // Logical (10,5)-(30,25) at scale 2 is (20,10) 40 x 40 in grim's image.
        QCOMPARE(QImage(path), gradient().copy(20, 10, 40, 40));
        QVERIFY(!model().active());
    }

    void escAndRightClickCaptureNothing() {
        QVERIFY(press());
        model().keyPressed(Qt::Key_Escape);
        QCOMPARE(stack().count(), 0);
        QVERIFY(!model().active());

        QVERIFY(press());
        model().pointerPressed(5, 5, Qt::RightButton);
        QCOMPARE(stack().count(), 0);
        QVERIFY(TestEnv::filesIn(env->screenshots()).isEmpty());
    }

    void aFailedGrabNotifiesAndNeverOpens() {
        env->setMode("grim", "fail");
        QVERIFY(!press());
        QVERIFY(!model().active());
        QCOMPARE(env->calls("notify-send").size(), 1);
    }

    void theNextGrabWaitsUntilAClosingOverlayIsOffScreen() {
        auto &overlay = env->daemon().service<Overlay>();
        overlay.attachSurface(); // as if a surface were showing it
        QVERIFY(press());
        model().cancel();
        QVERIFY(overlay.isClosing());

        int grabsWhenOffScreen = -1;
        connect(&overlay, &Overlay::offScreen, this, [&] { grabsWhenOffScreen = env->calls("grim").size(); });
        QTimer::singleShot(100, &overlay, [&] { overlay.surfaceGone(); });
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        QCOMPARE(grabsWhenOffScreen, 1); // the full-screen grab came after, not before
        QCOMPARE(env->calls("grim").size(), 2);
    }

    // The surface, off-screen.
    void theSurfaceIsWarmedUpInvisiblyAndOpensWithTheQmlLoaded() {
        auto &overlay = env->daemon().service<Overlay>();
        tarn::OverlaySurface surface(overlay, env->daemon().service<tarn::Palette>());
        QCOMPARE(surface.view().status(), QQuickView::Ready);
        surface.warmUp();
        QTRY_VERIFY(surface.warmedUp());
        QTRY_VERIFY(!surface.visible()); // mapped once, then gone
        QVERIFY(env->calls("grim").isEmpty());
        QVERIFY(!model().active());

        QVERIFY(press());
        QVERIFY(surface.visible());
        QCOMPARE(surface.view().size(), QSize(64, 40));
    }

    void draggingOnTheSurfaceCapturesAndHidesIt() {
        auto &overlay = env->daemon().service<Overlay>();
        tarn::OverlaySurface surface(overlay, env->daemon().service<tarn::Palette>());
        QVERIFY(press());
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        auto *root = surface.view().rootObject();
        QVERIFY(!findItem(root, "sizePill")->isVisible());

        QTest::mousePress(&surface.view(), Qt::LeftButton, {}, QPoint(10, 5));
        QTest::mouseMove(&surface.view(), QPoint(30, 25));
        QTRY_VERIFY(findItem(root, "sizePill")->isVisible());
        QTRY_COMPARE(findItem(root, "sizeLabel")->property("text").toString(), QString::fromUtf8("40 × 40"));
        QTest::mouseRelease(&surface.view(), Qt::LeftButton, {}, QPoint(30, 25));

        QCOMPARE(stack().count(), 1);
        QVERIFY(!surface.visible());
        QTRY_VERIFY(!overlay.isClosing());
    }

    void escOnTheSurfaceCancels() {
        auto &overlay = env->daemon().service<Overlay>();
        tarn::OverlaySurface surface(overlay, env->daemon().service<tarn::Palette>());
        QVERIFY(press());
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        QTest::keyClick(&surface.view(), Qt::Key_Escape);
        QVERIFY(!surface.visible());
        QCOMPARE(stack().count(), 0);
    }
};

QTEST_MAIN(OverlayTest)
#include "overlay_test.moc"
