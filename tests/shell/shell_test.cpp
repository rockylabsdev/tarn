// The Shell socket. A stand-in shell (a QLocalSocket reading JSON lines) connects to
// $XDG_RUNTIME_DIR/tarn/shell.sock in the scratch runtime folder and checks every row of the
// state table in docs/shell-socket.md. The recorder, gifski and notifications are the usual stubs.
#include "test_env.h"

#include "overlay/overlay.h"
#include "recording/recording_session.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTest>

using tarn::Overlay;
using tarn::OverlayModel;
using tarn::RecordingSession;
using tarn::ThumbnailStack;
using tarn::test::TestEnv;

namespace {
const QByteArray kMonitor = R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":128,"height":80,"scale":2.0,"transform":0}])";
constexpr int Enter = Qt::Key_Return;

QByteArray gradientPng() {
    QImage image(128, 80, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixel(x, y, qRgb(x * 2, y * 3, (x ^ y) * 4));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// A stand-in for the shell's side: connects, collects every line, can send lines.
struct FakeShell {
    QLocalSocket socket;
    QList<QJsonObject> lines;
    QList<qint64> arrivals; // ms since the first line

    bool connectTo(const QString &path) {
        QObject::connect(&socket, &QLocalSocket::readyRead, &socket, [this] {
            while (socket.canReadLine()) {
                if (!clock.isValid())
                    clock.start();
                lines << QJsonDocument::fromJson(socket.readLine()).object();
                arrivals << clock.elapsed();
            }
        });
        socket.connectToServer(path);
        return socket.waitForConnected(2000);
    }
    void send(const QByteArray &bytes) {
        socket.write(bytes);
        socket.flush();
    }
    QStringList states() const {
        QStringList out;
        for (const auto &l : lines)
            out << l.value("recording").toString();
        return out;
    }
    QElapsedTimer clock;
};
} // namespace

class ShellSocketTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    RecordingSession &session() { return env->daemon().service<RecordingSession>(); }
    ThumbnailStack &stack() { return env->daemon().service<ThumbnailStack>(); }
    OverlayModel &model() { return env->daemon().service<Overlay>().model(); }
    QString sock() const { return env->runtimeDir() + "/tarn/shell.sock"; }

    void startRecording(bool gif = false) {
        QVERIFY(env->request({"capture", "record"}).ok);
        model().pointerPressed(10, 5, Qt::LeftButton);
        model().pointerMoved(30, 25);
        model().pointerReleased(30, 25, Qt::LeftButton);
        model().keyPressed(Enter, gif ? Qt::AltModifier : Qt::NoModifier);
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, 5000);
    }
    void stopByKey() { QVERIFY(env->request({"capture", "record"}).ok); }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        env->setFixture("hyprctl", "monitors.json", kMonitor);
        env->setFixture("grim", "output.png", gradientPng());
        env->setPointer(10, 10);
    }
    void cleanup() { env.reset(); }

    void theDaemonListensInTheRuntimeFolder() {
        QVERIFY(QFileInfo(sock()).exists());
        QVERIFY(!QFileInfo::exists(env->runtimeDir() + "/tarn/island.sock")); // no alias for the old name
        FakeShell shell;
        QVERIFY(shell.connectTo(sock()));
    }

    // ---- idle, and the current state on connect ----

    void aNewConnectionGetsIdleWhenNothingIsRecording() {
        FakeShell shell;
        QVERIFY(shell.connectTo(sock()));
        QTRY_COMPARE(shell.lines.size(), 1);
        QCOMPARE(shell.lines[0].value("recording").toString(), QStringLiteral("idle"));
    }

    void aClientConnectingMidRecordingGetsItAtOnce() {
        startRecording();
        QTest::qWait(60);
        FakeShell shell;
        QVERIFY(shell.connectTo(sock()));
        QTRY_COMPARE(shell.lines.size(), 1);
        const auto l = shell.lines[0];
        QCOMPARE(l.value("recording").toString(), QStringLiteral("recording"));
        QCOMPARE(l.value("format").toString(), QStringLiteral("mp4"));
        QVERIFY(l.value("elapsedMs").toInteger() >= 50);
        QVERIFY(!l.contains("progress"));
        stopByKey();
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, 10000);
    }

    void aClientConnectingAfterDoneGetsDone() {
        startRecording();
        stopByKey();
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, 10000);
        FakeShell shell;
        QVERIFY(shell.connectTo(sock()));
        QTRY_COMPARE(shell.lines.size(), 1);
        QCOMPARE(shell.lines[0].value("recording").toString(), QStringLiteral("done"));
    }

    void aRestartedDaemonListensAgainAndStartsIdle() {
        startRecording();
        env->restartDaemon();
        FakeShell shell;
        QVERIFY(shell.connectTo(sock()));
        QTRY_COMPARE(shell.lines.size(), 1);
        QCOMPARE(shell.lines[0].value("recording").toString(), QStringLiteral("idle"));
    }

    // ---- the state table ----

    void anMp4WalksStartingRecordingDoneWithoutEncoding() {
        FakeShell shell;
        QVERIFY(shell.connectTo(sock()));
        QTRY_COMPARE(shell.lines.size(), 1);
        startRecording();
        stopByKey();
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, 10000);
        QTRY_COMPARE(shell.states(), (QStringList{"idle", "starting", "recording", "done"}));
        for (int i = 1; i < shell.lines.size(); ++i) {
            QCOMPARE(shell.lines[i].value("format").toString(), QStringLiteral("mp4"));
            QVERIFY(shell.lines[i].contains("elapsedMs"));
            QVERIFY(!shell.lines[i].contains("progress"));
        }
        QVERIFY(shell.lines[3].value("elapsedMs").toInteger() >= 0);
    }

    void pauseAndResumeSendPausedThenRecordingAndTheTimeHolds() {
        startRecording();
        FakeShell shell;
        QVERIFY(shell.connectTo(sock()));
        QTRY_COMPARE(shell.lines.size(), 1);
        session().pause();
        QTRY_COMPARE(shell.lines.size(), 2);
        QCOMPARE(shell.lines[1].value("recording").toString(), QStringLiteral("paused"));
        const auto held = shell.lines[1].value("elapsedMs").toInteger();
        QTest::qWait(100);
        session().resume();
        QTRY_COMPARE(shell.lines.size(), 3);
        QCOMPARE(shell.lines[2].value("recording").toString(), QStringLiteral("recording"));
        QVERIFY(shell.lines[2].value("elapsedMs").toInteger() - held < 60); // the pause was not counted
        stopByKey();
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, 10000);
    }

    void restartGoesBackThroughStartingAtZero() {
        startRecording();
        FakeShell shell;
        QVERIFY(shell.connectTo(sock()));
        QTRY_COMPARE(shell.lines.size(), 1);
        session().restart();
        QTRY_COMPARE_WITH_TIMEOUT(shell.states(), (QStringList{"recording", "starting", "recording"}), 5000);
        QVERIFY(shell.lines[1].value("elapsedMs").toInteger() < 100);
        stopByKey();
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, 10000);
    }

    void deleteSendsDeleted() {
        startRecording();
        FakeShell shell;
        QVERIFY(shell.connectTo(sock()));
        QTRY_COMPARE(shell.lines.size(), 1);
        session().deleteRecording();
        QTRY_COMPARE(shell.states(), (QStringList{"recording", "deleted"}));
    }

    void aGifSendsEncodingWithRisingProgressAtMostTenAtSecondThenDone() {
        env->setFixture("gifski", "frames", "60");
        env->setFixture("gifski", "frame_delay", "0.01");
        FakeShell shell;
        QVERIFY(shell.connectTo(sock()));
        QTRY_COMPARE(shell.lines.size(), 1);
        startRecording(true);
        stopByKey();
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, 15000);
        QTRY_COMPARE(shell.states().last(), QStringLiteral("done"));

        QList<double> progress;
        QList<qint64> times;
        for (int i = 0; i < shell.lines.size(); ++i) {
            const auto l = shell.lines[i];
            QCOMPARE(l.value("format").toString(), i == 0 ? QString() : QStringLiteral("gif"));
            if (l.value("recording").toString() == "encoding") {
                QVERIFY(l.contains("progress"));
                progress << l.value("progress").toDouble();
                times << shell.arrivals[i];
            } else {
                QVERIFY(!l.contains("progress"));
            }
        }
        QVERIFY(progress.size() >= 2);
        QCOMPARE(progress.first(), 0.0);
        QCOMPARE(progress.last(), 1.0);
        for (int i = 1; i < progress.size(); ++i)
            QVERIFY(progress[i] > progress[i - 1]);
        // No more than 10 a second: any 1 s window holds at most 10 (+1 for the edge) lines.
        for (int i = 0; i + 10 < times.size(); ++i)
            QVERIFY2(times[i + 10] - times[i] >= 900, "more than 10 encoding lines in one second");
        const auto states = shell.states();
        QVERIFY(states.indexOf("recording") < states.indexOf("encoding"));
        QVERIFY(states.indexOf("encoding") < states.indexOf("done"));
    }

    // ---- shell -> Tarn ----

    void stopFromTheShellFinishesTheRecordingToAThumbnail() {
        startRecording();
        FakeShell shell;
        QVERIFY(shell.connectTo(sock()));
        QTRY_COMPARE(shell.lines.size(), 1);
        shell.send("{\"stop\": true}\n");
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, 10000);
        QCOMPARE(stack().ids().size(), 1);
        QCOMPARE(stack().chip(stack().ids().first()), QStringLiteral("MP4 0:06"));
        QTRY_COMPARE(shell.states().last(), QStringLiteral("done"));
    }

    void stopFromTheShellWorksWhilePaused() {
        startRecording();
        session().pause();
        FakeShell shell;
        QVERIFY(shell.connectTo(sock()));
        QTRY_COMPARE(shell.lines.size(), 1);
        QCOMPARE(shell.lines[0].value("recording").toString(), QStringLiteral("paused"));
        shell.send("{\"stop\":true}\n");
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, 10000);
    }

    void stopWhileNothingIsRecordingDoesNothing() {
        FakeShell shell;
        QVERIFY(shell.connectTo(sock()));
        QTRY_COMPARE(shell.lines.size(), 1);
        shell.send("{\"stop\": true}\n");
        QTest::qWait(150);
        QCOMPARE(session().state(), RecordingSession::Idle);
        QCOMPARE(shell.lines.size(), 1);
    }

    void stopDuringStartingIsIgnoredNotQueued() {
        env->setFixture("gpu-screen-recorder", "start_delay", "0.6");
        QVERIFY(env->request({"capture", "record"}).ok);
        model().pointerPressed(10, 5, Qt::LeftButton);
        model().pointerMoved(30, 25);
        model().pointerReleased(30, 25, Qt::LeftButton);
        model().keyPressed(Enter);
        QCOMPARE(session().state(), RecordingSession::Starting);
        FakeShell shell;
        QVERIFY(shell.connectTo(sock()));
        QTRY_COMPARE(shell.lines.size(), 1);
        shell.send("{\"stop\": true}\n");
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, 5000);
        QTest::qWait(300);
        QCOMPARE(session().state(), RecordingSession::Recording);
        stopByKey();
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, 10000);
    }

    // ---- robustness ----

    void unknownKeysAndGarbageFromTheShellAreIgnored() {
        startRecording();
        FakeShell shell;
        QVERIFY(shell.connectTo(sock()));
        QTRY_COMPARE(shell.lines.size(), 1);
        shell.send("not json\n{\"pause\": true}\n{\"stop\": false}\n[1,2]\n{\"stop\": \"yes\"}\n\n");
        QTest::qWait(200);
        QCOMPARE(session().state(), RecordingSession::Recording);
        // A known key with extras still works, and a line split over two writes is put together.
        shell.send("{\"future\": [1], \"stop\"");
        QTest::qWait(50);
        QCOMPARE(session().state(), RecordingSession::Recording);
        shell.send(": true, \"x\": null}\n");
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, 10000);
    }

    void everyLineCarriesOnlyKnownKeysSoNewOnesCanBeAddedLater() {
        startRecording();
        FakeShell shell;
        QVERIFY(shell.connectTo(sock()));
        QTRY_COMPARE(shell.lines.size(), 1);
        for (const QString &k : shell.lines[0].keys())
            QVERIFY2((QStringList{"recording", "elapsedMs", "format", "progress"}).contains(k), qPrintable(k));
    }

    void tarnBehavesTheSameWithNoClientAndWithClientsComingAndGoing() {
        {
            FakeShell shell;
            QVERIFY(shell.connectTo(sock()));
            QTRY_COMPARE(shell.lines.size(), 1);
        } // the shell quits
        startRecording();
        FakeShell a, b;
        QVERIFY(a.connectTo(sock()));
        QVERIFY(b.connectTo(sock()));
        QTRY_COMPARE(a.lines.size(), 1);
        QTRY_COMPARE(b.lines.size(), 1);
        stopByKey();
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, 10000); // both get it
        QTRY_COMPARE(a.states().last(), QStringLiteral("done"));
        QTRY_COMPARE(b.states().last(), QStringLiteral("done"));
        QCOMPARE(stack().ids().size(), 1);
    }
};

QTEST_MAIN(ShellSocketTest)
#include "shell_test.moc"
