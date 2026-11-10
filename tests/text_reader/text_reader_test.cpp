// The Text reader reads each History original in the background, so
// History can be searched by what was in the picture. Tesseract is a stub here.
#include "test_env.h"

#include "history/history.h"
#include "recording/recording_session.h"
#include "settings/settings.h"
#include "text/text_reader.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using tarn::History;
using tarn::RecordingSession;
using tarn::test::TestEnv;

class TextReaderTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    QString historyDir() const { return env->scratch() + "/data/tarn/history"; }
    QJsonObject describe(const QString &id) {
        return QJsonDocument::fromJson(TestEnv::readFile(historyDir() + "/" + id + ".json")).object();
    }
    tarn::TextReader &reader() { return env->daemon().service<tarn::TextReader>(); }
    void waitIdle() { QTRY_VERIFY(!reader().busy()); }
    void plant(const QString &id, bool withText, const QString &text = {}) {
        QDir().mkpath(historyDir());
        QFile png(historyDir() + "/" + id + ".png");
        QVERIFY(png.open(QIODevice::WriteOnly));
        png.write(TestEnv::defaultGrimPng());
        QJsonObject d{{"captureTime", "2026-11-01T10:00:00"}, {"kind", "area"}};
        if (withText)
            d["text"] = text;
        QFile json(historyDir() + "/" + id + ".json");
        QVERIFY(json.open(QIODevice::WriteOnly));
        json.write(QJsonDocument(d).toJson());
    }
    void startRecording() {
        auto &s = env->daemon().service<RecordingSession>();
        QVERIFY(s.start(QRect(20, 10, 80, 50), QRect(100, 50, 256, 160), 2.0, "DP-1").ok);
        QTRY_COMPARE_WITH_TIMEOUT(s.state(), RecordingSession::Recording, 5000);
    }

private slots:
    void init() { env = std::make_unique<TestEnv>(); }
    void cleanup() { env.reset(); }

    void aNewCaptureGainsItsText() {
        reader();
        QSignalSpy spy(&env->daemon().service<History>(), &History::textRead);
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.first().first().toString(), QString("2026-11-03_09-30-15"));
        QVERIFY(describe("2026-11-03_09-30-15")["text"].toString().contains("Tarn takes screenshots"));
        QCOMPARE(TestEnv::pixelsOf(env->stdinOfCall("tesseract", 1)), TestEnv::defaultGrimImage());
    }

    void aPictureWithNoTextGetsAnEmptyString() {
        env->setMode("tesseract", "empty");
        reader();
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        QTRY_VERIFY(describe("2026-11-03_09-30-15").contains("text"));
        QCOMPARE(describe("2026-11-03_09-30-15")["text"].toString(), QString(""));
    }

    void readsRunUnderNiceWithTheLanguagesSetting() {
        reader();
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        QTRY_COMPARE(env->calls("nice").size(), 1);
        waitIdle();
        const auto nice = env->calls("nice");
        QCOMPARE(nice[0].mid(0, 3), (QStringList{"-n", "19", "tesseract"}));
        const int l = nice[0].indexOf("-l");
        QVERIFY(l > 0);
        QCOMPARE(nice[0][l + 1], env->daemon().service<tarn::Settings>().textLanguages());
    }

    void catchUpReadsEveryDescriptionWithoutText() {
        plant("a", false);
        plant("b", true, "kept");
        plant("c", false);
        reader();
        QTRY_VERIFY(describe("a").contains("text") && describe("c").contains("text"));
        waitIdle();
        QCOMPARE(env->calls("tesseract").size(), 2);
        QCOMPARE(describe("b")["text"].toString(), QString("kept"));
    }

    void entriesAlreadyReadAreNotReadAgain() {
        plant("a", true, "");
        reader();
        QTest::qWait(300);
        waitIdle();
        QCOMPARE(env->calls("tesseract").size(), 0);
    }

    void readsAreOneAtATime() {
        env->setMode("tesseract", "slow");
        plant("a", false);
        plant("b", false);
        reader();
        QTRY_COMPARE(env->calls("tesseract").size(), 1);
        QTest::qWait(400);
        QCOMPARE(env->calls("tesseract").size(), 1); // the second waits
        QTRY_COMPARE_WITH_TIMEOUT(env->calls("tesseract").size(), 2, 5000);
        waitIdle();
    }

    void waitsWhileARecordingRuns() {
        reader();
        startRecording();
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        QTest::qWait(500);
        QCOMPARE(env->calls("tesseract").size(), 0);
        auto &s = env->daemon().service<RecordingSession>();
        s.stop();
        QTRY_COMPARE_WITH_TIMEOUT(s.state(), RecordingSession::Done, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(describe("2026-11-03_09-30-15").contains("text"), 5000);
    }

    void aFailedReadLeavesTextMissingUntilTheNextStart() {
        env->setMode("tesseract", "fail");
        plant("a", false);
        reader();
        QTRY_COMPARE(env->calls("tesseract").size(), 1);
        waitIdle();
        QTest::qWait(300);
        QVERIFY(!describe("a").contains("text"));
        QCOMPARE(env->calls("tesseract").size(), 1);
        env->setMode("tesseract", "ok");
        env->restartDaemon();
        reader();
        QTRY_VERIFY(describe("a").contains("text"));
    }

    void withoutTesseractNothingIsReadAndNothingIsSaid() {
        plant("a", false);
        qputenv("PATH", (env->scratch() + "/sysbin").toUtf8()); // no stubs: no tesseract
        reader();
        QTest::qWait(400);
        QVERIFY(!describe("a").contains("text"));
        QVERIFY(!reader().busy());
        QCOMPARE(env->calls("nice").size(), 0);
        QCOMPARE(env->calls("notify-send").size(), 0);
    }
};

QTEST_MAIN(TextReaderTest)
#include "text_reader_test.moc"
