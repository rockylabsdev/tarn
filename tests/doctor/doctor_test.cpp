// `tarn doctor` runs in the client, with no daemon needed, and reports one line per
// check (✓ / ! / ✗). These run the real binary against the stub programs, so they
// cover the whole path: what it asks of each tool, what it prints, the exit code.
#include "test_env.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QTest>

using tarn::test::TestEnv;

struct Run {
    int exitCode;
    QString err;
    QString out;
    qint64 ms;
};

class DoctorTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;
    QProcess daemon;
    QString path; // empty: the harness's PATH (every stub present)

    // The binary with `path` as its PATH, so a tool can be absent.
    Run runTarn(const QStringList &args) {
        QProcess p;
        QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
        if (!path.isEmpty())
            e.insert("PATH", path);
        p.setProcessEnvironment(e);
        QElapsedTimer clock;
        clock.start();
        p.start(TARN_BIN, args);
        if (!p.waitForFinished(30000)) {
            p.kill();
            return {-99, "timed out", {}, clock.elapsed()};
        }
        return {p.exitCode(), QString::fromUtf8(p.readAllStandardError()), QString::fromUtf8(p.readAllStandardOutput()), clock.elapsed()};
    }

    bool startDaemon() {
        daemon.setProcessChannelMode(QProcess::MergedChannels);
        daemon.start(TARN_BIN, {"daemon"});
        for (int i = 0; i < 100; ++i) {
            if (runTarn({"ping"}).exitCode == 1) // "unknown command" is a reply from a live daemon
                return true;
            QTest::qWait(50);
        }
        return false;
    }

    // A PATH holding every stub and safe tool except `missing`: each stub is a one-line
    // wrapper (a stub finds its fixtures from its own path, so it cannot be a symlink).
    void withoutPrograms(const QStringList &missing) {
        const QString dir = env->scratch() + "/doctorbin";
        QVERIFY(QDir().mkpath(dir));
        for (const QString &name : QDir(TARN_STUB_DIR).entryList(QDir::Files)) {
            if (missing.contains(name) || name.startsWith('_'))
                continue;
            QFile wrapper(dir + '/' + name);
            QVERIFY(wrapper.open(QIODevice::WriteOnly));
            wrapper.write(QStringLiteral("#!/bin/sh\nexec %1/%2 \"$@\"\n").arg(TARN_STUB_DIR, name).toUtf8());
            wrapper.close();
            QVERIFY(wrapper.setPermissions(QFile::permissions(wrapper.fileName()) | QFile::ExeOwner));
        }
        const QString sysbin = env->scratch() + "/sysbin";
        for (const QString &name : QDir(sysbin).entryList(QDir::Files | QDir::System)) {
            if (!missing.contains(name) && !QFile::exists(dir + '/' + name))
                QVERIFY(QFile::link(QFile::symLinkTarget(sysbin + '/' + name), dir + '/' + name));
        }
        path = dir;
    }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        path.clear();
    }
    void cleanup() {
        daemon.terminate();
        daemon.waitForFinished(3000);
        env.reset();
    }

    void everythingPresentIsAllGoodAndExitsZero() {
        QVERIFY2(startDaemon(), daemon.readAll().constData());
        const Run r = runTarn({"doctor"});
        QVERIFY2(r.exitCode == 0, qPrintable(r.out + r.err));
        QVERIFY2(!r.out.contains(QStringLiteral("✗")), qPrintable(r.out));
        QVERIFY2(!hasLine(r, "! "), qPrintable(r.out));
        for (const char *line : {"Hyprland", "grim", "gpu-screen-recorder", "Tarn's file", "daemon", "gifski", "ffmpeg", "tesseract", "ZXingReader", "encoder"})
            QVERIFY2(hasLine(r, QStringLiteral("✓ ") + line), line);
    }

    void eachRequiredToolMissingFailsWithItsPacmanLine_data() {
        QTest::addColumn<QString>("program");
        QTest::addColumn<QString>("package");
        QTest::newRow("grim") << "grim" << "grim";
        QTest::newRow("gpu-screen-recorder") << "gpu-screen-recorder" << "gpu-screen-recorder";
    }
    void eachRequiredToolMissingFailsWithItsPacmanLine() {
        QFETCH(QString, program);
        QFETCH(QString, package);
        QVERIFY2(startDaemon(), daemon.readAll().constData());
        withoutPrograms({program});
        const Run r = runTarn({"doctor"});
        QCOMPARE(r.exitCode, 1);
        QVERIFY2(hasLine(r, "✗ " + program), qPrintable(r.out));
        QVERIFY2(r.out.contains("pacman -S " + package), qPrintable(r.out));
    }

    void aDaemonThatIsNotRunningFailsAndTheRestStillReports() {
        const Run r = runTarn({"doctor"});
        QCOMPARE(r.exitCode, 1);
        QVERIFY2(hasLine(r, "✗ daemon"), qPrintable(r.out));
        QVERIFY2(r.out.contains("tarn daemon"), qPrintable(r.out));
        QVERIFY2(hasLine(r, "✓ grim"), qPrintable(r.out));
    }

    void aTarnFileThatIsNotLoadedFails() {
        QVERIFY2(startDaemon(), daemon.readAll().constData());
        env->setFixture("hyprctl", "binds.json", bindsJson("F9", ""));
        const Run r = runTarn({"doctor"});
        QCOMPARE(r.exitCode, 1);
        QVERIFY2(hasLine(r, "✗ Tarn's file"), qPrintable(r.out));
    }

    // Lua binds report `arg` as an opaque number, so the file is recognized by its
    // descriptions, which stay when a user moves a bind to another key.
    void aTarnFileWithAChangedKeyIsStillLoaded() {
        QVERIFY2(startDaemon(), daemon.readAll().constData());
        env->setFixture("hyprctl", "binds.json", bindsJson("F5", "Tarn: Text capture"));
        const Run r = runTarn({"doctor"});
        QCOMPARE(r.exitCode, 0);
        QVERIFY2(hasLine(r, "✓ Tarn's file"), qPrintable(r.out));
    }

    void noHyprlandGivesOneMessageNotARunOfFailures() {
        QVERIFY2(startDaemon(), daemon.readAll().constData());
        env->setMode("hyprctl", "fail"); // as hyprctl does with no session
        const Run r = runTarn({"doctor"});
        QCOMPARE(r.exitCode, 1);
        QVERIFY2(hasLine(r, "✗ Hyprland"), qPrintable(r.out));
        QVERIFY2(r.out.count(QStringLiteral("✗")) == 1, qPrintable(r.out));
        QVERIFY2(r.out.contains("Skipped"), qPrintable(r.out));
        QVERIFY2(!r.out.contains("Tarn's file:"), qPrintable(r.out));
        QVERIFY2(hasLine(r, "✓ grim"), qPrintable(r.out)); // the checks that don't need it still run
    }

    void hyprctlMissingSaysSoAndExitsNonZero() {
        QVERIFY2(startDaemon(), daemon.readAll().constData());
        withoutPrograms({"hyprctl"});
        const Run r = runTarn({"doctor"});
        QCOMPARE(r.exitCode, 1);
        QVERIFY2(r.out.contains("pacman -S hyprland"), qPrintable(r.out));
        QVERIFY2(r.out.count(QStringLiteral("✗")) == 1, qPrintable(r.out));
    }

    void eachOptionalToolMissingIsANoteThatNamesWhatItUnlocks_data() {
        QTest::addColumn<QString>("program");
        QTest::addColumn<QString>("package");
        QTest::addColumn<QString>("unlocks");
        QTest::newRow("gifski") << "gifski" << "gifski" << "GIF";
        QTest::newRow("ffmpeg") << "ffmpeg" << "ffmpeg" << "Trim";
        QTest::newRow("tesseract") << "tesseract" << "tesseract" << "Text capture";
        QTest::newRow("ZXingReader") << "ZXingReader" << "zxing-cpp" << "QR";
    }
    void eachOptionalToolMissingIsANoteThatNamesWhatItUnlocks() {
        QFETCH(QString, program);
        QFETCH(QString, package);
        QFETCH(QString, unlocks);
        QVERIFY2(startDaemon(), daemon.readAll().constData());
        withoutPrograms({program});
        const Run r = runTarn({"doctor"});
        QCOMPARE(r.exitCode, 0);
        QVERIFY2(hasLine(r, "! " + program), qPrintable(r.out));
        QVERIFY2(r.out.contains("pacman -S " + package) || r.out.contains("pacman -S tesseract"), qPrintable(r.out));
        QVERIFY2(lineStarting(r, "! " + program).contains(unlocks), qPrintable(r.out));
    }

    void tesseractShowsItsInstalledLanguages() {
        QVERIFY2(startDaemon(), daemon.readAll().constData());
        env->setFixture("tesseract", "langs.txt", "List of available languages in \"/usr/share/tessdata/\" (3):\neng\nfra\nosd\n");
        const Run r = runTarn({"doctor"});
        QVERIFY2(lineStarting(r, "✓ tesseract").contains("eng, fra"), qPrintable(r.out));
        QVERIFY2(!lineStarting(r, "✓ tesseract").contains("osd"), qPrintable(r.out));
    }

    void tesseractWithNoLanguageDataIsANoteWithItsPacmanLine() {
        QVERIFY2(startDaemon(), daemon.readAll().constData());
        env->setFixture("tesseract", "langs.txt", "List of available languages in \"/usr/share/tessdata/\" (1):\nosd\n");
        const Run r = runTarn({"doctor"});
        QCOMPARE(r.exitCode, 0);
        QVERIFY2(lineStarting(r, "! tesseract").contains("pacman -S tesseract-data-eng"), qPrintable(r.out));
    }

    void aSoftwarePointerIsInformationOnly() {
        QVERIFY2(startDaemon(), daemon.readAll().constData());
        env->setFixture("hyprctl", "no_hardware_cursors.json", "{\"option\": \"cursor:no_hardware_cursors\", \"int\": 2, \"set\": true}");
        const Run r = runTarn({"doctor"});
        QCOMPARE(r.exitCode, 0);
        QVERIFY2(lineStarting(r, "! cursor:no_hardware_cursors").contains("information only"), qPrintable(r.out));
    }

    void theEncoderIsHardwareWhenTheRecorderListsH264() {
        QVERIFY2(startDaemon(), daemon.readAll().constData());
        const Run r = runTarn({"doctor"});
        QVERIFY2(lineStarting(r, "✓ encoder").contains("hardware h264"), qPrintable(r.out));
    }

    void theEncoderFallsBackToTheCpuWhenOnlySoftwareH264IsListed() {
        QVERIFY2(startDaemon(), daemon.readAll().constData());
        env->setMode("gpu-screen-recorder", "cpu");
        const Run r = runTarn({"doctor"});
        QCOMPARE(r.exitCode, 0);
        QVERIFY2(lineStarting(r, "! encoder").contains("CPU"), qPrintable(r.out));
    }

    // On a real session the portal step after the list hangs for about 25 s, and the list
    // never leaves the recorder's stdio buffer on a pipe: doctor must read it line buffered
    // and stop there.
    void theEncoderIsReadBeforeAPortalThatNeverAnswers() {
        QVERIFY2(startDaemon(), daemon.readAll().constData());
        env->setMode("gpu-screen-recorder", "portal");
        const Run r = runTarn({"doctor"});
        QCOMPARE(r.exitCode, 0);
        QVERIFY2(lineStarting(r, "✓ encoder").contains("hardware h264"), qPrintable(r.out));
        QVERIFY2(r.ms < 2500, qPrintable(QString::number(r.ms)));
    }

    void aHangingProbeIsKilledAndReportedAsCouldntTell() {
        QVERIFY2(startDaemon(), daemon.readAll().constData());
        env->setMode("gpu-screen-recorder", "hang");
        const Run r = runTarn({"doctor"});
        QCOMPARE(r.exitCode, 0);
        QVERIFY2(lineStarting(r, "! encoder").contains("couldn't tell"), qPrintable(r.out));
        QVERIFY2(r.ms < 8000, qPrintable(QString::number(r.ms)));
    }

    // Each call has its own timeout, but the whole run shares one budget: with every
    // call after the first hanging, doctor still ends within a few seconds and the
    // checks that never got their turn say they couldn't tell.
    void everyToolHangingStillEndsWithinTheTotalBudget() {
        QVERIFY2(startDaemon(), daemon.readAll().constData());
        env->setMode("hyprctl", "hang_queries");
        env->setMode("tesseract", "hang");
        env->setMode("gpu-screen-recorder", "hang");
        const Run r = runTarn({"doctor"});
        QVERIFY2(r.ms < 6500, qPrintable(QString::number(r.ms) + "\n" + r.out));
        QVERIFY2(r.exitCode >= 0, qPrintable(r.out + r.err));
        QVERIFY2(lineStarting(r, "! tesseract").contains("couldn't"), qPrintable(r.out));
        QVERIFY2(lineStarting(r, "! cursor:no_hardware_cursors").contains("couldn't"), qPrintable(r.out));
        QVERIFY2(lineStarting(r, "! encoder").contains("couldn't tell"), qPrintable(r.out));
    }

    // A daemon that sends bytes but never a newline must not hold doctor up for ever.
    void aDaemonThatTricklesWithoutANewlineIsNotAnswering() {
        QProcess standIn;
        standIn.start("/usr/bin/python3", {"-I", TARN_TRICKLE_DAEMON, qEnvironmentVariable("XDG_RUNTIME_DIR") + "/tarn/tarn.sock"});
        QVERIFY(standIn.waitForStarted(3000));
        for (int i = 0; i < 100 && !QFile::exists(qEnvironmentVariable("XDG_RUNTIME_DIR") + "/tarn/tarn.sock"); ++i)
            QTest::qWait(50);
        const Run r = runTarn({"doctor"});
        standIn.kill();
        standIn.waitForFinished(3000);
        QVERIFY2(r.ms < 6500, qPrintable(QString::number(r.ms)));
        QVERIFY2(hasLine(r, "✗ daemon"), qPrintable(r.out));
        QCOMPARE(r.exitCode, 1);
    }

    void theUsageLineListsDoctor() {
        const Run r = runTarn({});
        QCOMPARE(r.exitCode, 2);
        QVERIFY2(r.err.contains("tarn doctor"), qPrintable(r.err));
    }

    void theNoDaemonErrorKeepsItsWordingAndPointsAtDoctor() {
        const Run r = runTarn({"capture", "fullscreen"});
        QCOMPARE(r.exitCode, 2);
        QVERIFY(r.err.contains("no daemon is running. Start it with `tarn daemon`"));
        QVERIFY2(r.err.contains("run `tarn doctor` for a full check", Qt::CaseInsensitive), qPrintable(r.err));
    }

    void aMissingToolErrorKeepsItsWordingAndPointsAtDoctor() {
        withoutPrograms({"grim"});
        qputenv("PATH", path.toUtf8()); // the daemon, started next, sees the same PATH (TestEnv restores it)
        QVERIFY2(startDaemon(), daemon.readAll().constData());
        const Run r = runTarn({"capture", "fullscreen"});
        QCOMPARE(r.exitCode, 1);
        QVERIFY2(r.err.contains("grim could not be started (is it installed?)"), qPrintable(r.err));
        QVERIFY2(r.err.contains("run `tarn doctor` for a full check", Qt::CaseInsensitive), qPrintable(r.err));
    }

private:
    static bool hasLine(const Run &r, const QString &start) { return !lineStarting(r, start).isEmpty(); }
    static QString lineStarting(const Run &r, const QString &start) {
        for (const QString &line : r.out.split('\n'))
            if (line.startsWith(start))
                return line;
        return {};
    }
    static QByteArray bindsJson(const QString &key, const QString &description) {
        return QStringLiteral("[{\"key\": \"%1\", \"modmask\": 64, \"description\": \"%2\", \"has_description\": %3, "
                              "\"dispatcher\": \"HL.Dispatcher(exec_cmd)\", \"arg\": \"6\"}]")
            .arg(key, description, description.isEmpty() ? "false" : "true").toUtf8();
    }
};

QTEST_GUILESS_MAIN(DoctorTest)
#include "doctor_test.moc"
