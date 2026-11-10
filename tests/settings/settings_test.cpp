// The settings file (config.toml): written once, applied live, one bad
// line, `tarn settings`, auto-close, auto Copy/Save, and the rule that a
// shortened retention sends newly expired History entries to the Trash.
#include "test_env.h"

#include "history/history.h"
#include "settings/settings.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QDir>
#include <QImage>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTest>

using tarn::History;
using tarn::Settings;
using tarn::ThumbnailStack;
using tarn::test::TestEnv;

class SettingsTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    QString configFile() const { return env->scratch() + "/config/tarn/config.toml"; }
    QString stateFile() const { return env->scratch() + "/state/tarn/state.toml"; }
    Settings &settings() { return env->daemon().service<Settings>(); }
    ThumbnailStack &stack() { return env->daemon().service<ThumbnailStack>(); }
    History &history() { return env->daemon().service<History>(); }
    QString historyDir() const { return env->scratch() + "/data/tarn/history"; }
    QString trashFiles() const { return env->scratch() + "/data/Trash/files"; }

    // Saves the file the way a simple editor does (same file, new content).
    void writeConfig(const QByteArray &text) {
        QFile f(configFile());
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(text);
    }
    // Appends one line, like the user editing the commented file.
    // Changes one line's value, like the user editing the commented file.
    void setLine(const QByteArray &key, const QByteArray &value) {
        QList<QByteArray> lines = TestEnv::readFile(configFile()).split('\n');
        for (QByteArray &line : lines)
            if (line.startsWith(key + " = "))
                line = key + " = " + value;
        writeConfig(lines.join('\n'));
    }
    int capture() {
        if (!env->request({"capture", "fullscreen"}).ok)
            return -1;
        return stack().ids().last();
    }
    void plant(const QString &id, const QDateTime &when) {
        QDir().mkpath(historyDir());
        QFile png(historyDir() + "/" + id + ".png");
        QVERIFY(png.open(QIODevice::WriteOnly));
        png.write("x");
        QFile json(historyDir() + "/" + id + ".json");
        QVERIFY(json.open(QIODevice::WriteOnly));
        json.write(QJsonDocument(QJsonObject{{"captureTime", when.toString(Qt::ISODate)}, {"kind", "area"}}).toJson());
    }

private slots:
    void init() { env = std::make_unique<TestEnv>(); }
    void cleanup() { env.reset(); }

    // ---- the file ----

    void aMissingFileIsWrittenWithEverySettingItsDefaultAndAComment() {
        const QString text = QString::fromUtf8(TestEnv::readFile(configFile()));
        for (const char *key : {"screenshot_folder", "recordings_folder", "keep_history_for", "copy_automatically",
                                "save_automatically", "close_thumbnail_after"}) {
            const int at = text.indexOf(QString("\n%1 = ").arg(key));
            QVERIFY2(at > 0, key);
            QVERIFY2(text.left(at).trimmed().split('\n').last().startsWith('#'), key); // a comment right above it
        }
        QVERIFY(text.contains("screenshot_folder = \"~/Pictures/Screenshots\""));
        QVERIFY(text.contains("recordings_folder = \"~/Videos/Tarn\""));
        QVERIFY(text.contains("keep_history_for = \"1 month\""));
        QVERIFY(text.contains("copy_automatically = false"));
        QVERIFY(text.contains("close_thumbnail_after = 0"));
        // The defaults it names are the defaults in force.
        QCOMPARE(settings().screenshotFolder(), env->screenshots());
        QCOMPARE(settings().recordingsFolder(), env->home() + "/Videos/Tarn");
        QCOMPARE(settings().keepHistoryFor(), QString("1 month"));
        QVERIFY(!settings().copyAutomatically() && !settings().saveAutomatically());
        QCOMPARE(settings().closeThumbnailAfterSeconds(), 0.0);
        QVERIFY(settings().showPointerInRecordings());
        QVERIFY(env->calls("notify-send").isEmpty()); // the shipped file has no mistakes
    }

    void anExistingFileIsNeverRewritten() {
        const QByteArray mine = "# my own notes\nsave_automatically = true\n";
        writeConfig(mine);
        env->restartDaemon();
        QCOMPARE(TestEnv::readFile(configFile()), mine);
        QVERIFY(settings().saveAutomatically());
        QVERIFY(!settings().copyAutomatically()); // lines I left out are defaults, and still not added to the file
        QCOMPARE(TestEnv::readFile(configFile()), mine);
        QVERIFY(env->calls("notify-send").isEmpty());
    }

    void theStateFileAndTheSettingsFileAreSeparate() {
        QVERIFY(capture() > 0);
        QVERIFY(!TestEnv::readFile(configFile()).contains("last_area"));
        QVERIFY(!TestEnv::readFile(stateFile()).contains("copy_automatically"));
    }

    // ---- live reload ----

    void aSavedEditChangesBehaviorWithoutARestart() {
        QVERIFY(!settings().saveAutomatically());
        setLine("save_automatically", "true");
        QTRY_VERIFY(settings().saveAutomatically());
        setLine("screenshot_folder", "\"~/Elsewhere/Shots\"");
        QTRY_COMPARE(settings().screenshotFolder(), env->home() + "/Elsewhere/Shots");
        // ...and the new folder is what Save really uses.
        const int id = capture();
        QVERIFY(id > 0);
        QTRY_COMPARE(TestEnv::filesIn(env->home() + "/Elsewhere/Shots").size(), 1); // once the PNG is made
        QVERIFY(TestEnv::filesIn(env->screenshots()).isEmpty());
    }

    void anEditorThatReplacesTheFileIsStillWatched() {
        for (int i = 1; i <= 2; ++i) {
            QSaveFile f(configFile()); // write a new file and rename it over the old one
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(i == 1 ? "copy_automatically = true\n" : "copy_automatically = false\n");
            QVERIFY(f.commit());
            if (i == 1)
                QTRY_VERIFY(settings().copyAutomatically());
            else
                QTRY_VERIFY(!settings().copyAutomatically());
        }
    }

    // ---- one bad line ----

    void oneBadLineNotifiesOnceNamingItAndTheRestStillApplies() {
        writeConfig("save_automatically = true\n"
                    "\n"
                    "copy_automatically = \"yes please\"\n"
                    "keep_history_for = \"3 weeks\"\n");
        QTRY_VERIFY(settings().saveAutomatically());
        QCOMPARE(settings().keepHistoryFor(), QString("3 weeks"));
        QVERIFY(!settings().copyAutomatically()); // the bad one stays as it was
        QCOMPARE(env->calls("notify-send").size(), 1);
        const QString said = env->calls("notify-send").first().join(' ');
        QVERIFY2(said.contains("line 3"), qPrintable(said));
        QVERIFY2(said.contains("copy_automatically"), qPrintable(said));
        // Saving the same file again does not repeat the notification.
        writeConfig(TestEnv::readFile(configFile()));
        QTest::qWait(250);
        QCOMPARE(env->calls("notify-send").size(), 1);
    }

    void aBadValueKeepsTheLastGoodValueNotTheDefault() {
        writeConfig("close_thumbnail_after = 7\n");
        QTRY_COMPARE(settings().closeThumbnailAfterSeconds(), 7.0);
        writeConfig("close_thumbnail_after = \"soon\"\n");
        QTRY_COMPARE(env->calls("notify-send").size(), 1);
        QCOMPARE(settings().closeThumbnailAfterSeconds(), 7.0);
        QVERIFY(env->calls("notify-send").first().join(' ').contains("line 1"));
        // Fixing the line applies it again.
        writeConfig("close_thumbnail_after = 9\n");
        QTRY_COMPARE(settings().closeThumbnailAfterSeconds(), 9.0);
        QCOMPARE(env->calls("notify-send").size(), 1);
    }

    void theSecondsHintSaysWhatZeroMeansForThatSetting() {
        writeConfig("close_thumbnail_after = \"soon\"\n");
        QTRY_COMPARE(env->calls("notify-send").size(), 1);
        QVERIFY(env->calls("notify-send").last().join(' ').contains("0 means never"));
        writeConfig("timer_delay = \"soon\"\n");
        QTRY_COMPARE(env->calls("notify-send").size(), 2);
        const QString timer = env->calls("notify-send").last().join(' ');
        QVERIFY(timer.contains("0 means at once"));
        QVERIFY(!timer.contains("never"));
    }

    void aBadGifMaxWidthIsReportedAndTheLastGoodOneStays() {
        writeConfig("gif_max_width = 800\n");
        QTRY_COMPARE(settings().gifMaxWidth(), 800);
        for (const char *bad : {"gif_max_width = \"wide\"\n", "gif_max_width = -1\n", "gif_max_width = 640.5\n"}) {
            const qsizetype before = env->calls("notify-send").size();
            writeConfig(bad);
            QTRY_COMPARE(env->calls("notify-send").size(), before + 1);
            QVERIFY(env->calls("notify-send").last().join(' ').contains("0 means no limit"));
            QCOMPARE(settings().gifMaxWidth(), 800);
        }
        writeConfig("gif_max_width = 0\n");
        QTRY_COMPARE(settings().gifMaxWidth(), 0);
    }

    void aSyntaxErrorOnOneLineAlsoLeavesTheOthersWorking() {
        writeConfig("copy_automatically = true\n"
                    "save_automatically = tru e\n"
                    "keep_history_for = \"2 weeks\"\n");
        QTRY_VERIFY(settings().copyAutomatically());
        QCOMPARE(settings().keepHistoryFor(), QString("2 weeks"));
        QVERIFY(!settings().saveAutomatically());
        QCOMPARE(env->calls("notify-send").size(), 1);
        QVERIFY(env->calls("notify-send").first().join(' ').contains("line 2"));
    }

    void anUnknownSettingNamesItsLine() {
        writeConfig("save_automaticaly = true\n");
        QTRY_COMPARE(env->calls("notify-send").size(), 1);
        QVERIFY(env->calls("notify-send").first().join(' ').contains("line 1"));
    }

    // ---- tarn settings ----

    void tarnSettingsOpensTheFileWithTheDefaultOpener() {
        QVERIFY(env->request({"settings"}).ok);
        QTRY_COMPARE(env->calls("xdg-open"), (QList<QStringList>{{configFile()}}));
    }

    // ---- auto-close, auto Copy / Save ----

    void autoCloseClosesTheThumbnailWithoutSaving() {
        writeConfig("close_thumbnail_after = 0.15\n");
        QTRY_COMPARE(settings().closeThumbnailAfterSeconds(), 0.15);
        QVERIFY(capture() > 0);
        QCOMPARE(stack().count(), 1);
        QTRY_COMPARE(stack().count(), 0);
        QVERIFY(TestEnv::filesIn(env->screenshots()).isEmpty()); // like the x button: nothing saved
        QVERIFY(env->calls("tarn-clip").isEmpty());
        QVERIFY(stack().canRestore()); // and it is the last closed one, so `tarn restore` brings it back
    }

    void autoCloseDelayStartsAgainWhenTheThumbnailIsRevealedAfterAnEditorVisit() {
        writeConfig("close_thumbnail_after = 0.5\n");
        QTRY_COMPARE(settings().closeThumbnailAfterSeconds(), 0.5);
        const int id = capture();
        QVERIFY(id > 0);
        QTest::qWait(100);
        stack().setHidden(id, true); // the Editor opens
        QTest::qWait(200);
        stack().setHidden(id, false); // Done: 0.3 s after the capture
        QTest::qWait(350); // 0.65 s: the first timer's 0.5 s has passed, the new full delay has not
        QCOMPARE(stack().count(), 1);
        QTRY_COMPARE(stack().count(), 0); // and it does close, a full delay after the reveal
        QVERIFY(TestEnv::filesIn(env->screenshots()).isEmpty());
    }

    void autoCloseThatFiredWhileHiddenStillClosesAfterTheReveal() {
        writeConfig("close_thumbnail_after = 0.2\n");
        QTRY_COMPARE(settings().closeThumbnailAfterSeconds(), 0.2);
        const int id = capture();
        QVERIFY(id > 0);
        stack().setHidden(id, true);
        QTest::qWait(500); // the timer fires while hidden: nothing happens
        QVERIFY(stack().isHidden(id)); // still there, hidden
        stack().setHidden(id, false);
        QTRY_COMPARE(stack().count(), 0);
    }

    void saveAfterSaveAutomaticallyOnlyClosesUnlessEdited() {
        writeConfig("save_automatically = true\n");
        QTRY_VERIFY(settings().saveAutomatically());
        const int id = capture();
        QVERIFY(id > 0);
        QTRY_COMPARE(TestEnv::filesIn(env->screenshots()), QStringList{"2026-11-03_09-30-15.png"}); // once the PNG is made
        const QString path = stack().save(id);
        QCOMPARE(QFileInfo(path).fileName(), QString("2026-11-03_09-30-15.png"));
        QCOMPARE(TestEnv::filesIn(env->screenshots()), QStringList{"2026-11-03_09-30-15.png"}); // no _2
        QCOMPARE(stack().count(), 0);

        // After an edit the picture differs from the saved file, so Save writes it.
        const int second = capture();
        QVERIFY(second > 0);
        QTRY_COMPARE(TestEnv::filesIn(env->screenshots()).size(), 2);
        QImage changed(8, 8, QImage::Format_ARGB32);
        changed.fill(Qt::red);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        changed.save(&buffer, "PNG");
        stack().setEdited(second, png);
        QVERIFY(!stack().save(second).isEmpty());
        QCOMPARE(TestEnv::filesIn(env->screenshots()).size(), 3); // 1st, the auto-saved 2nd (_2), the edited one (_3)
    }

    void autoCloseIsIgnoredForARecording() {
        writeConfig("close_thumbnail_after = 0.1\n");
        QTRY_COMPARE(settings().closeThumbnailAfterSeconds(), 0.1);
        const int rec = stack().addRecording(TestEnv::defaultGrimPng(), env->daemon().now(), "/tmp/x.mp4");
        QVERIFY(rec > 0);
        QTest::qWait(400);
        QCOMPARE(stack().count(), 1);
    }

    void autoCopyAndAutoSaveRunAndTheThumbnailStillShows() {
        writeConfig("copy_automatically = true\nsave_automatically = true\n");
        QTRY_VERIFY(settings().copyAutomatically() && settings().saveAutomatically());
        QVERIFY(capture() > 0);
        QCOMPARE(stack().count(), 1);
        QTRY_COMPARE(TestEnv::filesIn(env->screenshots()), QStringList{"2026-11-03_09-30-15.png"}); // once the PNG is made
        QCOMPARE(env->calls("tarn-clip").size(), 1);
        // The saved path is recorded with the History entry, as for the Save button.
        history().flush();
        const auto d = QJsonDocument::fromJson(TestEnv::readFile(historyDir() + "/2026-11-03_09-30-15.json")).object();
        QCOMPARE(d["savedPaths"].toArray().size(), 1);
    }

    void autoCopyAloneSavesNothing() {
        writeConfig("copy_automatically = true\n");
        QTRY_VERIFY(settings().copyAutomatically());
        QVERIFY(capture() > 0);
        QTRY_COMPARE(env->calls("tarn-clip").size(), 1); // once the PNG is made
        QVERIFY(TestEnv::filesIn(env->screenshots()).isEmpty());
        QCOMPARE(stack().count(), 1);
    }

    // ---- shortened retention goes to the Trash ----

    void shorteningRetentionSendsNewlyExpiredEntriesToTheTrash() {
        // "Now" is 2026-11-03. One entry is two months old (already past the 1 month
        // in force), one is two weeks old (inside it), one is new.
        plant("long_gone", QDateTime(QDate(2026, 9, 3), QTime(8, 0, 0)));
        plant("two_weeks", QDateTime(QDate(2026, 10, 20), QTime(8, 0, 0)));
        plant("fresh", QDateTime(QDate(2026, 11, 2), QTime(8, 0, 0)));
        env->restartDaemon(); // the start-up sweep applies the 1 month in force: long_gone goes for good
        QCOMPARE(TestEnv::filesIn(historyDir()), (QStringList{"fresh.json", "fresh.png", "two_weeks.json", "two_weeks.png"}));
        QVERIFY(TestEnv::filesIn(trashFiles()).isEmpty());

        writeConfig("keep_history_for = \"1 week\"\n"); // shortened, live
        QTRY_COMPARE(TestEnv::filesIn(historyDir()), (QStringList{"fresh.json", "fresh.png"}));
        QCOMPARE(TestEnv::filesIn(trashFiles()), (QStringList{"two_weeks.json", "two_weeks.png"}));
        // Recoverable.
        QVERIFY(history().restore("two_weeks"));
        QVERIFY(QFile::exists(historyDir() + "/two_weeks.png"));
    }

    void entriesAlreadyPastTheOldRetentionStillGoForGood() {
        plant("two_weeks", QDateTime(QDate(2026, 10, 20), QTime(8, 0, 0)));
        writeConfig("keep_history_for = \"1 week\"\n");
        QTRY_VERIFY(TestEnv::filesIn(historyDir()).isEmpty());
        QCOMPARE(TestEnv::filesIn(trashFiles()).size(), 2);
        // Shortening once more: what was past 1 week is in the Trash already; a
        // 2-day-old entry is newly expired and goes there too.
        plant("four_days", QDateTime(QDate(2026, 10, 30), QTime(8, 0, 0)));
        plant("ancient", QDateTime(QDate(2025, 1, 1), QTime(8, 0, 0)));
        history().sweep(); // retention still 1 week: ancient is past it, was past it before: for good
        QVERIFY(!QFile::exists(trashFiles() + "/ancient.png"));
        writeConfig("keep_history_for = \"2 days\"\n");
        QTRY_VERIFY(QFile::exists(trashFiles() + "/four_days.png"));
    }

    void theShortenedRetentionAlsoAppliesAtTheNextStartWhenEditedWhileTheDaemonWasOff() {
        plant("two_weeks", QDateTime(QDate(2026, 10, 20), QTime(8, 0, 0)));
        env->restartDaemon();
        writeConfig("keep_history_for = \"1 week\"\n");
        QTest::qWait(150); // reloaded live, which swept it already; the rule is the same either way
        env->restartDaemon();
        QVERIFY(QFile::exists(trashFiles() + "/two_weeks.png"));
        QVERIFY(!QFile::exists(historyDir() + "/two_weeks.png"));
    }

    void lengtheningOrForeverDeletesNothing() {
        plant("two_weeks", QDateTime(QDate(2026, 10, 20), QTime(8, 0, 0)));
        writeConfig("keep_history_for = \"forever\"\n");
        QTRY_COMPARE(settings().keepHistoryFor(), QString("forever"));
        history().sweep();
        QVERIFY(QFile::exists(historyDir() + "/two_weeks.png"));
        writeConfig("keep_history_for = \"2 weeks\"\n"); // back from forever: newly expired entries -> Trash
        QTRY_VERIFY(QFile::exists(trashFiles() + "/two_weeks.png"));
    }
};

QTEST_MAIN(SettingsTest)
#include "settings_test.moc"
