// Sequence. S keeps the Overlay open on the same frozen image after each
// shot; shots reach History at once while their Thumbnails are held until the
// Overlay closes, then pushed in order under the stack's overflow rule. Text and
// Color shots copy and notify on their own. Record or a Timer shot ends it.
#include "test_env.h"

#include "history/history.h"
#include "frame/frame.h"
#include "overlay/overlay.h"
#include "preset/preset_store.h"
#include "text/text_capture.h"
#include "thumbnails/thumbnail_stack.h"

#include <QBuffer>
#include <QImage>
#include <QTest>

using tarn::Overlay;
using tarn::OverlayModel;
using tarn::ThumbnailStack;
using tarn::test::TestEnv;

namespace {
QByteArray pngOf(const QImage &image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}
// Device pixel (x, y) is rgb(x * 2, y * 3, (x ^ y) * 4).
QImage gradient() {
    QImage image(128, 80, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixel(x, y, qRgb(x * 2, y * 3, (x ^ y) * 4));
    return image;
}
// One 128 x 80 monitor at scale 2: 64 x 40 logical pixels.
const QByteArray kMonitor = R"([{"id":0,"name":"DP-1","x":0,"y":0,"width":128,"height":80,"scale":2.0,"transform":0}])";
} // namespace

class SequenceTest : public QObject {
    Q_OBJECT
    std::unique_ptr<TestEnv> env;
    int m_second = 0;

    OverlayModel &model() { return env->daemon().service<Overlay>().model(); }
    ThumbnailStack &stack() { return env->daemon().service<ThumbnailStack>(); }
    QStringList history() { return env->daemon().service<tarn::History>().ids(); }
    void openSequence() {
        QVERIFY(env->request({"capture", "area"}).ok);
        model().keyPressed(Qt::Key_S);
        QVERIFY(model().sequence());
    }
    // A 20 x 20 logical Selection from (x, 5), each one second after the last.
    void shot(int x) {
        env->setNow(QDateTime(QDate(2026, 11, 4), QTime(12, 0, m_second++), Qt::UTC));
        model().pointerPressed(x, 5, Qt::LeftButton);
        model().pointerMoved(x + 20, 25);
        model().pointerReleased(x + 20, 25, Qt::LeftButton);
    }

private slots:
    void init() {
        env = std::make_unique<TestEnv>();
        m_second = 0;
        env->setFixture("hyprctl", "monitors.json", kMonitor);
        env->setFixture("grim", "output.png", pngOf(gradient()));
        env->setPointer(10, 10);
    }
    void cleanup() { env.reset(); }

    void eachShotHasItsProjectFileWhileTheOverlayIsStillOpen() {
        tarn::Frame frame;
        frame.background.kind = "plain";
        frame.background.params = {{"color", "#ffff0000"}};
        frame.padding = 0.1;
        auto &presets = env->daemon().service<tarn::PresetStore>();
        QVERIFY(presets.save("Red", frame));
        QVERIFY(presets.setAuto("Red"));
        openSequence();
        shot(2);
        shot(30);
        env->daemon().service<tarn::History>().flush();
        QCOMPARE(history().size(), 2);
        QCOMPARE(stack().count(), 0); // the Thumbnails still wait for the Overlay to close
        for (const QString &id : history())
            QVERIFY(env->daemon().service<tarn::History>().hasProject(id));
        model().keyPressed(Qt::Key_Escape);
        QCOMPARE(stack().count(), 2);
        for (const QString &id : history())
            QVERIFY(env->daemon().service<tarn::History>().hasProject(id));
    }

    void startsOffEveryTimeAndSToggles() {
        QVERIFY(env->request({"capture", "area"}).ok);
        QVERIFY(!model().sequence());
        model().keyPressed(Qt::Key_S);
        QVERIFY(model().sequence());
        model().keyPressed(Qt::Key_S);
        QVERIFY(!model().sequence());
        model().keyPressed(Qt::Key_S);
        model().keyPressed(Qt::Key_Escape);
        QVERIFY(!model().active());
        QVERIFY(env->request({"capture", "area"}).ok);
        QVERIFY(!model().sequence());
        QCOMPARE(model().taken(), 0);
    }

    void shotsKeepTheOverlayOpenOnTheSameImageAndCount() {
        openSequence();
        const int generation = env->daemon().service<Overlay>().generation();
        shot(5);
        QVERIFY(model().active());
        QVERIFY(!model().hasSelection());
        QCOMPARE(model().taken(), 1);
        shot(25);
        QCOMPARE(model().taken(), 2);
        QVERIFY(model().active());
        QCOMPARE(env->daemon().service<Overlay>().generation(), generation);
        QCOMPARE(env->calls("grim").size(), 1);
    }

    void shotsGoToHistoryAtOnceAndThumbnailsAreHeldUntilLeaving() {
        openSequence();
        shot(5);
        shot(25);
        QCOMPARE(history().size(), 2);
        QCOMPARE(stack().count(), 0);
        model().keyPressed(Qt::Key_Escape);
        QVERIFY(!model().active());
        QCOMPARE(stack().count(), 2);
        QCOMPARE(history().size(), 2);
        const QList<int> ids = stack().ids();
        QCOMPARE(stack().historyId(ids[0]), history()[0]);
        QCOMPARE(stack().historyId(ids[1]), history()[1]);
    }

    void thumbnailsPushedInOrderWithOverflow() {
        openSequence();
        for (int i = 0; i < 6; ++i)
            shot(2 + i * 3);
        QCOMPARE(model().taken(), 6);
        QCOMPARE(stack().count(), 0);
        model().cancel();
        QCOMPARE(stack().count(), ThumbnailStack::kMaxThumbnails);
        // The two oldest overflowed; the four newest remain, in order.
        const QStringList all = history();
        QCOMPARE(all.size(), 6);
        const QList<int> ids = stack().ids();
        for (int i = 0; i < 4; ++i)
            QCOMPARE(stack().historyId(ids[i]), all[i + 2]);
    }

    void escClearsAKeptSelectionThenLeavesKeepingEveryShot() {
        openSequence();
        shot(5);
        model().pointerPressed(10, 5, Qt::LeftButton, Qt::ControlModifier);
        model().pointerMoved(30, 25, Qt::ControlModifier);
        model().pointerReleased(30, 25, Qt::LeftButton, Qt::ControlModifier);
        QVERIFY(model().kept());
        model().keyPressed(Qt::Key_Escape);
        QVERIFY(model().active());
        QVERIFY(!model().kept());
        QCOMPARE(model().taken(), 1);
        model().keyPressed(Qt::Key_Escape);
        QVERIFY(!model().active());
        QCOMPARE(stack().count(), 1);
        QCOMPARE(history().size(), 1);
    }

    void aRecordStartEndsTheSequenceAndLeaves() {
        openSequence();
        shot(5);
        model().keyPressed(Qt::Key_2);
        model().pointerPressed(10, 5, Qt::LeftButton);
        model().pointerMoved(30, 25);
        model().pointerReleased(30, 25, Qt::LeftButton);
        QVERIFY(model().kept()); // Record always keeps its Selection
        model().confirm();
        QVERIFY(!model().active());
        QVERIFY(!model().sequence());
        QCOMPARE(stack().count(), 1); // the earlier shot's Thumbnail
    }

    void aTimerShotEndsTheSequence() {
        openSequence();
        shot(5);
        model().keyPressed(Qt::Key_T);
        shot(25);
        QVERIFY(!model().active());
        QVERIFY(!model().sequence());
        QCOMPARE(stack().count(), 1);
        QCOMPARE(model().taken(), 1); // the timer shot is not a Sequence shot
    }

    void textShotsCopyAndNotifyEachTime() {
        openSequence();
        model().keyPressed(Qt::Key_3);
        shot(5);
        QTRY_VERIFY(!env->daemon().service<tarn::TextCapture>().busy());
        QVERIFY(model().active());
        QCOMPARE(env->calls("tarn-clip").size(), 1);
        QCOMPARE(env->calls("notify-send").size(), 1);
        shot(25);
        QTRY_VERIFY(!env->daemon().service<tarn::TextCapture>().busy());
        QCOMPARE(env->calls("tarn-clip").size(), 2);
        QCOMPARE(env->calls("notify-send").size(), 2);
        QCOMPARE(model().taken(), 2);
        QVERIFY(model().active());
        QCOMPARE(history().size(), 0); // Text captures are never kept
    }

    void colorPicksCopyAndNotifyEachTimeAndStayOpen() {
        openSequence();
        model().keyPressed(Qt::Key_4);
        model().pointerPressed(10, 5, Qt::LeftButton);
        model().pointerReleased(10, 5, Qt::LeftButton);
        QVERIFY(model().active());
        QCOMPARE(model().taken(), 1);
        model().pointerPressed(20, 10, Qt::LeftButton);
        model().pointerReleased(20, 10, Qt::LeftButton);
        QVERIFY(model().active());
        QCOMPARE(model().taken(), 2);
        QCOMPARE(env->calls("tarn-clip").size(), 2);
        QCOMPARE(QString::fromUtf8(env->stdinOfCall("tarn-clip", 2)), QString("#503cf0")); // device (40, 20)
        QCOMPARE(env->calls("notify-send").size(), 2);
        model().keyPressed(Qt::Key_Escape);
        QVERIFY(!model().active());
    }
};

QTEST_MAIN(SequenceTest)
#include "sequence_test.moc"
