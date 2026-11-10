// Copy on a Thumbnail puts the capture on the clipboard through wl-copy,
// backed by a PNG file in the shared cache folder that is swept after a day.
#include "test_env.h"

#include "clipboard/clipboard.h"
#include "clipboard/helper/clip_offers.h"
#include "thumbnails/thumbnail_stack.h"

#include <QDir>
#include <QFile>
#include <QTest>

using tarn::Clipboard;
using tarn::ThumbnailStack;
using tarn::test::TestEnv;

class CopyTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;

    ThumbnailStack &stack() { return env->daemon().service<ThumbnailStack>(); }
    int captureOne() { return env->request({"capture", "fullscreen"}).ok ? stack().ids().last() : -1; }
    QString shared() const { return env->scratch() + "/cache/tarn/shared"; }
    void touch(const QString &name, const QDateTime &mtime) {
        QDir().mkpath(shared());
        QFile f(shared() + '/' + name);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
        f.flush();
        QVERIFY(f.setFileTime(mtime, QFileDevice::FileModificationTime));
        f.close();
    }

private slots:
    void init() { env = std::make_unique<TestEnv>(); }
    void cleanup() { env.reset(); }

    void copyHandsTheSharedFileToTheHelperAndWritesTheSharedFile() {
        const int id = captureOne();
        QVERIFY(id > 0);
        QVERIFY(stack().copy(id));

        const QString file = shared() + "/2026-11-03_09-30-15.png";
        QCOMPARE(env->calls("tarn-clip"), (QList<QStringList>{{file}}));
        QVERIFY(env->calls("wl-copy").isEmpty());
        QCOMPARE(TestEnv::filesIn(shared()), QStringList{"2026-11-03_09-30-15.png"});
        QCOMPARE(TestEnv::pixelsOf(TestEnv::readFile(file)), TestEnv::defaultGrimImage());
    }

    void copyClosesTheThumbnailQuietly() {
        const int id = captureOne();
        QVERIFY(stack().copy(id));
        QCOMPARE(stack().count(), 0);
        QVERIFY(env->calls("notify-send").isEmpty());
    }

    void twoCopiesInOneSecondNeverOverwrite() {
        const int a = captureOne(), b = captureOne();
        QVERIFY(stack().copy(a));
        QVERIFY(stack().copy(b));
        QCOMPARE(TestEnv::filesIn(shared()), (QStringList{"2026-11-03_09-30-15.png", "2026-11-03_09-30-15_2.png"}));
    }

    void aFailingHelperKeepsTheThumbnailAndNotifies() {
        const int id = captureOne();
        env->setMode("tarn-clip", "fail");
        QVERIFY(!stack().copy(id));
        QCOMPARE(stack().count(), 1);
        QCOMPARE(env->calls("notify-send").size(), 1);
        QVERIFY(TestEnv::filesIn(shared()).isEmpty());
    }

    void copyFileOffersTheFileOnly() {
        QString error;
        QVERIFY(env->daemon().service<Clipboard>().copyFile("/tmp/x.mp4", &error));
        QCOMPARE(env->calls("tarn-clip"), (QList<QStringList>{{"--file-only", "/tmp/x.mp4"}}));
    }

    void theOfferListHasTheThreeFormsAndFileOnlyDropsTheImage() {
        using namespace tarn::clip;
        const auto both = offersFor("/home/me/a b.png", false);
        QCOMPARE(both.size(), size_t(3));
        QCOMPARE(both[0].mime, std::string("image/png"));
        QVERIFY(both[0].isFileContent);
        QCOMPARE(both[1].mime, std::string("text/uri-list"));
        QCOMPARE(both[1].text, std::string("file:///home/me/a%20b.png\r\n"));
        QCOMPARE(both[2].mime, std::string("x-special/gnome-copied-files"));
        QCOMPARE(both[2].text, std::string("copy\nfile:///home/me/a%20b.png"));
        const auto fileOnly = offersFor("/x.png", true);
        QCOMPARE(fileOnly.size(), size_t(2));
        QCOMPARE(fileOnly[0].mime, std::string("text/uri-list"));
    }

    void theSweepRemovesOnlyFilesOlderThanADay() {
        const QDateTime now = env->now();
        touch("old.png", now.addSecs(-25 * 3600));
        touch("recent.png", now.addSecs(-23 * 3600));
        touch("new.png", now);
        QCOMPARE(env->daemon().service<Clipboard>().sweep(), 1);
        QCOMPARE(TestEnv::filesIn(shared()), (QStringList{"new.png", "recent.png"}));
    }

    void theSweepRunsWhenTheDaemonStarts() {
        touch("old.png", env->now().addDays(-2));
        touch("fresh.png", env->now());
        env->restartDaemon();
        QCOMPARE(TestEnv::filesIn(shared()), QStringList{"fresh.png"});
    }
};

QTEST_GUILESS_MAIN(CopyTest)
#include "copy_test.moc"
