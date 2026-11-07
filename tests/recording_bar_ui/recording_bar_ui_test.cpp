// The Recording bar on screen (off-screen): the Recording bar's cells, its 0.5 s of ignored
// clicks, the grip, and that it never takes focus. Look and instant state
// changes at real speed are needs-eyes.
#include "test_env.h"

#include "palette/palette.h"
#include "recording/recording_session.h"
#include "recording_surface.h"

#include <QImage>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

using tarn::RecordingSession;
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
QPoint centerOf(QQuickItem *item) {
    return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
}
} // namespace

class RecordingBarUiTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;
    std::unique_ptr<tarn::RecordingSurface> surface;
    QQuickItem *root = nullptr, *bar = nullptr;

    RecordingSession &session() { return env->daemon().service<RecordingSession>(); }
    QQuickItem *item(const char *name) { return findItem(root, name); }
    void click(const char *name) { QTest::mouseClick(&surface->view(), Qt::LeftButton, {}, centerOf(item(name))); }

    // A running Recording with its bar up (clicks not yet armed).
    void startRecording() {
        env->setFixture("hyprctl", "monitors.json",
                        R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":512,"height":320,"scale":2.0,"transform":0}])");
        surface = std::make_unique<tarn::RecordingSurface>(session(), env->daemon().service<tarn::Palette>());
        QVERIFY(session().start(QRect(20, 10, 100, 70), QRect(0, 0, 256, 160), 2.0, "DP-1").ok);
        QVERIFY(QTest::qWaitForWindowExposed(&surface->view()));
        root = surface->view().rootObject();
        bar = findItem(root, "recordingBar");
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, 5000);
        QTRY_VERIFY(bar->isVisible());
    }
    void waitArmed() { QTRY_VERIFY_WITH_TIMEOUT(bar->property("armed").toBool(), 3000); }

private slots:
    void init() { env = std::make_unique<TestEnv>(); }
    void cleanup() {
        surface.reset();
        env.reset();
    }

    void theBarHasStopAndTimeThenFour40pxCells() {
        startRecording();
        for (const char *name : {"pauseButton", "restartButton", "deleteButton", "gripButton"}) {
            QVERIFY2(item(name), name);
            QCOMPARE(item(name)->width(), 40.0);
        }
        QVERIFY(item("stopButton"));
        QVERIFY(!item("micGlyph")->isVisible());
        QCOMPARE(QRect(int(bar->x()), int(bar->y()), int(bar->width()), int(bar->height())), session().barRect());
        QVERIFY(surface->view().flags() & Qt::WindowDoesNotAcceptFocus);
        QQuickItem *focused = surface->view().activeFocusItem();
        QVERIFY(!focused || (focused != bar && !bar->isAncestorOf(focused))); // nothing on the bar takes focus
    }

    void theBarIgnoresClicksForItsFirstHalfSecond() {
        startRecording();
        QVERIFY(!bar->property("armed").toBool());
        click("deleteButton");
        click("stopButton");
        QTest::qWait(100);
        QCOMPARE(session().state(), RecordingSession::Recording); // both ignored
        waitArmed();
        click("pauseButton");
        QCOMPARE(session().state(), RecordingSession::Paused);
    }

    void pauseAndResumeSwapTheButtonAtOnce() {
        startRecording();
        waitArmed();
        auto *pause = item("pauseButton");
        QCOMPARE(pause->property("icon").toString(), QString("player-pause"));
        click("pauseButton");
        QCOMPARE(session().state(), RecordingSession::Paused);
        QVERIFY(bar->isVisible());
        QCOMPARE(pause->property("icon").toString(), QString("player-play"));
        click("pauseButton");
        QCOMPARE(session().state(), RecordingSession::Recording);
        QCOMPARE(pause->property("icon").toString(), QString("player-pause"));
    }

    // The stop button stops, paused or not, so it always shows a stop shape: the
    // pause/play button beside it is what shows the pause.
    void theStopButtonShowsStopWhilePaused() {
        startRecording();
        waitArmed();
        auto *stop = item("stopDot");
        QVERIFY(stop->isVisible());
        QCOMPARE(item("pausedGlyph"), nullptr);
        click("pauseButton");
        QCOMPARE(session().state(), RecordingSession::Paused);
        QVERIFY(stop->isVisible());
        QVERIFY(stop->property("radius").toReal() < stop->property("width").toReal() / 2); // a square, not the recording dot
        click("pauseButton");
        QCOMPARE(session().state(), RecordingSession::Recording);
        QVERIFY(stop->isVisible());
    }

    void restartGoesBackThroughStartingAndTheBarWaitsForTheFile() {
        startRecording();
        waitArmed();
        click("restartButton");
        QCOMPARE(session().state(), RecordingSession::Starting);
        QVERIFY(!bar->isVisible());
        QVERIFY(item("recordingOutline")->isVisible());
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, 5000);
        QTRY_VERIFY(bar->isVisible());
        QVERIFY(!bar->property("armed").toBool()); // a fresh bar ignores clicks again
    }

    // The QML loads and binds to the auto-hide state; hiding is instant.
    void aFullScreenBarHidesAtOnceAndTheStripBringsItBack() {
        env->setFixture("hyprctl", "monitors.json",
                        R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":512,"height":320,"scale":2.0,"transform":0}])");
        surface = std::make_unique<tarn::RecordingSurface>(session(), env->daemon().service<tarn::Palette>());
        QVERIFY(session().start(QRect(0, 0, 256, 160), QRect(0, 0, 256, 160), 2.0, "DP-1").ok);
        QVERIFY(QTest::qWaitForWindowExposed(&surface->view()));
        root = surface->view().rootObject();
        bar = findItem(root, "recordingBar");
        QQuickItem *strip = findItem(root, "barStrip");
        QVERIFY(strip);
        QVERIFY(!strip->isVisible()); // not recording yet
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, 5000);
        QTRY_VERIFY(bar->isVisible());
        QVERIFY(strip->isVisible());
        QCOMPARE(QRectF(strip->x(), strip->y(), strip->width(), strip->height()), QRectF(0, 158, 256, 2));
        QCOMPARE(QRect(int(bar->x()), int(bar->y()), int(bar->width()), int(bar->height())), session().barRect());
        env->setNow(env->daemon().now().addMSecs(3000));
        session().tick();
        QTRY_VERIFY(!bar->isVisible()); // gone with no animation
        QVERIFY(strip->isVisible());
        session().setPointerOnStrip(true);
        QTRY_VERIFY(bar->isVisible());
        session().stop();
        QTRY_VERIFY(!strip->isVisible());
    }

    // The OSD motion: a full-screen bar rises in over frames, not at once, and sinks out.
    void anAutoHidingBarRisesAndSinksOnSprings() {
        env->setFixture("hyprctl", "monitors.json",
                        R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":512,"height":320,"scale":2.0,"transform":0}])");
        surface = std::make_unique<tarn::RecordingSurface>(session(), env->daemon().service<tarn::Palette>());
        root = surface->view().rootObject();
        bar = findItem(root, "recordingBar");
        QList<qreal> opacities;
        connect(bar, &QQuickItem::opacityChanged, this, [&] { opacities << bar->opacity(); });
        QVERIFY(session().start(QRect(0, 0, 256, 160), QRect(0, 0, 256, 160), 2.0, "DP-1").ok);
        QVERIFY(QTest::qWaitForWindowExposed(&surface->view()));
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(bar->opacity(), 1.0, 3000);
        QVERIFY2(opacities.size() > 3, "it must rise over several frames, not appear at once");
        QVERIFY(opacities.first() < 0.5);
        QVERIFY(std::is_sorted(opacities.begin(), opacities.end()));
        QVERIFY(bar->isVisible());

        // Hide after 3 s: it falls to nothing and is then invisible (and so takes no input).
        opacities.clear();
        env->setNow(env->daemon().now().addMSecs(3000));
        session().tick();
        QVERIFY(!session().barVisible());
        QTRY_VERIFY_WITH_TIMEOUT(!bar->isVisible(), 3000);
        QCOMPARE(bar->opacity(), 0.0);
        QVERIFY2(opacities.size() > 3, "it must sink over several frames, not vanish at once");
        QVERIFY(std::is_sorted(opacities.begin(), opacities.end(), std::greater<qreal>()));
    }

    // A reversal mid-motion (the pointer flicking at the strip) bends: no jump in the opacity.
    void aReversalMidSinkBendsInsteadOfJumping() {
        env->setFixture("hyprctl", "monitors.json",
                        R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":512,"height":320,"scale":2.0,"transform":0}])");
        surface = std::make_unique<tarn::RecordingSurface>(session(), env->daemon().service<tarn::Palette>());
        root = surface->view().rootObject();
        bar = findItem(root, "recordingBar");
        QVERIFY(session().start(QRect(0, 0, 256, 160), QRect(0, 0, 256, 160), 2.0, "DP-1").ok);
        QVERIFY(QTest::qWaitForWindowExposed(&surface->view()));
        QTRY_COMPARE_WITH_TIMEOUT(bar->opacity(), 1.0, 5000);
        env->setNow(env->daemon().now().addMSecs(3000));
        session().tick();
        QTRY_VERIFY_WITH_TIMEOUT(bar->opacity() < 0.9, 3000);
        QVERIFY(bar->opacity() > 0.0);
        const qreal before = bar->opacity();
        QList<qreal> after;
        connect(bar, &QQuickItem::opacityChanged, this, [&] { after << bar->opacity(); });
        session().setPointerOnStrip(true);
        QVERIFY(session().barVisible());
        QTRY_VERIFY_WITH_TIMEOUT(after.size() >= 1, 1000);
        QVERIFY2(qAbs(after.first() - before) < 0.3, qPrintable(QString("%1 -> %2").arg(before).arg(after.first())));
        QTRY_COMPARE_WITH_TIMEOUT(bar->opacity(), 1.0, 3000);
    }

    void deleteVanishesTheBarAndTheSurfaceAtOnce() {
        startRecording();
        waitArmed();
        click("deleteButton");
        QCOMPARE(session().state(), RecordingSession::Deleted);
        QVERIFY(!surface->visible());
        QVERIFY(TestEnv::filesIn(env->keptRecordings()).isEmpty());
    }

    void stopVanishesTheBarBeforeTheFileIsFinished() {
        startRecording();
        waitArmed();
        click("stopButton");
        QVERIFY(!bar->isVisible()); // at once, while the recorder is still closing the file
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, 10000);
    }

    void theGripDragsTheBar() {
        startRecording();
        waitArmed();
        const QRect before = session().barRect();
        const QPoint from = centerOf(item("gripButton"));
        QTest::mousePress(&surface->view(), Qt::LeftButton, {}, from);
        QTest::mouseMove(&surface->view(), from + QPoint(10, -20));
        QTest::mouseMove(&surface->view(), from + QPoint(20, -40));
        QTest::mouseRelease(&surface->view(), Qt::LeftButton, {}, from + QPoint(20, -40));
        const QRect after = session().barRect();
        QVERIFY2(after.x() > before.x() && after.y() < before.y(),
                 qPrintable(QString("%1,%2 -> %3,%4").arg(before.x()).arg(before.y()).arg(after.x()).arg(after.y())));
        QCOMPARE(int(bar->x()), after.x());
        QCOMPARE(int(bar->y()), after.y());
        QCOMPARE(session().state(), RecordingSession::Recording); // dragging is not a click on anything
    }

    void theMicGlyphShowsWhenTheMicIsOn() {
        startRecording();
        QVERIFY(!item("micGlyph")->isVisible());
        const qreal narrow = bar->width();
        session().setMicOn(true);
        QVERIFY(item("micGlyph")->isVisible());
        QTRY_VERIFY(bar->width() > narrow);
    }
};

QTEST_MAIN(RecordingBarUiTest)
#include "recording_bar_ui_test.moc"
