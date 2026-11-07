// Recording on screen (off-screen): the Record panel over a kept Selection, the red
// outline and Recording bar of a running Recording, and a Recording's chip on its
// Thumbnail. Layer-shell roles need a Wayland session, so what is checked here is
// what can be seen in an ordinary window; look and feel at real speed is needs-eyes.
#include "test_env.h"

#include "overlay/overlay.h"
#include "overlay_surface.h"
#include "palette/palette.h"
#include "recording/recording_session.h"
#include "recording_surface.h"
#include "thumbnail_surface.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QImage>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTest>

using tarn::OverlayModel;
using tarn::RecordingSession;
using tarn::test::TestEnv;

namespace {
// How long a wait on the visible result may take. The recorder, ffmpeg and gifski are separate
// processes, and a loaded machine can stretch a wait well past QTRY's 5 s default (18 s seen
// with the CPUs busy). A passing wait returns the moment it is true; a long limit only costs
// time when the result never comes.
constexpr int kTryMs = 60000;
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

// A 256 x 160 logical monitor (512 x 320 device px at scale 2): room for a panel and a bar.
void bigMonitor(TestEnv &env, int logicalW = 256, int logicalH = 160) {
    env.setFixture("hyprctl", "monitors.json",
                   QStringLiteral(R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":%1,"height":%2,"scale":2.0,"transform":0}])")
                       .arg(logicalW * 2).arg(logicalH * 2).toUtf8());
    QImage img(logicalW * 2, logicalH * 2, QImage::Format_RGB32);
    img.fill(Qt::gray);
    QByteArray png;
    QBuffer b(&png);
    b.open(QIODevice::WriteOnly);
    img.save(&b, "PNG");
    env.setFixture("grim", "output.png", png);
    env.setPointer(10, 10);
}
} // namespace

class RecordingUiTest : public QObject {
    Q_OBJECT
private slots:
    // The Record panel: inside the kept Selection at its bottom center.
    void theRecordPanelSitsInsideTheSelectionAtItsBottomCenter() {
        TestEnv env;
        bigMonitor(env, 400, 300); // the panel is 232 x 145: it needs a Selection of 264 x 177 or more
        auto &overlay = env.daemon().service<tarn::Overlay>();
        tarn::OverlaySurface surface(overlay, env.daemon().service<tarn::Palette>());
        QVERIFY(env.request({"capture", "record"}).ok);
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        auto *root = surface.view().rootObject();
        QVERIFY(!findItem(root, "recordPanel")->isVisible()); // nothing kept yet

        // Below the Mode bar's top spot, so the bar stays at the top (stepping clear of
        // it is overlay_modes' theRecordPanelStaysClearOfTheModeBarNearTheBottomEdge).
        QTest::mousePress(&surface.view(), Qt::LeftButton, {}, QPoint(20, 100));
        QTest::mouseMove(&surface.view(), QPoint(320, 290));
        QTest::mouseRelease(&surface.view(), Qt::LeftButton, {}, QPoint(320, 290)); // no Ctrl needed
        QVERIFY(overlay.model().kept());
        auto *panel = findItem(root, "recordPanel");
        QTRY_VERIFY_WITH_TIMEOUT(panel->isVisible(), kTryMs);
        const QRectF sel(20, 100, 300, 190);
        QVERIFY(!overlay.model().modeBarAtBottom());
        QVERIFY(sel.contains(QRectF(panel->x(), panel->y(), panel->width(), panel->height())));
        QCOMPARE(panel->x() + panel->width() / 2, sel.center().x());
        QCOMPARE(panel->y() + panel->height(), sel.bottom() - 16);
        QVERIFY(!findItem(root, "keptPills")->isVisible()); // Record Video replaces Cancel / Capture
        QVERIFY(findItem(root, "handle0")->isVisible());    // the Handles still work
    }

    void aSmallAreaGetsThePanelJustOutside() {
        TestEnv env;
        bigMonitor(env, 400, 300);
        auto &overlay = env.daemon().service<tarn::Overlay>();
        tarn::OverlaySurface surface(overlay, env.daemon().service<tarn::Palette>());
        QVERIFY(env.request({"capture", "record"}).ok);
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        QTest::mousePress(&surface.view(), Qt::LeftButton, {}, QPoint(20, 10));
        QTest::mouseMove(&surface.view(), QPoint(60, 30));
        QTest::mouseRelease(&surface.view(), Qt::LeftButton, {}, QPoint(60, 30));
        auto *panel = findItem(surface.view().rootObject(), "recordPanel");
        QTRY_VERIFY_WITH_TIMEOUT(panel->isVisible(), kTryMs);
        QVERIFY(panel->y() > 30); // under the Selection, not over it
    }

    void clickingRecordVideoStartsTheRecordingAndEscNeverStopsIt() {
        TestEnv env;
        bigMonitor(env, 1000, 600); // roomy: the Mode bar (418 px) must not sit over the panel
        auto &overlay = env.daemon().service<tarn::Overlay>();
        auto &session = env.daemon().service<RecordingSession>();
        tarn::OverlaySurface surface(overlay, env.daemon().service<tarn::Palette>());
        QVERIFY(env.request({"capture", "record"}).ok);
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        QTest::mousePress(&surface.view(), Qt::LeftButton, {}, QPoint(20, 10));
        QTest::mouseMove(&surface.view(), QPoint(220, 120));
        QTest::mouseRelease(&surface.view(), Qt::LeftButton, {}, QPoint(220, 120));
        auto *button = findItem(surface.view().rootObject(), "recordVideoButton");
        QTRY_VERIFY_WITH_TIMEOUT(button->isVisible(), kTryMs);
        QTest::mouseClick(&surface.view(), Qt::LeftButton, {}, centerOf(button));
        QVERIFY(!overlay.model().active());
        QCOMPARE(session.state(), RecordingSession::Starting);
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), RecordingSession::Recording, kTryMs);
        QTest::keyClick(&surface.view(), Qt::Key_Escape);
        QTest::qWait(200);
        QCOMPARE(session.state(), RecordingSession::Recording);
    }

    // The outline is red at once; the bar waits for the file; both stay outside the area.
    void theOutlineIsRedAtOnceAndTheBarComesWithTheFile() {
        TestEnv env;
        bigMonitor(env);
        env.setFixture("gpu-screen-recorder", "start_delay", "0.8");
        auto &session = env.daemon().service<RecordingSession>();
        tarn::RecordingSurface surface(session, env.daemon().service<tarn::Palette>());
        QVERIFY(!surface.visible());
        QVERIFY(session.start(QRect(20, 10, 100, 70), QRect(0, 0, 256, 160), 2.0, "DP-1").ok);
        QVERIFY(surface.visible());
        QVERIFY(surface.view().flags() & Qt::WindowDoesNotAcceptFocus); // never takes focus
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        auto *root = surface.view().rootObject();
        auto *outline = findItem(root, "recordingOutline");
        auto *bar = findItem(root, "recordingBar");
        QVERIFY(outline->isVisible());
        QVERIFY(!bar->isVisible());
        QCOMPARE(outline->x(), 18.0); // 2 px outside the area
        QCOMPARE(outline->y(), 8.0);
        QCOMPARE(outline->width(), 104.0);
        QCOMPARE(outline->height(), 74.0);
        // Red where the outline is, and the area itself is left clear.
        const QImage frame = surface.view().grabWindow();
        QTRY_VERIFY_WITH_TIMEOUT(!frame.isNull(), kTryMs);
        const QColor line = surface.view().grabWindow().pixelColor(19, 45);
        QVERIFY2(line.red() > 230 && line.green() < 100 && line.blue() < 90, qPrintable(line.name()));
        QCOMPARE(surface.view().grabWindow().pixelColor(60, 45).alpha(), 0);

        QTRY_COMPARE_WITH_TIMEOUT(session.state(), RecordingSession::Recording, kTryMs);
        QTRY_VERIFY_WITH_TIMEOUT(bar->isVisible(), kTryMs);
        const QRectF barRect(bar->x(), bar->y(), bar->width(), bar->height());
        QVERIFY(!barRect.intersects(QRectF(18, 8, 104, 74)));
        QVERIFY(bar->y() > 82); // below
        QCOMPARE(QRect(int(bar->x()), int(bar->y()), int(bar->width()), int(bar->height())), session.barRect());
        QVERIFY(findItem(root, "elapsedLabel")->property("text").toString().startsWith("0:"));

        // The red stop on the bar stops it.
        auto *stop = findItem(root, "stopButton");
        QTRY_VERIFY_WITH_TIMEOUT(bar->property("armed").toBool(), kTryMs); // the bar ignores clicks for 0.5 s
        QTest::mouseClick(&surface.view(), Qt::LeftButton, {}, centerOf(stop));
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), RecordingSession::Done, kTryMs);
        QTRY_VERIFY_WITH_TIMEOUT(!surface.visible(), kTryMs);
    }

    // After a GIF stop the bar becomes a progress ring with "GIF 42%", then leaves.
    void theBarShowsGifProgressWhileEncodingThenLeaves() {
        TestEnv env;
        bigMonitor(env);
        env.setFixture("gifski", "frames", "40");
        env.setFixture("gifski", "frame_delay", "0.05"); // ~2 s of progress
        auto &session = env.daemon().service<RecordingSession>();
        tarn::RecordingSurface surface(session, env.daemon().service<tarn::Palette>());
        QVERIFY(session.start(QRect(20, 10, 100, 70), QRect(0, 0, 256, 160), 2.0, "DP-1", RecordingSession::Gif).ok);
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), RecordingSession::Recording, kTryMs);
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        auto *root = surface.view().rootObject();
        auto *bar = findItem(root, "recordingBar");
        auto *ring = findItem(root, "gifProgress");
        QTRY_VERIFY_WITH_TIMEOUT(bar->isVisible(), kTryMs);
        QVERIFY(!ring->isVisible());
        const qreal recordingWidth = bar->width();

        session.stop();
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), RecordingSession::Encoding, kTryMs);
        QTRY_VERIFY_WITH_TIMEOUT(ring->isVisible(), kTryMs);
        QVERIFY(bar->isVisible()); // the same bar, narrower
        QVERIFY(!findItem(root, "stopButton")->isVisible() || !findItem(root, "stopButton")->parentItem()->isVisible());
        auto *label = findItem(root, "gifLabel");
        QTRY_VERIFY2(QRegularExpression("^GIF \\d+%$").match(label->property("text").toString()).hasMatch(),
                     qPrintable(label->property("text").toString()));
        QTRY_VERIFY_WITH_TIMEOUT(label->property("text").toString() != "GIF 0%", kTryMs);
        // It springs to its width (the target is narrower than the recording bar).
        QTRY_VERIFY2(bar->width() < recordingWidth, qPrintable(QString::number(bar->width())));

        QTRY_COMPARE_WITH_TIMEOUT(session.state(), RecordingSession::Done, kTryMs);
        QTRY_VERIFY_WITH_TIMEOUT(!surface.visible(), kTryMs);
    }

    void aFullScreenBarIsInsideAndHidesAfterItsFirstSeconds() {
        TestEnv env;
        bigMonitor(env);
        auto &session = env.daemon().service<RecordingSession>();
        tarn::RecordingSurface surface(session, env.daemon().service<tarn::Palette>());
        QVERIFY(session.start(QRect(0, 0, 256, 160), QRect(0, 0, 256, 160), 2.0, "DP-1").ok);
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), RecordingSession::Recording, kTryMs);
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        auto *root = surface.view().rootObject();
        QVERIFY(findItem(root, "recordingOutline")->isVisible());
        QVERIFY(findItem(root, "recordingBar")->isVisible()); // inside now; it auto-hides after 3 s
        env.setNow(env.daemon().now().addMSecs(3000));
        session.tick();
        QTRY_VERIFY(!findItem(root, "recordingBar")->isVisible());
        session.stop(); // Ctrl+Print still stops it
        QTRY_COMPARE_WITH_TIMEOUT(session.state(), RecordingSession::Done, kTryMs);
    }

    void aRecordingThumbnailShowsItsChip() {
        TestEnv env;
        QImage poster(64, 40, QImage::Format_RGB32);
        poster.fill(Qt::darkCyan);
        QByteArray png;
        QBuffer b(&png);
        b.open(QIODevice::WriteOnly);
        poster.save(&b, "PNG");
        auto &stack = env.daemon().service<tarn::ThumbnailStack>();
        tarn::ThumbnailSurface surface(stack, env.daemon().service<tarn::Palette>());
        stack.addRecording(png, env.daemon().now(), env.keptRecordings() + "/a.mp4", 6000);
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        auto *root = surface.view().rootObject();
        auto *card = findItem(root, "card");
        QTRY_VERIFY_WITH_TIMEOUT(card, kTryMs);
        QTRY_COMPARE_WITH_TIMEOUT(card->x(), 16.0, kTryMs); // done springing in
        auto *chip = findItem(card, "recordingChip");
        QVERIFY(!chip->isVisible()); // it is part of the hover controls
        QTest::mouseMove(&surface.view(), card->mapToScene(QPointF(card->width() - 20, card->height() - 20)).toPoint());
        QTRY_VERIFY_WITH_TIMEOUT(chip->isVisible(), kTryMs);
        QCOMPARE(findItem(chip, "chipText")->property("text").toString(), QString("MP4 0:06"));
    }
};

QTEST_MAIN(RecordingUiTest)
#include "recording_ui_test.moc"
