// Super+Print takes a full-screen shot of the monitor under the pointer,
// it becomes a Thumbnail, and Save writes it to the screenshot folder.
#include "test_env.h"

#include "process/process_runner.h"
#include "storage/storage.h"
#include "thumbnails/thumbnail_stack.h"

#include <QFile>
#include <QTest>

using tarn::Reply;
using tarn::ThumbnailStack;
using tarn::test::TestEnv;

static const QByteArray kTwoMonitors =
    R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":2560,"height":1440,"scale":1.0,"transform":0},)"
    R"({"id":1,"name":"HDMI-A-1","x":2560,"y":0,"width":1920,"height":1080,"scale":1.5,"transform":0}])";

class FullscreenTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    ThumbnailStack &stack() { return env->daemon().service<ThumbnailStack>(); }
    int captureOne() {
        const Reply r = env->request({"capture", "fullscreen"});
        return r.ok && stack().count() > 0 ? stack().ids().last() : -1;
    }

private slots:
    void init() { env = std::make_unique<TestEnv>(); }
    void cleanup() { env.reset(); }

    void grabsOnlyTheMonitorUnderThePointer() {
        env->setFixture("hyprctl", "monitors.json", kTwoMonitors);
        env->setPointer(3000, 400); // inside HDMI-A-1 (2560..3200 logical after 1.5 scale)
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        QCOMPARE(env->calls("grim"), (QList<QStringList>{{"-o", "HDMI-A-1", "-t", "ppm", "-"}}));

        env->setPointer(10, 10);
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        QCOMPARE(env->calls("grim").last(), (QStringList{"-o", "DP-1", "-t", "ppm", "-"}));
    }

    // The layer surface has one output at a time: it must follow the newest Thumbnail, or a
    // capture on a second monitor would show its card on the first.
    void theThumbnailsShowOnTheMonitorOfTheNewestCapture() {
        env->setFixture("hyprctl", "monitors.json", kTwoMonitors);
        env->setPointer(3000, 400);
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        QCOMPARE(stack().monitorName(), QStringLiteral("HDMI-A-1"));

        env->setPointer(10, 10);
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        QCOMPARE(stack().monitorName(), QStringLiteral("DP-1"));
    }

    void aPointerOffEveryMonitorFailsWithoutGrabbing() {
        env->setPointer(99999, 99999);
        const Reply r = env->request({"capture", "fullscreen"});
        QVERIFY(!r.ok);
        QVERIFY2(r.error.contains("monitor"), qPrintable(r.error));
        QVERIFY(env->calls("grim").isEmpty());
        QCOMPARE(env->calls("notify-send").size(), 1);
    }

    void aCaptureBecomesAThumbnailAndSavesNothingByItself() {
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        QCOMPARE(stack().count(), 1);
        QVERIFY(!QFile::exists(env->screenshots()));
        QVERIFY(env->calls("wl-copy").isEmpty()); // nothing copied either
    }

    void saveWritesGrimsPngNamedByCaptureTime() {
        env->setNow(QDateTime(QDate(2026, 11, 3), QTime(9, 30, 15)));
        const int id = captureOne();
        QVERIFY(id > 0);
        env->setNow(QDateTime(QDate(2026, 11, 3), QTime(9, 45, 0))); // saved later: name stays the capture time

        const QString path = stack().save(id);
        QCOMPARE(path, env->screenshots() + "/2026-11-03_09-30-15.png");
        QCOMPARE(TestEnv::pixelsOf(TestEnv::readFile(path)), TestEnv::defaultGrimImage()); // pixel-identical
        QCOMPARE(stack().count(), 0); // a saved Thumbnail goes away
    }

    // The PNG is made in the background; anything pressed before it exists waits for it.
    void copySaveAndPinStraightAfterACaptureHaveGrimsPixels() {
        QVERIFY(stack().copy(captureOne()));
        QCOMPARE(TestEnv::pixelsOf(env->stdinOfCall("tarn-clip", 1).isEmpty()
                                       ? TestEnv::readFile(env->calls("tarn-clip").first().first())
                                       : env->stdinOfCall("tarn-clip", 1)),
                 TestEnv::defaultGrimImage());
        QCOMPARE(TestEnv::pixelsOf(TestEnv::readFile(stack().save(captureOne()))), TestEnv::defaultGrimImage());
        const ThumbnailStack::Capture pinned = stack().pin(captureOne());
        QCOMPARE(TestEnv::pixelsOf(pinned.png), TestEnv::defaultGrimImage());
    }

    void saveCreatesAMissingFolder() {
        QVERIFY(!QFile::exists(env->pictures()));
        QVERIFY(!stack().save(captureOne()).isEmpty());
        QVERIFY(QFileInfo(env->screenshots()).isDir());
    }

    void aClashAddsNumbersAndNeverOverwrites() {
        QDir().mkpath(env->screenshots());
        const QString taken = env->screenshots() + "/2026-11-03_09-30-15.png";
        QFile f(taken);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("somebody else's file");
        f.close();

        const int a = captureOne(), b = captureOne(), c = captureOne(); // all in the same second
        QCOMPARE(stack().save(a), env->screenshots() + "/2026-11-03_09-30-15_2.png");
        QCOMPARE(stack().save(b), env->screenshots() + "/2026-11-03_09-30-15_3.png");
        QCOMPARE(stack().save(c), env->screenshots() + "/2026-11-03_09-30-15_4.png");
        QCOMPARE(TestEnv::readFile(taken), QByteArray("somebody else's file"));
        QCOMPARE(TestEnv::filesIn(env->screenshots()).size(), 4);
    }

    void aFailingGrimGivesAnErrorAndANotificationButNoThumbnail() {
        env->setMode("grim", "fail");
        const Reply r = env->request({"capture", "fullscreen"});
        QVERIFY(!r.ok);
        QVERIFY2(r.error.contains("grim"), qPrintable(r.error));
        QCOMPARE(stack().count(), 0);
        const auto notes = env->calls("notify-send");
        QCOMPARE(notes.size(), 1);
        QVERIFY(notes[0].join(' ').contains("could not take the screenshot"));
    }

    void aFailedSaveKeepsTheThumbnailAndNotifies() {
        const int id = captureOne();
        QDir().mkpath(env->pictures());
        QFile blocker(env->screenshots()); // a file where the folder should be
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();

        QVERIFY(stack().save(id).isEmpty());
        QCOMPARE(stack().count(), 1);
        QCOMPARE(env->calls("notify-send").size(), 1);
    }

    void unknownCommandsAreRefused() {
        const Reply r = env->request({"capture", "nonsense"});
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("unknown command"));
    }

    void aProgramWithoutAStubIsNotFound() {
        // The harness test: slurp is installed on the real machine but has no stub.
        auto &runner = env->daemon().service<tarn::ProcessRunner>();
        QVERIFY(!runner.run("slurp", {}).started);
        // wl-copy has one: it logs its arguments and keeps its input.
        QVERIFY(runner.run("wl-copy", {"--type", "image/png"}, "pixels").succeeded());
        QCOMPARE(env->calls("wl-copy"), (QList<QStringList>{{"--type", "image/png"}}));
        QCOMPARE(env->stdinOfCall("wl-copy", 1), QByteArray("pixels"));
    }
};

QTEST_GUILESS_MAIN(FullscreenTest)
#include "fullscreen_test.moc"
