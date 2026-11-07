// Ctrl-let go keeps the Selection; it is moved, resized by Handles and
// keys, then taken (Enter, double-click, Capture) or cleared (Esc).
#include "test_env.h"

#include "overlay/overlay.h"
#include "overlay/overlay_model.h"
#include "overlay_surface.h"
#include "palette/palette.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QImage>
#include <QQuickItem>
#include <QSignalSpy>
#include <QTest>

using tarn::OverlayModel;

namespace {
constexpr int Ctrl = Qt::ControlModifier;
constexpr int Alt = Qt::AltModifier;

QQuickItem *findItem(QQuickItem *from, const QString &name) {
    if (from->objectName() == name)
        return from;
    for (QQuickItem *c : from->childItems())
        if (QQuickItem *hit = findItem(c, name))
            return hit;
    return nullptr;
}
} // namespace

class OverlayKeptTest : public QObject {
    Q_OBJECT
    // A kept Selection (10,5) 20 x 20 on a 64 x 40 monitor.
    static void keep(OverlayModel &m) {
        m.open(OverlayModel::Screenshot, QSize(64, 40));
        m.pointerPressed(10, 5, Qt::LeftButton);
        m.pointerMoved(30, 25);
        m.pointerReleased(30, 25, Qt::LeftButton, Ctrl);
    }

private slots:
    void aPlainReleaseStillCapturesAtOnceAndCtrlKeeps() {
        OverlayModel m;
        QSignalSpy confirmed(&m, &OverlayModel::confirmed);
        m.open(OverlayModel::Screenshot, QSize(64, 40));
        m.pointerPressed(10, 5, Qt::LeftButton);
        m.pointerReleased(30, 25, Qt::LeftButton);
        QCOMPARE(confirmed.count(), 1);

        keep(m);
        QCOMPARE(confirmed.count(), 1);
        QVERIFY(m.active());
        QVERIFY(m.kept());
        QCOMPARE(m.selection(), QRect(10, 5, 20, 20));
    }

    void draggingInsideMovesAndStaysOnScreen() {
        OverlayModel m;
        keep(m);
        m.pointerPressed(20, 15, Qt::LeftButton);
        m.pointerMoved(25, 18);
        QCOMPARE(m.selection(), QRect(15, 8, 20, 20));
        m.pointerMoved(500, 500);
        QCOMPARE(m.selection(), QRect(44, 20, 20, 20)); // stopped at the monitor's edge
        m.pointerReleased(500, 500, Qt::LeftButton);
        QVERIFY(m.kept());
        QVERIFY(m.active());
    }

    void pressingOutsideDoesNothing() {
        OverlayModel m;
        keep(m);
        m.pointerPressed(50, 35, Qt::LeftButton);
        m.pointerMoved(55, 38);
        m.pointerReleased(55, 38, Qt::LeftButton);
        QCOMPARE(m.selection(), QRect(10, 5, 20, 20));
    }

    void handlesResizeFromEveryCornerAndEdge() {
        OverlayModel m;
        keep(m); // edges: left 10, top 5, right 30, bottom 25
        QCOMPARE(m.handleAt(10, 5), 0);
        QCOMPARE(m.handleAt(30, 25), 2);
        QCOMPARE(m.handleAt(20, 5), 4);
        QCOMPARE(m.handleAt(10, 15), 7);
        QCOMPARE(m.handleAt(20, 15), -1);

        m.pointerPressed(30, 25, Qt::LeftButton); // bottom-right
        m.pointerMoved(40, 30);
        QCOMPARE(m.selection(), QRect(10, 5, 30, 25));
        m.pointerReleased(40, 30, Qt::LeftButton);

        m.pointerPressed(10, 5, Qt::LeftButton); // top-left
        m.pointerMoved(12, 7);
        QCOMPARE(m.selection(), QRect(12, 7, 28, 23));
        m.pointerReleased(12, 7, Qt::LeftButton);

        m.pointerPressed(26, 7, Qt::LeftButton); // top edge
        m.pointerMoved(26, 10);
        QCOMPARE(m.selection(), QRect(12, 10, 28, 20));
        m.pointerReleased(26, 10, Qt::LeftButton);

        m.pointerPressed(40, 20, Qt::LeftButton); // right edge
        m.pointerMoved(45, 20);
        QCOMPARE(m.selection(), QRect(12, 10, 33, 20));
        m.pointerReleased(45, 20, Qt::LeftButton);
        QVERIFY(m.kept());
    }

    void draggingAHandlePastTheOppositeEdgeFlipsTheSelection() {
        OverlayModel m;
        keep(m);
        m.pointerPressed(30, 15, Qt::LeftButton); // right edge
        m.pointerMoved(5, 15);
        QCOMPARE(m.selection(), QRect(5, 5, 5, 20));
    }

    void arrowsMoveAndAltArrowsResize() {
        OverlayModel m;
        keep(m);
        m.keyPressed(Qt::Key_Right);
        m.keyPressed(Qt::Key_Down);
        QCOMPARE(m.selection(), QRect(11, 6, 20, 20));
        m.keyPressed(Qt::Key_Left);
        m.keyPressed(Qt::Key_Up);
        m.keyPressed(Qt::Key_Up);
        QCOMPARE(m.selection(), QRect(10, 4, 20, 20));
        m.keyPressed(Qt::Key_Right, Alt);
        m.keyPressed(Qt::Key_Down, Alt);
        QCOMPARE(m.selection(), QRect(10, 4, 21, 21));
        m.keyPressed(Qt::Key_Left, Alt);
        m.keyPressed(Qt::Key_Up, Alt);
        m.keyPressed(Qt::Key_Up, Alt);
        QCOMPARE(m.selection(), QRect(10, 4, 20, 19));
        QVERIFY(m.kept());
    }

    void keysStopAtTheMonitorEdgeAndASizeOfOne() {
        OverlayModel m;
        m.open(OverlayModel::Screenshot, QSize(64, 40));
        m.pointerPressed(0, 0, Qt::LeftButton);
        m.pointerReleased(2, 2, Qt::LeftButton, Ctrl);
        m.keyPressed(Qt::Key_Left);
        m.keyPressed(Qt::Key_Up);
        QCOMPARE(m.selection(), QRect(0, 0, 2, 2));
        m.keyPressed(Qt::Key_Left, Alt);
        m.keyPressed(Qt::Key_Left, Alt);
        QCOMPARE(m.selection().width(), 1);
    }

    void enterTakesTheKeptSelectionWhereItNowIs() {
        OverlayModel m;
        QSignalSpy confirmed(&m, &OverlayModel::confirmed);
        keep(m);
        m.keyPressed(Qt::Key_Right);
        m.keyPressed(Qt::Key_Return);
        QCOMPARE(confirmed.count(), 1);
        QCOMPARE(confirmed.first().at(0).toRect(), QRect(11, 5, 20, 20));
        QVERIFY(!m.active());
        QVERIFY(!m.kept());
    }

    void doubleClickInsideTakesItButOutsideDoesNot() {
        OverlayModel m;
        QSignalSpy confirmed(&m, &OverlayModel::confirmed);
        keep(m);
        m.pointerDoubleClicked(55, 35);
        QCOMPARE(confirmed.count(), 0);
        m.pointerDoubleClicked(20, 15);
        QCOMPARE(confirmed.count(), 1);
    }

    void confirmIsWhatTheCapturePillCalls() {
        OverlayModel m;
        QSignalSpy confirmed(&m, &OverlayModel::confirmed);
        keep(m);
        m.confirm();
        QCOMPARE(confirmed.count(), 1);
    }

    void escClearsTheSelectionAndAnotherEscCancels() {
        OverlayModel m;
        QSignalSpy confirmed(&m, &OverlayModel::confirmed);
        QSignalSpy canceled(&m, &OverlayModel::canceled);
        keep(m);
        m.keyPressed(Qt::Key_Escape);
        QVERIFY(m.active());
        QVERIFY(!m.kept());
        QVERIFY(!m.hasSelection());
        QCOMPARE(canceled.count(), 0);
        m.pointerPressed(5, 5, Qt::LeftButton); // picking works again
        m.pointerReleased(15, 15, Qt::LeftButton);
        QCOMPARE(confirmed.count(), 1);

        keep(m);
        m.keyPressed(Qt::Key_Escape);
        m.keyPressed(Qt::Key_Escape);
        QCOMPARE(canceled.count(), 1);
    }

    void aKeptSelectionKeepsItsModeSoRecordCanSitOnIt() {
        OverlayModel m;
        QSignalSpy confirmed(&m, &OverlayModel::confirmed);
        m.open(OverlayModel::Record, QSize(64, 40));
        m.pointerPressed(10, 5, Qt::LeftButton);
        m.pointerReleased(30, 25, Qt::LeftButton, Ctrl);
        QVERIFY(m.kept());
        m.confirm();
        QCOMPARE(confirmed.first().at(1).value<OverlayModel::Mode>(), OverlayModel::Record);
    }

    void thePositionShowsInDevicePixels() {
        OverlayModel m;
        m.open(OverlayModel::Screenshot, QSize(64, 40), 2.0);
        m.pointerPressed(10, 5, Qt::LeftButton);
        m.pointerReleased(30, 25, Qt::LeftButton, Ctrl);
        QCOMPARE(m.positionLabel(), QString("20, 10"));
    }

    // The surface: Handles and the dark pills appear, and Capture works.
    void theSurfaceShowsHandlesAndPillsAndCapturePillTakesIt() {
        tarn::test::TestEnv env;
        env.setFixture("hyprctl", "monitors.json",
                       R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":512,"height":320,"scale":2.0,"transform":0}])");
        QImage img(512, 320, QImage::Format_RGB32);
        img.fill(Qt::gray);
        QByteArray png;
        QBuffer b(&png);
        b.open(QIODevice::WriteOnly);
        img.save(&b, "PNG");
        env.setFixture("grim", "output.png", png);
        env.setPointer(10, 10);
        auto &overlay = env.daemon().service<tarn::Overlay>();
        tarn::OverlaySurface surface(overlay, env.daemon().service<tarn::Palette>());
        QVERIFY(env.request({"capture", "area"}).ok);
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        auto *root = surface.view().rootObject();
        QVERIFY(!findItem(root, "handle0"));

        QTest::mousePress(&surface.view(), Qt::LeftButton, {}, QPoint(10, 5));
        QTest::mouseMove(&surface.view(), QPoint(30, 25));
        QTest::mouseRelease(&surface.view(), Qt::LeftButton, Qt::ControlModifier, QPoint(30, 25));
        QVERIFY(overlay.model().kept());
        QTRY_VERIFY(findItem(root, "handle7") && findItem(root, "handle7")->isVisible());
        for (int h = 0; h < 8; ++h)
            QCOMPARE(findItem(root, "handle" + QString::number(h))->width(), 14.0);
        auto *outline = findItem(root, "outline");
        QCOMPARE(outline->property("border").value<QObject *>()->property("width").toInt(), 3);
        QVERIFY(qAbs(outline->property("border").value<QObject *>()->property("color").value<QColor>().alphaF() - 0.5) < 0.01);
        QTRY_VERIFY(findItem(root, "capturePill")->isVisible());
        QVERIFY(findItem(root, "cancelPill")->isVisible());
        QVERIFY(findItem(root, "sizeLabel")->property("text").toString().startsWith("20, 10"));

        auto *pill = findItem(root, "capturePill");
        QTest::mouseClick(&surface.view(), Qt::LeftButton, {}, pill->mapToScene(QPointF(pill->width() / 2, pill->height() / 2)).toPoint());
        QVERIFY(!overlay.model().active());
        QCOMPARE(env.daemon().service<tarn::ThumbnailStack>().count(), 1);
    }
};

QTEST_MAIN(OverlayKeptTest)
#include "overlay_kept_test.moc"
