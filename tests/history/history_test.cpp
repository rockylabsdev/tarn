// History keeps every image capture's untouched original, sweeps old
// ones, and can send an entry to the freedesktop Trash and back.
#include "test_env.h"

#include "history/history.h"
#include "thumbnails/thumbnail_stack.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using tarn::History;
using tarn::ThumbnailStack;
using tarn::test::TestEnv;

class HistoryTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    History &history() { return env->daemon().service<History>(); }
    QString dataDir() const { return env->scratch() + "/data"; }
    QString historyDir() const { return dataDir() + "/tarn/history"; }
    QJsonObject describe(const QString &id) {
        history().flush();
        return QJsonDocument::fromJson(TestEnv::readFile(historyDir() + "/" + id + ".json")).object();
    }
    // An old entry, as an earlier day's capture would have left it.
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

    void aCaptureWritesAnUntouchedPngAndDescriptionPair() {
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        history().flush();
        QCOMPARE(TestEnv::filesIn(historyDir()), (QStringList{"2026-11-03_09-30-15.json", "2026-11-03_09-30-15.png"}));
        QCOMPARE(TestEnv::pixelsOf(TestEnv::readFile(historyDir() + "/2026-11-03_09-30-15.png")), TestEnv::defaultGrimImage());
    }

    void theDescriptionIsFilled() {
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        const QJsonObject d = describe("2026-11-03_09-30-15");
        QCOMPARE(d["kind"].toString(), QString("fullscreen"));
        QCOMPARE(QDateTime::fromString(d["captureTime"].toString(), Qt::ISODate), env->daemon().now());
        QVERIFY(d["position"].toObject()["width"].toInt() > 0);
        QVERIFY(d["savedPaths"].toArray().isEmpty());
        QVERIFY(!d.contains("app")); // window captures only
    }

    void windowCapturesRecordAppAndTitle() {
        const QString id = history().record("png", {env->daemon().now(), "window", "firefox", "Docs", QRect(5, 6, 700, 800)});
        const QJsonObject d = describe(id);
        QCOMPARE(d["app"].toString(), QString("firefox"));
        QCOMPARE(d["title"].toString(), QString("Docs"));
        QCOMPARE(d["position"].toObject()["x"].toInt(), 5);
        QCOMPARE(d["position"].toObject()["height"].toInt(), 800);
    }

    void twoCapturesInOneSecondKeepBoth() {
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        history().flush();
        QCOMPARE(TestEnv::filesIn(historyDir()).size(), 4);
    }

    void savingAddsEverySavedPathToTheDescription() {
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        auto &stack = env->daemon().service<ThumbnailStack>();
        const QString path = stack.save(stack.ids().last());
        QVERIFY(!path.isEmpty());
        QCOMPARE(describe("2026-11-03_09-30-15")["savedPaths"].toArray(), QJsonArray{path});

        history().addSavedPath("2026-11-03_09-30-15", "/elsewhere/a.png");
        QCOMPARE(describe("2026-11-03_09-30-15")["savedPaths"].toArray().size(), 2);
        // The PNG stays the untouched original.
        QCOMPARE(TestEnv::pixelsOf(TestEnv::readFile(historyDir() + "/2026-11-03_09-30-15.png")), TestEnv::defaultGrimImage());
    }

    void aFailedCaptureWritesNothing() {
        env->setMode("grim", "fail");
        QVERIFY(!env->request({"capture", "fullscreen"}).ok);
        history().flush();
        QVERIFY(TestEnv::filesIn(historyDir()).isEmpty());
    }

    void sweepRemovesOnlyEntriesOlderThanOneMonthByCaptureTime() {
        plant("old", QDateTime(QDate(2026, 10, 3), QTime(9, 30, 14)));  // one second past a month
        plant("edge", QDateTime(QDate(2026, 10, 3), QTime(9, 30, 15))); // exactly a month: kept
        plant("new", QDateTime(QDate(2026, 11, 1), QTime(8, 0, 0)));
        QCOMPARE(history().sweep(), 1);
        QCOMPARE(TestEnv::filesIn(historyDir()), (QStringList{"edge.json", "edge.png", "new.json", "new.png"}));
    }

    void sweepGoesByCaptureTimeNotFileAge() {
        plant("old", QDateTime(QDate(2025, 1, 1), QTime(0, 0, 0))); // file just created, capture time old
        history().sweep();
        QVERIFY(TestEnv::filesIn(historyDir()).isEmpty());
    }

    void theDaemonSweepsAtStartUp() {
        plant("old", QDateTime(QDate(2025, 1, 1), QTime(0, 0, 0)));
        plant("new", QDateTime(QDate(2026, 11, 2), QTime(0, 0, 0)));
        env->restartDaemon();
        QCOMPARE(TestEnv::filesIn(historyDir()), (QStringList{"new.json", "new.png"}));
    }

    void trashAndRestoreRoundTrip() {
        QVERIFY(env->request({"capture", "fullscreen"}).ok);
        const QString id = "2026-11-03_09-30-15";
        history().flush();
        const QByteArray png = TestEnv::readFile(historyDir() + "/" + id + ".png");

        QVERIFY(history().trash(id));
        QVERIFY(TestEnv::filesIn(historyDir()).isEmpty());
        QVERIFY(QFile::exists(dataDir() + "/Trash/files/" + id + ".png"));
        QVERIFY(QFile::exists(dataDir() + "/Trash/files/" + id + ".json"));
        const QByteArray info = TestEnv::readFile(dataDir() + "/Trash/info/" + id + ".png.trashinfo");
        QVERIFY(info.startsWith("[Trash Info]\nPath=" + historyDir().toUtf8() + "/" + id.toUtf8() + ".png\n"));
        QVERIFY(info.contains("DeletionDate=2026-11-03T09:30:15"));

        QVERIFY(history().restore(id));
        QCOMPARE(TestEnv::readFile(historyDir() + "/" + id + ".png"), png);
        QCOMPARE(describe(id)["kind"].toString(), QString("fullscreen"));
        QVERIFY(TestEnv::filesIn(dataDir() + "/Trash/files").isEmpty());
        QVERIFY(TestEnv::filesIn(dataDir() + "/Trash/info").isEmpty());
    }

    void trashingAnUnknownEntryFails() {
        QVERIFY(!history().trash("nope"));
        QVERIFY(!history().restore("nope"));
    }
};

QTEST_GUILESS_MAIN(HistoryTest)
#include "history_test.moc"
