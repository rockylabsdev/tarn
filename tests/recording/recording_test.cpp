// Ctrl+Print records an area to an MP4. The recorder is the stub
// gpu-screen-recorder (a small real MP4 written after a delay, stopped by SIGINT);
// ffmpeg and ffprobe are the real ones, reading that sample.
#include "test_env.h"

#include "overlay/overlay.h"
#include "recording/recording_bar.h"
#include "recording/recording_session.h"
#include "recording/recording_store.h"
#include "settings/settings.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTest>

using tarn::Overlay;
using tarn::OverlayModel;
using tarn::RecordingSession;
using tarn::ThumbnailStack;
using tarn::test::TestEnv;

namespace {
// How long a wait on the visible result may take. The recorder, ffmpeg and gifski are separate
// processes, and a loaded machine can stretch a wait well past QTRY's 5 s default (18 s seen
// with the CPUs busy). A passing wait returns the moment it is true; a long limit only costs
// time when the result never comes.
constexpr int kTryMs = 60000;
const QByteArray kMonitor = R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":128,"height":80,"scale":2.0,"transform":0}])";
const QString kSample = QStringLiteral(TARN_STUB_DIR "/fixtures/gpu-screen-recorder/sample.mp4");
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
} // namespace

class RecordingTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    RecordingSession &session() { return env->daemon().service<RecordingSession>(); }
    ThumbnailStack &stack() { return env->daemon().service<ThumbnailStack>(); }
    OverlayModel &model() { return env->daemon().service<Overlay>().model(); }
    static QByteArray sample() { return TestEnv::readFile(kSample); }

    // Ctrl+Print, a drag of (10,5) 20 x 20 logical px (40 x 40 device px at scale 2), Enter.
    void pickAndStart() {
        QVERIFY(env->request({"capture", "record"}).ok);
        QVERIFY(model().active());
        QCOMPARE(model().mode(), OverlayModel::Record);
        model().pointerPressed(10, 5, Qt::LeftButton);
        model().pointerMoved(30, 25);
        model().pointerReleased(30, 25, Qt::LeftButton);
        model().keyPressed(Enter);
    }
    // Ctrl+Print, Enter, then wait for the recorder to be writing.
    void startRecording() {
        pickAndStart();
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, kTryMs);
    }
    void stopAndWait() {
        QVERIFY(env->request({"capture", "record"}).ok);
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, kTryMs);
    }
    int recordOne() {
        startRecording();
        stopAndWait();
        return stack().ids().last();
    }
    // Something else leaves the restore slot.
    void closeAScreenshot() {
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        stack().close(stack().ids().last());
    }

    void startGif() {
        QVERIFY(env->request({"capture", "record"}).ok);
        model().pointerPressed(10, 5, Qt::LeftButton);
        model().pointerMoved(30, 25);
        model().pointerReleased(30, 25, Qt::LeftButton);
        model().keyPressed(Enter, Qt::AltModifier);
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, kTryMs);
    }
    void stopGif() {
        QVERIFY(env->request({"capture", "record"}).ok);
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, kTryMs);
    }
    static QByteArray gifSample() { return TestEnv::readFile(QStringLiteral(TARN_STUB_DIR "/fixtures/gifski/sample.gif")); }


private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        env->setFixture("hyprctl", "monitors.json", kMonitor);
        env->setFixture("grim", "output.png", gradientPng());
        env->setPointer(10, 10);
    }
    void cleanup() { env.reset(); }

    // ---- the Overlay in record mode ----

    void recordModeKeepsAFinishedDragSoTheRecordPanelCanSitOverIt() {
        OverlayModel m;
        QSignalSpy confirmed(&m, &OverlayModel::confirmed);
        m.open(OverlayModel::Record, QSize(64, 40));
        m.pointerPressed(10, 5, Qt::LeftButton);
        m.pointerReleased(30, 25, Qt::LeftButton); // no Ctrl, yet it is kept
        QVERIFY(m.kept());
        QVERIFY(m.active());
        QVERIFY(confirmed.isEmpty());
        QCOMPARE(m.selection(), QRect(10, 5, 20, 20));
        m.keyPressed(Enter);
        QCOMPARE(confirmed.count(), 1);
        QCOMPARE(confirmed.first().at(0).toRect(), QRect(10, 5, 20, 20));
        QCOMPARE(confirmed.first().at(1).value<OverlayModel::Mode>(), OverlayModel::Record);
    }

    void recordModeSelectsAnAreaOnlyAndEscCancels() {
        OverlayModel m;
        QSignalSpy canceled(&m, &OverlayModel::canceled);
        m.open(OverlayModel::Record, QSize(64, 40));
        m.pointerPressed(10, 5, Qt::LeftButton);
        m.pointerReleased(30, 25, Qt::LeftButton);
        m.keyPressed(Qt::Key_Escape); // clears the kept Selection (the Handles' rule)
        QVERIFY(!m.kept());
        m.keyPressed(Qt::Key_Escape);
        QCOMPARE(canceled.count(), 1);
    }

    void altEnterConfirmsAsAGifAndEnterAsAnMp4() {
        OverlayModel m;
        QSignalSpy confirmed(&m, &OverlayModel::confirmed);
        bool gifWhileDelivered = false;
        connect(&m, &OverlayModel::confirmed, this, [&] { gifWhileDelivered = m.confirmedAsGif(); });
        m.open(OverlayModel::Record, QSize(64, 40));
        m.pointerPressed(10, 5, Qt::LeftButton);
        m.pointerReleased(30, 25, Qt::LeftButton);
        m.keyPressed(Enter, Qt::AltModifier);
        QCOMPARE(confirmed.count(), 1);
        QVERIFY(gifWhileDelivered);
        QVERIFY(!m.confirmedAsGif()); // only while the signal is delivered
        m.open(OverlayModel::Record, QSize(64, 40));
        m.pointerPressed(10, 5, Qt::LeftButton);
        m.pointerReleased(30, 25, Qt::LeftButton);
        m.keyPressed(Enter);
        QCOMPARE(confirmed.count(), 2);
        QVERIFY(!gifWhileDelivered);
    }

    void aKeyboardDragIsKeptToo() {
        OverlayModel m;
        QSignalSpy confirmed(&m, &OverlayModel::confirmed);
        m.open(OverlayModel::Record, QSize(64, 40));
        m.pointerMoved(10, 5);
        m.keyPressed(Enter);
        m.pointerMoved(30, 25);
        m.keyPressed(Enter);
        QVERIFY(m.kept());
        QVERIFY(confirmed.isEmpty());
        m.keyPressed(Enter);
        QCOMPARE(confirmed.count(), 1);
    }

    // ---- starting: the recorder and the session's states ----

    void theRecorderGetsTheRegionAt60FpsWithThePointer() {
        startRecording();
        const auto calls = env->calls("gpu-screen-recorder");
        QCOMPARE(calls.size(), 1);
        const QStringList kept = {env->keptRecordings() + "/2026-11-03_09-30-15.mp4"};
        QCOMPARE(calls.first(), (QStringList{"-w", "region", "-region", "40x40+20+10", "-f", "60", "-k", "h264", "-c", "mp4",
                                              "-cursor", "yes", "-fallback-cpu-encoding", "yes", "-o", kept.first()}));
    }

    void everyRecorderStartAsksForTheCpuFallback() {
        // No encoder probe: the flag is always there, MP4 or GIF, with or without sound.
        const QRect r(0, 0, 40, 40);
        for (const bool gif : {false, true}) {
            const QStringList args = RecordingSession::recorderArguments(r, true, "/o.mp4", true, true, gif);
            QCOMPARE(args.at(args.indexOf("-fallback-cpu-encoding") + 1), QString("yes"));
            QCOMPARE(args.at(args.indexOf("-k") + 1), QString("h264"));
        }
    }

    void anOrdinaryMp4KeepsTheRecordersDefaultQuality() {
        startRecording();
        QVERIFY(!env->calls("gpu-screen-recorder").first().contains("-q"));
    }

    void thePointerSettingCanLeaveItOut() {
        QDir().mkpath(env->scratch() + "/config/tarn");
        QFile config(env->scratch() + "/config/tarn/config.toml");
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write("show_pointer_in_recordings = false\n");
        config.close();
        env->restartDaemon();
        startRecording();
        const QStringList args = env->calls("gpu-screen-recorder").first();
        QCOMPARE(args.at(args.indexOf("-cursor") + 1), QString("no"));
    }

    void oddSizesAreRoundedDownToEvenForH264() {
        QVERIFY(session().start(QRect(1, 1, 13, 13), QRect(0, 0, 64, 40), 1.0, "DP-1").ok);
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, kTryMs);
        QVERIFY(env->calls("gpu-screen-recorder").first().contains("12x12+1+1"));
    }

    void statesGoIdleStartingRecordingDone() {
        QCOMPARE(session().state(), RecordingSession::Idle);
        QSignalSpy states(&session(), &RecordingSession::stateChanged);
        recordOne();
        QList<RecordingSession::State> seen;
        for (const auto &s : states)
            seen << s.at(0).value<RecordingSession::State>();
        QCOMPARE(seen, (QList<RecordingSession::State>{RecordingSession::Starting, RecordingSession::Recording, RecordingSession::Done}));
    }

    void aSlowPosterFrameDoesNotBlockTheDaemonAfterStop() {
        startRecording();
        env->setFixture("ffmpeg", "poster_delay", "1");
        QVERIFY(env->request({"capture", "record"}).ok); // stop
        QCOMPARE(session().barVisible(), false);
        QVERIFY(session().state() != RecordingSession::Done); // the poster is being made
        QElapsedTimer clock;
        clock.start();
        QVERIFY(env->request({"settings"}).ok); // answered meanwhile
        QVERIFY(clock.elapsed() < 800);
        QCOMPARE(stack().count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, kTryMs);
        QCOMPARE(stack().count(), 1);
        QCOMPARE(stack().chip(stack().ids().last()), QString("MP4 0:06"));
    }

    void itIsRecordingOnlyOnceTheOutputFileExistsAndTheBarWaitsForThat() {
        env->setFixture("gpu-screen-recorder", "start_delay", "0.8");
        pickAndStart();
        // The outline is red at once; there is no bar yet and no file.
        QCOMPARE(session().state(), RecordingSession::Starting);
        QVERIFY(session().active());
        QVERIFY(!session().barVisible());
        QVERIFY(!QFileInfo::exists(session().outputPath()));
        QTest::qWait(300);
        QCOMPARE(session().state(), RecordingSession::Starting);
        QVERIFY(!session().barVisible());

        bool fileExistedAtTheTransition = false, barShown = false;
        connect(&session(), &RecordingSession::stateChanged, this, [&](RecordingSession::State s) {
            if (s == RecordingSession::Recording) {
                fileExistedAtTheTransition = QFileInfo::exists(session().outputPath());
                barShown = session().barVisible();
            }
        });
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, kTryMs);
        QVERIFY(fileExistedAtTheTransition);
        QVERIFY(barShown);
        session().disconnect(this);
    }

    void aSecondCtrlPrintWhileStartingStopsOnceItIsWriting() {
        env->setFixture("gpu-screen-recorder", "start_delay", "0.5");
        pickAndStart();
        QCOMPARE(session().state(), RecordingSession::Starting);
        QVERIFY(env->request({"capture", "record"}).ok); // stop asked for early
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, kTryMs);
        QCOMPARE(stack().count(), 1);
    }

    void escNeverStopsARecording() {
        startRecording();
        model().keyPressed(Qt::Key_Escape); // the Overlay is closed: nothing listens
        model().keyPressed(Qt::Key_Escape);
        QTest::qWait(300);
        QCOMPARE(session().state(), RecordingSession::Recording);
        QVERIFY(session().barVisible());
        QVERIFY(env->readFile(env->keptRecordings() + "/2026-11-03_09-30-15.mp4") == sample());
    }

    void aRecorderThatCannotStartNotifiesAndLeavesNothing() {
        env->setMode("gpu-screen-recorder", "die");
        pickAndStart();
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Idle, kTryMs);
        QCOMPARE(env->calls("notify-send").size(), 1);
        QVERIFY(TestEnv::filesIn(env->keptRecordings()).isEmpty());
        QCOMPARE(stack().count(), 0);
    }

    void stopBeforeTheFileExistsSendsNoSignalYet() { // a signal then could kill the recorder's output
        env->setMode("gpu-screen-recorder", "silent");
        pickAndStart();
        QTest::qWait(200);
        session().stop();
        QTest::qWait(200);
        QCOMPARE(session().state(), RecordingSession::Starting);
        const QString signalLog = env->scratch() + "/stubs/signals/gpu-screen-recorder.log";
        QVERIFY(!QFileInfo::exists(signalLog) || TestEnv::readFile(signalLog).isEmpty());
    }

    void onlyOneRecordingAtATime() {
        startRecording();
        QVERIFY(!session().start(QRect(0, 0, 10, 10), QRect(0, 0, 64, 40), 2.0).ok);
        QCOMPARE(env->calls("gpu-screen-recorder").size(), 1);
    }

    // ---- stopping and the Thumbnail ----

    void ctrlPrintStopsAndTheRecordingLandsAsAThumbnailWithAChip() {
        QSignalSpy recorded(&session(), &RecordingSession::recorded);
        const int id = recordOne();
        QCOMPARE(recorded.count(), 1);
        QCOMPARE(stack().kind(id), ThumbnailStack::Recording);
        QCOMPARE(stack().chip(id), QString("MP4 0:06"));
        QCOMPARE(stack().videoPath(id), env->keptRecordings() + "/2026-11-03_09-30-15.mp4");
        QVERIFY(!stack().image(id).isNull());
        QCOMPARE(TestEnv::readFile(stack().videoPath(id)), sample());
        QVERIFY(!session().barVisible());
        QVERIFY(!session().active());
        QCOMPARE(env->calls("notify-send").size(), 0);
    }

    void aRecordingNeverClosesOnItsOwnAndIsNotInHistory() {
        const int id = recordOne();
        QVERIFY(!stack().autoClose(id));
        QVERIFY(stack().historyId(id).isEmpty());
        QVERIFY(!QDir(env->scratch() + "/data/tarn/history").exists() || QDir(env->scratch() + "/data/tarn/history").isEmpty());
    }

    void theBarSitsOutsideTheAreaSoItIsNeverInTheVideo() {
        // A 256 x 160 logical monitor, so a bar fits.
        QImage big(512, 320, QImage::Format_RGB32);
        big.fill(Qt::darkCyan);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        big.save(&buffer, "PNG");
        env->setFixture("grim", "output.png", png);
        env->setFixture("hyprctl", "monitors.json",
                        R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":512,"height":320,"scale":2.0,"transform":0}])");
        QVERIFY(env->request({"capture", "record"}).ok);
        model().pointerPressed(20, 10, Qt::LeftButton);
        model().pointerReleased(100, 60, Qt::LeftButton);
        model().keyPressed(Enter);
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Recording, kTryMs);
        QVERIFY(session().hasBar());
        const QRect bar = session().barRect(), area = session().area();
        QCOMPARE(area, QRect(20, 10, 80, 50));
        QVERIFY(!bar.intersects(area.adjusted(-2, -2, 2, 2))); // not in the video, nor over its outline
        QVERIFY(QRect(0, 0, 256, 160).contains(bar));
        QVERIFY(bar.top() > area.bottom()); // below, the first choice
    }

    // ---- Save, Copy, restore, kept files ----

    void saveWritesToTheRecordingsFolderAndClosesTheThumbnail() {
        const int id = recordOne();
        const QString path = stack().save(id);
        QCOMPARE(path, env->recordings() + "/2026-11-03_09-30-15.mp4");
        QCOMPARE(TestEnv::readFile(path), sample());
        QCOMPARE(stack().count(), 0);
        // A second Recording in the same second never overwrites the first.
        const int again = recordOne();
        QCOMPARE(stack().save(again), env->recordings() + "/2026-11-03_09-30-15_2.mp4");
    }

    void copyPutsTheFileOnTheClipboardAndSurvivesTheKeptFile() {
        const int id = recordOne();
        QVERIFY(stack().copy(id));
        const auto calls = env->calls("tarn-clip");
        QCOMPARE(calls.size(), 1);
        QCOMPARE(calls.first().first(), QString("--file-only"));
        const QString shared = calls.first().last();
        QVERIFY(shared.startsWith(env->scratch() + "/cache/tarn/shared/"));
        QCOMPARE(TestEnv::readFile(shared), sample());
        QCOMPARE(stack().count(), 0);

        // The Recording can no longer be restored once another Thumbnail takes the slot:
        // its kept file goes, but what the clipboard offers stays.
        QVERIFY(QFileInfo::exists(env->keptRecordings() + "/2026-11-03_09-30-15.mp4"));
        closeAScreenshot();
        QVERIFY(TestEnv::filesIn(env->keptRecordings()).isEmpty());
        QCOMPARE(TestEnv::readFile(shared), sample());
    }

    void restoreBringsBackAClosedRecordingWithItsKeptFile() {
        const int id = recordOne();
        const QString kept = stack().videoPath(id);
        stack().close(id);
        QCOMPARE(stack().count(), 0);
        QVERIFY(QFileInfo::exists(kept));
        QVERIFY(env->request({"restore"}).ok);
        QCOMPARE(stack().count(), 1);
        const int back = stack().ids().last();
        QCOMPARE(stack().kind(back), ThumbnailStack::Recording);
        QCOMPARE(stack().videoPath(back), kept);
        QCOMPARE(TestEnv::readFile(kept), sample());
        QCOMPARE(stack().chip(back), QString("MP4 0:06"));
    }

    void theKeptFileGoesOnceTheRecordingCanNoLongerBeRestored() {
        const int id = recordOne();
        const QString kept = stack().videoPath(id);
        stack().close(id);
        QVERIFY(QFileInfo::exists(kept));
        closeAScreenshot(); // takes the restore slot
        QVERIFY(!QFileInfo::exists(kept));
        QVERIFY(TestEnv::filesIn(env->recordings()).isEmpty()); // closed unsaved: gone, as the user chose
    }

    void aSavedRecordingIsNotSavedAgainAfterACrash() {
        const int id = recordOne();
        QVERIFY(!stack().save(id).isEmpty()); // the kept file stays for restore
        QVERIFY(!TestEnv::filesIn(env->keptRecordings()).isEmpty());
        env->restartDaemon(); // as after a crash
        QCOMPARE(TestEnv::filesIn(env->recordings()), QStringList{"2026-11-03_09-30-15.mp4"});
        QVERIFY(TestEnv::filesIn(env->keptRecordings()).isEmpty());
        QCOMPARE(env->calls("notify-send").size(), 0);
    }

    // ---- crash recovery ----

    void aKeptFileLeftByACrashIsSavedAtStartWithOneNotification() {
        QVERIFY(QDir().mkpath(env->keptRecordings()));
        QVERIFY(QFile::copy(kSample, env->keptRecordings() + "/2026-11-03_08-00-00.mp4"));
        QVERIFY(QFile::copy(kSample, env->keptRecordings() + "/2026-11-03_08-05-00.mp4"));
        env->restartDaemon();
        QCOMPARE(TestEnv::filesIn(env->recordings()), (QStringList{"2026-11-03_08-00-00.mp4", "2026-11-03_08-05-00.mp4"}));
        QCOMPARE(TestEnv::readFile(env->recordings() + "/2026-11-03_08-00-00.mp4"), sample());
        QVERIFY(TestEnv::filesIn(env->keptRecordings()).isEmpty());
        QCOMPARE(env->calls("notify-send").size(), 1); // one, however many
        QCOMPARE(stack().count(), 0);
    }

    void startingWithNothingLeftOverSaysNothing() {
        env->restartDaemon();
        QCOMPARE(env->calls("notify-send").size(), 0);
        QVERIFY(!QDir(env->recordings()).exists());
    }

    void aRecordingSavedAtRecoveryNeverOverwritesAnExistingFile() {
        QVERIFY(QDir().mkpath(env->keptRecordings()));
        QVERIFY(QDir().mkpath(env->recordings()));
        QVERIFY(QFile::copy(kSample, env->keptRecordings() + "/2026-11-03_08-00-00.mp4"));
        QFile existing(env->recordings() + "/2026-11-03_08-00-00.mp4");
        QVERIFY(existing.open(QIODevice::WriteOnly));
        existing.write("mine");
        existing.close();
        env->restartDaemon();
        QCOMPARE(TestEnv::readFile(env->recordings() + "/2026-11-03_08-00-00.mp4"), QByteArray("mine"));
        QCOMPARE(TestEnv::readFile(env->recordings() + "/2026-11-03_08-00-00_2.mp4"), sample());
    }


    // ---- GIF ----

    void altEnterRecordsAnMp4TheSameWay() {
        startGif();
        QCOMPARE(session().format(), RecordingSession::Gif);
        const auto calls = env->calls("gpu-screen-recorder");
        QCOMPARE(calls.size(), 1);
        QVERIFY(calls.first().contains("mp4"));
        QVERIFY(calls.first().last().endsWith(".mp4"));
        // Footage that only feeds a GIF is recorded at the top quality preset, an ordinary MP4 is not
        QCOMPARE(calls.first().at(calls.first().indexOf("-q") + 1), QString("ultra"));
        QVERIFY(env->calls("gifski").isEmpty()); // nothing is encoded while recording
        stopGif();
    }

    // The GIF is made at low CPU priority, so the desktop stays smooth meanwhile.
    void theGifEncodersRunUnderNice() {
        startGif();
        stopGif();
        bool gifski = false, ffmpeg = false;
        for (const QStringList &c : env->calls("nice")) {
            gifski |= c.mid(0, 3) == QStringList{"-n", "10", "gifski"};
            ffmpeg |= c.mid(0, 3) == QStringList{"-n", "10", "ffmpeg"};
        }
        QVERIFY2(gifski && ffmpeg, qPrintable(QString::number(env->calls("nice").size()) + " nice calls"));
    }

    void stopMakesTheGifWithGifskiAtQuality100AndTwentyFps() {
        startGif();
        stopGif();
        const auto calls = env->calls("gifski");
        QCOMPARE(calls.size(), 1);
        const QString kept = env->keptRecordings() + "/2026-11-03_09-30-15";
        // 40 px wide = the 20 logical px of the drag at scale 2: full size, not gifski's 800x600 default.
        QCOMPARE(calls.first(), (QStringList{"--fps", "20", "--width", "40", "--quality", "100", "-o", kept + ".gif.part", "-"}));
        // ffmpeg fed gifski a y4m stream of the kept MP4.
        QVERIFY(env->stdinOfCall("gifski", 1).startsWith("YUV4MPEG2"));
        QVERIFY(env->stdinOfCall("gifski", 1).size() > 1000);
        // The GIF sits beside the MP4, which is kept.
        QCOMPARE(TestEnv::filesIn(env->keptRecordings()), (QStringList{"2026-11-03_09-30-15.gif", "2026-11-03_09-30-15.mp4"}));
        QCOMPARE(TestEnv::readFile(kept + ".gif"), gifSample());
        QCOMPARE(TestEnv::readFile(kept + ".mp4"), sample());
    }

    void ffmpegDecodesTo20FpsY4m() {
        QCOMPARE(RecordingSession::ffmpegGifArguments("/k/a.mp4"),
                 (QStringList{"-v", "error", "-i", "/k/a.mp4", "-vf", "fps=20", "-f", "yuv4mpegpipe", "-"}));
    }

    void aWideGifIsScaledDownKeepingItsAspectAndANarrowOneIsNeverScaledUp() {
        // min(iw,1280): a wider source shrinks to 1280, a narrower one keeps its own width. -2 keeps the aspect, height even.
        QCOMPARE(RecordingSession::ffmpegGifArguments("/k/a.mp4", 1280),
                 (QStringList{"-v", "error", "-i", "/k/a.mp4", "-vf", "fps=20,scale='min(iw,1280)':-2", "-f", "yuv4mpegpipe", "-"}));
        QCOMPARE(RecordingSession::gifWidth(2560, 1280), 1280);
        QCOMPARE(RecordingSession::gifWidth(800, 1280), 800);
    }

    void aLimitOfZeroTurnsTheScalingOff() {
        QCOMPARE(RecordingSession::ffmpegGifArguments("/k/a.mp4", 0),
                 (QStringList{"-v", "error", "-i", "/k/a.mp4", "-vf", "fps=20", "-f", "yuv4mpegpipe", "-"}));
        QCOMPARE(RecordingSession::gifWidth(2560, 0), 2560);
    }

    void theGifWidthLimitIsASettingThatDefaultsTo1280AndIsInTheFirstRunFile() {
        auto &settings = env->daemon().service<tarn::Settings>();
        QCOMPARE(settings.gifMaxWidth(), 1280);
        const QString text = QString::fromUtf8(TestEnv::readFile(env->scratch() + "/config/tarn/config.toml"));
        QVERIFY(text.contains("\ngif_max_width = 1280"));
        QVERIFY(text.left(text.indexOf("\ngif_max_width = ")).trimmed().split('\n').last().startsWith('#'));
    }

    void aGifRecordedWiderThanTheLimitIsMadeAtTheLimitAndTheMp4StaysFull() {
        QDir().mkpath(env->scratch() + "/config/tarn");
        QFile config(env->scratch() + "/config/tarn/config.toml");
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write("gif_max_width = 30\n"); // the drag is 40 px wide
        config.close();
        env->restartDaemon();
        startGif();
        stopGif();
        QStringList ffmpeg, gifski;
        for (const auto &c : env->calls("nice"))
            if (c.mid(0, 3) == QStringList{"-n", "10", "ffmpeg"})
                ffmpeg = c;
        QVERIFY(ffmpeg.contains("fps=20,scale='min(iw,30)':-2"));
        gifski = env->calls("gifski").first();
        QCOMPARE(gifski.at(gifski.indexOf("--width") + 1), QString("30"));
        // The recorder was told the full Selection.
        const auto rec = env->calls("gpu-screen-recorder").first();
        QCOMPARE(rec.at(rec.indexOf("-region") + 1), QString("40x40+20+10"));
    }

    void theStateIsEncodingWhileTheGifIsMadeThenDone() {
        env->setFixture("gifski", "frames", "30");
        env->setFixture("gifski", "frame_delay", "0.02");
        startGif();
        QList<RecordingSession::State> seen;
        bool barUpWhileEncoding = false, activeWhileEncoding = false;
        double progressAtStart = -1;
        connect(&session(), &RecordingSession::stateChanged, this, [&](RecordingSession::State st) {
            seen << st;
            if (st == RecordingSession::Encoding) {
                barUpWhileEncoding = session().barVisible();
                activeWhileEncoding = session().active();
                progressAtStart = session().encodingProgress();
            }
        });
        stopGif();
        session().disconnect(this);
        QCOMPARE(seen, (QList<RecordingSession::State>{RecordingSession::Encoding, RecordingSession::Done}));
        QVERIFY(barUpWhileEncoding); // the bar stays, as a progress ring
        QVERIFY(activeWhileEncoding);
        QCOMPARE(progressAtStart, 0.0);
        QCOMPARE(session().encodingProgress(), 1.0);
        QVERIFY(!session().barVisible());
    }

    void progressComesFromGifskisOutputAndNeverGoesBack() {
        env->setFixture("gifski", "frames", "40");
        env->setFixture("gifski", "frame_delay", "0.01");
        startGif();
        QSignalSpy progress(&session(), &RecordingSession::encodingProgressChanged);
        stopGif();
        QVERIFY(progress.count() >= 3);
        double last = -1;
        for (const auto &args : progress) {
            QVERIFY(args.first().toDouble() >= last);
            last = args.first().toDouble();
        }
        QCOMPARE(progress.first().first().toDouble(), 0.0);
        QCOMPARE(progress.last().first().toDouble(), 1.0);
        // Only the final report is allowed to be 1.
        for (int i = 0; i < progress.count() - 1; ++i)
            QVERIFY(progress.at(i).first().toDouble() < 1.0);
    }

    void progressIsReportedAtMostTenTimesASecond() {
        env->setFixture("gifski", "frames", "100"); // 100 reports in a second or more
        env->setFixture("gifski", "frame_delay", "0.01");
        startGif();
        QSignalSpy progress(&session(), &RecordingSession::encodingProgressChanged);
        QElapsedTimer clock;
        QVERIFY(env->request({"capture", "record"}).ok);
        clock.start();
        QTRY_COMPARE_WITH_TIMEOUT(session().state(), RecordingSession::Done, kTryMs);
        const qint64 ms = clock.elapsed();
        // The first (0) and the last (1), plus at most one per 100 ms in between.
        QVERIFY2(progress.count() <= 2 + ms / 100 + 1, qPrintable(QString("%1 signals in %2 ms").arg(progress.count()).arg(ms)));
        QVERIFY(progress.count() >= 3);
        QVERIFY(progress.count() < 60); // 100 reports did not become 100 signals
    }

    void gifskisProgressTextIsParsed() {
        QCOMPARE(RecordingSession::parseGifskiProgress("\rFrame 3 / 12 ____ \r"), 0.25);
        QCOMPARE(RecordingSession::parseGifskiProgress("\rFrame 1 / 100 __\rFrame 50 / 100 ###\r159KB GIF; Frame 51 / 100 #"), 0.51);
        QCOMPARE(RecordingSession::parseGifskiProgress("Frame 1 / 12\n"), 1.0 / 12);
        QCOMPARE(RecordingSession::parseGifskiProgress("Frame 12 / 12 "), 1.0);
        QCOMPARE(RecordingSession::parseGifskiProgress("Frame 9 / 100"), -1.0); // "100" may not be complete yet
        QCOMPARE(RecordingSession::parseGifskiProgress("gifski created out.gif"), -1.0);
        QCOMPARE(RecordingSession::parseGifskiProgress(""), -1.0);
        QCOMPARE(RecordingSession::parseGifskiProgress("Frame 5 / 0 x"), -1.0);
    }

    void aRecordingThumbnailRemembersWhereItWasRecordedForAGifPin() {
        startGif();
        const QRect area = session().area();
        stopGif();
        const int id = stack().ids().last();
        QVERIFY(area.isValid());
        QCOMPARE(stack().origin(id).size(), area.size());
    }

    void theThumbnailIsAGifChipAndCopyAndSaveUseTheGif() {
        startGif();
        stopGif();
        const int id = stack().ids().last();
        QCOMPARE(stack().kind(id), ThumbnailStack::Recording);
        QCOMPARE(stack().chip(id), QString("GIF 0:06"));
        QVERIFY(stack().videoPath(id).endsWith("2026-11-03_09-30-15.gif"));

        QVERIFY(stack().copy(id));
        const auto calls = env->calls("tarn-clip");
        QCOMPARE(calls.size(), 1);
        QVERIFY(calls.first().last().endsWith(".gif"));
        QCOMPARE(TestEnv::readFile(calls.first().last()), gifSample());

        // Copy closed it; restore it and Save.
        QVERIFY(env->request({"restore"}).ok);
        const int back = stack().ids().last();
        const QString path = stack().save(back);
        QCOMPARE(path, env->recordings() + "/2026-11-03_09-30-15.gif");
        QCOMPARE(TestEnv::readFile(path), gifSample());
        QCOMPARE(TestEnv::filesIn(env->recordings()), QStringList{"2026-11-03_09-30-15.gif"}); // the MP4 is not saved too
    }

    void bothFilesGoWhenTheRecordingCanNoLongerBeRestored() {
        startGif();
        stopGif();
        stack().close(stack().ids().last());
        QCOMPARE(TestEnv::filesIn(env->keptRecordings()).size(), 2);
        closeAScreenshot();
        QVERIFY(TestEnv::filesIn(env->keptRecordings()).isEmpty());
    }

    void aMissingGifskiGivesANotificationAndNothingStarts() {
        // A PATH holding every stub except gifski.
        const QString dir = env->scratch() + "/no-gifski";
        QVERIFY(QDir().mkpath(dir));
        for (const QFileInfo &stub : QDir(QStringLiteral(TARN_STUB_DIR)).entryInfoList(QDir::Files))
            if (stub.fileName() != "gifski")
                QVERIFY(QFile::link(stub.absoluteFilePath(), dir + '/' + stub.fileName()));
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", (dir + ':' + env->scratch() + "/sysbin").toUtf8());

        QVERIFY(env->request({"capture", "record"}).ok);
        model().pointerPressed(10, 5, Qt::LeftButton);
        model().pointerMoved(30, 25);
        model().pointerReleased(30, 25, Qt::LeftButton);
        model().keyPressed(Enter, Qt::AltModifier);
        qputenv("PATH", oldPath);

        QCOMPARE(session().state(), RecordingSession::Idle);
        QVERIFY(env->calls("gpu-screen-recorder").isEmpty()); // no footage is recorded for a GIF that cannot be made
        const auto notes = env->calls("notify-send");
        QCOMPARE(notes.size(), 1);
        QVERIFY(notes.first().join(' ').contains("gifski"));
        QVERIFY(TestEnv::filesIn(env->keptRecordings()).isEmpty());
        QCOMPARE(stack().count(), 0);
    }

    void aGifskiThatFailsLeavesTheMp4WithANotificationNotASilentFallback() {
        startGif();
        env->setMode("gifski", "die");
        stopGif();
        const auto notes = env->calls("notify-send");
        QCOMPARE(notes.size(), 1);
        QVERIFY(notes.first().join(' ').contains("GIF"));
        QVERIFY(notes.first().join(' ').contains("stub told to die"));
        const int id = stack().ids().last();
        QCOMPARE(stack().chip(id), QString("MP4 0:06")); // the video, labeled honestly
        QCOMPARE(TestEnv::filesIn(env->keptRecordings()), QStringList{"2026-11-03_09-30-15.mp4"}); // no partial GIF
        QCOMPARE(session().format(), RecordingSession::Mp4);
    }

    void aCrashDuringEncodingSavesTheMp4NotAPartialGif() {
        QVERIFY(QDir().mkpath(env->keptRecordings()));
        QVERIFY(QFile::copy(kSample, env->keptRecordings() + "/2026-11-03_08-00-00.mp4"));
        QFile part(env->keptRecordings() + "/2026-11-03_08-00-00.gif.part");
        QVERIFY(part.open(QIODevice::WriteOnly));
        part.write("GIF89a half");
        part.close();
        env->restartDaemon();
        QCOMPARE(TestEnv::filesIn(env->recordings()), QStringList{"2026-11-03_08-00-00.mp4"});
        QVERIFY(TestEnv::filesIn(env->keptRecordings()).isEmpty());
    }

    void aFinishedGifLeftByACrashIsSavedInsteadOfItsMp4() {
        QVERIFY(QDir().mkpath(env->keptRecordings()));
        QVERIFY(QFile::copy(kSample, env->keptRecordings() + "/2026-11-03_08-00-00.mp4"));
        QVERIFY(QFile::copy(QStringLiteral(TARN_STUB_DIR "/fixtures/gifski/sample.gif"), env->keptRecordings() + "/2026-11-03_08-00-00.gif"));
        env->restartDaemon();
        QCOMPARE(TestEnv::filesIn(env->recordings()), QStringList{"2026-11-03_08-00-00.gif"});
        QVERIFY(TestEnv::filesIn(env->keptRecordings()).isEmpty());
    }

    // ---- where the Recording bar goes ----

    void theBarGoesBelowThenAboveThenRightThenLeftThenNowhere() {
        using tarn::placeRecordingBar;
        const QSize screen(1000, 600), bar(120, 44);
        // Below: centered on the area, 2 px outline + 12 px gap away.
        QCOMPARE(placeRecordingBar(QRect(400, 100, 200, 200), screen, bar), QRect(QPoint(440, 314), bar));
        // No room below: above.
        QCOMPARE(placeRecordingBar(QRect(400, 300, 200, 280), screen, bar), QRect(QPoint(440, 242), bar));
        // None above or below: right of it, vertically centered.
        QCOMPARE(placeRecordingBar(QRect(100, 0, 500, 600), screen, bar), QRect(QPoint(614, 278), bar));
        // ...else left.
        QCOMPARE(placeRecordingBar(QRect(400, 0, 600, 600), screen, bar), QRect(QPoint(266, 278), bar));
        // The whole screen: nowhere outside, so the bar goes inside.
        QVERIFY(placeRecordingBar(QRect(0, 0, 1000, 600), screen, bar).isNull());
        // Along the edge it stays on screen.
        QCOMPARE(placeRecordingBar(QRect(0, 100, 50, 100), screen, bar), QRect(QPoint(0, 214), bar));
    }

    // No room outside means inside, at the bottom center, 12 px above the edge, auto-hiding.
    void withNoRoomOutsideTheBarGoesInsideAtTheBottomCenterAndAutoHides() {
        using tarn::placeRecordingBarOrInside;
        const QSize screen(1000, 600), bar(120, 44);
        const QRect inside(QPoint(440, 544), bar);
        auto full = placeRecordingBarOrInside(QRect(0, 0, 1000, 600), screen, bar);
        QCOMPARE(full.rect, inside);
        QVERIFY(full.autoHide);
        // An area nearly filling the monitor: no room on any side.
        auto nearly = placeRecordingBarOrInside(QRect(10, 10, 980, 580), screen, bar);
        QCOMPARE(nearly.rect, inside);
        QVERIFY(nearly.autoHide);
        // With room outside nothing changes.
        auto below = placeRecordingBarOrInside(QRect(400, 100, 200, 200), screen, bar);
        QCOMPARE(below.rect, QRect(QPoint(440, 314), bar));
        QVERIFY(!below.autoHide);
        QVERIFY(placeRecordingBarOrInside(QRect(), screen, bar).rect.isNull());
    }
};

QTEST_MAIN(RecordingTest)
#include "recording_test.moc"
