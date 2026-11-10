// Capture hide: the Thumbnail stack is off screen while Tarn takes a shot or
// records, so Thumbnails are never in a capture. Each test logs every change
// of the stack's captureHidden() with how many times grim (or the recorder) had
// run by then: the hide must come before the program runs, the return after.
#include "test_env.h"

#include "overlay/overlay.h"
#include "recording/recording_session.h"
#include "settings/settings.h"
#include "timer/countdown.h"
#include "thumbnails/thumbnail_stack.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using tarn::Countdown;
using tarn::Overlay;
using tarn::RecordingSession;
using tarn::OverlayModel;
using tarn::ThumbnailStack;
using tarn::test::TestEnv;

namespace {
// (hidden?, calls of the program so far) at each change of captureHidden().
using Log = QList<QPair<bool, int>>;
constexpr int kTryMs = 60000; // the recorder, ffmpeg and gifski are processes: a busy machine is slow
// One monitor, 64 x 40 logical at scale 2: grim's 128 x 80 fixture is the whole of it.
const QByteArray kMonitor = R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":128,"height":80,"scale":2.0,"transform":0,"activeWorkspace":{"id":1},"specialWorkspace":{"id":0}}])";
} // namespace

class CaptureHideTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;
    Log log;

    ThumbnailStack &stack() { return env->daemon().service<ThumbnailStack>(); }
    OverlayModel &model() { return env->daemon().service<Overlay>().model(); }
    Countdown &countdown() { return env->daemon().service<Countdown>(); }
    // Moves the test clock on and lets the Countdown notice.
    void advance(int ms) {
        env->setNow(env->now().addMSecs(ms));
        countdown().tick();
    }
    RecordingSession &session() { return env->daemon().service<RecordingSession>(); }
    // Ctrl+Print, an area, Enter (Alt+Enter for a GIF).
    void pickAndStart(bool gif = false) {
        { const auto r = env->request({"capture", "record"}); QVERIFY2(r.ok, qPrintable(r.error)); }
        dragArea();
        model().keyPressed(Qt::Key_Return, gif ? Qt::AltModifier : Qt::NoModifier);
    }
    void startRecording(bool gif = false) {
        pickAndStart(gif);
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, kTryMs);
    }
    void dragArea() { // logical (10,5) to (30,25)
        model().pointerPressed(10, 5, Qt::LeftButton);
        model().pointerMoved(30, 25);
        model().pointerReleased(30, 25, Qt::LeftButton);
    }
    // A Thumbnail on screen, then the log starts, counting `program`'s calls.
    void withAThumbnail(const QString &program = QStringLiteral("grim")) {
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        QCOMPARE(stack().count(), 1);
        watch(program);
    }
    void watch(const QString &program) {
        log.clear();
        connect(&stack(), &ThumbnailStack::captureHiddenChanged, this,
                [this, program] { log << qMakePair(stack().captureHidden(), int(env->calls(program).size())); });
    }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        env->setNow(QDateTime(QDate(2026, 11, 10), QTime(10, 0, 0)));
        env->setFixture("hyprctl", "monitors.json", kMonitor);
        env->setFixture("grim", "output.png", TestEnv::defaultGrimPng()); // read from the test's state, whatever PATH holds
        env->setPointer(10, 10);
    }
    void cleanup() {
        env.reset();
        log.clear();
    }

    void fullScreenHidesTheStackBeforeGrimAndBringsItBackWithTheNewThumbnail() {
        withAThumbnail();
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        QCOMPARE(log, (Log{{true, 1}, {false, 2}}));
        QCOMPARE(stack().count(), 2);
        QVERIFY(!stack().captureHidden());
    }

    // ---- the Overlay: hidden from before its frozen shot until it closes ----

    void anAreaCaptureKeepsTheStackHiddenWhileTheOverlayIsOpen() {
        withAThumbnail();
        QVERIFY(env->request({"capture", "area"}).ok);
        QCOMPARE(log, (Log{{true, 1}}));
        dragArea();
        QVERIFY(!model().active());
        QCOMPARE(log, (Log{{true, 1}, {false, 2}}));
        QCOMPARE(stack().count(), 2); // back with the new Thumbnail
    }

    void escapeBringsTheStackBackUnchanged() {
        withAThumbnail();
        QVERIFY(env->request({"capture", "area"}).ok);
        model().keyPressed(Qt::Key_Escape);
        QCOMPARE(log, (Log{{true, 1}, {false, 2}}));
        QCOMPARE(stack().count(), 1);
    }

    void aColorPickBringsTheStackBack() {
        withAThumbnail();
        QVERIFY(env->request({"capture", "area"}).ok);
        model().keyPressed(Qt::Key_4);
        QCOMPARE(model().mode(), OverlayModel::Color);
        model().pointerMoved(10, 10);
        model().pointerPressed(10, 10, Qt::LeftButton);
        model().pointerReleased(10, 10, Qt::LeftButton);
        QVERIFY(!model().active());
        QCOMPARE(log, (Log{{true, 1}, {false, 2}}));
    }

    void aSequenceBringsTheStackBackWithItsShotsWhenTheOverlayCloses() {
        withAThumbnail();
        QVERIFY(env->request({"capture", "area"}).ok);
        model().keyPressed(Qt::Key_S);
        dragArea();
        dragArea();
        QCOMPARE(log, (Log{{true, 1}}));
        int countWhenBack = -1;
        connect(&stack(), &ThumbnailStack::captureHiddenChanged, this, [&] { countWhenBack = stack().count(); });
        model().keyPressed(Qt::Key_Escape);
        QCOMPARE(log, (Log{{true, 1}, {false, 2}}));
        QCOMPARE(countWhenBack, 3); // the held shots were on the stack before it came back
    }

    void aTextCaptureBringsTheStackBack() {
        withAThumbnail();
        QVERIFY(env->request({"capture", "area"}).ok);
        model().keyPressed(Qt::Key_3);
        QCOMPARE(model().mode(), OverlayModel::Text);
        dragArea();
        QCOMPARE(log, (Log{{true, 1}, {false, 2}}));
    }

    void aFailedGrimBringsTheStackBack() {
        withAThumbnail();
        env->setMode("grim", "fail");
        QVERIFY(!env->request({"capture", "area"}).ok);
        QCOMPARE(log, (Log{{true, 1}, {false, 2}}));
        QVERIFY(!env->request({"capture", "fullscreen"}).ok);
        QCOMPARE(log, (Log{{true, 1}, {false, 2}, {true, 2}, {false, 3}}));
        QCOMPARE(stack().count(), 1);
    }

    // ---- Shift+Print: off screen for grim only ----

    void captureRepeatHidesTheStackForItsShot() {
        const QJsonObject window{{"address", "0xleft"}, {"mapped", true}, {"hidden", false}, {"workspace", QJsonObject{{"id", 1}}},
                                 {"at", QJsonArray{0, 0}}, {"size", QJsonArray{40, 40}}, {"fullscreen", 0}, {"floating", false},
                                 {"class", "left"}, {"title", "left"}, {"focusHistoryID", 0}};
        env->setFixture("hyprctl", "clients.json", QJsonDocument(QJsonArray{window}).toJson());
        env->setFixture("hyprctl", "activewindow.json", QJsonDocument(window).toJson());
        QVERIFY(env->request({"capture", "area"}).ok);
        dragArea(); // the Last area of "left", and a Thumbnail
        QCOMPARE(stack().count(), 1);
        watch(QStringLiteral("grim"));
        QVERIFY(env->request({"capture", "repeat"}).ok);
        QVERIFY(!model().active()); // no Overlay
        QCOMPARE(log, (Log{{true, 1}, {false, 2}}));
        QCOMPARE(stack().count(), 2);
    }

    // ---- the Timer: hidden from the Overlay through the Countdown to the shot ----

    void theTimerKeepsTheStackHiddenThroughTheCountdown() {
        withAThumbnail();
        QVERIFY(env->request({"capture", "timer"}).ok);
        dragArea();
        QVERIFY(!model().active());
        QVERIFY(countdown().active());
        QCOMPARE(log, (Log{{true, 1}})); // no flash between the Overlay and the Countdown
        advance(1000);
        QVERIFY(stack().captureHidden());
        advance(2500);
        QVERIFY(!countdown().active());
        QCOMPARE(log, (Log{{true, 1}, {false, 3}})); // back after the Countdown's own shot
        QCOMPARE(stack().count(), 2);
    }

    void aCanceledCountdownBringsTheStackBack() {
        withAThumbnail();
        QVERIFY(env->request({"capture", "timer"}).ok);
        dragArea();
        QVERIFY(env->request({"capture", "timer"}).ok); // pressed again: canceled
        QVERIFY(!countdown().active());
        QCOMPARE(log, (Log{{true, 1}, {false, 2}}));
        QCOMPARE(stack().count(), 1);
    }

    // ---- a Recording: hidden until the recorder has exited ----

    void aRecordingKeepsTheStackHiddenUntilTheRecorderExits() {
        withAThumbnail(QStringLiteral("gpu-screen-recorder"));
        RecordingSession::State whenBack = RecordingSession::Idle;
        connect(&stack(), &ThumbnailStack::captureHiddenChanged, this, [&] { whenBack = session().state(); });
        startRecording();
        QCOMPARE(log, (Log{{true, 0}})); // from the Overlay, through the start, with no flash
        QVERIFY(env->request({"capture", "record"}).ok); // stop
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, kTryMs);
        QCOMPARE(log, (Log{{true, 0}, {false, 1}}));
        QCOMPARE(whenBack, RecordingSession::Recording); // the recorder had exited; the Thumbnail was still to come
        QCOMPARE(stack().count(), 2);
    }

    void aGifRecordingBringsTheStackBackBeforeItsEncodingEnds() {
        withAThumbnail(QStringLiteral("gpu-screen-recorder"));
        bool gifWasDone = true;
        connect(&stack(), &ThumbnailStack::captureHiddenChanged, this,
                [&] { gifWasDone = !env->calls("gifski").isEmpty() || session().state() == RecordingSession::Done; });
        startRecording(true);
        QVERIFY(env->request({"capture", "record"}).ok);
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, kTryMs);
        QCOMPARE(log, (Log{{true, 0}, {false, 1}}));
        QVERIFY(!gifWasDone);
    }

    void deletingTheRecordingBringsTheStackBack() {
        withAThumbnail(QStringLiteral("gpu-screen-recorder"));
        startRecording();
        session().deleteRecording();
        QCOMPARE(log, (Log{{true, 0}, {false, 1}}));
        QCOMPARE(stack().count(), 1);
    }

    void restartKeepsTheStackHidden() {
        withAThumbnail(QStringLiteral("gpu-screen-recorder"));
        startRecording();
        session().restart();
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, kTryMs);
        QCOMPARE(log, (Log{{true, 0}}));
        session().deleteRecording();
        QVERIFY(!stack().captureHidden());
    }

    void aRecorderThatDiesBringsTheStackBack() {
        withAThumbnail(QStringLiteral("gpu-screen-recorder"));
        env->setMode("gpu-screen-recorder", "die");
        pickAndStart();
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Idle, kTryMs);
        QCOMPARE(log.size(), 2);
        QVERIFY(!stack().captureHidden());
    }

    void aMissingGifskiBringsTheStackBack() {
        withAThumbnail(QStringLiteral("gpu-screen-recorder"));
        // A PATH holding every stub except gifski.
        const QString dir = env->scratch() + "/no-gifski";
        QVERIFY(QDir().mkpath(dir));
        for (const QFileInfo &stub : QDir(QStringLiteral(TARN_STUB_DIR)).entryInfoList(QDir::Files))
            if (stub.fileName() != "gifski")
                QVERIFY(QFile::link(stub.absoluteFilePath(), dir + '/' + stub.fileName()));
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", (dir + ':' + env->scratch() + "/sysbin").toUtf8());
        pickAndStart(true);
        qputenv("PATH", oldPath);
        QCOMPARE(session().state(), RecordingSession::Idle);
        QCOMPARE(log, (Log{{true, 0}, {false, 0}}));
    }

    void aThumbnailAddedDuringARecordingWaitsForItsEnd() {
        withAThumbnail(QStringLiteral("gpu-screen-recorder"));
        startRecording();
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        QCOMPARE(stack().count(), 2);
        QVERIFY(stack().captureHidden());
        session().deleteRecording();
        QVERIFY(!stack().captureHidden());
    }

    // ---- auto-close: paused while hidden, the full delay again on return ----

    void noThumbnailAutoClosesWhileHiddenAndTheFullDelayStartsOnReturn() {
        QFile config(env->scratch() + "/config/tarn/config.toml");
        QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Truncate));
        config.write("close_thumbnail_after = 0.4\n");
        config.close();
        QTRY_COMPARE(env->daemon().service<tarn::Settings>().closeThumbnailAfterSeconds(), 0.4);
        withAThumbnail();
        QVERIFY(env->request({"capture", "area"}).ok);
        QTest::qWait(700); // past its delay, off screen
        QCOMPARE(stack().count(), 1);
        model().keyPressed(Qt::Key_Escape);
        QTest::qWait(200); // back for less than the delay
        QCOMPARE(stack().count(), 1);
        QTRY_COMPARE(stack().count(), 0);
    }

    // ---- a second key press while the stack leaves ----

    // The hide lets the event loop run once before grim, so a request queued now
    // arrives in the middle of the first. It must not start a second shot.
    void aSecondPressDuringTheHideDoesNotGrabAgain_data() {
        QTest::addColumn<QStringList>("first");
        QTest::addColumn<QStringList>("second");
        QTest::newRow("area, area") << QStringList{"capture", "area"} << QStringList{"capture", "area"};
        QTest::newRow("area, full screen") << QStringList{"capture", "area"} << QStringList{"capture", "fullscreen"};
        QTest::newRow("full screen, area") << QStringList{"capture", "fullscreen"} << QStringList{"capture", "area"};
        QTest::newRow("full screen, full screen") << QStringList{"capture", "fullscreen"} << QStringList{"capture", "fullscreen"};
    }
    void aSecondPressDuringTheHideDoesNotGrabAgain() {
        QFETCH(QStringList, first);
        QFETCH(QStringList, second);
        withAThumbnail();
        bool answered = false;
        QMetaObject::invokeMethod(this, [&] { answered = env->request(second).ok; }, Qt::QueuedConnection);
        QVERIFY(env->request(first).ok);
        QVERIFY(answered); // it was handled during the first, quietly
        QCOMPARE(env->calls("grim").size(), 2);
        QVERIFY(env->calls("notify-send").isEmpty());
    }

    void withNoThumbnailTheOverlayStillOpensAndCloses() {
        watch(QStringLiteral("grim"));
        QVERIFY(env->request({"capture", "area"}).ok);
        dragArea();
        QCOMPARE(stack().count(), 1);
        QVERIFY(!stack().captureHidden());
    }
};

QTEST_MAIN(CaptureHideTest)
#include "capture_hide_test.moc"
