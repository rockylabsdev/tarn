// The Recording bar's controls, below QML. Pause, resume, restart and delete
// change the Recording session's state and send the recorder signals; the stub
// gpu-screen-recorder logs the ones it gets (INT is a stop, USR2 toggles pause).
#include "test_env.h"

#include "recording/recording_session.h"
#include "thumbnails/thumbnail_stack.h"

#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>

using tarn::RecordingSession;
using tarn::test::TestEnv;

namespace {
const QByteArray kMonitor = R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":512,"height":320,"scale":2.0,"transform":0}])";
}

class RecordingBarTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    RecordingSession &session() { return env->daemon().service<RecordingSession>(); }
    tarn::ThumbnailStack &stack() { return env->daemon().service<tarn::ThumbnailStack>(); }
    QString signalLog() const { return env->scratch() + "/stubs/signals/gpu-screen-recorder.log"; }
    QStringList signalsSent() const {
        return QString::fromUtf8(TestEnv::readFile(signalLog())).split('\n', Qt::SkipEmptyParts);
    }
    // 80 x 50 logical px at (20, 10) on a 256 x 160 logical monitor: a bar fits below.
    void startRecording() {
        QVERIFY(session().start(QRect(20, 10, 80, 50), QRect(0, 0, 256, 160), 2.0, "DP-1").ok);
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, 5000);
    }

    // The whole 256 x 160 monitor, so the bar sits inside and auto-hides.
    void startFullScreen() {
        QVERIFY(session().start(QRect(0, 0, 256, 160), QRect(0, 0, 256, 160), 2.0, "DP-1").ok);
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, 5000);
    }
    void after(int ms) {
        env->setNow(env->daemon().now().addMSecs(ms));
        session().tick();
    }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        env->setFixture("hyprctl", "monitors.json", kMonitor);
    }
    void cleanup() { env.reset(); }

    void pauseAndResumeToggleTheRecorderAndKeepTheBar() {
        startRecording();
        QVERIFY(session().canPause());
        session().pause();
        QCOMPARE(session().state(), RecordingSession::Paused);
        QVERIFY(session().barVisible()); // the bar stays while paused
        QTRY_COMPARE(signalsSent(), (QStringList{"USR2"}));
        session().resume();
        QCOMPARE(session().state(), RecordingSession::Recording);
        QTRY_COMPARE(signalsSent(), (QStringList{"USR2", "USR2"}));
    }

    void pausedTimeDoesNotCount() {
        startRecording();
        QTest::qWait(300);
        session().pause();
        const qint64 atPause = session().elapsedMs();
        QVERIFY(atPause >= 250);
        QTest::qWait(500);
        QCOMPARE(session().elapsedMs(), atPause); // frozen
        session().resume();
        QTest::qWait(200);
        const qint64 after = session().elapsedMs();
        QVERIFY2(after >= atPause + 150 && after < atPause + 450, qPrintable(QString::number(after - atPause)));
    }

    void pauseIsDisabledUntilTheRecorderIsWriting() {
        env->setMode("gpu-screen-recorder", "silent"); // never writes the file
        QVERIFY(session().start(QRect(20, 10, 80, 50), QRect(0, 0, 256, 160), 2.0, "DP-1").ok);
        QTest::qWait(150);
        QCOMPARE(session().state(), RecordingSession::Starting);
        QVERIFY(!session().canPause());
        session().pause(); // would kill the real recorder
        QTest::qWait(150);
        QCOMPARE(session().state(), RecordingSession::Starting);
        QVERIFY(!QFileInfo::exists(signalLog()) || signalsSent().isEmpty());
    }

    void resumeDoesNothingUnlessPaused() {
        startRecording();
        session().resume();
        QTest::qWait(150);
        QCOMPARE(session().state(), RecordingSession::Recording);
        QVERIFY(!QFileInfo::exists(signalLog()) || signalsSent().isEmpty());
    }

    void stoppingWhilePausedStillFinishesTheRecording() {
        startRecording();
        session().pause();
        session().stop();
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, 10000);
        QCOMPARE(stack().count(), 1);
    }

    void theBarVanishesAtOnceOnAStop() {
        startRecording();
        QSignalSpy gone(&session(), &RecordingSession::barVisibleChanged);
        QVERIFY(session().barVisible());
        session().stop();
        QVERIFY(!session().barVisible()); // before the file is finished
        QCOMPARE(gone.count(), 1);
        QVERIFY(!session().canPause());
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, 10000);
    }

    void restartThrowsTheRecordingAwayAndStartsAgainFromZeroThroughStarting() {
        startRecording();
        const QString first = session().outputPath();
        QTest::qWait(400);
        QVERIFY(session().elapsedMs() >= 350);
        QSignalSpy states(&session(), &RecordingSession::stateChanged);
        env->setFixture("gpu-screen-recorder", "start_delay", "0.5");
        session().restart();
        QCOMPARE(session().state(), RecordingSession::Starting);
        QVERIFY(!session().barVisible());
        QCOMPARE(session().elapsedMs(), qint64(0));
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, 5000);
        QVERIFY(session().elapsedMs() < 300);
        QList<RecordingSession::State> seen;
        for (const auto &s : states)
            seen << s.at(0).value<RecordingSession::State>();
        QCOMPARE(seen, (QList<RecordingSession::State>{RecordingSession::Starting, RecordingSession::Recording}));
        // The old recorder was stopped, a new one started with the same region; one file kept.
        QTRY_COMPARE(env->calls("gpu-screen-recorder").size(), 2);
        const auto calls = env->calls("gpu-screen-recorder");
        QCOMPARE(calls[0].mid(0, 4), calls[1].mid(0, 4));
        QTRY_VERIFY(signalsSent().contains("INT"));
        QCOMPARE(TestEnv::filesIn(env->keptRecordings()).size(), 1);
        QCOMPARE(stack().count(), 0);
        Q_UNUSED(first);
        // And it stops into a Thumbnail as usual.
        session().stop();
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, 10000);
        QCOMPARE(stack().count(), 1);
    }

    void restartWorksFromPausedAndRunsUnpaused() {
        startRecording();
        session().pause();
        session().restart();
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, 5000);
        QVERIFY(session().canPause());
        QCOMPARE(env->calls("gpu-screen-recorder").size(), 2);
    }

    void deleteLeavesNoThumbnailNoFileAndNoQuestion() {
        startRecording();
        QSignalSpy recorded(&session(), &RecordingSession::recorded);
        session().deleteRecording();
        QCOMPARE(session().state(), RecordingSession::Deleted);
        QVERIFY(!session().barVisible());
        QVERIFY(!session().active());
        QTRY_VERIFY(signalsSent().contains("INT"));
        QVERIFY(TestEnv::filesIn(env->keptRecordings()).isEmpty());
        QVERIFY(TestEnv::filesIn(env->recordings()).isEmpty());
        QCOMPARE(stack().count(), 0);
        QCOMPARE(recorded.count(), 0);
        QCOMPARE(env->calls("notify-send").size(), 0);
        // Another Recording can start afterwards.
        startRecording();
    }

    void deleteWorksWhilePaused() {
        startRecording();
        session().pause();
        session().deleteRecording();
        QCOMPARE(session().state(), RecordingSession::Deleted);
        QVERIFY(TestEnv::filesIn(env->keptRecordings()).isEmpty());
    }

    void deleteAndRestartDoNothingBeforeTheRecorderIsWriting() {
        env->setMode("gpu-screen-recorder", "silent");
        QVERIFY(session().start(QRect(20, 10, 80, 50), QRect(0, 0, 256, 160), 2.0, "DP-1").ok);
        session().deleteRecording();
        session().restart();
        QTest::qWait(150);
        QCOMPARE(session().state(), RecordingSession::Starting);
        QTRY_COMPARE(env->calls("gpu-screen-recorder").size(), 1);
    }

    void theGripMovesTheBarForThisRecordingOnly() {
        startRecording();
        session().setBarSize(120, 44);
        const QRect placed = session().barRect();
        QVERIFY(session().hasBar());
        session().moveBarTo(150, 100);
        QCOMPARE(session().barRect(), QRect(150, 100, 120, 44).translated(-qMax(0, 150 + 120 - 256), 0));
        session().moveBarTo(-50, 500); // kept on the monitor
        QCOMPARE(session().barRect(), QRect(0, 160 - 44, 120, 44));
        session().stop();
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, 10000);
        // A new Recording places it by the rule again.
        startRecording();
        QCOMPARE(session().barRect(), placed);
    }

    // A GIF Recording keeps its format across a restart,
    // and delete is ignored while the GIF is encoding.
    void restartKeepsTheGifFormatAndTheGifStillComesOut() {
        QVERIFY(session().start(QRect(20, 10, 80, 50), QRect(0, 0, 256, 160), 2.0, "DP-1", RecordingSession::Gif).ok);
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, 5000);
        session().restart();
        QCOMPARE(session().format(), RecordingSession::Gif);
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, 5000);
        QVERIFY(session().gif());
        session().stop();
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, 15000);
        QCOMPARE(session().format(), RecordingSession::Gif);
        QCOMPARE(stack().count(), 1);
        QCOMPARE(env->calls("gifski").size(), 1);
    }

    void deleteAndRestartAreIgnoredWhileTheGifEncodes() {
        env->setFixture("gifski", "frames", "40");
        env->setFixture("gifski", "frame_delay", "0.05");
        QVERIFY(session().start(QRect(20, 10, 80, 50), QRect(0, 0, 256, 160), 2.0, "DP-1", RecordingSession::Gif).ok);
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, 5000);
        session().stop();
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Encoding, 10000);
        QVERIFY(session().barVisible());
        session().deleteRecording();
        session().restart();
        QCOMPARE(session().state(), RecordingSession::Encoding);
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, 15000);
        QCOMPARE(stack().count(), 1);
    }

    void micOnIsAPropertyTheBarReads() {
        QVERIFY(!session().micOn());
        QSignalSpy changed(&session(), &RecordingSession::micOnChanged);
        session().setMicOn(true);
        QVERIFY(session().micOn());
        QCOMPARE(changed.count(), 1);
    }

    void aFullScreenBarIsInsideShownWhenTheRecorderWritesThenHiddenAfter3s() {
        QVERIFY(session().start(QRect(0, 0, 256, 160), QRect(0, 0, 256, 160), 2.0, "DP-1").ok);
        QCOMPARE(session().state(), RecordingSession::Starting);
        QVERIFY(!session().barVisible()); // not before the recorder writes
        QVERIFY(session().autoHide());
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, 5000);
        QVERIFY(session().barVisible());
        const QRect r = session().barRect();
        QCOMPARE(r.x(), (256 - r.width()) / 2);     // bottom center
        QCOMPARE(r.bottom() + 1 + 12, 160);         // 12 px above the edge
        after(2999);
        QVERIFY(session().barVisible());
        after(1);
        QVERIFY(!session().barVisible());
        QVERIFY(session().hasBar()); // hidden, but placed
    }

    void theStripShowsTheBarAndItHidesOneSecondAfterThePointerLeavesBarAndStrip() {
        startFullScreen();
        after(3000);
        QVERIFY(!session().barVisible());
        session().setPointerOnStrip(true);
        QVERIFY(session().barVisible());
        after(10000);
        QVERIFY(session().barVisible()); // pointer still on the strip
        session().setPointerOnBar(true);  // moves up onto the bar
        session().setPointerOnStrip(false);
        after(10000);
        QVERIFY(session().barVisible());
        session().setPointerOnBar(false);
        after(999);
        QVERIFY(session().barVisible());
        after(1);
        QVERIFY(!session().barVisible());
    }

    void thePointerReturningWithinTheSecondKeepsTheBar() {
        startFullScreen();
        session().setPointerOnBar(true);
        after(5000);
        session().setPointerOnBar(false);
        after(900);
        session().setPointerOnStrip(true);
        after(5000);
        QVERIFY(session().barVisible());
    }

    void theBarStaysWhilePausedAndGetsItsThreeSecondsBackOnResume() {
        startFullScreen();
        session().pause();
        QCOMPARE(session().state(), RecordingSession::Paused);
        after(10000);
        QVERIFY(session().barVisible());
        session().resume();
        QVERIFY(session().barVisible());
        after(2999);
        QVERIFY(session().barVisible());
        after(1);
        QVERIFY(!session().barVisible());
    }

    void aBarShownByTheStripCanBePausedAndStaysShown() {
        startFullScreen();
        after(3000);
        QVERIFY(!session().barVisible());
        session().setPointerOnStrip(true);
        session().pause();
        session().setPointerOnStrip(false);
        after(10000);
        QVERIFY(session().barVisible());
    }

    void theStripExistsOnlyForAnAutoHidingBarWhileItsRecordingRuns() {
        QSignalSpy strip(&session(), &RecordingSession::stripChanged);
        QVERIFY(session().start(QRect(0, 0, 256, 160), QRect(0, 0, 256, 160), 2.0, "DP-1").ok);
        QVERIFY(session().stripRect().isNull()); // starting: nothing is recording yet
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, 5000);
        QCOMPARE(session().stripRect(), QRect(0, 158, 256, 2)); // 2 px along the bottom edge, the monitor's width
        session().pause();
        QVERIFY(!session().stripRect().isNull());
        session().resume();
        session().stop();
        QVERIFY(session().stripRect().isNull()); // stopping
        QTRY_VERIFY_WITH_TIMEOUT(session().state() == RecordingSession::Done || session().state() == RecordingSession::Idle, 8000);
        QVERIFY(session().stripRect().isNull());
        QVERIFY(strip.count() >= 2);
    }

    void theStripGoesWithARestartedOrDeletedRecording() {
        startFullScreen();
        session().restart();
        QVERIFY(session().stripRect().isNull()); // starting again
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, 5000);
        QVERIFY(!session().stripRect().isNull());
        QVERIFY(session().barVisible()); // shown again for 3 s
        session().deleteRecording();
        QVERIFY(session().stripRect().isNull());
    }

    void anAreaWithRoomOutsideHasNoStripAndNeverHides() {
        startRecording(); // 80 x 50 at (20, 10): the bar fits below
        QVERIFY(!session().autoHide());
        QVERIFY(session().stripRect().isNull());
        after(60000);
        QVERIFY(session().barVisible());
        session().setPointerOnBar(false);
        after(60000);
        QVERIFY(session().barVisible());
    }

    void aDraggedAutoHidingBarReappearsWhereItWasDragged() {
        startFullScreen();
        session().moveBarTo(10, 20);
        const QRect moved = session().barRect();
        QCOMPARE(moved.topLeft(), QPoint(10, 20));
        after(3000);
        QVERIFY(!session().barVisible());
        session().setPointerOnStrip(true);
        QVERIFY(session().barVisible());
        QCOMPARE(session().barRect(), moved);
    }
};

QTEST_MAIN(RecordingBarTest)
#include "recording_bar_test.moc"
