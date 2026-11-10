// The Thumbnail stack's rules: at most 4, overflow auto-save, hidden while
// editing, Save as…, Delete through History's Trash, Close all, and the single
// restore slot (`tarn restore`).
#include "test_env.h"

#include "history/history.h"
#include "storage/image_file.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QSignalSpy>
#include <QTest>

using tarn::History;
using tarn::ThumbnailStack;
using tarn::test::TestEnv;

class StackTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;
    int shots = 0;

    ThumbnailStack &stack() { return env->daemon().service<ThumbnailStack>(); }
    History &history() { return env->daemon().service<History>(); }
    // Each capture is a second later, so each has its own file name.
    int capture() {
        env->setNow(QDateTime(QDate(2026, 11, 3), QTime(9, 30, 0)).addSecs(shots++));
        if (!env->request({"capture", "fullscreen"}).ok)
            return -1;
        return stack().ids().last();
    }
    QString historyDir() const { return env->scratch() + "/data/tarn/history"; }
    QString trashFiles() const { return env->scratch() + "/data/Trash/files"; }
    QStringList savedNames() const { return TestEnv::filesIn(env->screenshots()); }
    static QByteArray pngOf(const QColor &c) {
        QImage image(32, 20, QImage::Format_RGB32);
        image.fill(c);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return bytes;
    }
    // Makes the screenshot folder impossible to create, so every save fails.
    void breakScreenshotFolder() {
        QDir().mkpath(env->pictures());
        QFile blocker(env->screenshots());
        QVERIFY(blocker.open(QIODevice::WriteOnly));
    }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        shots = 0;
    }
    void cleanup() { env.reset(); }

    // ---- the limit ----

    void fourThumbnailsStayAndNothingIsSaved() {
        for (int i = 0; i < 4; ++i)
            QVERIFY(capture() > 0);
        QCOMPARE(stack().count(), 4);
        QVERIFY(savedNames().isEmpty());
    }

    void aFifthCaptureAutoSavesTheOldestAndClosesIt() {
        const int oldest = capture();
        for (int i = 0; i < 3; ++i)
            capture();
        QSignalSpy closed(&stack(), &ThumbnailStack::closed);
        QVERIFY(capture() > 0);
        QCOMPARE(stack().count(), 4);
        QVERIFY(!stack().ids().contains(oldest));
        QCOMPARE(savedNames(), QStringList{"2026-11-03_09-30-00.png"}); // the oldest, by its capture time
        QCOMPARE(closed.size(), 1);
        QCOMPARE(closed.first().first().toInt(), oldest);
    }

    void overflowAutoSaveRecordsThePathInHistory() {
        capture();
        for (int i = 0; i < 4; ++i)
            capture();
        history().flush();
        const QByteArray json = TestEnv::readFile(historyDir() + "/2026-11-03_09-30-00.json");
        QVERIFY(json.contains(env->screenshots().toUtf8() + "/2026-11-03_09-30-00.png"));
    }

    void overflowSavesTheEditedPictureNotTheOriginal() {
        const int oldest = capture();
        const QByteArray edited = pngOf(Qt::magenta);
        stack().setEdited(oldest, edited);
        for (int i = 0; i < 4; ++i)
            capture();
        QCOMPARE(TestEnv::readFile(env->screenshots() + "/2026-11-03_09-30-00.png"), edited);
        history().flush();
        QCOMPARE(TestEnv::pixelsOf(TestEnv::readFile(historyDir() + "/2026-11-03_09-30-00.png")), TestEnv::defaultGrimImage()); // original untouched
    }

    void aFailedAutoSaveKeepsTheOldestAndRaisesOneNotification() {
        const int oldest = capture();
        for (int i = 0; i < 3; ++i)
            capture();
        breakScreenshotFolder();
        QVERIFY(capture() > 0);
        QVERIFY(stack().ids().contains(oldest));
        QCOMPARE(stack().count(), 5); // nothing was lost
        const auto notes = env->calls("notify-send");
        QCOMPARE(notes.size(), 1);
        QVERIFY(notes.first().join(' ').contains("Screenshots")); // the reason names the folder
    }

    // ---- hidden while editing ----

    void aHiddenThumbnailDoesNotCountTowardTheFour() {
        const int editing = capture();
        stack().setHidden(editing, true);
        QVERIFY(stack().isHidden(editing));
        QCOMPARE(stack().count(), 0);
        for (int i = 0; i < 4; ++i)
            capture();
        QCOMPARE(stack().count(), 4);
        QVERIFY(savedNames().isEmpty()); // no overflow: the hidden one is not a fifth
        QCOMPARE(stack().totalCount(), 5);
    }

    void hiddenLeavesTheModelAndComesBackInPlace() {
        const int a = capture(), b = capture(), c = capture();
        QSignalSpy hid(&stack(), &ThumbnailStack::hidden), shown(&stack(), &ThumbnailStack::revealed);
        stack().setHidden(b, true);
        QCOMPARE(stack().ids(), (QList<int>{a, c}));
        QCOMPARE(stack().rowCount(), 2);
        QCOMPARE(hid.size(), 1);
        stack().setHidden(b, false);
        QCOMPARE(stack().ids(), (QList<int>{a, b, c}));
        QCOMPARE(stack().data(stack().index(1), ThumbnailStack::IdRole).toInt(), b);
        QCOMPARE(shown.size(), 1);
    }

    void aHiddenThumbnailCanStillBeSavedAndClosed() {
        const int id = capture();
        stack().setHidden(id, true);
        QVERIFY(!stack().save(id).isEmpty());
        QCOMPARE(stack().totalCount(), 0);
    }

    void showingOneAgainOverFourAutoSavesTheOldest() {
        const int editing = capture();
        stack().setHidden(editing, true);
        for (int i = 0; i < 4; ++i)
            capture();
        stack().setHidden(editing, false);
        QCOMPARE(stack().count(), 4);
        QCOMPARE(savedNames(), QStringList{"2026-11-03_09-30-00.png"});
    }

    // ---- kinds and auto-close ----

    void autoCloseClosesAScreenshotButNeverARecording() {
        const int shot = capture();
        const int rec = stack().addRecording(TestEnv::defaultGrimPng(), env->daemon().now(), "/tmp/x.mp4");
        QCOMPARE(stack().kind(rec), ThumbnailStack::Recording);
        QVERIFY(!stack().autoClose(rec));
        QCOMPARE(stack().count(), 2);
        QVERIFY(stack().autoClose(shot));
        QCOMPARE(stack().count(), 1);
    }

    void aRecordingTakesASlotAndOverflowSavesItToTheRecordingsFolderNotTheScreenshots() {
        QDir().mkpath(env->keptRecordings());
        const QString kept = env->keptRecordings() + "/2026-11-03_09-29-00.mp4";
        QVERIFY(QFile::copy(QStringLiteral(TARN_STUB_DIR "/fixtures/gpu-screen-recorder/sample.mp4"), kept));
        const int rec = stack().addRecording(TestEnv::defaultGrimPng(), env->daemon().now(), kept, 6000);
        for (int i = 0; i < 4; ++i)
            capture();
        QVERIFY(!stack().ids().contains(rec));
        QVERIFY(savedNames().isEmpty());
        QCOMPARE(TestEnv::filesIn(env->recordings()), QStringList{"2026-11-03_09-29-00.mp4"});
    }

    // ---- Save as… ----

    void saveAsWritesPngAndRemembersTheFolder() {
        const int id = capture();
        QCOMPARE(stack().saveAsFolder(), env->screenshots()); // nothing remembered yet
        const QString target = env->scratch() + "/elsewhere/shot.png";
        QVERIFY(stack().saveAs(id, target));
        QCOMPARE(TestEnv::pixelsOf(TestEnv::readFile(target)), TestEnv::defaultGrimImage());
        QCOMPARE(stack().count(), 0);
        QCOMPARE(stack().saveAsFolder(), env->scratch() + "/elsewhere");
    }

    void theLastSaveAsFolderSurvivesARestart() {
        QVERIFY(stack().saveAs(capture(), env->scratch() + "/elsewhere/a.png"));
        env->restartDaemon();
        QCOMPARE(stack().saveAsFolder(), env->scratch() + "/elsewhere");
    }

    void aFailedSaveAsLeavesTheExistingFileAlone() {
        const QString target = env->scratch() + "/elsewhere/keep.jpg";
        QDir().mkpath(env->scratch() + "/elsewhere");
        QFile existing(target);
        QVERIFY(existing.open(QIODevice::WriteOnly));
        existing.write("precious");
        existing.close();
        QString error;
        // Bytes that are not a picture: the JPEG writer fails after the target was opened.
        QVERIFY(!tarn::writeImageFile("not a png", target, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(TestEnv::readFile(target), QByteArray("precious"));
        QCOMPARE(TestEnv::filesIn(env->scratch() + "/elsewhere"), QStringList{"keep.jpg"}); // no temp file left
        // And a good write still replaces it.
        QVERIFY(tarn::writeImageFile(pngOf(Qt::blue), target, &error));
        QVERIFY(TestEnv::readFile(target) != QByteArray("precious"));
        QCOMPARE(QImage(target).size(), QSize(32, 20));
    }

    void saveAsWritesJpeg() {
        const int id = capture();
        const QString target = env->scratch() + "/out/shot.jpg";
        QVERIFY(stack().saveAs(id, target));
        QCOMPARE(QImage(target).size(), QImage::fromData(TestEnv::defaultGrimPng()).size());
        QCOMPARE(QImageReader(target).format(), QByteArray("jpeg"));
    }

    void saveAsWritesWebpWhereThePluginExistsAndNotifiesWhereItDoesNot() {
        const int id = capture();
        const QString target = env->scratch() + "/out/shot.webp";
        const bool can = QImageWriter::supportedImageFormats().contains("webp");
        QCOMPARE(stack().saveAs(id, target), can);
        if (can) {
            QCOMPARE(QImageReader(target).format(), QByteArray("webp"));
        } else {
            QVERIFY(!QFile::exists(target));
            QCOMPARE(stack().count(), 1); // kept
            QCOMPARE(env->calls("notify-send").size(), 1);
        }
    }

    void saveAsToAnUnknownFormatKeepsTheThumbnailAndNotifies() {
        const int id = capture();
        QVERIFY(!stack().saveAs(id, env->scratch() + "/out/shot.bmp"));
        QCOMPARE(stack().count(), 1);
        QCOMPARE(env->calls("notify-send").size(), 1);
    }

    void saveAsRecordsThePathInHistory() {
        const int id = capture();
        QVERIFY(stack().saveAs(id, env->scratch() + "/out/shot.png"));
        history().flush();
        QVERIFY(TestEnv::readFile(historyDir() + "/2026-11-03_09-30-00.json").contains((env->scratch() + "/out/shot.png").toUtf8()));
    }

    // ---- Delete and Close all ----

    void deleteSendsTheHistoryOriginalToTheTrashAndClosesTheThumbnail() {
        const int id = capture();
        history().flush();
        QVERIFY(stack().trash(id));
        QCOMPARE(stack().count(), 0);
        QVERIFY(!QFile::exists(historyDir() + "/2026-11-03_09-30-00.png"));
        QCOMPARE(TestEnv::filesIn(trashFiles()), (QStringList{"2026-11-03_09-30-00.json", "2026-11-03_09-30-00.png"}));
        QVERIFY(savedNames().isEmpty()); // Delete never saves
    }

    void deleteKeepsTheThumbnailAndNotifiesWhenHistoryHasNoOriginal() {
        const int id = capture();
        history().flush();
        QFile::remove(historyDir() + "/2026-11-03_09-30-00.png");
        QFile::remove(historyDir() + "/2026-11-03_09-30-00.json");
        QVERIFY(!stack().trash(id));
        QCOMPARE(stack().count(), 1);
        QCOMPARE(env->calls("notify-send").size(), 1);
    }

    void closeAllClosesEveryThumbnailHiddenOnesToo() {
        capture();
        capture();
        const int hid = capture();
        stack().setHidden(hid, true);
        stack().closeAll();
        QCOMPARE(stack().totalCount(), 0);
        QVERIFY(savedNames().isEmpty());
    }

    // ---- restore ----

    void restoreWithNothingClosedIsRefused() {
        const auto reply = env->request({"restore"});
        QVERIFY(!reply.ok);
        QVERIFY(!reply.error.isEmpty());
    }

    void restoreBringsBackWhateverClosedIt_data() {
        QTest::addColumn<int>("how");
        QTest::newRow("x") << 0;
        QTest::newRow("copy") << 1;
        QTest::newRow("save") << 2;
        QTest::newRow("save as") << 3;
        QTest::newRow("pin") << 4;
        QTest::newRow("delete") << 5;
        QTest::newRow("overflow") << 6;
        QTest::newRow("close all") << 7;
        QTest::newRow("auto-close") << 8;
    }
    void restoreBringsBackWhateverClosedIt() {
        QFETCH(int, how);
        const int id = capture();
        const QByteArray edited = pngOf(Qt::magenta);
        stack().setEdited(id, edited);
        history().flush();
        switch (how) {
        case 0: stack().close(id); break;
        case 1: QVERIFY(stack().copy(id)); break;
        case 2: QVERIFY(!stack().save(id).isEmpty()); break;
        case 3: QVERIFY(stack().saveAs(id, env->scratch() + "/out/s.png")); break;
        case 4: stack().pin(id); break;
        case 5: QVERIFY(stack().trash(id)); break;
        case 6:
            for (int i = 0; i < 4; ++i)
                capture();
            break;
        case 7: stack().closeAll(); break;
        case 8: QVERIFY(stack().autoClose(id)); break;
        }
        QVERIFY(!stack().ids().contains(id));
        const int before = stack().count();

        QVERIFY(env->request({"restore"}).ok);
        QCOMPARE(stack().count(), how == 6 ? 4 : before + 1);
        const int back = stack().ids().last();
        // The restored Thumbnail is the capture with its edits: it saves as the edited picture.
        QCOMPARE(stack().image(back).pixelColor(0, 0), QColor(Qt::magenta));
        QCOMPARE(stack().historyId(back), QString("2026-11-03_09-30-00"));
        if (how != 6) // (restoring over four overflows the oldest, which fills the slot again)
            QVERIFY(!stack().canRestore()); // the single slot is used up
        if (how == 5) {
            QVERIFY(QFile::exists(historyDir() + "/2026-11-03_09-30-00.png"));
            QVERIFY(QFile::exists(historyDir() + "/2026-11-03_09-30-00.json"));
            QVERIFY(!QFile::exists(trashFiles() + "/2026-11-03_09-30-00.png"));
        }
    }

    void restoredEditsAreWhatSavingWrites() {
        const int id = capture();
        const QByteArray edited = pngOf(Qt::magenta);
        stack().setEdited(id, edited);
        stack().close(id);
        QVERIFY(stack().restore());
        const QString path = stack().save(stack().ids().last());
        QCOMPARE(TestEnv::readFile(path), edited);
    }

    void thereIsOnlyOneRestoreSlot() {
        const int a = capture(), b = capture();
        stack().close(a);
        stack().close(b);
        QVERIFY(stack().restore());
        QCOMPARE(stack().count(), 1);
        QVERIFY(!env->request({"restore"}).ok); // a was overwritten by b
        QCOMPARE(stack().count(), 1);
    }

    void closeAllLeavesTheNewestInTheSlot() {
        capture();
        const int newest = (capture(), stack().ids().last());
        Q_UNUSED(newest);
        stack().closeAll();
        QVERIFY(stack().restore());
        QCOMPARE(stack().historyId(stack().ids().last()), QString("2026-11-03_09-30-01"));
    }

    void restoringAStackOfFourAutoSavesTheOldest() {
        const int first = capture();
        stack().close(first);
        for (int i = 0; i < 4; ++i)
            capture();
        QVERIFY(stack().restore());
        QCOMPARE(stack().count(), 4);
        QCOMPARE(savedNames(), QStringList{"2026-11-03_09-30-01.png"}); // the oldest of the four on screen
    }

    void aDeletedThumbnailWhoseOriginalLeftTheTrashStaysInTheSlotAndNotifies() {
        const int id = capture();
        history().flush();
        QVERIFY(stack().trash(id));
        QDir(trashFiles()).removeRecursively();
        QVERIFY(!env->request({"restore"}).ok);
        QCOMPARE(stack().count(), 0);
        QCOMPARE(env->calls("notify-send").size(), 1);
        QVERIFY(stack().canRestore());
    }

    void aRestoredThumbnailGetsAFreshId() {
        const int id = capture();
        stack().close(id);
        QVERIFY(stack().restore());
        QVERIFY(stack().ids().last() != id);
    }
};

QTEST_MAIN(StackTest)
#include "stack_test.moc"
