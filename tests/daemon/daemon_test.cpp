// `tarn daemon` listens, `tarn capture fullscreen` sends one request and
// exits, and fails clearly when no daemon is running. These run the real
// binary (offscreen, in the scratch environment), so they cover the socket too.
#include "test_env.h"

#include <QProcess>
#include <QTest>

using tarn::test::TestEnv;

struct Run {
    int exitCode;
    QString err;
    QString out;
};

static Run runTarn(const QStringList &args) {
    QProcess p;
    p.start(TARN_BIN, args);
    if (!p.waitForFinished(20000)) {
        p.kill();
        return {-99, "timed out"};
    }
    return {p.exitCode(), QString::fromUtf8(p.readAllStandardError()), QString::fromUtf8(p.readAllStandardOutput())};
}

class DaemonTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;
    QProcess daemon;

    bool startDaemon() {
        daemon.setProcessChannelMode(QProcess::MergedChannels);
        daemon.start(TARN_BIN, {"daemon"});
        // Ready once the socket accepts a request: poll with a harmless unknown one.
        for (int i = 0; i < 100; ++i) {
            if (runTarn({"ping"}).exitCode == 1) // "unknown command" is a reply from a live daemon
                return true;
            QTest::qWait(50);
        }
        return false;
    }

private slots:
    void init() { env = std::make_unique<TestEnv>(); }
    void cleanup() {
        daemon.terminate();
        daemon.waitForFinished(3000);
        env.reset();
    }

    void clientFailsClearlyWhenNoDaemonIsRunning() {
        const Run r = runTarn({"capture", "fullscreen"});
        QCOMPARE(r.exitCode, 2);
        QVERIFY2(r.err.contains("no daemon is running"), qPrintable(r.err));
        QVERIFY(r.err.contains("tarn daemon"));
        QVERIFY(env->calls("grim").isEmpty());
    }

    void versionPrintsTheNumberWithNoDaemonRunning() {
        const Run r = runTarn({"--version"});
        QCOMPARE(r.exitCode, 0);
        QCOMPARE(r.out, QString("tarn 1.0.0\n"));
        QVERIFY2(r.err.isEmpty(), qPrintable(r.err));
    }

    void theUsageLineListsVersion() {
        const Run r = runTarn({});
        QCOMPARE(r.exitCode, 2);
        QVERIFY2(r.err.contains("tarn --version"), qPrintable(r.err));
    }

    void captureFullscreenReachesTheDaemonAndGrabsTheScreen() {
        QVERIFY2(startDaemon(), daemon.readAll().constData());
        const Run r = runTarn({"capture", "fullscreen"});
        QCOMPARE(r.exitCode, 0);
        QCOMPARE(env->calls("grim"), (QList<QStringList>{{"-o", "DP-1", "-t", "png", "-"}}));
    }

    void aFailedCaptureIsReportedByTheClient() {
        QVERIFY2(startDaemon(), daemon.readAll().constData());
        env->setMode("grim", "fail");
        const Run r = runTarn({"capture", "fullscreen"});
        QCOMPARE(r.exitCode, 1);
        QVERIFY2(r.err.contains("grim"), qPrintable(r.err));
    }

    void quitStopsTheDaemonAndFreesTheSocket() {
        QVERIFY2(startDaemon(), daemon.readAll().constData());
        QCOMPARE(runTarn({"quit"}).exitCode, 0);
        QVERIFY(daemon.waitForFinished(5000));
        QCOMPARE(runTarn({"capture", "fullscreen"}).exitCode, 2);
    }

    void aSecondDaemonRefusesToStart() {
        QVERIFY2(startDaemon(), daemon.readAll().constData());
        const Run r = runTarn({"daemon"});
        QCOMPARE(r.exitCode, 1);
        QVERIFY(r.err.contains("already running"));
    }
};

QTEST_GUILESS_MAIN(DaemonTest)
#include "daemon_test.moc"
