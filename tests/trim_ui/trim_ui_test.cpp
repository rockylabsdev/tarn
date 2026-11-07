// The Trim window (QML, compiled in), driven with the same mouse and key
// events a user makes, off-screen, with the real ffmpeg making the preview frames
// and the filmstrip from a committed 3 s clip. What this cannot check is
// needs-eyes: the real compositor (floating, centered, no animation, dragging by a
// bar), how it looks next to the prototype, and the speed of the preview.
#include "test_env.h"

#include "palette/palette.h"
#include "thumbnail_surface.h"
#include "thumbnails/thumbnail_stack.h"
#include "trim/trim.h"
#include "trim_windows.h"
#include "recording/recording_store.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QQuickItem>
#include <QQuickView>
#include <QTest>

using tarn::test::TestEnv;

namespace {

QQuickItem *findItem(QQuickItem *from, const QString &objectName) {
    if (from->objectName() == objectName)
        return from;
    for (QQuickItem *child : from->childItems())
        if (QQuickItem *hit = findItem(child, objectName))
            return hit;
    return nullptr;
}

void collect(QQuickItem *from, const QString &objectName, QList<QQuickItem *> &out) {
    if (from->objectName() == objectName)
        out << from;
    for (QQuickItem *child : from->childItems())
        collect(child, objectName, out);
}

QByteArray posterPng() {
    QImage image(64, 48, QImage::Format_RGB32);
    image.fill(Qt::darkCyan);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

struct Fixture {
    TestEnv env;
    tarn::ThumbnailStack &stack = env.daemon().service<tarn::ThumbnailStack>();
    tarn::Trim &trim = env.daemon().service<tarn::Trim>();
    tarn::TrimWindows windows{trim, env.daemon().service<tarn::Palette>()};
    QString kept;
    int id = -1;

    Fixture() {
        QString error;
        kept = env.daemon().service<tarn::RecordingStore>().newKeptPath(env.daemon().now(), "mp4", &error);
        QFile file(kept);
        file.open(QIODevice::WriteOnly);
        file.write(TestEnv::readFile(QStringLiteral(TARN_STUB_DIR) + "/fixtures/gpu-screen-recorder/sample60.mp4"));
        file.close();
        id = stack.addRecording(posterPng(), env.daemon().now(), kept, 3000);
    }

    QQuickView *open() {
        stack.edit(id); // what a click on the Recording's Thumbnail does
        QQuickView *view = windows.viewFor(id);
        if (!view || view->status() != QQuickView::Ready || !QTest::qWaitForWindowExposed(view))
            return nullptr;
        QTest::qWait(50);
        return view;
    }
    static QPoint centerOf(QQuickItem *item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); }
    static void click(QQuickView *view, QQuickItem *item) { QTest::mouseClick(view, Qt::LeftButton, {}, centerOf(item)); }
};

} // namespace

class TrimUiTest : public QObject {
    Q_OBJECT
private slots:
    void theWindowIsFramelessTitledForTheWindowRuleAndSizedByTheEditorsRule() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        QCOMPARE(view->title(), QString("Tarn trim")); // what tarn.lua's window rule matches
        QVERIFY(view->flags() & Qt::FramelessWindowHint);
        QCOMPARE(view->size(), f.trim.session(f.id)->openingSize());
        QVERIFY(view->width() >= 1180);
    }

    void theBarsAre52AndTheBottomBar104WithAFilmstripOf52() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        auto *top = findItem(root, "topBar"), *bottom = findItem(root, "bottomBar"), *preview = findItem(root, "preview");
        QCOMPARE(top->height(), 52.0);
        QCOMPARE(bottom->height(), 104.0);
        QCOMPARE(bottom->mapToScene(QPointF(0, 0)).y(), view->height() - 104.0);
        QCOMPARE(preview->height(), view->height() - 52.0 - 104.0);
        auto *frame = findItem(root, "accentFrame");
        QCOMPARE(frame->height(), 52.0 + 6);
        // The frame's 14 px ends stick out each side of the kept range (all of it, to begin with).
        QCOMPARE(frame->width(), view->width() - 60.0 + 28);
        for (const char *name : {"closeButton", "timeLabel", "durationLabel", "startHereButton", "endHereButton", "keepsLabel",
                                 "copyButton", "saveAsButton", "doneButton", "startHandle", "endHandle", "playhead", "dim"})
            QVERIFY2(findItem(root, name), name);
        QCOMPARE(findItem(root, "playhead")->width(), 3.0);
        QCOMPARE(findItem(root, "startHandle")->width(), 14.0);
    }

    void theTopBarShowsTheTimeAndWhatItKeeps() {
        Fixture f;
        QQuickView *view = f.open();
        auto *root = view->rootObject();
        QCOMPARE(findItem(root, "timeLabel")->property("text").toString(), QString("0:00.00"));
        QCOMPARE(findItem(root, "durationLabel")->property("text").toString(), QString("/ 0:03.00"));
        QCOMPARE(findItem(root, "keepsLabel")->property("text").toString(), QString("Keeps 0:03.00"));
        auto *session = f.trim.session(f.id);
        session->dragStart(1000.0);
        session->dragEnd(2500.0);
        QCOMPARE(findItem(root, "keepsLabel")->property("text").toString(), QString("Keeps 0:01.50"));
        QCOMPARE(findItem(root, "timeLabel")->property("text").toString(), QString("0:02.50"));
    }

    void theTicksAreLabeledAndNoMoreThanTen() {
        Fixture f;
        QQuickView *view = f.open();
        QList<QQuickItem *> labels;
        collect(view->rootObject(), "tickLabel", labels);
        QVERIFY(!labels.isEmpty() && labels.size() <= 11);
        QVERIFY(labels.first()->property("text").toString().startsWith("0:00"));
    }

    void theFilmstripIsMadeOfRealFramesOfTheRecording() {
        Fixture f;
        QQuickView *view = f.open();
        QList<QQuickItem *> images;
        collect(view->rootObject(), "stripImage", images);
        QCOMPARE(images.size(), 2); // the dimmed whole strip and the kept part
        for (auto *image : images)
            QTRY_COMPARE_WITH_TIMEOUT(image->property("status").toInt(), 1, 10000); // Image.Ready
        // 64x48 frames shown 52 px tall: 69 px a tile, enough of them to cover 1220 px, each a different frame.
        QCOMPARE(images.first()->property("sourceSize").toSize().height(), 104);
        QVERIFY(images.first()->width() >= 1220);
    }

    void draggingTheStartHandleSnapsToAWholeFrameAndTheFramesInTheStripFollow() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        auto *root = view->rootObject();
        auto *handle = findItem(root, "startHandle");
        const QPoint from = Fixture::centerOf(handle);
        const QPoint to = from + QPoint(300, 0);
        QTest::mousePress(view, Qt::LeftButton, {}, from);
        QTest::mouseMove(view, from + QPoint(100, 0));
        QTest::mouseMove(view, to);
        auto *session = f.trim.session(f.id);
        // 1220 px for 180 frames; the pointer is 7 px left of the range edge it drags.
        const double expected = qRound((to.x() + 7 - 30) / 1220.0 * 180);
        QCOMPARE(session->startFrame(), qint64(expected));
        QVERIFY(findItem(root, "timePill")->isVisible());
        QVERIFY(!findItem(root, "playhead")->isVisible()); // hidden while a Handle is dragged
        QTest::mouseRelease(view, Qt::LeftButton, {}, to);
        QCOMPARE(session->playheadFrame(), session->startFrame()); // it sits on the Handle afterwards
        QVERIFY(findItem(root, "playhead")->isVisible());
        // The handle and the accent frame moved with it, to a whole frame's x.
        QCOMPARE(qRound(handle->x() + 14), qRound(30 + session->startFrame() / 180.0 * 1220));
        QCOMPARE(findItem(root, "keepsLabel")->property("text").toString(),
                 "Keeps " + session->keptText());
    }

    void thePreviewShowsTheFrameUnderTheHandleMadeByFfmpeg() {
        Fixture f;
        QQuickView *view = f.open();
        auto *root = view->rootObject();
        auto *image = findItem(root, "previewImage");
        auto *session = f.trim.session(f.id);
        // Until the first frame is decoded, the one frame under the Playhead is made by ffmpeg.
        auto *warmUp = findItem(root, "warmUpImage");
        QVERIFY(warmUp);
        // After warm-up the preview reads the in-memory cache: no ffmpeg run per frame.
        QTRY_VERIFY_WITH_TIMEOUT(session->frameCache().complete(), 20000);
        QTRY_VERIFY2(image->property("source").toString().startsWith(QString("image://trimcache/%1/0/c0/").arg(f.id)), qPrintable(image->property("source").toString()));
        QCOMPARE(image->property("status").toInt(), 1); // Image.Ready, synchronously
        QCOMPARE(warmUp->property("source").toString(), QString());
        const auto before = f.env.calls("ffmpeg").size();
        session->dragStart(1500.0); // frame 90
        QVERIFY(image->property("source").toString().contains("/c90/")); // at once, no waiting
        QCOMPARE(image->property("status").toInt(), 1);
        QVERIFY(image->property("paintedWidth").toInt() > 0);
        for (int ms = 0; ms < 1400; ms += 20) // a Handle drag: many frames
            session->dragStart(ms);
        QTest::qWait(300); // settled on an exact frame: nothing to refine
        QCOMPARE(f.env.calls("ffmpeg").size(), before);
    }

    void doneByEnterShowsTrimmingWithProgressAndCancelThenCancelReturns() {
        Fixture f;
        f.env.setMode("ffmpeg", "fake");
        f.env.setFixture("ffmpeg", "progress_us", "1000000");
        f.env.setFixture("ffmpeg", "step_delay", "30");
        QQuickView *view = f.open();
        auto *root = view->rootObject();
        auto *session = f.trim.session(f.id);
        session->dragStart(1000.0); // keeps 2 s
        QVERIFY(findItem(root, "filmstrip")->isVisible());
        QVERIFY(!findItem(root, "trimmingBar")->isVisible());
        QTest::keyClick(view, Qt::Key_Return);
        QTRY_VERIFY(session->trimming());
        auto *bar = findItem(root, "trimmingBar");
        QVERIFY(bar->isVisible());
        QVERIFY(!findItem(root, "filmstrip")->isVisible());
        QCOMPARE(findItem(root, "trimmingLabel")->property("text").toString(), QString("Trimming… 0%"));
        QCOMPARE(findItem(root, "doneButton")->parentItem()->opacity() < 1, true); // the top bar's buttons dim
        Fixture::click(view, findItem(root, "cancelButton"));
        QTRY_VERIFY(!session->trimming());
        QVERIFY(findItem(root, "filmstrip")->isVisible());
        QVERIFY(!bar->isVisible());
        QCOMPARE(session->startFrame(), 60);
    }

    void theTrimmingLabelFollowsTheProgress() {
        Fixture f;
        f.env.setMode("ffmpeg", "fake");
        f.env.setFixture("ffmpeg", "progress_us", "1240000"); // of a 2 s keep: 62%
        f.env.setFixture("ffmpeg", "step_delay", "1");
        QQuickView *view = f.open();
        auto *root = view->rootObject();
        auto *session = f.trim.session(f.id);
        session->dragStart(1000.0);
        session->done();
        QTRY_COMPARE_WITH_TIMEOUT(findItem(root, "trimmingLabel")->property("text").toString(), QString("Trimming… 62%"), 5000);
        auto *track = findItem(root, "progressTrack"), *fill = findItem(root, "progressFill");
        QCOMPARE(qRound(fill->width() / track->width() * 100), 62);
        session->cancelTrim();
        QTRY_VERIFY(!session->trimming());
    }

    void aWindowClosedByTheCompositorCancelsTheTrim() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        view->close(); // Hyprland's Super+Q
        QVERIFY(!f.trim.session(f.id));
        QVERIFY(!f.stack.isHidden(f.id));
        QTRY_VERIFY(!f.windows.viewFor(f.id));
    }

    void escClosesTheWindow() {
        Fixture f;
        QQuickView *view = f.open();
        QVERIFY(view);
        QTest::keyClick(view, Qt::Key_Escape);
        QVERIFY(!f.trim.session(f.id));
        QVERIFY(!view->isVisible() || !f.windows.viewFor(f.id));
        QVERIFY(!f.stack.isHidden(f.id));
    }

    void startHereAndEndHereButtonsMoveTheHandlesToThePlayhead() {
        Fixture f;
        QQuickView *view = f.open();
        auto *root = view->rootObject();
        auto *session = f.trim.session(f.id);
        session->scrub(1000.0);
        Fixture::click(view, findItem(root, "startHereButton"));
        QCOMPARE(session->startFrame(), 60);
        session->scrub(2000.0);
        Fixture::click(view, findItem(root, "endHereButton"));
        QCOMPARE(session->endFrame(), 120);
    }

    void theKeysDriveTheHandlesThePlayheadAndPlayback() {
        Fixture f;
        QQuickView *view = f.open();
        auto *session = f.trim.session(f.id);
        QTest::keyClick(view, Qt::Key_Right);
        QCOMPARE(session->playheadFrame(), 1);
        QTest::keyClick(view, Qt::Key_Right, Qt::ShiftModifier);
        QCOMPARE(session->playheadFrame(), 61);
        QTest::keyClick(view, Qt::Key_I);
        QCOMPARE(session->startFrame(), 61);
        QTest::keyClick(view, Qt::Key_Right, Qt::ShiftModifier);
        QTest::keyClick(view, Qt::Key_O);
        QCOMPARE(session->endFrame(), 121);
        QTest::keyClick(view, Qt::Key_Left);
        QCOMPARE(session->playheadFrame(), 120);
        QTest::keyClick(view, Qt::Key_Home);
        QCOMPARE(session->playheadFrame(), 61);
        QTest::keyClick(view, Qt::Key_End);
        QCOMPARE(session->playheadFrame(), 121);
        QTest::keyClick(view, Qt::Key_Space);
        QVERIFY(session->playing());
        QTest::keyClick(view, Qt::Key_Space);
        QVERIFY(!session->playing());
    }

    void thePlayAndTransportButtonsWork() {
        Fixture f;
        QQuickView *view = f.open();
        auto *root = view->rootObject();
        auto *session = f.trim.session(f.id);
        Fixture::click(view, findItem(root, "playButton"));
        QVERIFY(session->playing());
        Fixture::click(view, findItem(root, "playButton"));
        QVERIFY(!session->playing());
        Fixture::click(view, findItem(root, "toEndButton"));
        QCOMPARE(session->playheadFrame(), session->endFrame());
        Fixture::click(view, findItem(root, "toStartButton"));
        QCOMPARE(session->playheadFrame(), 0);
    }

    void pressingAndDraggingOnTheFilmstripScrubsAndThePillFollows() {
        Fixture f;
        QQuickView *view = f.open();
        auto *root = view->rootObject();
        auto *session = f.trim.session(f.id);
        auto *area = findItem(root, "stripArea");
        const QPointF origin = area->mapToScene(QPointF(0, 0));
        const double sx = root->property("sx").toDouble(), sw = root->property("sw").toDouble();
        const double y = origin.y() + 34;
        QTest::mousePress(view, Qt::LeftButton, {}, QPoint(int(sx + sw / 2), int(y)));
        QCOMPARE(session->playheadFrame(), 90);
        QTest::mouseMove(view, QPoint(int(sx + sw * 0.75), int(y)));
        QCOMPARE(session->playheadFrame(), 135);
        auto *pill = findItem(root, "timePill");
        QVERIFY(pill->isVisible());
        QCOMPARE(pill->findChild<QQuickItem *>()->property("text").toString(), session->timeText(135));
        QTest::mouseRelease(view, Qt::LeftButton, {}, QPoint(int(sx + sw * 0.75), int(y)));
    }

    // ── From the Recording's Thumbnail ───────────────────────────────────────────────

    void aRecordingsThumbnailHasATrimButtonAndClickingItOpensTrim() {
        Fixture f;
        tarn::ThumbnailSurface surface(f.stack, f.env.daemon().service<tarn::Palette>());
        QVERIFY(QTest::qWaitForWindowExposed(&surface.view()));
        auto *card = findItem(surface.view().rootObject(), "card");
        QTRY_VERIFY(card);
        QTRY_COMPARE_WITH_TIMEOUT(card->x(), 16.0, 1500); // done springing in
        QTest::mouseMove(&surface.view(), card->mapToScene(QPointF(card->width() / 2, card->height() / 2)).toPoint());
        auto *button = findItem(card, "editButton");
        QTRY_VERIFY(button->isVisible());
        QTest::mouseClick(&surface.view(), Qt::LeftButton, {}, Fixture::centerOf(button));
        QTRY_VERIFY(f.trim.session(f.id));
        QVERIFY(f.stack.isHidden(f.id));
    }
};

QTEST_MAIN(TrimUiTest)
#include "trim_ui_test.moc"
